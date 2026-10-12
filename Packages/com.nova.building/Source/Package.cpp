// com.nova.building 진입점: 컴포넌트 Building (게임 빌드에도 — DLL 을 불러올 때 등록), Window > Building Generator,
//  GameObject > 3D Object > Building > 양식, .building 프리셋 에셋, CLI "building" (styles · check · create · set · info · window · preset · materials)
//  재질은 텍스처 팩 (com.nova.textures — 종속 패키지) 의 id 로 (TextureLibrary)
#include "pch.h"
#include "Building.h"
#include "BuildingDesc.h"
#include "BuildingGenerator.h"
#include "BuildingTextures.h"
#include "BuildingGeneratorWindow.h"
#include "CliServer.h"
#include "EditorExtensions.h"
#include "EditorGUIManager.h"
#include "SceneManager.h"
#include "Scene.h"
#include "GameObject.h"
#include "Transform.h"
#include "SelectionManager.h"
#include "UndoSystem.h"
#include "TextureLibrary.h"
#include <chrono>

NOVA_PACKAGE_EXPORT const char* NovaPackage_Abi() { return NOVA_PACKAGE_ABI_VERSION; }

namespace
{
	constexpr const char* kPackage = "com.nova.building";
	using json = nlohmann::json;

	std::string PathOf(GameObject* go)
	{
		std::string p = go->GetName();
		for (GameObject* q = go->GetParent(); q; q = q->GetParent())
			p = q->GetName() + "/" + p;
		return p;
	}

	// 대상: "#fileID" 또는 이름 (장면의 모든 오브젝트에서)
	GameObject* FindTarget(const json& t, std::string& error)
	{
		Scene* scene = SceneManager::GetI()->GetCurrentScene();
		if (!scene) { error = "no scene is open"; return nullptr; }
		const std::string name = t.is_string() ? t.get<std::string>() : t.dump();
		std::vector<GameObject*> found;
		std::function<void(GameObject*)> walk = [&](GameObject* go) {
			if (!go) return;
			if ((name[0] == '#' && "#" + std::to_string(go->GetFileID()) == name) || go->GetName() == name || PathOf(go) == name)
				found.push_back(go);
			for (GameObject* c : go->GetChildren())
				walk(c);
		};
		for (GameObject* go : scene->GameObjectsView())
			if (go && go->GetParent() == nullptr)
				walk(go);
		if (found.empty()) { error = "no object named '" + name + "'"; return nullptr; }
		if (found.size() > 1) { error = "ambiguous: " + std::to_string(found.size()) + " objects named '" + name + "' (use #fileID)"; return nullptr; }
		return found[0];
	}

	bool Vec3Arg(const json& a, const char* key, Vec3& out)
	{
		if (!a.contains(key)) return false;
		const json& v = a[key];
		if (v.is_array() && v.size() >= 3) { out = Vec3(v[0].get<float>(), v[1].get<float>(), v[2].get<float>()); return true; }
		if (v.is_string())
		{
			float x, y, z;
			if (sscanf_s(v.get<std::string>().c_str(), "%f,%f,%f", &x, &y, &z) == 3) { out = Vec3(x, y, z); return true; }
		}
		return false;
	}

	void Place(GameObject* go, const json& a)
	{
		Vec3 v;
		if (Vec3Arg(a, "position", v)) go->GetTransform()->SetPosition(v);
		if (Vec3Arg(a, "rotation", v)) go->GetTransform()->SetLocalEulerAngles(v);
		if (Vec3Arg(a, "scale", v)) go->GetTransform()->SetLocalScale(v);
	}

	void AfterEdit(const std::string& name, GameObject* go)
	{
		Undo::SetActionName(name);
		if (go) Undo::Touch(go);
		Undo::RequestCheck();
	}

	bool EditMode(std::string& e)
	{
		if (!Application::IsPlaying()) return true;
		e = "not in Play mode (stop first)";
		return false;
	}

