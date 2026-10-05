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
		"  operators                               operator node types (inputs, settings) — values computed per particle\n"
		"  op.add <path> --type Multiply [--params '{...}'] [--x 0 --y 400]   -> id\n"
		"  op.set <path> --id N [--params '{...}'] [--x --y]   op.remove <path> --id N\n"
		"  op.connect <path> --from N --to M --input B        op.disconnect <path> --to M --input B\n"
		"  block.link <path> --system S --context c --index i --param Speed --from N   block.unlink ... --param Speed\n"
		"  attribute.add <path> --name Phase --type Float|Vector3   attribute.remove <path> --name Phase   (custom attributes, 4 floats)\n"
		"  subgraph.new <Assets/X.vfxoperator> [--overwrite]   Sub Graph file: properties = inputs, Output (Sub Graph) node = result;\n"
		"      edit it with property.* / op.* like a .vfx, use it with op.add --type SubGraph --params '{\"Path\":\"Assets/X.vfxoperator\"}'\n"
		"  blockgraph.new <Assets/X.vfxblock> [--overwrite]   Block Sub Graph: properties = inputs, system 'Block' initialize/update = the blocks;\n"
		"      use it with block.add --type SubgraphBlock --params '{\"Path\":\"Assets/X.vfxblock\",\"Strength\":5}' (either context)\n"
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
				static const char* kinds[] = { "Float", "Int", "Bool", "Enum", "Vector3", "Color", "Curve", "Gradient", "Text" };
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
			list.push_back({ { "type", d.Type }, { "label", d.Label }, { "context", d.AnyContext ? "initialize or update" : d.Ctx == Vfx::Context::Initialize ? "initialize" : "update" },
				{ "help", d.Help }, { "params", params } });
		}
		json r = { { "blocks", list } };
		r["output"] = {
			{ "blend", { "Additive", "Alpha", "Opaque" } },
			{ "shape", { "SoftDot", "Glow", "Star", "Sparkle", "Ring", "Spark", "Smoke", "Square", "Texture", "Heart", "Mesh" } },
			{ "mesh", Vfx::MeshNames() },
			{ "orient", { "FaceCamera", "AlongVelocity", "Horizontal" } },
			{ "fields", "stretch (AlongVelocity length per speed), softDistance, intensity (HDR, >1 blooms), texture, flipbook{columns,rows,fps}, "
				"sort Auto|On|Off, trail{points,length,width,only}, mesh (shape Mesh: Cube|Sphere|Cylinder|Cone|Crystal), lit (mesh shading)" } };
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
		if (op == "operators")
		{
			static const char* kinds[] = { "Float", "Int", "Bool", "Enum", "Vector3", "Color", "Curve", "Gradient", "Text" };
			json list = json::array();
			for (const Vfx::OperatorDesc& d : Vfx::Operators())
			{
				json ins = json::array(), sets = json::array();
				for (const Vfx::OperatorInput& in : d.Inputs)
					ins.push_back({ { "name", in.Name }, { "kind", kinds[(int)in.Kind] }, { "default", { in.Default[0], in.Default[1], in.Default[2], in.Default[3] } } });
				for (const Vfx::ParamDesc& p : d.Settings)
				{
					json e = { { "name", p.Name }, { "kind", kinds[(int)p.Kind] } };
					if (p.Kind == Vfx::ParamKind::Enum) e["options"] = Vfx::EnumOptions(p);
					sets.push_back(e);
				}
				list.push_back({ { "type", d.Type }, { "label", d.Label }, { "category", d.Category }, { "inputs", ins }, { "settings", sets },
					{ "output", kinds[(int)d.Output] }, { "help", d.Help } });
			}
			r = { { "operators", list }, { "notes", "values are float4; scalars fill all four lanes. Link an operator to a block value with block.link (Float/Int/Bool/Enum/Vector3/Color values). "
				"Attribute operators read the particle as the block sees it (Initialize: being built; Update: current)." } };
			return true;
		}
		if (op == "templates") { r = { { "templates", Vfx::TemplateNames() } }; return true; }
		if (op == "list") { r = { { "assets", Vfx::FindAssets() }, { "subgraphs", Vfx::FindAssets(Vfx::kSubgraphExtension) }, { "blockSubgraphs", Vfx::FindAssets(Vfx::kBlockSubgraphExtension) } }; return true; }
		if (op == "blockgraph.new")
		{
			const std::string path = a.value("path", std::string());
			if (path.empty() || Simplify(std::filesystem::path(path).extension().string()) != "vfxblock")
			{
				e = "blockgraph.new needs a path ending in .vfxblock";
				return false;
			}
			std::error_code ec;
			if (std::filesystem::exists(Vfx::FullPath(path), ec) && !a.value("overwrite", false))
			{
				e = path + " exists (use --overwrite)";
				return false;
			}
			return SaveAsset(path, Vfx::DefaultBlockSubgraph(), r, e);
		}
		if (op == "subgraph.new")
		{
			const std::string path = a.value("path", std::string());
			if (path.empty() || Simplify(std::filesystem::path(path).extension().string()) != "vfxoperator")
			{
				e = "subgraph.new needs a path ending in .vfxoperator";
				return false;
			}
			std::error_code ec;
			if (std::filesystem::exists(Vfx::FullPath(path), ec) && !a.value("overwrite", false))
			{
				e = path + " exists (use --overwrite)";
				return false;
			}
			return SaveAsset(path, Vfx::DefaultSubgraph(), r, e);
		}
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
					json item = { { "object", v->GetGameObject() ? v->GetGameObject()->GetName() : "" }, { "asset", v->AssetPath },
						{ "alive", v->AliveParticleCount() }, { "systems", systems }, { "enabled", v->IsEnabled() }, { "culled", v->IsCulled() }, { "error", v->AssetError() } };
					// 월드 경계 (몇 프레임 늦은 GPU 값 + 크기 · 꼬리 여유) — 검사 · 디버그용
					Vec3 mn, mx;
					if (v->GetWorldBounds(mn, mx))
						item["bounds"] = { { mn.x, mn.y, mn.z }, { mx.x, mx.y, mx.z } };
					list.push_back(item);
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
		if (op == "encode")
		{
			// 진단: 시스템의 블록 목록 (58. VFX.fx 가 읽는 float4 — 머리 · 값 칸 · 연산 노드 식)
			const int si = SystemIndex(asset, a, e);
			if (si < 0) return false;
			struct Props : Vfx::PropertySource { const Vfx::Asset& A; Props(const Vfx::Asset& x) : A(x) {} bool Get(const std::string& k, std::array<float, 4>& o) const override { const Vfx::Property* p = A.FindProperty(k); if (!p) return false; o = p->Value; return true; } } props(asset);
			const float identity[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
			Vfx::Encoded enc;
			Vfx::Encode(asset.Systems[si], props, identity, enc, &asset);
			json prog = json::array();
			for (const auto& v : enc.Program) prog.push_back({ v[0], v[1], v[2], v[3] });
			r = { { "initStart", enc.InitStart }, { "initCount", enc.InitCount }, { "updateStart", enc.UpdateStart }, { "updateCount", enc.UpdateCount }, { "program", prog } };
			return true;
		}
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
				if (!Vfx::AllowedIn(*d, wantInit ? Vfx::Context::Initialize : Vfx::Context::Update)) { e = std::string(d->Type) + " belongs to " + (wantInit ? "update" : "initialize"); return false; }
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
		if (op.rfind("op.", 0) == 0)
		{
			auto find = [&](int id) -> Vfx::OperatorNode* {
				for (Vfx::OperatorNode& n : asset.Operators) if (n.Id == id) return &n;
				return nullptr;
			};
			if (op == "op.add")
			{
				const Vfx::OperatorDesc* d = Vfx::FindOperator(a.value("type", std::string()));
				if (!d) { e = "unknown operator type (nova vfx operators)"; return false; }
				Vfx::OperatorNode n;
				n.Id = asset.NewOperatorId();
				n.Type = d->Type;
				if (json p = JsonArg(a, "params"); p.is_object()) n.Params = p;
				n.X = a.value("x", -420.0f);
				n.Y = a.value("y", 120.0f * (float)asset.Operators.size());
				asset.Operators.push_back(n);
				if (!SaveAsset(path, asset, r, e)) return false;
				r["id"] = n.Id;
				return true;
			}
			const int id = a.value("id", a.value("to", -1));
			Vfx::OperatorNode* n = find(id);
			if (!n) { e = "no operator " + std::to_string(id) + " (nova vfx info lists operators)"; return false; }
			if (op == "op.set")
			{
				if (json p = JsonArg(a, "params"); p.is_object()) n->Params.merge_patch(p);
				if (a.contains("x")) n->X = a["x"].get<float>();
				if (a.contains("y")) n->Y = a["y"].get<float>();
				return SaveAsset(path, asset, r, e);
			}
			if (op == "op.remove")
			{
				for (Vfx::OperatorNode& o : asset.Operators)
					for (auto it = o.Inputs.begin(); it != o.Inputs.end();)
						it = it->is_number_integer() && it->get<int>() == id ? o.Inputs.erase(it) : std::next(it);
				for (Vfx::System& sys : asset.Systems)
					for (auto* list : { &sys.Initialize, &sys.Update })
						for (Vfx::Block& b : *list)
							for (auto it = b.Links.begin(); it != b.Links.end();)
								it = it->is_number_integer() && it->get<int>() == id ? b.Links.erase(it) : std::next(it);
				asset.Operators.erase(std::remove_if(asset.Operators.begin(), asset.Operators.end(), [&](const Vfx::OperatorNode& o) { return o.Id == id; }), asset.Operators.end());
				return SaveAsset(path, asset, r, e);
			}
			const std::string input = a.value("input", std::string());
			const std::vector<Vfx::NodeInput> inputs = Vfx::OperatorInputs(*n);
			const Vfx::NodeInput* in = nullptr;
			for (const Vfx::NodeInput& i : inputs) if (Simplify(i.Name) == Simplify(input)) in = &i;
			if (!in) { e = n->Type + " has no input '" + input + "'"; return false; }
			for (auto it = n->Inputs.begin(); it != n->Inputs.end(); ++it)
				if (Simplify(it.key()) == Simplify(input)) { n->Inputs.erase(it); break; }
			if (op == "op.connect")
			{
				const int from = a.value("from", -1);
				if (!find(from)) { e = "no operator " + std::to_string(from); return false; }
				n->Inputs[in->Name] = from;
				// 고리 · 너무 깊은 식은 받지 않는다
				struct Props : Vfx::PropertySource { const Vfx::Asset& A; Props(const Vfx::Asset& x) : A(x) {} bool Get(const std::string& k, std::array<float, 4>& o) const override { const Vfx::Property* p = A.FindProperty(k); if (!p) return false; o = p->Value; return true; } } props(asset);
				std::vector<std::array<float, 4>> code;
				std::string err;
				if (!Vfx::CompileOperator(asset, n->Id, props, code, err) && err.find("loop") != std::string::npos) { e = err; return false; }
				return SaveAsset(path, asset, r, e);
			}
			if (op == "op.disconnect")
				return SaveAsset(path, asset, r, e);
		}
		if (op == "block.link" || op == "block.unlink")
		{
			const int si = SystemIndex(asset, a, e);
			if (si < 0) return false;
			std::vector<Vfx::Block>* list = BlockList(asset.Systems[si], a, e);
			if (!list) return false;
			const int index = a.value("index", -1);
			if (index < 0 || index >= (int)list->size()) { e = "needs --index 0.." + std::to_string((int)list->size() - 1); return false; }
			Vfx::Block& b = (*list)[index];
			const Vfx::BlockDesc* d = Vfx::FindBlock(b.Type);
			const Vfx::ParamDesc* pd = d ? Vfx::FindParam(*d, a.value("param", std::string())) : nullptr;
			if (!pd) { e = "block " + b.Type + " has no value '" + a.value("param", std::string()) + "'"; return false; }
			if (!Vfx::Linkable(*pd)) { e = std::string(pd->Name) + " cannot take an operator (curve / gradient)"; return false; }
			for (auto it = b.Links.begin(); it != b.Links.end(); ++it)
				if (Simplify(it.key()) == Simplify(pd->Name)) { b.Links.erase(it); break; }
			if (op == "block.link")
			{
				const int from = a.value("from", -1);
				if (!asset.FindOperatorNode(from)) { e = "no operator " + std::to_string(from); return false; }
				b.Links[pd->Name] = from;
			}
			return SaveAsset(path, asset, r, e);
		}
		if (op == "attribute.add" || op == "attribute.remove")
		{
			const std::string name = a.value("name", std::string());
			if (name.empty()) { e = "needs --name"; return false; }
			auto it = std::find_if(asset.Attributes.begin(), asset.Attributes.end(), [&](const Vfx::Attribute& x) { return x.Name == name; });
			if (op == "attribute.remove")
			{
				if (it == asset.Attributes.end()) { e = "no custom attribute '" + name + "'"; return false; }
				asset.Attributes.erase(it);
				return SaveAsset(path, asset, r, e);
			}
			if (it != asset.Attributes.end()) { e = "custom attribute '" + name + "' exists"; return false; }
			const std::string type = Simplify(a.value("type", std::string("Float")));
			if (type != "float" && type != "vector3") { e = "--type Float or Vector3"; return false; }
			int lanes = type == "vector3" ? 3 : 1;
			for (const Vfx::Attribute& x : asset.Attributes) lanes += x.Vector ? 3 : 1;
			if (lanes > Vfx::kAttributeLanes) { e = "custom attributes are 4 floats per particle (Float = 1, Vector3 = 3) - no room for " + name; return false; }
			asset.Attributes.push_back({ name, type == "vector3" });
			return SaveAsset(path, asset, r, e);
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
