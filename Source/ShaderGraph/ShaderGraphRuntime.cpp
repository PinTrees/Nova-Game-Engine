#include "pch.h"
#include "ShaderGraphRuntime.h"
#include "CustomShaders.h"
#include "ShaderCache.h"
#include "UMaterial.h"
#include "Effects.h"
#include "RenderLayers.h"
#include "SpriteBatch.h"
#include "UnityGUI.h"
#include "EngineTime.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <chrono>
#include <map>
#include <thread>
#include <atomic>
#include <unordered_map>

namespace fs = std::filesystem;

namespace ShaderGraph
{
	namespace
	{
		constexpr const char* kOwnerPrefix = "shadergraph:";

		// 만든 그래프 셰이더 하나
		struct Compiled
		{
			std::string Name, Asset, Owner;
			uint64 Generation = 0;
			Graph G;
			InstancedBasicEffect* Fx = nullptr;
			FxTechnique* Batch = nullptr;
			FxTechnique* Skinned = nullptr;
			FxTechnique* DepthBatch = nullptr;      // 잘라내기 (Alpha Clipping) 만
			FxTechnique* DepthSkinned = nullptr;
			FxTechnique* ShadowBatch = nullptr;
			FxTechnique* ShadowSkinned = nullptr;
			FxVar* Time = nullptr;
			FxVar* ViewProjTex = nullptr;
			FxVar* View = nullptr;
			FxVar* ShadowLight = nullptr;
			FxVar* ShadowBias = nullptr;
			FxVar* White = nullptr;         // 비어 있는 Texture2D 입력 (Sub Graph · Custom Function)
			std::vector<FxVar*> PropVars;   // G.Properties 순서
			std::vector<std::pair<FxVar*, ComPtr<GfxShaderResourceView>>> NodeTextures;   // Sample Texture 2D 의 option texture
		};
		std::map<std::string, std::unique_ptr<Compiled>> s_Graphs;
		std::map<std::string, std::string> s_Errors;
		uint64 s_Generation = 0;

		// 만드는 중 (백그라운드 컴파일): 셰이더 이름 → 그 그래프 · .fx
		struct Building
		{
			std::string Asset;
			std::wstring FxPath;
			Graph G;
		};
		std::map<std::string, Building> s_Building;

		// 그래프가 쓰는 파일 (Sub Graph · Custom Function .hlsl): 바뀌면 다시 만든다 (성공 · 실패 모두 — 고치면 다시)
		struct Deps
		{
			std::string Asset;
			std::vector<std::pair<std::string, long long>> Files;   // 경로, 파일 시각
		};
		std::map<std::string, Deps> s_Deps;
		std::chrono::steady_clock::time_point s_DepsCheck;

		long long FileStamp(const std::string& asset)
		{
			std::error_code ec;
			const auto t = fs::last_write_time(FullPath(asset), ec);
			return ec ? -1 : (long long)t.time_since_epoch().count();
		}

		void RecordDeps(const std::string& name, const std::string& asset, const std::vector<std::string>& files)
		{
			Deps d;
			d.Asset = asset;
			for (const std::string& f : files)
				d.Files.push_back({ f, FileStamp(f) });
			s_Deps[name] = std::move(d);
		}

		// 재질마다 해석한 값 (재질 Properties 가 바뀌거나 그래프를 다시 만들면 다시)
		struct Parsed
		{
			uint64 Revision = 0, Generation = 0;
			std::vector<XMFLOAT4> Values;
			std::vector<ComPtr<GfxShaderResourceView>> Textures;
		};
		std::unordered_map<const UMaterial*, Parsed> s_Cache;

		// 그래프 파일 목록 (Shader 목록은 Inspector 가 매 프레임 묻는다 → 몇 초에 한 번만 디스크를 훑는다)
		struct AssetEntry { std::string Asset, Name; };
		std::vector<AssetEntry> s_Assets;
		std::chrono::steady_clock::time_point s_AssetsTime;
		bool s_AssetsValid = false;

		const auto s_Start = std::chrono::steady_clock::now();
		// Time 노드: 프레임마다 한 번 정한 값 (깊이 프리패스와 본 패스가 같은 시각이어야 정점이 같은 자리 — 다르면 EQUAL 깊이가 어긋나 검게 빈다)
		float s_FrameTime = 0.0f;

		std::string Lower(std::string s)
		{
			for (char& c : s) c = (char)tolower((unsigned char)c);
			return s;
		}

		std::string Utf8(const std::wstring& w) { return wstring_to_string(w); }

