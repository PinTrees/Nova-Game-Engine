#include "pch.h"
#include "ModelEditorWindow.h"
#include "ModelOps.h"
#include "UndoSystem.h"
#include "ImGui/imgui_internal.h"
#include <commdlg.h>

using namespace Modeling;
using json = nlohmann::json;

ModelEditorWindow* ModelEditorWindow::s_Instance = nullptr;

namespace
{
	constexpr float kPi = 3.14159265358979f;
	const ImU32 kText = IM_COL32(220, 220, 220, 255);
	const ImU32 kTextDim = IM_COL32(150, 150, 150, 255);
	const ImU32 kAccent = IM_COL32(255, 160, 60, 255);

	ImVec2 Sub(ImVec2 a, ImVec2 b) { return ImVec2(a.x - b.x, a.y - b.y); }
	float Len(ImVec2 a) { return sqrtf(a.x * a.x + a.y * a.y); }

	float DistToSegment(ImVec2 p, ImVec2 a, ImVec2 b)
	{
		const ImVec2 ab = Sub(b, a);
		const float l2 = ab.x * ab.x + ab.y * ab.y;
		float t = l2 > 1e-6f ? ((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / l2 : 0.0f;
		t = std::clamp(t, 0.0f, 1.0f);
		return Len(ImVec2(a.x + ab.x * t - p.x, a.y + ab.y * t - p.y));
	}

	std::string Utf8(const std::wstring& w)
	{
		if (w.empty()) return std::string();
		const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
		std::string s(n, '\0');
		WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
		return s;
	}

	// 파일 고르기 (filter = L"Models\0*.fbx;*.obj\0\0" 처럼)
	std::string FileDialog(bool save, const wchar_t* filter, const wchar_t* defExt)
	{
		wchar_t buf[1024] = {};
		OPENFILENAMEW ofn = {};
		ofn.lStructSize = sizeof(ofn);
		ofn.hwndOwner = Application::GetI()->GetMainHwnd();
		ofn.lpstrFile = buf;
		ofn.nMaxFile = 1024;
		ofn.lpstrFilter = filter;
		ofn.lpstrDefExt = defExt;
		ofn.Flags = OFN_NOCHANGEDIR | (save ? OFN_OVERWRITEPROMPT : (OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST));
		const BOOL ok = save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
		return ok ? Utf8(buf) : std::string();
	}

	// 쿼터니언 → 오일러 (도, x = pitch, y = yaw, z = roll) — CreateFromYawPitchRoll 의 역
	Vec3 ToEulerDeg(const Quaternion& q)
	{
		const Matrix m = Matrix::CreateFromQuaternion(q);
		const float cy = sqrtf(m._33 * m._33 + m._31 * m._31);
		const float x = atan2f(-m._32, cy);
		float y, z;
		if (cy > 16.0f * FLT_EPSILON) { y = atan2f(m._31, m._33); z = atan2f(m._12, m._22); }
		else { y = 0.0f; z = atan2f(-m._21, m._11); }
		Vec3 e = Vec3(x, y, z) * (180.0f / kPi);
		if (fabsf(e.x) < 5e-4f) e.x = 0.0f;
		if (fabsf(e.y) < 5e-4f) e.y = 0.0f;
		if (fabsf(e.z) < 5e-4f) e.z = 0.0f;
		return e;
	}

	bool PlaneHit(const Vec3& o, const Vec3& d, const Vec3& p, const Vec3& n, Vec3& hit)
	{
		const float den = d.Dot(n);
		if (fabsf(den) < 1e-8f) return false;
		const float t = (p - o).Dot(n) / den;
		hit = o + d * t;
		return true;
	}

	// 광선과 선 (p + s·a) 이 가장 가까운 s
	float ClosestOnLine(const Vec3& ro, const Vec3& rd, const Vec3& p, const Vec3& a)
	{
		const Vec3 w = p - ro;
		const float b = a.Dot(rd), d = a.Dot(w), e = rd.Dot(w);
		const float den = 1.0f - b * b;
		if (fabsf(den) < 1e-6f) return 0.0f;
		return (b * e - d) / den;
	}

	const char* ModalName(int m)
	{
		static const char* names[] = { "", "Move", "Rotate", "Scale", "Inset", "Bevel", "Loop Cut", "Box Select" };
		return names[m];
	}
}

ModelEditorWindow::ModelEditorWindow()
	: EditorWindow("Model Editor", ICON_FA_CUBES)
{
	s_Instance = this;
	SetIsOpened(false);   // Window > Model Editor 로 연다
}

ModelEditorWindow::~ModelEditorWindow()
{
	if (s_Instance == this)
		s_Instance = nullptr;
}

void ModelEditorWindow::Focus()
{
	if (s_Instance == nullptr)
		return;
	s_Instance->SetIsOpened(true);
	ImGui::SetWindowFocus(s_Instance->GetImGuiName().c_str());
}

void ModelEditorWindow::Update()
{
}

void ModelEditorWindow::BeforeBegin()
{
	ImGui::SetNextWindowSize(ImVec2(1200.0f, 760.0f), ImGuiCond_FirstUseEver);
	if (EditorWindow* scene = EditorGUIManager::GetI()->FindWindow("Scene"))
		if (ImGuiWindow* w = ImGui::FindWindowByName(scene->GetImGuiName().c_str()); w && w->DockId)
			ImGui::SetNextWindowDockID(w->DockId, ImGuiCond_FirstUseEver);
}

// ------------------------------------------------------------------ 도움
bool ModelEditorWindow::Run(const std::string& op, const json& args)
{
	json r;
	std::string err;
	if (!RunOp(op, args, r, err))
	{
		SetStatus("! " + err);
		return false;
	}
	std::string s = op;
	if (r.contains("selection")) s += "  |  selected " + std::to_string(r["selection"].value("verts", 0)) + " verts, " + std::to_string(r["selection"].value("faces", 0)) + " faces";
	SetStatus(s);
	m_Dirty = true;
	return true;
}

void ModelEditorWindow::SetStatus(const std::string& s)
{
	m_Status = s;
	m_StatusTime = ImGui::GetTime();
}

Vec3 ModelEditorWindow::SelectionPivot() const
{
	const Document& d = Doc();
	if (d.EditMode)
	{
		const Object* o = d.ActiveObject();
		return o ? Vec3::Transform(o->M.SelectionCenter(), o->World()) : Vec3(0, 0, 0);
	}
	Vec3 c(0, 0, 0);
	int n = 0;
	for (const Object& o : d.Objects) if (o.Selected) { c += o.Position; ++n; }
	return n ? c / (float)n : c;
}

float ModelEditorWindow::PixelToWorld(const Vec3& at) const
{
	const float fov = m_Cam.Fov * kPi / 180.0f;
	const float depth = m_Cam.Ortho ? m_Cam.Distance : (std::max)(0.01f, (at - m_Cam.Eye()).Dot(m_Cam.Forward()));
	return 2.0f * depth * tanf(fov * 0.5f) / (std::max)(1.0f, m_ViewSize.y);
}

void ModelEditorWindow::FrameAll()
{
	Vec3 mn(FLT_MAX, FLT_MAX, FLT_MAX), mx(-FLT_MAX, -FLT_MAX, -FLT_MAX);
	for (const Object& o : Doc().Objects)
		if (o.Visible)
			for (const Vert& v : o.M.Verts) { const Vec3 p = Vec3::Transform(v.P, o.World()); mn = Vec3::Min(mn, p); mx = Vec3::Max(mx, p); }
	if (mn.x > mx.x) { mn = Vec3(-1, 0, -1); mx = Vec3(1, 2, 1); }
	m_Cam.Frame(mn, mx, m_ViewSize.y > 1 ? m_ViewSize.x / m_ViewSize.y : 1.5f);
	m_Dirty = true;
}

void ModelEditorWindow::FrameSelected()
{
	Document& d = Doc();
	Vec3 mn(FLT_MAX, FLT_MAX, FLT_MAX), mx(-FLT_MAX, -FLT_MAX, -FLT_MAX);
	for (int i = 0; i < (int)d.Objects.size(); ++i)
	{
		const Object& o = d.Objects[i];
		const bool editing = d.EditMode && i == d.Active;
		if (!editing && (d.EditMode || !o.Selected)) continue;
		for (const Vert& v : o.M.Verts)
			if (!editing || v.Sel) { const Vec3 p = Vec3::Transform(v.P, o.World()); mn = Vec3::Min(mn, p); mx = Vec3::Max(mx, p); }
	}
	if (mn.x > mx.x) { FrameAll(); return; }
	m_Cam.Frame(mn, mx, m_ViewSize.y > 1 ? m_ViewSize.x / m_ViewSize.y : 1.5f);
	m_Dirty = true;
}

void ModelEditorWindow::OpenFile(bool import)
{
	const std::string path = import
		? FileDialog(false, L"3D models (*.fbx;*.obj;*.glb;*.gltf;*.dae;*.3ds;*.blend)\0*.fbx;*.obj;*.glb;*.gltf;*.dae;*.3ds;*.blend\0All files\0*.*\0\0", nullptr)
		: FileDialog(false, L"NOVA model (*.nmodel)\0*.nmodel\0\0", L"nmodel");
	if (path.empty())
		return;
	if (Run(import ? "import" : "open", { { "path", path } }))
		FrameAll();
}

void ModelEditorWindow::SaveFile(bool saveAs)
{
	std::string path = Doc().Path;
	if (saveAs || path.empty())
		path = FileDialog(true, L"NOVA model (*.nmodel)\0*.nmodel\0\0", L"nmodel");
	if (!path.empty())
		Run("save", { { "path", path } });
}

void ModelEditorWindow::ExportFile(const wchar_t* ext)
{
	std::wstring filter;
	if (wcscmp(ext, L"fbx") == 0) filter = std::wstring(L"FBX (*.fbx)\0*.fbx\0\0", 21);
	else if (wcscmp(ext, L"obj") == 0) filter = std::wstring(L"OBJ (*.obj)\0*.obj\0\0", 21);
	else filter = std::wstring(L"glTF binary (*.glb)\0*.glb\0\0", 29);
	const std::string path = FileDialog(true, filter.c_str(), ext);
	if (!path.empty())
		Run("export", { { "path", path } });
}

// ------------------------------------------------------------------ 고르기
int ModelEditorWindow::PickVertex(ImVec2 mouse, float radius) const
{
	const Document& d = Doc();
	const Object* o = d.ActiveObject();
	if (!o) return -1;
	const Matrix w = o->World();
	int best = -1;
	float bestD = radius;
	for (int i = 0; i < (int)o->M.Verts.size(); ++i)
	{
		Vec3 s;
		if (!m_Raster.Project(Vec3::Transform(o->M.Verts[i].P, w), s)) continue;
		const float dd = Len(ImVec2(s.x - mouse.x, s.y - mouse.y));
		if (dd < bestD && (m_Opt.XRay || m_Raster.Visible(s)))
		{
			bestD = dd;
			best = i;
		}
	}
	return best;
}

int ModelEditorWindow::PickEdge(ImVec2 mouse, float radius)
{
	Document& d = Doc();
	Object* o = d.ActiveObject();
	if (!o) return -1;
	const Matrix w = o->World();
	const auto& edges = o->M.Edges();
	int best = -1;
	float bestD = radius;
	for (int i = 0; i < (int)edges.size(); ++i)
	{
		Vec3 a, b;
		if (!m_Raster.Project(Vec3::Transform(o->M.Verts[edges[i].A].P, w), a) || !m_Raster.Project(Vec3::Transform(o->M.Verts[edges[i].B].P, w), b)) continue;
		const float dd = DistToSegment(mouse, ImVec2(a.x, a.y), ImVec2(b.x, b.y));
		if (dd >= bestD) continue;
		if (!m_Opt.XRay)
		{
			// 마우스에 가장 가까운 선 위의 점이 보이나
			const ImVec2 ab = Sub(ImVec2(b.x, b.y), ImVec2(a.x, a.y));
			const float l2 = (std::max)(1e-6f, ab.x * ab.x + ab.y * ab.y);
			const float t = std::clamp(((mouse.x - a.x) * ab.x + (mouse.y - a.y) * ab.y) / l2, 0.0f, 1.0f);
			const Vec3 p = Vec3::Transform(o->M.Verts[edges[i].A].P + (o->M.Verts[edges[i].B].P - o->M.Verts[edges[i].A].P) * t, w);
			Vec3 s;
			if (!m_Raster.Project(p, s) || !m_Raster.Visible(s)) continue;
		}
		bestD = dd;
		best = i;
	}
	return best;
}

int ModelEditorWindow::PickFace(ImVec2 mouse) const
{
	const int id = m_Raster.FaceAt((int)mouse.x, (int)mouse.y);
	if (id < 0 || (id >> 20) != Doc().Active) return -1;
	return id & 0xFFFFF;
}

int ModelEditorWindow::PickObject(ImVec2 mouse) const
{
	const int id = m_Raster.FaceAt((int)mouse.x, (int)mouse.y);
	return id < 0 ? -1 : (id >> 20);
}

void ModelEditorWindow::ClickSelect(bool extend)
{
	Document& d = Doc();
	const ImVec2 m = ToView(ImGui::GetIO().MousePos);
	if (!d.EditMode)
	{
		const int hit = PickObject(m);
		if (!extend) for (Object& o : d.Objects) o.Selected = false;
		if (hit >= 0)
		{
			Object& o = d.Objects[hit];
			o.Selected = extend ? !(o.Selected && d.Active == hit) : true;
			d.Active = hit;
		}
		d.Changed();
		m_Dirty = true;
		return;
	}
	Object* o = d.ActiveObject();
	if (!o) return;
	Modeling::Mesh& mesh = o->M;
	const bool alt = ImGui::GetIO().KeyAlt;
	if (alt)
	{
		// Alt+클릭: 변 고리 (Blender)
		const int e = PickEdge(m, 14.0f);
		if (e < 0) return;
		if (!extend) mesh.SelectAll(false);
		mesh.SelectEdgeLoop(e);
		if (d.Mode == SelectMode::Face) mesh.Flush(SelectMode::Edge), mesh.Flush(SelectMode::Face);
		d.Changed();
		m_Dirty = true;
		return;
	}
	if (d.Mode == SelectMode::Vertex)
	{
		const int v = PickVertex(m, 14.0f);
		if (!extend) mesh.SelectAll(false);
		if (v >= 0) mesh.Verts[v].Sel = extend ? !mesh.Verts[v].Sel : true;
		mesh.Flush(SelectMode::Vertex);
	}
	else if (d.Mode == SelectMode::Edge)
	{
		const int e = PickEdge(m, 10.0f);
		if (!extend) mesh.SelectAll(false);
		if (e >= 0)
		{
			const Edge ed = mesh.Edges()[e];
			const uint64 k = EdgeKey(ed.A, ed.B);
			if (extend && mesh.SelEdges.count(k)) mesh.SelEdges.erase(k);
			else mesh.SelEdges.insert(k);
		}
		mesh.Flush(SelectMode::Edge);
	}
	else
	{
		const int f = PickFace(m);
		if (!extend) mesh.SelectAll(false);
		if (f >= 0) mesh.Faces[f].Sel = extend ? !mesh.Faces[f].Sel : true;
		mesh.Flush(SelectMode::Face);
	}
	d.Changed();
	m_Dirty = true;
}

void ModelEditorWindow::BoxSelect(ImVec2 a, ImVec2 b, bool extend, bool subtract)
{
	Document& d = Doc();
	const ImVec2 mn((std::min)(a.x, b.x), (std::min)(a.y, b.y)), mx((std::max)(a.x, b.x), (std::max)(a.y, b.y));
	auto inside = [&](const Vec3& s) { return s.x >= mn.x && s.x <= mx.x && s.y >= mn.y && s.y <= mx.y; };
	if (!d.EditMode)
	{
		if (!extend && !subtract) for (Object& o : d.Objects) o.Selected = false;
		for (int i = 0; i < (int)d.Objects.size(); ++i)
		{
			Object& o = d.Objects[i];
			Vec3 c(0, 0, 0);
			for (const Vert& v : o.M.Verts) c += v.P;
			if (!o.M.Verts.empty()) c /= (float)o.M.Verts.size();
			Vec3 s;
			if (o.Visible && m_Raster.Project(Vec3::Transform(c, o.World()), s) && inside(s))
			{
				o.Selected = !subtract;
				if (!subtract) d.Active = i;
			}
		}
		d.Changed();
		m_Dirty = true;
		return;
	}
	Object* o = d.ActiveObject();
	if (!o) return;
	Modeling::Mesh& mesh = o->M;
	const Matrix w = o->World();
	if (!extend && !subtract) mesh.SelectAll(false);
	std::vector<Vec3> screen(mesh.Verts.size());
	std::vector<uint8_t> in(mesh.Verts.size(), 0);
	for (int i = 0; i < (int)mesh.Verts.size(); ++i)
		in[i] = m_Raster.Project(Vec3::Transform(mesh.Verts[i].P, w), screen[i]) && inside(screen[i]) && (m_Opt.XRay || m_Raster.Visible(screen[i])) ? 1 : 0;
	if (d.Mode == SelectMode::Vertex)
	{
		for (int i = 0; i < (int)mesh.Verts.size(); ++i) if (in[i]) mesh.Verts[i].Sel = !subtract;
		mesh.Flush(SelectMode::Vertex);
	}
	else if (d.Mode == SelectMode::Edge)
	{
		for (const Edge& e : mesh.Edges())
			if (in[e.A] && in[e.B]) { if (subtract) mesh.SelEdges.erase(EdgeKey(e.A, e.B)); else mesh.SelEdges.insert(EdgeKey(e.A, e.B)); }
		mesh.Flush(SelectMode::Edge);
	}
	else
	{
		for (int f = 0; f < (int)mesh.Faces.size(); ++f)
		{
			Vec3 s;
			if (m_Raster.Project(Vec3::Transform(mesh.FaceCenter(f), w), s) && inside(s) && (m_Opt.XRay || m_Raster.Visible(s)))
				mesh.Faces[f].Sel = !subtract;
		}
		mesh.Flush(SelectMode::Face);
	}
	d.Changed();
	m_Dirty = true;
}

void ModelEditorWindow::UpdateHover()
{
	Document& d = Doc();
	int obj = -1, elem = -1;
	if (d.EditMode && m_Modal == Modal::None && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
	{
		const ImVec2 m = ToView(ImGui::GetIO().MousePos);
		if (m.x >= 0 && m.y >= 0 && m.x < m_ViewSize.x && m.y < m_ViewSize.y)
		{
			obj = d.Active;
			elem = d.Mode == SelectMode::Vertex ? PickVertex(m, 14.0f) : (d.Mode == SelectMode::Edge ? PickEdge(m, 10.0f) : PickFace(m));
			if (elem < 0) obj = -1;
		}
	}
	if (obj != m_Opt.HoverObject || elem != m_Opt.HoverElement)
	{
		m_Opt.HoverObject = obj;
		m_Opt.HoverElement = elem;
		m_Dirty = true;
	}
}

// ------------------------------------------------------------------ 끌기 연산
void ModelEditorWindow::BeginModal(Modal m, const std::string& op)
{
	Document& d = Doc();
	if (m == Modal::Box || m == Modal::LoopCut)
	{
		m_Modal = m;
		return;
	}
	Object* a = d.ActiveObject();
	if (d.EditMode)
	{
		if (!a || a->M.SelectedVerts().empty()) { SetStatus("nothing selected"); return; }
	}
	else
	{
		bool any = false;
		for (const Object& o : d.Objects) any = any || o.Selected;
		if (!any) { SetStatus("no object selected"); return; }
	}
	// op 가 비어 있으면 이 끌기가 Undo 스냅숏을 남긴다 (돌출 · 복제 뒤 이동은 그 연산이 이미 남겼다)
	m_ModalOp = op;
	if (op.empty())
		d.PushUndo(ModalName((int)m));
	m_Modal = m;
	m_Axis = -1;
	m_Numeric.clear();
	m_StartMouse = ToView(ImGui::GetIO().MousePos);
	m_Pivot = SelectionPivot();
	m_StartPositions.clear();
	m_StartRotations.clear();
	m_StartScales.clear();
	if (d.EditMode)
	{
		for (const Vert& v : a->M.Verts) m_StartPositions.push_back(v.P);
		ComputeWeights();
	}
	else
		for (const Object& o : d.Objects) { m_StartPositions.push_back(o.Position); m_StartRotations.push_back(o.Rotation); m_StartScales.push_back(o.Scale); }
}

// 비례 편집 무게 (끔 / 돌출 · 복제 뒤 이동 = 고른 점만 1)
void ModelEditorWindow::ComputeWeights()
{
	m_Weights.clear();
	Object* a = Doc().ActiveObject();
	if (!a || !m_Prop || m_ModalOp == "extrude" || m_ModalOp == "duplicate") return;
	const float s = (std::max)(1e-6f, (fabsf(a->Scale.x) + fabsf(a->Scale.y) + fabsf(a->Scale.z)) / 3.0f);
	m_Weights = a->M.ProportionalWeights(m_PropRadius / s, (Falloff)m_PropFalloff);
}

float ModelEditorWindow::WeightOf(size_t i) const
{
	if (i < m_Weights.size()) return m_Weights[i];
	const Object* a = Doc().ActiveObject();
	return a && i < a->M.Verts.size() && a->M.Verts[i].Sel ? 1.0f : 0.0f;
}

void ModelEditorWindow::ApplyModal()
{
	Document& d = Doc();
	const ImVec2 mouse = ToView(ImGui::GetIO().MousePos);
	Vec3 pivotS;
	m_Raster.Project(m_Pivot, pivotS);
	const ImVec2 ps(pivotS.x, pivotS.y);
	float numeric = 0.0f;
	const bool hasNumeric = !m_Numeric.empty() && m_Numeric != "-" && sscanf_s(m_Numeric.c_str(), "%f", &numeric) == 1;
	Vec3 axisDir = m_CustomAxis ? m_AxisDir : Vec3(m_Axis == 0 ? 1.0f : 0.0f, m_Axis == 1 ? 1.0f : 0.0f, m_Axis == 2 ? 1.0f : 0.0f);
	const bool constrained = m_CustomAxis || m_Axis >= 0;
	m_Opt.ExtraLines.clear();
	if (constrained)
		m_Opt.ExtraLines.push_back({ m_Pivot - axisDir * 1000.0f, m_Pivot + axisDir * 1000.0f });

	if (m_Modal == Modal::Inset || m_Modal == Modal::Bevel)
	{
		const float px = Len(Sub(mouse, ps)) - Len(Sub(m_StartMouse, ps));
		float v = (m_Modal == Modal::Inset ? -px : px) * PixelToWorld(m_Pivot);
		v = (std::max)(0.0f, v);
		if (hasNumeric) v = numeric;
		d.RestoreLastSnapshot();
		json r;
		std::string err;
		m_ModalArgs = m_Modal == Modal::Inset ? json{ { "thickness", v } } : json{ { "offset", v } };
		RunOpNoUndo(m_Modal == Modal::Inset ? "inset" : "bevel", m_ModalArgs, r, err);
		m_Dirty = true;
		return;
	}

	Object* a = d.ActiveObject();
	if (m_Modal == Modal::Grab)
	{
		Vec3 o0, d0, o1, d1, delta(0, 0, 0);
		m_Raster.Ray(m_StartMouse.x, m_StartMouse.y, o0, d0);
		m_Raster.Ray(mouse.x, mouse.y, o1, d1);
		if (constrained)
		{
			const float s0 = ClosestOnLine(o0, d0, m_Pivot, axisDir), s1 = ClosestOnLine(o1, d1, m_Pivot, axisDir);
			delta = axisDir * (hasNumeric ? numeric : s1 - s0);
		}
		else
		{
			Vec3 h0, h1;
			if (PlaneHit(o0, d0, m_Pivot, m_Cam.Forward(), h0) && PlaneHit(o1, d1, m_Pivot, m_Cam.Forward(), h1)) delta = h1 - h0;
			if (hasNumeric) delta = Vec3(numeric, 0, 0);
		}
		if (ImGui::GetIO().KeyCtrl)
		{
			// Ctrl = 0.1 단위로 맞춤 (Blender 의 스냅 증분)
			delta = Vec3(roundf(delta.x * 10.0f) / 10.0f, roundf(delta.y * 10.0f) / 10.0f, roundf(delta.z * 10.0f) / 10.0f);
		}
		if (d.EditMode && a)
		{
			const Vec3 local = Vec3::TransformNormal(delta, a->World().Invert());
			for (size_t i = 0; i < a->M.Verts.size() && i < m_StartPositions.size(); ++i)
				a->M.Verts[i].P = m_StartPositions[i] + local * WeightOf(i);
		}
		else
			for (size_t i = 0; i < d.Objects.size() && i < m_StartPositions.size(); ++i)
				d.Objects[i].Position = d.Objects[i].Selected ? m_StartPositions[i] + delta : m_StartPositions[i];
		if (m_ModalOp == "extrude")
			m_ModalArgs = m_CustomAxis ? json{ { "distance", delta.Dot(m_AxisDir) } } : json{ { "direction", { delta.x, delta.y, delta.z } } };
		else if (m_ModalOp == "duplicate" || m_ModalOp == "object.duplicate")
			m_ModalArgs = { { "offset", { delta.x, delta.y, delta.z } } };
		else
			m_ModalArgs = { { "delta", { delta.x, delta.y, delta.z } } };
	}
	else if (m_Modal == Modal::Rotate)
	{
		const float a0 = atan2f(m_StartMouse.y - ps.y, m_StartMouse.x - ps.x), a1 = atan2f(mouse.y - ps.y, mouse.x - ps.x);
		Vec3 axis = constrained ? axisDir : m_Cam.Forward();
		// 화면에서 시계 방향으로 돌린 만큼 (보는 쪽에서 본 회전과 같게)
		const float facing = axis.Dot(m_Cam.Forward()) >= 0.0f ? 1.0f : -1.0f;
		float angle = -(a1 - a0) * facing;
		if (hasNumeric) angle = numeric * kPi / 180.0f;
		if (ImGui::GetIO().KeyCtrl) angle = roundf(angle / (5.0f * kPi / 180.0f)) * (5.0f * kPi / 180.0f);
		const Quaternion q = Quaternion::CreateFromAxisAngle(axis, angle);
		if (d.EditMode && a)
		{
			const Matrix inv = a->World().Invert();
			Vec3 la = Vec3::TransformNormal(axis, inv);
			la.Normalize();
			const Vec3 lp = Vec3::Transform(m_Pivot, inv);
			for (size_t i = 0; i < a->M.Verts.size() && i < m_StartPositions.size(); ++i)
			{
				const float k = WeightOf(i);
				a->M.Verts[i].P = k > 0.0f ? Vec3::Transform(m_StartPositions[i] - lp, Quaternion::CreateFromAxisAngle(la, angle * k)) + lp : m_StartPositions[i];
			}
		}
		else
			for (size_t i = 0; i < d.Objects.size() && i < m_StartRotations.size(); ++i)
				d.Objects[i].Rotation = d.Objects[i].Selected ? m_StartRotations[i] * q : m_StartRotations[i];
		m_ModalArgs = { { "angle", angle * 180.0f / kPi }, { "axis", { axis.x, axis.y, axis.z } } };
	}
	else if (m_Modal == Modal::Scale)
	{
		float f = Len(Sub(mouse, ps)) / (std::max)(1.0f, Len(Sub(m_StartMouse, ps)));
		if (hasNumeric) f = numeric;
		if (ImGui::GetIO().KeyCtrl) f = roundf(f * 10.0f) / 10.0f;
		const Vec3 s = constrained && !m_CustomAxis ? Vec3(m_Axis == 0 ? f : 1.0f, m_Axis == 1 ? f : 1.0f, m_Axis == 2 ? f : 1.0f) : Vec3(f, f, f);
		if (d.EditMode && a)
		{
			const Vec3 lp = Vec3::Transform(m_Pivot, a->World().Invert());
			for (size_t i = 0; i < a->M.Verts.size() && i < m_StartPositions.size(); ++i)
				a->M.Verts[i].P = lp + (m_StartPositions[i] - lp) * (Vec3(1, 1, 1) + (s - Vec3(1, 1, 1)) * WeightOf(i));
		}
		else
			for (size_t i = 0; i < d.Objects.size() && i < m_StartScales.size(); ++i)
				d.Objects[i].Scale = d.Objects[i].Selected ? m_StartScales[i] * s : m_StartScales[i];
		m_ModalArgs = { { "factor", { s.x, s.y, s.z } } };
	}
	if (a && d.EditMode && a->MirrorX && a->MirrorClip)
		for (size_t i = 0; i < a->M.Verts.size() && i < m_StartPositions.size(); ++i)
			if (fabsf(m_StartPositions[i].x) < 1e-4f) a->M.Verts[i].P.x = 0.0f;   // 거울 가운데는 X = 0 에 (Clipping)
	if (d.EditMode && m_Prop && !m_Weights.empty() && m_ModalArgs.is_object())
	{
		// Last Operation 으로 다시 할 때도 같은 비례 편집
		m_ModalArgs["proportional"] = m_PropRadius;
		static const char* kFall[] = { "smooth", "sphere", "root", "sharp", "linear", "constant" };
		m_ModalArgs["falloff"] = kFall[std::clamp(m_PropFalloff, 0, 5)];
	}
	if (a) a->M.Touch();
	++d.Revision;
	m_Dirty = true;
}

void ModelEditorWindow::EndModal(bool confirm)
{
	Document& d = Doc();
	const Modal m = m_Modal;
	m_Modal = Modal::None;
	m_CustomAxis = false;
	m_Opt.ExtraLines.clear();
	m_Dirty = true;
	if (m == Modal::Box || m == Modal::LoopCut)
		return;
	if (!confirm)
	{
		d.CancelUndo();
		SetStatus(std::string(ModalName((int)m)) + " cancelled");
		return;
	}
	std::string op = m_ModalOp;
	if (op.empty())
		op = m == Modal::Grab ? "translate" : m == Modal::Rotate ? "rotate" : m == Modal::Scale ? "scale" : m == Modal::Inset ? "inset" : "bevel";
	SetLast(op, m_ModalArgs);
	d.Changed();
	if (Object* a = d.ActiveObject()) a->M.Touch();
	SetStatus(op + " " + m_ModalArgs.dump());
}

void ModelEditorWindow::HandleModal()
{
	ImGuiIO& io = ImGui::GetIO();
	if (m_Modal == Modal::Box)
	{
		if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
		{
			BoxSelect(m_BoxStart, ToView(io.MousePos), io.KeyShift, io.KeyCtrl);
			EndModal(true);
		}
		return;
	}
	if (m_Modal == Modal::LoopCut)
	{
		if (io.MouseWheel != 0.0f) m_LoopCuts = std::clamp(m_LoopCuts + (io.MouseWheel > 0 ? 1 : -1), 1, 32);
		if (ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)) { EndModal(false); return; }
		if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
		{
			Object* o = Doc().ActiveObject();
			const int e = PickEdge(ToView(io.MousePos), 14.0f);
			m_Modal = Modal::None;
			if (o && e >= 0)
			{
				const Edge ed = o->M.Edges()[e];
				Run("loopcut", { { "edge", { ed.A, ed.B } }, { "cuts", m_LoopCuts } });
			}
		}
		return;
	}
	// 축 · 숫자 입력
	auto toggleAxis = [&](int axis) { m_CustomAxis = false; m_Axis = m_Axis == axis ? -1 : axis; };
	if (ImGui::IsKeyPressed(ImGuiKey_X, false)) toggleAxis(0);
	if (ImGui::IsKeyPressed(ImGuiKey_Y, false)) toggleAxis(1);
	if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) toggleAxis(2);
	for (int k = 0; k <= 9; ++k)
		if (ImGui::IsKeyPressed((ImGuiKey)(ImGuiKey_0 + k), false) || ImGui::IsKeyPressed((ImGuiKey)(ImGuiKey_Keypad0 + k), false)) m_Numeric += (char)('0' + k);
	if (ImGui::IsKeyPressed(ImGuiKey_Period, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadDecimal, false)) m_Numeric += '.';
	if (ImGui::IsKeyPressed(ImGuiKey_Minus, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract, false))
		m_Numeric = !m_Numeric.empty() && m_Numeric[0] == '-' ? m_Numeric.substr(1) : "-" + m_Numeric;
	if (ImGui::IsKeyPressed(ImGuiKey_Backspace) && !m_Numeric.empty()) m_Numeric.pop_back();
	ApplyModal();
	if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))
		EndModal(true);
	else if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsKeyPressed(ImGuiKey_Escape))
		EndModal(false);
}

