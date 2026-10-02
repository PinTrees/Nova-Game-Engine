#include "pch.h"
#include "ShaderGraphOps.h"
#include "ShaderGraphRuntime.h"
#include <filesystem>

namespace fs = std::filesystem;

namespace ShaderGraph
{
	namespace
	{
		constexpr size_t kUndoMax = 100;

		std::string Lower(std::string s)
		{
			for (char& c : s) c = (char)tolower((unsigned char)c);
			return s;
		}

		// "Master" · 0 → 0, 숫자 → 노드 id
		bool NodeIdArg(const json& v, int& out)
		{
			if (v.is_number_integer()) { out = v.get<int>(); return true; }
			if (v.is_string())
			{
				const std::string s = Lower(v.get<std::string>());
				if (s == "master" || s == "0") { out = 0; return true; }
				try { out = std::stoi(s); return true; }
				catch (...) { return false; }
			}
			return false;
		}

		bool ReadFloats(const json& v, float out[4], int& count)
		{
			if (v.is_number()) { out[0] = out[1] = out[2] = out[3] = v.get<float>(); count = 1; return true; }
			if (v.is_array() && !v.empty() && v.size() <= 4)
			{
				count = (int)v.size();
				for (int i = 0; i < count; ++i)
				{
					if (!v[i].is_number()) return false;
					out[i] = v[i].get<float>();
				}
				return true;
			}
			return false;
		}

		const PortDef* FindIn(const NodeDef& d, const std::string& name)
		{
			for (const PortDef& p : d.In) if (Lower(p.Name) == Lower(name)) return &p;
			return nullptr;
		}

		// 포트 이름은 대소문자 무시로 받고 정식 이름을 돌려준다
		std::string OutPortName(const Graph& g, const Node& n, const std::string& want)
		{
			if (n.Type == "Property") return "Out";
			const NodeDef* d = DefOf(n);
			if (!d || d->Out.empty()) return std::string();
			if (want.empty()) return d->Out[0].Name;
			for (const PortDef& p : d->Out) if (Lower(p.Name) == Lower(want)) return p.Name;
			return std::string();
		}

		std::string InPortName(const Graph& g, int nodeId, const std::string& want)
		{
			std::vector<PortDef> ports;
			if (nodeId == 0) ports = MasterInputs(g);
			else if (const Node* n = g.FindNode(nodeId))
				if (const NodeDef* d = DefOf(*n)) ports = d->In;
			for (const PortDef& p : ports) if (Lower(p.Name) == Lower(want)) return p.Name;
			return std::string();
		}

		bool ValidType(const std::string& t)
		{
			return t == "Float" || t == "Color" || t == "Vector2" || t == "Vector3" || t == "Vector4" || t == "Texture2D";
		}

		std::string ProperType(const std::string& t)
		{
			for (const char* k : { "Float", "Color", "Vector2", "Vector3", "Vector4", "Texture2D" })
				if (Lower(t) == Lower(k)) return k;
			if (Lower(t) == "texture" || Lower(t) == "texture 2d") return "Texture2D";
			return t;
		}

		Property* FindPropertyMutable(Graph& g, const std::string& ref)
		{
			for (Property& p : g.Properties) if (p.Ref == ref || Lower(p.Name) == Lower(ref)) return &p;
			return nullptr;
		}

		// 속성 값 · 범위 · 그림 (property.add / property.set 공용)
		bool ApplyPropertyArgs(Property& p, const json& a, std::string& e)
		{
			if (a.contains("name") && a["name"].is_string()) p.Name = a["name"].get<std::string>();
			if (a.contains("value"))
			{
				float v[4] = { 0, 0, 0, 0 };
				int n = 0;
				if (!ReadFloats(a["value"], v, n)) { e = "value must be a number or [x, y, z, w]"; return false; }
				for (int i = 0; i < 4; ++i) p.Value[i] = i < n ? v[i] : (n == 1 ? v[0] : (i == 3 && p.Type == "Color" ? 1.0f : 0.0f));
			}
			if (a.contains("range"))
			{
				if (a["range"].is_boolean() && !a["range"].get<bool>()) p.Range = false;
				else if (a["range"].is_array() && a["range"].size() == 2) { p.Range = true; p.Min = a["range"][0].get<float>(); p.Max = a["range"][1].get<float>(); }
				else { e = "range must be [min, max] or false"; return false; }
			}
			if (a.contains("texture") && a["texture"].is_string()) p.Texture = a["texture"].get<std::string>();
			return true;
		}