		std::string ReadFile(const std::wstring& path)
		{
			std::ifstream in(path, std::ios::binary);
			if (!in) return std::string();
			std::ostringstream ss;
			ss << in.rdbuf();
			return ss.str();
		}

		std::string NormAsset(const std::string& a)
		{
			std::string s = Lower(a);
			for (char& c : s) if (c == '/') c = '\\';
			return s;
		}

		// 그래프 파일의 셰이더 이름 (경로 설정 + 파일 이름)
		std::string NameFromFile(const std::string& asset)
		{
			std::string path = kDefaultPath;
			const std::string text = ReadFile(FullPath(asset));
			const nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
			if (j.is_object() && j.contains("path") && j["path"].is_string() && !j["path"].get<std::string>().empty())
				path = j["path"].get<std::string>();
			while (!path.empty() && (path.back() == '/' || path.back() == ' ')) path.pop_back();
			return path + "/" + Utf8(fs::path(string_to_wstring(asset)).stem().wstring());
		}

		XMFLOAT4 ValueOf(const Property& p, const nlohmann::json& props)
		{
			XMFLOAT4 v(p.Value[0], p.Value[1], p.Value[2], p.Value[3]);
			if (!props.contains(p.Ref))
				return v;
			const nlohmann::json& j = props[p.Ref];
			float* f = &v.x;
			if (j.is_number())
				f[0] = j.get<float>();
			else if (j.is_array())
				for (int i = 0; i < 4 && i < (int)j.size(); ++i)
					if (j[i].is_number()) f[i] = j[i].get<float>();
			return v;
		}

		std::string TexturePathOf(const Property& p, const nlohmann::json& props)
		{
			if (props.contains(p.Ref) && props[p.Ref].is_string())
				return props[p.Ref].get<std::string>();
			return p.Texture;
		}

		ComPtr<GfxShaderResourceView> LoadTex(const std::string& path)
		{
			return path.empty() ? nullptr : ResourceManager::GetI()->LoadTexture(string_to_wstring(path));
		}

		const Parsed& Values(const Compiled& c, const UMaterial& m)
		{
			Parsed& p = s_Cache[&m];
			if (p.Revision == m.PropertiesRevision() && p.Generation == c.Generation)
				return p;
			p.Revision = m.PropertiesRevision();
			p.Generation = c.Generation;
			p.Values.clear();
			p.Textures.clear();
			const nlohmann::json& props = m.Properties();
			for (const Property& prop : c.G.Properties)
			{
				p.Values.push_back(ValueOf(prop, props));
				p.Textures.push_back(prop.Type == "Texture2D" ? LoadTex(TexturePathOf(prop, props)) : nullptr);
			}
			return p;
		}

		// 그래프 값 → 이펙트 (재질의 엔진 값 · 시간 · 속성 · 그림) 후 패스
		void Bind(const Compiled& c, UMaterial* m, GfxContext* dc, FxTechnique* tech)
		{
			m->Apply(c.Fx);
			const Parsed& p = Values(c, *m);
			for (size_t i = 0; i < c.PropVars.size() && i < p.Values.size(); ++i)
			{
				FxVar* v = c.PropVars[i];
				if (!v) continue;
				if (c.G.Properties[i].Type == "Texture2D")
					v->SetResource(p.Textures[i] ? p.Textures[i].Get() : SpriteBatch::WhiteTexture());   // Unity: 비어 있으면 흰색
				else
					v->SetFloatVector(&p.Values[i].x);
			}
			for (const auto& [var, srv] : c.NodeTextures)
				var->SetResource(srv ? srv.Get() : SpriteBatch::WhiteTexture());
			if (c.White)
				c.White->SetResource(SpriteBatch::WhiteTexture());
			if (c.Time)
			{
				const float t = s_FrameTime;
				const float tv[4] = { t, sinf(t), cosf(t), Time::DeltaTime() };
				c.Time->SetFloatVector(tv);
			}
			tech->GetPassByIndex(0)->Apply(0, dc);
		}

		void SetMatrixVar(FxVar* v, CXMMATRIX m)
		{
			if (v) v->SetMatrix(reinterpret_cast<const float*>(&m));
		}

		// 그림자 패스: 엔진 그림자 셰이더와 같은 빛 · 바이어스
		void SetShadowVars(const Compiled& c)
		{
			const CustomShaders::ShadowCaster& s = CustomShaders::CurrentShadow();
			if (c.ShadowLight) c.ShadowLight->SetFloatVector(&s.Light.x);
			const float bias[4] = { s.Bias[0], s.Bias[1], 0, 0 };
			if (c.ShadowBias) c.ShadowBias->SetFloatVector(bias);
		}

