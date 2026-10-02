#include "pch.h"
#include "ModelOps.h"
#include "ModelRaster.h"
#include <filesystem>
#include <sstream>

namespace Modeling
{
	using json = nlohmann::json;

	namespace
	{
		constexpr float kDeg = 3.14159265358979f / 180.0f;

		struct Ctx
		{
			Document& D;
			const json& A;
			json& R;
			std::string& E;
		};
		using OpFn = std::function<bool(Ctx&)>;
		struct OpEntry { OpInfo Info; OpFn Fn; };

		LastOp s_Last;

		// ---- 인자 읽기
		bool GetVec3(const json& a, const char* key, Vec3& out)
		{
			if (!a.contains(key)) return false;
			const json& v = a[key];
			if (v.is_number()) { const float s = v.get<float>(); out = Vec3(s, s, s); return true; }
			if (v.is_array() && v.size() == 3 && v[0].is_number()) { out = Vec3(v[0].get<float>(), v[1].get<float>(), v[2].get<float>()); return true; }
			return false;
		}
		Vec3 Vec3Or(const json& a, const char* key, const Vec3& def) { Vec3 v = def; GetVec3(a, key, v); return v; }
		float F(const json& a, const char* key, float def) { return a.contains(key) && a[key].is_number() ? a[key].get<float>() : def; }
		int I(const json& a, const char* key, int def) { return a.contains(key) && a[key].is_number() ? a[key].get<int>() : def; }
		bool B(const json& a, const char* key, bool def) { return a.contains(key) && a[key].is_boolean() ? a[key].get<bool>() : (a.contains(key) && a[key].is_number() ? a[key].get<int>() != 0 : def); }
		std::string S(const json& a, const char* key, const std::string& def = "") { return a.contains(key) && a[key].is_string() ? a[key].get<std::string>() : def; }
		json V(const Vec3& v) { return { v.x, v.y, v.z }; }

		// x / y / z / 0 / 1 / 2 → 축 번호 (-1 = 없음)
		int Axis(const json& a, const char* key, int def)
		{
			if (!a.contains(key)) return def;
			if (a[key].is_number()) return std::clamp(a[key].get<int>(), 0, 2);
			const std::string s = a[key].get<std::string>();
			if (s.empty()) return def;
			const char c = (char)tolower(s.back());
			return c == 'x' ? 0 : (c == 'y' ? 1 : (c == 'z' ? 2 : def));
		}

		Object* Active(Ctx& c)
		{
			Object* o = c.D.ActiveObject();
			if (o == nullptr) c.E = "no active object (add one: model add --type cube, or select one: model object.select --name <name>)";
			return o;
		}

		Matrix ToLocal(const Object& o) { return o.World().Invert(); }
		Vec3 DirToLocal(const Object& o, const Vec3& d) { return Vec3::TransformNormal(d, ToLocal(o)); }

		// 편집 연산 결과: 요약 + "changed": {무엇: 수}
		bool Done(Ctx& c, int changed, const char* what = "elements")
		{
			c.R = c.D.Summary(false);
			c.R["changed"] = { { what, changed } };
			c.D.Changed();
			return true;
		}

		Mesh* EditMesh(Ctx& c)
		{
			Object* o = Active(c);
			return o ? &o->M : nullptr;
		}

		// ---- 오브젝트 · 프리미티브
		bool AddPrimitive(Ctx& c)
		{
			const std::string type = S(c.A, "type", "cube");
			const Vec3 loc = Vec3Or(c.A, "location", Vec3(0, 0, 0));
			const float size = F(c.A, "size", 1.0f);
			const float radius = F(c.A, "radius", size * 0.5f);
			// Edit 모드면 활성 메시에 더한다 (Blender 와 같음), 아니면 새 오브젝트
			const bool intoMesh = c.D.EditMode && c.D.ActiveObject() != nullptr;
			Mesh tmp;
			const Vec3 at = intoMesh ? Vec3(0, 0, 0) : Vec3(0, 0, 0);
			int n = 0;
			if (type == "cube") n = tmp.AddCube(Vec3Or(c.A, "dimensions", Vec3(size, size, size)), at);
			else if (type == "plane") n = tmp.AddPlane(Vec2(F(c.A, "sizeX", size), F(c.A, "sizeZ", size)), at, I(c.A, "cutsX", 1), I(c.A, "cutsZ", 1));
			else if (type == "circle") n = tmp.AddCircle(I(c.A, "vertices", 32), radius, at, B(c.A, "fill", true));
			else if (type == "cylinder") n = tmp.AddCylinder(I(c.A, "vertices", 32), radius, F(c.A, "depth", size * 2.0f), at, B(c.A, "caps", true), F(c.A, "radiusTop", -1.0f));
			else if (type == "cone") n = tmp.AddCylinder(I(c.A, "vertices", 32), radius, F(c.A, "depth", size * 2.0f), at, true, F(c.A, "radiusTop", 0.0f));
			else if (type == "uvsphere" || type == "sphere") n = tmp.AddUVSphere(I(c.A, "segments", 32), I(c.A, "rings", 16), radius, at);
			else if (type == "icosphere") n = tmp.AddIcoSphere(I(c.A, "subdivisions", 2), radius, at);
			else if (type == "torus") n = tmp.AddTorus(I(c.A, "majorSegments", 48), I(c.A, "minorSegments", 12), F(c.A, "majorRadius", 1.0f), F(c.A, "minorRadius", 0.25f), at);
			else { c.E = "unknown type '" + type + "' (cube plane circle cylinder cone uvsphere icosphere torus)"; return false; }
			if (B(c.A, "smooth", false)) for (Face& f : tmp.Faces) f.Smooth = true;
			std::string name = S(c.A, "name");
			if (name.empty()) { name = type; name[0] = (char)toupper(name[0]); if (type == "uvsphere") name = "Sphere"; }
			if (intoMesh)
			{
				Object* o = c.D.ActiveObject();
				for (Vert& v : o->M.Verts) v.Sel = false;
				for (Face& f : o->M.Faces) f.Sel = false;
				o->M.SelEdges.clear();
				o->M.Append(tmp, Matrix::CreateTranslation(loc) * ToLocal(*o));
				o->M.Flush(SelectMode::Face);
				return Done(c, n, "faces_added");
			}
			const int idx = c.D.AddObject(name);
			Object& o = c.D.Objects[idx];
			o.M = tmp;
			o.Position = loc;
			c.R = c.D.Summary(false);
			c.R["object"] = o.Name;
			c.R["changed"] = { { "faces_added", n } };
			return true;
		}