		std::vector<OpInfo> BuildOps()
		{
			return {
				{ "help", "this list" },
				{ "nodes", "[--category Math] node types with their ports (width 0 = dynamic, 10 = Texture2D)" },
				{ "new", "<path.shadergraph | path.shadersubgraph> [--material Lit|Unlit|Decal]: new graph (saved) and open it. A Sub Graph starts with one output Out (Vector3)" },
				{ "open", "<path.shadergraph>" },
				{ "save", "[path]: save and build the shader (errors come back)" },
				{ "info", "the open graph: settings, properties, nodes (inputs: linked / value), edges" },
				{ "output.add", "--name Out --type Float|Vector2|Vector3|Vector4|Color (Sub Graph: an output of the Sub Graph node)" },
				{ "output.set", "--name Out [--rename New] [--type Vector3] (Sub Graph)" },
				{ "output.delete", "--name Out (Sub Graph)" },
				{ "settings", "[--material Lit|Unlit|Decal] [--surface Opaque|Transparent] [--alpha-clip true|false] [--path \"Shader Graphs\"] (Graph Settings; path = shader name prefix)" },
				{ "node.add", "--type Multiply [--x 0 --y 0] [--values {\"B\":[1,0,0,1]}] [--options {\"mask\":\"xy\"}] → id. Sub Graph: --options {\"asset\":\"Assets/x.shadersubgraph\"}; Custom Function: --options {\"name\",\"mode\":\"String|File\",\"body\",\"file\",\"inputs\":[{\"name\",\"type\"}],\"outputs\":[...]}" },
				{ "node.set", "--id N [--values {...}] [--options {...}] [--x --y]  (values / options merge; null removes)" },
				{ "node.delete", "--id N" },
				{ "connect", "--from N [--out Port] --to M|Master --in Port  (an input keeps one link: a new one replaces it)" },
				{ "disconnect", "--to M|Master --in Port" },
				{ "property.add", "--name Tint --type Float|Color|Vector2|Vector3|Vector4|Texture2D [--value ...] [--range 0,1] [--texture Assets/x.png] [--ref _Tint] [--node] [--x --y] → ref (--node also adds a Property node)" },
				{ "property.set", "--ref _Tint [--name] [--value] [--range a,b|false] [--texture]" },
				{ "property.delete", "--ref _Tint (its Property nodes too)" },
				{ "compile", "generate the shader code without saving [--hlsl] → error / line count (GPU compile happens on save)" },
				{ "material", "[--mat Assets/x.mat]: a material that uses this graph (next to the graph by default) → path" },
				{ "undo", "" },
				{ "redo", "" },
			};
		}
	}

	Document& Doc()
	{
		static Document d;
		return d;
	}

	void Document::Snapshot()
	{
		UndoStack.push_back(G.ToJson());
		if (UndoStack.size() > kUndoMax) UndoStack.erase(UndoStack.begin());
		RedoStack.clear();
	}

	bool Document::Undo()
	{
		if (UndoStack.empty()) return false;
		RedoStack.push_back(G.ToJson());
		std::string e;
		G.FromJson(UndoStack.back(), e);
		UndoStack.pop_back();
		Changed();
		return true;
	}

	bool Document::Redo()
	{
		if (RedoStack.empty()) return false;
		UndoStack.push_back(G.ToJson());
		std::string e;
		G.FromJson(RedoStack.back(), e);
		RedoStack.pop_back();
		Changed();
		return true;
	}

