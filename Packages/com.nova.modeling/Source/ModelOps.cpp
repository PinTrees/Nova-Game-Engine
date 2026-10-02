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
			opt.Shade = shade == "toon" ? Shading::Toon : (shade == "normals" ? Shading::Normals : (shade == "uv" ? Shading::UVChecker : (shade == "weights" ? Shading::Weights : Shading::Solid)));
			opt.Bones = B(c.A, "bones", true);
			opt.WeightBone = S(c.A, "bone");
			opt.SelectedBone = c.D.Rig.Find(opt.WeightBone);
			if (opt.Shade == Shading::Weights && opt.SelectedBone < 0) { c.E = "--shading weights needs --bone <name> (model rig.list)"; return false; }
			opt.Outline = B(c.A, "outline", shade == "toon");   // 툰이면 기본으로 외곽선
			opt.OutlineWidth = F(c.A, "outlineWidth", (std::max)(1.0f, w / 400.0f));
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

		// 실루엣 비교: 기준 그림의 시점 · 크기 그대로 메시를 그려 덮이는 정도 (IoU) + 높이 띠마다 너비 차이
		bool Compare(Ctx& c)
		{
			RefImage* ref = c.A.contains("ref") ? c.D.FindRef(S(c.A, "ref")) : (c.D.Refs.size() == 1 ? &c.D.Refs[0] : nullptr);
			if (!ref) { c.E = c.D.Refs.empty() ? "no reference image (model ref.add --path front.png --view front --height 1.6)" : "several references: pass --ref <name>"; return false; }
			if (ref->Pixels.empty()) { c.E = "reference image not loaded: " + ref->Path; return false; }
			Vec3 R, U, Fw;
			ViewBasis(ref->View, R, U, Fw);
			const int maxSide = std::clamp(I(c.A, "size", 512), 64, 2048);
			const float scale = maxSide / (float)(std::max)(ref->W, ref->H);
			const int W = (std::max)(16, (int)roundf(ref->W * scale)), H = (std::max)(16, (int)roundf(ref->H * scale));
			ViewCamera cam;
			cam.SetPreset(ref->View);
			cam.Ortho = true;
			cam.Target = ref->Center;
			cam.Distance = ref->Height / (2.0f * tanf(cam.Fov * 0.5f * kDeg));
			Raster r;
			r.Resize(W, H);
			RasterOptions o;
			o.Background = o.Grid = o.Wireframe = o.Selection = o.Refs = false;
			r.Render(c.D, cam, o);
			// 기준 그림 마스크: 투명도가 있으면 알파, 없으면 네 모서리 평균 (배경) 과 다른 색
			bool alpha = false;
			for (uint32 p : ref->Pixels) if ((p >> 24) < 250) { alpha = true; break; }
			auto rgb = [](uint32 p) { return Vec3((p & 255) / 255.0f, ((p >> 8) & 255) / 255.0f, ((p >> 16) & 255) / 255.0f); };
			const Vec3 bg = (rgb(ref->Pixels[0]) + rgb(ref->Pixels[ref->W - 1]) + rgb(ref->Pixels[(size_t)(ref->H - 1) * ref->W]) + rgb(ref->Pixels[(size_t)ref->H * ref->W - 1])) * 0.25f;
			const float threshold = F(c.A, "threshold", 0.2f);
			std::vector<uint8_t> refMask((size_t)W * H), meshMask((size_t)W * H);
			int both = 0, meshOnly = 0, refOnly = 0;
			for (int y = 0; y < H; ++y)
				for (int x = 0; x < W; ++x)
				{
					const int sx = std::clamp((int)((x + 0.5f) / W * ref->W), 0, ref->W - 1), sy = std::clamp((int)((y + 0.5f) / H * ref->H), 0, ref->H - 1);
					const uint32 p = ref->Pixels[(size_t)sy * ref->W + sx];
					const bool inRef = alpha ? (p >> 24) > 127 : (rgb(p) - bg).Length() > threshold;
					const bool inMesh = r.FaceId[(size_t)y * W + x] >= 0;
					refMask[(size_t)y * W + x] = inRef;
					meshMask[(size_t)y * W + x] = inMesh;
					both += inRef && inMesh;
					meshOnly += inMesh && !inRef;
					refOnly += inRef && !inMesh;
				}
			const int uni = both + meshOnly + refOnly;
			const float px = ref->Height / H;   // 픽셀 하나 = 미터
			auto worldAt = [&](float x, float y) { return ref->Center + R * ((x / W - 0.5f) * ref->Width()) + U * ((0.5f - y / H) * ref->Height); };
			auto rd = [](float v) { return roundf(v * 1000.0f) / 1000.0f; };
			// 높이 띠: 띠마다 가로 범위 (왼쪽 · 오른쪽 끝) → 너비 · 가운데
			const int bands = std::clamp(I(c.A, "bands", 12), 1, 64);
			json bj = json::array(), hints = json::array();
			for (int b = 0; b < bands; ++b)
			{
				const int y0 = b * H / bands, y1 = (b + 1) * H / bands;
				int rl = W, rr = -1, ml = W, mr = -1;
				for (int y = y0; y < y1; ++y)
					for (int x = 0; x < W; ++x)
					{
						if (refMask[(size_t)y * W + x]) { rl = (std::min)(rl, x); rr = (std::max)(rr, x); }
						if (meshMask[(size_t)y * W + x]) { ml = (std::min)(ml, x); mr = (std::max)(mr, x); }
					}
				if (rr < 0 && mr < 0) continue;
				const Vec3 at = worldAt(W * 0.5f, (y0 + y1) * 0.5f);
				json e = { { "at", { rd(at.x), rd(at.y), rd(at.z) } } };
				const float refW = rr >= 0 ? (rr - rl + 1) * px : 0.0f, meshW = mr >= 0 ? (mr - ml + 1) * px : 0.0f;
				e["refWidth"] = rd(refW);
				e["meshWidth"] = rd(meshW);
				if (rr >= 0) { const Vec3 p = worldAt((rl + rr + 1) * 0.5f, (y0 + y1) * 0.5f); e["refCenter"] = { rd(p.x), rd(p.y), rd(p.z) }; }
				if (mr >= 0) { const Vec3 p = worldAt((ml + mr + 1) * 0.5f, (y0 + y1) * 0.5f); e["meshCenter"] = { rd(p.x), rd(p.y), rd(p.z) }; }
				bj.push_back(e);
				const float tol = ref->Height * 0.02f;
				char buf[200];
				if (rr < 0) { snprintf(buf, sizeof(buf), "near (%.2f, %.2f, %.2f): mesh is %.2f m wide but the reference is empty here", at.x, at.y, at.z, meshW); hints.push_back(buf); }
				else if (mr < 0) { snprintf(buf, sizeof(buf), "near (%.2f, %.2f, %.2f): reference is %.2f m wide but the mesh is missing", at.x, at.y, at.z, refW); hints.push_back(buf); }
				else if (fabsf(meshW - refW) > tol) { snprintf(buf, sizeof(buf), "near (%.2f, %.2f, %.2f): mesh %.2f m wide, reference %.2f m (%s %.2f m)", at.x, at.y, at.z, meshW, refW, meshW > refW ? "narrow by" : "widen by", fabsf(meshW - refW)); hints.push_back(buf); }
			}
			c.R = json::object();
			c.R["ref"] = ref->Name;
			c.R["view"] = ref->View;
			c.R["iou"] = uni ? rd(both / (float)uni) : 0.0f;
			c.R["extra"] = rd((both + refOnly) ? meshOnly / (float)(both + refOnly) : 0.0f);      // 기준 밖으로 나간 메시 (기준 넓이 대비)
			c.R["missing"] = rd((both + refOnly) ? refOnly / (float)(both + refOnly) : 0.0f);    // 메시가 못 덮은 기준
			c.R["bands"] = bj;
			c.R["hints"] = hints;
			c.R["metersPerPixel"] = px;
			const std::string out = S(c.A, "out");
			if (!out.empty())
			{
				// 차이 그림: 회색 = 겹침, 빨강 = 메시만, 파랑 = 기준만
				for (size_t i = 0; i < r.Color.size(); ++i)
					r.Color[i] = refMask[i] && meshMask[i] ? Rgba(200, 200, 200) : (meshMask[i] ? Rgba(235, 70, 60) : (refMask[i] ? Rgba(60, 120, 245) : Rgba(28, 28, 30)));
				if (!r.SavePng(out, c.E)) return false;
				c.R["diff"] = out;
			}
			return true;
		}

		bool Batch(Ctx& c);

		// ---- 리깅 연산 (ModelRig.cpp) 을 연산 표 모양으로
		bool RigCall(Ctx& c, bool (*fn)(Document&, const json&, json&, std::string&))
		{
			json report;
			if (!fn(c.D, c.A, report, c.E)) return false;
			c.R = c.D.Summary(false);
			for (auto it = report.begin(); it != report.end(); ++it) c.R[it.key()] = it.value();
			c.D.Changed();
			return true;
		}

		json BoneJson(const Armature& arm, int i)
		{
			const Bone& b = arm.Bones[i];
			auto r = [](const Vec3& v) { return json{ roundf(v.x * 1e4f) / 1e4f, roundf(v.y * 1e4f) / 1e4f, roundf(v.z * 1e4f) / 1e4f }; };
			json j = { { "name", b.Name }, { "parent", b.Parent >= 0 ? arm.Bones[b.Parent].Name : "" }, { "head", r(b.Head) }, { "tail", r(b.Tail) } };
			if (!b.Human.empty()) j["human"] = b.Human;
			if (!b.Deform) j["deform"] = false;
			if (b.Spring) j["spring"] = { { "chain", b.Chain }, { "stiffness", b.Stiffness }, { "drag", b.Drag }, { "gravity", b.Gravity }, { "radius", b.HitRadius } };
			return j;
		}

		// 리깅 확인: 가중치 없는 점, 본마다 점 수, 필수 사람 본
		bool RigCheck(Ctx& c)
		{
			const Armature& arm = c.D.Rig;
			if (arm.Empty()) { c.E = "no armature"; return false; }
			json objs = json::array(), problems = json::array();
			for (const Object& o : c.D.Objects)
			{
				if (!o.Visible || o.M.Verts.empty()) continue;
				const std::vector<int> g2b = GroupToBone(arm, o.M);
				std::vector<std::pair<int, float>> bw;
				int none = 0, maxInf = 0;
				for (const Vert& v : o.M.Verts)
				{
					VertexBones(arm, g2b, v, v.P, false, bw);
					if (bw.empty()) ++none;
					maxInf = (std::max)(maxInf, (int)bw.size());
				}
				objs.push_back({ { "object", o.Name }, { "verts", (int)o.M.Verts.size() }, { "unweighted", none }, { "maxInfluences", maxInf } });
				if (none) problems.push_back(o.Name + ": " + std::to_string(none) + " vertices have no bone weight (they would stick to the nearest bone on export) - model rig.weights");
			}
			static const char* kRequired[] = { "Hips", "Spine", "Head", "LeftUpperArm", "LeftLowerArm", "LeftHand", "RightUpperArm", "RightLowerArm", "RightHand", "LeftUpperLeg", "LeftLowerLeg", "LeftFoot", "RightUpperLeg", "RightLowerLeg", "RightFoot" };
			json missing = json::array();
			for (const char* h : kRequired) if (arm.FindHuman(h) < 0) missing.push_back(h);
			if (!missing.empty()) problems.push_back("humanoid bones missing (VRM / Humanoid retargeting need them): " + missing.dump());
			c.R = c.D.Summary(false);
			c.R["objects"] = objs;
			c.R["missingHumanBones"] = missing;
			c.R["problems"] = problems;
			c.R["ok"] = problems.empty();
			return true;
		}

		// Edit 모드 점 변환: fn(로컬 위치, 무게) → 새 위치. 비례 편집 (--proportional 반경 [--falloff]) · 거울 가운데 고정
		int TransformVerts(Ctx& c, Object& o, const std::function<Vec3(const Vec3&, float)>& fn)
		{
			const float s = (std::max)(1e-6f, (fabsf(o.Scale.x) + fabsf(o.Scale.y) + fabsf(o.Scale.z)) / 3.0f);
			const float radius = F(c.A, "proportional", 0.0f) / s;
			std::vector<float> w;
			if (radius > 0.0f) w = o.M.ProportionalWeights(radius, FalloffFromName(S(c.A, "falloff", "smooth")));
			int n = 0;
			const int shape = o.ShapeEdit >= 0 && o.ShapeEdit < (int)o.M.Shapes.size() ? o.ShapeEdit : -1;
			for (size_t i = 0; i < o.M.Verts.size(); ++i)
			{
				Vert& v = o.M.Verts[i];
				const float k = radius > 0.0f ? w[i] : (v.Sel ? 1.0f : 0.0f);
				if (k <= 0.0f) continue;
				const bool onPlane = o.MirrorX && o.MirrorClip && fabsf(v.P.x) < 1e-4f;
				if (shape >= 0)
				{
					// 셰이프 키 고치기: 보이는 위치 (기본 + 차이) 를 옮기고 차이만 저장, 기본 위치는 그대로
					Vec3 p = fn(v.P + o.M.ShapeDelta((int)i, shape), k) - v.P;
					if (onPlane) p.x = 0.0f;
					o.M.SetShapeDelta((int)i, shape, p);
				}
				else
				{
					v.P = fn(v.P, k);
					if (onPlane) v.P.x = 0.0f;   // 거울 가운데는 그대로 (Clipping)
				}
				++n;
			}
			o.M.Touch();
			return n;
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
			add("export", "--path <out.fbx|obj|glb|vrm> [--selected] [--title] [--author] [--outlineWidth 0.003]: FBX 7.4 binary / OBJ / GLB (meters, Y up). With an armature GLB is skinned; .vrm = VRM 1.0 (humanoid + spring bones + MToon) for the engine (create character --model x.vrm)", false, [](Ctx& c) {
				const std::string p = S(c.A, "path");
				if (!c.D.Export(p, B(c.A, "selected", false), c.E, c.A)) return false;
				c.R = c.D.Summary(false); c.R["path"] = p;
				std::string ext = U8String(PathU8(p).extension());
				for (char& ch : ext) ch = (char)tolower((unsigned char)ch);
				if (!c.D.Rig.Empty() && (ext == ".fbx" || ext == ".obj")) c.R["warning"] = "armature and weights are not written to " + ext + " (mesh only) - export .vrm or .glb for a rigged character";
				return true; });
			add("undo", "undo the last change", false, [](Ctx& c) { if (!c.D.Undo()) { c.E = "nothing to undo"; return false; } c.R = c.D.Summary(false); return true; });
			add("redo", "redo", false, [](Ctx& c) { if (!c.D.Redo()) { c.E = "nothing to redo"; return false; } c.R = c.D.Summary(false); return true; });
			add("render", "--dir <folder> [--views front,right,back,top,persp] [--size 768 | [w,h]] [--shading solid|toon|normals|uv|weights --bone <name>] [--bones true] [--outline] [--outlineWidth px] [--wire true|false] [--selection] [--grid] [--xray] [--zoom 1] [--target [x,y,z]]: PNG per view", false, Render);

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
					for (Vert& v : o.M.Verts)
					{
						v.P = Vec3::Transform(v.P, w);
						for (auto& x : v.K) x.second = Vec3::TransformNormal(x.second, w);   // 셰이프 차이도 (회전 · 크기)
					}
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
			add("translate", "--delta [x,y,z] | --to [x,y,z] (moves selection center there) [--proportional <radius m> --falloff smooth|sphere|root|sharp|linear|constant]", true, [](Ctx& c) {
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
				const Vec3 ld = DirToLocal(*o, d);
				return Done(c, TransformVerts(c, *o, [&](const Vec3& p, float k) { return p + ld * k; }), "moved"); });
			add("rotate", "--angle <deg> --axis x|y|z|[x,y,z] [--pivot [x,y,z]] (default: selection center) [--proportional <radius m> --falloff ..]", true, [](Ctx& c) {
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
				const float ang = F(c.A, "angle", 0.0f) * kDeg;
				return Done(c, TransformVerts(c, *o, [&](const Vec3& p, float k) { return Vec3::Transform(p - pivot, Quaternion::CreateFromAxisAngle(lan, ang * k)) + pivot; }), "rotated"); });
			add("scale", "--factor s|[x,y,z] [--pivot [x,y,z]] (default: selection center) [--proportional <radius m> --falloff ..]", true, [](Ctx& c) {
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
				return Done(c, TransformVerts(c, *o, [&](const Vec3& p, float k) { return pivot + (p - pivot) * (Vec3(1, 1, 1) + (s - Vec3(1, 1, 1)) * k); }), "scaled"); });
			add("set.positions", "--positions [[index, x, y, z], ..] (world coords)", true, [](Ctx& c) {
				Object* o = Active(c); if (!o) return false;
				std::vector<std::pair<int, Vec3>> pos;
				const Matrix l = ToLocal(*o);
				if (c.A.contains("positions"))
					for (const auto& p : c.A["positions"])
						if (p.is_array() && p.size() == 4) pos.push_back({ p[0].get<int>(), Vec3::Transform(Vec3(p[1].get<float>(), p[2].get<float>(), p[3].get<float>()), l) });
				if (o->ShapeEdit >= 0 && o->ShapeEdit < (int)o->M.Shapes.size())
				{
					int n = 0;
					for (const auto& [i, p] : pos) if (i >= 0 && i < (int)o->M.Verts.size()) { o->M.SetShapeDelta(i, o->ShapeEdit, p - o->M.Verts[i].P); ++n; }
					return Done(c, n, "moved");
				}
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
			add("material.set", "--index <n> [--name <name>] [--color r,g,b (0..1)] [--only true = just name/color, no faces]: material slot for selected faces (all if none)", true, [](Ctx& c) {
				Mesh* m = EditMesh(c); if (!m) return false;
				const int idx = (std::max)(0, I(c.A, "index", 0));
				if ((int)c.D.Materials.size() <= idx) c.D.Materials.resize(idx + 1);
				if (c.A.contains("name")) c.D.Materials[idx] = S(c.A, "name");
				Vec3 col;
				if (GetVec3(c.A, "color", col))
				{
					while ((int)c.D.MaterialColors.size() <= idx) c.D.MaterialColors.push_back(Vec3(0.8f, 0.8f, 0.8f));
					c.D.MaterialColors[idx] = Vec3(std::clamp(col.x, 0.0f, 1.0f), std::clamp(col.y, 0.0f, 1.0f), std::clamp(col.z, 0.0f, 1.0f));
				}
				if (B(c.A, "only", false)) return Done(c, 0, "faces");
				if (m->SelectedFaces().empty()) { for (Face& f : m->Faces) f.Material = idx; return Done(c, (int)m->Faces.size(), "faces"); }
				int n = 0;
				for (Face& f : m->Faces) if (f.Sel) { f.Material = idx; ++n; }
				return Done(c, n, "faces"); });
			// ================= 버텍스 그룹 (이름 있는 선택 집합 · 나중에 스킨 가중치)
			add("group.assign", "--name <group> [--weight 1]: put selected vertices in the group (created if new)", true, [](Ctx& c) {
				Mesh* m = EditMesh(c); if (!m) return false;
				const std::string n = S(c.A, "name");
				if (n.empty()) { c.E = "need --name"; return false; }
				if (m->SelectedVerts().empty()) { c.E = "select vertices first"; return false; }
				const int g = m->AddGroup(n);
				m->AssignGroup(g, std::clamp(F(c.A, "weight", 1.0f), 0.0f, 1.0f));
				Done(c, m->GroupCount(g), "group_verts");
				return true; });
			add("group.remove", "--name <group>: take selected vertices out of the group", true, [](Ctx& c) {
				Mesh* m = EditMesh(c); if (!m) return false;
				const int g = m->FindGroup(S(c.A, "name"));
				if (g < 0) { c.E = "no group '" + S(c.A, "name") + "'"; return false; }
				return Done(c, m->RemoveFromGroup(g), "removed"); });
			add("group.select", "--name <group> [--extend] [--deselect]: select (or deselect) the group's vertices", false, [](Ctx& c) {
				Mesh* m = EditMesh(c); if (!m) return false;
				const int g = m->FindGroup(S(c.A, "name"));
				if (g < 0) { c.E = "no group '" + S(c.A, "name") + "'"; return false; }
				const bool deselect = B(c.A, "deselect", false);
				if (!deselect && !B(c.A, "extend", false)) m->SelectAll(false);
				m->SelectGroup(g, !deselect);
				m->Flush(SelectMode::Vertex);
				if (c.D.Mode != SelectMode::Vertex) m->Flush(SelectMode::Vertex);
				c.R = c.D.Summary(false); c.D.Changed(); return true; });
			add("group.delete", "--name <group>", true, [](Ctx& c) {
				Mesh* m = EditMesh(c); if (!m) return false;
				const int g = m->FindGroup(S(c.A, "name"));
				if (g < 0) { c.E = "no group '" + S(c.A, "name") + "'"; return false; }
				m->DeleteGroup(g);
				return Done(c, 1, "deleted"); });
			add("group.list", "vertex groups of the active mesh: name, vertex count, bounds (world)", false, [](Ctx& c) {
				Object* o = Active(c); if (!o) return false;
				json list = json::array();
				const Matrix w = o->World();
				for (int g = 0; g < (int)o->M.Groups.size(); ++g)
				{
					Vec3 mn(FLT_MAX, FLT_MAX, FLT_MAX), mx(-FLT_MAX, -FLT_MAX, -FLT_MAX);
					int n = 0;
					for (int i = 0; i < (int)o->M.Verts.size(); ++i)
						if (o->M.Weight(i, g) > 0.0f) { const Vec3 p = Vec3::Transform(o->M.Verts[i].P, w); mn = Vec3::Min(mn, p); mx = Vec3::Max(mx, p); ++n; }
					json e = { { "name", o->M.Groups[g] }, { "verts", n } };
					if (n) { e["min"] = V(mn); e["max"] = V(mx); }
					list.push_back(e);
				}
				c.R = { { "object", o->Name }, { "groups", list } };
				return true; });

			// ================= 체크포인트 (해 보고 되돌리기)
			add("checkpoint", "--save <name> | --restore <name> | --delete <name> | --list: named document states (restore is undoable)", false, [](Ctx& c) {
				if (c.A.contains("save")) { c.D.SaveCheckpoint(S(c.A, "save")); }
				else if (c.A.contains("restore")) { if (!c.D.RestoreCheckpoint(S(c.A, "restore"))) { c.E = "no checkpoint '" + S(c.A, "restore") + "'"; return false; } }
				else if (c.A.contains("delete")) { if (!c.D.DeleteCheckpoint(S(c.A, "delete"))) { c.E = "no checkpoint '" + S(c.A, "delete") + "'"; return false; } }
				c.R = c.D.Summary(false);
				c.R["checkpoints"] = c.D.CheckpointNames();
				return true; });

			// ================= 기준 그림 · 비교
			add("ref.add", "--path <image> [--view front|back|left|right|top] [--height 1.7] [--center x,y,z (default: bottom on the floor)] [--opacity 0.5] [--name]: reference image behind that view", false, [](Ctx& c) {
				RefImage r;
				r.Path = S(c.A, "path");
				r.View = S(c.A, "view", "front");
				Vec3 R, U, Fw;
				if (!ViewBasis(r.View, R, U, Fw)) { c.E = "unknown view '" + r.View + "'"; return false; }
				r.Height = (std::max)(0.01f, F(c.A, "height", 1.7f));
				r.Center = r.View == "top" || r.View == "bottom" ? Vec3(0, 0, 0) : Vec3(0, r.Height * 0.5f, 0);
				GetVec3(c.A, "center", r.Center);
				r.Opacity = std::clamp(F(c.A, "opacity", 0.5f), 0.0f, 1.0f);
				r.Name = S(c.A, "name", r.View);
				if (!r.Load(c.E)) return false;
				if (RefImage* old = c.D.FindRef(r.Name)) *old = std::move(r);
				else c.D.Refs.push_back(std::move(r));
				++c.D.Revision;
				c.R = { { "refs", (int)c.D.Refs.size() } };
				RefImage* added = c.D.FindRef(S(c.A, "name", S(c.A, "view", "front")));
				c.R["ref"] = { { "name", added->Name }, { "size", { added->W, added->H } }, { "width", added->Width() }, { "height", added->Height }, { "center", V(added->Center) } };
				return true; });
			add("ref.set", "--name <ref> [--height] [--center x,y,z] [--opacity] [--visible true|false]", false, [](Ctx& c) {
				RefImage* r = c.D.FindRef(S(c.A, "name"));
				if (!r) { c.E = "no reference '" + S(c.A, "name") + "'"; return false; }
				r->Height = (std::max)(0.01f, F(c.A, "height", r->Height));
				GetVec3(c.A, "center", r->Center);
				r->Opacity = std::clamp(F(c.A, "opacity", r->Opacity), 0.0f, 1.0f);
				r->Visible = B(c.A, "visible", r->Visible);
				++c.D.Revision;
				c.R = { { "name", r->Name }, { "width", r->Width() }, { "height", r->Height }, { "center", V(r->Center) } };
				return true; });
			add("ref.remove", "--name <ref> | --all", false, [](Ctx& c) {
				if (B(c.A, "all", false)) c.D.Refs.clear();
				else
				{
					const std::string n = S(c.A, "name");
					const size_t before = c.D.Refs.size();
					c.D.Refs.erase(std::remove_if(c.D.Refs.begin(), c.D.Refs.end(), [&](const RefImage& r) { return r.Name == n; }), c.D.Refs.end());
					if (before == c.D.Refs.size()) { c.E = "no reference '" + n + "'"; return false; }
				}
				++c.D.Revision;
				c.R = { { "refs", (int)c.D.Refs.size() } };
				return true; });
			add("ref.list", "reference images", false, [](Ctx& c) {
				json list = json::array();
				for (const RefImage& r : c.D.Refs)
					list.push_back({ { "name", r.Name }, { "path", r.Path }, { "view", r.View }, { "center", V(r.Center) }, { "width", r.Width() }, { "height", r.Height }, { "opacity", r.Opacity }, { "visible", r.Visible }, { "loaded", !r.Pixels.empty() } });
				c.R = { { "refs", list } };
				return true; });
			add("compare", "[--ref <name>] [--bands 12] [--out diff.png] [--size 512] [--threshold 0.2]: silhouette of all visible meshes vs the reference (IoU, extra, missing, width per height band, hints)", false, Compare);

			// ================= 여러 연산을 한 번에
			add("batch", "--steps [{\"op\":\"add\",\"type\":\"cube\"}, ...] [--atomic true]: run ops in order as ONE undo step; stops at the first failure (atomic = roll everything back). CLI: nova model batch <file> (one op per line)", false, Batch);

			// ================= 모디파이어 (원래 메시는 그대로)
			add("modifier.mirror", "--enable true|false [--clip true]: live X mirror of the active object (model one half; center verts stay on X = 0)", true, [](Ctx& c) {
				Object* o = Active(c); if (!o) return false;
				o->MirrorX = B(c.A, "enable", true);
				o->MirrorClip = B(c.A, "clip", o->MirrorClip);
				return Done(c, 1, "objects"); });
			add("modifier.subsurf", "--levels 0..3: Subdivision Surface preview (render / export / compare use it)", true, [](Ctx& c) {
				Object* o = Active(c); if (!o) return false;
				o->Subsurf = std::clamp(I(c.A, "levels", 1), 0, 3);
				return Done(c, 1, "objects"); });
			add("modifier.apply", "bake the active object's modifiers into its mesh", true, [](Ctx& c) {
				Object* o = Active(c); if (!o) return false;
				if (!o->HasModifiers()) { c.E = "no modifiers on " + o->Name; return false; }
				o->ApplyModifiers();
				for (Vert& v : o->M.Verts) v.Sel = false;
				for (Face& f : o->M.Faces) f.Sel = false;
				o->M.SelEdges.clear();
				return Done(c, (int)o->M.Faces.size(), "faces"); });

			// ================= UV
			add("uv.smart", "[--angle 66] [--margin 0.02]: Smart UV Project of selected faces (all if none): charts by normal angle, packed into 0..1", true, [](Ctx& c) {
				Mesh* m = EditMesh(c); if (!m) return false;
				return Done(c, m->SmartUV(F(c.A, "angle", 66.0f), std::clamp(F(c.A, "margin", 0.02f), 0.0f, 0.2f)), "charts"); });
			add("uv.project", "--mode box|cylinder|sphere|planar: project UVs of selected faces (all if none), fitted to 0..1", true, [](Ctx& c) {
				Mesh* m = EditMesh(c); if (!m) return false;
				const std::string mode = S(c.A, "mode", "box");
				const int k = mode == "cylinder" ? 1 : (mode == "sphere" ? 2 : (mode == "planar" ? 3 : 0));
				return Done(c, m->ProjectUV(k), "faces"); });

			// ================= 머리카락
			auto strand = [](Ctx& c, int defaultSides) {
				std::vector<Vec3> pts;
				if (c.A.contains("points"))
					for (const auto& p : c.A["points"])
						if (p.is_array() && p.size() == 3) pts.push_back(Vec3(p[0].get<float>(), p[1].get<float>(), p[2].get<float>()));
				if (pts.size() < 2) { c.E = "need --points [[x,y,z], [x,y,z], ...] (root first, at least 2)"; return false; }
				Vec3 center(0, 1.2f, 0);
				GetVec3(c.A, "center", center);
				// Edit 모드면 활성 메시에, 아니면 새 오브젝트 (이름 기본 Hair)
				Object* o = c.D.EditMode ? c.D.ActiveObject() : nullptr;
				if (!o)
				{
					const int idx = c.D.AddObject(S(c.A, "name", "Hair"));
					o = &c.D.Objects[idx];
				}
				const Matrix l = ToLocal(*o);
				for (Vec3& p : pts) p = Vec3::Transform(p, l);
				center = Vec3::Transform(center, l);
				const int before = (int)o->M.Faces.size();
				const int n = o->M.AddStrand(pts, F(c.A, "width", 0.08f), F(c.A, "thickness", 0.03f), F(c.A, "tip", 0.0f), I(c.A, "sides", defaultSides), center, I(c.A, "segments", 0));
				if (c.A.contains("material")) for (int f = before; f < (int)o->M.Faces.size(); ++f) o->M.Faces[f].Material = (std::max)(0, I(c.A, "material", 0));
				Done(c, n, "faces_added");
				c.R["object"] = o->Name;
				return true;
			};
			add("hair.strand", "--points [[x,y,z],..] (root → tip) [--width 0.08] [--thickness 0.03] [--tip 0] [--sides 4] [--center x,y,z (head center, strands face away)] [--segments] [--material n] [--name Hair]: anime hair clump along a smooth curve", true,
				[strand](Ctx& c) { return strand(c, 4); });
			add("hair.card", "--points [[x,y,z],..] [--width 0.06] [--tip 0.2] [--center x,y,z] [--segments] [--material n]: flat hair card (front faces away from center), UV u across / v root→tip", true,
				[strand](Ctx& c) { return strand(c, 0); });
			add("material.list", "material slots: index, name, color, faces using it (active mesh)", false, [](Ctx& c) {
				json list = json::array();
				Object* o = c.D.ActiveObject();
				for (int i = 0; i < (int)(std::max)(c.D.Materials.size(), c.D.MaterialColors.size()); ++i)
				{
					int faces = 0;
					if (o) for (const Face& f : o->M.Faces) faces += f.Material == i;
					list.push_back({ { "index", i }, { "name", i < (int)c.D.Materials.size() ? c.D.Materials[i] : "" }, { "color", V(c.D.MaterialColor(i)) }, { "faces", faces } });
				}
				c.R = { { "materials", list } };
				return true; });

			// ================= 셰이프 키 (표정): VRM 표정 이름 (happy angry sad relaxed surprised aa ih ou ee oh blink blinkLeft blinkRight) 이면 그 표정으로 내보낸다
			auto shapeObj = [](Ctx& c) -> Object* {
				Object* o = c.A.contains("object") ? (c.D.Find(S(c.A, "object")) >= 0 ? &c.D.Objects[c.D.Find(S(c.A, "object"))] : nullptr) : c.D.ActiveObject();
				if (!o) c.E = "no such object (select one or pass --object)";
				return o;
			};
			auto shapeList = [](Object& o) {
				json list = json::array();
				for (int s = 0; s < (int)o.M.Shapes.size(); ++s)
					list.push_back({ { "name", o.M.Shapes[s] }, { "verts", o.M.ShapeVertCount(s) }, { "value", s < (int)o.ShapeValues.size() ? o.ShapeValues[s] : 0.0f }, { "editing", o.ShapeEdit == s } });
				return list;
			};
			add("shape.add", "--name <key> [--object]: new shape key (expression) on the object - then shape keys are edited with translate / rotate / scale (Edit mode) until shape.edit --basis", true, [shapeObj, shapeList](Ctx& c) {
				Object* o = shapeObj(c); if (!o) return false;
				const std::string n = S(c.A, "name");
				if (n.empty()) { c.E = "need --name (VRM expression names: happy angry sad relaxed surprised aa ih ou ee oh blink blinkLeft blinkRight)"; return false; }
				o->ShapeEdit = o->M.AddShape(n);
				Done(c, (int)o->M.Shapes.size(), "shapes");
				c.R["shapes"] = shapeList(*o);
				return true; });
			add("shape.edit", "--name <key> | --basis [--object]: which shape key translate / rotate / scale change (basis = the mesh itself)", true, [shapeObj, shapeList](Ctx& c) {
				Object* o = shapeObj(c); if (!o) return false;
				if (B(c.A, "basis", false)) o->ShapeEdit = -1;
				else
				{
					const int s = o->M.FindShape(S(c.A, "name"));
					if (s < 0) { c.E = "no shape key '" + S(c.A, "name") + "' (model shape.list)"; return false; }
					o->ShapeEdit = s;
				}
				Done(c, 1, "objects");
				c.R["shapes"] = shapeList(*o);
				return true; });
			add("shape.value", "--name <key> --value 0..1 [--object]: preview a shape key (render / window)", true, [shapeObj, shapeList](Ctx& c) {
				Object* o = shapeObj(c); if (!o) return false;
				const int s = o->M.FindShape(S(c.A, "name"));
				if (s < 0) { c.E = "no shape key '" + S(c.A, "name") + "'"; return false; }
				if ((int)o->ShapeValues.size() < (int)o->M.Shapes.size()) o->ShapeValues.resize(o->M.Shapes.size(), 0.0f);
				o->ShapeValues[s] = std::clamp(F(c.A, "value", 1.0f), -1.0f, 2.0f);
				Done(c, 1, "shapes");
				c.R["shapes"] = shapeList(*o);
				return true; });
			add("shape.list", "[--object]: shape keys (name, moved vertices, preview value, editing)", false, [shapeObj, shapeList](Ctx& c) {
				Object* o = shapeObj(c); if (!o) return false;
				c.R = { { "object", o->Name }, { "shapes", shapeList(*o) }, { "editing", o->ShapeEdit >= 0 ? o->M.Shapes[o->ShapeEdit] : "Basis" } };
				return true; });
			add("shape.delete", "--name <key> [--object]", true, [shapeObj, shapeList](Ctx& c) {
				Object* o = shapeObj(c); if (!o) return false;
				const int s = o->M.FindShape(S(c.A, "name"));
				if (s < 0) { c.E = "no shape key '" + S(c.A, "name") + "'"; return false; }
				o->M.DeleteShape(s);
				if (s < (int)o->ShapeValues.size()) o->ShapeValues.erase(o->ShapeValues.begin() + s);
				o->ShapeEdit = o->ShapeEdit == s ? -1 : (o->ShapeEdit > s ? o->ShapeEdit - 1 : o->ShapeEdit);
				Done(c, 1, "deleted");
				c.R["shapes"] = shapeList(*o);
				return true; });
			add("shape.mirror", "--name <key> --to <new key> [--object]: copy a shape key to the other side (X mirror, e.g. blinkLeft → blinkRight) - only vertices whose mirror twin exists", true, [shapeObj, shapeList](Ctx& c) {
				Object* o = shapeObj(c); if (!o) return false;
				Mesh& m = o->M;
				const int s = m.FindShape(S(c.A, "name"));
				if (s < 0) { c.E = "no shape key '" + S(c.A, "name") + "'"; return false; }
				const std::string to = S(c.A, "to");
				if (to.empty()) { c.E = "need --to <new key>"; return false; }
				const int t = m.AddShape(to);
				// 거울 짝: 위치 (x 반대) 를 격자로
				auto key = [](const Vec3& p) { return std::make_tuple((long long)llroundf(p.x * 10000.0f), (long long)llroundf(p.y * 10000.0f), (long long)llroundf(p.z * 10000.0f)); };
				std::map<std::tuple<long long, long long, long long>, int> at;
				for (int i = 0; i < (int)m.Verts.size(); ++i) at[key(m.Verts[i].P)] = i;
				int n = 0;
				for (int i = 0; i < (int)m.Verts.size(); ++i) m.SetShapeDelta(i, t, Vec3(0, 0, 0));
				for (int i = 0; i < (int)m.Verts.size(); ++i)
				{
					const Vec3 d = m.ShapeDelta(i, s);
					if (d.LengthSquared() < 1e-14f) continue;
					const Vec3 p = m.Verts[i].P;
					auto it = at.find(key(Vec3(-p.x, p.y, p.z)));
					if (it == at.end()) continue;
					m.SetShapeDelta(it->second, t, Vec3(-d.x, d.y, d.z));
					++n;
				}
				Done(c, n, "verts");
				c.R["shapes"] = shapeList(*o);
				return true; });

			// ================= 리깅 (4 단계): 본 이름 = 버텍스 그룹 이름 = 스킨 가중치
			add("rig.humanoid", "[--style auto|chibi|adult] [--points {\"LeftHand\":[x,y,z],..,\"Head.end\":[x,y,z]}] [--upperChest]: fit a Unity Humanoid skeleton to the mesh (parts found by object / vertex group names: Head, Body, ArmL, ArmR, LegL, LegR, Hand, Foot; else height proportions). Non-human bones are kept", true,
				[](Ctx& c) { return RigCall(c, FitHumanoid); });
			add("rig.weights", "[--objects A,B] [--method auto|nearest] [--smooth 4] [--keepSpring true]: automatic skin weights (bone distance per mesh island, then smoothed along edges) into vertex groups named like the bones", true,
				[](Ctx& c) { return RigCall(c, AutoWeights); });
			add("rig.chain", "--objects Hair,Bang | (selected objects) [--group <vertex group>] [--radial 8 (skirt: chains around)] [--bones auto] [--parent Head] [--name] [--stiffness 0.75] [--drag 0.4] [--gravity 0.05] [--radius 0.02] [--minVerts 6]: spring bone chain per island (hair strand) root to tip, with weights; VRM Spring Bone -> engine Dynamic Bone", true,
				[](Ctx& c) { return RigCall(c, AddChains); });
			add("rig.colliders", "[--scale 0.9] [--clear]: body colliders for spring bones (head sphere, torso / limb capsules sized from the weighted vertices)", true, [](Ctx& c) {
				if (B(c.A, "clear", false)) { c.D.Rig.Colliders.clear(); return Done(c, 0, "colliders"); }
				return RigCall(c, AutoColliders); });
			add("rig.list", "bones (name, parent, head, tail, human, spring) and colliders", false, [](Ctx& c) {
				json bones = json::array();
				for (int i = 0; i < (int)c.D.Rig.Bones.size(); ++i) bones.push_back(BoneJson(c.D.Rig, i));
				json cols = json::array();
				for (const Collider& col : c.D.Rig.Colliders)
					cols.push_back({ { "bone", col.Bone >= 0 && col.Bone < (int)c.D.Rig.Bones.size() ? c.D.Rig.Bones[col.Bone].Name : "" }, { "radius", col.Radius }, { "shape", col.Capsule ? "capsule" : "sphere" } });
				c.R = { { "bones", bones }, { "colliders", cols } };
				return true; });
			add("rig.bone", "--name <bone> [--head x,y,z] [--tail x,y,z] [--parent <bone>] [--rename] [--human <HumanBodyBones>] [--deform] [--spring true --stiffness --drag --gravity --radius] [--delete] | --add (new bone): edit one bone (world coords)", true, [](Ctx& c) {
				Armature& arm = c.D.Rig;
				const std::string name = S(c.A, "name");
				if (name.empty()) { c.E = "need --name"; return false; }
				int i = arm.Find(name);
				if (B(c.A, "add", false))
				{
					if (i >= 0) { c.E = "bone '" + name + "' already exists"; return false; }
					Bone b;
					b.Name = name;
					arm.Bones.push_back(b);
					i = (int)arm.Bones.size() - 1;
				}
				if (i < 0) { c.E = "no bone '" + name + "' (model rig.list)"; return false; }
				if (B(c.A, "delete", false)) { arm.Remove(i); return Done(c, 1, "deleted"); }
				Bone& b = arm.Bones[i];
				GetVec3(c.A, "head", b.Head);
				GetVec3(c.A, "tail", b.Tail);
				if (c.A.contains("parent"))
				{
					const std::string p = S(c.A, "parent");
					const int pi = p.empty() ? -1 : arm.Find(p);
					if (!p.empty() && pi < 0) { c.E = "no bone '" + p + "'"; return false; }
					for (int k = pi; k >= 0; k = arm.Bones[k].Parent) if (k == i) { c.E = "parent would make a loop"; return false; }
					b.Parent = pi;
				}
				if (c.A.contains("human")) b.Human = S(c.A, "human");
				b.Deform = B(c.A, "deform", b.Deform);
				b.Spring = B(c.A, "spring", b.Spring);
				b.Stiffness = F(c.A, "stiffness", b.Stiffness);
				b.Drag = F(c.A, "drag", b.Drag);
				b.Gravity = F(c.A, "gravity", b.Gravity);
				b.HitRadius = F(c.A, "radius", b.HitRadius);
				std::string finalName = b.Name;
				if (c.A.contains("rename"))
				{
					// 그룹 이름도 같이 (가중치가 따라간다)
					const std::string nn = S(c.A, "rename");
					if (nn.empty() || arm.Find(nn) >= 0) { c.E = "bad or taken name '" + nn + "'"; return false; }
					for (Object& o : c.D.Objects) { const int g = o.M.FindGroup(b.Name); if (g >= 0) o.M.Groups[g] = nn; }
					b.Name = nn;
					finalName = nn;
				}
				arm.Sort();
				Done(c, 1, "bones");
				c.R["bone"] = BoneJson(arm, arm.Find(finalName));
				return true; });
			add("rig.pose", "--bone <name> --rotation x,y,z (degrees, world axes, on top of the parent) | --reset: preview pose (render / window show the skinned mesh) - check weights by bending limbs", true, [](Ctx& c) {
				Armature& arm = c.D.Rig;
				if (B(c.A, "reset", false)) { arm.ClearPose(); return Done(c, (int)arm.Bones.size(), "bones"); }
				const int i = arm.Find(S(c.A, "bone"));
				if (i < 0) { c.E = "no bone '" + S(c.A, "bone") + "' (model rig.list)"; return false; }
				Vec3 r;
				if (!GetVec3(c.A, "rotation", r)) { c.E = "need --rotation x,y,z (degrees)"; return false; }
				arm.Bones[i].Pose = Quaternion::CreateFromYawPitchRoll(r.y * kDeg, r.x * kDeg, r.z * kDeg);
				arm.Bones[i].PoseEuler = r;
				return Done(c, 1, "bones"); });
			add("rig.check", "rig problems: vertices without weights, missing humanoid bones", false, RigCheck);
			add("rig.clear", "[--weights true]: delete the armature (and the bone vertex groups)", true, [](Ctx& c) {
				if (B(c.A, "weights", true))
					for (Object& o : c.D.Objects)
						for (int g = (int)o.M.Groups.size() - 1; g >= 0; --g)
							if (c.D.Rig.Find(o.M.Groups[g]) >= 0) o.M.DeleteGroup(g);
				const int n = (int)c.D.Rig.Bones.size();
				c.D.Rig = Armature();
				return Done(c, n, "bones"); });

			return ops;
		}
	}

	namespace
	{
		// 여러 연산 = Undo 한 번. atomic 이면 실패할 때 모두 되돌린다
		bool Batch(Ctx& c)
		{
			if (!c.A.contains("steps") || !c.A["steps"].is_array() || c.A["steps"].empty()) { c.E = "need --steps [{\"op\": ...}, ...]"; return false; }
			const bool atomic = B(c.A, "atomic", true);
			c.D.PushUndo("batch");
			json results = json::array();
			int i = 0;
			for (const json& step : c.A["steps"])
			{
				++i;
				const std::string op = step.is_object() ? step.value("op", std::string()) : std::string();
				json args = step.is_object() ? step : json::object();
				args.erase("op");
				json r;
				std::string err;
				if (op == "batch" || op == "undo" || op == "redo" || op == "new" || op == "open")
					err = "not allowed inside batch";
				else if (op.empty())
					err = "missing \"op\"";
				if (!err.empty() || !RunOpNoUndo(op, args, r, err))
				{
					c.E = "step " + std::to_string(i) + " (" + op + "): " + err + (atomic ? "  [all steps rolled back]" : "  [earlier steps kept]");
					if (atomic) c.D.CancelUndo();
					return false;
				}
				json brief = { { "op", op } };
				for (const char* k : { "changed", "files", "iou", "object", "path", "hints" })
					if (r.contains(k)) brief[k] = r[k];
				results.push_back(brief);
			}
			c.R = c.D.Summary(true);
			c.R["steps"] = results;
			c.D.Changed();
			return true;
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