		std::vector<int> SelectedObjects(Document& d)
		{
			std::vector<int> out;
			for (int i = 0; i < (int)d.Objects.size(); ++i) if (d.Objects[i].Selected) out.push_back(i);
			if (out.empty() && d.Active >= 0) out.push_back(d.Active);
			return out;
		}

		// ---- 고르기 공통: 결과를 모드에 맞게
		bool SelDone(Ctx& c, Mesh& m, SelectMode mode)
		{
			m.Flush(mode);
			c.R = c.D.Summary(false);
			c.D.Changed();
			return true;
		}

		bool Render(Ctx& c)
		{
			// 여러 시점 PNG: --dir 폴더 (front.png …) 또는 --path 하나
			std::vector<std::string> views;
			if (c.A.contains("views") && c.A["views"].is_array()) for (const auto& v : c.A["views"]) views.push_back(v.get<std::string>());
			else if (c.A.contains("views") && c.A["views"].is_string())
			{
				std::stringstream ss(c.A["views"].get<std::string>());
				std::string t;
				while (std::getline(ss, t, ',')) if (!t.empty()) views.push_back(t);
			}
			if (views.empty()) views = { "front", "right", "back", "top", "persp" };
			const std::string dir = S(c.A, "dir"), single = S(c.A, "path");
			if (dir.empty() && single.empty()) { c.E = "need --dir <folder> (one PNG per view) or --path <file.png>"; return false; }
			int w = 768, h = 768;
			if (c.A.contains("size"))
			{
				if (c.A["size"].is_array() && c.A["size"].size() == 2) { w = c.A["size"][0].get<int>(); h = c.A["size"][1].get<int>(); }
				else if (c.A["size"].is_number()) w = h = c.A["size"].get<int>();
			}
			w = std::clamp(w, 64, 4096);
			h = std::clamp(h, 64, 4096);
			RasterOptions opt;
			opt.Grid = B(c.A, "grid", true);
			opt.Wireframe = B(c.A, "wire", true);
			opt.Selection = B(c.A, "selection", opt.Wireframe);   // 깨끗한 그림 (--wire false) 이면 선택 표시도 끈다
			opt.XRay = B(c.A, "xray", false);
			const std::string shade = S(c.A, "shading", "solid");
			opt.Shade = shade == "toon" ? Shading::Toon : (shade == "normals" ? Shading::Normals : Shading::Solid);
			// 경계 상자 (보이는 것)
			Vec3 mn(FLT_MAX, FLT_MAX, FLT_MAX), mx(-FLT_MAX, -FLT_MAX, -FLT_MAX);
			for (const Object& o : c.D.Objects)
				if (o.Visible)
					for (const Vert& v : o.M.Verts) { const Vec3 p = Vec3::Transform(v.P, o.World()); mn = Vec3::Min(mn, p); mx = Vec3::Max(mx, p); }
			if (mn.x > mx.x) { mn = Vec3(-1, 0, -1); mx = Vec3(1, 2, 1); }
			Raster r;
			r.Resize(w, h);
			json files = json::array();
			for (size_t i = 0; i < views.size() && (single.empty() || i == 0); ++i)
			{
				ViewCamera cam;
				if (!cam.SetPreset(views[i])) { c.E = "unknown view '" + views[i] + "' (front back left right top bottom persp three-quarter front-persp)"; return false; }
				cam.Frame(mn, mx, w / (float)h);
				if (c.A.contains("zoom")) cam.Distance /= (std::max)(0.05f, F(c.A, "zoom", 1.0f));
				Vec3 target;
				if (GetVec3(c.A, "target", target)) cam.Target = target;
				r.Render(c.D, cam, opt);
				const std::string path = single.empty() ? U8String(PathU8(dir) / PathU8(views[i] + ".png")) : single;
				if (!r.SavePng(path, c.E)) return false;
				files.push_back(path);
			}
			c.R = c.D.Summary(true);
			c.R["files"] = files;
			return true;
		}