	json NodeJson(const Graph& g, const Node& n)
	{
		json inputs = json::object();
		if (const NodeDef* d = DefOf(n))
			for (const PortDef& p : d->In)
			{
				json in = { { "width", p.Width } };
				for (const Edge& e : g.Edges)
					if (e.ToNode == n.Id && e.ToPort == p.Name)
						in["link"] = { e.FromNode, e.FromPort };
				if (!in.contains("link"))
				{
					if (n.Values.contains(p.Name)) in["value"] = n.Values[p.Name];
					else if (!p.Bind.empty()) in["bind"] = p.Bind;
				}
				inputs[p.Name] = in;
			}
		json outputs = json::object();
		if (n.Type == "Property")
			outputs["Out"] = OutputWidth(g, n, "Out");
		else if (const NodeDef* d = DefOf(n))
			for (const PortDef& p : d->Out)
				outputs[p.Name] = OutputWidth(g, n, p.Name);
		return { { "id", n.Id }, { "type", n.Type }, { "pos", { n.X, n.Y } }, { "inputs", inputs }, { "outputs", outputs }, { "options", n.Options } };
	}

	json Document::Summary() const
	{
		const json props = G.ToJson()["properties"];
		json nodes = json::array();
		for (const Node& n : G.Nodes)
			nodes.push_back(NodeJson(G, n));
		json master = json::object();
		for (const PortDef& p : MasterInputs(G))
		{
			json m = json();
			for (const Edge& e : G.Edges)
				if (e.ToNode == 0 && e.ToPort == p.Name)
					m = { e.FromNode, e.FromPort };
			master[p.Name] = m;
		}
		json outputs = json::array();
		for (const SubOutput& o : G.Outputs) outputs.push_back({ { "name", o.Name }, { "type", o.Type } });
		return { { "path", Asset }, { "kind", G.Kind }, { "outputs", outputs }, { "shader", Asset.empty() || G.IsSubGraph() ? "" : ShaderNameOf(Asset) }, { "dirty", Dirty }, { "material", G.Material },
			{ "surface", G.Surface }, { "alphaClip", G.AlphaClip }, { "shaderPath", G.Path }, { "compiling", !Asset.empty() && IsCompiling(ShaderNameOf(Asset)) },
			{ "properties", props }, { "nodes", nodes }, { "master", master }, { "error", Asset.empty() ? "" : LastError(ShaderNameOf(Asset)) } };
	}

	const std::vector<OpInfo>& Ops()
	{
		static const std::vector<OpInfo> ops = BuildOps();
		return ops;
	}

	Graph NewGraph(const std::string& material)
	{
		Graph g;
		g.Material = material == "Unlit" || material == "Decal" ? material : "Lit";
		return g;
	}

	bool OpenDoc(const std::string& assetPath, std::string& error)
	{
		Graph g;
		if (!g.Load(FullPath(assetPath), error))
			return false;
		Document& d = Doc();
		d.G = std::move(g);
		d.Asset = wstring_to_string(PathManager::GetI()->GetCutSolutionPath(FullPath(assetPath)));
		d.Dirty = false;
		d.UndoStack.clear();
		d.RedoStack.clear();
		++d.Revision;
		return true;
	}

	bool SaveDoc(const std::string& assetPath, std::string& error, bool wait)
	{
		Document& d = Doc();
		std::string path = assetPath.empty() ? d.Asset : assetPath;
		if (path.empty()) { error = "no path (save <Assets/...shadergraph>)"; return false; }
		const std::string ext = Lower(fs::path(path).extension().string());
		if (ext != kExtension && ext != kSubExtension) path += d.G.IsSubGraph() ? kSubExtension : kExtension;
		if (!d.G.Save(FullPath(path), error))
			return false;
		d.Asset = wstring_to_string(PathManager::GetI()->GetCutSolutionPath(FullPath(path)));
		d.Dirty = false;
		++d.Revision;
		if (d.G.IsSubGraph())
		{
			// Sub Graph 는 셰이더가 아니다 — 쓰는 그래프들은 파일이 바뀐 것을 보고 다시 만든다 (UpdateRuntime). 기다리면 지금
			if (wait)
				RebuildUsers(d.Asset, error);
			return error.empty();
		}
		return Reload(d.Asset, error, wait);
	}

