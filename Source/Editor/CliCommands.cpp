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
#include "BuildPipeline.h"
#include "Transform.h"
#include "EngineInfo.h"

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
	bool SaveTexture(ID3D11Texture2D* tex, const std::wstring& file, int& w, int& h, std::string& error)
	{
		auto device = Application::GetI()->GetDevice();
		auto ctx = Application::GetI()->GetDeviceContext();
		DirectX::ScratchImage captured;
		HRESULT hr = DirectX::CaptureTexture(device, ctx, tex, captured);
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
		error = "unknown type '" + typeIn + "' (empty, cube, sphere, capsule, cylinder, plane, quad, directional-light, point-light, spot-light, "
			"camera, terrain, tree, rock, rock-scatter, ocean, lake, river, particle-system, audio-source, volume)";
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
				{ "selection", sel ? json{ { "path", PathOf(sel) }, { "id", IdOf(sel) } } : json() },
				{ "canUndo", Undo::CanUndo() }, { "undo", Undo::UndoName() },
			};
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
			if (!c) { e = "unknown component type '" + type + "' (type names as in nova get output, e.g. RigidBody, BoxCollider, Light, AudioSource)"; return false; }
			go->AddComponent(c);
			if (a.contains("values") && a["values"].is_object())
			{
				json merged = c->toJson();
				merged.merge_patch(a["values"]);
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
			ID3D11Texture2D* tex = nullptr;
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
				SceneManager::GetI()->DiscardChanges();
			}
			::PostMessageW(Application::GetI()->GetMainHwnd(), WM_CLOSE, 0, 0);
			r = { { "closing", true } };
			return true;
		});
	}
}
