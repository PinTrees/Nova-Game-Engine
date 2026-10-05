#include "pch.h"
#include "VfxGraphWindow.h"
#include "VfxAssistantWindow.h"
#include "VfxCli.h"
#include "VisualEffect.h"
#include "VfxRuntime.h"
#include "ParticleSystemEditor.h"
#include "GameObjectFactory.h"
#include "EditorExtensions.h"
#include "EditorGUIManager.h"
#include "SelectionManager.h"
#include "UnityGUI.h"
#include "ImGui/imgui_internal.h"
#include "IconsFontAwesome/IconsFontAwesome6.h"
#include <filesystem>

namespace ed = ax::NodeEditor;
namespace fs = std::filesystem;

VfxGraphWindow* VfxGraphWindow::s_Instance = nullptr;

namespace
{
	constexpr float kNodeW = 300.0f;
	const char* kContextNames[4] = { "Spawn", "Initialize Particle", "Update Particle", "Output Particle Quad" };
	// Unity VFX Graph 의 문맥 색 (Spawn 주황 · Initialize 초록 · Update 노랑 · Output 보라)
	const ImU32 kContextColors[4] = { IM_COL32(214, 128, 52, 255), IM_COL32(64, 160, 92, 255), IM_COL32(196, 160, 48, 255), IM_COL32(128, 92, 196, 255) };
	const char* kBlendNames[] = { "Additive", "Alpha Blend" };
	const char* kShapeNames[] = { "Soft Dot", "Glow", "Star", "Sparkle", "Ring", "Spark", "Smoke", "Square", "Texture", "Heart" };
	const char* kOrientNames[] = { "Face Camera", "Along Velocity", "Horizontal" };
	const char* kPropTypes[] = { "Float", "Int", "Bool", "Vector3", "Color" };

	// 노드 · 핀 · 선 Id: 위 8 비트 = 종류, 나머지 = 값 (시스템 · 연산 노드 Id · 블록 값 자리)
	enum : uint64_t { kTagSystem = 1, kTagOp, kTagEventIn, kTagEventOut, kTagOpOut, kTagOpIn, kTagParam, kTagEventLink, kTagOpLink, kTagParamLink };
	uintptr_t Tagged(uint64_t tag, uint64_t v) { return (uintptr_t)((tag << 56) | (v & 0x00FFFFFFFFFFFFFFull)); }
	uint64_t TagOf(uintptr_t id) { return (uint64_t)id >> 56; }
	uint64_t ValOf(uintptr_t id) { return (uint64_t)id & 0x00FFFFFFFFFFFFFFull; }
	ed::NodeId NodeOf(int s) { return ed::NodeId(Tagged(kTagSystem, (uint64_t)s)); }
	ed::NodeId OpNodeOf(int id) { return ed::NodeId(Tagged(kTagOp, (uint64_t)id)); }
	ed::PinId EventIn(int s) { return ed::PinId(Tagged(kTagEventIn, (uint64_t)s)); }
	ed::PinId EventOut(int s) { return ed::PinId(Tagged(kTagEventOut, (uint64_t)s)); }
	ed::LinkId LinkOf(int child) { return ed::LinkId(Tagged(kTagEventLink, (uint64_t)child)); }
	ed::PinId OpOut(int id) { return ed::PinId(Tagged(kTagOpOut, (uint64_t)id)); }
	uint64_t OpInVal(int id, int input) { return ((uint64_t)id << 8) | (uint64_t)input; }
	ed::PinId OpIn(int id, int input) { return ed::PinId(Tagged(kTagOpIn, OpInVal(id, input))); }
	uint64_t ParamVal(int s, int ctx, int block, int param) { return ((uint64_t)s << 40) | ((uint64_t)ctx << 36) | ((uint64_t)block << 16) | (uint64_t)param; }
	ed::PinId ParamPin(int s, int ctx, int block, int param) { return ed::PinId(Tagged(kTagParam, ParamVal(s, ctx, block, param))); }
	struct ParamRef { int S, Ctx, Block, Param; };
	ParamRef DecodeParam(uint64_t v) { return { (int)(v >> 40), (int)((v >> 36) & 0xF), (int)((v >> 16) & 0xFFFFF), (int)(v & 0xFFFF) }; }
	int SystemOfId(uintptr_t id) { return (int)ValOf(id); }
	const ImU32 kOpColor = IM_COL32(70, 140, 220, 255);



	std::string Lower(std::string s)
	{
		for (char& c : s) c = (char)tolower((unsigned char)c);
		return s;
	}

	int LinkedTo(const Vfx::Block& b, const char* param)
	{
		for (auto it = b.Links.begin(); it != b.Links.end(); ++it)
			if (Lower(it.key()) == Lower(param) && it->is_number_integer())
				return it->get<int>();
		return -1;
	}

	std::string Stem(const std::string& path) { return fs::path(string_to_wstring(path)).stem().string(); }

	ImU32 ToU32(const float* c)
	{
		// HDR 색을 보기 좋게 (1 넘는 값은 줄인다)
		const float m = (std::max)({ c[0], c[1], c[2], 1.0f });
		return ImGui::ColorConvertFloat4ToU32(ImVec4(c[0] / m, c[1] / m, c[2] / m, std::clamp(c[3], 0.0f, 1.0f) * 0.7f + 0.3f));
	}

	void GradientBar(const std::vector<Vfx::GradientKey>& keys, ImVec2 size)
	{
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const ImVec2 p = ImGui::GetCursorScreenPos();
		const int steps = 24;
		for (int i = 0; i < steps; ++i)
		{
			const float t = (i + 0.5f) / steps;
			float c[4] = { 1, 1, 1, 1 };
			// 선형 보간 (Vfx 의 SampleGradient 와 같은 규칙)
			if (!keys.empty())
			{
				const Vfx::GradientKey* a = &keys.front();
				const Vfx::GradientKey* b = &keys.back();
				for (size_t k = 1; k < keys.size(); ++k)
					if (t <= keys[k].T) { a = &keys[k - 1]; b = &keys[k]; break; }
				const float f = std::clamp((t - a->T) / (std::max)(b->T - a->T, 1e-6f), 0.0f, 1.0f);
				for (int j = 0; j < 4; ++j) c[j] = a->C[j] + (b->C[j] - a->C[j]) * f;
			}
			dl->AddRectFilled(ImVec2(p.x + size.x * i / steps, p.y), ImVec2(p.x + size.x * (i + 1) / steps + 1, p.y + size.y), ToU32(c));
		}
		dl->AddRect(p, ImVec2(p.x + size.x, p.y + size.y), IM_COL32(0, 0, 0, 120));
		ImGui::Dummy(size);
	}

	void CurvePlot(const std::vector<Vfx::CurveKey>& keys, ImVec2 size)
	{
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const ImVec2 p = ImGui::GetCursorScreenPos();
		dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), IM_COL32(30, 30, 30, 255));
		float top = 1.0f;
		for (const auto& k : keys) top = (std::max)(top, k.V);
		ImVec2 prev;
		for (int i = 0; i <= 32; ++i)
		{
			const float t = i / 32.0f;
			float v = keys.empty() ? 1.0f : keys.front().V;
			for (size_t k = 1; k < keys.size(); ++k)
				if (t <= keys[k].T)
				{
					v = keys[k - 1].V + (keys[k].V - keys[k - 1].V) * std::clamp((t - keys[k - 1].T) / (std::max)(keys[k].T - keys[k - 1].T, 1e-6f), 0.0f, 1.0f);
					break;
				}
				else if (k + 1 == keys.size())
					v = keys.back().V;
			const ImVec2 q(p.x + size.x * t, p.y + size.y - size.y * std::clamp(v / top, 0.0f, 1.0f));
			if (i > 0) dl->AddLine(prev, q, IM_COL32(120, 220, 120, 255), 1.5f);
			prev = q;
		}
		ImGui::Dummy(size);
	}

	bool Compatible(const Vfx::ParamDesc& p, Vfx::PropertyType t)
	{
		switch (p.Kind)
		{
		case Vfx::ParamKind::Float: case Vfx::ParamKind::Int: case Vfx::ParamKind::Bool: case Vfx::ParamKind::Enum:
			return t == Vfx::PropertyType::Float || t == Vfx::PropertyType::Int || t == Vfx::PropertyType::Bool;
		case Vfx::ParamKind::Vector3: return t == Vfx::PropertyType::Vector3 || t == Vfx::PropertyType::Color;
		case Vfx::ParamKind::Color: return t == Vfx::PropertyType::Color || t == Vfx::PropertyType::Vector3;
		default: return false;
		}
	}

	std::string BoundTo(const Vfx::Block& b, const char* param)
	{
		for (auto it = b.Bind.begin(); it != b.Bind.end(); ++it)
			if (Lower(it.key()) == Lower(param) && it->is_string())
				return it->get<std::string>();
		return std::string();
	}

	void SetBind(Vfx::Block& b, const char* param, const std::string& prop)
	{
		for (auto it = b.Bind.begin(); it != b.Bind.end(); ++it)
			if (Lower(it.key()) == Lower(param)) { b.Bind.erase(it); break; }
		if (!prop.empty()) b.Bind[param] = prop;
	}

	// 블록 한 줄 요약 (노드 안)
	std::string Summary(const Vfx::Block& b, const Vfx::BlockDesc& d)
	{
		char buf[128] = {};
		auto f = [&](const char* n) { return b.GetFloat(d, n); };
		switch (d.Id)
		{
		case 1: { const auto o = Vfx::EnumOptions(d.Params[0]); const int i = std::clamp((int)f("Shape"), 0, (int)o.size() - 1); snprintf(buf, sizeof(buf), "%s  r %.2g", o[i].c_str(), f("Radius")); break; }
		case 2: snprintf(buf, sizeof(buf), "%.3g .. %.3g m/s", f("MinSpeed"), f("MaxSpeed")); break;
		case 3: snprintf(buf, sizeof(buf), "%.3g .. %.3g s", f("Min"), f("Max")); break;
		case 4: snprintf(buf, sizeof(buf), "%.3g .. %.3g m", f("Min"), f("Max")); break;
		case 21: snprintf(buf, sizeof(buf), "%.3g", f("Coefficient")); break;
		case 22: snprintf(buf, sizeof(buf), "intensity %.3g  freq %.3g", f("Intensity"), f("Frequency")); break;
		case 23: snprintf(buf, sizeof(buf), "speed %.3g  pull %.3g", f("Speed"), f("Pull")); break;
		case 24: snprintf(buf, sizeof(buf), "strength %.3g", f("Strength")); break;
		case 28: snprintf(buf, sizeof(buf), "%.3g m/s", f("Max")); break;
		case 29: snprintf(buf, sizeof(buf), "%.3g deg/s", f("Speed")); break;
		default: break;
		}
		return buf;
	}

	void PlaceInScene(const std::string& path)
	{
		Scene* scene = SceneManager::GetI()->GetCurrentScene();
		if (!scene) return;
		GameObject* go = GameObjectFactory::CreateVisualEffect(path);
		go->SetName(Stem(path));
		scene->AddRootGameObject(go);
		SelectionManager::SetSelectedGameObject(go);
	}
}

VfxGraphWindow::VfxGraphWindow()
	: EditorWindow("Visual Effect Graph", ICON_FA_WAND_MAGIC_SPARKLES)
{
	s_Instance = this;
	SetIsOpened(false);
}

VfxGraphWindow::~VfxGraphWindow()
{
	if (m_Ctx) ed::DestroyEditor(m_Ctx);
	if (s_Instance == this) s_Instance = nullptr;
}

void VfxGraphWindow::BeforeBegin()
{
	if (m_FocusPending)
	{
		ImGui::SetNextWindowFocus();
		m_FocusPending = false;
	}
	ImGui::SetNextWindowSize(ImVec2(1400.0f, 820.0f), ImGuiCond_FirstUseEver);
	if (EditorWindow* scene = EditorGUIManager::GetI()->FindWindow("Scene"))
		if (ImGuiWindow* w = ImGui::FindWindowByName(scene->GetImGuiName().c_str()); w && w->DockId)
			ImGui::SetNextWindowDockID(w->DockId, ImGuiCond_FirstUseEver);
}