// ------------------------------------------------------------------ 입력
void ModelEditorWindow::HandleShortcuts()
{
	ImGuiIO& io = ImGui::GetIO();
	if (io.WantTextInput)
		return;
	Document& d = Doc();
	const bool ctrl = io.KeyCtrl, shift = io.KeyShift, alt = io.KeyAlt;
	auto pressed = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, false); };

	if (ctrl && pressed(ImGuiKey_Z)) { if (shift) { if (d.Redo()) SetStatus("Redo"); } else if (d.Undo()) SetStatus("Undo"); m_Dirty = true; return; }
	if (ctrl && pressed(ImGuiKey_Y)) { if (d.Redo()) SetStatus("Redo"); m_Dirty = true; return; }
	if (ctrl && pressed(ImGuiKey_S)) { SaveFile(false); return; }
	if (pressed(ImGuiKey_Tab))
	{
		if (d.ActiveObject()) { Run("mode", { { "mode", d.EditMode ? "object" : "edit" } }); }
		return;
	}
	// 시점 (숫자 패드)
	auto preset = [&](const char* name) { const float dist = m_Cam.Distance; const Vec3 t = m_Cam.Target; m_Cam.SetPreset(name); m_Cam.Distance = dist; m_Cam.Target = t; m_Dirty = true; };
	if (pressed(ImGuiKey_Keypad1)) preset(ctrl ? "back" : "front");
	if (pressed(ImGuiKey_Keypad3)) preset(ctrl ? "left" : "right");
	if (pressed(ImGuiKey_Keypad7)) preset(ctrl ? "bottom" : "top");
	if (pressed(ImGuiKey_Keypad5)) { m_Cam.Ortho = !m_Cam.Ortho; m_Dirty = true; }
	if (pressed(ImGuiKey_O) && !ctrl && !alt) { m_Prop = !m_Prop; SetStatus(m_Prop ? "Proportional Editing on (wheel while moving = radius)" : "Proportional Editing off"); return; }
	if (pressed(ImGuiKey_KeypadDecimal) || (pressed(ImGuiKey_F) && !d.EditMode)) FrameSelected();
	if (pressed(ImGuiKey_Home)) FrameAll();
	if (alt && pressed(ImGuiKey_Z)) { m_Opt.XRay = !m_Opt.XRay; m_Dirty = true; return; }

	// 변환
	if (!ctrl && !alt && pressed(ImGuiKey_G)) { BeginModal(Modal::Grab); return; }
	if (!ctrl && !alt && pressed(ImGuiKey_R)) { BeginModal(Modal::Rotate); return; }
	if (!ctrl && !alt && pressed(ImGuiKey_S)) { BeginModal(Modal::Scale); return; }

	if (!d.EditMode)
	{
		if (pressed(ImGuiKey_A)) { Run("object.select", { { "all", !alt } }); return; }
		if ((pressed(ImGuiKey_X) || pressed(ImGuiKey_Delete)) && !ctrl) { Run("object.delete"); return; }
		if (shift && pressed(ImGuiKey_D)) { if (Run("object.duplicate")) BeginModal(Modal::Grab, "object.duplicate"); return; }
		if (ctrl && pressed(ImGuiKey_J)) { Run("object.join"); return; }
		if (ctrl && pressed(ImGuiKey_A)) { Run("object.apply"); return; }
		return;
	}
	// ---- Edit 모드
	if (!ctrl && !alt && !shift)
	{
		if (pressed(ImGuiKey_1)) { Run("mode", { { "select", "vertex" } }); return; }
		if (pressed(ImGuiKey_2)) { Run("mode", { { "select", "edge" } }); return; }
		if (pressed(ImGuiKey_3)) { Run("mode", { { "select", "face" } }); return; }
	}
	if (pressed(ImGuiKey_A)) { Run(alt ? "select.none" : "select.all"); return; }
	if (ctrl && pressed(ImGuiKey_I)) { Run("select.invert"); return; }
	if (pressed(ImGuiKey_L) || (ctrl && pressed(ImGuiKey_L))) { Run("select.linked"); return; }
	if (ctrl && pressed(ImGuiKey_KeypadAdd)) { Run("select.grow"); return; }
	if (ctrl && pressed(ImGuiKey_KeypadSubtract)) { Run("select.shrink"); return; }
	if (pressed(ImGuiKey_F) && !ctrl) { Run("fill"); return; }
	if (pressed(ImGuiKey_M)) { Run("merge", { { "type", "center" } }); return; }
	if ((pressed(ImGuiKey_X) || pressed(ImGuiKey_Delete)) && !ctrl)
	{
		Run("delete", { { "type", d.Mode == SelectMode::Face ? "faces" : "verts" } });
		return;
	}
	if (pressed(ImGuiKey_E) && !ctrl)
	{
		Object* o = d.ActiveObject();
		if (!o) return;
		// 고른 면의 평균 법선 (월드) 쪽으로만 끌기
		Vec3 n(0, 0, 0);
		bool faces = false;
		for (int f = 0; f < (int)o->M.Faces.size(); ++f) if (o->M.Faces[f].Sel) { n += o->M.FaceNormal(f); faces = true; }
		if (Run("extrude", { { "distance", 0.0f } }))
		{
			BeginModal(Modal::Grab, "extrude");
			if (faces && n.LengthSquared() > 1e-10f)
			{
				Matrix nw = o->World(); nw.Translation(Vec3(0, 0, 0));
				m_AxisDir = Vec3::TransformNormal(n, nw);
				m_AxisDir.Normalize();
				m_CustomAxis = true;
			}
		}
		return;
	}
	if (pressed(ImGuiKey_I) && !ctrl) { BeginModal(Modal::Inset); return; }
	if (ctrl && pressed(ImGuiKey_B)) { BeginModal(Modal::Bevel); return; }
	if (ctrl && pressed(ImGuiKey_R)) { m_LoopCuts = 1; BeginModal(Modal::LoopCut); SetStatus("Loop Cut: click an edge (wheel = cuts, Esc = cancel)"); return; }
	if (shift && pressed(ImGuiKey_D)) { if (Run("duplicate")) BeginModal(Modal::Grab, "duplicate"); return; }
	if (shift && ctrl && pressed(ImGuiKey_N)) { Run("recalc_normals"); return; }
}

