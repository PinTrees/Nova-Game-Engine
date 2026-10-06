#include "pch.h"
#include "PhysicsSettings.h"
#include "Physics2DSettings.h"
#include "Physics2DManager.h"
#include "TagsAndLayers.h"
#include "SpriteSlicer.h"
#include "SpriteAnimator.h"
#include "AssetImportSettings.h"
#include "PackageManager.h"
#include "AutoSave.h"
#include "PackageManagerWindow.h"
#include "AudioMixerWindow.h"
#include "CliCommands.h"
#include "CliServer.h"
#include "UndoSystem.h"
#include "GameObjectFactory.h"
#include "EditorGUIManager.h"
#include "SceneEditorWindow.h"
#include "GameViewEditorWindow.h"
#include "EditorCamera.h"
#include "ScriptEngine.h"
#include "CSharpScript.h"
#include "BuildPipeline.h"
#include "Transform.h"
#include "EngineInfo.h"
#include "PreferencesWindow.h"
#include "ProjectSettingsWindow.h"
#include "BuildSettingsWindow.h"
#include "GraphicsSettings.h"
#include "BuildSettings.h"
#include "App.h"
#include "ShaderCross.h"
#include "RhiTest.h"
#include "GfxTest.h"
#include "GfxGL.h"
#include "GLContext.h"
#include "Profiler.h"
#include "PhysicsManager.h"
#include "Terrain.h"
#include "TerrainData.h"
#include "TerrainEditor.h"
#include "MeshBatcher.h"
#include "Collider.h"
#include "AssetImportSettings.h"
#include "UISystem.h"
#include "ImportSettingsInspector.h"
#include "AudioClip.h"
#include "HumanoidAvatar.h"
#include "SkinnedMesh.h"

namespace
{
	using json = nlohmann::json;
	using CliServer::Register;

	Scene* CurrentScene() { return SceneManager::GetI()->GetCurrentScene(); }

	std::string PathOf(GameObject* go)
	{
		std::string p = go->GetName();
		for (GameObject* q = go->GetParent(); q; q = q->GetParent())
			p = q->GetName() + "/" + p;
		return p;
	}

	std::string IdOf(GameObject* go) { return "#" + std::to_string(go->GetFileID()); }

	std::string Lower(std::string s)
	{
		std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)tolower(c); });
		return s;
	}

	// 대상 찾기: "#fileID" | "부모/자식" 경로 | 이름 (같은 이름이 여럿이면 오류 + 후보 id)
	GameObject* Resolve(const json& target, std::string& error)
	{
		Scene* scene = CurrentScene();
		if (!scene)
		{
			error = "no scene is open";
			return nullptr;
		}
		if (!target.is_string() || target.get<std::string>().empty())
		{
			error = "missing target (object name, Parent/Child path or #id)";
			return nullptr;
		}
		const std::string t = target.get<std::string>();
		if (t[0] == '#')
		{
			const uint64 id = strtoull(t.c_str() + 1, nullptr, 10);
			if (GameObject* go = scene->FindByFileID(id))
				return go;
			error = "no object with id " + t;
			return nullptr;
		}
		if (t.find('/') != std::string::npos)
		{
			std::vector<GameObject*> level = scene->GetRootGameObjects();
			GameObject* found = nullptr;
			size_t start = 0;
			while (start <= t.size())
			{
				const size_t slash = t.find('/', start);
				const std::string part = t.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
				found = nullptr;
				for (GameObject* g : level)
					if (g->GetName() == part)
					{
						found = g;
						break;
					}
				if (!found)
				{
					error = "path not found: '" + t + "' (missing '" + part + "')";
					return nullptr;
				}
				level = found->GetChildren();
				if (slash == std::string::npos)
					break;
				start = slash + 1;
			}
			return found;
		}
		std::vector<GameObject*> matches;
		for (GameObject* g : scene->GetAllGameObjects())
			if (g->GetName() == t)
				matches.push_back(g);
		if (matches.size() == 1)
			return matches[0];
		if (matches.empty())
		{
			error = "object not found: '" + t + "'";
			return nullptr;
		}
		error = "ambiguous: " + std::to_string(matches.size()) + " objects named '" + t + "', use a Parent/Child path or an id:";
		for (size_t i = 0; i < matches.size() && i < 8; ++i)
			error += " " + IdOf(matches[i]) + " (" + PathOf(matches[i]) + ")";
		return nullptr;
	}

	bool ReadVec3(const json& v, Vec3& out)
	{
		if (v.is_array() && v.size() == 3 && v[0].is_number() && v[1].is_number() && v[2].is_number())
		{
			out = Vec3(v[0].get<float>(), v[1].get<float>(), v[2].get<float>());
			return true;
		}
		if (v.is_string())
		{
			float x, y, z;
			if (sscanf_s(v.get<std::string>().c_str(), "%f,%f,%f", &x, &y, &z) == 3)
			{
				out = Vec3(x, y, z);
				return true;
			}
		}
		return false;
	}

	json V3(const Vec3& v) { return json::array({ v.x, v.y, v.z }); }

	json Summary(GameObject* go, bool withComponents, int depth)
	{
		json j = { { "name", go->GetName() }, { "id", IdOf(go) } };
		if (!go->IsActive())
			j["active"] = false;
		if (withComponents)
		{
			json c = json::array();
			for (const auto& comp : go->GetComponents())
				c.push_back(comp->GetType());
			j["components"] = c;
		}
		const auto children = go->GetChildren();
		if (!children.empty())
		{
			if (depth == 0)
				j["childCount"] = children.size();
			else
			{
				json arr = json::array();
				for (GameObject* child : children)
					arr.push_back(Summary(child, withComponents, depth - 1));
				j["children"] = arr;
			}
		}
		return j;
	}

	json ObjectDetails(GameObject* go)
	{
		Transform* t = go->GetTransform();
		json j = {
			{ "name", go->GetName() }, { "id", IdOf(go) }, { "path", PathOf(go) }, { "active", go->IsActive() },
			{ "tag", go->GetTag() }, { "layer", (int)go->GetLayerIndex() }, { "layerName", TagsAndLayers::LayerName(go->GetLayerIndex()) }, { "static", go->IsStatic() },
			{ "parent", go->GetParent() ? json(IdOf(go->GetParent())) : json() },
			{ "children", go->GetChildren().size() },
		};
		if (t)
		{
			j["position"] = V3(t->GetLocalPosition());
			j["rotation"] = V3(t->GetLocalEulerAngles());
			j["scale"] = V3(t->GetLocalScale());
			j["worldPosition"] = V3(t->GetPosition());
		}
		json comps = json::array();
		for (const auto& comp : go->GetComponents())
			comps.push_back(comp->toJson());
		j["components"] = comps;
		return j;
	}

	// 바꾼 뒤: Undo 한 단계 ("CLI <이름>"), 선택하지 않은 오브젝트도 다음 확정에서 다시 직렬화
	void AfterEdit(const std::string& name, GameObject* go = nullptr)
	{
		if (go)
			Undo::Touch(go);
		Undo::SetActionName("CLI " + name);
		Undo::RequestCheck();
	}

	bool RequireEditMode(std::string& error)
	{
		if (Application::IsPlaying())
		{
			error = "not allowed in Play mode (nova stop first)";
			return false;
		}
		return true;
	}

	Component* FindComponent(GameObject* go, const std::string& type)
	{
		const std::string lt = Lower(type);
		for (const auto& c : go->GetComponents())
			if (Lower(c->GetType()) == lt)
				return c.get();
		return nullptr;
	}

	SceneEditorWindow* SceneWindow() { return dynamic_cast<SceneEditorWindow*>(EditorGUIManager::GetI()->FindWindow("Scene")); }

	std::wstring ProjectFile(const std::string& path)
	{
		std::filesystem::path p(string_to_wstring(path));
		if (p.is_absolute())
			return p.wstring();
		return PathManager::GetI()->GetMovePathW(p.wstring());
	}

	// 텍스처 → PNG/JPG (알파는 불투명으로: Scene 뷰는 빈 배경을 투명으로 지운다)
	bool SaveTexture(GfxTexture2D* tex, const std::wstring& file, int& w, int& h, std::string& error)
	{
		auto ctx = Application::GetI()->GetDeviceContext();
		DirectX::ScratchImage captured;
		HRESULT hr = Gfx::CaptureTexture(ctx, tex, captured);
		if (FAILED(hr))
		{
			error = "capture failed";
			return false;
		}
		const DirectX::Image* img = captured.GetImage(0, 0, 0);
		DirectX::ScratchImage converted;
		if (img->format != DXGI_FORMAT_R8G8B8A8_UNORM && img->format != DXGI_FORMAT_B8G8R8A8_UNORM)
		{
			hr = DirectX::Convert(*img, DXGI_FORMAT_R8G8B8A8_UNORM, DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, converted);
			if (FAILED(hr))
			{
				error = "format conversion failed";
				return false;
			}
			img = converted.GetImage(0, 0, 0);
		}
		for (size_t y = 0; y < img->height; ++y)
		{
			uint8_t* row = img->pixels + y * img->rowPitch;
			for (size_t x = 0; x < img->width; ++x)
				row[x * 4 + 3] = 255;
		}
		std::error_code ec;
		std::filesystem::create_directories(std::filesystem::path(file).parent_path(), ec);
		const std::wstring ext = Lower(wstring_to_string(std::filesystem::path(file).extension().wstring())) == ".jpg" ? L".jpg" : L".png";
		hr = DirectX::SaveToWICFile(*img, DirectX::WIC_FLAGS_FORCE_SRGB, DirectX::GetWICCodec(ext == L".jpg" ? DirectX::WIC_CODEC_JPEG : DirectX::WIC_CODEC_PNG), file.c_str());
		if (FAILED(hr))
		{
			error = "could not write " + wstring_to_string(file);
			return false;
		}
		w = (int)img->width;
		h = (int)img->height;
		return true;
	}

	// RGBA8 (행 0 = 위) → PNG
	bool SavePng(const std::vector<uint8_t>& rgba, int w, int h, const std::wstring& file, std::string& error)
	{
		DirectX::Image img = {};
		img.width = w;
		img.height = h;
		img.format = DXGI_FORMAT_R8G8B8A8_UNORM;
		img.rowPitch = (size_t)w * 4;
		img.slicePitch = img.rowPitch * h;
		img.pixels = const_cast<uint8_t*>(rgba.data());
		std::error_code ec;
		std::filesystem::create_directories(std::filesystem::path(file).parent_path(), ec);
		if (FAILED(DirectX::SaveToWICFile(img, DirectX::WIC_FLAGS_FORCE_SRGB, DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), file.c_str())))
		{
			error = "could not write " + wstring_to_string(file);
			return false;
		}
		return true;
	}

	// 두 RGBA8 그림의 차이: 채널 평균·최대, 어떤 채널이든 8 넘게 다른 픽셀 비율, 차이 그림(×4)
	json CompareImages(const std::vector<uint8_t>& A, const std::vector<uint8_t>& B, int w, int h, const std::string& outDir, const char* diffName)
	{
		if (A.size() != B.size() || A.empty())
			return { { "error", "image sizes differ" } };
		double sum = 0;
		int maxDiff = 0;
		size_t over = 0;
		std::vector<uint8_t> diff(A.size());
		for (size_t i = 0; i < A.size(); i += 4)
		{
			int pixelMax = 0;
			for (int c = 0; c < 3; ++c)
			{
				const int d = std::abs((int)A[i + c] - (int)B[i + c]);
				sum += d;
				pixelMax = (std::max)(pixelMax, d);
				diff[i + c] = (uint8_t)(std::min)(255, d * 4);
			}
			diff[i + 3] = 255;
			maxDiff = (std::max)(maxDiff, pixelMax);
			over += pixelMax > 8 ? 1 : 0;
		}
		const size_t pixels = A.size() / 4;
		json r = { { "mean", sum / (pixels * 3.0) }, { "max", maxDiff }, { "over8Percent", 100.0 * over / pixels } };
		std::string err;
		if (!outDir.empty() && SavePng(diff, w, h, (std::filesystem::path(string_to_wstring(outDir)) / diffName).wstring(), err))
			r["png"] = outDir + "\\" + diffName;
		return r;
	}

	GameObject* CreateByType(const std::string& typeIn, const json& args, std::string& error)
	{
		const std::string type = Lower(typeIn);
		const std::string name = args.value("name", std::string());
		auto named = [&](GameObject* go) {
			if (go && !name.empty())
				go->SetName(name);
			return go;
		};
		if (type == "empty") return named(GameObjectFactory::CreateEmpty(name.empty() ? "GameObject" : name));
		if (type == "cube") return named(GameObjectFactory::CreateCube());
		if (type == "sphere") return named(GameObjectFactory::CreateSphere());
		if (type == "capsule") return named(GameObjectFactory::CreateCapsule());
		if (type == "cylinder") return named(GameObjectFactory::CreateCylinder());
		if (type == "plane") return named(GameObjectFactory::CreatePlane());
		if (type == "quad") return named(GameObjectFactory::CreateQuad());
		if (type == "directional-light") return named(GameObjectFactory::CreateDirectionalLight());
		if (type == "point-light") return named(GameObjectFactory::CreatePointLight());
		if (type == "spot-light") return named(GameObjectFactory::CreateSpotLight());
		if (type == "camera") return named(GameObjectFactory::CreateCamera());
		if (type == "terrain") return named(GameObjectFactory::CreateTerrain());
		if (type == "tree") return named(GameObjectFactory::CreateTree());
		if (type == "particle-system") return named(GameObjectFactory::CreateParticleSystem());
		if (type == "visual-effect") return named(GameObjectFactory::CreateVisualEffect(args.value("asset", std::string())));
		if (type == "audio-source") return named(GameObjectFactory::CreateAudioSource());
		if (type == "rock") return named(GameObjectFactory::CreateRock(args.value("preset", 2)));
		if (type == "rock-scatter") return named(GameObjectFactory::CreateRockScatter(args.value("preset", 2)));
		if (type == "ocean") return named(GameObjectFactory::CreateWaterBody(0));
		if (type == "lake") return named(GameObjectFactory::CreateWaterBody(1));
		if (type == "river") return named(GameObjectFactory::CreateWaterBody(2));
		if (type == "volume") return named(GameObjectFactory::CreateVolume(GameObjectFactory::VolumeShape::Global));
		if (type == "character")
		{
			// 기본 캐릭터 (스킨 메시 + Animator). --model <FBX> --controller <.controller> 로 다른 모델·컨트롤러
			const std::string model = args.value("model", std::string());
			const std::string controller = args.value("controller", std::string());
			if (model.empty() && controller.empty())
				return named(GameObjectFactory::CreateAnimatedCharacter());
			// 컨트롤러를 주지 않으면 기본 컨트롤러 (Humanoid 리타게팅으로 다른 모델도 Idle · 걷기)
			GameObject* g = GameObjectFactory::CreateAnimatedCharacter(args.value("name", std::string("Character")),
				model.empty() ? std::string(GameObjectFactory::kDefaultCharacterModel) : model,
				controller.empty() ? std::string(GameObjectFactory::kDefaultCharacterController) : controller);
			return named(g);
		}
		if (type == "third-person-character" || type == "player")
		{
			// + Character Controller + ThirdPersonController + Main Camera 의 Follow Camera (패키지가 없으면 넣는다)
			std::string note;
			GameObject* g = GameObjectFactory::CreateThirdPersonCharacter("Player", &note);
			EditorLog::Write("CLI", "%s", note.c_str());
			return named(g);
		}
		error = "unknown type '" + typeIn + "' (empty, cube, sphere, capsule, cylinder, plane, quad, directional-light, point-light, spot-light, "
			"camera, terrain, tree, rock, rock-scatter, ocean, lake, river, particle-system, visual-effect, audio-source, volume, character, third-person-character)";
		return nullptr;
	}

	void Place(GameObject* go, const json& args)
	{
		Transform* t = go->GetTransform();
		Vec3 v;
		if (args.contains("position") && ReadVec3(args["position"], v)) t->SetLocalPosition(v);
		if (args.contains("rotation") && ReadVec3(args["rotation"], v)) t->SetLocalEulerAngles(v);
		if (args.contains("scale") && ReadVec3(args["scale"], v)) t->SetLocalScale(v);
		if (args.contains("worldPosition") && ReadVec3(args["worldPosition"], v)) t->SetPosition(v);
	}
}