		const XMMATRIX& ToTex()
		{
			static const XMMATRIX toTex(0.5f, 0.0f, 0.0f, 0.0f, 0.0f, -0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.5f, 0.5f, 0.0f, 1.0f);
			return toTex;
		}

		Compiled* Current(const std::string& name)
		{
			auto it = s_Graphs.find(name);
			return it != s_Graphs.end() ? it->second.get() : nullptr;
		}

		// ---- 재질 Inspector: 그래프 속성 (Blackboard 순서)
		bool Inspector(const std::string& name, UMaterial& m)
		{
			Compiled* c = Current(name);
			if (!c)
			{
				UnityGUI::HelpBox(("The shader graph failed to build: " + LastError(name)).c_str(), true);
				return false;
			}
			nlohmann::json& props = m.Properties();
			bool changed = false;
			for (const Property& p : c->G.Properties)
			{
				ImGui::PushID(p.Ref.c_str());
				const char* label = p.Name.c_str();
				XMFLOAT4 v = ValueOf(p, props);
				float* f = &v.x;
				if (p.Type == "Float")
				{
					if (p.Range ? UnityGUI::Slider(label, f, p.Min, p.Max) : UnityGUI::Float(label, f)) { props[p.Ref] = f[0]; changed = true; }
				}
				else if (p.Type == "Color")
				{
					if (UnityGUI::Color(label, f)) { props[p.Ref] = { f[0], f[1], f[2], f[3] }; changed = true; }
				}
				else if (p.Type == "Vector2")
				{
					if (UnityGUI::Vector2Pair(label, "X", &f[0], "Y", &f[1])) { props[p.Ref] = { f[0], f[1] }; changed = true; }
				}
				else if (p.Type == "Vector3")
				{
					if (UnityGUI::Vector3(label, f)) { props[p.Ref] = { f[0], f[1], f[2] }; changed = true; }
				}
				else if (p.Type == "Vector4")
				{
					UnityGUI::FieldRow row = UnityGUI::BeginFieldRow(label);
					ImGui::SetCursorScreenPos(ImVec2(row.fieldX, row.p.y));
					ImGui::SetNextItemWidth(row.fieldW);
					if (ImGui::DragFloat4("##v4", f, 0.01f)) { props[p.Ref] = { f[0], f[1], f[2], f[3] }; changed = true; }
					UnityGUI::EndFieldRow(row);
				}
				else if (p.Type == "Texture2D")
				{
					std::string path = TexturePathOf(p, props);
					if (UnityGUI::TextField(label, &path)) { props[p.Ref] = path; changed = true; }
					if (ImGui::BeginDragDropTarget())
					{
						const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_FILE");
						if (!payload) payload = ImGui::AcceptDragDropPayload("PNG_FILE");
						if (payload)
						{
							std::string dropped(static_cast<const char*>(payload->Data));
							const std::string root = Utf8(PathManager::GetI()->GetContentPathW());
							if (_strnicmp(dropped.c_str(), root.c_str(), root.size()) == 0)
								dropped = dropped.substr(root.size());
							props[p.Ref] = dropped;
							changed = true;
						}
						ImGui::EndDragDropTarget();
					}
				}
				ImGui::PopID();
			}
			if (c->G.Properties.empty())
				UnityGUI::HelpBox("This graph has no Blackboard properties. Add some in the Shader Graph window to set them per material.", false);
			return changed;
		}

		// ---- 백그라운드 컴파일 작업 (.fx 하나)
		struct Job
		{
			std::wstring FxPath;
			std::string Content;              // 시작할 때의 파일 내용 (끝났을 때 바뀌었으면 다시)
			std::thread Thread;
			std::atomic<bool> Done{ false };
			HRESULT Hr = S_OK;
			~Job() { if (Thread.joinable()) Thread.join(); }   // 끝낼 때 진행 중인 컴파일 (joinable thread 를 지우면 프로그램이 멈춘다)
		};
		std::map<std::wstring, std::unique_ptr<Job>> s_Jobs;