void ModelEditorWindow::HandleViewportInput(bool hovered)
{
	ImGuiIO& io = ImGui::GetIO();
	// 카메라: 가운데 버튼 = 돌리기 (Shift = 옮기기), Alt + 왼쪽 = 돌리기, 휠 = 확대
	const bool orbitDrag = ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f) || (io.KeyAlt && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f) && m_Modal == Modal::None);
	if (orbitDrag && (hovered || ImGui::IsItemActive()))
	{
		const ImVec2 dlt = io.MouseDelta;
		if (io.KeyShift)
		{
			const float s = PixelToWorld(m_Cam.Target);
			const Vec3 f = m_Cam.Forward();
			Vec3 right = Vec3(0, 1, 0).Cross(f);
			if (right.LengthSquared() < 1e-6f) right = Vec3(1, 0, 0);
			right.Normalize();
			const Vec3 up = f.Cross(right);
			m_Cam.Target += (-right * dlt.x + up * dlt.y) * s;
		}
		else
		{
			m_Cam.Yaw += dlt.x * 0.008f;
			m_Cam.Pitch = std::clamp(m_Cam.Pitch + dlt.y * 0.008f, -kPi * 0.5f + 0.001f, kPi * 0.5f - 0.001f);
			if (m_Cam.Ortho && fabsf(dlt.x) + fabsf(dlt.y) > 0.0f) m_Cam.Ortho = false;   // Blender: 돌리면 원근으로
		}
		m_Dirty = true;
	}
	const bool propModal = m_Prop && Doc().EditMode && (m_Modal == Modal::Grab || m_Modal == Modal::Rotate || m_Modal == Modal::Scale) && !m_Weights.empty();
	if (propModal && io.MouseWheel != 0.0f)
	{
		m_PropRadius = std::clamp(m_PropRadius * powf(1.15f, -io.MouseWheel), 0.001f, 1000.0f);
		ComputeWeights();
		ApplyModal();
	}
	else if (hovered && io.MouseWheel != 0.0f && m_Modal != Modal::LoopCut)
	{
		m_Cam.Distance = std::clamp(m_Cam.Distance * powf(0.88f, io.MouseWheel), 0.01f, 10000.0f);
		m_Dirty = true;
	}
	if (m_Modal != Modal::None)
		return;
	// 왼쪽 클릭 = 고르기, 끌기 = 상자 고르기
	if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.KeyAlt)
	{
		m_BoxPending = true;
		m_BoxStart = ToView(io.MousePos);
	}
	if (m_BoxPending)
	{
		if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
		{
			m_BoxPending = false;
			ClickSelect(io.KeyShift);
		}
		else if (Len(Sub(ToView(io.MousePos), m_BoxStart)) > 4.0f)
		{
			m_BoxPending = false;
			BeginModal(Modal::Box);
		}
	}
	if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !Doc().EditMode && Doc().ActiveObject())
		Run("mode", { { "mode", "edit" } });   // 오브젝트 두 번 클릭 = Edit 모드
}

