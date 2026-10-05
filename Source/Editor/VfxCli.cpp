#include "pch.h"
#include "VfxCli.h"
#include "VfxAsset.h"
#include "VisualEffect.h"
#include "VfxRuntime.h"
#include "CliServer.h"

// nova vfx <op> [Assets/X.vfx] [--key value ...] — .vfx 에셋 편집 · 장면의 Visual Effect 제어.
//  VFX Assistant (Claude Code) 도 이 명령만 쓴다 (허용 도구 = Bash(nova vfx *)) → 사람이 쓰는 CLI 와 같은 길
namespace
{
	using json = nlohmann::json;

	std::string Simplify(const std::string& s)
	{
		std::string o;
		for (char c : s)
			if (isalnum((unsigned char)c))
				o += (char)tolower((unsigned char)c);
		return o;
	}

	const char* kHelp =
		"nova vfx <op> [Assets/X.vfx] [--key value ...]\n"
		"  blocks                                  block types with params (initialize / update)\n"
		"  templates                               built-in templates\n"
		"  new <path> [--template Fireworks] [--overwrite]\n"
		"  info <path>                             asset JSON + issues\n"
		"  set <path> --data '{...}' | --file f    replace the whole asset\n"
		"  system.add <path> --name N [--data '{...}'] [--template T --from S]\n"
		"  system.set <path> --system N --data '{\"spawn\":{\"rate\":200},\"output\":{\"shape\":\"Star\"}}'   (merge)\n"
		"  system.remove <path> --system N\n"
		"  block.add <path> --system N --context initialize|update --type T [--params '{...}'] [--bind '{...}'] [--index i]\n"
		"  block.set <path> --system N --context c --index i [--params '{...}'] [--bind '{...}'] [--enabled false]   (params merge)\n"
		"  block.remove <path> --system N --context c --index i\n"
		"  property.add <path> --name N --type Float|Int|Bool|Vector3|Color --value v [--range 0,10]\n"
		"  property.set <path> --name N [--value v] [--rename M] [--range a,b]\n"
		"  property.remove <path> --name N\n"
		"  event [path] --name OnPlay|OnStop|Custom [--object Name]   send an event to Visual Effects (all, or using path)\n"
		"  override [path] --name Prop --value v [--object Name]      set an exposed property on scene Visual Effects\n"
		"  restart [path] [--object Name]\n"
		"  stats                                   Visual Effects in the scene: asset, alive particles per system\n"
		"  window [path] | assistant [path]        open the Visual Effect Graph / VFX Assistant window\n"
		"  assistant.send --message \"...\" [path]   ask the VFX Assistant (local Claude Code); assistant.status | assistant.stop\n"
		"place it in the scene: nova create visual-effect --asset <path> [--name N] [--position x,y,z]";

	bool LoadAsset(const json& a, std::string& path, Vfx::Asset& out, std::string& error)
	{
		path = a.value("path", std::string());
		if (path.empty())
		{
			error = "needs a .vfx path (Assets/Effects/X.vfx)";
			return false;
		}
		const Vfx::Loaded l = Vfx::Load(path);
		if (!l.Data)
		{
			error = l.Error.empty() ? "cannot load " + path : l.Error;
			return false;
		}
		out = *l.Data;
		return true;
	}

	json Summary(const std::string& path, const Vfx::Asset& asset)
	{
		json r = asset.ToJson();
		r["path"] = path;
		r["issues"] = asset.Validate();
		return r;
	}

	bool SaveAsset(const std::string& path, const Vfx::Asset& asset, json& result, std::string& error)
	{
		if (!Vfx::Save(path, asset, error))
			return false;
		result = Summary(path, asset);
		return true;
	}

	// JSON 값: 문자열로 온 JSON ('{...}') 도 받는다
	json JsonArg(const json& a, const char* key)
	{
		if (!a.contains(key))
			return json();
		const json& v = a[key];
		if (v.is_string())
		{
			json p = json::parse(v.get<std::string>(), nullptr, false);
			if (!p.is_discarded())
				return p;
		}
		return v;
	}