		void StartJob(const std::wstring& fxPath)
		{
			auto job = std::make_unique<Job>();
			job->FxPath = fxPath;
			job->Content = ReadFile(fxPath);
			Job* j = job.get();
			j->Thread = std::thread([j]() {
				ComPtr<ID3DBlob> blob, msgs;
				j->Hr = ShaderCache::CompileEffect(j->FxPath, ShaderCache::DefaultFlags(), blob, msgs);
				j->Done = true;
			});
			s_Jobs[fxPath] = std::move(job);
		}

		// 만드는 중인 셰이더 하나를 끝까지 (컴파일이 끝났으면 이펙트 · 등록). wait = 컴파일을 기다린다
		//  돌려주는 값: 0 진행 중, 1 등록함, -1 실패
		int FinishBuilding(const std::string& name, bool wait)
		{
			auto it = s_Building.find(name);
			if (it == s_Building.end())
				return -1;
			Building& b = it->second;
			std::string error;
			int state = CompileInBackground(b.FxPath, error);
			while (wait && state == 0)
			{
				if (auto j = s_Jobs.find(b.FxPath); j != s_Jobs.end() && j->second->Thread.joinable())
					j->second->Thread.join();
				state = CompileInBackground(b.FxPath, error);
			}
			if (state == 0)
				return 0;
			if (state < 0)
			{
				s_Errors[name] = error.empty() ? "compile failed" : error;
				EditorLog::Write("ShaderGraph", "%s: shader compile failed\n%s", name.c_str(), s_Errors[name].c_str());
				s_Building.erase(it);
				return -1;
			}

			// 이펙트 (캐시 적중) → 새 셰이더로 바꿔 등록 (예전 이펙트는 그 뒤에 내린다)
			auto c = std::make_unique<Compiled>();
			c->Name = name;
			c->Asset = b.Asset;
			c->G = b.G;
			c->Generation = ++s_Generation;
			c->Owner = kOwnerPrefix + name + "#" + std::to_string(c->Generation);
			c->Fx = CustomShaders::LoadEffect(c->Owner, b.FxPath, error);
			s_Building.erase(it);
			if (!c->Fx)
			{
				const std::string msg = ShaderCache::LastMessages(fs::path(b.FxPath).lexically_normal().wstring());
				s_Errors[name] = msg.empty() ? error : msg;
				return -1;
			}
			FxEffect* fx = c->Fx->GetFX();
			auto tech = [&](const char* n) -> FxTechnique* {
				FxTechnique* t = fx->GetTechniqueByName(n);
				return t && t->IsValid() ? t : nullptr;
			};
			auto var = [&](const std::string& n) -> FxVar* {
				FxVar* v = fx->GetVariableByName(n.c_str());
				return v && v->IsValid() ? v : nullptr;
			};
			c->Batch = tech("GraphBatchTech");
			c->Skinned = tech("GraphSkinnedTech");
			c->DepthBatch = tech("GraphDepthBatchTech");
			c->DepthSkinned = tech("GraphDepthSkinnedTech");
			c->ShadowBatch = tech("GraphShadowBatchTech");
			c->ShadowSkinned = tech("GraphShadowSkinnedTech");
			if (!c->Batch)
			{
				s_Errors[name] = "the generated shader has no GraphBatchTech";
				CustomShaders::UnregisterOwner(c->Owner);
				return -1;
			}
			c->Time = var("gSGTime");
			c->ViewProjTex = var("gViewProjTex");
			c->View = var("gSGView");
			c->ShadowLight = var("gSGShadowLight");
			c->ShadowBias = var("gSGShadowBias");
			c->White = var("gSG_White");
			for (const Property& p : c->G.Properties)
				c->PropVars.push_back(var("gSG_" + Sanitize(p.Ref)));
			for (const Node& n : c->G.Nodes)
			{
				const std::string tex = n.Options.value("texture", std::string());
				if (n.Type == "Sample Texture 2D" && !tex.empty())
					if (FxVar* v = var("gSG_NodeTex" + std::to_string(n.Id)))
						c->NodeTextures.push_back({ v, LoadTex(tex) });
			}
			s_Errors.erase(name);

			CustomShaders::Shader shader;
			shader.Name = name;
			shader.Owner = c->Owner;
			shader.DrawInstanced = [name](CustomShaders::InstancedDraw& d) {
				Compiled* c = Current(name);
				if (!c) return;
				c->Fx->SetViewProj(d.ViewProj);
				c->Fx->SetTexTransform(XMMatrixIdentity());
				FxTechnique* tech = c->Batch;
				switch (d.Pass)
				{
				case CustomShaders::DrawPass::NormalDepth:
					SetMatrixVar(c->View, d.View);
					tech = c->DepthBatch;
					break;
				case CustomShaders::DrawPass::Shadow:
					SetShadowVars(*c);
					tech = c->ShadowBatch;
					break;
				default:
				{
					const XMMATRIX vpt = d.ViewProj * ToTex();
					SetMatrixVar(c->ViewProjTex, vpt);
					RenderLayers::SetObjectLayer(c->Fx, d.LayerBit);
					break;
				}
				}
				if (!tech) return;
				Bind(*c, d.Material, d.Context, tech);
				d.Draw();
			};
			if (c->Skinned)
				shader.DrawSkinned = [name](CustomShaders::SkinnedDraw& d) {
					Compiled* c = Current(name);
					if (!c) return;
					// 엔진 스킨 패스와 같은 행렬 · 본 (lilToon 과 같은 방법). 물체 레이어 비트는 SkinnedMeshRenderer 가 이미 넣었다
					XMMATRIX w = d.World;
					w.r[3] = XMVectorSet(0, 0, 0, 1);
					XMVECTOR det;
					const XMMATRIX invT = XMMatrixTranspose(XMMatrixInverse(&det, w));
					const XMMATRIX wvp = d.World * d.ViewProj;
					c->Fx->SetWorld(d.World);
					c->Fx->SetWorldInvTranspose(invT);
					c->Fx->SetViewProj(d.ViewProj);
					c->Fx->SetWorldViewProj(wvp);
					c->Fx->SetWorldViewProjTex(wvp * ToTex());
					c->Fx->SetTexTransform(XMMatrixIdentity());
					if (d.Bones && d.BoneCount > 0)
						c->Fx->SetBoneTransforms(d.Bones, d.BoneCount);
					FxTechnique* tech = c->Skinned;
					if (d.Pass == CustomShaders::DrawPass::NormalDepth)
					{
						SetMatrixVar(c->View, d.View);
						tech = c->DepthSkinned;
					}
					else if (d.Pass == CustomShaders::DrawPass::Shadow)
					{
						SetShadowVars(*c);
						tech = c->ShadowSkinned;
					}
					if (!tech) return;
					Bind(*c, d.Material, d.Context, tech);
					d.Draw();
				};
			shader.CustomDepth = [name](const UMaterial&) {
				const Compiled* c = Current(name);
				return c && c->DepthBatch && c->ShadowBatch;   // 잘라내기 또는 정점 이동 (생성기가 그때만 깊이 · 그림자 기법을 만든다)
			};
			shader.Transparent = [name](const UMaterial&) {
				const Compiled* c = Current(name);
				return c && c->G.Surface == "Transparent";
			};
			shader.Inspector = [name](UMaterial& m) { return Inspector(name, m); };
			shader.DefaultProperties = [name]() {
				Compiled* c = Current(name);
				return c ? DefaultProperties(c->G) : nlohmann::json::object();
			};
			std::string oldOwner;
			if (Compiled* old = Current(name))
				oldOwner = old->Owner;
			s_Graphs[name] = std::move(c);
			CustomShaders::Register(shader);   // 같은 이름을 바꾼다
			if (!oldOwner.empty())
				CustomShaders::UnregisterOwner(oldOwner);   // 예전 이펙트 (셰이더 등록은 위에서 새것으로 바뀜 — Owner 가 달라 지워지지 않는다)
			EditorLog::Write("ShaderGraph", "built %s (%s)", name.c_str(), s_Graphs[name]->Asset.c_str());
			return 1;
		}