		bool Get(Ctx& c)
		{
			// 점 · 면 · 변 목록 (월드 좌표) — AI 가 번호를 찾는 데
			Object* o = Active(c);
			if (!o) return false;
			Mesh& m = o->M;
			const std::string what = S(c.A, "what", "verts");
			const bool selOnly = B(c.A, "selected", false);
			const int limit = I(c.A, "limit", 2000);
			const Matrix w = o->World();
			json list = json::array();
			int total = 0;
			if (what == "verts")
			{
				for (int i = 0; i < (int)m.Verts.size(); ++i)
				{
					if (selOnly && !m.Verts[i].Sel) continue;
					++total;
					if ((int)list.size() >= limit) continue;
					const Vec3 p = Vec3::Transform(m.Verts[i].P, w);
					list.push_back({ i, roundf(p.x * 1e4f) / 1e4f, roundf(p.y * 1e4f) / 1e4f, roundf(p.z * 1e4f) / 1e4f });
				}
				c.R["format"] = "[index, x, y, z]";
			}
			else if (what == "faces")
			{
				Matrix nw = w; nw.Translation(Vec3(0, 0, 0));
				for (int f = 0; f < (int)m.Faces.size(); ++f)
				{
					if (selOnly && !m.Faces[f].Sel) continue;
					++total;
					if ((int)list.size() >= limit) continue;
					Vec3 n = Vec3::TransformNormal(m.FaceNormal(f), nw); n.Normalize();
					const Vec3 ct = Vec3::Transform(m.FaceCenter(f), w);
					list.push_back({ f, m.Faces[f].V, { roundf(n.x * 1e3f) / 1e3f, roundf(n.y * 1e3f) / 1e3f, roundf(n.z * 1e3f) / 1e3f },
						{ roundf(ct.x * 1e4f) / 1e4f, roundf(ct.y * 1e4f) / 1e4f, roundf(ct.z * 1e4f) / 1e4f } });
				}
				c.R["format"] = "[index, [verts], normal, center]";
			}
			else if (what == "edges")
			{
				const auto& edges = m.Edges();
				for (int e = 0; e < (int)edges.size(); ++e)
				{
					if (selOnly && !m.SelEdges.count(EdgeKey(edges[e].A, edges[e].B))) continue;
					++total;
					if ((int)list.size() < limit) list.push_back({ e, edges[e].A, edges[e].B, (int)edges[e].Faces.size() });
				}
				c.R["format"] = "[index, a, b, faceCount]";
			}
			else { c.E = "what = verts | faces | edges"; return false; }
			c.R["object"] = o->Name;
			c.R["total"] = total;
			c.R["items"] = list;
			if (total > (int)list.size()) c.R["truncated"] = true;
			return true;
		}

		// 변 지정: --edge 번호 또는 --edge [a, b] (점 번호 둘)
		int EdgeArg(Mesh& m, const json& a)
		{
			if (!a.contains("edge")) return -1;
			if (a["edge"].is_number()) return a["edge"].get<int>();
			if (a["edge"].is_array() && a["edge"].size() == 2) return m.FindEdge(a["edge"][0].get<int>(), a["edge"][1].get<int>());
			return -1;
		}