namespace CliCommands
{
	void RegisterAll()
	{
		Register("help", "list commands", [](const json&, json& r, std::string&) { r = CliServer::Commands(); return true; });

		// nova wait N: 요청의 waitFrames 동안 에디터가 계속 그린다 (백그라운드에서도) — 끝나면 지금 프레임 번호
		// 성능 측정 (nova perf): perf-begin 이 프로파일러를 켜고, waitFrames 뒤의 perf 가 그 사이 프레임을 평균
		static std::chrono::steady_clock::time_point s_PerfStart;
		static uint64_t s_PerfFirst = 0;
		static int s_PerfFrame0 = 0;
		Register("perf-begin", "start a perf measurement (nova perf)", [](const json&, json& r, std::string&) {
			Profiler::ForceCollecting(true);
			s_PerfStart = std::chrono::steady_clock::now();
			s_PerfFrame0 = ImGui::GetFrameCount();
			s_PerfFirst = Profiler::History().empty() ? 0 : Profiler::History().back().Index;
			r = json::object();
			return true;
		});
		Register("perf", "finish a perf measurement: frame time, CPU / GPU ms, top GPU passes / CPU scopes {depth? (CPU scope depth, default 2)} (nova perf --frames N)", [](const json& a, json& r, std::string& e) {
			const int cpuDepth = std::clamp(a.value("depth", 2), 0, 8);
			const int gpuDepth = std::clamp(a.value("gpuDepth", 1), 0, 8);   // GPU 구간 깊이 (기본 = 뷰 + 단계)
			const double wallMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - s_PerfStart).count();
			Profiler::ForceCollecting(false);
			int frames = 0, gpuFrames = 0;
			double cpu = 0, gpu = 0, cpuMax = 0;
			std::map<std::string, std::pair<double, int>> passes, cpuScopes;
			std::map<std::string, std::pair<double, double>> passWork;   // GPU 구간의 삼각형 · 픽셀 셰이더 수 (PIPELINE_STATISTICS — 시간과 달리 CPU 대기에 흔들리지 않는다)
			for (const Profiler::Frame& f : Profiler::History())
			{
				if (f.Index <= s_PerfFirst + 2)   // 켠 직후 2 프레임은 건너뛴다 (구간이 다 차지 않음)
					continue;
				++frames;
				cpu += f.CpuMs;
				cpuMax = (std::max)(cpuMax, (double)f.CpuMs);
				for (const Profiler::CpuSample& c : f.Cpu)
					if (c.Depth <= cpuDepth)
					{
						auto& p = cpuScopes[std::string(c.Depth, '.') + c.Name];
						p.first += c.Ms;
						++p.second;
					}
				if (f.GpuMs >= 0.0f)
				{
					++gpuFrames;
					gpu += f.GpuMs;
					for (const Profiler::GpuSample& g : f.Gpu)
						if (g.Depth <= gpuDepth)
						{
							const std::string key = std::string(g.Depth, '.') + g.Name;
							auto& p = passes[key];
							p.first += g.Ms;
							++p.second;
							auto& w = passWork[key];
							w.first += (double)g.Primitives;
							w.second += (double)g.Pixels;
						}
				}
			}
			if (frames == 0) { e = "no frames recorded (run nova perf, not perf directly)"; return false; }
			std::vector<std::pair<double, std::string>> top;
			for (auto& [name, p] : passes)
				top.push_back({ p.first / (std::max)(1, gpuFrames), name });
			std::sort(top.rbegin(), top.rend());
			json list = json::array();
			for (size_t i = 0; i < top.size() && i < (gpuDepth > 1 ? 40u : 12u); ++i)
			{
				const auto& w = passWork[top[i].second];
				const double n = (std::max)(1, gpuFrames);
				list.push_back({ { "pass", top[i].second }, { "ms", std::round(top[i].first * 1000.0) / 1000.0 },
					{ "primitives", std::round(w.first / n) }, { "pixels", std::round(w.second / n) } });
			}
			std::vector<std::pair<double, std::string>> topCpu;
			for (auto& [name, p] : cpuScopes)
				topCpu.push_back({ p.first / frames, name });
			std::sort(topCpu.rbegin(), topCpu.rend());
			json cpuList = json::array();
			for (size_t i = 0; i < topCpu.size() && i < 24; ++i)
				cpuList.push_back({ { "scope", topCpu[i].second }, { "ms", std::round(topCpu[i].first * 1000.0) / 1000.0 } });
			const double avgFrame = wallMs / (std::max)(1, ImGui::GetFrameCount() - s_PerfFrame0);   // 벽시계 (수직 동기 없음)
			r = {
				{ "graphicsAPI", GraphicsAPIToKey(GraphicsSettings::GetActiveAPI()) },
				{ "frames", frames },
				{ "frameMs", std::round(avgFrame * 1000.0) / 1000.0 },
				{ "fps", std::round(1000.0 / avgFrame * 10.0) / 10.0 },
				{ "cpuMs", std::round(cpu / frames * 1000.0) / 1000.0 },
				{ "cpuMaxMs", std::round(cpuMax * 1000.0) / 1000.0 },
				{ "gpuMs", gpuFrames ? std::round(gpu / gpuFrames * 1000.0) / 1000.0 : -1.0 },
				{ "gpuPasses", list },
				{ "cpuScopes", cpuList },
				{ "stats", [&] {   // 프레임 평균 통계 (드로 콜·삼각형·컬링 … — RecordProfilerStats)
					std::map<std::string, std::pair<double, int>> st;
					for (const Profiler::Frame& f : Profiler::History())
						if (f.Index > s_PerfFirst + 2)
							for (const Profiler::Stat& s : f.Stats)
								{ auto& v = st[s.Name]; v.first += s.Value; ++v.second; }
					json o = json::object();
					for (auto& [k, v] : st) o[k] = std::round(v.first / (std::max)(1, v.second) * 1000.0) / 1000.0;
					return o;
				}() },   // 깊이 depth 까지 CPU 구간 (프레임 평균, 상위 24)
			};
			return true;
		});

		Register("exec", "run C# code in the editor (expression = its value, statements = return value) {code}", [](const json& a, json& r, std::string& e) {
			const std::string code = a.value("code", std::string());
			if (code.empty()) { e = "code is empty"; return false; }
			std::string result;
			if (!ScriptEngine::Exec(code, result, e)) return false;
			r = { { "result", result } };
			return true;
		});