		// 그래프를 읽어 .fx 를 쓰고 백그라운드 컴파일을 시작한다
		bool StartBuilding(const std::string& name, const std::string& asset, std::string& error)
		{
			Building b;
			b.Asset = asset;
			if (!b.G.Load(FullPath(asset), error))
			{
				s_Errors[name] = error;
				return false;
			}
			const CodeResult code = Generate(b.G);
			RecordDeps(name, asset, code.Files);
			if (!code.Error.empty())
			{
				error = code.Error;
				s_Errors[name] = error;
				EditorLog::Write("ShaderGraph", "%s: %s", name.c_str(), error.c_str());
				return false;
			}
			b.FxPath = GeneratedPath(name, asset);
			if (!WriteIfChanged(b.FxPath, code.Hlsl))
			{
				error = "cannot write " + Utf8(b.FxPath);
				s_Errors[name] = error;
				return false;
			}
			std::string jobError;
			CompileInBackground(b.FxPath, jobError);   // 시작 (이미 캐시에 있으면 곧 끝난다)
			s_Building[name] = std::move(b);
			return true;
		}

		bool Create(const std::string& name)
		{
			if (s_Building.count(name))
				return FinishBuilding(name, false) == 1;
			const std::string asset = FindGraphAsset(name);
			if (asset.empty())
				return false;
			std::string error;
			if (!StartBuilding(name, asset, error))
				return false;
			return FinishBuilding(name, false) == 1;   // 캐시에 있으면 바로
		}