// ------------------------------------------------------------------ 그리기
void ModelEditorWindow::UploadTexture()
{
	auto device = Application::GetI()->GetDevice();
	auto ctx = Application::GetI()->GetDeviceContext();
	if (!device || !ctx)
		return;
	if (!m_Texture || m_TexW != m_Raster.Width || m_TexH != m_Raster.Height)
	{
		D3D11_TEXTURE2D_DESC td = {};
		td.Width = m_Raster.Width;
		td.Height = m_Raster.Height;
		td.MipLevels = td.ArraySize = 1;
		td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		td.SampleDesc.Count = 1;
		td.Usage = D3D11_USAGE_DEFAULT;
		td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		m_Srv.Reset();
		m_Texture.Reset();
		if (FAILED(device->CreateTexture2D(&td, nullptr, m_Texture.GetAddressOf())))
			return;
		device->CreateShaderResourceView(m_Texture.Get(), nullptr, m_Srv.GetAddressOf());
		m_TexW = m_Raster.Width;
		m_TexH = m_Raster.Height;
	}
	ctx->UpdateSubresource(m_Texture.Get(), 0, nullptr, m_Raster.Color.data(), m_Raster.Width * 4, 0);
}

void ModelEditorWindow::RenderViewport()
{
	Document& d = Doc();
	const int w = (std::max)(8, (int)m_ViewSize.x), h = (std::max)(8, (int)m_ViewSize.y);
	if (w != m_Raster.Width || h != m_Raster.Height)
	{
		m_Raster.Resize(w, h);
		m_Dirty = true;
	}
	if (d.Revision != m_DrawnRevision)
		m_Dirty = true;
	if (!m_Dirty)
		return;
	m_Raster.Render(d, m_Cam, m_Opt);
	UploadTexture();
	m_DrawnRevision = d.Revision;
	m_Dirty = false;
}