		Register("terrain-trees", "mass place trees on a terrain (Paint Trees > Mass Place Trees) {target, count, clear?}", [](const json& a, json& r, std::string& e) {
			if (!RequireEditMode(e)) return false;
			GameObject* go = Resolve(a.value("target", json()), e);
			if (!go) return false;
			Terrain* terrain = go->GetComponent<Terrain>();
			std::shared_ptr<TerrainData> data = terrain ? terrain->GetTerrainData() : nullptr;
			if (!data) { e = PathOf(go) + " has no Terrain with terrain data"; return false; }
			if (a.value("clear", false))
			{
				data->TreeInstances.clear();
				data->OnTreesChanged();
			}
			if (data->TreePrototypes.empty())   // 종류가 없으면 Oak / Pine / Birch
				for (int preset : { 0, 1, 2 })
					TerrainEditor::AddTreePrototype(*data, preset);
			const int placed = TerrainEditor::MassPlaceTrees(terrain, (std::max)(0, a.value("count", 100)));
			r = { { "placed", placed }, { "trees", data->TreeInstances.size() }, { "prototypes", data->TreePrototypes.size() } };
			return true;
		});

		// 테셀레이션 전체 켜기 · 끄기 (끄면 테셀레이션이 없는 기기처럼 — POM · 픽셀 범프): nova tessellation info | set --enabled false
		Register("tessellation", "tessellation op: {op: info | set, enabled?} (material Displacement Mode, terrain heights, snow)", [](const json& a, json& r, std::string& e) {
			const std::string op = a.value("op", std::string("info"));
			if (op == "set" && a.contains("enabled"))
			{
				const auto& v = a["enabled"];
				MeshBatcher::TessellationEnabled() = v.is_boolean() ? v.get<bool>() : (v.is_string() ? v.get<std::string>() != "false" : v.get<int>() != 0);
			}
			else if (op != "info" && op != "set")
			{
				e = "unknown op (info | set --enabled true|false)";
				return false;
			}
			r = { { "enabled", MeshBatcher::TessellationEnabled() } };
			return true;
		});

		// 지형 높이: 원 안을 그 높이로 (가장자리 soft m 부드럽게 — 작으면 절벽) — 검사 · 자동화용 (Raise / Set Height 브러시와 같은 높이맵)
		Register("terrain-height", "set terrain height inside a circle {target, height (m), center [x,z], radius m, soft? m (1)}", [](const json& a, json& r, std::string& e) {
			if (!RequireEditMode(e)) return false;
			GameObject* go = Resolve(a.value("target", json()), e);
			if (!go) return false;
			Terrain* terrain = go->GetComponent<Terrain>();
			std::shared_ptr<TerrainData> data = terrain ? terrain->GetTerrainData() : nullptr;
			if (!data) { e = PathOf(go) + " has no Terrain with terrain data"; return false; }
			TerrainData& d = *data;
			if (!a.contains("center") || !a["center"].is_array() || a["center"].size() != 2) { e = "center [x,z] is required"; return false; }
			const float cx = a["center"][0].get<float>(), cz = a["center"][1].get<float>();
			const float radius = a.value("radius", 5.0f), soft = (std::max)(0.01f, a.value("soft", 1.0f));
			const float target = std::clamp(a.value("height", 0.0f) / (std::max)(d.Size.y, 1e-3f), 0.0f, 1.0f);
			const Vec3 origin = go->GetTransform()->GetPosition();
			const int res = d.HeightmapResolution;
			int x0 = res, z0 = res, x1 = -1, z1 = -1;
			for (int z = 0; z < res; ++z)
				for (int x = 0; x < res; ++x)
				{
					const float wx = origin.x + (float)x / (res - 1) * d.Size.x - cx, wz = origin.z + (float)z / (res - 1) * d.Size.z - cz;
					const float k = std::clamp((radius - sqrtf(wx * wx + wz * wz)) / soft, 0.0f, 1.0f);
					if (k <= 0.0f) continue;
					float& h = d.Heights[(size_t)z * res + x];
					h = h + (target - h) * k;
					x0 = (std::min)(x0, x); z0 = (std::min)(z0, z); x1 = (std::max)(x1, x); z1 = (std::max)(z1, z);
				}
			if (x1 >= 0)
				d.OnHeightsChanged(x0, z0, x1, z1);
			r = { { "changed", x1 >= 0 } };
			return true;
		});

		// Terrain Layer: 목록 · 더하기 · 바꾸기 · 칠하기 (Paint Texture 와 같은 컨트롤 맵) — 검사 · 자동화용
		Register("terrain-layer", "terrain layers {target, add?: .terrainlayer, set?: index + layer, fill?: index [center [x,z], radius m, soft m]}: list after", [](const json& a, json& r, std::string& e) {
			if (!RequireEditMode(e)) return false;
			GameObject* go = Resolve(a.value("target", json()), e);
			if (!go) return false;
			Terrain* terrain = go->GetComponent<Terrain>();
			std::shared_ptr<TerrainData> data = terrain ? terrain->GetTerrainData() : nullptr;
			if (!data) { e = PathOf(go) + " has no Terrain with terrain data"; return false; }
			TerrainData& d = *data;
			if (a.contains("add"))
			{
				if ((int)d.Layers.size() >= TerrainData::kMaxLayers) { e = "a terrain has at most 4 layers"; return false; }
				auto layer = TerrainLayer::Load(a["add"].get<std::string>());
				if (!layer) { e = "terrain layer not found: " + a["add"].get<std::string>(); return false; }
				d.Layers.push_back(layer);
				if (d.Layers.size() == 1)
					for (size_t i = 0; i < d.Control.size(); i += 4)
						d.Control[i] = 255, d.Control[i + 1] = d.Control[i + 2] = d.Control[i + 3] = 0;
				d.OnControlChanged(0, 0, d.ControlResolution - 1, d.ControlResolution - 1);
			}
			if (a.contains("set"))
			{
				const int index = a["set"].get<int>();
				if (index < 0 || index >= (int)d.Layers.size()) { e = "no layer " + std::to_string(index); return false; }
				auto layer = TerrainLayer::Load(a.value("layer", std::string()));
				if (!layer) { e = "terrain layer not found"; return false; }
				d.Layers[index] = layer;
				d.OnControlChanged(0, 0, d.ControlResolution - 1, d.ControlResolution - 1);
			}
			if (a.contains("fill"))
			{
				// 그 레이어 가중치 1 (원 안만 — center 는 월드 xz, 가장자리 soft m (기본 1) 부드럽게)
				const int index = a["fill"].get<int>();
				if (index < 0 || index >= (int)d.Layers.size()) { e = "no layer " + std::to_string(index); return false; }
				const Vec3 origin = go->GetTransform()->GetPosition();
				Vec3 c(0, 0, 0);
				const bool circle = a.contains("center") && ReadVec3(json::array({ a["center"][0], 0, a["center"][1] }), c);
				const float radius = a.value("radius", 1e9f);
				const float soft = (std::max)(0.01f, a.value("soft", 1.0f));
				const int res = d.ControlResolution;
				for (int z = 0; z < res; ++z)
					for (int x = 0; x < res; ++x)
					{
						float k = 1.0f;
						if (circle)
						{
							const float wx = origin.x + (x + 0.5f) / res * d.Size.x - c.x, wz = origin.z + (z + 0.5f) / res * d.Size.z - c.z;
							k = std::clamp((radius - sqrtf(wx * wx + wz * wz)) / soft, 0.0f, 1.0f);
						}
						if (k <= 0.0f) continue;
						uint8_t* px = &d.Control[((size_t)z * res + x) * 4];
						for (int ch = 0; ch < 4; ++ch)
							px[ch] = (uint8_t)std::lround(px[ch] * (1.0f - k) + (ch == index ? 255.0f : 0.0f) * k);
					}
				d.OnControlChanged(0, 0, res - 1, res - 1);
			}
			json layers = json::array();
			for (const auto& l : d.Layers)
				layers.push_back({ { "path", l->Path }, { "diffuse", l->DiffusePath }, { "height", l->HeightPath }, { "heightAmplitude", l->HeightAmplitude } });
			r = { { "layers", layers } };
			return true;
		});

		Register("raycast", "Physics.Raycast (Play mode) {origin, direction, maxDistance?, triggers?, layerMask?}", [](const json& a, json& r, std::string& e) {
			if (!Application::IsPlaying()) { e = "raycast needs Play mode (the physics world exists only while playing)"; return false; }
			Vec3 origin, dir;
			if (!a.contains("origin") || !ReadVec3(a["origin"], origin) || !a.contains("direction") || !ReadVec3(a["direction"], dir))
			{
				e = "origin and direction are required (x,y,z)";
				return false;
			}
			RaycastHit hit;
			if (!PhysicsManager::GetI()->Raycast(origin, dir, hit, a.value("maxDistance", 1000.0f), a.value("triggers", false), a.value("layerMask", PhysicsManager::kDefaultRaycastLayers)))
			{
				r = { { "hit", false } };
				return true;
			}
			r = { { "hit", true }, { "object", hit.gameObject ? PathOf(hit.gameObject) : "" }, { "collider", hit.collider ? hit.collider->InspectorTitle() : "" },
				{ "point", { hit.point.x, hit.point.y, hit.point.z } }, { "normal", { hit.normal.x, hit.normal.y, hit.normal.z } }, { "distance", hit.distance } };
			return true;
		});

		// Project Settings > Tags and Layers
		Register("layers", "layers and tags: list, or {set: layer, name} / {addTag} / {removeTag}", [](const json& a, json& r, std::string& e) {
			if (a.contains("set"))
			{
				const int layer = a["set"].get<int>();
				if (TagsAndLayers::IsBuiltinLayer(layer)) { e = "layer " + std::to_string(layer) + " is a Builtin Layer"; return false; }
				if (!TagsAndLayers::SetLayerName(layer, a.value("name", std::string()))) { e = "cannot name layer " + std::to_string(layer) + " (0..31, the name must be unique)"; return false; }
			}
			if (a.contains("addTag")) TagsAndLayers::AddTag(a["addTag"].get<std::string>());
			if (a.contains("addSortingLayer") && TagsAndLayers::AddSortingLayer(a["addSortingLayer"].get<std::string>()) < 0) { e = "sorting layer exists"; return false; }
			if (a.contains("moveSortingLayer"))
			{
				const int id = TagsAndLayers::SortingLayerIdFromName(a["moveSortingLayer"].get<std::string>());
				if (id < 0 || !TagsAndLayers::MoveSortingLayer(id, a.value("by", -1))) { e = "cannot move that sorting layer"; return false; }
			}
			if (a.contains("removeTag") && !TagsAndLayers::RemoveTag(a["removeTag"].get<std::string>())) { e = "cannot remove tag (builtin or unknown)"; return false; }
			json layers = json::array();
			for (int i : TagsAndLayers::NamedLayers())
				layers.push_back({ { "layer", i }, { "name", TagsAndLayers::LayerName(i) }, { "builtin", TagsAndLayers::IsBuiltinLayer(i) } });
			json sorting = json::array();
			for (const TagsAndLayers::SortingLayer& s : TagsAndLayers::SortingLayers())
				sorting.push_back({ { "name", s.Name }, { "id", s.Id } });
			r = { { "layers", layers }, { "tags", TagsAndLayers::Tags() }, { "sortingLayers", sorting } };
			return true;
		});