	// 값 덮어쓰기 (묶음 안 키만 — 나머지는 그대로). {"style": 이름} 만 있으면 그 양식으로
	void Merge(BuildingDesc& d, const json& values)
	{
		if (values.contains("style") && !values.contains("shape") && values.size() <= 2)
		{
			BuildingDesc n;
			n.Seed = values.value("seed", d.Seed);
			n.ApplyStyle(values["style"].is_string() ? (std::max)(0, BuildingDesc::FindStyle(values["style"].get<std::string>())) : values["style"].get<int>());
			d = n;
			return;
		}
		json j = d.ToJson();
		for (auto it = values.begin(); it != values.end(); ++it)
		{
			if (it.value().is_object() && j.contains(it.key()) && j[it.key()].is_object())
				for (auto k = it.value().begin(); k != it.value().end(); ++k)
					j[it.key()][k.key()] = k.value();
			else
				j[it.key()] = it.value();
		}
		d.FromJson(j);
	}

	json Info(Building* b)
	{
		int count = 0;
		const char* const* names = BuildingDesc::StyleNames(count);
		const Building::Stats& st = b->GetStats();
		json mats = json::object();
		for (int s = 0; s < BuildingGenerator::SlotCount; ++s)
			mats[BuildingGenerator::SlotKey(s)] = BuildingTextures::SlotMaterialId(b->Desc, s);
		return json{ { "object", b->GetGameObject()->GetName() }, { "style", b->Desc.Style >= 0 && b->Desc.Style < count ? names[b->Desc.Style] : "Custom" },
			{ "vertices", st.Vertices }, { "triangles", st.Triangles }, { "subsets", st.Subsets }, { "windows", st.Windows }, { "doors", st.Doors },
			{ "wings", st.Wings }, { "towers", st.Towers }, { "size", { st.Size.x, st.Size.y, st.Size.z } }, { "ms", st.Ms }, { "materials", mats },
			{ "desc", b->Desc.ToJson() } };
	}