		void ScanAssets()
		{
			std::vector<AssetEntry> found;
			const fs::path root = PathManager::GetI()->GetContentPathW();
			std::error_code ec;
			for (fs::recursive_directory_iterator it(root / L"Assets", fs::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec))
			{
				if (ec) break;
				if (!it->is_regular_file(ec) || Lower(it->path().extension().string()) != kExtension)
					continue;
				const std::string asset = Utf8(fs::relative(it->path(), root, ec).wstring());
				found.push_back({ asset, NameFromFile(asset) });
			}
			std::sort(found.begin(), found.end(), [](const AssetEntry& a, const AssetEntry& b) { return a.Asset < b.Asset; });
			// 목록이 바뀌면 (새 그래프 · 이름 바꿈) 예전에 못 찾은 이름을 다시 찾게
			bool changed = found.size() != s_Assets.size();
			for (size_t i = 0; !changed && i < found.size(); ++i)
				changed = found[i].Asset != s_Assets[i].Asset || found[i].Name != s_Assets[i].Name;
			s_Assets = std::move(found);
			if (changed)
				for (const AssetEntry& e : s_Assets)
					CustomShaders::Forget(e.Name);
			s_AssetsTime = std::chrono::steady_clock::now();
			s_AssetsValid = true;
		}

		const std::vector<AssetEntry>& Entries(bool refresh)
		{
			if (refresh || !s_AssetsValid || std::chrono::steady_clock::now() - s_AssetsTime > std::chrono::seconds(3))
				ScanAssets();
			return s_Assets;
		}
	}

	int CompileInBackground(const std::wstring& fxPath, std::string& error)
	{
		auto it = s_Jobs.find(fxPath);
		if (it == s_Jobs.end())
		{
			StartJob(fxPath);
			return 0;
		}
		Job& j = *it->second;
		if (!j.Done)
			return 0;
		if (j.Thread.joinable())
			j.Thread.join();
		const HRESULT hr = j.Hr;
		const bool stale = ReadFile(fxPath) != j.Content;
		s_Jobs.erase(it);
		if (stale)
		{
			StartJob(fxPath);   // 컴파일하는 동안 파일이 바뀌었다
			return 0;
		}
		if (FAILED(hr))
		{
			error = ShaderCache::LastMessages(fs::path(fxPath).lexically_normal().wstring());
			if (error.empty()) error = "compile failed";
			return -1;
		}
		return 1;
	}

	bool WriteIfChanged(const std::wstring& path, const std::string& text)
	{
		if (ReadFile(path) == text)
			return true;
		std::error_code ec;
		fs::create_directories(fs::path(path).parent_path(), ec);
		std::ofstream os(path, std::ios::binary | std::ios::trunc);
		if (!os)
			return false;
		os << text;
		return true;
	}

	std::wstring FullPath(const std::string& assetPath)
	{
		if (fs::path(string_to_wstring(assetPath)).is_absolute())
			return string_to_wstring(assetPath);
		return PathManager::GetI()->GetMovePathW(string_to_wstring(assetPath));
	}

	std::string ShaderNameOf(const std::string& assetPath)
	{
		const std::string key = NormAsset(assetPath);
		for (const AssetEntry& e : s_Assets)
			if (NormAsset(e.Asset) == key)
				return e.Name;
		return NameFromFile(assetPath);
	}

	std::vector<std::string> GraphAssets(bool refresh)
	{
		std::vector<std::string> out;
		for (const AssetEntry& e : Entries(refresh))
			out.push_back(e.Asset);
		return out;
	}

	std::string FindGraphAsset(const std::string& shaderName)
	{
		for (int pass = 0; pass < 2; ++pass)
		{
			std::vector<std::string> matches;
			for (const AssetEntry& e : Entries(pass == 1))
				if (e.Name == shaderName)
					matches.push_back(e.Asset);
			if (matches.size() == 1)
				return matches[0];
			if (matches.size() > 1)
			{
				// Unity 처럼 같은 이름이 둘이면 어느 쪽인지 알 수 없다 → 경로 (Blackboard) 를 바꾸라고 알린다
				std::string list;
				for (const std::string& m : matches) list += (list.empty() ? "" : ", ") + m;
				s_Errors[shaderName] = "two shader graphs are named '" + shaderName + "' (" + list + ") - change the path of one in its Blackboard";
				return std::string();
			}
		}
		if (!s_Errors.count(shaderName))
			s_Errors[shaderName] = "no shader graph named '" + shaderName + "' under Assets";
		return std::string();
	}