		// 텍스처 자르기 (Sprite Mode = Multiple) + 프레임 애니메이션
		Register("sprite-slice", "slice a texture into sprites {path, mode: grid|count|auto|sheet, cell?: [w,h], count?: [cols,rows], offset?, padding?, pivot?: [x,y], keepEmpty?, minSize?, ppu?, filter?: point|bilinear, animation?: fps (also write <name>.spriteanim)}", [](const json& a, json& r, std::string& e) {
			const std::string rel = a.value("path", std::string());
			const std::wstring full = PathManager::GetI()->GetMovePathW(string_to_wstring(rel));
			SpriteSlicer::Pixels px;
			if (rel.empty() || !px.Load(full)) { e = "cannot read image " + rel; return false; }
			SpriteSlicer::Options o;
			o.BaseName = wstring_to_string(std::filesystem::path(full).stem().wstring());
			if (a.contains("pivot") && a["pivot"].is_array() && a["pivot"].size() == 2) { o.PivotX = a["pivot"][0].get<float>(); o.PivotY = a["pivot"][1].get<float>(); }
			o.KeepEmpty = a.value("keepEmpty", false);
			auto pair = [&](const char* key, int d0, int d1, int out[2]) {
				out[0] = d0; out[1] = d1;
				if (a.contains(key) && a[key].is_array() && a[key].size() == 2) { out[0] = a[key][0].get<int>(); out[1] = a[key][1].get<int>(); }
			};
			int cell[2], count[2], off[2], pad[2];
			pair("cell", 32, 32, cell); pair("count", 4, 1, count); pair("offset", 0, 0, off); pair("padding", 0, 0, pad);
			const std::string mode = a.value("mode", std::string("grid"));
			std::vector<AssetImport::SpriteRect> rects;
			float fps = 12.0f;
			if (mode == "grid") rects = SpriteSlicer::GridBySize(px, cell[0], cell[1], off[0], off[1], pad[0], pad[1], o);
			else if (mode == "count") rects = SpriteSlicer::GridByCount(px, count[0], count[1], o);
			else if (mode == "auto") rects = SpriteSlicer::Automatic(px, a.value("minSize", 4), o);
			else if (mode == "sheet")
			{
				const std::wstring sheet = SpriteSlicer::SheetJsonFor(full);
				if (sheet.empty() || !SpriteSlicer::FromSheetJson(sheet, px.W, px.H, o, rects, &fps)) { e = "no 2D Animator sheet .json next to the image"; return false; }
			}
			else { e = "mode: grid | count | auto | sheet"; return false; }
			if (rects.empty()) { e = "no sprites found"; return false; }
			json settings = AssetImport::LoadJson(full);
			AssetImport::TextureSettings ts;
			ts.FromJson(settings);
			ts.TextureType = AssetImport::TextureSettings::Sprite;
			ts.SpriteMode = AssetImport::TextureSettings::MultipleSprites;
			ts.MipMaps = false;
			ts.Sprites = rects;
			if (a.contains("ppu")) ts.PixelsPerUnit = (std::max)(0.01f, a["ppu"].get<float>());
			if (a.contains("filter")) ts.FilterMode = a["filter"].get<std::string>() == "point" ? AssetImport::TextureSettings::Point : AssetImport::TextureSettings::Bilinear;
			if (!ImportSettingsInspector::Apply(full, ts.ToJson(), e)) return false;   // .meta 저장 + 다시 가져오기
			json names = json::array();
			for (const AssetImport::SpriteRect& s : rects) names.push_back(s.Name);
			r = { { "sprites", (int)rects.size() }, { "names", names }, { "image", { px.W, px.H } } };
			if (a.contains("animation"))
			{
				SpriteAnimClip clip;
				clip.Fps = a["animation"].is_number() ? a["animation"].get<float>() : fps;
				for (const AssetImport::SpriteRect& s : rects) clip.Frames.push_back(rel + "#" + s.Name);
				std::filesystem::path anim = std::filesystem::path(string_to_wstring(rel)).replace_extension(L".spriteanim");
				const std::string animPath = wstring_to_string(anim.wstring());
				if (!SpriteAnimClips::Save(animPath, clip)) { e = "cannot write " + animPath; return false; }
				r["animation"] = animPath;
				r["fps"] = clip.Fps;
			}
			return true;
		});

		// Project Settings > Physics: Gravity, Layer Collision Matrix
		Register("physics-settings", "physics settings: get, or {gravity: [x,y,z]} / {collide: [a,b] | ignore: [a,b]} (layer numbers or names) / {all: true|false}", [](const json& a, json& r, std::string& e) {
			auto layerOf = [&](const json& v) { return v.is_string() ? TagsAndLayers::NameToLayer(v.get<std::string>()) : v.get<int>(); };
			if (a.value("2d", false))
			{
				// Project Settings > Physics 2D (Box2D)
				if (a.contains("gravity") && a["gravity"].is_array() && a["gravity"].size() >= 2)
					Physics2DSettings::SetGravity(Vec2(a["gravity"][0].get<float>(), a["gravity"][1].get<float>()));
				for (const char* key : { "collide", "ignore" })
					if (a.contains(key))
					{
						const json& p = a[key];
						if (!p.is_array() || p.size() != 2) { e = std::string(key) + " needs two layers [a, b]"; return false; }
						const int la = layerOf(p[0]), lb = layerOf(p[1]);
						if (la < 0 || lb < 0 || la > 31 || lb > 31) { e = "unknown layer in " + p.dump(); return false; }
						Physics2DSettings::SetLayersCollide(la, lb, std::string(key) == "collide");
					}
				if (a.contains("all")) Physics2DSettings::SetAllCollide(a["all"].get<bool>());
				const Vec2 g2 = Physics2DSettings::Gravity();
				json ignored2 = json::array();
				const std::vector<int> named2 = TagsAndLayers::NamedLayers();
				for (size_t i = 0; i < named2.size(); ++i)
					for (size_t k = i; k < named2.size(); ++k)
						if (!Physics2DSettings::LayersCollide(named2[i], named2[k]))
							ignored2.push_back({ TagsAndLayers::LayerName(named2[i]), TagsAndLayers::LayerName(named2[k]) });
				r = { { "gravity", { g2.x, g2.y } }, { "ignoredPairs", ignored2 }, { "bodies", Physics2DManager::BodyCount() }, { "playing", Physics2DManager::Active() } };
				return true;
			}
			if (a.contains("gravity") && a["gravity"].is_array() && a["gravity"].size() == 3)
				PhysicsSettings::SetGravity(Vec3(a["gravity"][0].get<float>(), a["gravity"][1].get<float>(), a["gravity"][2].get<float>()));
			for (const char* key : { "collide", "ignore" })
				if (a.contains(key))
				{
					const json& p = a[key];
					if (!p.is_array() || p.size() != 2) { e = std::string(key) + " needs two layers [a, b]"; return false; }
					const int la = layerOf(p[0]), lb = layerOf(p[1]);
					if (la < 0 || lb < 0 || la > 31 || lb > 31) { e = "unknown layer in " + p.dump(); return false; }
					PhysicsSettings::SetLayersCollide(la, lb, std::string(key) == "collide");
				}
			if (a.contains("all")) PhysicsSettings::SetAllCollide(a["all"].get<bool>());
			const Vec3 g = PhysicsSettings::Gravity();
			json ignored = json::array();
			const std::vector<int> named = TagsAndLayers::NamedLayers();
			for (size_t i = 0; i < named.size(); ++i)
				for (size_t k = i; k < named.size(); ++k)
					if (!PhysicsSettings::LayersCollide(named[i], named[k]))
						ignored.push_back({ TagsAndLayers::LayerName(named[i]), TagsAndLayers::LayerName(named[k]) });
			r = { { "gravity", { g.x, g.y, g.z } }, { "ignoredPairs", ignored } };
			return true;
		});

		Register("wait", "keep rendering for the request's waitFrames, then answer {} (nova wait N)", [](const json&, json& r, std::string&) {
			r = { { "frame", ImGui::GetFrameCount() } };
			return true;
		});

		Register("info", "editor state: project, scene, dirty, playing, selection", [](const json&, json& r, std::string&) {
			Scene* scene = CurrentScene();
			GameObject* sel = SelectionManager::GetSelectedObjectType() == SelectionType::GAMEOBJECT ? SelectionManager::GetSelectedGameObject() : nullptr;
			r = {
				{ "version", ENGINE_VERSION_A },
				{ "project", PathManager::GetI()->GetContentPathS() },
				{ "scene", scene ? wstring_to_string(scene->GetScenePath()) : "" },
				{ "dirty", SceneManager::GetI()->IsCurrentSceneDirty() },
				{ "playing", Application::IsPlaying() },
				{ "paused", Application::IsPaused() },
				{ "compiling", ScriptEngine::IsCompiling() },
				{ "objects", scene ? scene->GetAllGameObjects().size() : 0 },
				{ "liveObjects", GameObject::LiveCount() },   // 메모리에 있는 GameObject 전체 (지운 오브젝트가 새는지 확인)
				{ "selection", sel ? json{ { "path", PathOf(sel) }, { "id", IdOf(sel) } } : json() },
				{ "canUndo", Undo::CanUndo() }, { "undo", Undo::UndoName() },
				{ "graphicsAPI", GraphicsAPIToKey(GraphicsSettings::GetActiveAPI()) },
			};
			wchar_t title[512] = {};
			::GetWindowTextW(Application::GetI()->GetMainHwnd(), title, 512);
			r["title"] = wstring_to_string(title);
			return true;
		});

		Register("hierarchy", "scene tree {components?, depth?, root?}", [](const json& a, json& r, std::string& e) {
			Scene* scene = CurrentScene();
			if (!scene) { e = "no scene is open"; return false; }
			const bool comps = a.value("components", false);
			const int depth = a.value("depth", -1);
			r = json::array();
			if (a.contains("root"))
			{
				GameObject* root = Resolve(a["root"], e);
				if (!root) return false;
				r.push_back(Summary(root, comps, depth));
				return true;
			}
			for (GameObject* go : scene->GetRootGameObjects())
				r.push_back(Summary(go, comps, depth));
			return true;
		});

		Register("find", "search objects {name? (substring), component?, limit?}", [](const json& a, json& r, std::string& e) {
			Scene* scene = CurrentScene();
			if (!scene) { e = "no scene is open"; return false; }
			const std::string name = Lower(a.value("name", std::string()));
			const std::string comp = a.value("component", std::string());
			const int limit = a.value("limit", 200);
			r = json::array();
			for (GameObject* go : scene->GetAllGameObjects())
			{
				if (!name.empty() && Lower(go->GetName()).find(name) == std::string::npos) continue;
				if (!comp.empty() && !FindComponent(go, comp)) continue;
				r.push_back({ { "path", PathOf(go) }, { "id", IdOf(go) } });
				if ((int)r.size() >= limit) break;
			}
			return true;
		});

		Register("get", "object details + component JSON {target, component?}", [](const json& a, json& r, std::string& e) {
			GameObject* go = Resolve(a.value("target", json()), e);
			if (!go) return false;
			if (a.contains("component"))
			{
				Component* c = FindComponent(go, a["component"].get<std::string>());
				if (!c) { e = "no component '" + a["component"].get<std::string>() + "' on " + PathOf(go); return false; }
				r = c->toJson();
				return true;
			}
			r = ObjectDetails(go);
			return true;
		});