	bool Run(const json& a, json& r, std::string& e)
	{
		const std::string op = a.value("op", std::string("styles"));
		int count = 0;
		const char* const* names = BuildingDesc::StyleNames(count);
		auto styleArg = [&](int fallback) {
			if (!a.contains("style")) return fallback;
			return a["style"].is_string() ? BuildingDesc::FindStyle(a["style"].get<std::string>()) : a["style"].get<int>();
		};
		if (op == "help")
		{
			r = { { "ops", { "styles", "check [--seeds 3]", "create --style <name|index> [--seed] [--name] [--position x,y,z] [--rotation] [--values {...}]",
				"set --target <name|#id> [--style] [--seed] [--values {...}]", "info --target <name>", "materials [--target <name>]",
				"window [--style] [--step 0..7] [--target] [--yaw --pitch --zoom]", "preset [--target <name> --name <preset> | --path Assets/Buildings/x.building]" } },
				{ "values", "BuildingDesc JSON groups: shape, towers, walls, windows, roof, details, colors, materials (slot -> texture id, see nova textures list)" } };
			return true;
		}
		if (op == "styles")
		{
			json list = json::array();
			for (int s = 0; s < count; ++s)
				list.push_back({ { "index", s }, { "name", names[s] }, { "description", BuildingDesc::StyleDescription(s) } });
			r = { { "count", count }, { "styles", list } };
			return true;
		}
		if (op == "materials" && !a.contains("target"))
		{
			// 재질 칸 (slot) 마다 기본 텍스처 id 와 고를 수 있는 분류
			json slots = json::array();
			BuildingDesc d;
			for (int s = 0; s < BuildingGenerator::SlotCount; ++s)
				slots.push_back({ { "slot", BuildingGenerator::SlotKey(s) }, { "name", BuildingGenerator::SlotName(s) },
					{ "suggested", BuildingTextures::SlotCategories(s) } });
			r = { { "slots", slots }, { "note", "set a slot: nova building set --target X --values {\"materials\":{\"roof\":\"roof/thatch-mossy\"}} (\"\" = style default, \"procedural\" = built-in)" } };
			return true;
		}
		if (op == "check")
		{
			json list = json::array();
			int bad = 0, built = 0;
			const int seeds = a.value("seeds", 3);
			for (int s = 0; s < count; ++s)
				for (int k = 0; k <= seeds; ++k)
				{
					BuildingDesc d;
					d.ApplyStyle(s);
					if (k > 0)
						d.Randomize(k * 101 + s);
					const auto t0 = std::chrono::steady_clock::now();
					BuildingGenerator::Output out;
					BuildingGenerator::Generate(d, out);
					const float ms = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count();
					++built;
					bool finite = true;
					size_t badIndex = 0;
					for (const auto& part : out.Parts)
					{
						for (const auto& v : part.V)
							finite &= std::isfinite(v.pos.x) && std::isfinite(v.pos.y) && std::isfinite(v.pos.z) && std::isfinite(v.normal.x) && std::isfinite(v.tex.x) && std::isfinite(v.tex.y);
						for (uint32_t i : part.I)
							badIndex += i >= part.V.size();
					}
					const float h = out.Max.y - out.Min.y, w = (std::max)(out.Max.x - out.Min.x, out.Max.z - out.Min.z);
					std::string issue;
					if (out.Triangles() == 0) issue = "empty mesh";
					else if (!finite) issue = "non-finite vertex";
					else if (badIndex) issue = "index out of range";
					else if (h < d.WallHeight() * 0.8f || h > d.WallHeight() * 4.0f + 40.0f) issue = "height " + std::to_string(h);
					else if (w < (std::min)(d.Width, d.Depth) * 0.8f || w > (std::max)(d.Width, d.Depth) * 3.0f + 24.0f) issue = "width " + std::to_string(w);
					else if (out.Doors == 0 && !d.Arcade && d.Shape != BuildingDesc::Plan::Bridge) issue = "no door";
					json j = { { "style", names[s] }, { "seed", k }, { "vertices", out.Vertices() }, { "triangles", out.Triangles() }, { "windows", out.Windows },
						{ "doors", out.Doors }, { "size", { out.Max.x - out.Min.x, h, out.Max.z - out.Min.z } }, { "ms", ms } };
					if (!issue.empty())
					{
						j["issue"] = issue;
						++bad;
					}
					if (k == 0 || !issue.empty())
						list.push_back(j);
				}
			r = { { "count", count }, { "built", built }, { "bad", bad }, { "buildings", list } };
			return true;
		}
		if (op == "window")
		{
			if (Application::IsPlayer() || !BuildingGeneratorWindow::Instance()) { e = "the Building Generator window is not available"; return false; }
			Building* target = nullptr;
			if (a.contains("target"))
			{
				GameObject* go = FindTarget(a["target"], e);
				if (!go) return false;
				target = go->GetComponent<Building>();
				if (!target) { e = "'" + go->GetName() + "' has no Building"; return false; }
			}
			BuildingGeneratorWindow::Open(target, a.value("step", -1));
			BuildingGeneratorWindow* w = BuildingGeneratorWindow::Instance();
			const int style = styleArg(-1);
			if (style >= 0)
			{
				w->Desc().ApplyStyle(style);
				if (a.value("seed", 0) > 0)
					w->Desc().Randomize(a.value("seed", 0));
			}
			if (a.contains("values"))
				Merge(w->Desc(), a["values"]);
			w->Changed();
			w->SetView(a.value("yaw", -32.0f), a.value("pitch", 18.0f), a.value("zoom", 1.0f));
			r = { { "step", w->Step() }, { "style", w->Desc().Style }, { "desc", w->Desc().ToJson() } };
			return true;
		}
		if (op == "preset")
		{
			const std::string path = a.value("path", std::string());
			if (a.contains("target"))
			{
				GameObject* go = FindTarget(a["target"], e);
				if (!go) return false;
				Building* b = go->GetComponent<Building>();
				if (!b) { e = "'" + go->GetName() + "' has no Building"; return false; }
				if (!path.empty())
				{
					if (!EditMode(e)) return false;
					BuildingDesc d = b->Desc;
					if (!BuildingGeneratorWindow::LoadPreset(path, d)) { e = "cannot read " + path; return false; }
					b->SetDesc(d);
					b->Rebuild();
					AfterEdit("Load Building Preset", go);
					r = Info(b);
					r.erase("desc");
					return true;
				}
				std::string saved;
				if (!BuildingGeneratorWindow::SavePreset(a.value("name", go->GetName()), b->Desc, saved)) { e = "cannot save the preset"; return false; }
				r = { { "path", saved } };
				return true;
			}
			json list = json::array();
			for (const std::string& p : BuildingGeneratorWindow::ListPresets())
				list.push_back(p);
			r = { { "presets", list } };
			return true;
		}
		if (op == "create")
		{
			if (!EditMode(e)) return false;
			Scene* scene = SceneManager::GetI()->GetCurrentScene();
			if (!scene) { e = "no scene is open"; return false; }
			const int style = styleArg(0);
			if (style < 0 || style >= count) { e = "unknown style (nova building styles)"; return false; }
			BuildingDesc d;
			d.ApplyStyle(style);
			if (a.value("seed", 0) > 0)
				d.Randomize(a.value("seed", 0));
			if (a.contains("values"))
				Merge(d, a["values"]);
			GameObject* go = Building::CreateObject(d, a.contains("name") && a["name"].is_string() ? a["name"].get<std::string>() : std::string(names[style]));
			scene->AddRootGameObject(go);
			Place(go, a);
			if (a.value("select", true))
				SelectionManager::SetSelectedGameObject(go);
			AfterEdit("Create " + go->GetName(), go);
			r = Info(go->GetComponent<Building>());
			r["path"] = PathOf(go);
			r["id"] = "#" + std::to_string(go->GetFileID());
			r.erase("desc");
			return true;
		}
		// set · info · materials: 대상 건물
		if (!a.contains("target")) { e = "--target is required"; return false; }
		GameObject* go = FindTarget(a["target"], e);
		if (!go) return false;
		Building* b = go->GetComponent<Building>();
		if (!b) { e = "'" + go->GetName() + "' has no Building"; return false; }
		if (op == "set")
		{
			if (!EditMode(e)) return false;
			BuildingDesc d = b->Desc;
			const int style = styleArg(-1);
			if (style >= 0)
				d.ApplyStyle(style);
			if (a.value("seed", 0) > 0)
				d.Randomize(a.value("seed", 0));
			if (a.contains("values"))
				Merge(d, a["values"]);
			b->SetDesc(d);
			b->Rebuild();
			AfterEdit("Set Building " + go->GetName(), go);
			r = Info(b);
			r.erase("desc");
			return true;
		}
		if (op == "info" || op == "materials")
		{
			r = Info(b);
			if (op == "materials")
				r = { { "object", r["object"] }, { "materials", r["materials"] } };
			return true;
		}
		e = "unknown op '" + op + "' (help, styles, materials, check, create, set, info, window, preset)";
		return false;
	}
}