	bool IsGraphShader(const std::string& shaderName)
	{
		if (s_Graphs.count(shaderName) || s_Building.count(shaderName))
			return true;
		for (const AssetEntry& e : Entries(false))
			if (e.Name == shaderName)
				return true;
		return false;
	}

	std::wstring GeneratedPath(const std::string& shaderName, const std::string& assetPath)
	{
		// 이름 + 그래프 경로 해시 (다른 폴더의 같은 이름이 같은 파일을 쓰지 않게)
		char hash[16];
		snprintf(hash, sizeof(hash), "%08x", (unsigned)(std::hash<std::string>()(NormAsset(assetPath)) & 0xFFFFFFFFu));
		const std::string stem = Sanitize(shaderName) + "_" + hash;
		return (fs::path(PathManager::GetI()->GetContentPathW()) / L"Library" / L"ShaderGraph" / string_to_wstring(stem + ".fx")).lexically_normal().wstring();
	}

	std::string LastError(const std::string& shaderName)
	{
		auto it = s_Errors.find(shaderName);
		return it != s_Errors.end() ? it->second : std::string();
	}

	bool IsCompiling(const std::string& shaderName)
	{
		return s_Building.count(shaderName) != 0;
	}

	bool Reload(const std::string& assetPath, std::string& error, bool wait)
	{
		Entries(true);
		const std::string name = ShaderNameOf(assetPath);
		CustomShaders::Forget(name);
		s_Errors.erase(name);
		// 같은 셰이더 이름의 그래프가 둘이면 (다른 폴더의 같은 파일 이름) 재질이 어느 쪽인지 알 수 없다 → 경로를 바꾸라고
		if (FindGraphAsset(name).empty())
		{
			error = LastError(name);
			return false;
		}
		if (!StartBuilding(name, assetPath, error))
			return false;
		const int state = FinishBuilding(name, wait);
		if (state < 0)
		{
			error = LastError(name);
			return false;
		}
		return true;
	}

	bool RebuildUsers(const std::string& subGraphAsset, std::string& error)
	{
		// 그 Sub Graph 를 (직접 · 간접으로) 쓰는 그래프: 지난번에 만들 때 기록한 파일 목록 + 아직 안 만든 그래프는 파일 내용으로
		const std::string key = NormAsset(subGraphAsset);
		std::vector<std::string> users;
		for (const std::string& asset : GraphAssets(true))
		{
			const std::string name = ShaderNameOf(asset);
			bool uses = false;
			if (auto d = s_Deps.find(name); d != s_Deps.end())
				for (const auto& f : d->second.Files) uses |= NormAsset(f.first) == key;
			if (!uses)
			{
				std::string text = Lower(ReadFile(FullPath(asset)));
				for (char& c : text) if (c == '/') c = '\\';
				std::string k = key;
				std::string escaped;
				for (char c : k) { escaped += c; if (c == '\\') escaped += '\\'; }   // JSON 안의 \ 는 \\
				uses = text.find(k) != std::string::npos || text.find(escaped) != std::string::npos;
			}
			if (uses)
				users.push_back(asset);
		}
		bool ok = true;
		for (const std::string& asset : users)
		{
			std::string e;
			if (!Reload(asset, e, true))
			{
				ok = false;
				error += (error.empty() ? "" : "\n") + asset + ": " + e;
			}
		}
		return ok;
	}

	std::vector<std::string> SubGraphAssets()
	{
		std::vector<std::string> out;
		const fs::path root = PathManager::GetI()->GetContentPathW();
		std::error_code ec;
		for (fs::recursive_directory_iterator it(root / L"Assets", fs::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec))
		{
			if (ec) break;
			if (it->is_regular_file(ec) && Lower(it->path().extension().string()) == kSubExtension)
				out.push_back(Utf8(fs::relative(it->path(), root, ec).wstring()));
		}
		std::sort(out.begin(), out.end());
		return out;
	}