void ModelEditorWindow::DrawMenuBar()
{
	Document& d = Doc();
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(12.0f, 4.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 3.0f));
	if (!ImGui::BeginMenuBar())
	{
		ImGui::PopStyleVar(2);
		return;
	}
	if (ImGui::BeginMenu("File"))
	{
		if (ImGui::MenuItem("New")) { Run("new"); FrameAll(); }
		if (ImGui::MenuItem("Open...")) OpenFile(false);
		if (ImGui::MenuItem("Import (FBX, OBJ, glTF...)")) OpenFile(true);
		ImGui::Separator();
		if (ImGui::MenuItem("Save", "Ctrl+S")) SaveFile(false);
		if (ImGui::MenuItem("Save As...")) SaveFile(true);
		ImGui::Separator();
		if (ImGui::BeginMenu("Export"))
		{
			if (ImGui::MenuItem("FBX (.fbx)")) ExportFile(L"fbx");
			if (ImGui::MenuItem("Wavefront (.obj)")) ExportFile(L"obj");
			if (ImGui::MenuItem("glTF Binary (.glb)")) ExportFile(L"glb");
			ImGui::EndMenu();
		}
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("Edit"))
	{
		if (ImGui::MenuItem(("Undo " + d.UndoLabel()).c_str(), "Ctrl+Z", false, d.CanUndo())) { d.Undo(); m_Dirty = true; }
		if (ImGui::MenuItem(("Redo " + d.RedoLabel()).c_str(), "Ctrl+Shift+Z", false, d.CanRedo())) { d.Redo(); m_Dirty = true; }
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("View"))
	{
		if (ImGui::MenuItem("Frame All", "Home")) FrameAll();
		if (ImGui::MenuItem("Frame Selected", "Numpad .")) FrameSelected();
		ImGui::Separator();
		for (const char* v : { "front", "back", "right", "left", "top", "bottom", "persp" })
			if (ImGui::MenuItem(v)) { const float dist = m_Cam.Distance; const Vec3 t = m_Cam.Target; m_Cam.SetPreset(v); m_Cam.Distance = dist; m_Cam.Target = t; m_Dirty = true; }
		ImGui::Separator();
		if (ImGui::MenuItem("Orthographic", "Numpad 5", m_Cam.Ortho)) { m_Cam.Ortho = !m_Cam.Ortho; m_Dirty = true; }
		if (ImGui::MenuItem("X-Ray", "Alt+Z", m_Opt.XRay)) { m_Opt.XRay = !m_Opt.XRay; m_Dirty = true; }
		if (ImGui::MenuItem("Wireframe Overlay", nullptr, m_Opt.Wireframe)) { m_Opt.Wireframe = !m_Opt.Wireframe; m_Dirty = true; }
		if (ImGui::MenuItem("Grid", nullptr, m_Opt.Grid)) { m_Opt.Grid = !m_Opt.Grid; m_Dirty = true; }
		if (ImGui::MenuItem("Reference Images", nullptr, m_Opt.Refs)) { m_Opt.Refs = !m_Opt.Refs; m_Dirty = true; }
		ImGui::Separator();
		if (ImGui::MenuItem("Solid", nullptr, m_Opt.Shade == Shading::Solid)) { m_Opt.Shade = Shading::Solid; m_Dirty = true; }
		if (ImGui::MenuItem("Toon", nullptr, m_Opt.Shade == Shading::Toon)) { m_Opt.Shade = Shading::Toon; m_Dirty = true; }
		if (ImGui::MenuItem("Normals", nullptr, m_Opt.Shade == Shading::Normals)) { m_Opt.Shade = Shading::Normals; m_Dirty = true; }
		if (ImGui::MenuItem("UV Checker", nullptr, m_Opt.Shade == Shading::UVChecker)) { m_Opt.Shade = Shading::UVChecker; m_Dirty = true; }
		if (ImGui::MenuItem("Weights (selected bone)", nullptr, m_Opt.Shade == Shading::Weights)) { m_Opt.Shade = Shading::Weights; m_Dirty = true; }
		if (ImGui::MenuItem("Bones", nullptr, m_Opt.Bones)) { m_Opt.Bones = !m_Opt.Bones; m_Dirty = true; }
		if (ImGui::MenuItem("Toon Outline", nullptr, m_Opt.Outline)) { m_Opt.Outline = !m_Opt.Outline; m_Dirty = true; }
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("Add"))
	{
		struct P { const char* Label; const char* Type; };
		static const P prims[] = { { "Cube", "cube" }, { "Plane", "plane" }, { "Circle", "circle" }, { "UV Sphere", "uvsphere" }, { "Ico Sphere", "icosphere" },
			{ "Cylinder", "cylinder" }, { "Cone", "cone" }, { "Torus", "torus" } };
		for (const P& p : prims)
			if (ImGui::MenuItem(p.Label)) { Run("add", { { "type", p.Type }, { "location", { m_Cam.Target.x, 0.0f, m_Cam.Target.z } } }); }
		ImGui::EndMenu();
	}
	if (d.EditMode)
	{
		if (ImGui::BeginMenu("Select"))
		{
			if (ImGui::MenuItem("All", "A")) Run("select.all");
			if (ImGui::MenuItem("None", "Alt+A")) Run("select.none");
			if (ImGui::MenuItem("Invert", "Ctrl+I")) Run("select.invert");
			ImGui::Separator();
			if (ImGui::MenuItem("Linked", "L")) Run("select.linked");
			if (ImGui::MenuItem("Grow", "Ctrl+Numpad +")) Run("select.grow");
			if (ImGui::MenuItem("Shrink", "Ctrl+Numpad -")) Run("select.shrink");
			ImGui::Separator();
			if (ImGui::MenuItem("Faces Facing Up")) Run("select.normal", { { "direction", { 0, 1, 0 } } });
			if (ImGui::MenuItem("Right Half (+X)")) Run("select.box", { { "min", { 0.0001f, -1e9f, -1e9f } }, { "max", { 1e9f, 1e9f, 1e9f } } });
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Mesh"))
		{
			if (ImGui::MenuItem("Extrude", "E")) Run("extrude", { { "distance", 0.2f } });
			if (ImGui::MenuItem("Extrude Individual")) Run("extrude", { { "distance", 0.2f }, { "individual", true } });
			if (ImGui::MenuItem("Inset", "I")) Run("inset", { { "thickness", 0.05f } });
			if (ImGui::MenuItem("Bevel", "Ctrl+B")) Run("bevel", { { "offset", 0.05f } });
			if (ImGui::MenuItem("Loop Cut", "Ctrl+R")) { m_LoopCuts = 1; BeginModal(Modal::LoopCut); }
			if (ImGui::MenuItem("Subdivide")) Run("subdivide");
			if (ImGui::MenuItem("Subdivision Surface (Catmull-Clark)")) Run("subsurf", { { "levels", 1 } });
			ImGui::Separator();
			if (ImGui::MenuItem("Mirror X")) Run("mirror", { { "axis", "x" } });
			if (ImGui::MenuItem("Symmetrize +X to -X")) Run("symmetrize", { { "direction", "+x" } });
			if (ImGui::MenuItem("Duplicate", "Shift+D")) Run("duplicate");
			ImGui::Separator();
			if (ImGui::MenuItem("Merge at Center", "M")) Run("merge", { { "type", "center" } });
			if (ImGui::MenuItem("Merge by Distance")) Run("merge", { { "type", "distance" }, { "distance", 0.0001f } });
			if (ImGui::MenuItem("Fill", "F")) Run("fill");
			if (ImGui::MenuItem("Bridge Edge Loops")) Run("bridge");
			ImGui::Separator();
			if (ImGui::MenuItem("Shade Smooth")) Run("shade", { { "smooth", true } });
			if (ImGui::MenuItem("Shade Flat")) Run("shade", { { "smooth", false } });
			if (ImGui::MenuItem("Recalculate Normals", "Shift+Ctrl+N")) Run("recalc_normals");
			if (ImGui::MenuItem("Flip Normals")) Run("flip");
			if (ImGui::MenuItem("Smooth Vertices")) Run("smooth");
			if (ImGui::MenuItem("Triangulate")) Run("triangulate");
			ImGui::Separator();
			if (ImGui::BeginMenu("UV"))
			{
				if (ImGui::MenuItem("Smart UV Project")) { Run("uv.smart"); m_Opt.Shade = Shading::UVChecker; }
				if (ImGui::MenuItem("Cube Projection")) { Run("uv.project", { { "mode", "box" } }); m_Opt.Shade = Shading::UVChecker; }
				if (ImGui::MenuItem("Cylinder Projection")) { Run("uv.project", { { "mode", "cylinder" } }); m_Opt.Shade = Shading::UVChecker; }
				if (ImGui::MenuItem("Sphere Projection")) { Run("uv.project", { { "mode", "sphere" } }); m_Opt.Shade = Shading::UVChecker; }
				if (ImGui::MenuItem("Project From Normal")) { Run("uv.project", { { "mode", "planar" } }); m_Opt.Shade = Shading::UVChecker; }
				ImGui::EndMenu();
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Delete Vertices", "X")) Run("delete", { { "type", "verts" } });
			if (ImGui::MenuItem("Delete Faces")) Run("delete", { { "type", "faces" } });
			ImGui::EndMenu();
		}
	}
	else if (ImGui::BeginMenu("Object"))
	{
		if (ImGui::MenuItem("Duplicate", "Shift+D")) Run("object.duplicate");
		if (ImGui::MenuItem("Join", "Ctrl+J")) Run("object.join");
		if (ImGui::MenuItem("Apply Transforms", "Ctrl+A")) Run("object.apply");
		if (ImGui::MenuItem("Delete", "X")) Run("object.delete");
		ImGui::EndMenu();
	}
	// 모드 · 고르기 모드
	ImGui::Separator();
	const char* modes[] = { "Object Mode", "Edit Mode" };
	int mode = d.EditMode ? 1 : 0;
	ImGui::SetNextItemWidth(110.0f);
	if (ImGui::Combo("##mode", &mode, modes, 2) && (mode == 1) != d.EditMode)
		Run("mode", { { "mode", mode ? "edit" : "object" } });
	if (d.EditMode)
	{
		const char* labels[] = { "Vert", "Edge", "Face" };
		const char* keys[] = { "vertex", "edge", "face" };
		for (int i = 0; i < 3; ++i)
		{
			const bool on = (int)d.Mode == i;
			if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.27f, 0.45f, 0.70f, 1.0f));
			if (ImGui::SmallButton(labels[i])) Run("mode", { { "select", keys[i] } });
			if (on) ImGui::PopStyleColor();
		}
	}
	ImGui::EndMenuBar();
	ImGui::PopStyleVar(2);
}