		Register("set", "change an object {target, name?, active?, tag?, layer?, static?, position?, rotation?, scale?, worldPosition?, component?, values?}",
			[](const json& a, json& r, std::string& e) {
			if (!RequireEditMode(e)) return false;
			GameObject* go = Resolve(a.value("target", json()), e);
			if (!go) return false;
			if (a.contains("name")) go->SetName(a["name"].get<std::string>());
			if (a.contains("active")) go->SetActive(a["active"].get<bool>());
			if (a.contains("tag")) go->SetTag(a["tag"].get<std::string>());
			if (a.contains("layer"))
			{
				// 번호 또는 이름 (Project Settings > Tags and Layers)
				int layer = a["layer"].is_string() ? TagsAndLayers::NameToLayer(a["layer"].get<std::string>()) : a["layer"].get<int>();
				if (layer < 0 || layer > 31) { e = "no layer '" + (a["layer"].is_string() ? a["layer"].get<std::string>() : a["layer"].dump()) + "' (nova layers)"; return false; }
				go->SetLayerIndex((uint8)layer);
			}
			if (a.contains("static")) go->SetStatic(a["static"].get<bool>());
			Place(go, a);
			if (a.contains("component"))
			{
				const std::string type = a["component"].get<std::string>();
				if (Lower(type) == "transform") { e = "use position / rotation / scale / worldPosition for Transform"; return false; }
				Component* c = FindComponent(go, type);
				if (!c) { e = "no component '" + type + "' on " + PathOf(go); return false; }
				if (!a.contains("values") || !a["values"].is_object()) { e = "values must be a JSON object of component fields (see nova get <target> --component " + type + ")"; return false; }
				json merged = c->toJson();
				merged.merge_patch(a["values"]);
				c->fromJson(merged);
				r["component"] = c->toJson();
			}
			AfterEdit("Set " + go->GetName(), go);
			r["object"] = ObjectDetails(go);
			r["object"].erase("components");
			return true;
		});

		Register("create", "create an object {type, name?, parent?, position?, rotation?, scale?, preset?}", [](const json& a, json& r, std::string& e) {
			if (!RequireEditMode(e)) return false;
			Scene* scene = CurrentScene();
			if (!scene) { e = "no scene is open"; return false; }
			GameObject* parent = nullptr;
			if (a.contains("parent") && !a["parent"].is_null())
			{
				parent = Resolve(a["parent"], e);
				if (!parent) return false;
			}
			// UI: "ui:Button", "ui:Dropdown", "ui:ScrollView" … = GameObject > UI 메뉴와 같은 구조 (캔버스가 없으면 Canvas + EventSystem 도)
			const std::string type = a.value("type", std::string("empty"));
			if (type.rfind("ui:", 0) == 0)
			{
				GameObject* ui = UISystem::Create(type.substr(3), scene, parent);
				if (!ui) { e = "unknown UI kind '" + type.substr(3) + "' (Image, Text, Panel, Button, Toggle, Slider, Scrollbar, ScrollView, Dropdown, InputField, Canvas, EventSystem)"; return false; }
				if (a.contains("name") && a["name"].is_string()) ui->SetName(a["name"].get<std::string>());
				AfterEdit("Create " + ui->GetName(), ui);
				r = { { "path", PathOf(ui) }, { "id", IdOf(ui) } };
				return true;
			}
			GameObject* go = CreateByType(type, a, e);
			if (!go) return false;
			if (parent)
				go->SetParent(parent, false);
			else
				scene->AddRootGameObject(go);
			Place(go, a);
			if (a.value("select", true))
				SelectionManager::SetSelectedGameObject(go);
			AfterEdit("Create " + go->GetName(), go);
			r = { { "path", PathOf(go) }, { "id", IdOf(go) } };
			return true;
		});

		Register("delete", "delete an object (and its children) {target}", [](const json& a, json& r, std::string& e) {
			if (!RequireEditMode(e)) return false;
			GameObject* go = Resolve(a.value("target", json()), e);
			if (!go) return false;
			r = { { "deleted", PathOf(go) }, { "id", IdOf(go) } };
			if (SelectionManager::GetSelectedObjectType() == SelectionType::GAMEOBJECT && SelectionManager::GetSelectedGameObject() == go)
				SelectionManager::ClearSelection();
			CurrentScene()->DestroyGameObject(go);
			AfterEdit("Delete");
			return true;
		});

		Register("add-component", "add a component {target, type}", [](const json& a, json& r, std::string& e) {
			if (!RequireEditMode(e)) return false;
			GameObject* go = Resolve(a.value("target", json()), e);
			if (!go) return false;
			const std::string type = a.value("type", std::string());
			if (FindComponent(go, type) && Lower(type) == "transform") { e = "already has a Transform"; return false; }
			std::shared_ptr<Component> c = ComponentFactory::Instance().CreateComponent(type);
			if (!c)
			{
				// C# 스크립트: 클래스 이름 (또는 script:이름) — 컴파일된 MonoBehaviour 클래스여야 한다
				const std::string cls = type.rfind("script:", 0) == 0 ? type.substr(7) : type;
				if (ScriptEngine::FindClass(cls) != nullptr)
					c = CSharpScript::Create(cls);
			}
			if (!c) { e = "unknown component type '" + type + "' (type names as in nova get output, e.g. RigidBody, BoxCollider, Light, AudioSource, or a compiled C# MonoBehaviour class name)"; return false; }
			// 값을 먼저 검사한다 (이름을 못 찾으면 컴포넌트를 붙이지 않고 실패)
			const bool hasValues = a.contains("values") && a["values"].is_object();
			json values = hasValues ? a["values"] : json::object();
			if (hasValues)
			{
				if (auto* script = dynamic_cast<CSharpScript*>(c.get()))
				{
					// 스크립트는 필드만 넘겨도 된다 ({"speed":42} = {"fields":{"speed":42}}).
					// GameObject·Transform 필드에 이름/경로/#id 문자열을 주면 그 오브젝트의 fileID 로 바꾼다
					if (!values.contains("fields"))
						values = json{ { "fields", values } };
					if (const ScriptEngine::ClassInfo* ci = ScriptEngine::FindClass(script->GetClassName()))
						for (const ScriptEngine::FieldInfo& f : ci->Fields)
							if ((f.Type == "GameObject" || f.Type == "Transform") && values["fields"].contains(f.Name) && values["fields"][f.Name].is_string())
							{
								GameObject* ref = Resolve(values["fields"][f.Name], e);
								if (!ref) return false;
								values["fields"][f.Name] = ref->GetFileID();
							}
				}
			}
			go->AddComponent(c);
			if (hasValues)
			{
				json merged = c->toJson();
				merged.merge_patch(values);
				c->fromJson(merged);
			}
			AfterEdit("Add " + type, go);
			r = c->toJson();
			return true;
		});

		Register("remove-component", "remove a component {target, type}", [](const json& a, json& r, std::string& e) {
			if (!RequireEditMode(e)) return false;
			GameObject* go = Resolve(a.value("target", json()), e);
			if (!go) return false;
			const std::string type = a.value("type", std::string());
			if (Lower(type) == "transform") { e = "cannot remove Transform"; return false; }
			Component* c = FindComponent(go, type);
			if (!c) { e = "no component '" + type + "' on " + PathOf(go); return false; }
			CurrentScene()->DestroyComponent(c);
			AfterEdit("Remove " + type, go);
			r = { { "removed", type }, { "from", PathOf(go) } };
			return true;
		});

		Register("parent", "re-parent {target, parent (null = root), keepWorld?}", [](const json& a, json& r, std::string& e) {
			if (!RequireEditMode(e)) return false;
			GameObject* go = Resolve(a.value("target", json()), e);
			if (!go) return false;
			GameObject* parent = nullptr;
			if (a.contains("parent") && !a["parent"].is_null() && !(a["parent"].is_string() && a["parent"].get<std::string>().empty()))
			{
				parent = Resolve(a["parent"], e);
				if (!parent) return false;
			}
			go->SetParent(parent, a.value("keepWorld", true));
			AfterEdit("Parent " + go->GetName(), go);
			r = { { "path", PathOf(go) }, { "id", IdOf(go) } };
			return true;
		});

		Register("import-settings", "asset Import Settings (.meta): get, or set + reimport {path, values?, reset?}", [](const json& a, json& r, std::string& e) {
			const std::wstring file = ProjectFile(a.value("path", std::string()));
			const AssetImport::Kind kind = AssetImport::KindOf(file);
			if (kind == AssetImport::Kind::None) { e = "no import settings for this file type (textures, .fbx models, .wav/.ogg/.mp3 audio)"; return false; }
			std::error_code ec;
			if (!std::filesystem::exists(file, ec)) { e = "asset not found: " + a.value("path", std::string()); return false; }
			if (a.value("reset", false) || (a.contains("values") && a["values"].is_object()))
			{
				json settings = a.value("reset", false) ? json::object() : AssetImport::LoadJson(file);
				if (a.contains("values") && a["values"].is_object())
					settings.merge_patch(a["values"]);
				if (a.value("reset", false) && !a.contains("values"))
				{
					// 기본값으로: .meta 를 지운다
					if (!RequireEditMode(e)) return false;
					std::filesystem::remove(AssetImport::MetaPath(file), ec);
					ImportSettingsInspector::Reimport(file);
				}
				else if (!ImportSettingsInspector::Apply(file, settings, e))
					return false;
			}
			const std::string rel = wstring_to_string(PathManager::GetI()->GetCutSolutionPath(file));
			r = { { "path", rel }, { "importer", kind == AssetImport::Kind::Texture ? "TextureImporter" : (kind == AssetImport::Kind::Model ? "ModelImporter" : "AudioImporter") },
				{ "hasMeta", std::filesystem::exists(AssetImport::MetaPath(file), ec) }, { "settings", AssetImport::LoadJson(file) } };
			// 가져온 결과 (확인용)
			if (kind == AssetImport::Kind::Texture)
			{
				ResourceManager::GetI()->LoadTexture(string_to_wstring(rel));
				AssetImport::TextureInfo t;
				if (AssetImport::GetTextureInfo(file, t))
					r["imported"] = { { "sourceWidth", t.SourceWidth }, { "sourceHeight", t.SourceHeight }, { "width", t.Width }, { "height", t.Height },
						{ "mips", t.Mips }, { "format", t.Format }, { "bytes", t.Bytes } };
			}
			else if (kind == AssetImport::Kind::Audio)
			{
				if (auto clip = AudioClip::Load(rel))
					r["imported"] = { { "channels", clip->Channels }, { "sourceChannels", clip->SourceChannels }, { "frequency", clip->Frequency },
						{ "length", clip->Length }, { "streaming", clip->Streaming }, { "bytes", clip->Streaming && clip->Encoded ? clip->Encoded->size() : clip->Data.size() } };
			}
			else if (auto mf = ResourceManager::GetI()->LoadMeshFile(rel))
			{
				json m = { { "meshes", mf->Meshs.size() }, { "skinnedMeshes", mf->SkinnedMeshs.size() }, { "clips", mf->SkinnedData.AnimationClips.size() } };
				if (!mf->Avatas.empty() && mf->Avatas[0])
				{
					const Humanoid::Avatar& av = Humanoid::Get(*mf->Avatas[0]);
					m["unitScale"] = mf->Avatas[0]->UnitScale;
					m["humanoid"] = av.Valid;
					m["humanBones"] = av.Found;
					json bones = json::object();
					for (int b = 0; b < Humanoid::BoneCount; ++b)
						if (av.Node[b] >= 0 && av.Node[b] < (int)mf->Avatas[0]->NodeNames.size())
							bones[Humanoid::BoneName(b)] = mf->Avatas[0]->NodeNames[av.Node[b]];
					m["bones"] = bones;
				}
				r["imported"] = m;
			}
			return true;
		});