void VfxGraphWindow::Open(const std::string& assetPath)
{
	if (!s_Instance) return;
	if (!assetPath.empty() && Lower(assetPath) != Lower(s_Instance->m_Path))
		s_Instance->Load(assetPath);
	s_Instance->SetIsOpened(true);
	s_Instance->m_FocusPending = true;
}

void VfxGraphWindow::Select(int system, int context, int block)
{
	if (!s_Instance) return;
	s_Instance->m_SelSystem = system;
	s_Instance->m_SelContext = context;
	s_Instance->m_SelBlock = block;
	s_Instance->m_SelProperty = -1;
}

// ------------------------------------------------------------------ 문서
void VfxGraphWindow::Load(const std::string& path)
{
	if (m_Dirty && !m_Path.empty())
		Save();   // 다른 에셋을 열면 지금 것을 저장 (Unity 는 묻는다 — 잃지 않게 저장)
	const Vfx::Loaded l = Vfx::Load(path);
	if (!l.Data)
	{
		m_Status = l.Error.empty() ? "cannot open " + path : l.Error;
		m_StatusError = true;
		return;
	}
	m_Path = path;
	m_Asset = *l.Data;
	m_SeenRevision = l.Revision;
	m_Dirty = false;
	m_Undo.clear();
	m_Redo.clear();
	m_SelSystem = m_SelContext = m_SelBlock = m_SelProperty = -1;
	++m_LayoutRevision;
	m_NavigatePending = true;
	m_Status = "opened " + path;
	m_StatusError = false;
}

void VfxGraphWindow::Save()
{
	if (m_Path.empty()) return;
	std::string error;
	if (Vfx::Save(m_Path, m_Asset, error))
	{
		m_Dirty = false;
		m_SeenRevision = Vfx::Load(m_Path).Revision;
		m_Status = "saved " + m_Path;
		m_StatusError = false;
	}
	else
	{
		m_Status = error;
		m_StatusError = true;
	}
}

void VfxGraphWindow::Snapshot()
{
	m_Undo.push_back(m_Asset);
	if (m_Undo.size() > 100) m_Undo.erase(m_Undo.begin());
	m_Redo.clear();
}

void VfxGraphWindow::BeginEdit()
{
	if (!m_EditActive)
	{
		Snapshot();
		m_EditActive = true;
	}
}

void VfxGraphWindow::EndEditIfIdle()
{
	if (m_EditActive && !ImGui::IsAnyItemActive())
		m_EditActive = false;
}

void VfxGraphWindow::Changed(bool layout)
{
	m_Dirty = true;
	if (layout) ++m_LayoutRevision;
	// 장면의 Visual Effect 가 저장 전에도 바로 따라온다
	Vfx::SetLive(m_Path, std::make_shared<Vfx::Asset>(m_Asset));
	m_SeenRevision = Vfx::Load(m_Path).Revision;
}

void VfxGraphWindow::Undo()
{
	if (m_Undo.empty()) return;
	m_Redo.push_back(m_Asset);
	m_Asset = m_Undo.back();
	m_Undo.pop_back();
	Changed(true);
}

void VfxGraphWindow::Redo()
{
	if (m_Redo.empty()) return;
	m_Undo.push_back(m_Asset);
	m_Asset = m_Redo.back();
	m_Redo.pop_back();
	Changed(true);
}

void VfxGraphWindow::AddSystem(const Vfx::System& src, ImVec2 canvasPos)
{
	Snapshot();
	Vfx::System s = src;
	const std::string base = s.Name;
	for (int n = 2; m_Asset.FindSystem(s.Name) >= 0; ++n)
		s.Name = base + " " + std::to_string(n);
	s.SpawnCtx.Parent.clear();
	s.Editor["x"] = canvasPos.x;
	s.Editor["y"] = canvasPos.y;
	m_Asset.Systems.push_back(s);
	m_SelSystem = (int)m_Asset.Systems.size() - 1;
	m_SelContext = 0;
	m_SelBlock = -1;
	Changed(true);
}

// ------------------------------------------------------------------ 그리기
void VfxGraphWindow::OnRender()
{
	// 파일이 밖에서 바뀌었다 (nova vfx · VFX Assistant · 다른 창) → 다시 읽는다
	if (!m_Path.empty())
	{
		const Vfx::Loaded l = Vfx::Load(m_Path);
		if (l.Revision != m_SeenRevision && l.Data)
		{
			m_Asset = *l.Data;
			m_SeenRevision = l.Revision;
			m_Dirty = false;
			++m_LayoutRevision;
			if (m_SelSystem >= (int)m_Asset.Systems.size()) m_SelSystem = m_SelContext = m_SelBlock = -1;
			if (m_SelProperty >= (int)m_Asset.Properties.size()) m_SelProperty = -1;
		}
	}

	// 단축키 (창이 포커스일 때, 글 입력 중이 아닐 때)
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput)
	{
		const bool ctrl = ImGui::GetIO().KeyCtrl;
		if (ctrl && ImGui::IsKeyPressed(ImGuiKey_S)) Save();
		if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z)) Undo();
		if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Y)) Redo();
	}

	DrawMenuBar();
	if (m_Path.empty())
	{
		ImGui::Dummy(ImVec2(0, 40));
		ImGui::Indent(40);
		ImGui::PushFont(UnityGUI::BoldFont());
		ImGui::TextUnformatted("Visual Effect Graph");
		ImGui::PopFont();
		ImGui::TextDisabled("Open a .vfx asset (double-click in the Project window), or create one from a template:");
		ImGui::Dummy(ImVec2(0, 8));
		for (const std::string& t : Vfx::TemplateNames())
		{
			if (ImGui::Button(t.c_str(), ImVec2(160, 0)))
			{
				Vfx::Asset a;
				Vfx::MakeTemplate(t, a);
				std::string path = "Assets\\VFX\\" + t + ".vfx";
				std::error_code ec;
				for (int n = 2; fs::exists(Vfx::FullPath(path), ec); ++n)
					path = "Assets\\VFX\\" + t + " " + std::to_string(n) + ".vfx";
				std::string error;
				if (Vfx::Save(path, a, error))
					Load(path);
				else
				{
					m_Status = error;
					m_StatusError = true;
				}
			}
			ImGui::SameLine();
			if (ImGui::GetContentRegionAvail().x < 170) ImGui::NewLine();
		}
		ImGui::Unindent(40);
		return;
	}

	const ImVec2 avail = ImGui::GetContentRegionAvail();
	const float statusH = ImGui::GetFrameHeight();
	// 좁은 창이면 양옆 판을 줄인다 (캔버스가 너무 작아지지 않게)
	const float leftW = avail.x < 1300.0f ? 190.0f : 250.0f, rightW = avail.x < 1300.0f ? 280.0f : 330.0f;
	const float h = (std::max)(100.0f, avail.y - statusH);
	DrawBlackboard(leftW, h);
	ImGui::SameLine(0, 0);
	DrawCanvas((std::max)(200.0f, avail.x - leftW - rightW), h);
	ImGui::SameLine(0, 0);
	DrawInspector(rightW, h);

	// 상태 줄
	int alive = 0, effects = 0;
	for (VisualEffect* v : VisualEffect::All())
		if (Lower(v->AssetPath) == Lower(m_Path)) { alive += v->AliveParticleCount(); ++effects; }
	const auto issues = m_Asset.Validate();
	ImGui::SetCursorPosX(8);
	if (!VfxRuntime::LastError().empty())
		ImGui::TextColored(ImVec4(1, 0.4f, 0.3f, 1), "%s", VfxRuntime::LastError().c_str());
	else if (!issues.empty())
		ImGui::TextColored(ImVec4(1, 0.75f, 0.3f, 1), "%s %s", ICON_FA_TRIANGLE_EXCLAMATION, issues.front().c_str());
	else if (!m_Status.empty())
		ImGui::TextColored(m_StatusError ? ImVec4(1, 0.4f, 0.3f, 1) : ImVec4(0.6f, 0.6f, 0.6f, 1), "%s", m_Status.c_str());
	ImGui::SameLine((std::max)(400.0f, avail.x - 330.0f));
	ImGui::TextDisabled("%d in scene  |  %d particles alive", effects, alive);
	EndEditIfIdle();
}

void VfxGraphWindow::DrawMenuBar()
{
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(14.0f, 4.0f));
	if (!ImGui::BeginMenuBar())
	{
		ImGui::PopStyleVar();
		return;
	}
	if (ImGui::BeginMenu("File"))
	{
		if (ImGui::BeginMenu("New from Template"))
		{
			for (const std::string& t : Vfx::TemplateNames())
				if (ImGui::MenuItem(t.c_str()))
				{
					Vfx::Asset a;
					Vfx::MakeTemplate(t, a);
					std::string path = "Assets\\VFX\\" + t + ".vfx";
					std::error_code ec;
					for (int n = 2; fs::exists(Vfx::FullPath(path), ec); ++n)
						path = "Assets\\VFX\\" + t + " " + std::to_string(n) + ".vfx";
					std::string error;
					if (Vfx::Save(path, a, error)) Load(path);
				}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Open"))
		{
			for (const std::string& p : Vfx::FindAssets())
				if (ImGui::MenuItem(p.c_str())) Load(p);
			ImGui::EndMenu();
		}
		if (ImGui::MenuItem("Save", "Ctrl+S", false, !m_Path.empty())) Save();
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("Edit"))
	{
		if (ImGui::MenuItem("Undo", "Ctrl+Z", false, !m_Undo.empty())) Undo();
		if (ImGui::MenuItem("Redo", "Ctrl+Y", false, !m_Redo.empty())) Redo();
		ImGui::EndMenu();
	}
	if (!m_Path.empty())
	{
		ImGui::Separator();
		ImGui::TextUnformatted((Stem(m_Path) + (m_Dirty ? " *" : "")).c_str());
		ImGui::Separator();
		// 장면의 이 에셋을 쓰는 Visual Effect 제어 (Unity 의 Play Controls)
		auto each = [&](auto fn) {
			for (VisualEffect* v : VisualEffect::All())
				if (Lower(v->AssetPath) == Lower(m_Path)) fn(v);
		};
		if (ImGui::MenuItem(ICON_FA_PLAY " Play")) each([](VisualEffect* v) { v->Play(); });
		if (ImGui::MenuItem(ICON_FA_STOP " Stop")) each([](VisualEffect* v) { v->Stop(); });
		if (ImGui::MenuItem(ICON_FA_ROTATE_RIGHT " Restart")) each([](VisualEffect* v) { v->Reinit(); });
		if (ImGui::MenuItem("Place in Scene")) PlaceInScene(m_Path);
		ImGui::Separator();
		if (ImGui::MenuItem(ICON_FA_COMMENTS " VFX Assistant"))
			VfxAssistantWindow::Open(m_Path);
	}
	ImGui::EndMenuBar();
	ImGui::PopStyleVar();
}