		std::vector<OpEntry>& Registry()
		{
			static std::vector<OpEntry> ops;
			if (!ops.empty())
				return ops;
			auto add = [&](const char* name, const char* help, bool mutates, OpFn fn) { ops.push_back({ { name, help, mutates }, std::move(fn) }); };

			// ================= 문서
			add("info", "summary: objects, verts, faces, bounds, selection", false, [](Ctx& c) { c.R = c.D.Summary(true); return true; });
			add("get", "--what verts|faces|edges [--selected] [--limit 2000]: element list of the active object (world coords)", false, Get);
			add("new", "empty document", false, [](Ctx& c) { c.D.New(); c.R = c.D.Summary(true); return true; });
			add("open", "--path <file.nmodel>", false, [](Ctx& c) {
				if (!c.D.Load(S(c.A, "path"), c.E)) return false;
				c.R = c.D.Summary(true); return true; });
			add("save", "[--path <file.nmodel>] (default: last path)", false, [](Ctx& c) {
				std::string p = S(c.A, "path", c.D.Path);
				if (p.empty()) { c.E = "need --path <file.nmodel>"; return false; }
				if (!c.D.Save(p, c.E)) return false;
				c.R = c.D.Summary(false); c.R["path"] = p; return true; });
			add("import", "--path <model.fbx|obj|gltf|glb|dae…> [--append]: read with Assimp (keeps quads/ngons, bakes node transforms, meters)", true, [](Ctx& c) {
				if (!c.D.Import(S(c.A, "path"), B(c.A, "append", false), c.E)) return false;
				c.R = c.D.Summary(true); return true; });
			add("export", "--path <out.fbx|obj|glb> [--selected]: FBX 7.4 binary / OBJ / GLB (meters, Y up)", false, [](Ctx& c) {
				const std::string p = S(c.A, "path");
				if (!c.D.Export(p, B(c.A, "selected", false), c.E)) return false;
				c.R = c.D.Summary(false); c.R["path"] = p; return true; });
			add("undo", "undo the last change", false, [](Ctx& c) { if (!c.D.Undo()) { c.E = "nothing to undo"; return false; } c.R = c.D.Summary(false); return true; });
			add("redo", "redo", false, [](Ctx& c) { if (!c.D.Redo()) { c.E = "nothing to redo"; return false; } c.R = c.D.Summary(false); return true; });
			add("render", "--dir <folder> [--views front,right,back,top,persp] [--size 768 | [w,h]] [--shading solid|toon|normals] [--wire true|false] [--selection] [--grid] [--xray] [--zoom 1] [--target [x,y,z]]: PNG per view", false, Render);

			// ================= 오브젝트
			add("add", "--type cube|plane|circle|cylinder|cone|uvsphere|icosphere|torus [--size 1] [--radius] [--depth] [--vertices 32] [--segments 32 --rings 16] [--subdivisions 2] [--location [x,y,z]] [--name] [--smooth]: new object (edit mode: adds into the active mesh)", true, AddPrimitive);
			add("mode", "--mode object|edit [--select vertex|edge|face]", false, [](Ctx& c) {
				const std::string m = S(c.A, "mode");
				if (m == "edit") { if (!Active(c)) return false; c.D.EditMode = true; }
				else if (m == "object") c.D.EditMode = false;
				const std::string s = S(c.A, "select");
				if (s == "vertex" || s == "vert") c.D.Mode = SelectMode::Vertex;
				else if (s == "edge") c.D.Mode = SelectMode::Edge;
				else if (s == "face") c.D.Mode = SelectMode::Face;
				if (Object* o = c.D.ActiveObject(); o && !s.empty()) o->M.Flush(c.D.Mode);
				c.D.Changed();
				c.R = c.D.Summary(false); return true; });
			add("object.select", "--name <name> | --names [..] [--extend] | --all true|false: select objects (first = active)", false, [](Ctx& c) {
				if (c.A.contains("all")) { for (Object& o : c.D.Objects) o.Selected = B(c.A, "all", true); c.R = c.D.Summary(true); c.D.Changed(); return true; }
				std::vector<std::string> names;
				if (c.A.contains("names")) names = c.A["names"].get<std::vector<std::string>>();
				if (c.A.contains("name")) names.insert(names.begin(), S(c.A, "name"));
				if (!B(c.A, "extend", false)) for (Object& o : c.D.Objects) o.Selected = false;
				for (size_t i = 0; i < names.size(); ++i)
				{
					const int idx = c.D.Find(names[i]);
					if (idx < 0) { c.E = "no object named '" + names[i] + "'"; return false; }
					c.D.Objects[idx].Selected = true;
					if (i == 0) c.D.Active = idx;
				}
				c.D.Changed();
				c.R = c.D.Summary(true); return true; });
			add("object.delete", "delete selected objects", true, [](Ctx& c) {
				int n = 0;
				for (int i = (int)c.D.Objects.size() - 1; i >= 0; --i) if (c.D.Objects[i].Selected) { c.D.Objects.erase(c.D.Objects.begin() + i); ++n; }
				c.D.Active = c.D.Objects.empty() ? -1 : 0;
				c.D.EditMode = false;
				return Done(c, n, "deleted"); });
			add("object.duplicate", "[--offset [x,y,z]]: duplicate selected objects (new ones selected)", true, [](Ctx& c) {
				const Vec3 off = Vec3Or(c.A, "offset", Vec3(0, 0, 0));
				std::vector<int> sel = SelectedObjects(c.D);
				for (int i : sel) c.D.Objects[i].Selected = false;
				for (int i : sel)
				{
					Object o = c.D.Objects[i];
					o.Name = c.D.UniqueName(o.Name);
					o.Position += off;
					o.Selected = true;
					c.D.Objects.push_back(o);
					c.D.Active = (int)c.D.Objects.size() - 1;
				}
				return Done(c, (int)sel.size(), "duplicated"); });
			add("object.rename", "--name <new> [--object <old>]", true, [](Ctx& c) {
				Object* o = c.A.contains("object") ? (c.D.Find(S(c.A, "object")) >= 0 ? &c.D.Objects[c.D.Find(S(c.A, "object"))] : nullptr) : c.D.ActiveObject();
				if (!o) { c.E = "no such object"; return false; }
				o->Name = c.D.UniqueName(S(c.A, "name", o->Name));
				return Done(c, 1, "renamed"); });
			add("object.transform", "[--object name] [--location [x,y,z]] [--rotation [x,y,z] degrees] [--scale s|[x,y,z]]: set object transform", true, [](Ctx& c) {
				Object* o = c.A.contains("object") ? (c.D.Find(S(c.A, "object")) >= 0 ? &c.D.Objects[c.D.Find(S(c.A, "object"))] : nullptr) : c.D.ActiveObject();
				if (!o) { c.E = "no such object"; return false; }
				GetVec3(c.A, "location", o->Position);
				Vec3 r;
				if (GetVec3(c.A, "rotation", r)) o->Rotation = Quaternion::CreateFromYawPitchRoll(r.y * kDeg, r.x * kDeg, r.z * kDeg);
				GetVec3(c.A, "scale", o->Scale);
				return Done(c, 1); });
			add("object.apply", "bake location/rotation/scale of selected objects into their meshes", true, [](Ctx& c) {
				std::vector<int> sel = SelectedObjects(c.D);
				for (int i : sel)
				{
					Object& o = c.D.Objects[i];
					const Matrix w = o.World();
					for (Vert& v : o.M.Verts) v.P = Vec3::Transform(v.P, w);
					// 거울 크기 (음수) 면 면 방향을 뒤집는다
					if (o.Scale.x * o.Scale.y * o.Scale.z < 0.0f)
						for (Face& f : o.M.Faces) { std::reverse(f.V.begin(), f.V.end()); std::reverse(f.UV.begin(), f.UV.end()); }
					o.Position = Vec3(0, 0, 0); o.Rotation = Quaternion::Identity; o.Scale = Vec3(1, 1, 1);
					o.M.Touch();
				}
				return Done(c, (int)sel.size(), "applied"); });
			add("object.join", "join selected objects into the active one", true, [](Ctx& c) {
				Object* a = Active(c);
				if (!a) return false;
				const Matrix toActive = ToLocal(*a);
				int n = 0;
				const std::string activeName = a->Name;
				for (int i = (int)c.D.Objects.size() - 1; i >= 0; --i)
				{
					Object& o = c.D.Objects[i];
					if (!o.Selected || o.Name == activeName) continue;
					c.D.Objects[c.D.Find(activeName)].M.Append(o.M, o.World() * toActive);
					c.D.Objects.erase(c.D.Objects.begin() + i);
					++n;
				}
				c.D.Active = c.D.Find(activeName);
				return Done(c, n, "joined"); });
			add("object.visible", "--visible true|false [--object name]", true, [](Ctx& c) {
				Object* o = c.A.contains("object") ? (c.D.Find(S(c.A, "object")) >= 0 ? &c.D.Objects[c.D.Find(S(c.A, "object"))] : nullptr) : c.D.ActiveObject();
				if (!o) { c.E = "no such object"; return false; }
				o->Visible = B(c.A, "visible", true);
				return Done(c, 1); });

			// ================= 고르기 (활성 메시)
			add("select.all", "select everything", false, [](Ctx& c) { Mesh* m = EditMesh(c); if (!m) return false; m->SelectAll(true); return SelDone(c, *m, SelectMode::Face); });
			add("select.none", "deselect", false, [](Ctx& c) { Mesh* m = EditMesh(c); if (!m) return false; m->SelectAll(false); c.R = c.D.Summary(false); c.D.Changed(); return true; });
			add("select.invert", "invert (in the current select mode)", false, [](Ctx& c) { Mesh* m = EditMesh(c); if (!m) return false; m->InvertSelection(c.D.Mode); c.R = c.D.Summary(false); c.D.Changed(); return true; });
			add("select.verts", "--ids [..] [--extend]", false, [](Ctx& c) {
				Mesh* m = EditMesh(c); if (!m) return false;
				if (!B(c.A, "extend", false)) m->SelectAll(false);
				for (int i : c.A.value("ids", std::vector<int>())) if (i >= 0 && i < (int)m->Verts.size()) m->Verts[i].Sel = true;
				return SelDone(c, *m, SelectMode::Vertex); });
			add("select.faces", "--ids [..] [--extend]", false, [](Ctx& c) {
				Mesh* m = EditMesh(c); if (!m) return false;
				if (!B(c.A, "extend", false)) m->SelectAll(false);
				for (int i : c.A.value("ids", std::vector<int>())) if (i >= 0 && i < (int)m->Faces.size()) m->Faces[i].Sel = true;
				return SelDone(c, *m, SelectMode::Face); });
			add("select.edges", "--ids [..] (edge indices) | --pairs [[a,b],..] [--extend]", false, [](Ctx& c) {
				Mesh* m = EditMesh(c); if (!m) return false;
				if (!B(c.A, "extend", false)) m->SelectAll(false);
				const auto& edges = m->Edges();
				for (int i : c.A.value("ids", std::vector<int>())) if (i >= 0 && i < (int)edges.size()) m->SelEdges.insert(EdgeKey(edges[i].A, edges[i].B));
				if (c.A.contains("pairs")) for (const auto& p : c.A["pairs"]) if (m->FindEdge(p[0], p[1]) >= 0) m->SelEdges.insert(EdgeKey(p[0], p[1]));
				return SelDone(c, *m, SelectMode::Edge); });
			add("select.box", "--min [x,y,z] --max [x,y,z] [--extend] [--faces]: vertices (or face centers) inside a world box", false, [](Ctx& c) {
				Object* o = Active(c); if (!o) return false;
				Mesh& m = o->M;
				const Vec3 mn = Vec3Or(c.A, "min", Vec3(-FLT_MAX, -FLT_MAX, -FLT_MAX)), mx = Vec3Or(c.A, "max", Vec3(FLT_MAX, FLT_MAX, FLT_MAX));
				if (!B(c.A, "extend", false)) m.SelectAll(false);
				const Matrix w = o->World();
				auto inside = [&](const Vec3& p) { return p.x >= mn.x && p.y >= mn.y && p.z >= mn.z && p.x <= mx.x && p.y <= mx.y && p.z <= mx.z; };
				if (B(c.A, "faces", false))
				{
					for (int f = 0; f < (int)m.Faces.size(); ++f) if (inside(Vec3::Transform(m.FaceCenter(f), w))) m.Faces[f].Sel = true;
					return SelDone(c, m, SelectMode::Face);
				}
				for (Vert& v : m.Verts) if (inside(Vec3::Transform(v.P, w))) v.Sel = true;
				return SelDone(c, m, SelectMode::Vertex); });
			add("select.normal", "--direction [x,y,z] [--angle 30] [--extend]: faces whose normal points this way", false, [](Ctx& c) {
				Object* o = Active(c); if (!o) return false;
				if (!B(c.A, "extend", false)) o->M.SelectAll(false);
				o->M.SelectByNormal(DirToLocal(*o, Vec3Or(c.A, "direction", Vec3(0, 1, 0))), F(c.A, "angle", 30.0f));
				c.R = c.D.Summary(false); c.D.Changed(); return true; });
			add("select.linked", "grow to everything connected", false, [](Ctx& c) { Mesh* m = EditMesh(c); if (!m) return false; m->SelectLinked(); c.R = c.D.Summary(false); c.D.Changed(); return true; });
			add("select.loop", "--edge <index>|[a,b] [--extend]: edge loop", false, [](Ctx& c) {
				Mesh* m = EditMesh(c); if (!m) return false;
				if (!B(c.A, "extend", false)) m->SelectAll(false);
				const int e = EdgeArg(*m, c.A);
				if (e < 0) { c.E = "need --edge <index> or --edge [a,b]"; return false; }
				m->SelectEdgeLoop(e); c.R = c.D.Summary(false); c.D.Changed(); return true; });
			add("select.ring", "--edge <index>|[a,b] [--extend]: edge ring", false, [](Ctx& c) {
				Mesh* m = EditMesh(c); if (!m) return false;
				if (!B(c.A, "extend", false)) m->SelectAll(false);
				const int e = EdgeArg(*m, c.A);
				if (e < 0) { c.E = "need --edge <index> or --edge [a,b]"; return false; }
				m->SelectEdgeRing(e); c.R = c.D.Summary(false); c.D.Changed(); return true; });
			add("select.grow", "grow selection by one ring", false, [](Ctx& c) { Mesh* m = EditMesh(c); if (!m) return false; m->GrowSelection(); c.R = c.D.Summary(false); c.D.Changed(); return true; });
			add("select.shrink", "shrink selection", false, [](Ctx& c) { Mesh* m = EditMesh(c); if (!m) return false; m->ShrinkSelection(); c.R = c.D.Summary(false); c.D.Changed(); return true; });

			// ================= 변환 (Edit 모드 = 고른 점, Object 모드 = 고른 오브젝트)
			add("translate", "--delta [x,y,z] | --to [x,y,z] (moves selection center there)", true, [](Ctx& c) {
				if (!c.D.EditMode)
				{
					Vec3 d = Vec3Or(c.A, "delta", Vec3(0, 0, 0));
					std::vector<int> sel = SelectedObjects(c.D);
					for (int i : sel) c.D.Objects[i].Position += d;
					return Done(c, (int)sel.size(), "moved");
				}
				Object* o = Active(c); if (!o) return false;
				Vec3 to, d = Vec3Or(c.A, "delta", Vec3(0, 0, 0));
				if (GetVec3(c.A, "to", to)) d = to - Vec3::Transform(o->M.SelectionCenter(), o->World());
				return Done(c, o->M.Translate(DirToLocal(*o, d)), "moved"); });
			add("rotate", "--angle <deg> --axis x|y|z|[x,y,z] [--pivot [x,y,z]] (default: selection center)", true, [](Ctx& c) {
				Vec3 axis(0, 1, 0);
				if (!GetVec3(c.A, "axis", axis)) { const int a = Axis(c.A, "axis", 1); axis = Vec3(a == 0 ? 1.0f : 0.0f, a == 1 ? 1.0f : 0.0f, a == 2 ? 1.0f : 0.0f); }
				axis.Normalize();
				const Quaternion q = Quaternion::CreateFromAxisAngle(axis, F(c.A, "angle", 0.0f) * kDeg);
				if (!c.D.EditMode)
				{
					std::vector<int> sel = SelectedObjects(c.D);
					for (int i : sel) c.D.Objects[i].Rotation = c.D.Objects[i].Rotation * q;
					return Done(c, (int)sel.size(), "rotated");
				}
				Object* o = Active(c); if (!o) return false;
				// 월드 축 → 로컬: 로컬 공간에서의 회전 = W · R · W⁻¹ (회전 · 균일 크기 가정)
				const Vec3 la = DirToLocal(*o, axis);
				Vec3 lan = la; lan.Normalize();
				Vec3 pivotW;
				const Vec3 pivot = GetVec3(c.A, "pivot", pivotW) ? Vec3::Transform(pivotW, ToLocal(*o)) : o->M.SelectionCenter();
				return Done(c, o->M.Rotate(Quaternion::CreateFromAxisAngle(lan, F(c.A, "angle", 0.0f) * kDeg), pivot), "rotated"); });
			add("scale", "--factor s|[x,y,z] [--pivot [x,y,z]] (default: selection center)", true, [](Ctx& c) {
				const Vec3 s = Vec3Or(c.A, "factor", Vec3(1, 1, 1));
				if (!c.D.EditMode)
				{
					std::vector<int> sel = SelectedObjects(c.D);
					for (int i : sel) c.D.Objects[i].Scale *= s;
					return Done(c, (int)sel.size(), "scaled");
				}
				Object* o = Active(c); if (!o) return false;
				Vec3 pivotW;
				const Vec3 pivot = GetVec3(c.A, "pivot", pivotW) ? Vec3::Transform(pivotW, ToLocal(*o)) : o->M.SelectionCenter();
				return Done(c, o->M.Scale(s, pivot), "scaled"); });
			add("set.positions", "--positions [[index, x, y, z], ..] (world coords)", true, [](Ctx& c) {
				Object* o = Active(c); if (!o) return false;
				std::vector<std::pair<int, Vec3>> pos;
				const Matrix l = ToLocal(*o);
				if (c.A.contains("positions"))
					for (const auto& p : c.A["positions"])
						if (p.is_array() && p.size() == 4) pos.push_back({ p[0].get<int>(), Vec3::Transform(Vec3(p[1].get<float>(), p[2].get<float>(), p[3].get<float>()), l) });
				return Done(c, o->M.SetPositions(pos), "moved"); });

			// ================= 메시 편집 (활성 메시의 선택에)
			add("extrude", "[--distance 0.2] [--direction [x,y,z] (world offset)] [--individual]: extrude selected faces (or boundary edges)", true, [](Ctx& c) {
				Object* o = Active(c); if (!o) return false;
				if (B(c.A, "individual", false)) return Done(c, o->M.ExtrudeIndividual(F(c.A, "distance", 0.2f)), "faces");
				Vec3 dir;
				if (GetVec3(c.A, "direction", dir)) { const Vec3 l = DirToLocal(*o, dir); return Done(c, o->M.ExtrudeRegion(0.0f, &l), "faces"); }
				// 거리: 로컬 법선 방향 × distance (월드 크기 보정)
				const float s = (std::max)(1e-6f, (fabsf(o->Scale.x) + fabsf(o->Scale.y) + fabsf(o->Scale.z)) / 3.0f);
				return Done(c, o->M.ExtrudeRegion(F(c.A, "distance", 0.2f) / s), "faces"); });
			add("inset", "--thickness 0.05 [--depth 0] [--individual]", true, [](Ctx& c) {
				Mesh* m = EditMesh(c); if (!m) return false;
				return Done(c, m->Inset(F(c.A, "thickness", 0.05f), F(c.A, "depth", 0.0f), B(c.A, "individual", false)), "faces"); });
			add("subdivide", "[--cuts 1]: split selected faces (quads → 4, tris → 4)", true, [](Ctx& c) {
				Mesh* m = EditMesh(c); if (!m) return false;
				int n = 0;
				for (int i = 0; i < std::clamp(I(c.A, "cuts", 1), 1, 4); ++i) n = m->Subdivide(1);
				return Done(c, n, "faces"); });
			add("subsurf", "[--levels 1]: Catmull-Clark subdivision of the whole mesh (smooth)", true, [](Ctx& c) {
				Mesh* m = EditMesh(c); if (!m) return false;
				return Done(c, m->CatmullClark(I(c.A, "levels", 1)), "faces"); });
			add("loopcut", "--edge <index>|[a,b] [--cuts 1] [--factor 0.5]: cut a loop across the ring of quads through this edge", true, [](Ctx& c) {
				Mesh* m = EditMesh(c); if (!m) return false;
				const int e = EdgeArg(*m, c.A);
				if (e < 0) { c.E = "need --edge <index> or --edge [a,b] (see: model get --what edges)"; return false; }
				return Done(c, m->LoopCut(e, I(c.A, "cuts", 1), F(c.A, "factor", 0.5f)), "verts_added"); });
			add("bevel", "--offset 0.05: chamfer selected edges", true, [](Ctx& c) {
				Mesh* m = EditMesh(c); if (!m) return false;
				return Done(c, m->Bevel(F(c.A, "offset", 0.05f), I(c.A, "segments", 1)), "edges"); });
			add("delete", "--type verts|faces|only_faces", true, [](Ctx& c) {
				Mesh* m = EditMesh(c); if (!m) return false;
				const std::string t = S(c.A, "type", c.D.Mode == SelectMode::Face ? "faces" : "verts");
				if (t == "verts") return Done(c, m->DeleteVerts(), "deleted");
				if (t == "faces") return Done(c, m->DeleteFaces(false), "deleted");
				if (t == "only_faces") return Done(c, m->DeleteFaces(true), "deleted");
				c.E = "type = verts | faces | only_faces"; return false; });
			add("merge", "--type center|distance [--distance 0.0001]", true, [](Ctx& c) {
				Mesh* m = EditMesh(c); if (!m) return false;
				if (S(c.A, "type", "center") == "distance") return Done(c, m->MergeByDistance(F(c.A, "distance", 1e-4f), !m->SelectedVerts().empty()), "merged");
				return Done(c, m->MergeAtCenter(), "merged"); });
			add("fill", "make a face from the selected vertices", true, [](Ctx& c) { Mesh* m = EditMesh(c); if (!m) return false; return Done(c, m->Fill(), "faces"); });
			add("bridge", "connect two selected boundary loops with quads", true, [](Ctx& c) {
				Mesh* m = EditMesh(c); if (!m) return false;
				const int n = m->BridgeEdgeLoops();
				if (n == 0) { c.E = "select exactly two boundary edge loops with the same vertex count"; return false; }
				return Done(c, n, "faces"); });
			add("mirror", "--axis x|y|z [--merge true]: mirrored copy of the mesh (local axis) welded at the center", true, [](Ctx& c) {
				Mesh* m = EditMesh(c); if (!m) return false;
				return Done(c, m->Mirror(Axis(c.A, "axis", 0), B(c.A, "merge", true), F(c.A, "distance", 1e-4f)), "faces"); });
			add("symmetrize", "--direction +x|-x|+y|-y|+z|-z: copy one half onto the other (default +x → -x)", true, [](Ctx& c) {
				Mesh* m = EditMesh(c); if (!m) return false;
				const std::string d = S(c.A, "direction", "+x");
				return Done(c, m->Symmetrize(Axis(c.A, "direction", 0), d.empty() || d[0] != '-'), "faces"); });
			add("flip", "flip normals of selected faces (all if none)", true, [](Ctx& c) { Mesh* m = EditMesh(c); if (!m) return false; return Done(c, m->FlipNormals(), "faces"); });
			add("recalc_normals", "[--inside]: make normals consistent and point outside", true, [](Ctx& c) { Mesh* m = EditMesh(c); if (!m) return false; return Done(c, m->RecalculateNormals(B(c.A, "inside", false)), "flipped"); });
			add("smooth", "[--factor 0.5] [--iterations 1]: relax selected vertices", true, [](Ctx& c) {
				Mesh* m = EditMesh(c); if (!m) return false;
				return Done(c, m->Smooth(F(c.A, "factor", 0.5f), I(c.A, "iterations", 1)), "verts"); });
			add("shade", "--smooth true|false: Shade Smooth / Flat (selected faces, all if none)", true, [](Ctx& c) {
				Mesh* m = EditMesh(c); if (!m) return false;
				return Done(c, m->SetSmooth(B(c.A, "smooth", true)), "faces"); });
			add("triangulate", "split selected faces into triangles", true, [](Ctx& c) { Mesh* m = EditMesh(c); if (!m) return false; return Done(c, m->Triangulate(), "faces"); });
			add("duplicate", "[--offset [x,y,z]]: duplicate selected faces (copy selected)", true, [](Ctx& c) {
				Object* o = Active(c); if (!o) return false;
				const int n = o->M.Duplicate();
				o->M.Translate(DirToLocal(*o, Vec3Or(c.A, "offset", Vec3(0, 0, 0))));
				return Done(c, n, "faces"); });
			add("face.add", "--points [[x,y,z], ..]: one face through these world points (order = outward normal by (b-a)x(c-a))", true, [](Ctx& c) {
				Object* o = Active(c); if (!o) return false;
				std::vector<Vec3> pts;
				const Matrix l = ToLocal(*o);
				if (c.A.contains("points")) for (const auto& p : c.A["points"]) pts.push_back(Vec3::Transform(Vec3(p[0].get<float>(), p[1].get<float>(), p[2].get<float>()), l));
				if (pts.size() < 3) { c.E = "need at least 3 points"; return false; }
				for (Vert& v : o->M.Verts) v.Sel = false;
				for (Face& f : o->M.Faces) f.Sel = false;
				const int n = o->M.AddFace(pts);
				o->M.MergeByDistance(1e-5f, false);
				o->M.Flush(SelectMode::Face);
				return Done(c, n, "faces"); });
			add("material.set", "--index <n> [--name <name>]: material slot for selected faces", true, [](Ctx& c) {
				Mesh* m = EditMesh(c); if (!m) return false;
				const int idx = (std::max)(0, I(c.A, "index", 0));
				if ((int)c.D.Materials.size() <= idx) c.D.Materials.resize(idx + 1);
				if (c.A.contains("name")) c.D.Materials[idx] = S(c.A, "name");
				int n = 0;
				for (Face& f : m->Faces) if (f.Sel) { f.Material = idx; ++n; }
				return Done(c, n, "faces"); });
			return ops;
		}
	}