NOVA_PACKAGE_EXPORT void NovaPackage_OnLoad()
{
	CliServer::Register("building", "building generator {op: help | styles | materials | check | create | set | info | window | preset, ...} (nova building help)",
		[](const json& args, json& result, std::string& error) { return Run(args, result, error); });
	if (Application::IsPlayer())
		return;   // 게임 빌드에는 편집기가 없다 (컴포넌트는 DLL 을 불러올 때 등록 — 저장된 건물이 다시 지어진다)
	BuildingGeneratorWindow::RegisterEditor(kPackage);
	int count = 0;
	const char* const* names = BuildingDesc::StyleNames(count);
	for (int s = 0; s < count; ++s)
	{
		EditorExtensions::CreateMenuItem item;
		item.Owner = kPackage;
		item.Path = std::string("3D Object/Building/") + names[s];
		item.Create = [s](Scene* scene, GameObject* parent) -> GameObject* {
			int n = 0;
			const char* const* styleNames = BuildingDesc::StyleNames(n);
			BuildingDesc d;
			d.ApplyStyle(s);
			GameObject* go = Building::CreateObject(d, styleNames[s]);
			if (parent)
				go->SetParent(parent, false);
			else if (scene)
				scene->AddRootGameObject(go);
			return go;
		};
		EditorExtensions::RegisterCreateMenu(item);
	}
}

NOVA_PACKAGE_EXPORT void NovaPackage_OnUnload()
{
	CliServer::Unregister("building");
	EditorExtensions::UnregisterOwner(kPackage);
	BuildingGeneratorWindow::UnregisterEditor();
	Building::s_OpenGenerator = nullptr;
	BuildingTextures::ClearCache();
}