void VfxGraphWindow::DrawBlackboard(float width, float height)
{
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 6.0f));
	ImGui::BeginChild("##blackboard", ImVec2(width, height), true);
	ImGui::PopStyleVar();
	ImGui::PushFont(UnityGUI::BoldFont());
	ImGui::TextUnformatted("Blackboard");
	ImGui::PopFont();
	ImGui::SameLine(width - 34.0f);
	if (ImGui::SmallButton(ICON_FA_PLUS "##addprop")) ImGui::OpenPopup("##addprop");
	if (ImGui::BeginPopup("##addprop"))
	{
		for (int t = 0; t < 5; ++t)
			if (ImGui::MenuItem(kPropTypes[t]))
			{
				Snapshot();
				Vfx::Property p;
				p.Type = (Vfx::PropertyType)t;
				p.Name = std::string("New ") + kPropTypes[t];
				for (int n = 2; m_Asset.FindProperty(p.Name); ++n) p.Name = std::string("New ") + kPropTypes[t] + " " + std::to_string(n);
				if (p.Type == Vfx::PropertyType::Color) p.Value = { 1, 1, 1, 1 };
				m_Asset.Properties.push_back(p);
				m_SelProperty = (int)m_Asset.Properties.size() - 1;
				m_SelSystem = -1;
				Changed();
			}
		ImGui::EndPopup();
	}
	ImGui::TextDisabled("Exposed properties (Inspector override)");
	ImGui::Separator();
	for (int i = 0; i < (int)m_Asset.Properties.size(); ++i)
	{
		const Vfx::Property& p = m_Asset.Properties[i];
		ImGui::PushID(i);
		if (ImGui::Selectable(p.Name.c_str(), m_SelProperty == i && m_SelSystem < 0))
		{
			m_SelProperty = i;
			m_SelSystem = m_SelContext = m_SelBlock = -1;
		}
		ImGui::SameLine(width - 70.0f);
		ImGui::TextDisabled("%s", kPropTypes[(int)p.Type]);
		ImGui::PopID();
	}
	ImGui::Dummy(ImVec2(0, 12));
	ImGui::PushFont(UnityGUI::BoldFont());
	ImGui::TextUnformatted("Systems");
	ImGui::PopFont();
	ImGui::Separator();
	for (int s = 0; s < (int)m_Asset.Systems.size(); ++s)
	{
		const Vfx::System& sys = m_Asset.Systems[s];
		ImGui::PushID(1000 + s);
		const std::string label = sys.Name + (sys.SpawnCtx.Parent.empty() ? "" : "  <- " + sys.SpawnCtx.Parent);
		if (ImGui::Selectable(label.c_str(), m_SelSystem == s && m_SelContext < 0))
		{
			m_SelSystem = s;
			m_SelContext = m_SelBlock = -1;
			m_SelProperty = -1;
			if (m_Ctx)
			{
				ed::SetCurrentEditor(m_Ctx);
				ed::SelectNode(NodeOf(s));
				ed::NavigateToSelection(false, 0.3f);
				ed::SetCurrentEditor(nullptr);
			}
		}
		ImGui::PopID();
	}
	ImGui::EndChild();
}