	void UpdateRuntime()
	{
		s_FrameTime = std::chrono::duration<float>(std::chrono::steady_clock::now() - s_Start).count();
		// 쓰는 Sub Graph · .hlsl 이 바뀌었으면 다시 만든다 (1 초에 한 번 확인)
		if (!s_Deps.empty() && std::chrono::steady_clock::now() - s_DepsCheck > std::chrono::seconds(1))
		{
			s_DepsCheck = std::chrono::steady_clock::now();
			std::vector<std::pair<std::string, std::string>> changed;
			for (const auto& [name, d] : s_Deps)
			{
				if (s_Building.count(name))
					continue;
				for (const auto& [file, stamp] : d.Files)
					if (FileStamp(file) != stamp) { changed.push_back({ name, d.Asset }); break; }
			}
			for (const auto& [name, asset] : changed)
			{
				std::string error;
				EditorLog::Write("ShaderGraph", "%s: a sub graph / .hlsl it uses changed - rebuilding", name.c_str());
				if (!Reload(asset, error, false))
					RecordDeps(name, asset, s_Deps[name].Files.empty() ? std::vector<std::string>() : [&] { std::vector<std::string> f; for (const auto& x : s_Deps[name].Files) f.push_back(x.first); return f; }());
			}
		}
		if (s_Building.empty())
			return;
		std::vector<std::string> names;
		for (const auto& [name, b] : s_Building) names.push_back(name);
		for (const std::string& name : names)
			FinishBuilding(name, false);
	}

	nlohmann::json DefaultProperties(const Graph& g)
	{
		nlohmann::json j = nlohmann::json::object();
		for (const Property& p : g.Properties)
		{
			const int w = p.Width();
			if (p.Type == "Texture2D") j[p.Ref] = p.Texture;
			else if (w == 1) j[p.Ref] = p.Value[0];
			else
			{
				nlohmann::json a = nlohmann::json::array();
				for (int i = 0; i < w; ++i) a.push_back(p.Value[i]);
				j[p.Ref] = a;
			}
		}
		return j;
	}

	std::string MakeMaterial(const std::string& graphAsset, const std::string& matPath, std::string& error)
	{
		const std::string name = ShaderNameOf(graphAsset);
		if (s_Building.count(name))
			FinishBuilding(name, true);
		const CustomShaders::Shader* cs = CustomShaders::Find(name);
		if (!cs && s_Building.count(name) && FinishBuilding(name, true) == 1)
			cs = CustomShaders::Find(name);
		if (!cs)
		{
			error = "the graph does not build: " + LastError(name);
			return std::string();
		}
		fs::path target = matPath.empty()
			? fs::path(FullPath(graphAsset)).replace_extension(L".mat")
			: fs::path(FullPath(matPath));
		std::error_code ec;
		if (matPath.empty())
			for (int i = 1; fs::exists(target, ec); ++i)
				target = fs::path(FullPath(graphAsset)).parent_path() / (fs::path(FullPath(graphAsset)).stem().wstring() + L" " + std::to_wstring(i) + L".mat");
		fs::create_directories(target.parent_path(), ec);
		// 엔진이 기본 재질을 만들고 (New Material N.mat) 이름을 바꾼다
		const std::string created = UMaterial::Create(Utf8(target.parent_path().wstring()));
		const fs::path createdFull = FullPath(created);
		if (createdFull != target)
		{
			fs::remove(target, ec);
			fs::rename(createdFull, target, ec);
			if (ec) { error = "cannot rename the new material: " + ec.message(); return std::string(); }
		}
		const std::string rel = Utf8(PathManager::GetI()->GetCutSolutionPath(target.wstring()));
		std::unique_ptr<UMaterial> m(UMaterial::Load(rel));
		if (!m) { error = "cannot load " + rel; return std::string(); }
		m->SetShaderName(name, cs->DefaultProperties ? cs->DefaultProperties() : nlohmann::json::object());
		UMaterial::Save(m.get());
		return rel;
	}

	void InitRuntime()
	{
		CustomShaders::Provider p;
		p.Prefix = "";   // 이름 앞부분이 그래프마다 다르다 (Blackboard 경로) — 프로젝트의 그래프 목록에서 찾는다
		p.Owner = "shadergraph";
		p.Create = [](const std::string& name) { return Create(name); };
		p.Pending = [](const std::string& name) { return s_Building.count(name) != 0; };
		p.List = []() {
			std::vector<std::string> out;
			for (const AssetEntry& e : Entries(false))
				out.push_back(e.Name);
			return out;
		};
		CustomShaders::RegisterProvider(p);
	}
}