void ModelEditorWindow::DrawOverlay(ImDrawList* dl)
{
	Document& d = Doc();
	const ImVec2 o = m_ViewPos;
	// 왼쪽 위: 시점 이름 · 오브젝트
	std::string view = m_Cam.Ortho ? "Orthographic" : "User Perspective";
	if (m_Cam.Ortho)
	{
		const Vec3 f = m_Cam.Forward();
		if (f.z < -0.99f) view = "Front Orthographic";
		else if (f.z > 0.99f) view = "Back Orthographic";
		else if (f.x < -0.99f) view = "Right Orthographic";
		else if (f.x > 0.99f) view = "Left Orthographic";
		else if (f.y < -0.99f) view = "Top Orthographic";
		else if (f.y > 0.99f) view = "Bottom Orthographic";
	}
	dl->AddText(ImVec2(o.x + 10, o.y + 8), kText, view.c_str());
	const Object* a = d.ActiveObject();
	std::string info = a ? "(" + a->Name + ")" : "(no object)";
	if (d.EditMode && a)
	{
		int sv = 0, sf = 0;
		for (const Vert& v : a->M.Verts) sv += v.Sel;
		for (const Face& f : a->M.Faces) sf += f.Sel;
		info += "  Verts " + std::to_string(sv) + "/" + std::to_string(a->M.Verts.size()) + "  Faces " + std::to_string(sf) + "/" + std::to_string(a->M.Faces.size());
	}
	dl->AddText(ImVec2(o.x + 10, o.y + 26), kTextDim, info.c_str());

	// 오른쪽 위: 축 표시 (X 빨강, Y 초록, Z 파랑)
	{
		const ImVec2 c(o.x + m_ViewSize.x - 44.0f, o.y + 48.0f);
		const Matrix v = m_Cam.View();
		struct Ax { Vec3 D; ImU32 Col; const char* L; };
		Ax axes[3] = { { Vec3(1, 0, 0), IM_COL32(230, 80, 90, 255), "X" }, { Vec3(0, 1, 0), IM_COL32(130, 200, 60, 255), "Y" }, { Vec3(0, 0, 1), IM_COL32(80, 140, 240, 255), "Z" } };
		std::sort(std::begin(axes), std::end(axes), [&](const Ax& p, const Ax& q) { return Vec3::TransformNormal(p.D, v).z > Vec3::TransformNormal(q.D, v).z; });
		for (const Ax& ax : axes)
		{
			const Vec3 s = Vec3::TransformNormal(ax.D, v);
			const ImVec2 e(c.x + s.x * 30.0f, c.y - s.y * 30.0f);
			dl->AddLine(c, e, ax.Col, 2.0f);
			dl->AddCircleFilled(e, 8.0f, ax.Col);
			dl->AddText(ImVec2(e.x - 4.0f, e.y - 7.0f), IM_COL32(20, 20, 20, 255), ax.L);
		}
	}

	// 비례 편집 반경 (끄는 중)
	if (m_Prop && !m_Weights.empty() && (m_Modal == Modal::Grab || m_Modal == Modal::Rotate || m_Modal == Modal::Scale))
	{
		Vec3 c, e;
		const Vec3 f = m_Cam.Forward();
		Vec3 right = Vec3(0, 1, 0).Cross(f);
		if (right.LengthSquared() < 1e-6f) right = Vec3(1, 0, 0);
		right.Normalize();
		if (m_Raster.Project(m_Pivot, c) && m_Raster.Project(m_Pivot + right * m_PropRadius, e))
			dl->AddCircle(ImVec2(o.x + c.x, o.y + c.y), Len(ImVec2(e.x - c.x, e.y - c.y)), IM_COL32(255, 255, 255, 120), 64, 1.5f);
	}

	// 상자 고르기
	if (m_Modal == Modal::Box)
	{
		const ImVec2 a0(o.x + m_BoxStart.x, o.y + m_BoxStart.y), a1 = ImGui::GetIO().MousePos;
		dl->AddRectFilled(ImVec2((std::min)(a0.x, a1.x), (std::min)(a0.y, a1.y)), ImVec2((std::max)(a0.x, a1.x), (std::max)(a0.y, a1.y)), IM_COL32(255, 255, 255, 20));
		dl->AddRect(ImVec2((std::min)(a0.x, a1.x), (std::min)(a0.y, a1.y)), ImVec2((std::max)(a0.x, a1.x), (std::max)(a0.y, a1.y)), IM_COL32(255, 255, 255, 160));
	}
	// Loop Cut: 마우스 아래 변 강조
	if (m_Modal == Modal::LoopCut)
		if (Object* ao = Doc().ActiveObject())
		{
			const int e = PickEdge(ToView(ImGui::GetIO().MousePos), 14.0f);
			if (e >= 0)
			{
				const Edge ed = ao->M.Edges()[e];
				Vec3 sa, sb;
				if (m_Raster.Project(Vec3::Transform(ao->M.Verts[ed.A].P, ao->World()), sa) && m_Raster.Project(Vec3::Transform(ao->M.Verts[ed.B].P, ao->World()), sb))
					dl->AddLine(ImVec2(o.x + sa.x, o.y + sa.y), ImVec2(o.x + sb.x, o.y + sb.y), IM_COL32(255, 230, 60, 255), 3.0f);
			}
		}

	// 아래: 끌기 중 값 / 도움말 / 상태
	std::string bottom;
	if (m_Modal == Modal::Grab || m_Modal == Modal::Rotate || m_Modal == Modal::Scale || m_Modal == Modal::Inset || m_Modal == Modal::Bevel)
	{
		bottom = std::string(ModalName((int)m_Modal)) + "  " + m_ModalArgs.dump();
		if (m_CustomAxis) bottom += "  along normal";
		else if (m_Axis >= 0) bottom += std::string("  along ") + "XYZ"[m_Axis];
		if (!m_Numeric.empty()) bottom += "  [" + m_Numeric + "]";
		bottom += "    X/Y/Z axis, type a number, Ctrl snap, LMB/Enter confirm, RMB/Esc cancel";
	}
	else if (m_Modal == Modal::LoopCut)
		bottom = "Loop Cut: click an edge   cuts " + std::to_string(m_LoopCuts) + " (wheel)   Esc cancel";
	else if (d.EditMode)
		bottom = "G move  R rotate  S scale  E extrude  I inset  Ctrl+R loop cut  Ctrl+B bevel  X delete  M merge  F fill  1/2/3 vert/edge/face  Tab object mode";
	else
		bottom = "Click select  G/R/S transform  Tab edit mode  Shift+D duplicate  X delete  MMB orbit  Shift+MMB pan  Wheel zoom  Numpad views";
	dl->AddRectFilled(ImVec2(o.x, o.y + m_ViewSize.y - 22.0f), ImVec2(o.x + m_ViewSize.x, o.y + m_ViewSize.y), IM_COL32(30, 30, 30, 200));
	dl->AddText(ImVec2(o.x + 8, o.y + m_ViewSize.y - 19.0f), m_Modal != Modal::None ? kAccent : kTextDim, bottom.c_str());
	if (!m_Status.empty() && ImGui::GetTime() - m_StatusTime < 6.0)
	{
		const ImU32 col = m_Status[0] == '!' ? IM_COL32(255, 110, 100, 255) : IM_COL32(170, 210, 255, 255);
		dl->AddText(ImVec2(o.x + 10, o.y + m_ViewSize.y - 42.0f), col, m_Status.c_str());
	}
}