// 노드 안의 짧은 값 편집 (콤보 · 색 고르기 창은 노드 편집기 안에서 열 수 없어 Inspector 로)
bool VfxGraphWindow::DrawBlockFields(Vfx::Block& b, const Vfx::BlockDesc& d, bool compact, int sys, int ctx, int block, bool allowBind)
{
	bool changed = false;
	const float labelW = compact ? 92.0f : 120.0f;
	const float fieldW = compact ? kNodeW - labelW - 26.0f : ImGui::GetContentRegionAvail().x - labelW - 34.0f;
	for (const Vfx::ParamDesc& p : d.Params)
	{
		// 쓰지 않는 값은 숨긴다 (모양 · 모드에 따라)
		if (d.Id == 1)
		{
			const int shape = (int)b.GetFloat(d, "Shape");
			const std::string n = p.Name;
			if ((n == "Size" && shape != 3) || (n == "Radius" && (shape == 0 || shape == 3 || shape == 5)) || (n == "Height" && shape != 4 && shape != 5) ||
				(n == "ConeAngle" && shape != 4) || (n == "Thickness" && shape != 6) || (n == "Arc" && shape != 2 && shape != 4 && shape != 6) ||
				(n == "Surface" && (shape == 0 || shape == 5)))
				continue;
		}
		if (d.Id == 2)
		{
			const int mode = (int)b.GetFloat(d, "Mode");
			if ((p.Name == std::string("Spread") && mode != 3) || (p.Name == std::string("Direction") && mode != 0 && mode != 3)) continue;
		}
		if (d.Id == 5)
		{
			const int mode = (int)b.GetFloat(d, "Mode");
			const std::string n = p.Name;
			if ((n == "ColorB" && mode != 1) || ((n == "Saturation" || n == "Brightness") && mode != 2)) continue;
		}
		ImGui::PushID(p.Name);
		const int linked = LinkedTo(b, p.Name);
		// 노드 안: 연산 노드를 이을 핀 (이을 수 있는 값만)
		if (compact && sys >= 0 && Vfx::Linkable(p))
		{
			ed::BeginPin(ParamPin(sys, ctx, block, (int)(&p - d.Params.data())), ed::PinKind::Input);
			ed::PinPivotAlignment(ImVec2(0.0f, 0.5f));
			ImGui::TextColored(linked >= 0 ? ImVec4(0.45f, 0.75f, 1.0f, 1.0f) : ImVec4(0.45f, 0.45f, 0.45f, 1.0f), linked >= 0 ? ICON_FA_CIRCLE : ICON_FA_CIRCLE_NOTCH);
			ed::EndPin();
			ImGui::SameLine(0, 4);
		}
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(p.Name);
		if (!compact && p.Tip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", p.Tip);
		ImGui::SameLine(labelW + (compact && sys >= 0 ? 14.0f : 0.0f));
		ImGui::SetNextItemWidth(fieldW - (compact && sys >= 0 ? 14.0f : 0.0f));
		const std::string bound = BoundTo(b, p.Name);
		std::array<float, 4> v = b.GetVector(d, p.Name);
		if (linked >= 0)
		{
			// 연산 노드에 이은 값 (파티클마다 계산)
			const Vfx::OperatorNode* on = m_Asset.FindOperatorNode(linked);
			const Vfx::OperatorDesc* od = on ? Vfx::FindOperator(on->Type) : nullptr;
			ImGui::TextColored(ImVec4(0.45f, 0.75f, 1.0f, 1.0f), "<- %s #%d", od ? od->Label : "?", linked);
			if (!compact)
			{
				ImGui::SameLine();
				if (ImGui::SmallButton(ICON_FA_XMARK "##unlink"))
				{
					Snapshot();
					for (auto it = b.Links.begin(); it != b.Links.end(); ++it)
						if (Lower(it.key()) == Lower(p.Name)) { b.Links.erase(it); break; }
					changed = true;
				}
			}
		}
		else if (!bound.empty())
		{
			ImGui::TextColored(ImVec4(0.55f, 0.8f, 1.0f, 1.0f), ICON_FA_LINK " %s", bound.c_str());
		}
		else
		{
			const float speed = (p.Max > p.Min) ? (p.Max - p.Min) / 300.0f : 0.02f;
			switch (p.Kind)
			{
			case Vfx::ParamKind::Float:
				if (ImGui::DragFloat("##v", &v[0], speed, 0.0f, 0.0f, "%.3g")) { BeginEdit(); b.Params[p.Name] = v[0]; changed = true; }
				break;
			case Vfx::ParamKind::Text:
			{
				// 속성 이름 (연산 노드 Property): Blackboard 에서 고른다
				std::string cur;
				if (b.Params.contains(p.Name) && b.Params[p.Name].is_string()) cur = b.Params[p.Name].get<std::string>();
				if (ImGui::BeginCombo("##t", cur.empty() ? "(property)" : cur.c_str()))
				{
					for (const Vfx::Property& prop : m_Asset.Properties)
						if (ImGui::Selectable(prop.Name.c_str(), prop.Name == cur)) { Snapshot(); b.Params[p.Name] = prop.Name; changed = true; }
					ImGui::EndCombo();
				}
				break;
			}
			case Vfx::ParamKind::Int:
			{
				int i = (int)v[0];
				if (ImGui::DragInt("##v", &i, 0.1f, (int)p.Min, (int)p.Max)) { BeginEdit(); b.Params[p.Name] = i; changed = true; }
				break;
			}
			case Vfx::ParamKind::Bool:
			{
				bool on = v[0] > 0.5f;
				if (ImGui::Checkbox("##v", &on)) { Snapshot(); b.Params[p.Name] = on; changed = true; }
				break;
			}
			case Vfx::ParamKind::Enum:
			{
				const auto opts = Vfx::EnumOptions(p);
				int i = std::clamp((int)v[0], 0, (int)opts.size() - 1);
				if (compact)
				{
					// 노드 안: 눌러서 다음 값
					if (ImGui::Button((opts[i] + "##e").c_str(), ImVec2(fieldW, 0))) { Snapshot(); b.Params[p.Name] = opts[(i + 1) % opts.size()]; changed = true; }
				}
				else if (ImGui::BeginCombo("##v", opts[i].c_str()))
				{
					for (int k = 0; k < (int)opts.size(); ++k)
						if (ImGui::Selectable(opts[k].c_str(), k == i)) { Snapshot(); b.Params[p.Name] = opts[k]; changed = true; }
					ImGui::EndCombo();
				}
				break;
			}
			case Vfx::ParamKind::Vector3:
				if (ImGui::DragFloat3("##v", v.data(), speed, 0.0f, 0.0f, "%.2f")) { BeginEdit(); b.Params[p.Name] = { v[0], v[1], v[2] }; changed = true; }
				break;
			case Vfx::ParamKind::Color:
				if (compact)
				{
					ImGui::ColorButton("##c", ImVec4(v[0], v[1], v[2], v[3]), ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_AlphaPreviewHalf, ImVec2(fieldW, 0));
				}
				else if (ImGui::ColorEdit4("##v", v.data(), ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_AlphaPreviewHalf))
				{
					BeginEdit();
					b.Params[p.Name] = { v[0], v[1], v[2], v[3] };
					changed = true;
				}
				break;
			case Vfx::ParamKind::Curve:
			{
				auto keys = b.GetCurve(d, p.Name);
				CurvePlot(keys, ImVec2(fieldW, compact ? 28.0f : 70.0f));
				if (!compact)
				{
					for (size_t k = 0; k < keys.size(); ++k)
					{
						ImGui::PushID((int)k);
						ImGui::SetNextItemWidth(70);
						bool edit = ImGui::DragFloat("##t", &keys[k].T, 0.005f, 0.0f, 1.0f, "t %.2f");
						ImGui::SameLine();
						ImGui::SetNextItemWidth(90);
						edit |= ImGui::DragFloat("##val", &keys[k].V, 0.01f, 0.0f, 0.0f, "%.2f");
						ImGui::SameLine();
						const bool del = keys.size() > 1 && ImGui::SmallButton(ICON_FA_XMARK);
						if (edit || del)
						{
							if (del) { Snapshot(); keys.erase(keys.begin() + k); } else BeginEdit();
							json arr = json::array();
							for (const auto& key : keys) arr.push_back({ key.T, key.V });
							b.Params[p.Name] = arr;
							changed = true;
							ImGui::PopID();
							break;
						}
						ImGui::PopID();
					}
					if (ImGui::SmallButton("+ key"))
					{
						Snapshot();
						keys.push_back({ 1.0f, keys.empty() ? 1.0f : keys.back().V });
						json arr = json::array();
						for (const auto& key : keys) arr.push_back({ key.T, key.V });
						b.Params[p.Name] = arr;
						changed = true;
					}
				}
				break;
			}
			case Vfx::ParamKind::Gradient:
			{
				auto keys = b.GetGradient(d, p.Name);
				GradientBar(keys, ImVec2(fieldW, compact ? 14.0f : 22.0f));
				if (!compact)
				{
					for (size_t k = 0; k < keys.size(); ++k)
					{
						ImGui::PushID((int)k);
						ImGui::SetNextItemWidth(60);
						bool edit = ImGui::DragFloat("##t", &keys[k].T, 0.005f, 0.0f, 1.0f, "%.2f");
						ImGui::SameLine();
						ImGui::SetNextItemWidth(fieldW - 90);
						edit |= ImGui::ColorEdit4("##c", keys[k].C, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_AlphaPreviewHalf | ImGuiColorEditFlags_NoInputs);
						ImGui::SameLine();
						ImGui::SetNextItemWidth(60);
						edit |= ImGui::DragFloat("##a", &keys[k].C[3], 0.01f, 0.0f, 1.0f, "a %.2f");
						ImGui::SameLine();
						const bool del = keys.size() > 1 && ImGui::SmallButton(ICON_FA_XMARK);
						if (edit || del)
						{
							if (del) { Snapshot(); keys.erase(keys.begin() + k); } else BeginEdit();
							json arr = json::array();
							for (const auto& key : keys) arr.push_back({ key.T, key.C[0], key.C[1], key.C[2], key.C[3] });
							b.Params[p.Name] = arr;
							changed = true;
							ImGui::PopID();
							break;
						}
						ImGui::PopID();
					}
					if (ImGui::SmallButton("+ key"))
					{
						Snapshot();
						Vfx::GradientKey last = keys.empty() ? Vfx::GradientKey{ 1.0f, { 1, 1, 1, 1 } } : keys.back();
						last.T = 1.0f;
						keys.push_back(last);
						json arr = json::array();
						for (const auto& key : keys) arr.push_back({ key.T, key.C[0], key.C[1], key.C[2], key.C[3] });
						b.Params[p.Name] = arr;
						changed = true;
					}
				}
				break;
			}
			}
		}
		// 속성 연결 (Inspector)
		if (!compact && allowBind && linked < 0 && p.Kind != Vfx::ParamKind::Curve && p.Kind != Vfx::ParamKind::Gradient && p.Kind != Vfx::ParamKind::Text)
		{
			ImGui::SameLine();
			const bool any = std::any_of(m_Asset.Properties.begin(), m_Asset.Properties.end(), [&](const Vfx::Property& prop) { return Compatible(p, prop.Type); });
			ImGui::BeginDisabled(!any && bound.empty());
			if (ImGui::SmallButton(ICON_FA_LINK)) ImGui::OpenPopup("##bind");
			ImGui::EndDisabled();
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("Bind to an exposed property");
			if (ImGui::BeginPopup("##bind"))
			{
				if (ImGui::MenuItem("(none)", nullptr, bound.empty())) { Snapshot(); SetBind(b, p.Name, ""); changed = true; }
				for (const Vfx::Property& prop : m_Asset.Properties)
					if (Compatible(p, prop.Type) && ImGui::MenuItem(prop.Name.c_str(), nullptr, prop.Name == bound)) { Snapshot(); SetBind(b, p.Name, prop.Name); changed = true; }
				ImGui::EndPopup();
			}
		}
		ImGui::PopID();
	}
	return changed;
}

void VfxGraphWindow::DrawSystem(int s)
{
	Vfx::System& sys = m_Asset.Systems[s];
	ed::PushStyleColor(ed::StyleColor_NodeBorder, ImColor(m_SelSystem == s ? IM_COL32(255, 255, 255, 255) : IM_COL32(90, 90, 90, 255)));
	ed::BeginNode(NodeOf(s));
	ImGui::PushID(s);
	ImGui::PushItemWidth(kNodeW - 120.0f);

	// 머리: 이름 · 켜짐 · 용량 · 공간
	bool enabled = sys.Enabled;
	if (ImGui::Checkbox("##en", &enabled)) { Snapshot(); sys.Enabled = enabled; Changed(); }
	ImGui::SameLine();
	ImGui::PushFont(UnityGUI::BoldFont());
	ImGui::TextUnformatted(sys.Name.c_str());
	ImGui::PopFont();
	ImGui::SameLine(kNodeW - 28.0f);
	if (ImGui::SmallButton(ICON_FA_XMARK))
	{
		Snapshot();
		const std::string name = sys.Name;
		m_Asset.Systems.erase(m_Asset.Systems.begin() + s);
		for (Vfx::System& other : m_Asset.Systems)
			if (other.SpawnCtx.Parent == name) other.SpawnCtx.Parent.clear();
		m_SelSystem = m_SelContext = m_SelBlock = -1;
		Changed(true);
		ImGui::PopItemWidth();
		ImGui::PopID();
		ed::EndNode();
		ed::PopStyleColor();
		return;
	}
	ImGui::TextDisabled("capacity %d  |  %s  |  alive %d", sys.Capacity, sys.Local ? "Local" : "World", [&] {
		int alive = 0;
		for (VisualEffect* v : VisualEffect::All())
			if (Lower(v->AssetPath) == Lower(m_Path)) alive += v->SystemAliveCount(s);
		return alive;
	}());

	auto header = [&](int ctx, const char* extra) {
		ImGui::Dummy(ImVec2(0, 2));
		const ImVec2 p = ImGui::GetCursorScreenPos();
		const bool sel = m_SelSystem == s && m_SelContext == ctx && m_SelBlock < 0;
		ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + kNodeW, p.y + ImGui::GetFrameHeight()), kContextColors[ctx], 3.0f);
		if (sel) ImGui::GetWindowDrawList()->AddRect(p, ImVec2(p.x + kNodeW, p.y + ImGui::GetFrameHeight()), IM_COL32(255, 255, 255, 255), 3.0f, 0, 1.5f);
		ImGui::SetCursorScreenPos(ImVec2(p.x + 6, p.y + ImGui::GetStyle().FramePadding.y));
		ImGui::PushFont(UnityGUI::BoldFont());
		ImGui::TextUnformatted(kContextNames[ctx]);
		ImGui::PopFont();
		if (extra && *extra)
		{
			ImGui::SameLine();
			ImGui::TextUnformatted(extra);
		}
		ImGui::SetCursorScreenPos(p);
		if (ImGui::InvisibleButton("##ctx", ImVec2(kNodeW - ((ctx == 1 || ctx == 2) ? 26.0f : 0.0f), ImGui::GetFrameHeight())))
		{
			m_SelSystem = s;
			m_SelContext = ctx;
			m_SelBlock = -1;
			m_SelProperty = -1;
		}
		if (ctx == 1 || ctx == 2)
		{
			ImGui::SameLine(0, 0);
			if (ImGui::Button(ICON_FA_PLUS "##add", ImVec2(26.0f, ImGui::GetFrameHeight())))
			{
				m_OpenSearch = true;
				m_SearchSystem = s;
				m_SearchContext = ctx;
				m_SearchPos = ImGui::GetMousePos();
			}
		}
	};

	// Spawn (GPU Event 를 받는 핀)
	ImGui::PushID("spawn");
	const bool child = !sys.SpawnCtx.Parent.empty();
	header(0, child ? "(GPU Event)" : "");
	ed::BeginPin(EventIn(s), ed::PinKind::Input);
	ed::PinPivotAlignment(ImVec2(0.0f, 0.5f));
	ImGui::TextColored(child ? ImVec4(1.0f, 0.7f, 0.35f, 1.0f) : ImVec4(0.5f, 0.5f, 0.5f, 1.0f), child ? ICON_FA_CIRCLE " GPU Event" : ICON_FA_CIRCLE_NOTCH " GPU Event");
	ed::EndPin();
	if (child)
	{
		ImGui::SameLine();
		ImGui::TextDisabled("%s x %d", sys.SpawnCtx.Parent.c_str(), sys.SpawnCtx.CountPerEvent);
	}
	else
	{
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted("Rate");
		ImGui::SameLine(92.0f);
		ImGui::SetNextItemWidth(kNodeW - 118.0f);
		if (!sys.SpawnCtx.RateBind.empty())
			ImGui::TextColored(ImVec4(0.55f, 0.8f, 1.0f, 1.0f), ICON_FA_LINK " %s", sys.SpawnCtx.RateBind.c_str());
		else if (ImGui::DragFloat("##rate", &sys.SpawnCtx.Rate, 1.0f, 0.0f, 1e6f, "%.1f /s")) { BeginEdit(); Changed(); }
		if (!sys.SpawnCtx.Bursts.empty())
			ImGui::TextDisabled("%d burst(s)%s", (int)sys.SpawnCtx.Bursts.size(), sys.SpawnCtx.Loop ? "" : ", once");
	}
	ImGui::PopID();

	// Initialize · Update 블록
	for (int ctx = 1; ctx <= 2; ++ctx)
	{
		ImGui::PushID(ctx);
		std::vector<Vfx::Block>& list = ctx == 1 ? sys.Initialize : sys.Update;
		header(ctx, nullptr);
		int removeAt = -1;
		for (int i = 0; i < (int)list.size(); ++i)
		{
			Vfx::Block& b = list[i];
			const Vfx::BlockDesc* d = Vfx::FindBlock(b.Type);
			ImGui::PushID(i);
			bool on = b.Enabled;
			if (ImGui::Checkbox("##on", &on)) { Snapshot(); b.Enabled = on; Changed(); }
			ImGui::SameLine();
			const bool sel = m_SelSystem == s && m_SelContext == ctx && m_SelBlock == i;
			const std::string label = d ? d->Label : ("? " + b.Type);
			if (ImGui::Selectable(label.c_str(), sel, 0, ImVec2(kNodeW - 60.0f, 0)))
			{
				m_SelSystem = s;
				m_SelContext = ctx;
				m_SelBlock = i;
				m_SelProperty = -1;
			}
			ImGui::SameLine(kNodeW - 22.0f);
			if (ImGui::SmallButton(ICON_FA_XMARK)) removeAt = i;
			if (d)
			{
				ImGui::BeginDisabled(!b.Enabled);
				ImGui::Indent(22.0f);
				if (sel)
				{
					if (DrawBlockFields(b, *d, true, s, ctx, i)) Changed();
				}
				else
				{
					// 연산 노드에 이은 값은 고르지 않아도 핀과 함께 (선이 이어진다)
					for (int pi = 0; pi < (int)d->Params.size(); ++pi)
					{
						const int linked = LinkedTo(b, d->Params[pi].Name);
						if (linked < 0)
							continue;
						ed::BeginPin(ParamPin(s, ctx, i, pi), ed::PinKind::Input);
						ed::PinPivotAlignment(ImVec2(0.0f, 0.5f));
						ImGui::TextColored(ImVec4(0.45f, 0.75f, 1.0f, 1.0f), ICON_FA_CIRCLE " %s", d->Params[pi].Name);
						ed::EndPin();
					}
					const std::string sum = Summary(b, *d);
					if (!sum.empty()) ImGui::TextDisabled("%s", sum.c_str());
					else if (d->Id == 26) GradientBar(b.GetGradient(*d, "Gradient"), ImVec2(kNodeW - 40.0f, 10.0f));
					else if (d->Id == 27) CurvePlot(b.GetCurve(*d, "Curve"), ImVec2(kNodeW - 40.0f, 18.0f));
					else if (d->Id == 5)
					{
						const auto c = b.GetVector(*d, "ColorA");
						ImGui::ColorButton("##c", ImVec4(c[0], c[1], c[2], c[3]), ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_NoTooltip, ImVec2(kNodeW - 40.0f, 10.0f));
					}
				}
				ImGui::Unindent(22.0f);
				ImGui::EndDisabled();
			}
			ImGui::PopID();
		}
		if (removeAt >= 0)
		{
			Snapshot();
			list.erase(list.begin() + removeAt);
			m_SelBlock = -1;
			Changed();
		}
		if (list.empty())
			ImGui::TextDisabled("  (Space or + to add blocks)");
		// Update 끝: GPU Event 를 보내는 핀 (Trigger Event On Die)
		if (ctx == 2)
		{
			ImGui::Dummy(ImVec2(kNodeW - 110.0f, 0));
			ImGui::SameLine();
			ed::BeginPin(EventOut(s), ed::PinKind::Output);
			ed::PinPivotAlignment(ImVec2(1.0f, 0.5f));
			bool used = false;
			for (const Vfx::System& o : m_Asset.Systems) used |= o.SpawnCtx.Parent == sys.Name;
			ImGui::TextColored(used ? ImVec4(1.0f, 0.7f, 0.35f, 1.0f) : ImVec4(0.5f, 0.5f, 0.5f, 1.0f), used ? "On Die " ICON_FA_CIRCLE : "On Die " ICON_FA_CIRCLE_NOTCH);
			ed::EndPin();
		}
		ImGui::PopID();
	}

	// Output
	header(3, nullptr);
	{
		const Vfx::Output& o = sys.OutputCtx;
		ImGui::TextDisabled("%s  |  %s  |  %s", kShapeNames[(int)o.Look], kBlendNames[(int)o.BlendMode], kOrientNames[(int)o.Orientation]);
		ImGui::TextDisabled("intensity %.2g%s%s%s", o.Intensity, o.SoftDistance > 0 ? "  |  soft" : "", o.Trail ? "  |  trail" : "", o.Sorted() ? "  |  sorted" : "");
	}
	ImGui::Dummy(ImVec2(kNodeW, 2));
	ImGui::PopItemWidth();
	ImGui::PopID();
	ed::EndNode();
	ed::PopStyleColor();
}

