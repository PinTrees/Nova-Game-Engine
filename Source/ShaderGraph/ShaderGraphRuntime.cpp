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
			std::string Name, Asset;
			uint64 Generation = 0;
			Graph G;
			InstancedBasicEffect* Fx = nullptr;
			FxTechnique* Batch = nullptr;
			FxTechnique* Skinned = nullptr;
			FxVar* Time = nullptr;
			FxVar* ViewProjTex = nullptr;
			std::vector<FxVar*> PropVars;   // G.Properties 순서
			std::vector<std::pair<FxVar*, ComPtr<GfxShaderResourceView>>> NodeTextures;   // Sample Texture 2D 의 option texture
		};
		std::map<std::string, std::unique_ptr<Compiled>> s_Graphs;
		std::map<std::string, std::string> s_Errors;
		uint64 s_Generation = 0;

		// 재질마다 해석한 값 (재질 Properties 가 바뀌거나 그래프를 다시 만들면 다시)
		struct Parsed
		{
			uint64 Revision = 0, Generation = 0;
			std::vector<XMFLOAT4> Values;
			std::vector<ComPtr<GfxShaderResourceView>> Textures;
		};
		std::unordered_map<const UMaterial*, Parsed> s_Cache;

		// 그래프 파일 목록 (Shader 목록은 Inspector 가 매 프레임 묻는다 → 몇 초에 한 번만 디스크를 훑는다)
		std::vector<std::string> s_Assets;
		std::chrono::steady_clock::time_point s_AssetsTime;
		bool s_AssetsValid = false;

		const auto s_Start = std::chrono::steady_clock::now();

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
			if (c.Time)
			{
				const float t = std::chrono::duration<float>(std::chrono::steady_clock::now() - s_Start).count();
				const float tv[4] = { t, sinf(t), cosf(t), Time::DeltaTime() };
				c.Time->SetFloatVector(tv);
			}
			tech->GetPassByIndex(0)->Apply(0, dc);
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

		bool Create(const std::string& name)
		{
			const std::string asset = FindGraphAsset(name);
			if (asset.empty())
			{
				s_Errors[name] = "no " + name.substr(strlen(kShaderPrefix)) + kExtension + " under Assets";
				return false;
			}
			auto c = std::make_unique<Compiled>();
			c->Name = name;
			c->Asset = asset;
			std::string error;
			if (!c->G.Load(FullPath(asset), error))
			{
				s_Errors[name] = error;
				return false;
			}
			const CodeResult code = Generate(c->G);
			if (!code.Error.empty())
			{
				s_Errors[name] = code.Error;
				EditorLog::Write("ShaderGraph", "%s: %s", name.c_str(), code.Error.c_str());
				return false;
			}
			// .fx 를 쓴다 (같으면 그대로 — 셰이더 캐시가 다시 컴파일하지 않게)
			const std::wstring fxPath = GeneratedPath(name);
			std::error_code ec;
			fs::create_directories(fs::path(fxPath).parent_path(), ec);
			if (ReadFile(fxPath) != code.Hlsl)
			{
				std::ofstream os(fxPath, std::ios::binary | std::ios::trunc);
				if (!os)
				{
					s_Errors[name] = "cannot write " + Utf8(fxPath);
					return false;
				}
				os << code.Hlsl;
			}
			const std::string owner = kOwnerPrefix + name;
			c->Fx = CustomShaders::LoadEffect(owner, fxPath, error);
			if (!c->Fx)
			{
				std::string msg = ShaderCache::LastMessages(fs::path(fxPath).lexically_normal().wstring());
				s_Errors[name] = msg.empty() ? error : msg;
				EditorLog::Write("ShaderGraph", "%s: shader compile failed\n%s", name.c_str(), s_Errors[name].c_str());
				return false;
			}
			FxEffect* fx = c->Fx->GetFX();
			c->Batch = fx->GetTechniqueByName("GraphBatchTech");
			c->Skinned = fx->GetTechniqueByName("GraphSkinnedTech");
			if (!c->Batch || !c->Batch->IsValid())
			{
				s_Errors[name] = "the generated shader has no GraphBatchTech";
				CustomShaders::UnregisterOwner(owner);
				return false;
			}
			auto var = [&](const std::string& n) -> FxVar* {
				FxVar* v = fx->GetVariableByName(n.c_str());
				return v && v->IsValid() ? v : nullptr;
			};
			c->Time = var("gSGTime");
			c->ViewProjTex = var("gViewProjTex");
			for (const Property& p : c->G.Properties)
				c->PropVars.push_back(var("gSG_" + Sanitize(p.Ref)));
			for (const Node& n : c->G.Nodes)
			{
				const std::string tex = n.Options.value("texture", std::string());
				if (n.Type == "Sample Texture 2D" && !tex.empty())
					if (FxVar* v = var("gSG_NodeTex" + std::to_string(n.Id)))
						c->NodeTextures.push_back({ v, LoadTex(tex) });
			}
			c->Generation = ++s_Generation;
			s_Errors.erase(name);

			CustomShaders::Shader shader;
			shader.Name = name;
			shader.Owner = owner;
			shader.DrawInstanced = [name](CustomShaders::InstancedDraw& d) {
				Compiled* c = Current(name);
				if (!c) return;
				c->Fx->SetViewProj(d.ViewProj);
				const XMMATRIX vpt = d.ViewProj * ToTex();
				if (c->ViewProjTex) c->ViewProjTex->SetMatrix(reinterpret_cast<const float*>(&vpt));
				c->Fx->SetTexTransform(XMMatrixIdentity());
				RenderLayers::SetObjectLayer(c->Fx, d.LayerBit);
				Bind(*c, d.Material, d.Context, c->Batch);
				d.Draw();
			};
			if (c->Skinned && c->Skinned->IsValid())
				shader.DrawSkinned = [name](CustomShaders::SkinnedDraw& d) {
					Compiled* c = Current(name);
					if (!c) return;
					// 엔진 스킨 패스와 같은 행렬 · 본 (lilToon 과 같은 방법)
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
					RenderLayers::SetObjectLayer(c->Fx, ~0u);
					Bind(*c, d.Material, d.Context, c->Skinned);
					d.Draw();
				};
			shader.Inspector = [name](UMaterial& m) { return Inspector(name, m); };
			shader.DefaultProperties = [name]() {
				Compiled* c = Current(name);
				return c ? DefaultProperties(c->G) : nlohmann::json::object();
			};
			s_Graphs[name] = std::move(c);
			CustomShaders::Register(shader);
			EditorLog::Write("ShaderGraph", "built %s (%s)", name.c_str(), asset.c_str());
			return true;
		}

		void Drop(const std::string& name)
		{
			CustomShaders::UnregisterOwner(kOwnerPrefix + name);   // 이펙트도 같이
			s_Graphs.erase(name);
			CustomShaders::Forget(name);
		}

		void ScanAssets()
		{
			s_Assets.clear();
			const fs::path root = PathManager::GetI()->GetContentPathW();
			std::error_code ec;
			for (fs::recursive_directory_iterator it(root / L"Assets", fs::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec))
			{
				if (ec) break;
				if (!it->is_regular_file(ec) || Lower(it->path().extension().string()) != kExtension)
					continue;
				s_Assets.push_back(Utf8(fs::relative(it->path(), root, ec).wstring()));
			}
			std::sort(s_Assets.begin(), s_Assets.end());
			s_AssetsTime = std::chrono::steady_clock::now();
			s_AssetsValid = true;
		}
	}

	std::wstring FullPath(const std::string& assetPath)
	{
		if (fs::path(string_to_wstring(assetPath)).is_absolute())
			return string_to_wstring(assetPath);
		return PathManager::GetI()->GetMovePathW(string_to_wstring(assetPath));
	}

	std::string ShaderNameOf(const std::string& assetPath)
	{
		return std::string(kShaderPrefix) + Utf8(fs::path(string_to_wstring(assetPath)).stem().wstring());
	}

	std::vector<std::string> GraphAssets(bool refresh)
	{
		if (refresh || !s_AssetsValid || std::chrono::steady_clock::now() - s_AssetsTime > std::chrono::seconds(3))
			ScanAssets();
		return s_Assets;
	}

	std::string FindGraphAsset(const std::string& shaderName)
	{
		if (shaderName.rfind(kShaderPrefix, 0) != 0)
			return std::string();
		for (int pass = 0; pass < 2; ++pass)
		{
			for (const std::string& a : GraphAssets(pass == 1))
				if (ShaderNameOf(a) == shaderName)
					return a;
		}
		return std::string();
	}

	std::wstring GeneratedPath(const std::string& shaderName)
	{
		const std::string stem = Sanitize(shaderName.rfind(kShaderPrefix, 0) == 0 ? shaderName.substr(strlen(kShaderPrefix)) : shaderName);
		return (fs::path(PathManager::GetI()->GetContentPathW()) / L"Library" / L"ShaderGraph" / string_to_wstring(stem + ".fx")).lexically_normal().wstring();
	}

	std::string LastError(const std::string& shaderName)
	{
		auto it = s_Errors.find(shaderName);
		return it != s_Errors.end() ? it->second : std::string();
	}

	bool Reload(const std::string& assetPath, std::string& error)
	{
		const std::string name = ShaderNameOf(assetPath);
		GraphAssets(true);
		Drop(name);
		if (CustomShaders::Find(name))   // Provider 가 다시 만든다
			return true;
		error = LastError(name);
		if (error.empty()) error = "build failed";
		return false;
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
		const CustomShaders::Shader* cs = CustomShaders::Find(name);
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
		p.Prefix = kShaderPrefix;
		p.Owner = "shadergraph";
		p.Create = [](const std::string& name) { return Create(name); };
		p.List = []() {
			std::vector<std::string> out;
			for (const std::string& a : GraphAssets())
				out.push_back(ShaderNameOf(a));
			return out;
		};
		CustomShaders::RegisterProvider(p);
	}
}