void ModelEditorWindow::DrawSidePanel()
{
	Document& d = Doc();
	// ---- Outliner
	ImGui::TextDisabled("SCENE COLLECTION");
	ImGui::BeginChild("##outliner", ImVec2(0, 160.0f), true);
	for (int i = 0; i < (int)d.Objects.size(); ++i)
	{
		Object& o = d.Objects[i];
		ImGui::PushID(i);
		if (ImGui::SmallButton(o.Visible ? ICON_FA_EYE : ICON_FA_EYE_SLASH))
		{
			d.PushUndo("Visibility");
			o.Visible = !o.Visible;
			d.Changed();
		}
		ImGui::SameLine();
		const bool active = i == d.Active;
		if (active) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.75f, 0.4f, 1.0f));
		if (ImGui::Selectable((std::string(ICON_FA_CUBE) + "  " + o.Name).c_str(), o.Selected))
		{
			const bool extend = ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeyShift;
			if (!extend) for (Object& other : d.Objects) other.Selected = false;
			o.Selected = true;
			if (d.Active != i && d.EditMode) d.EditMode = false;
			d.Active = i;
			d.Changed();
		}
		if (active) ImGui::PopStyleColor();
		ImGui::PopID();
	}
	if (d.Objects.empty())
		ImGui::TextDisabled("Add > Cube, or File > Import");
	ImGui::EndChild();

	// ---- 활성 오브젝트
	if (Object* a = d.ActiveObject())
	{
		ImGui::Spacing();
		ImGui::TextDisabled("OBJECT");
		strncpy_s(m_RenameBuf, a->Name.c_str(), _TRUNCATE);
		ImGui::SetNextItemWidth(-1);
		if (ImGui::InputText("##name", m_RenameBuf, sizeof(m_RenameBuf), ImGuiInputTextFlags_EnterReturnsTrue) && m_RenameBuf[0])
			Run("object.rename", { { "name", std::string(m_RenameBuf) } });
		auto undoOnActivate = [&](const char* label) { if (ImGui::IsItemActivated()) d.PushUndo(label); };
		float loc[3] = { a->Position.x, a->Position.y, a->Position.z };
		if (ImGui::DragFloat3("Location", loc, 0.01f)) { a->Position = Vec3(loc[0], loc[1], loc[2]); d.Changed(); }
		undoOnActivate("Location");
		Vec3 e = ToEulerDeg(a->Rotation);
		float rot[3] = { e.x, e.y, e.z };
		if (ImGui::DragFloat3("Rotation", rot, 0.5f)) { a->Rotation = Quaternion::CreateFromYawPitchRoll(rot[1] * kPi / 180.0f, rot[0] * kPi / 180.0f, rot[2] * kPi / 180.0f); d.Changed(); }
		undoOnActivate("Rotation");
		float scl[3] = { a->Scale.x, a->Scale.y, a->Scale.z };
		if (ImGui::DragFloat3("Scale", scl, 0.01f)) { a->Scale = Vec3(scl[0], scl[1], scl[2]); d.Changed(); }
		undoOnActivate("Scale");

		if (d.EditMode && !a->M.SelectedVerts().empty())
		{
			// 고른 점의 중심 (월드) — 값을 바꾸면 고른 점을 옮긴다
			const Vec3 c = Vec3::Transform(a->M.SelectionCenter(), a->World());
			float med[3] = { c.x, c.y, c.z };
			if (ImGui::DragFloat3("Median", med, 0.005f))
			{
				const Vec3 delta = Vec3(med[0], med[1], med[2]) - c;
				a->M.Translate(Vec3::TransformNormal(delta, a->World().Invert()));
				a->M.Touch();
				d.Changed();
			}
			undoOnActivate("Move");
		}
		int tris = 0;
		for (const Face& f : a->M.Faces) tris += (int)f.V.size() - 2;
		ImGui::TextDisabled("Verts %d   Faces %d   Tris %d", (int)a->M.Verts.size(), (int)a->M.Faces.size(), tris);
		if (d.EditMode)
		{
			const int boundary = a->M.BoundaryEdges(), nonManifold = a->M.NonManifoldEdges();
			if (boundary || nonManifold) ImGui::TextDisabled("Open edges %d   Non-manifold %d", boundary, nonManifold);
		}
	}

	DrawModifiersPanel();
	if (d.EditMode)
	{
		DrawMaterialsPanel();
		DrawGroupsPanel();
	}
	DrawRefsPanel();
	DrawRigPanel();

	// ---- Last Operation (값을 바꾸면 되돌리고 다시)
	const LastOp& last = Last();
	if (!last.Name.empty() && last.Args.is_object() && !last.Args.empty())
	{
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.35f, 1.0f), "%s", last.Name.c_str());
		json args = last.Args;
		bool changed = false;
		for (auto it = args.begin(); it != args.end(); ++it)
		{
			const std::string key = it.key();
			ImGui::PushID(key.c_str());
			json& v = it.value();
			if (v.is_boolean()) { bool b = v.get<bool>(); if (ImGui::Checkbox(key.c_str(), &b)) { v = b; changed = true; } }
			else if (v.is_number_integer()) { int n = v.get<int>(); if (ImGui::InputInt(key.c_str(), &n)) { v = n; changed = true; } }
			else if (v.is_number()) { float f = v.get<float>(); if (ImGui::DragFloat(key.c_str(), &f, 0.005f)) { v = f; changed = true; } }
			else if (v.is_array() && v.size() == 3 && v[0].is_number())
			{
				float f3[3] = { v[0].get<float>(), v[1].get<float>(), v[2].get<float>() };
				if (ImGui::DragFloat3(key.c_str(), f3, 0.005f)) { v = { f3[0], f3[1], f3[2] }; changed = true; }
			}
			else if (v.is_string()) ImGui::LabelText(key.c_str(), "%s", v.get<std::string>().c_str());
			ImGui::PopID();
		}
		if (changed)
		{
			json r;
			std::string err;
			if (!RerunLast(args, r, err)) SetStatus("! " + err);
			m_Dirty = true;
		}
	}
}

void ModelEditorWindow::DrawModifiersPanel()
{
	Document& d = Doc();
	Object* a = d.ActiveObject();
	if (!a) return;
	ImGui::Spacing();
	ImGui::TextDisabled("MODIFIERS");
	bool mirror = a->MirrorX;
	if (ImGui::Checkbox("Mirror X", &mirror)) Run("modifier.mirror", { { "enable", mirror } });
	if (a->MirrorX)
	{
		ImGui::SameLine();
		bool clip = a->MirrorClip;
		if (ImGui::Checkbox("Clipping", &clip)) Run("modifier.mirror", { { "enable", true }, { "clip", clip } });
	}
	int levels = a->Subsurf;
	ImGui::SetNextItemWidth(120.0f);
	if (ImGui::SliderInt("Subdivision", &levels, 0, 3)) Run("modifier.subsurf", { { "levels", levels } });
	if (a->HasModifiers())
	{
		ImGui::SameLine();
		if (ImGui::Button("Apply")) Run("modifier.apply");
	}
	// 비례 편집 (O)
	ImGui::Checkbox("Proportional (O)", &m_Prop);
	if (m_Prop)
	{
		static const char* kFall[] = { "Smooth", "Sphere", "Root", "Sharp", "Linear", "Constant" };
		ImGui::SetNextItemWidth(90.0f);
		ImGui::Combo("##falloff", &m_PropFalloff, kFall, 6);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(80.0f);
		ImGui::DragFloat("Radius", &m_PropRadius, 0.005f, 0.001f, 100.0f, "%.3f m");
	}
}

void ModelEditorWindow::DrawMaterialsPanel()
{
	Document& d = Doc();
	Object* a = d.ActiveObject();
	if (!a) return;
	ImGui::Spacing();
	ImGui::TextDisabled("MATERIALS");
	const int count = (int)(std::max)((std::max)(d.Materials.size(), d.MaterialColors.size()), (size_t)1);
	for (int i = 0; i < count; ++i)
	{
		ImGui::PushID(i);
		Vec3 c = d.MaterialColor(i);
		float col[3] = { c.x, c.y, c.z };
		if (ImGui::ColorEdit3("##col", col, ImGuiColorEditFlags_NoInputs))
		{
			while ((int)d.MaterialColors.size() <= i) d.MaterialColors.push_back(Vec3(0.8f, 0.8f, 0.8f));
			d.MaterialColors[i] = Vec3(col[0], col[1], col[2]);
			d.Changed();
		}
		ImGui::SameLine();
		const std::string name = i < (int)d.Materials.size() && !d.Materials[i].empty() ? d.Materials[i] : (i == 0 ? "Material" : "Material." + std::to_string(i));
		ImGui::TextUnformatted(name.c_str());
		ImGui::SameLine();
		if (ImGui::SmallButton("Assign")) Run("material.set", { { "index", i } });
		ImGui::PopID();
	}
	if (ImGui::SmallButton("+ Slot"))
	{
		d.Materials.resize(count + 1);
		d.MaterialColors.resize(count + 1, Vec3(0.8f, 0.8f, 0.8f));
		d.Materials[count] = "Material." + std::to_string(count);
		d.Changed();
	}
}

