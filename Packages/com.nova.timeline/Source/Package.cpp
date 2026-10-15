// com.nova.timeline 진입점: Timeline 창 · .playable 에셋 · CLI (nova timeline) · C# API (Runtime/PlayableDirector.cs 가 DllImport("NovaTimeline"))
#include "pch.h"
#include "PlayableDirector.h"
#include "TimelineAsset.h"
#include "TimelineWindow.h"
#include "EditorExtensions.h"
#include "EditorGUIManager.h"
#include "ScriptBindings.h"
#include "CliServer.h"
#include "UnityGUI.h"

NOVA_PACKAGE_EXPORT const char* NovaPackage_Abi() { return NOVA_PACKAGE_ABI_VERSION; }

namespace
{
	constexpr const char* kPackage = "com.nova.timeline";
	TimelineWindow* s_Window = nullptr;

	PlayableDirector* FindDirector(uint64 gameObject)
	{
		GameObject* go = ScriptBindings::FindObject(gameObject);
		return go ? go->GetComponentIncludingPending<PlayableDirector>() : nullptr;
	}

	// CLI 대상: 이름 또는 #fileID
	GameObject* FindTarget(const std::string& name)
	{
		Scene* scene = SceneManager::GetI()->GetCurrentScene();
		if (scene == nullptr || name.empty())
			return nullptr;
		if (name[0] == '#')
			return scene->FindByFileID(std::strtoull(name.c_str() + 1, nullptr, 10));
		for (GameObject* go : scene->GetAllGameObjects())
			if (go && go->GetName() == name)
				return go;
		return nullptr;
	}

	double Num(const nlohmann::json& a, const char* k, double d)
	{
		if (!a.contains(k)) return d;
		const auto& v = a[k];
		return v.is_number() ? v.get<double>() : (v.is_string() ? atof(v.get<std::string>().c_str()) : (v.is_boolean() ? (v.get<bool>() ? 1.0 : 0.0) : d));
	}
	std::string Str(const nlohmann::json& a, const char* k)
	{
		if (!a.contains(k)) return std::string();
		const auto& v = a[k];
		return v.is_string() ? v.get<std::string>() : v.dump();
	}

	nlohmann::json Describe(TimelineAsset& asset, PlayableDirector* d)
	{
		nlohmann::json tracks = nlohmann::json::array();
		for (const TimelineTrack& t : asset.Tracks)
		{
			nlohmann::json clips = nlohmann::json::array();
			for (const TimelineClip& c : t.Clips)
				clips.push_back({ { "start", c.Start }, { "duration", c.Duration }, { "asset", c.Asset }, { "clip", c.ClipIndex }, { "easeIn", c.EaseIn }, { "easeOut", c.EaseOut } });
			nlohmann::json markers = nlohmann::json::array();
			for (const TimelineMarker& m : t.Markers)
				markers.push_back({ { "time", m.Time }, { "method", m.Method } });
			nlohmann::json tj = { { "id", t.Id }, { "type", t.Type }, { "name", t.Name }, { "muted", t.Muted }, { "clips", clips }, { "markers", markers } };
			if (d)
				if (GameObject* b = d->BoundObject(t))
					tj["binding"] = b->GetName();
			tracks.push_back(tj);
		}
		nlohmann::json r = { { "path", asset.Path }, { "duration", asset.Duration() }, { "frameRate", asset.FrameRate }, { "tracks", tracks } };
		if (d)
			r["director"] = { { "time", d->GetTime() }, { "playing", d->IsPlaying() }, { "wrapMode", d->WrapMode }, { "playOnAwake", d->PlayOnAwake } };
		return r;
	}