void VfxGraphWindow::DrawOperator(Vfx::OperatorNode& n)
{
	const Vfx::OperatorDesc* d = Vfx::FindOperator(n.Type);
	const bool sel = m_SelOp == n.Id;
	ed::PushStyleColor(ed::StyleColor_NodeBorder, ImColor(sel ? IM_COL32(255, 255, 255, 255) : kOpColor));
	ed::BeginNode(OpNodeOf(n.Id));
	ImGui::PushID(n.Id + 100000);
	const float w = 190.0f;
	// 머리: 이름 + 출력 핀
	const ImVec2 p0 = ImGui::GetCursorScreenPos();
	ImGui::GetWindowDrawList()->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + ImGui::GetFrameHeight()), kOpColor, 3.0f);
	ImGui::SetCursorScreenPos(ImVec2(p0.x + 6, p0.y + ImGui::GetStyle().FramePadding.y));
	ImGui::PushFont(UnityGUI::BoldFont());
	ImGui::TextUnformatted(d ? d->Label : n.Type.c_str());
	ImGui::PopFont();
	ImGui::SameLine(w - 34.0f);
	ed::BeginPin(OpOut(n.Id), ed::PinKind::Output);
	ed::PinPivotAlignment(ImVec2(1.0f, 0.5f));
	ImGui::TextUnformatted(ICON_FA_CIRCLE);
	ed::EndPin();
	ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + ImGui::GetFrameHeight() + 2.0f));
	if (d)
	{
		ImGui::PushItemWidth(w - 70.0f);
		for (int i = 0; i < (int)d->Inputs.size(); ++i)
		{
			const Vfx::OperatorInput& in = d->Inputs[i];
			ImGui::PushID(i);
			int from = -1;
			for (auto it = n.Inputs.begin(); it != n.Inputs.end(); ++it)
				if (Lower(it.key()) == Lower(in.Name) && it->is_number_integer()) from = it->get<int>();
			ed::BeginPin(OpIn(n.Id, i), ed::PinKind::Input);
			ed::PinPivotAlignment(ImVec2(0.0f, 0.5f));
			ImGui::TextColored(from >= 0 ? ImVec4(0.45f, 0.75f, 1.0f, 1.0f) : ImVec4(0.45f, 0.45f, 0.45f, 1.0f), from >= 0 ? ICON_FA_CIRCLE : ICON_FA_CIRCLE_NOTCH);
			ed::EndPin();
			ImGui::SameLine(0, 4);
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(in.Name);
			if (from < 0)
			{
				ImGui::SameLine(62.0f);
				std::array<float, 4> v = in.Default;
				if (n.Params.contains(in.Name)) v = Vfx::ValueFromJson(n.Params[in.Name], v);
				if (in.Kind == Vfx::ParamKind::Float)
				{
					if (ImGui::DragFloat("##v", &v[0], 0.01f, 0.0f, 0.0f, "%.3g")) { BeginEdit(); n.Params[in.Name] = v[0]; Changed(); }
				}
				else if (in.Kind == Vfx::ParamKind::Vector3)
				{
					if (ImGui::DragFloat3("##v", v.data(), 0.01f, 0.0f, 0.0f, "%.1f")) { BeginEdit(); n.Params[in.Name] = { v[0], v[1], v[2] }; Changed(); }
				}
				else
					ImGui::ColorButton("##c", ImVec4(v[0], v[1], v[2], v[3]), ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_AlphaPreviewHalf, ImVec2(w - 70.0f, 0));
			}
			ImGui::PopID();
		}
		ImGui::PopItemWidth();
		// 설정 (짧게 — 편집은 Inspector)
		for (const Vfx::ParamDesc& sp : d->Settings)
		{
			if (sp.Kind == Vfx::ParamKind::Curve)
			{
				Vfx::Block tmp;
				if (n.Params.contains(sp.Name)) tmp.Params[sp.Name] = n.Params[sp.Name];
				CurvePlot(tmp.GetCurve(Vfx::BlockDesc{}, sp.Name), ImVec2(w, 22.0f));
			}
			else if (sp.Kind == Vfx::ParamKind::Gradient)
			{
				Vfx::Block tmp;
				if (n.Params.contains(sp.Name)) tmp.Params[sp.Name] = n.Params[sp.Name];
				GradientBar(tmp.GetGradient(Vfx::BlockDesc{}, sp.Name), ImVec2(w, 12.0f));
			}
			else if (sp.Kind == Vfx::ParamKind::Enum)
			{
				const auto opts = Vfx::EnumOptions(sp);
				int cur = 0;
				if (n.Params.contains(sp.Name))
				{
					const json& v = n.Params[sp.Name];
					if (v.is_number()) cur = v.get<int>();
					else if (v.is_string()) for (int k = 0; k < (int)opts.size(); ++k) if (Lower(opts[k]) == Lower(v.get<std::string>())) cur = k;
				}
				cur = std::clamp(cur, 0, (int)opts.size() - 1);
				ImGui::TextDisabled("%s", sp.Name);
				ImGui::SameLine(62.0f);
				if (ImGui::Button((opts[cur] + "##set").c_str(), ImVec2(w - 62.0f, 0))) { Snapshot(); n.Params[sp.Name] = opts[(cur + 1) % opts.size()]; Changed(); }
			}
			else if (sp.Kind == Vfx::ParamKind::Text)
			{
				const std::string v = n.Params.contains(sp.Name) && n.Params[sp.Name].is_string() ? n.Params[sp.Name].get<std::string>() : std::string("(pick in Inspector)");
				ImGui::TextColored(ImVec4(0.55f, 0.8f, 1.0f, 1.0f), ICON_FA_LINK " %s", v.c_str());
			}
		}
	}
	ImGui::Dummy(ImVec2(w, 1));
	ImGui::PopID();
	ed::EndNode();
	ed::PopStyleColor();
}

void VfxGraphWindow::AddOperator(const std::string& type, ImVec2 canvasPos)
{
	Snapshot();
	Vfx::OperatorNode n;
	n.Id = m_Asset.NewOperatorId();
	n.Type = type;
	n.X = canvasPos.x;
	n.Y = canvasPos.y;
	m_Asset.Operators.push_back(n);
	m_SelOp = n.Id;
	m_SelSystem = m_SelContext = m_SelBlock = m_SelProperty = -1;
	Changed(true);
}

void VfxGraphWindow::RemoveOperator(int id)
{
	for (Vfx::OperatorNode& o : m_Asset.Operators)
		for (auto it = o.Inputs.begin(); it != o.Inputs.end();)
			it = it->is_number_integer() && it->get<int>() == id ? o.Inputs.erase(it) : std::next(it);
	for (Vfx::System& sys : m_Asset.Systems)
		for (auto* list : { &sys.Initialize, &sys.Update })
			for (Vfx::Block& b : *list)
				for (auto it = b.Links.begin(); it != b.Links.end();)
					it = it->is_number_integer() && it->get<int>() == id ? b.Links.erase(it) : std::next(it);
	m_Asset.Operators.erase(std::remove_if(m_Asset.Operators.begin(), m_Asset.Operators.end(), [&](const Vfx::OperatorNode& o) { return o.Id == id; }), m_Asset.Operators.end());
	if (m_SelOp == id) m_SelOp = -1;
}