void ModelEditorWindow::DrawGroupsPanel()
{
	Document& d = Doc();
	Object* a = d.ActiveObject();
	if (!a) return;
	Modeling::Mesh& m = a->M;
	ImGui::Spacing();
	ImGui::TextDisabled("VERTEX GROUPS");
	ImGui::BeginChild("##groups", ImVec2(0, 92.0f), true);
	for (int g = 0; g < (int)m.Groups.size(); ++g)
	{
		ImGui::PushID(g);
		if (ImGui::Selectable((m.Groups[g] + "  (" + std::to_string(m.GroupCount(g)) + ")").c_str(), m_Group == g))
		{
			m_Group = g;
			strncpy_s(m_GroupName, m.Groups[g].c_str(), _TRUNCATE);
		}
		ImGui::PopID();
	}
	if (m.Groups.empty()) ImGui::TextDisabled("Select vertices, type a name, Assign");
	ImGui::EndChild();
	ImGui::SetNextItemWidth(-1);
	ImGui::InputText("##groupName", m_GroupName, sizeof(m_GroupName));
	const std::string name = m_GroupName;
	if (ImGui::Button("Assign")) Run("group.assign", { { "name", name } });
	ImGui::SameLine();
	if (ImGui::Button("Remove")) Run("group.remove", { { "name", name } });
	ImGui::SameLine();
	if (ImGui::Button("Select")) Run("group.select", { { "name", name }, { "extend", true } });
	ImGui::SameLine();
	if (ImGui::Button("Deselect")) Run("group.select", { { "name", name }, { "deselect", true } });
	ImGui::SameLine();
	if (ImGui::Button(ICON_FA_TRASH)) Run("group.delete", { { "name", name } });
}

// 아마추어 (리깅): Humanoid 맞추기 · 자동 가중치 · 머리카락 사슬 · 충돌체, 본 목록 (고르면 가중치 보기 · 포즈)
void ModelEditorWindow::DrawRigPanel()
{
	Document& d = Doc();
	Armature& arm = d.Rig;
	ImGui::Spacing();
	ImGui::TextDisabled("ARMATURE");
	if (ImGui::Button("Humanoid")) Run("rig.humanoid");
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Fit a Humanoid skeleton (parts by name: Head, Body, ArmL/R, LegL/R)");
	ImGui::SameLine();
	ImGui::BeginDisabled(arm.Empty());
	if (ImGui::Button("Auto Weights")) Run("rig.weights");
	ImGui::SameLine();
	if (ImGui::Button("Chains")) Run("rig.chain");
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Spring bone chain per island of the selected objects (hair strands)");
	ImGui::SameLine();
	if (ImGui::Button("Colliders")) Run("rig.colliders");
	ImGui::EndDisabled();
	if (arm.Empty())
	{
		ImGui::TextDisabled("No armature");
		return;
	}
	int springs = 0;
	for (const Bone& b : arm.Bones) springs += b.Spring;
	ImGui::TextDisabled("%d bones (%d spring), %d colliders", (int)arm.Bones.size(), springs, (int)arm.Colliders.size());
	if (m_Bone >= (int)arm.Bones.size()) m_Bone = -1;
	ImGui::BeginChild("##bones", ImVec2(0, 120.0f), true);
	for (int i = 0; i < (int)arm.Bones.size(); ++i)
	{
		const Bone& b = arm.Bones[i];
		int depth = 0;
		for (int p = b.Parent; p >= 0 && depth < 12; p = arm.Bones[p].Parent) ++depth;
		ImGui::PushID(i);
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + depth * 8.0f);
		if (ImGui::Selectable((b.Spring ? "~ " + b.Name : b.Name).c_str(), m_Bone == i))
		{
			m_Bone = i;
			m_PoseEuler[0] = b.PoseEuler.x; m_PoseEuler[1] = b.PoseEuler.y; m_PoseEuler[2] = b.PoseEuler.z;
			m_Opt.SelectedBone = i;
			m_Opt.WeightBone = b.Name;
			m_Dirty = true;
		}
		ImGui::PopID();
	}
	ImGui::EndChild();
	if (m_Bone >= 0)
	{
		Bone& b = arm.Bones[m_Bone];
		bool weights = m_Opt.Shade == Shading::Weights;
		if (ImGui::Checkbox("Show Weights", &weights)) { m_Opt.Shade = weights ? Shading::Weights : Shading::Solid; m_Dirty = true; }
		// 포즈 미리보기 (Undo 없이 바로 — 저장 · 내보내기 하지 않는다)
		ImGui::SetNextItemWidth(-1);
		if (ImGui::DragFloat3("##pose", m_PoseEuler, 0.5f, -180.0f, 180.0f, "%.0f°"))
		{
			b.Pose = Quaternion::CreateFromYawPitchRoll(m_PoseEuler[1] / 57.29578f, m_PoseEuler[0] / 57.29578f, m_PoseEuler[2] / 57.29578f);
			b.PoseEuler = Vec3(m_PoseEuler[0], m_PoseEuler[1], m_PoseEuler[2]);
			d.Changed();
		}
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pose preview (degrees X Y Z) - bend to check the weights");
	}
	if (arm.HasPose() && ImGui::Button("Reset Pose"))
	{
		arm.ClearPose();
		m_PoseEuler[0] = m_PoseEuler[1] = m_PoseEuler[2] = 0.0f;
		d.Changed();
	}
}

void ModelEditorWindow::DrawRefsPanel()
{
	Document& d = Doc();
	ImGui::Spacing();
	ImGui::TextDisabled("REFERENCE IMAGES");
	for (int i = 0; i < (int)d.Refs.size(); ++i)
	{
		RefImage& r = d.Refs[i];
		ImGui::PushID(i);
		if (ImGui::Checkbox("##vis", &r.Visible)) m_Dirty = true;
		ImGui::SameLine();
		ImGui::Text("%s  (%s%s)", r.Name.c_str(), r.View.c_str(), r.Pixels.empty() ? ", missing" : "");
		ImGui::SameLine();
		const bool remove = ImGui::SmallButton(ICON_FA_XMARK);
		if (ImGui::SliderFloat("Opacity", &r.Opacity, 0.0f, 1.0f, "%.2f")) m_Dirty = true;
		float h = r.Height;
		if (ImGui::DragFloat("Height", &h, 0.01f, 0.01f, 100.0f, "%.2f m")) { r.Height = h; m_Dirty = true; }
		float c3[3] = { r.Center.x, r.Center.y, r.Center.z };
		if (ImGui::DragFloat3("Center", c3, 0.005f)) { r.Center = Vec3(c3[0], c3[1], c3[2]); m_Dirty = true; }
		ImGui::PopID();
		if (remove) { Run("ref.remove", { { "name", r.Name } }); break; }
	}
	static const char* kViews[] = { "front", "right", "back", "left", "top" };
	ImGui::SetNextItemWidth(80.0f);
	ImGui::Combo("##refView", &m_RefView, kViews, 5);
	ImGui::SameLine();
	ImGui::SetNextItemWidth(70.0f);
	ImGui::DragFloat("##refH", &m_RefHeight, 0.01f, 0.05f, 100.0f, "%.2f m");
	ImGui::SameLine();
	if (ImGui::Button("Add Image..."))
	{
		const std::string path = FileDialog(false, L"Images (*.png;*.jpg;*.jpeg;*.bmp)\0*.png;*.jpg;*.jpeg;*.bmp\0\0", nullptr);
		if (!path.empty() && Run("ref.add", { { "path", path }, { "view", kViews[m_RefView] }, { "height", m_RefHeight } }))
		{
			const float dist = m_Cam.Distance;
			m_Cam.SetPreset(kViews[m_RefView]);   // 그 시점으로 (기준 그림은 그 시점에서만 보인다)
			m_Cam.Distance = dist;
			m_Dirty = true;
		}
	}
	if (!d.Refs.empty() && ImGui::Button("Compare Silhouette"))
	{
		json r;
		std::string err;
		if (RunOp("compare", { { "ref", d.Refs.front().Name } }, r, err))
			SetStatus("compare " + d.Refs.front().Name + ": IoU " + r["iou"].dump() + (r["hints"].empty() ? "" : "  |  " + r["hints"][0].get<std::string>()));
		else
			SetStatus("! " + err);
	}
}

void ModelEditorWindow::OnRender()
{
	Document& d = Doc();
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
		Undo::BlockShortcuts();   // Ctrl+Z 는 이 창의 Undo
	DrawMenuBar();

	const ImVec2 avail = ImGui::GetContentRegionAvail();
	const float panelW = (std::min)(m_PanelWidth, avail.x * 0.45f);
	m_ViewSize = ImVec2((std::max)(16.0f, avail.x - panelW), (std::max)(16.0f, avail.y));
	m_ViewPos = ImGui::GetCursorScreenPos();
	if (m_FirstFrame && m_ViewSize.x > 32)
	{
		m_FirstFrame = false;
		FrameAll();
	}

	// 뷰포트: 입력을 받는 보이지 않는 버튼 + 그림
	ImGui::InvisibleButton("##viewport", m_ViewSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
	const bool hovered = ImGui::IsItemHovered();
	const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
	if (hovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Middle) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)))
		ImGui::SetWindowFocus();

	RenderViewport();   // 고르기는 지난 그림의 깊이 · 면 버퍼를 쓴다
	if (m_Modal != Modal::None)
		HandleModal();
	else if (focused && (hovered || !ImGui::IsAnyItemActive()))
		HandleShortcuts();
	HandleViewportInput(hovered);
	UpdateHover();
	RenderViewport();

	ImDrawList* dl = ImGui::GetWindowDrawList();
	if (m_Srv)
		dl->AddImage((ImTextureID)m_Srv.Get(), m_ViewPos, ImVec2(m_ViewPos.x + m_ViewSize.x, m_ViewPos.y + m_ViewSize.y));
	DrawOverlay(dl);

	// 오른쪽 패널
	ImGui::SetCursorScreenPos(ImVec2(m_ViewPos.x + m_ViewSize.x, m_ViewPos.y));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
	ImGui::BeginChild("##modelPanel", ImVec2(panelW, m_ViewSize.y), true);
	DrawSidePanel();
	ImGui::EndChild();
	ImGui::PopStyleVar();
	(void)d;
}