	bool RunCli(const nlohmann::json& a, nlohmann::json& r, std::string& e)
	{
		const std::string op = a.value("op", std::string("help"));
		if (op == "help")
		{
			r = { { "ops", { "create --path Assets/x.playable", "info --path P | --director GO",
				"add-track --path P --type Animation|Activation|Audio|Signal|Cinemachine [--name N]",
				"add-clip --path P --track T --start s --duration s [--asset file --clip i --clip-in s --speed x --ease-in s --ease-out s --loop --object GO]",
				"add-marker --path P --track T --time s --method M [--string --float --int --retroactive]",
				"director --director GO [--path P] [--play-on-awake true] [--wrap Hold|Loop|None]",
				"bind --director GO --track T --target GO", "play|pause|resume|stop --director GO", "time --director GO --value s (sets and evaluates)",
				"window [--director GO] (open the Timeline window on that director)" } } };
			return true;
		}
		PlayableDirector* d = nullptr;
		if (a.contains("director"))
		{
			GameObject* go = FindTarget(Str(a, "director"));
			if (go == nullptr) { e = "director object not found"; return false; }
			d = go->GetComponent<PlayableDirector>();
			if (d == nullptr && op == "director")
			{
				auto c = std::make_shared<PlayableDirector>();
				go->AddComponent(c);
				d = c.get();
			}
			if (d == nullptr) { e = "no Playable Director on " + go->GetName(); return false; }
		}
		if (op == "window")
		{
			// 창 열기 (편집기에서만): 고른 디렉터를 보여 준다
			if (s_Window == nullptr) { e = "no Timeline window (player)"; return false; }
			TimelineWindow::Focus(d);
			r = { { "open", true }, { "director", d && d->GetGameObject() ? d->GetGameObject()->GetName() : std::string() } };
			return true;
		}
		std::string path = Str(a, "path");
		if (op == "create")
		{
			if (path.empty()) { e = "path is required"; return false; }
			auto asset = TimelineAsset::Create(path);
			r = Describe(*asset, d);
			return true;
		}
		if (op == "director")
		{
			if (!path.empty()) d->AssetPath = path;
			if (a.contains("play-on-awake")) d->PlayOnAwake = Num(a, "play-on-awake", 1) != 0.0;
			if (a.contains("wrap")) { const std::string w = Str(a, "wrap"); d->WrapMode = w == "Loop" ? 1 : (w == "None" ? 2 : 0); }
			if (a.contains("initial-time")) d->InitialTime = Num(a, "initial-time", 0.0);
		}
		if (d && path.empty())
			path = d->AssetPath;
		std::shared_ptr<TimelineAsset> asset = path.empty() ? nullptr : TimelineAsset::Load(path);
		if (!asset) { e = "timeline asset not found (--path or a director with a Playable)"; return false; }
		if (op == "add-track")
		{
			TimelineTrack& t = asset->AddTrack(Str(a, "type").empty() ? "Animation" : Str(a, "type"), Str(a, "name"));
			r = { { "id", t.Id }, { "name", t.Name } };
			asset->Commit();
			return true;
		}
		TimelineTrack* track = a.contains("track") ? asset->FindTrack(Str(a, "track")) : nullptr;
		if (op == "add-clip" || op == "add-marker" || op == "bind")
			if (track == nullptr) { e = "track not found (id, name or index)"; return false; }
		if (op == "add-clip")
		{
			TimelineClip c;
			c.Start = Num(a, "start", 0.0);
			c.Asset = Str(a, "asset");
			c.ClipIndex = (int)Num(a, "clip", 0);
			c.ClipIn = Num(a, "clip-in", 0.0);
			c.Speed = Num(a, "speed", 1.0);
			c.EaseIn = Num(a, "ease-in", 0.0);
			c.EaseOut = Num(a, "ease-out", 0.0);
			c.Loop = Num(a, "loop", 0.0) != 0.0;
			c.Name = Str(a, "name");
			if (a.contains("object"))
				if (GameObject* o = FindTarget(Str(a, "object")))
					c.Object = o->GetFileID();
			double dur = Num(a, "duration", 0.0);
			if (dur <= 0.0 && track->Type == "Animation" && !c.Asset.empty())
				if (auto clip = ResourceManager::GetI()->LoadAnimationClip(c.Asset, c.ClipIndex))
					dur = clip->GetClipEndTime();
			c.Duration = dur > 0.0 ? dur : 1.0;
			track->Clips.push_back(c);
			std::sort(track->Clips.begin(), track->Clips.end(), [](const TimelineClip& p, const TimelineClip& q) { return p.Start < q.Start; });
			asset->Commit();
		}
		else if (op == "add-marker")
		{
			TimelineMarker m;
			m.Time = Num(a, "time", 0.0);
			m.Method = Str(a, "method");
			m.String = Str(a, "string");
			m.Float = (float)Num(a, "float", 0.0);
			m.Int = (int)Num(a, "int", 0.0);
			m.Retroactive = Num(a, "retroactive", 0.0) != 0.0;
			if (m.Method.empty()) { e = "method is required"; return false; }
			track->Markers.push_back(m);
			std::sort(track->Markers.begin(), track->Markers.end(), [](const TimelineMarker& p, const TimelineMarker& q) { return p.Time < q.Time; });
			asset->Commit();
		}
		else if (op == "bind")
		{
			if (d == nullptr) { e = "--director is required"; return false; }
			GameObject* target = FindTarget(Str(a, "target"));
			d->SetBinding(track->Id, target ? target->GetFileID() : 0);
		}
		else if (op == "play" || op == "pause" || op == "resume" || op == "stop" || op == "time")
		{
			if (d == nullptr) { e = "--director is required"; return false; }
			if (op == "play") d->Play();
			else if (op == "pause") d->Pause();
			else if (op == "resume") d->Resume();
			else if (op == "stop") d->Stop();
			else { d->SetTime(Num(a, "value", 0.0)); d->Evaluate(); }
		}
		else if (op != "info" && op != "director")
		{
			e = "unknown op '" + op + "' (nova timeline help)";
			return false;
		}
		r = Describe(*asset, d);
		return true;
	}
}