	int SystemIndex(const Vfx::Asset& asset, const json& a, std::string& error)
	{
		if (!a.contains("system"))
		{
			error = "needs --system <name or index>";
			return -1;
		}
		const json& s = a["system"];
		if (s.is_number_integer())
		{
			const int i = s.get<int>();
			if (i >= 0 && i < (int)asset.Systems.size()) return i;
		}
		else if (s.is_string())
		{
			const int i = asset.FindSystem(s.get<std::string>());
			if (i >= 0) return i;
			for (int k = 0; k < (int)asset.Systems.size(); ++k)
				if (Simplify(asset.Systems[k].Name) == Simplify(s.get<std::string>()))
					return k;
		}
		error = "no system '" + s.dump() + "' (systems: ";
		for (const auto& sys : asset.Systems) error += sys.Name + ", ";
		error += ")";
		return -1;
	}

	std::vector<Vfx::Block>* BlockList(Vfx::System& s, const json& a, std::string& error)
	{
		const std::string c = Simplify(a.value("context", std::string()));
		if (c == "initialize" || c == "init") return &s.Initialize;
		if (c == "update") return &s.Update;
		error = "needs --context initialize|update";
		return nullptr;
	}

	json DescribeBlocks()
	{
		json list = json::array();
		for (const Vfx::BlockDesc& d : Vfx::Blocks())
		{
			json params = json::array();
			for (const Vfx::ParamDesc& p : d.Params)
			{
				static const char* kinds[] = { "Float", "Int", "Bool", "Enum", "Vector3", "Color", "Curve", "Gradient" };
				json e = { { "name", p.Name }, { "kind", kinds[(int)p.Kind] } };
				switch (p.Kind)
				{
				case Vfx::ParamKind::Vector3: e["default"] = { p.Default[0], p.Default[1], p.Default[2] }; break;
				case Vfx::ParamKind::Color: e["default"] = { p.Default[0], p.Default[1], p.Default[2], p.Default[3] }; break;
				case Vfx::ParamKind::Curve: e["default"] = "[[t,value],...] (0..1 -> multiplier)"; break;
				case Vfx::ParamKind::Gradient: e["default"] = "[[t,r,g,b,a],...] (HDR rgb)"; break;
				case Vfx::ParamKind::Enum: e["options"] = Vfx::EnumOptions(p); e["default"] = Vfx::EnumOptions(p)[(size_t)p.Default[0]]; break;
				case Vfx::ParamKind::Bool: e["default"] = p.Default[0] > 0.5f; break;
				default: e["default"] = p.Default[0]; break;
				}
				if (p.Tip) e["tip"] = p.Tip;
				params.push_back(e);
			}
			list.push_back({ { "type", d.Type }, { "label", d.Label }, { "context", d.Ctx == Vfx::Context::Initialize ? "initialize" : "update" },
				{ "help", d.Help }, { "params", params } });
		}
		json r = { { "blocks", list } };
		r["output"] = {
			{ "blend", { "Additive", "Alpha" } },
			{ "shape", { "SoftDot", "Glow", "Star", "Sparkle", "Ring", "Spark", "Smoke", "Square", "Texture", "Heart" } },
			{ "orient", { "FaceCamera", "AlongVelocity", "Horizontal" } },
			{ "fields", "stretch (AlongVelocity length per speed), softDistance, intensity (HDR, >1 blooms), texture, flipbook{columns,rows,fps}" } };
		r["spawn"] = "rate (per second), rateBind (Float property), bursts[{time,count,cycles(0=forever),interval}], loop, duration (0 = forever), delay, "
			"startEvent (OnPlay), stopEvent (OnStop), parent (GPU event: spawn when that system's particles die) + countPerEvent";
		r["system"] = "name, capacity (max alive), space World|Local, spawn{}, initialize[], update[], output{}";
		return r;
	}

	// 장면의 Visual Effect (path 가 비면 모두, --object 면 그 이름만)
	std::vector<VisualEffect*> Targets(const json& a)
	{
		const std::string path = a.value("path", std::string());
		const std::string object = a.value("object", std::string());
		std::vector<VisualEffect*> out;
		for (VisualEffect* v : VisualEffect::All())
		{
			if (!path.empty() && Simplify(v->AssetPath) != Simplify(path))
				continue;
			if (!object.empty() && (!v->GetGameObject() || v->GetGameObject()->GetName() != object))
				continue;
			out.push_back(v);
		}
		return out;
	}