void VfxGraphWindow::DrawCanvas(float width, float height)
{
	if (!m_Ctx)
	{
		ed::Config config;
		config.SettingsFile = nullptr;   // 노드 자리는 .vfx 에
		m_Ctx = ed::CreateEditor(&config);
	}
	ImGui::BeginChild("##vfxcanvas", ImVec2(width, height), false, ImGuiWindowFlags_NoScrollbar);
	ed::SetCurrentEditor(m_Ctx);
	ed::Begin("##vfxgraph", ImVec2(width, height));

	const bool resync = m_AppliedLayout != m_LayoutRevision;
	if (resync)
	{
		for (int s = 0; s < (int)m_Asset.Systems.size(); ++s)
		{
			json& e = m_Asset.Systems[s].Editor;
			if (!e.contains("x") || !e.contains("y"))
			{
				// 자리가 없는 시스템 (견본 · CLI): 왼쪽부터 나란히 — 저장할 것으로 치지 않는다
				e["x"] = s * (kNodeW + 80.0f);
				e["y"] = 0.0f;
			}
			ed::SetNodePosition(NodeOf(s), ImVec2(e["x"].get<float>(), e["y"].get<float>()));
		}
		for (const Vfx::OperatorNode& n : m_Asset.Operators)
			ed::SetNodePosition(OpNodeOf(n.Id), ImVec2(n.X, n.Y));
	}

	for (int s = 0; s < (int)m_Asset.Systems.size(); ++s)
		DrawSystem(s);
	for (Vfx::OperatorNode& n : m_Asset.Operators)
		DrawOperator(n);
	// 연산 노드 선: 노드 → 노드 입력, 노드 → 블록 값
	for (const Vfx::OperatorNode& n : m_Asset.Operators)
		if (const Vfx::OperatorDesc* d = Vfx::FindOperator(n.Type))
			for (int i = 0; i < (int)d->Inputs.size(); ++i)
				for (auto it = n.Inputs.begin(); it != n.Inputs.end(); ++it)
					if (Lower(it.key()) == Lower(d->Inputs[i].Name) && it->is_number_integer() && m_Asset.FindOperatorNode(it->get<int>()))
						ed::Link(ed::LinkId(Tagged(kTagOpLink, OpInVal(n.Id, i))), OpOut(it->get<int>()), OpIn(n.Id, i), ImColor(IM_COL32(110, 170, 240, 255)), 2.0f);
	for (int si = 0; si < (int)m_Asset.Systems.size(); ++si)
		for (int ctx = 1; ctx <= 2; ++ctx)
		{
			const auto& list = ctx == 1 ? m_Asset.Systems[si].Initialize : m_Asset.Systems[si].Update;
			for (int bi = 0; bi < (int)list.size(); ++bi)
				if (const Vfx::BlockDesc* d = Vfx::FindBlock(list[bi].Type))
					for (int pi = 0; pi < (int)d->Params.size(); ++pi)
					{
						const int from = LinkedTo(list[bi], d->Params[pi].Name);
						if (from >= 0 && m_Asset.FindOperatorNode(from))
							ed::Link(ed::LinkId(Tagged(kTagParamLink, ParamVal(si, ctx, bi, pi))), OpOut(from), ParamPin(si, ctx, bi, pi), ImColor(IM_COL32(110, 170, 240, 255)), 2.0f);
					}
		}
	// GPU Event 선 (부모 Update 의 On Die → 자식 Spawn)
	for (int s = 0; s < (int)m_Asset.Systems.size(); ++s)
	{
		const int p = m_Asset.FindSystem(m_Asset.Systems[s].SpawnCtx.Parent);
		if (p >= 0 && p != s)
			ed::Link(LinkOf(s), EventOut(p), EventIn(s), ImColor(IM_COL32(255, 170, 80, 255)), 2.5f);
	}

	// 잇기: On Die → GPU Event
	if (ed::BeginCreate(ImVec4(1.0f, 0.7f, 0.35f, 1.0f), 2.5f))
	{
		ed::PinId a, b;
		if (ed::QueryNewLink(&a, &b) && a && b)
		{
			uintptr_t pa = a.Get(), pb = b.Get();
			if (TagOf(pa) != kTagEventOut && TagOf(pa) != kTagOpOut) std::swap(pa, pb);   // pa = 출력
			const uint64_t ta = TagOf(pa), tb = TagOf(pb);
			const int from = SystemOfId(pa), to = SystemOfId(pb);
			const bool ok = ta == kTagEventOut && tb == kTagEventIn && from != to && from >= 0 && to >= 0 &&
				from < (int)m_Asset.Systems.size() && to < (int)m_Asset.Systems.size();
			if (ta == kTagOpOut && (tb == kTagOpIn || tb == kTagParam))
			{
				// 연산 노드 → 노드 입력 · 블록 값
				const int src = (int)ValOf(pa);
				if (ed::AcceptNewItem(ImVec4(0.45f, 0.75f, 1.0f, 1.0f), 2.0f))
				{
					Snapshot();
					bool okLink = false;
					if (tb == kTagOpIn)
					{
						const int dst = (int)(ValOf(pb) >> 8), input = (int)(ValOf(pb) & 0xFF);
						for (Vfx::OperatorNode& n : m_Asset.Operators)
							if (n.Id == dst && dst != src)
								if (const Vfx::OperatorDesc* d = Vfx::FindOperator(n.Type); d && input < (int)d->Inputs.size())
								{
									n.Inputs[d->Inputs[input].Name] = src;
									okLink = true;
								}
						// 고리는 받지 않는다
						struct Props : Vfx::PropertySource { const Vfx::Asset& A; Props(const Vfx::Asset& x) : A(x) {} bool Get(const std::string& k, std::array<float, 4>& o) const override { const Vfx::Property* p = A.FindProperty(k); if (!p) return false; o = p->Value; return true; } } props(m_Asset);
						std::vector<std::array<float, 4>> code;
						std::string err;
						if (okLink && !Vfx::CompileOperator(m_Asset, dst, props, code, err) && err.find("loop") != std::string::npos)
						{
							m_Asset = m_Undo.back();
							m_Undo.pop_back();
							m_Status = "operators cannot form a loop";
							m_StatusError = true;
							okLink = false;
						}
					}
					else
					{
						const ParamRef pr = DecodeParam(ValOf(pb));
						if (pr.S < (int)m_Asset.Systems.size())
						{
							auto& list = pr.Ctx == 1 ? m_Asset.Systems[pr.S].Initialize : m_Asset.Systems[pr.S].Update;
							if (pr.Block < (int)list.size())
								if (const Vfx::BlockDesc* d = Vfx::FindBlock(list[pr.Block].Type); d && pr.Param < (int)d->Params.size())
								{
									list[pr.Block].Links[d->Params[pr.Param].Name] = src;
									okLink = true;
								}
						}
					}
					if (okLink) Changed();
					else if (!m_Undo.empty() && m_Status != "operators cannot form a loop") m_Undo.pop_back();
				}
			}
			else if (!ok)
				ed::RejectNewItem(ImVec4(1, 0.3f, 0.3f, 1), 2.0f);
			else if (ed::AcceptNewItem())
			{
				Snapshot();
				m_Asset.Systems[to].SpawnCtx.Parent = m_Asset.Systems[from].Name;
				if (m_Asset.SimulationOrder().size() != m_Asset.Systems.size())
				{
					m_Asset.Systems[to].SpawnCtx.Parent.clear();   // 고리는 만들지 않는다
					m_Undo.pop_back();
					m_Status = "GPU events cannot form a loop";
					m_StatusError = true;
				}
				else
					Changed();
			}
		}
	}
	ed::EndCreate();
	if (ed::BeginDelete())
	{
		ed::LinkId l;
		while (ed::QueryDeletedLink(&l))
			if (ed::AcceptDeletedItem())
			{
				const uint64_t tag = TagOf(l.Get()), val = ValOf(l.Get());
				if (tag == kTagEventLink)
				{
					const int child = (int)val;
					if (child >= 0 && child < (int)m_Asset.Systems.size())
					{
						Snapshot();
						m_Asset.Systems[child].SpawnCtx.Parent.clear();
						Changed();
					}
				}
				else if (tag == kTagOpLink)
				{
					const int dst = (int)(val >> 8), input = (int)(val & 0xFF);
					for (Vfx::OperatorNode& n : m_Asset.Operators)
						if (n.Id == dst)
							if (const Vfx::OperatorDesc* d = Vfx::FindOperator(n.Type); d && input < (int)d->Inputs.size())
							{
								Snapshot();
								for (auto it = n.Inputs.begin(); it != n.Inputs.end(); ++it)
									if (Lower(it.key()) == Lower(d->Inputs[input].Name)) { n.Inputs.erase(it); break; }
								Changed();
							}
				}
				else if (tag == kTagParamLink)
				{
					const ParamRef pr = DecodeParam(val);
					if (pr.S < (int)m_Asset.Systems.size())
					{
						auto& list = pr.Ctx == 1 ? m_Asset.Systems[pr.S].Initialize : m_Asset.Systems[pr.S].Update;
						if (pr.Block < (int)list.size())
							if (const Vfx::BlockDesc* d = Vfx::FindBlock(list[pr.Block].Type); d && pr.Param < (int)d->Params.size())
							{
								Snapshot();
								for (auto it = list[pr.Block].Links.begin(); it != list[pr.Block].Links.end(); ++it)
									if (Lower(it.key()) == Lower(d->Params[pr.Param].Name)) { list[pr.Block].Links.erase(it); break; }
								Changed();
							}
					}
				}
			}
		ed::NodeId n;
		while (ed::QueryDeletedNode(&n))
		{
			// 연산 노드는 Delete 로 지운다, 시스템은 머리의 × 로만 (실수로 Delete 키에 사라지지 않게)
			if (TagOf(n.Get()) == kTagOp && ed::AcceptDeletedItem())
			{
				Snapshot();
				RemoveOperator((int)ValOf(n.Get()));
				Changed(true);
			}
			else if (TagOf(n.Get()) != kTagOp)
				ed::RejectDeletedItem();
		}
	}
	ed::EndDelete();

	// 노드를 끌어 옮겼으면 자리 기억 (Undo 없이 — 저장할 것만 표시)
	if (!resync && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
		for (int s = 0; s < (int)m_Asset.Systems.size(); ++s)
		{
			const ImVec2 pos = ed::GetNodePosition(NodeOf(s));
			json& e = m_Asset.Systems[s].Editor;
			const float x = e.contains("x") ? e["x"].get<float>() : FLT_MAX, y = e.contains("y") ? e["y"].get<float>() : FLT_MAX;
			if (fabsf(pos.x - x) > 0.5f || fabsf(pos.y - y) > 0.5f)
			{
				e["x"] = pos.x;
				e["y"] = pos.y;
				m_Dirty = true;
			}
		}

	if (!resync && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
		for (Vfx::OperatorNode& n : m_Asset.Operators)
		{
			const ImVec2 pos = ed::GetNodePosition(OpNodeOf(n.Id));
			if (fabsf(pos.x - n.X) > 0.5f || fabsf(pos.y - n.Y) > 0.5f)
			{
				n.X = pos.x;
				n.Y = pos.y;
				m_Dirty = true;
			}
		}
	// 고른 연산 노드 (Inspector)
	{
		ed::NodeId selected[2];
		const int count = ed::GetSelectedNodes(selected, 2);
		if (count == 1 && TagOf(selected[0].Get()) == kTagOp)
		{
			if (m_SelOp != (int)ValOf(selected[0].Get()))
			{
				m_SelOp = (int)ValOf(selected[0].Get());
				m_SelSystem = m_SelContext = m_SelBlock = m_SelProperty = -1;
			}
		}
		else if (m_SelOp >= 0 && (count != 0 || m_SelSystem >= 0))
			m_SelOp = -1;
	}

	// 오른쪽 클릭 · Space = 검색 창 (블록이나 문맥을 골랐으면 그 문맥에, 아니면 새 시스템)
	const ImVec2 mouse = ImGui::GetMousePos();
	ed::Suspend();
	const bool space = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && !ImGui::IsAnyItemActive() && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Space);
	if (ed::ShowBackgroundContextMenu() || space || m_OpenSearch)
	{
		if (!m_OpenSearch)
		{
			const bool inContext = space && m_SelSystem >= 0 && (m_SelContext == 1 || m_SelContext == 2);
			m_SearchSystem = inContext ? m_SelSystem : -1;
			m_SearchContext = inContext ? m_SelContext : -1;
		}
		m_SearchPos = ed::ScreenToCanvas(mouse);
		ImGui::OpenPopup("##vfxsearch");
		m_Search[0] = 0;
		m_SearchCursor = 0;
		m_OpenSearch = false;
	}
	DrawSearchPopup();
	// Delete: 고른 블록
	if (m_SelBlock >= 0 && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Delete) &&
		m_SelSystem >= 0 && m_SelSystem < (int)m_Asset.Systems.size())
	{
		auto& list = m_SelContext == 1 ? m_Asset.Systems[m_SelSystem].Initialize : m_Asset.Systems[m_SelSystem].Update;
		if (m_SelBlock < (int)list.size())
		{
			Snapshot();
			list.erase(list.begin() + m_SelBlock);
			m_SelBlock = -1;
			Changed();
		}
	}
	ed::Resume();

	ed::End();
	if (resync)
	{
		if (m_AppliedLayout == 0 || m_LayoutRevision <= 2 || m_NavigatePending)
		{
			// 처음 열 때: 시스템이 많으면 앞의 세 개만 화면에 (모두 맞추면 글자를 읽을 수 없게 작아진다)
			if (m_Asset.Systems.size() > 3)
			{
				ed::ClearSelection();
				for (int s = 0; s < 3; ++s) ed::SelectNode(NodeOf(s), true);
				ed::NavigateToSelection(false, 0.0f);
				ed::ClearSelection();
			}
			else
				ed::NavigateToContent(0.0f);
			m_NavigatePending = false;
		}
		m_AppliedLayout = m_LayoutRevision;
	}
	ed::SetCurrentEditor(nullptr);
	ImGui::EndChild();
}

void VfxGraphWindow::DrawSearchPopup()
{
	ImGui::SetNextWindowSize(ImVec2(320.0f, 0.0f));
	if (!ImGui::BeginPopup("##vfxsearch"))
		return;
	const bool forContext = m_SearchSystem >= 0 && m_SearchSystem < (int)m_Asset.Systems.size() && (m_SearchContext == 1 || m_SearchContext == 2);
	ImGui::TextDisabled("%s", forContext ? (std::string("Add block to ") + m_Asset.Systems[m_SearchSystem].Name + " / " + kContextNames[m_SearchContext]).c_str() : "Create System or Operator");
	if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
	ImGui::SetNextItemWidth(-1);
	ImGui::InputTextWithHint("##q", "Search", m_Search, sizeof(m_Search));
	const std::string q = Lower(m_Search);

	struct Item { std::string Label, Category; std::function<void()> Run; };
	std::vector<Item> items;
	if (forContext)
	{
		const Vfx::Context ctx = m_SearchContext == 1 ? Vfx::Context::Initialize : Vfx::Context::Update;
		for (const Vfx::BlockDesc& d : Vfx::Blocks())
			if (d.Ctx == ctx)
				items.push_back({ d.Label, d.Category, [this, &d] {
					Snapshot();
					Vfx::Block b;
					b.Type = d.Type;
					auto& list = m_SearchContext == 1 ? m_Asset.Systems[m_SearchSystem].Initialize : m_Asset.Systems[m_SearchSystem].Update;
					list.push_back(b);
					m_SelSystem = m_SearchSystem;
					m_SelContext = m_SearchContext;
					m_SelBlock = (int)list.size() - 1;
					Changed();
				} });
	}
	else
	{
		items.push_back({ "Simple System", "System", [this] { AddSystem(Vfx::DefaultAsset().Systems[0], m_SearchPos); } });
		Vfx::System empty;
		empty.Name = "Empty System";
		items.push_back({ "Empty System", "System", [this, empty] { AddSystem(empty, m_SearchPos); } });
		for (const Vfx::OperatorDesc& od : Vfx::Operators())
			items.push_back({ od.Label, std::string("Operator / ") + od.Category, [this, &od] { AddOperator(od.Type, m_SearchPos); } });
		for (const std::string& t : Vfx::TemplateNames())
		{
			Vfx::Asset a;
			Vfx::MakeTemplate(t, a);
			for (const Vfx::System& s : a.Systems)
				items.push_back({ t + " / " + s.Name, "Template", [this, s] { AddSystem(s, m_SearchPos); } });
		}
	}
	std::vector<int> shown;
	for (int i = 0; i < (int)items.size(); ++i)
		if (q.empty() || Lower(items[i].Label).find(q) != std::string::npos || Lower(items[i].Category).find(q) != std::string::npos)
			shown.push_back(i);
	if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) m_SearchCursor = (std::min)(m_SearchCursor + 1, (int)shown.size() - 1);
	if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) m_SearchCursor = (std::max)(m_SearchCursor - 1, 0);
	m_SearchCursor = std::clamp(m_SearchCursor, 0, (std::max)(0, (int)shown.size() - 1));
	int chosen = -1;
	ImGui::BeginChild("##results", ImVec2(0, (std::min)(360.0f, 6.0f + shown.size() * ImGui::GetTextLineHeightWithSpacing())));
	std::string lastCat;
	for (int k = 0; k < (int)shown.size(); ++k)
	{
		const Item& it = items[shown[k]];
		if (it.Category != lastCat)
		{
			ImGui::TextDisabled("%s", it.Category.c_str());
			lastCat = it.Category;
		}
		if (ImGui::Selectable(("  " + it.Label + "##" + std::to_string(shown[k])).c_str(), k == m_SearchCursor)) chosen = shown[k];
		if (k == m_SearchCursor && (ImGui::IsKeyPressed(ImGuiKey_DownArrow) || ImGui::IsKeyPressed(ImGuiKey_UpArrow))) ImGui::SetScrollHereY();
	}
	ImGui::EndChild();
	if (chosen < 0 && ImGui::IsKeyPressed(ImGuiKey_Enter) && !shown.empty())
		chosen = shown[m_SearchCursor];
	if (chosen >= 0)
	{
		items[chosen].Run();
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
}