NOVA_PACKAGE_EXPORT void NovaPackage_OnLoad()
{
	CliServer::Register("timeline", "Timeline {op: help | create | info | add-track | add-clip | add-marker | director | bind | play | pause | resume | stop | time | window} (nova timeline help)",
		[](const nlohmann::json& a, nlohmann::json& r, std::string& e) { return RunCli(a, r, e); });
	if (Application::IsPlayer())
		return;
	s_Window = new TimelineWindow();
	EditorGUIManager::GetI()->RegisterWindow(s_Window);
	EditorExtensions::AssetType t;
	t.Owner = kPackage;
	t.Extension = ".playable";
	t.Icon = "timeline";
	t.CreateMenu = "Timeline";
	t.DefaultName = "New Timeline";
	t.Create = [](const std::string& path) { TimelineAsset::Create(path); };
	t.Open = [](const std::string&) { TimelineWindow::Focus(nullptr); };
	t.Inspector = [](const std::string& path) {
		if (auto a = TimelineAsset::Load(path))
		{
			UnityGUI::Label(std::filesystem::path(path).stem().string().c_str(), 0, true);
			char info[128];
			sprintf_s(info, "%d tracks, %.2f s", (int)a->Tracks.size(), a->Duration());
			UnityGUI::ValueLabel("Timeline", info);
			UnityGUI::HelpBox("Add a Playable Director to a GameObject, pick this asset, then edit it in Window > Timeline.", false);
		}
	};
	EditorExtensions::RegisterAssetType(t);
}

NOVA_PACKAGE_EXPORT void NovaPackage_OnUnload()
{
	CliServer::Unregister("timeline");
	EditorExtensions::UnregisterOwner(kPackage);
	if (s_Window)
	{
		EditorGUIManager::GetI()->UnregisterWindow(s_Window);
		delete s_Window;
		s_Window = nullptr;
	}
}

// ---- C#: 0 Play, 1 Pause, 2 Resume, 3 Stop, 4 Evaluate
NOVA_PACKAGE_EXPORT void TL_Call(uint64 id, int op)
{
	PlayableDirector* d = FindDirector(id);
	if (d == nullptr) return;
	switch (op) { case 0: d->Play(); break; case 1: d->Pause(); break; case 2: d->Resume(); break; case 3: d->Stop(); break; default: d->Evaluate(); break; }
}
// 0 time, 1 duration, 2 playing (0/1), 3 wrap mode, 4 initial time, 5 play on awake (0/1)
NOVA_PACKAGE_EXPORT double TL_Get(uint64 id, int prop)
{
	PlayableDirector* d = FindDirector(id);
	if (d == nullptr) return 0.0;
	switch (prop)
	{
	case 0: return d->GetTime();
	case 1: return d->Duration();
	case 2: return d->IsPlaying() ? 1.0 : 0.0;
	case 3: return d->WrapMode;
	case 4: return d->InitialTime;
	default: return d->PlayOnAwake ? 1.0 : 0.0;
	}
}
NOVA_PACKAGE_EXPORT void TL_Set(uint64 id, int prop, double v)
{
	PlayableDirector* d = FindDirector(id);
	if (d == nullptr) return;
	switch (prop)
	{
	case 0: d->SetTime(v); break;
	case 3: d->WrapMode = std::clamp((int)v, 0, 2); break;
	case 4: d->InitialTime = (std::max)(0.0, v); break;
	case 5: d->PlayOnAwake = v != 0.0; break;
	}
}
NOVA_PACKAGE_EXPORT void TL_SetBinding(uint64 id, const char* track, uint64 target)
{
	if (PlayableDirector* d = FindDirector(id))
		if (track) d->SetBinding(track, target);
}
NOVA_PACKAGE_EXPORT uint64 TL_GetBinding(uint64 id, const char* track)
{
	PlayableDirector* d = FindDirector(id);
	if (d == nullptr || track == nullptr) return 0;
	if (auto a = d->GetAsset())
		if (TimelineTrack* t = a->FindTrack(track))
		{
			auto it = d->Bindings.find(t->Id);
			return it == d->Bindings.end() ? 0 : it->second;
		}
	return 0;
}
NOVA_PACKAGE_EXPORT const char* TL_GetAsset(uint64 id)
{
	PlayableDirector* d = FindDirector(id);
	return ScriptBindings::ReturnString(d ? d->AssetPath : std::string());
}
NOVA_PACKAGE_EXPORT void TL_SetAsset(uint64 id, const char* path)
{
	if (PlayableDirector* d = FindDirector(id))
		d->AssetPath = path ? path : "";
}