	bool RunOp(const std::string& op, const json& a, json& r, std::string& e)
	{
		Document& d = Doc();
		Graph& g = d.G;
		const std::string path = a.value("path", std::string());

		if (op == "help")
		{
			r = json::object();
			for (const OpInfo& o : Ops()) r[o.Name] = o.Help;
			return true;
		}
		if (op == "nodes")
		{
			const std::string cat = Lower(a.value("category", std::string()));
			r = json::array();
			for (const NodeDef& def : NodeDefs())
			{
				if (!cat.empty() && Lower(def.Category).find(cat) == std::string::npos) continue;
				json in = json::object(), out = json::object();
				for (const PortDef& p : def.In) in[p.Name] = p.Bind.empty() ? json(p.Width) : json({ p.Width, p.Bind });
				for (const PortDef& p : def.Out) out[p.Name] = p.Width;
				json n = { { "type", def.Type }, { "category", def.Category }, { "in", in }, { "out", out } };
				if (!def.DefaultOptions.empty()) n["options"] = def.DefaultOptions;
				if (!def.Help.empty()) n["help"] = def.Help;
				r.push_back(n);
			}
			return true;
		}
		if (op == "new")
		{
			if (path.empty()) { e = "new <Assets/...shadergraph>"; return false; }
			std::string p = path;
			const std::string ext = Lower(fs::path(p).extension().string());
			if (ext != kExtension && ext != kSubExtension) p += kExtension;
			std::error_code ec;
			if (fs::exists(FullPath(p), ec) && !a.value("force", false)) { e = p + " exists (--force to replace)"; return false; }
			d.G = NewGraph(a.value("material", std::string("Lit")));
			if (Lower(fs::path(p).extension().string()) == kSubExtension)
			{
				d.G.Kind = "SubGraph";
				d.G.Outputs = { { "Out", "Vector3" } };
			}
			d.UndoStack.clear();
			d.RedoStack.clear();
			if (!SaveDoc(p, e)) return false;
			r = d.Summary();
			return true;
		}
		if (op == "open")
		{
			if (path.empty()) { e = "open <Assets/...shadergraph>"; return false; }
			if (!OpenDoc(path, e)) return false;
			r = d.Summary();
			return true;
		}
		if (op == "save")
		{
			const bool ok = SaveDoc(path, e);
			r = { { "path", d.Asset }, { "built", ok } };
			if (d.G.IsSubGraph()) r["kind"] = "SubGraph";
			else r["shader"] = ShaderNameOf(d.Asset);
			if (!ok) { r["error"] = e; e = "saved, but the shader did not build: " + e; }
			return ok;
		}
		if (op == "info")
		{
			r = d.Summary();
			return true;
		}
		if (op == "undo" || op == "redo")
		{
			if (!(op == "undo" ? d.Undo() : d.Redo())) { e = "nothing to " + op; return false; }
			r = d.Summary();
			return true;
		}
		if (op == "settings")
		{
			std::string m = a.value("material", g.Material);
			if (Lower(m) == "lit") m = "Lit";
			else if (Lower(m) == "unlit") m = "Unlit";
			else if (Lower(m) == "decal") m = "Decal";   // Decal Projector 의 재질 (Unity 의 Decal Graph)
			else { e = "material must be Lit, Unlit or Decal"; return false; }
			std::string surface = a.value("surface", g.Surface);
			if (Lower(surface) == "opaque") surface = "Opaque";
			else if (Lower(surface) == "transparent") surface = "Transparent";
			else { e = "surface must be Opaque or Transparent"; return false; }
			bool clip = g.AlphaClip;
			if (a.contains("alpha-clip")) clip = a["alpha-clip"].is_boolean() ? a["alpha-clip"].get<bool>() : Lower(a["alpha-clip"].dump()).find("true") != std::string::npos;
			if (a.contains("alphaClip") && a["alphaClip"].is_boolean()) clip = a["alphaClip"].get<bool>();
			std::string path = a.value("path", g.Path);
			while (!path.empty() && (path.back() == '/' || path.back() == ' ')) path.pop_back();
			if (path.empty()) { e = "path must not be empty (default: Shader Graphs)"; return false; }
			d.Snapshot();
			g.Material = m;
			g.Surface = surface;
			g.AlphaClip = clip;
			g.Path = path;
			// 새 Master 에 없는 입력의 선은 지운다 (Unlit 의 Normal, Alpha Clipping 을 끈 Threshold …)
			g.Edges.erase(std::remove_if(g.Edges.begin(), g.Edges.end(), [&](const Edge& x) { return x.ToNode == 0 && InPortName(g, 0, x.ToPort).empty(); }), g.Edges.end());
			d.Changed();
			r = { { "material", g.Material }, { "surface", g.Surface }, { "alphaClip", g.AlphaClip }, { "path", g.Path } };
			return true;
		}
		if (op == "node.add")
		{
			const std::string type = a.value("type", std::string());
			const NodeDef* def = nullptr;
			for (const NodeDef& x : NodeDefs()) if (Lower(x.Type) == Lower(type)) def = &x;
			if (!def) { e = "no node type '" + type + "' (nova shadergraph nodes)"; return false; }
			if (a.contains("options") && !a["options"].is_object()) { e = "--options must be a JSON object, got: " + a["options"].dump(); return false; }
			if (a.contains("values") && !a["values"].is_object()) { e = "--values must be a JSON object, got: " + a["values"].dump(); return false; }
			d.Snapshot();
			const int id = g.AddNode(def->Type, a.value("x", 0.0f), a.value("y", 0.0f));
			Node* n = g.FindNode(id);
			if (a.contains("options") && a["options"].is_object())
				n->Options.merge_patch(a["options"]);
			if (a.contains("values") && a["values"].is_object())
				for (auto it = a["values"].begin(); it != a["values"].end(); ++it)
				{
					const PortDef* p = FindIn(*DefOf(*n), it.key());
					if (!p) { g.RemoveNode(id); d.UndoStack.pop_back(); e = "no input '" + it.key() + "' on " + def->Type; return false; }
					n->Values[p->Name] = it.value();
				}
			if (def->Type == "Property")
			{
				const std::string ref = n->Options.value("ref", std::string());
				if (!ref.empty() && !g.FindProperty(ref))
					if (Property* p = FindPropertyMutable(g, ref)) n->Options["ref"] = p->Ref;
			}
			d.Changed();
			r = NodeJson(g, *n);
			return true;
		}
		if (op == "node.set" || op == "node.delete")
		{
			int id = -1;
			if (!a.contains("id") || !NodeIdArg(a["id"], id) || id == 0) { e = "--id N (a node, not Master)"; return false; }
			Node* n = g.FindNode(id);
			if (!n) { e = "no node " + std::to_string(id); return false; }
			d.Snapshot();
			if (op == "node.delete")
			{
				g.RemoveNode(id);
				d.Changed();
				r = { { "deleted", id } };
				return true;
			}
			if (a.contains("options") && !a["options"].is_object()) { d.UndoStack.pop_back(); e = "--options must be a JSON object, got: " + a["options"].dump(); return false; }
			if (a.contains("values") && !a["values"].is_object()) { d.UndoStack.pop_back(); e = "--values must be a JSON object, got: " + a["values"].dump(); return false; }
			// 설정 먼저 (Sub Graph · Custom Function 은 설정이 포트를 정한다)
			if (a.contains("options") && a["options"].is_object())
				n->Options.merge_patch(a["options"]);
			const NodeDef* def = DefOf(*n);
			if (a.contains("values") && a["values"].is_object())
				for (auto it = a["values"].begin(); it != a["values"].end(); ++it)
				{
					const PortDef* p = def ? FindIn(*def, it.key()) : nullptr;
					if (!p) { d.Undo(); d.RedoStack.clear(); e = "no input '" + it.key() + "' on " + n->Type; return false; }
					if (it.value().is_null()) n->Values.erase(p->Name);
					else n->Values[p->Name] = it.value();
				}
			if (a.contains("x")) n->X = a["x"].get<float>();
			if (a.contains("y")) n->Y = a["y"].get<float>();
			d.Changed();
			r = NodeJson(g, *n);
			return true;
		}
		if (op == "connect")
		{
			int from = -1, to = -1;
			if (!a.contains("from") || !NodeIdArg(a["from"], from)) { e = "--from N"; return false; }
			if (!a.contains("to") || !NodeIdArg(a["to"], to)) { e = "--to M|Master"; return false; }
			const Node* src = g.FindNode(from);
			if (!src) { e = "no node " + std::to_string(from); return false; }
			const std::string out = OutPortName(g, *src, a.value("out", std::string()));
			if (out.empty()) { e = "no output '" + a.value("out", std::string()) + "' on node " + std::to_string(from) + " (" + src->Type + ")"; return false; }
			const std::string in = InPortName(g, to, a.value("in", std::string()));
			if (in.empty()) { e = "no input '" + a.value("in", std::string()) + "' on " + (to == 0 ? std::string("Master (") + g.Material + ")" : "node " + std::to_string(to)); return false; }
			d.Snapshot();
			if (!g.Connect(from, out, to, in, e)) { d.UndoStack.pop_back(); return false; }
			d.Changed();
			r = { { "from", { from, out } }, { "to", { to, in } } };
			return true;
		}
		if (op == "disconnect")
		{
			int to = -1;
			if (!a.contains("to") || !NodeIdArg(a["to"], to)) { e = "--to M|Master"; return false; }
			const std::string in = InPortName(g, to, a.value("in", std::string()));
			if (in.empty()) { e = "no input '" + a.value("in", std::string()) + "'"; return false; }
			d.Snapshot();
			g.Disconnect(to, in);
			d.Changed();
			r = { { "to", { to, in } } };
			return true;
		}
		if (op == "output.add" || op == "output.set" || op == "output.delete")
		{
			if (!g.IsSubGraph()) { e = "outputs belong to a Sub Graph (.shadersubgraph)"; return false; }
			const std::string name = a.value("name", std::string());
			if (name.empty()) { e = "--name"; return false; }
			auto it = std::find_if(g.Outputs.begin(), g.Outputs.end(), [&](const SubOutput& o) { return Lower(o.Name) == Lower(name); });
			auto validType = [&](std::string& t) {
				t = ProperType(t);
				return t == "Float" || t == "Vector2" || t == "Vector3" || t == "Vector4" || t == "Color";
			};
			if (op == "output.add")
			{
				if (it != g.Outputs.end()) { e = "an output '" + name + "' exists"; return false; }
				std::string type = a.value("type", std::string("Vector3"));
				if (!validType(type)) { e = "type must be Float, Vector2, Vector3, Vector4 or Color"; return false; }
				d.Snapshot();
				g.Outputs.push_back({ name, type });
			}
			else
			{
				if (it == g.Outputs.end()) { e = "no output '" + name + "'"; return false; }
				d.Snapshot();
				if (op == "output.delete")
				{
					g.Disconnect(0, it->Name);
					g.Outputs.erase(it);
				}
				else
				{
					if (a.contains("type"))
					{
						std::string type = a["type"].get<std::string>();
						if (!validType(type)) { d.UndoStack.pop_back(); e = "type must be Float, Vector2, Vector3, Vector4 or Color"; return false; }
						it->Type = type;
					}
					if (a.contains("rename"))
					{
						const std::string to = a["rename"].get<std::string>();
						for (Edge& x : g.Edges) if (x.ToNode == 0 && x.ToPort == it->Name) x.ToPort = to;
						it->Name = to;
					}
				}
			}
			d.Changed();
			r = d.Summary()["outputs"];
			return true;
		}
		if (op == "property.add")
		{
			Property p;
			p.Name = a.value("name", std::string("Property"));
			p.Type = ProperType(a.value("type", std::string("Float")));
			if (!ValidType(p.Type)) { e = "type must be Float, Color, Vector2, Vector3, Vector4 or Texture2D"; return false; }
			if (p.Type == "Color") p.Value[0] = p.Value[1] = p.Value[2] = p.Value[3] = 1.0f;
			p.Ref = a.contains("ref") && a["ref"].is_string() ? Sanitize(a["ref"].get<std::string>()) : g.UniqueRef(p.Name);
			if (g.FindProperty(p.Ref)) { e = "a property already uses the reference " + p.Ref; return false; }
			if (!ApplyPropertyArgs(p, a, e)) return false;
			d.Snapshot();
			g.Properties.push_back(p);
			r = { { "ref", p.Ref } };
			if (a.value("node", false))
			{
				const int id = g.AddNode("Property", a.value("x", 0.0f), a.value("y", 0.0f));
				g.FindNode(id)->Options["ref"] = p.Ref;
				r["node"] = id;
			}
			d.Changed();
			return true;
		}
		if (op == "property.set" || op == "property.delete")
		{
			Property* p = FindPropertyMutable(g, a.value("ref", std::string()));
			if (!p) { e = "no property '" + a.value("ref", std::string()) + "'"; return false; }
			d.Snapshot();
			if (op == "property.delete")
			{
				const std::string ref = p->Ref;
				std::vector<int> drop;
				for (const Node& n : g.Nodes)
					if (n.Type == "Property" && n.Options.value("ref", std::string()) == ref) drop.push_back(n.Id);
				for (int id : drop) g.RemoveNode(id);
				g.Properties.erase(std::remove_if(g.Properties.begin(), g.Properties.end(), [&](const Property& x) { return x.Ref == ref; }), g.Properties.end());
				d.Changed();
				r = { { "deleted", ref }, { "nodes", drop } };
				return true;
			}
			if (!ApplyPropertyArgs(*p, a, e)) { d.UndoStack.pop_back(); return false; }
			d.Changed();
			r = g.ToJson()["properties"][p - g.Properties.data()];
			return true;
		}
		if (op == "batch")
		{
			// 여러 연산을 한 요청으로 (Undo 한 번). 노드 id 는 차례로 붙는다 (info 의 nextId 부터)
			const json steps = a.value("steps", json::array());
			const size_t undo0 = d.UndoStack.size();
			json results = json::array();
			for (size_t i = 0; i < steps.size(); ++i)
			{
				json st = steps[i];
				const std::string sop = st.value("op", std::string());
				st.erase("op");
				json sr;
				if (sop == "batch" || !RunOp(sop, st, sr, e))
				{
					e = "step " + std::to_string(i + 1) + " (" + sop + "): " + (sop == "batch" ? std::string("no nested batch") : e);
					r = { { "done", results } };
					return false;
				}
				results.push_back(sr);
			}
			if (d.UndoStack.size() > undo0 + 1)
				d.UndoStack.erase(d.UndoStack.begin() + undo0 + 1, d.UndoStack.end());
			r = { { "steps", results } };
			return true;
		}
		if (op == "compile")
		{
			const CodeResult c = Generate(g);
			r = { { "ok", c.Error.empty() }, { "used", c.Used } };
			if (!c.Error.empty()) r["error"] = c.Error;
			else r["lines"] = std::count(c.Hlsl.begin(), c.Hlsl.end(), '\n');
			if (a.value("hlsl", false)) r["hlsl"] = c.Hlsl;
			return true;
		}
		if (op == "material")
		{
			if (d.Asset.empty()) { e = "save the graph first"; return false; }
			if (d.Dirty && !SaveDoc(d.Asset, e)) return false;
			const std::string mat = MakeMaterial(d.Asset, a.value("mat", std::string()), e);
			if (mat.empty()) return false;
			r = { { "material", mat }, { "shader", ShaderNameOf(d.Asset) } };
			return true;
		}
		e = "unknown op '" + op + "' (nova shadergraph help)";
		return false;
	}
}