void VfxGraphWindow::DrawOperatorInspector(Vfx::OperatorNode& n)
{
	const Vfx::OperatorDesc* d = Vfx::FindOperator(n.Type);
	ImGui::PushFont(UnityGUI::BoldFont());
	ImGui::Text("%s  #%d", d ? d->Label : n.Type.c_str(), n.Id);
	ImGui::PopFont();
	if (!d)
		return;
	ImGui::TextWrapped("%s", d->Help);
	ImGui::Separator();
	// 연결하지 않은 입력 · 설정을 블록 값처럼 (가짜 블록 정의로 같은 편집기를 쓴다)
	Vfx::BlockDesc fake{ "", "", "", Vfx::Context::Update, 0, {}, "" };
	for (const Vfx::OperatorInput& in : d->Inputs)
	{
		bool connected = false;
		for (auto it = n.Inputs.begin(); it != n.Inputs.end(); ++it)
			if (Lower(it.key()) == Lower(in.Name) && it->is_number_integer())
			{
				connected = true;
				ImGui::TextDisabled("%s", in.Name);
				ImGui::SameLine(120.0f);
				ImGui::TextColored(ImVec4(0.45f, 0.75f, 1.0f, 1.0f), "<- #%d", it->get<int>());
			}
		if (!connected)
			fake.Params.push_back({ in.Name, in.Kind, in.Default });
	}
	for (const Vfx::ParamDesc& sp : d->Settings)
		fake.Params.push_back(sp);
	Vfx::Block tmp;
	tmp.Params = n.Params;
	if (DrawBlockFields(tmp, fake, false, -1, -1, -1, false))
	{
		n.Params = tmp.Params;
		Changed();
	}
	ImGui::Dummy(ImVec2(0, 6));
	// 이 노드를 쓰는 블록 값
	ImGui::TextDisabled("Used by:");
	for (const Vfx::System& sys : m_Asset.Systems)
		for (const auto* list : { &sys.Initialize, &sys.Update })
			for (const Vfx::Block& b : *list)
				for (auto it = b.Links.begin(); it != b.Links.end(); ++it)
					if (it->is_number_integer() && it->get<int>() == n.Id)
						ImGui::BulletText("%s / %s . %s", sys.Name.c_str(), b.Type.c_str(), it.key().c_str());
	for (const Vfx::OperatorNode& o : m_Asset.Operators)
		for (auto it = o.Inputs.begin(); it != o.Inputs.end(); ++it)
			if (it->is_number_integer() && it->get<int>() == n.Id)
				ImGui::BulletText("operator #%d %s . %s", o.Id, o.Type.c_str(), it.key().c_str());
	ImGui::Dummy(ImVec2(0, 6));
	if (ImGui::Button(ICON_FA_TRASH " Delete Operator"))
	{
		Snapshot();
		RemoveOperator(n.Id);
		Changed(true);
	}
}

