#include "pch.h"
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
#include "Collider.h"

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
			{ "tag", go->GetTag() }, { "layer", (int)go->GetLayerIndex() }, { "static", go->IsStatic() },
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
		if (type == "audio-source") return named(GameObjectFactory::CreateAudioSource());
		if (type == "rock") return named(GameObjectFactory::CreateRock(args.value("preset", 2)));
		if (type == "rock-scatter") return named(GameObjectFactory::CreateRockScatter(args.value("preset", 2)));
		if (type == "ocean") return named(GameObjectFactory::CreateWaterBody(0));
		if (type == "lake") return named(GameObjectFactory::CreateWaterBody(1));
		if (type == "river") return named(GameObjectFactory::CreateWaterBody(2));
		if (type == "volume") return named(GameObjectFactory::CreateVolume(GameObjectFactory::VolumeShape::Global));
		if (type == "character") return named(GameObjectFactory::CreateAnimatedCharacter());   // 기본 캐릭터 (스킨 메시 + Animator)
		error = "unknown type '" + typeIn + "' (empty, cube, sphere, capsule, cylinder, plane, quad, directional-light, point-light, spot-light, "
			"camera, terrain, tree, rock, rock-scatter, ocean, lake, river, particle-system, audio-source, volume, character)";
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
			const double wallMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - s_PerfStart).count();
			Profiler::ForceCollecting(false);
			int frames = 0, gpuFrames = 0;
			double cpu = 0, gpu = 0, cpuMax = 0;
			std::map<std::string, std::pair<double, int>> passes, cpuScopes;
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
						if (g.Depth <= 1)
						{
							auto& p = passes[std::string(g.Depth, '.') + g.Name];
							p.first += g.Ms;
							++p.second;
						}
				}
			}
			if (frames == 0) { e = "no frames recorded (run nova perf, not perf directly)"; return false; }
			std::vector<std::pair<double, std::string>> top;
			for (auto& [name, p] : passes)
				top.push_back({ p.first / (std::max)(1, gpuFrames), name });
			std::sort(top.rbegin(), top.rend());
			json list = json::array();
			for (size_t i = 0; i < top.size() && i < 12; ++i)
				list.push_back({ { "pass", top[i].second }, { "ms", std::round(top[i].first * 1000.0) / 1000.0 } });
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

		Register("raycast", "Physics.Raycast (Play mode) {origin, direction, maxDistance?, triggers?}", [](const json& a, json& r, std::string& e) {
			if (!Application::IsPlaying()) { e = "raycast needs Play mode (the physics world exists only while playing)"; return false; }
			Vec3 origin, dir;
			if (!a.contains("origin") || !ReadVec3(a["origin"], origin) || !a.contains("direction") || !ReadVec3(a["direction"], dir))
			{
				e = "origin and direction are required (x,y,z)";
				return false;
			}
			RaycastHit hit;
			if (!PhysicsManager::GetI()->Raycast(origin, dir, hit, a.value("maxDistance", 1000.0f), a.value("triggers", false)))
			{
				r = { { "hit", false } };
				return true;
			}
			r = { { "hit", true }, { "object", hit.gameObject ? PathOf(hit.gameObject) : "" }, { "collider", hit.collider ? hit.collider->InspectorTitle() : "" },
				{ "point", { hit.point.x, hit.point.y, hit.point.z } }, { "normal", { hit.normal.x, hit.normal.y, hit.normal.z } }, { "distance", hit.distance } };
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
			if (a.contains("layer")) go->SetLayerIndex((uint8)std::clamp(a["layer"].get<int>(), 0, 31));
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
			GameObject* go = CreateByType(a.value("type", std::string("empty")), a, e);
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

		Register("select", "select an object in the editor {target} (none = clear)", [](const json& a, json& r, std::string& e) {
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
			SelectionManager::ClearSelection();
			SceneManager::GetI()->LoadScene(PathManager::GetI()->GetCutSolutionPath(file));
			Scene* scene = CurrentScene();
			r = { { "scene", scene ? wstring_to_string(scene->GetScenePath()) : "" }, { "objects", scene ? scene->GetAllGameObjects().size() : 0 } };
			return true;
		});

		Register("scene-save", "save the current scene", [](const json&, json& r, std::string& e) {
			if (!RequireEditMode(e)) return false;
			Scene* scene = CurrentScene();
			if (!scene) { e = "no scene is open"; return false; }
			if (scene->GetScenePath().empty()) { e = "the scene has never been saved (use File > Save As in the editor once)"; return false; }
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
			if (app && app->IsOpenGL())
				back = app->BackBufferTexture();   // OpenGL: 엔진이 그리는 백버퍼 텍스처
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
				e = "unknown window '" + name + "' (preferences, project-settings, build-settings, scene, game, project, console, hierarchy, inspector, animator)";
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

		Register("build-status", "player build progress", [](const json&, json& r, std::string&) {
			r = { { "running", BuildPipeline::IsRunning() }, { "status", BuildPipeline::Status() } };
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
			}
			::PostMessageW(Application::GetI()->GetMainHwnd(), WM_CLOSE, 0, 0);
			r = { { "closing", true } };
			return true;
		});
	}
}