	bool Run(const std::string& op, const json& a, json& r, std::string& e)
	{
		if (op == "help") { r = { { "help", kHelp } }; return true; }
		if (op == "batch")
		{
			// 줄마다 연산 하나 (path 는 줄마다 쓰거나 batch 의 path 를 물려받는다)
			if (!a.contains("steps") || !a["steps"].is_array()) { e = "batch needs steps"; return false; }
			json results = json::array();
			for (const json& step : a["steps"])
			{
				json s = step;
				if (!s.contains("path") && a.contains("path")) s["path"] = a["path"];
				const std::string stepOp = s.value("op", std::string());
				s.erase("op");
				json one;
				if (!Run(stepOp, s, one, e))
				{
					e = stepOp + ": " + e;
					return false;
				}
				results.push_back({ { "op", stepOp } });
			}
			r = { { "steps", results } };
			return true;
		}
		if (op == "blocks") { r = DescribeBlocks(); return true; }
		if (op == "templates") { r = { { "templates", Vfx::TemplateNames() } }; return true; }
		if (op == "list") { r = { { "assets", Vfx::FindAssets() } }; return true; }
		if (op == "new")
		{
			const std::string path = a.value("path", std::string());
			if (path.empty() || Simplify(std::filesystem::path(path).extension().string()) != "vfx")
			{
				e = "new needs a path ending in .vfx";
				return false;
			}
			std::error_code ec;
			if (std::filesystem::exists(Vfx::FullPath(path), ec) && !a.value("overwrite", false))
			{
				e = path + " exists (use --overwrite)";
				return false;
			}
			Vfx::Asset asset = Vfx::DefaultAsset();
			if (a.contains("template") && !Vfx::MakeTemplate(a["template"].get<std::string>(), asset))
			{
				e = "no template '" + a["template"].get<std::string>() + "'";
				return false;
			}
			return SaveAsset(path, asset, r, e);
		}

		std::string path;
		if (op == "event" || op == "override" || op == "restart" || op == "stats")
		{
			const std::vector<VisualEffect*> targets = Targets(a);
			if (op == "stats")
			{
				json list = json::array();
				for (VisualEffect* v : targets)
				{
					json systems = json::array();
					if (auto asset = v->GetAsset())
						for (size_t i = 0; i < asset->Systems.size(); ++i)
							systems.push_back({ { "name", asset->Systems[i].Name }, { "alive", v->SystemAliveCount((int)i) } });
					list.push_back({ { "object", v->GetGameObject() ? v->GetGameObject()->GetName() : "" }, { "asset", v->AssetPath },
						{ "alive", v->AliveParticleCount() }, { "systems", systems }, { "enabled", v->IsEnabled() }, { "error", v->AssetError() } });
				}
				r = { { "effects", list }, { "gpu", VfxRuntime::Supported() }, { "drawCalls", VfxRuntime::LastDrawCalls() } };
				if (!VfxRuntime::LastError().empty()) r["runtimeError"] = VfxRuntime::LastError();
				return true;
			}
			if (targets.empty())
			{
				e = "no Visual Effect in the scene matches (nova vfx stats)";
				return false;
			}
			for (VisualEffect* v : targets)
			{
				if (op == "restart") v->Reinit();
				else if (op == "event")
				{
					if (!a.contains("name")) { e = "event needs --name"; return false; }
					v->SendEvent(a["name"].get<std::string>());
				}
				else
				{
					const std::string name = a.value("name", std::string());
					if (name.empty() || !a.contains("value")) { e = "override needs --name and --value"; return false; }
					if (!v->SetProperty(name, Vfx::ValueFromJson(a["value"], { 0, 0, 0, 1 })))
					{
						e = "no exposed property '" + name + "'";
						return false;
					}
				}
			}
			r = { { "effects", (int)targets.size() } };
			return true;
		}

		Vfx::Asset asset;
		if (op == "set")
		{
			path = a.value("path", std::string());
			json j = JsonArg(a, "data");
			if (j.is_null()) j = JsonArg(a, "json");   // nova call · batch (명령 줄의 --json 은 출력 형식)
			if (j.is_null() && a.contains("file"))
			{
				std::ifstream in(std::filesystem::path(string_to_wstring(a["file"].get<std::string>())), std::ios::binary);
				std::stringstream ss;
				ss << in.rdbuf();
				j = json::parse(ss.str(), nullptr, false);
			}
			if (path.empty() || !j.is_object() || !asset.FromJson(j, e))
			{
				if (e.empty()) e = "set needs a path and --data '{...}' (or --file)";
				return false;
			}
			return SaveAsset(path, asset, r, e);
		}
		if (!LoadAsset(a, path, asset, e))
			return false;
		if (op == "info" || op == "get" || op == "validate")
		{
			r = Summary(path, asset);
			return true;
		}
		if (op == "system.add")
		{
			Vfx::System s;
			json j = JsonArg(a, "data");
			if (j.is_null()) j = JsonArg(a, "json");   // nova call · batch (명령 줄의 --json 은 출력 형식)
			if (j.is_object())
				s = Vfx::SystemFromJson(j);
			else if (a.contains("template"))
			{
				Vfx::Asset t;
				if (!Vfx::MakeTemplate(a["template"].get<std::string>(), t)) { e = "no template"; return false; }
				const int i = a.contains("from") ? t.FindSystem(a["from"].get<std::string>()) : 0;
				if (i < 0 || i >= (int)t.Systems.size()) { e = "template has no such system"; return false; }
				s = t.Systems[i];
			}
			else
				s = Vfx::DefaultAsset().Systems[0];
			if (a.contains("name")) s.Name = a["name"].get<std::string>();
			std::string base = s.Name;
			for (int n = 2; asset.FindSystem(s.Name) >= 0; ++n) s.Name = base + " " + std::to_string(n);
			asset.Systems.push_back(s);
			return SaveAsset(path, asset, r, e);
		}
		if (op.rfind("system.", 0) == 0 || op.rfind("block.", 0) == 0)
		{
			const int si = SystemIndex(asset, a, e);
			if (si < 0) return false;
			Vfx::System& sys = asset.Systems[si];
			if (op == "system.remove")
			{
				asset.Systems.erase(asset.Systems.begin() + si);
				return SaveAsset(path, asset, r, e);
			}
			if (op == "system.set")
			{
				const json patch = JsonArg(a, "data").is_null() ? JsonArg(a, "json") : JsonArg(a, "data");
				if (!patch.is_object()) { e = "system.set needs --data '{...}'"; return false; }
				json cur = Vfx::SystemToJson(sys);
				cur.merge_patch(patch);
				const std::string oldName = sys.Name;
				sys = Vfx::SystemFromJson(cur);
				if (sys.Name != oldName)   // 이름을 바꾸면 GPU Event 자식도 따라간다
					for (Vfx::System& other : asset.Systems)
						if (other.SpawnCtx.Parent == oldName) other.SpawnCtx.Parent = sys.Name;
				return SaveAsset(path, asset, r, e);
			}
			std::vector<Vfx::Block>* list = BlockList(sys, a, e);
			if (!list) return false;
			if (op == "block.add")
			{
				const Vfx::BlockDesc* d = Vfx::FindBlock(a.value("type", std::string()));
				if (!d) { e = "unknown block type (nova vfx blocks)"; return false; }
				const bool wantInit = list == &sys.Initialize;
				if ((d->Ctx == Vfx::Context::Initialize) != wantInit) { e = std::string(d->Type) + " belongs to " + (wantInit ? "update" : "initialize"); return false; }
				Vfx::Block b;
				b.Type = d->Type;
				if (json p = JsonArg(a, "params"); p.is_object()) b.Params = p;
				if (json p = JsonArg(a, "bind"); p.is_object()) b.Bind = p;
				const int at = std::clamp(a.value("index", (int)list->size()), 0, (int)list->size());
				list->insert(list->begin() + at, b);
				return SaveAsset(path, asset, r, e);
			}
			const int index = a.value("index", -1);
			if (index < 0 || index >= (int)list->size()) { e = "needs --index 0.." + std::to_string((int)list->size() - 1); return false; }
			if (op == "block.remove")
			{
				list->erase(list->begin() + index);
				return SaveAsset(path, asset, r, e);
			}
			if (op == "block.set")
			{
				Vfx::Block& b = (*list)[index];
				if (json p = JsonArg(a, "params"); p.is_object()) b.Params.merge_patch(p);
				if (json p = JsonArg(a, "bind"); p.is_object()) b.Bind.merge_patch(p);
				if (a.contains("enabled")) b.Enabled = a["enabled"].is_boolean() ? a["enabled"].get<bool>() : a["enabled"].dump() != "\"false\"";
				return SaveAsset(path, asset, r, e);
			}
			if (op == "block.move")
			{
				const int to = std::clamp(a.value("to", index), 0, (int)list->size() - 1);
				Vfx::Block b = (*list)[index];
				list->erase(list->begin() + index);
				list->insert(list->begin() + to, b);
				return SaveAsset(path, asset, r, e);
			}
		}
		if (op.rfind("property.", 0) == 0)
		{
			const std::string name = a.value("name", std::string());
			if (name.empty()) { e = "needs --name"; return false; }
			auto it = std::find_if(asset.Properties.begin(), asset.Properties.end(), [&](const Vfx::Property& p) { return p.Name == name; });
			if (op == "property.add")
			{
				if (it != asset.Properties.end()) { e = "property exists"; return false; }
				Vfx::Property p;
				p.Name = name;
				if (a.contains("type") && !Vfx::PropertyTypeFromName(a["type"].get<std::string>(), p.Type)) { e = "type: Float|Int|Bool|Vector3|Color"; return false; }
				if (p.Type == Vfx::PropertyType::Color) p.Value = { 1, 1, 1, 1 };
				if (a.contains("value")) p.Value = Vfx::ValueFromJson(a["value"], p.Value);
				if (a.contains("range") && a["range"].is_array() && a["range"].size() == 2) { p.Min = a["range"][0]; p.Max = a["range"][1]; }
				asset.Properties.push_back(p);
				return SaveAsset(path, asset, r, e);
			}
			if (it == asset.Properties.end()) { e = "no property '" + name + "'"; return false; }
			if (op == "property.remove")
			{
				asset.Properties.erase(it);
				return SaveAsset(path, asset, r, e);
			}
			if (op == "property.set")
			{
				if (a.contains("value")) it->Value = Vfx::ValueFromJson(a["value"], it->Value);
				if (a.contains("range") && a["range"].is_array() && a["range"].size() == 2) { it->Min = a["range"][0]; it->Max = a["range"][1]; }
				if (a.contains("rename"))
				{
					const std::string to = a["rename"].get<std::string>();
					// 연결도 새 이름으로
					for (Vfx::System& s : asset.Systems)
					{
						if (s.SpawnCtx.RateBind == name) s.SpawnCtx.RateBind = to;
						for (auto* list : { &s.Initialize, &s.Update })
							for (Vfx::Block& b : *list)
								for (auto bit = b.Bind.begin(); bit != b.Bind.end(); ++bit)
									if (bit->is_string() && bit->get<std::string>() == name) *bit = to;
					}
					it->Name = to;
				}
				return SaveAsset(path, asset, r, e);
			}
		}
		e = "unknown op '" + op + "' (nova vfx help)";
		return false;
	}
}

namespace VfxCli
{
	void Register(WindowOps windowOps)
	{
		static WindowOps s_Window;
		s_Window = std::move(windowOps);
		CliServer::Register("vfx", "Visual Effect Graph op: {op, path?, ...} (nova vfx help)", [](const json& args, json& result, std::string& error) {
			const std::string op = args.value("op", std::string("help"));
			if (op == "window" || op.rfind("assistant", 0) == 0)
			{
				if (s_Window && s_Window(op, args, result, error))
					return error.empty();
				error = "unknown op '" + op + "'";
				return false;
			}
			json opArgs = args;
			opArgs.erase("op");
			return Run(op, opArgs, result, error);
		});
	}

	bool RunOp(const std::string& op, const nlohmann::json& args, nlohmann::json& result, std::string& error)
	{
		return Run(op, args, result, error);
	}
}