void VfxGraphWindow::DrawInspector(float width, float height)
{
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 6.0f));
	ImGui::BeginChild("##vfxinspector", ImVec2(width, height), true);
	ImGui::PopStyleVar();
	ImGui::PushFont(UnityGUI::BoldFont());
	ImGui::TextUnformatted("Inspector");
	ImGui::PopFont();
	ImGui::Separator();
	const float labelW = 120.0f;
	auto row = [&](const char* label) {
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(label);
		ImGui::SameLine(labelW);
		ImGui::SetNextItemWidth(-1);
	};

	// 연산 노드
	if (m_SelOp >= 0)
		for (Vfx::OperatorNode& n : m_Asset.Operators)
			if (n.Id == m_SelOp)
			{
				DrawOperatorInspector(n);
				ImGui::EndChild();
				return;
			}

	// 속성
	if (m_SelSystem < 0 && m_SelProperty >= 0 && m_SelProperty < (int)m_Asset.Properties.size())
	{
		Vfx::Property& p = m_Asset.Properties[m_SelProperty];
		char name[128];
		snprintf(name, sizeof(name), "%s", p.Name.c_str());
		row("Name");
		if (ImGui::InputText("##name", name, sizeof(name), ImGuiInputTextFlags_EnterReturnsTrue) && name[0] && !m_Asset.FindProperty(name))
		{
			Snapshot();
			const std::string old = p.Name;
			for (Vfx::System& s : m_Asset.Systems)
			{
				if (s.SpawnCtx.RateBind == old) s.SpawnCtx.RateBind = name;
				for (auto* list : { &s.Initialize, &s.Update })
					for (Vfx::Block& b : *list)
						for (auto it = b.Bind.begin(); it != b.Bind.end(); ++it)
							if (it->is_string() && it->get<std::string>() == old) *it = name;
			}
			p.Name = name;
			Changed();
		}
		row("Type");
		ImGui::TextDisabled("%s", kPropTypes[(int)p.Type]);
		row("Value");
		bool ch = false;
		switch (p.Type)
		{
		case Vfx::PropertyType::Float: ch = ImGui::DragFloat("##v", &p.Value[0], 0.01f); break;
		case Vfx::PropertyType::Int: { int i = (int)p.Value[0]; ch = ImGui::DragInt("##v", &i); p.Value[0] = (float)i; break; }
		case Vfx::PropertyType::Bool: { bool b = p.Value[0] > 0.5f; ch = ImGui::Checkbox("##v", &b); p.Value[0] = b ? 1.0f : 0.0f; break; }
		case Vfx::PropertyType::Vector3: ch = ImGui::DragFloat3("##v", p.Value.data(), 0.01f); break;
		case Vfx::PropertyType::Color: ch = ImGui::ColorEdit4("##v", p.Value.data(), ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float); break;
		}
		if (p.Type == Vfx::PropertyType::Float)
		{
			row("Range");
			float r[2] = { p.Min, p.Max };
			if (ImGui::DragFloat2("##r", r, 0.1f)) { BeginEdit(); p.Min = r[0]; p.Max = r[1]; Changed(); }
		}
		if (ch) { BeginEdit(); Changed(); }
		ImGui::Dummy(ImVec2(0, 6));
		if (ImGui::Button(ICON_FA_TRASH " Delete Property"))
		{
			Snapshot();
			m_Asset.Properties.erase(m_Asset.Properties.begin() + m_SelProperty);
			m_SelProperty = -1;
			Changed();
		}
		ImGui::EndChild();
		return;
	}
	if (m_SelSystem < 0 || m_SelSystem >= (int)m_Asset.Systems.size())
	{
		ImGui::TextDisabled("Select a system, context or block.");
		ImGui::Dummy(ImVec2(0, 8));
		// 에셋 설정: 화면 밖일 때 (Unity 의 Culling Flags)
		row("Culling");
		int cull = (int)m_Asset.CullingMode;
		const char* culls[] = { "Simulate When Visible", "Always Simulate" };
		if (ImGui::Combo("##culling", &cull, culls, 2)) { Snapshot(); m_Asset.CullingMode = (Vfx::Culling)cull; Changed(); }
		ImGui::Dummy(ImVec2(0, 8));
		ImGui::TextWrapped("Space / right-click: add blocks or systems. Drag a system's \"On Die\" pin to another system's \"GPU Event\" pin to spawn particles where they die.");
		const auto issues = m_Asset.Validate();
		for (const std::string& i : issues)
			ImGui::TextColored(ImVec4(1, 0.75f, 0.3f, 1), "%s", i.c_str());
		ImGui::EndChild();
		return;
	}
	Vfx::System& sys = m_Asset.Systems[m_SelSystem];

	// 블록
	if (m_SelBlock >= 0 && (m_SelContext == 1 || m_SelContext == 2))
	{
		auto& list = m_SelContext == 1 ? sys.Initialize : sys.Update;
		if (m_SelBlock < (int)list.size())
		{
			Vfx::Block& b = list[m_SelBlock];
			if (const Vfx::BlockDesc* d = Vfx::FindBlock(b.Type))
			{
				ImGui::PushFont(UnityGUI::BoldFont());
				ImGui::TextUnformatted(d->Label);
				ImGui::PopFont();
				ImGui::TextWrapped("%s", d->Help);
				ImGui::Separator();
				if (DrawBlockFields(b, *d, false)) Changed();
				ImGui::Dummy(ImVec2(0, 6));
				if (m_SelBlock > 0 && ImGui::Button("Move Up")) { Snapshot(); std::swap(list[m_SelBlock], list[m_SelBlock - 1]); --m_SelBlock; Changed(); }
				ImGui::SameLine();
				if (m_SelBlock + 1 < (int)list.size() && ImGui::Button("Move Down")) { Snapshot(); std::swap(list[m_SelBlock], list[m_SelBlock + 1]); ++m_SelBlock; Changed(); }
			}
			else
				ImGui::TextColored(ImVec4(1, 0.4f, 0.3f, 1), "unknown block %s", b.Type.c_str());
		}
		ImGui::EndChild();
		return;
	}

	// 시스템 · 문맥
	char name[128];
	snprintf(name, sizeof(name), "%s", sys.Name.c_str());
	row("System");
	if (ImGui::InputText("##sysname", name, sizeof(name), ImGuiInputTextFlags_EnterReturnsTrue) && name[0] && m_Asset.FindSystem(name) < 0)
	{
		Snapshot();
		for (Vfx::System& o : m_Asset.Systems)
			if (o.SpawnCtx.Parent == sys.Name) o.SpawnCtx.Parent = name;
		sys.Name = name;
		Changed();
	}
	row("Capacity");
	if (ImGui::DragInt("##cap", &sys.Capacity, 50.0f, 1, 4 * 1024 * 1024)) { BeginEdit(); Changed(); }
	row("Space");
	int space = sys.Local ? 1 : 0;
	const char* spaces[] = { "World", "Local" };
	if (ImGui::Combo("##space", &space, spaces, 2)) { Snapshot(); sys.Local = space == 1; Changed(); }

	const int ctx = m_SelContext < 0 ? 0 : m_SelContext;
	if (ctx == 0 || m_SelContext < 0)
	{
		ImGui::SeparatorText("Spawn");
		Vfx::Spawn& sp = sys.SpawnCtx;
		row("GPU Event From");
		std::string parentLabel = sp.Parent.empty() ? "(none - spawns itself)" : sp.Parent;
		if (ImGui::BeginCombo("##parent", parentLabel.c_str()))
		{
			if (ImGui::Selectable("(none - spawns itself)", sp.Parent.empty())) { Snapshot(); sp.Parent.clear(); Changed(); }
			for (const Vfx::System& o : m_Asset.Systems)
				if (o.Name != sys.Name && ImGui::Selectable(o.Name.c_str(), o.Name == sp.Parent))
				{
					Snapshot();
					const std::string old = sp.Parent;
					sp.Parent = o.Name;
					if (m_Asset.SimulationOrder().size() != m_Asset.Systems.size()) { sp.Parent = old; m_Undo.pop_back(); }
					else Changed();
				}
			ImGui::EndCombo();
		}
		if (!sp.Parent.empty())
		{
			row("Trigger");
			int trig = (int)sp.Trigger;
			const char* triggers[] = { "On Die (when the parent dies)", "Rate (while the parent lives)" };
			if (ImGui::Combo("##trigger", &trig, triggers, 2)) { Snapshot(); sp.Trigger = (Vfx::EventTrigger)trig; Changed(); }
			if (sp.Trigger == Vfx::EventTrigger::Rate)
			{
				row("Events / s");
				if (ImGui::DragFloat("##erate", &sp.EventRate, 0.5f, 0.0f, 240.0f, "%.1f per parent")) { BeginEdit(); Changed(); }
			}
			row("Count per Event");
			if (ImGui::DragInt("##cpe", &sp.CountPerEvent, 1.0f, 1, 4096)) { BeginEdit(); Changed(); }
		}
		else
		{
			row("Rate");
			if (ImGui::DragFloat("##rate", &sp.Rate, 1.0f, 0.0f, 1e6f, "%.1f / s")) { BeginEdit(); Changed(); }
			row("Rate Property");
			if (ImGui::BeginCombo("##ratebind", sp.RateBind.empty() ? "(none)" : sp.RateBind.c_str()))
			{
				if (ImGui::Selectable("(none)", sp.RateBind.empty())) { Snapshot(); sp.RateBind.clear(); Changed(); }
				for (const Vfx::Property& p : m_Asset.Properties)
					if (p.Type == Vfx::PropertyType::Float && ImGui::Selectable(p.Name.c_str(), p.Name == sp.RateBind)) { Snapshot(); sp.RateBind = p.Name; Changed(); }
				ImGui::EndCombo();
			}
			row("Loop");
			if (ImGui::Checkbox("##loop", &sp.Loop)) { Snapshot(); Changed(); }
			row("Duration");
			if (ImGui::DragFloat("##dur", &sp.Duration, 0.05f, 0.0f, 600.0f, sp.Duration <= 0 ? "forever" : "%.2f s")) { BeginEdit(); Changed(); }
			row("Delay");
			if (ImGui::DragFloat("##delay", &sp.Delay, 0.05f, 0.0f, 600.0f, "%.2f s")) { BeginEdit(); Changed(); }
			char ev[64];
			snprintf(ev, sizeof(ev), "%s", sp.StartEvent.c_str());
			row("Start Event");
			if (ImGui::InputText("##start", ev, sizeof(ev), ImGuiInputTextFlags_EnterReturnsTrue)) { Snapshot(); sp.StartEvent = ev; Changed(); }
			snprintf(ev, sizeof(ev), "%s", sp.StopEvent.c_str());
			row("Stop Event");
			if (ImGui::InputText("##stop", ev, sizeof(ev), ImGuiInputTextFlags_EnterReturnsTrue)) { Snapshot(); sp.StopEvent = ev; Changed(); }
			ImGui::TextDisabled("Bursts (time, count, cycles 0 = forever, interval)");
			int removeAt = -1;
			for (int i = 0; i < (int)sp.Bursts.size(); ++i)
			{
				Vfx::Burst& b = sp.Bursts[i];
				ImGui::PushID(i);
				const float w = (width - 60.0f) / 4.0f;
				ImGui::SetNextItemWidth(w);
				bool ch = ImGui::DragFloat("##t", &b.Time, 0.01f, 0.0f, 600.0f, "%.2fs");
				ImGui::SameLine();
				ImGui::SetNextItemWidth(w);
				ch |= ImGui::DragInt("##c", &b.Count, 1.0f, 0, 1000000);
				ImGui::SameLine();
				ImGui::SetNextItemWidth(w);
				ch |= ImGui::DragInt("##cy", &b.Cycles, 0.1f, 0, 100000);
				ImGui::SameLine();
				ImGui::SetNextItemWidth(w);
				ch |= ImGui::DragFloat("##i", &b.Interval, 0.01f, 0.01f, 600.0f, "%.2fs");
				ImGui::SameLine();
				if (ImGui::SmallButton(ICON_FA_XMARK)) removeAt = i;
				if (ch) { BeginEdit(); Changed(); }
				ImGui::PopID();
			}
			if (removeAt >= 0) { Snapshot(); sp.Bursts.erase(sp.Bursts.begin() + removeAt); Changed(); }
			if (ImGui::SmallButton("+ Burst")) { Snapshot(); sp.Bursts.push_back({}); Changed(); }
		}
	}
	if (ctx == 3 || m_SelContext < 0)
	{
		ImGui::SeparatorText("Output Particle Quad");
		Vfx::Output& o = sys.OutputCtx;
		int v = (int)o.BlendMode;
		row("Blend Mode");
		if (ImGui::Combo("##blend", &v, kBlendNames, 2)) { Snapshot(); o.BlendMode = (Vfx::Blend)v; Changed(); }
		v = (int)o.Look;
		row("Shape");
		if (ImGui::Combo("##shape", &v, kShapeNames, IM_ARRAYSIZE(kShapeNames))) { Snapshot(); o.Look = (Vfx::Shape)v; Changed(); }
		v = (int)o.Orientation;
		row("Orient");
		if (ImGui::Combo("##orient", &v, kOrientNames, 3)) { Snapshot(); o.Orientation = (Vfx::Orient)v; Changed(); }
		if (o.Orientation == Vfx::Orient::AlongVelocity)
		{
			row("Stretch");
			if (ImGui::DragFloat("##stretch", &o.Stretch, 0.002f, 0.0f, 2.0f)) { BeginEdit(); Changed(); }
		}
		row("Intensity (HDR)");
		if (ImGui::DragFloat("##int", &o.Intensity, 0.05f, 0.0f, 100.0f)) { BeginEdit(); Changed(); }
		row("Soft Particles");
		if (ImGui::DragFloat("##soft", &o.SoftDistance, 0.01f, 0.0f, 10.0f, o.SoftDistance <= 0 ? "off" : "%.2f m")) { BeginEdit(); Changed(); }
		if (o.Look == Vfx::Shape::Texture)
		{
			if (ParticleSystemEditor::TexturePicker("Texture", &o.Texture, "vfxtex:" + std::to_string((uintptr_t)this)))
			{
				Snapshot();
				Changed();
			}
			int fb[2] = { o.FlipbookColumns, o.FlipbookRows };
			row("Flipbook");
			if (ImGui::DragInt2("##fb", fb, 0.05f, 1, 64)) { BeginEdit(); o.FlipbookColumns = fb[0]; o.FlipbookRows = fb[1]; Changed(); }
			row("Flipbook FPS");
			if (ImGui::DragFloat("##fps", &o.FlipbookFps, 0.1f, 0.0f, 120.0f, o.FlipbookFps <= 0 ? "once per life" : "%.1f")) { BeginEdit(); Changed(); }
		}
		row("Sort");
		int sort = (int)o.Sort;
		const char* sorts[] = { "Auto (Alpha Blend)", "On", "Off" };
		if (ImGui::Combo("##sort", &sort, sorts, 3)) { Snapshot(); o.Sort = (Vfx::SortMode)sort; Changed(); }
		// 꼬리 (Unity 의 Output Particle Strip)
		row("Trail");
		if (ImGui::Checkbox("##trail", &o.Trail)) { Snapshot(); Changed(); }
		if (o.Trail)
		{
			row("Trail Points");
			if (ImGui::DragInt("##tp", &o.TrailPoints, 0.2f, 2, 32)) { BeginEdit(); Changed(); }
			row("Trail Length");
			if (ImGui::DragFloat("##tl", &o.TrailLength, 0.01f, 0.02f, 10.0f, "%.2f s")) { BeginEdit(); Changed(); }
			row("Trail Width");
			if (ImGui::DragFloat("##tw", &o.TrailWidth, 0.01f, 0.0f, 20.0f, "x %.2f size")) { BeginEdit(); Changed(); }
			row("Trail Only");
			if (ImGui::Checkbox("##to", &o.TrailOnly)) { Snapshot(); Changed(); }
		}
	}
	if (ctx == 1 || ctx == 2)
	{
		ImGui::SeparatorText(kContextNames[ctx]);
		ImGui::TextDisabled("Select a block, or press + / Space to add one.");
	}
	ImGui::EndChild();
}

// ------------------------------------------------------------------ 등록
void VfxGraphWindow::RegisterEditor()
{
	EditorGUIManager::GetI()->RegisterWindow(new VfxGraphWindow);
	EditorGUIManager::GetI()->RegisterWindow(new VfxAssistantWindow);
	VisualEffect::OpenGraph = [](const std::string& path) { VfxGraphWindow::Open(path); };

	// Project 창: Create > Visual Effect Graph (.vfx), 더블클릭 = 열기
	EditorExtensions::AssetType t;
	t.Owner = "vfx";
	t.Extension = ".vfx";
	t.Icon = "particle_system";
	t.CreateMenu = "Visual Effect Graph";
	t.DefaultName = "New VFX";
	t.Create = [](const std::string& path) {
		std::string err;
		Vfx::Save(path, Vfx::DefaultAsset(), err);
	};
	t.Open = [](const std::string& path) { VfxGraphWindow::Open(path); };
	t.Inspector = [](const std::string& path) {
		const Vfx::Loaded l = Vfx::Load(path);
		if (!l.Data)
		{
			UnityGUI::HelpBox(l.Error.c_str(), true);
			return;
		}
		UnityGUI::ValueLabel("Systems", std::to_string(l.Data->Systems.size()).c_str());
		UnityGUI::ValueLabel("Properties", std::to_string(l.Data->Properties.size()).c_str());
		int capacity = 0;
		for (const auto& s : l.Data->Systems) capacity += s.Capacity;
		UnityGUI::ValueLabel("Capacity", std::to_string(capacity).c_str());
		for (const std::string& i : l.Data->Validate())
			UnityGUI::HelpBox(i.c_str(), true);
		if (UnityGUI::CenterButton("Open Visual Effect Graph"))
			VfxGraphWindow::Open(path);
		if (UnityGUI::CenterButton("Place in Scene"))
			PlaceInScene(path);
	};
	EditorExtensions::RegisterAssetType(t);

	VfxCli::Register([](const std::string& op, const json& args, json& result, std::string& error) {
		const std::string path = args.value("path", std::string());
		result = json::object();
		if (op == "window")
		{
			VfxGraphWindow::Open(path);
			if (args.contains("system") && VfxGraphWindow::Instance())
			{
				// 시스템 번호 또는 이름, 문맥 (spawn · initialize · update · output), 블록 번호
				const Vfx::Loaded l = Vfx::Load(path.empty() ? VfxGraphWindow::Instance()->Path() : path);
				int s = args["system"].is_number_integer() ? args["system"].get<int>() : (l.Data ? l.Data->FindSystem(args["system"].get<std::string>()) : -1);
				static const char* contexts[] = { "spawn", "initialize", "update", "output" };
				int c = -1;
				for (int i = 0; i < 4; ++i) if (args.value("context", std::string()) == contexts[i]) c = i;
				VfxGraphWindow::Select(s, c, args.value("block", -1));
			}
			return true;
		}
		if (op == "assistant") { VfxAssistantWindow::Open(path); return true; }
		if (op.rfind("assistant.", 0) == 0) return VfxAssistantWindow::CliOp(op, args, result, error);
		return false;
	});
}