		Register("select", "select an object in the editor {target} | {asset} (Project file) (none = clear)", [](const json& a, json& r, std::string& e) {
			if (a.contains("asset") && a["asset"].is_string())
			{
				const std::wstring file = ProjectFile(a["asset"].get<std::string>());
				std::error_code ec;
				if (!std::filesystem::exists(file, ec)) { e = "asset not found: " + a["asset"].get<std::string>(); return false; }
				SelectionManager::SetSelectedFile(file);
				r = { { "asset", wstring_to_string(PathManager::GetI()->GetCutSolutionPath(file)) } };
				return true;
			}
			if (!a.contains("target") || a["target"].is_null())
			{
				SelectionManager::ClearSelection();
				r = nullptr;
				return true;
			}
			GameObject* go = Resolve(a["target"], e);
			if (!go) return false;
			SelectionManager::SetSelectedGameObject(go);
			r = { { "path", PathOf(go) }, { "id", IdOf(go) } };
			return true;
		});

		Register("scene-new", "new untitled scene (camera + light + volume) {force?}", [](const json& a, json& r, std::string& e) {
			if (!RequireEditMode(e)) return false;
			if (SceneManager::GetI()->IsCurrentSceneDirty() && !a.value("force", false))
			{
				e = "the current scene has unsaved changes (nova scene save, or --force to discard)";
				return false;
			}
			SceneManager::GetI()->CreateScene();
			Scene* scene = CurrentScene();
			r = { { "scene", "" }, { "objects", scene ? scene->GetAllGameObjects().size() : 0 } };
			return true;
		});

		Register("scene-open", "open a scene {path (Assets/...), force?}", [](const json& a, json& r, std::string& e) {
			if (!RequireEditMode(e)) return false;
			const std::string path = a.value("path", std::string());
			const std::wstring file = ProjectFile(path);
			std::error_code ec;
			if (path.empty() || !std::filesystem::exists(file, ec)) { e = "scene not found: " + path; return false; }
			if (SceneManager::GetI()->IsCurrentSceneDirty() && !a.value("force", false))
			{
				e = "the current scene has unsaved changes (nova scene save, or --force to discard)";
				return false;
			}
			const std::wstring scenePath = PathManager::GetI()->GetCutSolutionPath(file);
			Scene* previous = CurrentScene();
			const bool reload = previous && previous->GetScenePath() == scenePath;
			const uint64 serial = previous ? previous->GetSerial() : 0;
			if (reload)
				SceneManager::GetI()->DiscardChanges();
			else
				SceneManager::GetI()->LoadScene(scenePath);
			Scene* scene = CurrentScene();
			if (!scene || scene->GetScenePath() != scenePath || (reload && scene->GetSerial() == serial))
			{
				e = "scene could not be loaded (see nova log): " + path;
				return false;
			}
			SelectionManager::ClearSelection();
			r = { { "scene", scene ? wstring_to_string(scene->GetScenePath()) : "" }, { "objects", scene ? scene->GetAllGameObjects().size() : 0 } };
			return true;
		});

		Register("scene-save", "save the current scene {as? (Assets/... .scene: save as)}", [](const json& a, json& r, std::string& e) {
			if (!RequireEditMode(e)) return false;
			Scene* scene = CurrentScene();
			if (!scene) { e = "no scene is open"; return false; }
			if (a.contains("as"))
			{
				// 다른 이름으로 (대화상자 없이): 프로젝트 기준 경로
				std::wstring rel = string_to_wstring(a.value("as", ""));
				std::replace(rel.begin(), rel.end(), L'/', L'\\');
				if (rel.size() < 7 || _wcsicmp(rel.c_str() + rel.size() - 6, L".scene") != 0 || _wcsnicmp(rel.c_str(), L"Assets\\", 7) != 0)
				{
					e = "--as needs a path like Assets/Scenes/Name.scene";
					return false;
				}
				const std::wstring full = PathManager::GetI()->GetMovePathW(rel);
				std::error_code ec;
				std::filesystem::create_directories(std::filesystem::path(full).parent_path(), ec);
				scene->SetScenePath(rel);   // 다른 씬처럼 프로젝트 기준 상대 경로
			}
			if (scene->GetScenePath().empty()) { e = "the scene has never been saved (nova scene save --as Assets/Scenes/Name.scene)"; return false; }
			if (!SceneManager::GetI()->SaveCurrentScene(false)) { e = "save failed (see nova log)"; return false; }
			r = { { "scene", wstring_to_string(scene->GetScenePath()) } };
			return true;
		});

		Register("play", "enter Play mode", [](const json&, json& r, std::string& e) {
			if (!Application::IsPlaying())
			{
				if (!ScriptEngine::CanEnterPlayMode()) { e = "cannot enter Play mode: scripts are compiling or have errors"; return false; }
				Application::SetPaused(false);
				Application::SetPlaying(true);
				SceneManager::GetI()->HandlePlay();
			}
			r = { { "playing", true } };
			return true;
		});

		Register("stop", "leave Play mode", [](const json&, json& r, std::string&) {
			if (Application::IsPlaying())
			{
				Application::SetPlaying(false);
				Application::SetPaused(false);
				SelectionManager::ClearSelection();
				SceneManager::GetI()->HandleStop();
			}
			r = { { "playing", false } };
			return true;
		});

		Register("pause", "pause / resume Play mode {on?}", [](const json& a, json& r, std::string& e) {
			if (!Application::IsPlaying()) { e = "not playing"; return false; }
			Application::SetPaused(a.value("on", !Application::IsPaused()));
			r = { { "paused", Application::IsPaused() } };
			return true;
		});

		Register("step", "advance one frame while paused", [](const json&, json& r, std::string& e) {
			if (!Application::IsPlaying()) { e = "not playing"; return false; }
			Application::SetPaused(true);
			Application::RequestStep();
			r = { { "paused", true } };
			return true;
		});

		Register("undo", "undo the last change", [](const json&, json& r, std::string& e) {
			const std::string name = Undo::UndoName();
			if (!Undo::PerformUndo()) { e = "nothing to undo"; return false; }
			r = { { "undone", name } };
			return true;
		});

		Register("redo", "redo", [](const json&, json& r, std::string& e) {
			const std::string name = Undo::RedoName();
			if (!Undo::PerformRedo()) { e = "nothing to redo"; return false; }
			r = { { "redone", name } };
			return true;
		});

		Register("camera", "Scene view camera: get, or set {position, target} / {frame: target, distance?}", [](const json& a, json& r, std::string& e) {
			SceneEditorWindow* w = SceneWindow();
			EditorCamera* cam = w ? w->GetSceneCamera() : nullptr;
			if (!cam) { e = "Scene view is not open"; return false; }
			Vec3 pos, target;
			if (a.contains("frame"))
			{
				GameObject* go = Resolve(a["frame"], e);
				if (!go) return false;
				target = go->GetTransform()->GetPosition();
				const XMFLOAT3 look = cam->GetLook();
				const float dist = a.value("distance", 8.0f);
				pos = Vec3(target.x - look.x * dist, target.y - look.y * dist, target.z - look.z * dist);
				cam->LookAt(XMFLOAT3(pos.x, pos.y, pos.z), XMFLOAT3(target.x, target.y, target.z), XMFLOAT3(0, 1, 0));
			}
			else if (a.contains("position"))
			{
				if (!ReadVec3(a["position"], pos)) { e = "position must be [x,y,z]"; return false; }
				if (a.contains("target"))
				{
					if (!ReadVec3(a["target"], target)) { e = "target must be [x,y,z]"; return false; }
				}
				else
				{
					const XMFLOAT3 look = cam->GetLook();
					target = Vec3(pos.x + look.x, pos.y + look.y, pos.z + look.z);
				}
				cam->LookAt(XMFLOAT3(pos.x, pos.y, pos.z), XMFLOAT3(target.x, target.y, target.z), XMFLOAT3(0, 1, 0));
			}
			cam->UpdateViewMatrix();
			const XMFLOAT3 p = cam->GetPosition(), l = cam->GetLook();
			r = { { "position", { p.x, p.y, p.z } }, { "forward", { l.x, l.y, l.z } } };
			return true;
		});

		// 스크린샷: 앞 명령(카메라 이동 등)이 그려진 뒤에 찍도록 3 프레임 기다린다
		Register("screenshot", "save the Scene or Game view to PNG/JPG {path, view: scene|game}", [](const json& a, json& r, std::string& e) {
			const std::string view = Lower(a.value("view", std::string("scene")));
			GfxTexture2D* tex = nullptr;
			if (view == "game")
			{
				auto* w = dynamic_cast<GameViewEditorWindow*>(EditorGUIManager::GetI()->FindWindow("Game"));
				tex = w ? w->GetTexture() : nullptr;
			}
			else
			{
				SceneEditorWindow* w = SceneWindow();
				tex = w ? w->GetRenderTexture() : nullptr;
			}
			if (!tex) { e = "the " + view + " view has not been drawn (is its tab visible? is the editor minimized?)"; return false; }
			const std::string path = a.value("path", std::string());
			if (path.empty()) { e = "missing path"; return false; }
			const std::wstring file = ProjectFile(path);
			int w = 0, h = 0;
			if (!SaveTexture(tex, file, w, h, e)) return false;
			r = { { "path", wstring_to_string(file) }, { "width", w }, { "height", h }, { "view", view } };
			return true;
		}, 3);