	const std::vector<OpInfo>& Ops()
	{
		static std::vector<OpInfo> infos;
		if (infos.empty())
			for (const OpEntry& e : Registry()) infos.push_back(e.Info);
		return infos;
	}

	bool RunOpNoUndo(const std::string& name, const json& args, json& result, std::string& error)
	{
		for (OpEntry& e : Registry())
		{
			if (e.Info.Name != name)
				continue;
			Ctx c{ Doc(), args, result, error };
			try
			{
				return e.Fn(c);
			}
			catch (const std::exception& ex)
			{
				error = std::string("bad arguments: ") + ex.what();
				return false;
			}
		}
		error = "unknown model op '" + name + "'";
		return false;
	}

	bool RunOp(const std::string& name, const json& args, json& result, std::string& error)
	{
		for (OpEntry& e : Registry())
		{
			if (e.Info.Name != name)
				continue;
			Document& d = Doc();
			if (e.Info.Mutates)
				d.PushUndo(name);
			Ctx c{ d, args, result, error };
			bool ok = false;
			try
			{
				ok = e.Fn(c);
			}
			catch (const std::exception& ex)
			{
				error = std::string("bad arguments: ") + ex.what();
				ok = false;
			}
			if (!ok && e.Info.Mutates)
				d.CancelUndo();   // 실패: 스냅숏으로 되돌리고 Undo 목록에서 뺀다
			if (ok && e.Info.Mutates)
				s_Last = { name, args };
			return ok;
		}
		error = "unknown model op '" + name + "' (nova model help)";
		return false;
	}

	const LastOp& Last() { return s_Last; }
	void SetLast(const std::string& name, const json& args) { s_Last = { name, args }; }

	bool RerunLast(const json& args, json& result, std::string& error)
	{
		if (s_Last.Name.empty())
		{
			error = "no last operation";
			return false;
		}
		const std::string name = s_Last.Name;
		Doc().Undo();
		return RunOp(name, args, result, error);
	}
}