		// 에디터 전체 (메뉴·창·팝업까지): 백버퍼가 다 그려진 뒤 Present 직전에 찍는다
		Register("screenshot-editor", "save the whole editor window (UI included) to PNG/JPG {path}", [](const json& a, json& r, std::string& e) {
			App* app = Application::GetI()->GetApp();
			ComPtr<GfxTexture2D> back;
			if (app && (app->IsOpenGL() || app->IsVulkan()))
				back = app->BackBufferTexture();   // OpenGL · Vulkan: 엔진이 그리는 백버퍼 텍스처
			else
			{
				IDXGISwapChain* swap = app ? app->SwapChain() : nullptr;
				if (!swap) { e = "no swap chain"; return false; }
				ComPtr<ID3D11Texture2D> d3dBack;   // 스왑 체인은 DXGI 객체 → 진짜 D3D11 텍스처로 받아 Gfx 로 감싼다
				if (FAILED(swap->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(d3dBack.GetAddressOf())))) { e = "no back buffer"; return false; }
				back.Attach(Gfx::WrapD3D11As<GfxTexture2D>(d3dBack.Get()));
			}
			if (!back) { e = "no back buffer"; return false; }
			const std::string path = a.value("path", std::string());
			if (path.empty()) { e = "missing path"; return false; }
			const std::wstring file = ProjectFile(path);
			int w = 0, h = 0;
			if (!SaveTexture(back.Get(), file, w, h, e)) return false;
			r = { { "path", wstring_to_string(file) }, { "width", w }, { "height", h }, { "view", "editor" } };
			return true;
		}, 2, true);

		// 에디터 창 열기·닫기 (UI 확인용: 마우스 없이 분류까지)
		Register("window", "open / close an editor window {name: preferences|project-settings|build-settings, category?, close?}", [](const json& a, json& r, std::string& e) {
			const std::string name = Lower(a.value("name", std::string()));
			const bool close = a.value("close", false);
			const std::string category = a.value("category", std::string());
			if (name == "preferences")
			{
				if (close) PreferencesWindow::Close(); else PreferencesWindow::Open(category.empty() ? nullptr : category.c_str());
				r = { { "name", name }, { "open", PreferencesWindow::IsOpen() } };
			}
			else if (name == "project-settings")
			{
				if (close) ProjectSettingsWindow::Close(); else ProjectSettingsWindow::Open(category.empty() ? "Graphics" : category.c_str());
				r = { { "name", name }, { "open", ProjectSettingsWindow::IsOpen() } };
			}
			else if (name == "build-settings")
			{
				if (close) BuildSettingsWindow::Close(); else BuildSettingsWindow::Open();
				r = { { "name", name }, { "open", BuildSettingsWindow::IsOpen() } };
			}
			else if (name == "audio-mixer" || name == "mixer")
			{
				// --category <Assets\X.mixer> 로 열 믹서
				if (close) AudioMixerWindow::Close(); else AudioMixerWindow::Open(category);
				r = { { "name", name }, { "open", AudioMixerWindow::IsOpen() } };
			}
			else if (name == "package-manager" || name == "packages")
			{
				if (close) PackageManagerWindow::Close(); else PackageManagerWindow::Open(category.empty() ? nullptr : category.c_str());
				r = { { "name", name }, { "open", PackageManagerWindow::IsOpen() } };
			}
			else if (EditorWindow* w = [&]() -> EditorWindow* {
						// 도킹 창 (scene, game, project, console, hierarchy, inspector …): 탭을 앞으로
						for (const char* t : { "Scene", "Game", "Project", "Console", "Hierarchy", "Inspector", "Animator", "Profiler" })
							if (Lower(t) == name) return EditorGUIManager::GetI()->FindWindow(t);
						return nullptr;
					}())
			{
				if (a.value("float", false))
				{
					// 도킹에서 떼어 따로 떠 있는 OS 창으로 (뷰포트 시험)
					w->RequestFloat(ImVec2(80, 80), ImVec2(420, 520));
					r = { { "name", name }, { "floating", true } };
				}
				else
				{
					EditorGUIManager::GetI()->SelectTab(w->GetTitle());
					r = { { "name", name }, { "selected", true } };
				}
			}
			else
			{
				e = "unknown window '" + name + "' (preferences, project-settings, build-settings, package-manager, audio-mixer, scene, game, project, console, hierarchy, inspector, animator)";
				return false;
			}
			if (!category.empty()) r["category"] = category;
			return true;
		});

		// 그래픽 API 설정: 에디터 설정 + 플레이어 목록 읽기·쓰기, 이번 실행이 고른 API
		Register("graphics", "graphics API settings: get, or set {editor?: DirectX11|OpenGL, player?: [..], auto?}", [](const json& a, json& r, std::string& e) {
			auto parse = [&](const json& v, GraphicsAPI& out) {
				if (!v.is_string()) return false;
				const std::string k = Lower(v.get<std::string>());
				for (GraphicsAPI api : GraphicsSettings::AllAPIs())
					if (k == Lower(GraphicsAPIToKey(api)) || k == Lower(GraphicsAPIToString(api)) || (k == "d3d11" && api == GraphicsAPI::DirectX11) || (k == "gl" && api == GraphicsAPI::OpenGL))
					{
						out = api;
						return true;
					}
				return false;
			};
			if (a.contains("editor"))
			{
				GraphicsAPI api;
				if (!parse(a["editor"], api)) { e = "unknown API " + a["editor"].dump(); return false; }
				GraphicsSettings::SetEditorAPI(api);
			}
			BuildSettings::Player& p = BuildSettings::GetPlayer();
			bool playerChanged = false;
			if (a.contains("player"))
			{
				if (!a["player"].is_array() || a["player"].empty()) { e = "player must be a non-empty list"; return false; }
				std::vector<GraphicsAPI> list;
				for (const json& v : a["player"])
				{
					GraphicsAPI api;
					if (!parse(v, api)) { e = "unknown API " + v.dump(); return false; }
					if (std::find(list.begin(), list.end(), api) == list.end())
						list.push_back(api);
				}
				p.GraphicsAPIs = list;
				p.AutoGraphicsAPI = false;
				playerChanged = true;
			}
			if (a.contains("auto"))
			{
				p.AutoGraphicsAPI = a["auto"].get<bool>();
				playerChanged = true;
			}
			if (playerChanged)
				BuildSettings::SavePlayer();
			json apis = json::array();
			for (GraphicsAPI api : GraphicsSettings::AllAPIs())
			{
				std::string reason;
				const bool ok = GraphicsSettings::IsSupported(api, &reason);
				apis.push_back({ { "api", GraphicsAPIToKey(api) }, { "available", ok }, { "reason", reason } });
			}
			json list = json::array();
			for (GraphicsAPI api : p.GraphicsAPIs) list.push_back(GraphicsAPIToKey(api));
			json order = json::array();
			for (GraphicsAPI api : BuildSettings::PlayerGraphicsAPIs()) order.push_back(GraphicsAPIToKey(api));
			r = {
				{ "active", GraphicsAPIToKey(GraphicsSettings::GetActiveAPI()) },
				{ "selection", GraphicsSettings::SelectionLog() },
				{ "editor", GraphicsAPIToKey(GraphicsSettings::GetEditorAPI()) },
				{ "playerAuto", p.AutoGraphicsAPI },
				{ "playerList", list },
				{ "playerOrder", order },
				{ "apis", apis },
			};
			return true;
		});

		// 셰이더 자동 변환 검사: .fx → (DXC) SPIR-V → (SPIRV-Cross) GLSL 4.50. 파일마다 pass 성공 수, 실패 이유, (out 이 있으면) GLSL 파일
		// RHI 비교: 같은 장면(InstancedBasic PBR)을 DirectX 11 과 OpenGL 로 그려 PNG + 픽셀 차이
		Register("rhi-test", "render the RHI test scene with DirectX 11 and/or OpenGL and compare {api? (both|DirectX11|OpenGL), out? (folder for PNGs), width?, height?}", [](const json& a, json& r, std::string& e) {
			const std::string api = Lower(a.value("api", std::string("both")));
			const int w = std::clamp(a.value("width", 960), 16, 4096), h = std::clamp(a.value("height", 540), 16, 4096);
			const std::string outDir = a.value("out", std::string());
			std::vector<GraphicsAPI> apis;
			if (api == "both" || api == "directx11" || api == "d3d11" || api == "dx11") apis.push_back(GraphicsAPI::DirectX11);
			if (api == "both" || api == "opengl" || api == "gl") apis.push_back(GraphicsAPI::OpenGL);
			if (apis.empty()) { e = "api must be both, DirectX11 or OpenGL"; return false; }
			std::vector<RhiTest::Result> results;
			json list = json::array();
			for (GraphicsAPI g : apis)
			{
				EditorLog::Heartbeat();
				json item = { { "api", GraphicsAPIToKey(g) } };
				std::string err;
				GLContext::KeepCurrent keep;   // OpenGL 시험 장치가 끝나면 에디터의 현재 컨텍스트로 되돌림
				std::unique_ptr<Rhi::Device> dev = Rhi::CreateDevice(g, err);
				RhiTest::Result res;
				if (dev && RhiTest::RenderLitScene(*dev, w, h, res, err))
				{
					item["device"] = res.Device;
					item["loadMs"] = res.LoadMs;
					item["drawMs"] = res.DrawMs;
					if (!outDir.empty())
					{
						const std::wstring file = (std::filesystem::path(string_to_wstring(outDir)) / (std::string("rhi_") + GraphicsAPIToKey(g) + ".png")).wstring();
						if (SavePng(res.Rgba, w, h, file, err)) item["png"] = wstring_to_string(file);
						else item["error"] = err;
					}
					results.push_back(std::move(res));
				}
				else
				{
					item["error"] = err;
					if (dev) item["device"] = dev->Description();
				}
				list.push_back(item);
			}
			r = { { "width", w }, { "height", h }, { "results", list } };
			if (results.size() == 2)
				r["diff"] = CompareImages(results[0].Rgba, results[1].Rgba, w, h, outDir, "rhi_diff.png");
			return true;
		});

		// Gfx 층 비교: 엔진 렌더러와 같은 방식(Gfx + FxEffect)으로 시험 장면을 엔진 장치(DirectX 11)와 OpenGL(숨은 창)에 그려 비교
		Register("gfx-test", "render the Gfx test scene (engine-style Gfx + effects) on the engine device and/or OpenGL and compare {api? (both|DirectX11|OpenGL), out?, width?, height?}", [](const json& a, json& r, std::string& e) {
			const std::string api = Lower(a.value("api", std::string("both")));
			const int w = std::clamp(a.value("width", 960), 16, 4096), h = std::clamp(a.value("height", 540), 16, 4096);
			const std::string outDir = a.value("out", std::string());
			std::vector<GraphicsAPI> apis;
			if (api == "both" || api == "directx11" || api == "d3d11" || api == "dx11") apis.push_back(GraphicsAPI::DirectX11);
			if (api == "both" || api == "opengl" || api == "gl") apis.push_back(GraphicsAPI::OpenGL);
			if (apis.empty()) { e = "api must be both, DirectX11 or OpenGL"; return false; }
			std::vector<GfxTest::Result> results;
			json list = json::array();
			for (GraphicsAPI g : apis)
			{
				EditorLog::Heartbeat();
				json item = { { "api", GraphicsAPIToKey(g) } };
				std::string err;
				GfxTest::Result res;
				bool ok = false;
				if (g == GraphicsAPI::DirectX11)
				{
					if (!Gfx::Device() || !Gfx::Device()->Native()) err = "the engine device is not DirectX 11";
					else ok = GfxTest::Render(Gfx::Device(), Gfx::Context(), Rhi::Main(), w, h, res, err);
				}
				else
				{
					// 숨은 창의 GL 장치 (끝나면 효과·자원 → RHI 장치 → 컨텍스트 → 장치 순으로 놓는다)
					GLContext::KeepCurrent keep;   // 끝나면 에디터의 현재 컨텍스트로 되돌림 (에디터가 OpenGL 이면 꼭 필요)
					ComPtr<GfxDevice> gdev;
					ComPtr<GfxContext> gctx;
					if (GfxGL::CreateDevice(nullptr, gdev.GetAddressOf(), gctx.GetAddressOf(), err))
					{
						std::unique_ptr<Rhi::Device> rhi = GfxGL::CreateRhiDevice(gdev.Get(), gctx.Get(), err);
						if (rhi)
							ok = GfxTest::Render(gdev.Get(), gctx.Get(), rhi.get(), w, h, res, err);
						rhi.reset();
					}
					gctx.Reset();
					gdev.Reset();
				}
				if (ok)
				{
					item["loadMs"] = res.LoadMs;
					item["drawMs"] = res.DrawMs;
					if (!outDir.empty())
					{
						const std::wstring file = (std::filesystem::path(string_to_wstring(outDir)) / (std::string("gfx_") + GraphicsAPIToKey(g) + ".png")).wstring();
						if (SavePng(res.Rgba, w, h, file, err)) item["png"] = wstring_to_string(file);
						else item["error"] = err;
					}
					results.push_back(std::move(res));
				}
				else
					item["error"] = err;
				list.push_back(item);
			}
			r = { { "width", w }, { "height", h }, { "results", list } };
			if (results.size() == 2)
				r["diff"] = CompareImages(results[0].Rgba, results[1].Rgba, w, h, outDir, "gfx_diff.png");
			return true;
		});

		Register("shader-cross", "convert engine .fx shaders to GLSL {file? (name part, default all), out? (folder for .glsl files), errors? (max per file)}", [](const json& a, json& r, std::string& e) {
			std::string dxcError;
			if (!ShaderCross::Available(&dxcError)) { e = dxcError; return false; }
			const std::string filter = Lower(a.value("file", std::string()));
			const std::string outDir = a.value("out", std::string());
			const int maxErrors = a.value("errors", 3);
			const std::filesystem::path shaders = std::filesystem::path(PathManager::GetI()->GetEnginePathW()) / L"Shaders";
			std::vector<std::filesystem::path> files;
			std::error_code ec;
			for (const auto& f : std::filesystem::directory_iterator(shaders, ec))
				if (f.path().extension() == L".fx" && (filter.empty() || Lower(wstring_to_string(f.path().filename().wstring())).find(filter) != std::string::npos))
					files.push_back(f.path());
			std::sort(files.begin(), files.end());
			int totalPasses = 0, okPasses = 0, failedFiles = 0;
			json list = json::array();
			const auto t0 = std::chrono::steady_clock::now();
			for (const auto& f : files)
			{
				EditorLog::Heartbeat();   // 오래 걸리는 진단 명령: 파일마다 살아 있음을 알려 [HANG] 오탐을 막는다
				ShaderCross::EffectGlsl fx;
				ShaderCross::CompileEffect(f.wstring(), fx);
				json item = { { "file", wstring_to_string(f.filename().wstring()) }, { "techniques", fx.Fx.Techniques.size() }, { "passes", fx.Passes.size() }, { "ok", fx.PassesOk() },
					{ "blocks", fx.Blocks.size() }, { "samplers", fx.Samplers.size() }, { "images", fx.Images.size() }, { "buffers", fx.Buffers.size() } };
				if (!fx.Error.empty())
				{
					item["error"] = fx.Error.substr(0, 600);
					++failedFiles;
				}
				json errs = json::array();
				for (const auto& p : fx.Passes)
					if (!p.Error.empty() && (int)errs.size() < maxErrors)
						errs.push_back(p.Technique + "/" + p.Pass + ": " + p.Error.substr(0, 600));
				if (!errs.empty())
					item["errors"] = errs;
				totalPasses += (int)fx.Passes.size();
				okPasses += fx.PassesOk();
				if (!outDir.empty())
				{
					const std::filesystem::path dir = std::filesystem::path(string_to_wstring(outDir)) / f.stem();
					std::filesystem::create_directories(dir, ec);
					for (const auto& p : fx.Passes)
						for (const auto& s : p.Stages)
						{
							static const char* ext[] = { ".vert", ".tesc", ".tese", ".geom", ".frag", ".comp" };
							std::ofstream(dir / (p.Technique + "_" + p.Pass + ext[(int)s.StageType] + ".glsl"), std::ios::trunc) << s.Glsl;
						}
				}
				list.push_back(item);
			}
			r = { { "files", files.size() }, { "failedFiles", failedFiles }, { "passes", totalPasses }, { "passesOk", okPasses },
				{ "ms", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() }, { "results", list } };
			return true;
		});

		Register("assets", "list project files {path? (default Assets), pattern? (substring or .ext), limit?}", [](const json& a, json& r, std::string& e) {
			const std::string dir = a.value("path", std::string("Assets"));
			const std::string pattern = Lower(a.value("pattern", std::string()));
			const int limit = a.value("limit", 500);
			const std::wstring root = ProjectFile(dir);
			std::error_code ec;
			if (!std::filesystem::is_directory(root, ec)) { e = "not a folder: " + dir; return false; }
			r = json::array();
			for (auto it = std::filesystem::recursive_directory_iterator(root, ec); it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
			{
				if (ec || !it->is_regular_file(ec)) continue;
				const std::string rel = wstring_to_string(PathManager::GetI()->GetCutSolutionPath(it->path().wstring()));
				if (!pattern.empty() && Lower(rel).find(pattern) == std::string::npos) continue;
				r.push_back(rel);
				if ((int)r.size() >= limit) break;
			}
			return true;
		});

		Register("build", "start a player build {output, run?} (poll with build-status)", [](const json& a, json& r, std::string& e) {
			if (!RequireEditMode(e)) return false;
			if (BuildPipeline::IsRunning()) { e = "a build is already running"; return false; }
			BuildPipeline::Options o;
			o.OutputFolder = string_to_wstring(a.value("output", std::string()));
			if (o.OutputFolder.empty()) { e = "missing output folder"; return false; }
			o.Run = a.value("run", false);
			o.Reveal = false;
			if (!BuildPipeline::Start(o, e)) return false;
			r = { { "started", true }, { "output", a["output"] } };
			return true;
		});

		// Build Settings 의 Scenes In Build (Unity 의 EditorBuildSettings.scenes): 차례 = 빌드 번호
		Register("build-scenes", "Build Settings scenes {op: list|set|add|remove, scenes: \"Assets/A.scene,Assets/B.scene\"}", [](const json& a, json& r, std::string& e) {
			const std::string op = a.value("op", std::string("list"));
			std::vector<std::string> names;
			if (a.contains("scenes"))
			{
				const json& s = a["scenes"];
				if (s.is_array()) { for (const json& v : s) if (v.is_string()) names.push_back(v.get<std::string>()); }
				else if (s.is_string())
				{
					std::stringstream ss(s.get<std::string>());
					for (std::string item; std::getline(ss, item, ',');)
						if (!item.empty()) names.push_back(item);
				}
			}
			for (std::string& n : names)
				std::replace(n.begin(), n.end(), '/', '\\');
			auto& scenes = BuildSettings::Scenes();
			auto find = [&](const std::string& p) {
				return std::find_if(scenes.begin(), scenes.end(), [&](const BuildSettings::SceneEntry& s) { return _stricmp(s.Path.c_str(), p.c_str()) == 0; });
			};
			if (op == "set" || op == "add")
			{
				if (op == "set") scenes.clear();
				for (const std::string& n : names)
				{
					if (!std::filesystem::exists(PathManager::GetI()->GetMovePathW(string_to_wstring(n)))) { e = "scene not found: " + n; return false; }
					if (find(n) == scenes.end()) scenes.push_back({ n, true });
				}
				BuildSettings::SaveScenes();
			}
			else if (op == "remove")
			{
				for (const std::string& n : names)
					if (auto it = find(n); it != scenes.end()) scenes.erase(it);
				BuildSettings::SaveScenes();
			}
			else if (op != "list") { e = "op must be list, set, add or remove"; return false; }
			json list = json::array();
			for (size_t i = 0; i < scenes.size(); ++i)
				list.push_back({ { "index", i }, { "path", scenes[i].Path }, { "enabled", scenes[i].Enabled } });
			r = { { "scenes", list } };
			return true;
		});

		Register("build-status", "player build progress", [](const json&, json& r, std::string&) {
			r = { { "running", BuildPipeline::IsRunning() }, { "status", BuildPipeline::Status() } };
			return true;
		});

		// ---- 자동 저장 / 충돌 복구 ----
		Register("autosave", "auto save {action: status|now|recover|discard}", [](const json& a, json& r, std::string& e) {
			const std::string action = a.value("action", "status");
			if (action == "now")
			{
				if (!AutoSave::SaveNow(&e)) return false;
			}
			else if (action == "recover" || action == "discard")
			{
				if (!AutoSave::HasPendingRecovery()) { e = "nothing to recover"; return false; }
				if (!AutoSave::Recover(action == "recover")) { e = "recovery failed (see nova log)"; return false; }
			}
			else if (action != "status") { e = "usage: autosave status|now|recover|discard"; return false; }
			r = { { "enabled", AutoSave::Enabled() }, { "intervalMinutes", AutoSave::IntervalMinutes() },
				{ "folder", wstring_to_string(AutoSave::Folder()) }, { "pendingRecovery", AutoSave::PendingRecoveryInfo() } };
			return true;
		});

		// ---- 패키지 (Unity Package Manager) ----
		Register("package-list", "registry packages and the project's packages", [](const json&, json& r, std::string&) {
			json list = json::array();
			auto add = [&](const PackageInfo& p) {
				json c = json::array();
				for (const auto& ci : p.Components) c.push_back(ci.Type);
				list.push_back({ { "name", p.Name }, { "displayName", p.DisplayName }, { "version", p.Version }, { "embedded", p.Embedded }, { "local", p.Local },
					{ "inProject", PackageManager::IsInProject(p.Name) }, { "loaded", PackageManager::IsLoaded(p.Name) },
					{ "error", PackageManager::LoadError(p.Name) }, { "components", c } });
			};
			for (const PackageInfo& p : PackageManager::Registry()) add(p);
			for (const PackageInfo* p : PackageManager::InProject()) if (p->Embedded || p->Local) add(*p);
			r = { { "packages", list }, { "manifest", wstring_to_string(PackageManager::ManifestPath()) },
				{ "registry", wstring_to_string(PackageManager::RegistryFolder()) }, { "restartRequired", PackageManager::RestartRequired() } };
			return true;
		});
		Register("package-add", "add a package to the project {name (registry name, or a folder / package.json path = from disk)}", [](const json& a, json& r, std::string& e) {
			std::string name = a.value("name", "");
			if (name.find('\\') != std::string::npos || name.find('/') != std::string::npos)
			{
				std::string added;
				if (!PackageManager::AddFromDisk(string_to_wstring(name), e, &added)) return false;
				r = { { "added", added }, { "fromDisk", true }, { "loaded", PackageManager::IsLoaded(added) } };
				return true;
			}
			if (!PackageManager::Add(name, e)) return false;
			r = { { "added", name }, { "loaded", PackageManager::IsLoaded(name) } };
			return true;
		});
		Register("package-remove", "remove a package from the project {name}", [](const json& a, json& r, std::string& e) {
			const std::string name = a.value("name", "");
			if (!PackageManager::Remove(name, e)) return false;
			r = { { "removed", name }, { "restartRequired", PackageManager::RestartRequired() } };
			return true;
		});

		Register("quit", "close the editor {force? (discard unsaved changes)}", [](const json& a, json& r, std::string& e) {
			if (Application::IsPlaying())
			{
				Application::SetPlaying(false);
				SceneManager::GetI()->HandleStop();
			}
			if (SceneManager::GetI()->IsCurrentSceneDirty())
			{
				if (!a.value("force", false)) { e = "the scene has unsaved changes (nova scene save, or quit --force)"; return false; }
				SceneManager::GetI()->CancelScenePrompt();   // 닫기 확인 창이 떠 있어도 버리고 닫는다
				SceneManager::GetI()->DiscardChanges();
				SceneManager::GetI()->SetCloseWithoutPrompt();   // Untitled 씬은 되돌릴 파일이 없어 여전히 변경됨 → 묻지 않고 닫기
			}
			::PostMessageW(Application::GetI()->GetMainHwnd(), WM_CLOSE, 0, 0);
			r = { { "closing", true } };
			return true;
		});
	}
}
