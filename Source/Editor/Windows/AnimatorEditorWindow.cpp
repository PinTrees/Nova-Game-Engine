#include "pch.h"
#include "AnimatorEditorWindow.h"
#include "AnimatorController.h"
#include "Animator.h"
#include "UnityGUI.h"
#include "UndoSystem.h"
#include "ImGui/imgui_internal.h"

using namespace AnimatorTypes;

namespace
{
	// ---- Unity 다크 스킨 색 ----
	const ImU32 kGraphBg      = IM_COL32(40, 40, 40, 255);
	const ImU32 kGridMinor    = IM_COL32(35, 35, 35, 255);
	const ImU32 kGridMajor    = IM_COL32(29, 29, 29, 255);
	const ImU32 kPanelBg      = IM_COL32(56, 56, 56, 255);
	const ImU32 kToolbarBg    = IM_COL32(60, 60, 60, 255);
	const ImU32 kToolbarLine  = IM_COL32(35, 35, 35, 255);
	const ImU32 kTabInactive  = IM_COL32(45, 45, 45, 255);
	const ImU32 kText         = IM_COL32(210, 210, 210, 255);
	const ImU32 kTextDim      = IM_COL32(150, 150, 150, 255);
	const ImU32 kNodeGrey     = IM_COL32(88, 88, 88, 255);
	const ImU32 kNodeDefault  = IM_COL32(205, 120, 44, 255);
	const ImU32 kNodeEntry    = IM_COL32(38, 132, 66, 255);
	const ImU32 kNodeAny      = IM_COL32(52, 158, 150, 255);
	const ImU32 kNodeExit     = IM_COL32(178, 52, 52, 255);
	const ImU32 kNodeBorder   = IM_COL32(24, 24, 24, 255);
	const ImU32 kSelect       = IM_COL32(72, 146, 238, 255);
	const ImU32 kLine         = IM_COL32(214, 214, 214, 255);
	const ImU32 kLineDefault  = IM_COL32(205, 120, 44, 255);
	const ImU32 kLive         = IM_COL32(72, 146, 238, 255);
	const ImU32 kRowSelected  = IM_COL32(44, 93, 135, 255);

	constexpr float kToolbarH = 21.0f;
	constexpr float kStateW = 200.0f, kStateH = 40.0f;
	constexpr float kSpecialW = 150.0f, kSpecialH = 40.0f;

	ImVec2 Add(ImVec2 a, ImVec2 b) { return ImVec2(a.x + b.x, a.y + b.y); }
	ImVec2 Sub(ImVec2 a, ImVec2 b) { return ImVec2(a.x - b.x, a.y - b.y); }
	ImVec2 Mul(ImVec2 a, float s) { return ImVec2(a.x * s, a.y * s); }
	float Len(ImVec2 a) { return sqrtf(a.x * a.x + a.y * a.y); }

	float DistToSegment(ImVec2 p, ImVec2 a, ImVec2 b)
	{
		const ImVec2 ab = Sub(b, a);
		const float l2 = ab.x * ab.x + ab.y * ab.y;
		float t = l2 > 1e-6f ? ((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / l2 : 0.0f;
		t = std::clamp(t, 0.0f, 1.0f);
		return Len(Sub(p, Add(a, Mul(ab, t))));
	}

	// 선 + 가운데 화살표 (여러 전이가 겹치면 Unity 처럼 화살표 3개)
	void DrawArrowLine(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col, float zoom, int arrows)
	{
		const float thickness = (std::max)(1.0f, 2.0f * zoom);
		dl->AddLine(a, b, col, thickness);
		const float len = Len(Sub(b, a));
		if (len < 1.0f)
			return;
		const ImVec2 dir = Mul(Sub(b, a), 1.0f / len);
		const ImVec2 perp(-dir.y, dir.x);
		const float size = (std::max)(3.0f, 6.0f * zoom);
		const ImVec2 mid = Mul(Add(a, b), 0.5f);
		for (int k = 0; k < arrows; ++k)
		{
			const float shift = (k - (arrows - 1) * 0.5f) * size * 2.2f;
			const ImVec2 c = Add(mid, Mul(dir, shift));
			dl->AddTriangleFilled(Add(c, Mul(dir, size)), Add(Sub(c, Mul(dir, size)), Mul(perp, size)), Sub(Sub(c, Mul(dir, size)), Mul(perp, size)), col);
		}
	}

	bool ContainsNoCase(const std::string& text, const char* needle)
	{
		if (needle == nullptr || needle[0] == 0)
			return true;
		std::string a = text, b = needle;
		std::transform(a.begin(), a.end(), a.begin(), ::tolower);
		std::transform(b.begin(), b.end(), b.begin(), ::tolower);
		return a.find(b) != std::string::npos;
	}

	void PushPopupStyle()
	{
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 5));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 4));
		ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.22f, 0.22f, 0.22f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.17f, 0.36f, 0.53f, 1.0f));
	}
	void PopPopupStyle()
	{
		ImGui::PopStyleColor(2);
		ImGui::PopStyleVar(2);
	}

	// 툴바 버튼 모양 (텍스트 + 배경). 눌리면 true
	bool ToolbarButton(ImDrawList* dl, const char* id, const char* text, ImVec2 pos, float width, bool active)
	{
		ImGui::SetCursorScreenPos(pos);
		const bool pressed = ImGui::InvisibleButton(id, ImVec2(width, kToolbarH - 1));
		const bool hovered = ImGui::IsItemHovered();
		const ImU32 bg = active ? IM_COL32(78, 92, 112, 255) : (hovered ? IM_COL32(72, 72, 72, 255) : kToolbarBg);
		dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + kToolbarH - 1), bg);
		const ImVec2 ts = ImGui::CalcTextSize(text);
		dl->AddText(ImVec2(pos.x + (width - ts.x) * 0.5f, pos.y + (kToolbarH - ts.y) * 0.5f - 1), kText, text);
		return pressed;
	}
}

AnimatorEditorWindow* AnimatorEditorWindow::s_Instance = nullptr;

AnimatorEditorWindow::AnimatorEditorWindow()
	: EditorWindow("Animator", ICON_FA_CIRCLE_PLAY)
{
	s_Instance = this;
}

AnimatorEditorWindow::~AnimatorEditorWindow()
{
	if (s_Instance == this)
		s_Instance = nullptr;
}

void AnimatorEditorWindow::Focus()
{
	if (s_Instance == nullptr)
		return;
	s_Instance->SetIsOpened(true);
	ImGui::SetWindowFocus(s_Instance->GetImGuiName().c_str());
}

void AnimatorEditorWindow::Update()
{
	// (개발/검증용) NOVA_FOCUS_WINDOW=Animator 이면 시작 직후 이 탭을 앞으로
	static int s_Frames = 0;
	if (s_Frames < 30)
	{
		++s_Frames;
		char focus[64] = {};
		if (s_Frames > 3 && ::GetEnvironmentVariableA("NOVA_FOCUS_WINDOW", focus, sizeof(focus)) > 0 && _stricmp(focus, "Animator") == 0)
			ImGui::SetWindowFocus(GetImGuiName().c_str());
	}
}

void AnimatorEditorWindow::Open(std::shared_ptr<AnimatorController> controller)
{
	if (controller == m_Controller)
		return;
	m_Controller = controller;
	m_Layer = 0;
	m_SelNode = kNoNode;
	m_SelTransition = -1;
	m_Drag = DragMode::None;
	m_RenameParam = -1;
	m_RenameLayer = -1;
	m_NeedFrame = true;
}

// ------------------------------------------------------------------ 대상 / 선택
void AnimatorEditorWindow::ResolveTarget()
{
	Scene* scene = SceneManager::GetI()->GetCurrentScene();
	if (m_Target && scene)
	{
		const auto all = scene->GetAllGameObjects();
		if (std::find(all.begin(), all.end(), m_Target) == all.end())
			m_Target = nullptr;   // 씬이 바뀌었거나 (Play/Stop) 오브젝트가 지워졌다
	}

	const SelectionType type = SelectionManager::GetSelectedObjectType();
	if (type == SelectionType::GAMEOBJECT)
	{
		if (GameObject* go = SelectionManager::GetSelectedGameObject())
			if (Animator* animator = go->GetComponent<Animator>())
			{
				m_Target = go;
				if (animator->GetController())
					Open(animator->GetController());
			}
	}
	else if (type == SelectionType::FILE && SelectionManager::GetSelectedSubType() == SelectionSubType::ANIMATOR_CONTROLLER)
	{
		auto controller = SelectionManager::GetSelectAnimatorController();
		if (controller && controller != m_Controller)
		{
			Open(controller);
			m_Target = nullptr;
		}
	}

	// Play 중 대상이 없으면 같은 컨트롤러를 쓰는 첫 Animator 를 Live Link 대상으로
	if (m_Target == nullptr && m_Controller && scene && Application::IsPlaying())
		for (GameObject* go : scene->GetAllGameObjects())
			if (Animator* animator = go->GetComponent<Animator>())
				if (animator->GetController() == m_Controller)
				{
					m_Target = go;
					break;
				}

	if (m_Controller && (m_Layer < 0 || m_Layer >= (int)m_Controller->Layers.size()))
		m_Layer = 0;
}

void AnimatorEditorWindow::SyncSelection()
{
	const AnimatorSelection& sel = SelectionManager::GetAnimatorSelection();
	if (SelectionManager::GetSelectedObjectType() != SelectionType::ANIMATOR || sel.Controller != m_Controller || sel.Layer != m_Layer)
	{
		m_SelNode = kNoNode;
		m_SelTransition = -1;
		return;
	}
	const AnimatorLayer& layer = m_Controller->Layers[m_Layer];
	if (sel.State >= 0 && sel.State < (int)layer.States.size())
	{
		m_SelNode = sel.State;
		m_SelTransition = -1;
	}
	else if (sel.Transition >= 0 && sel.Transition < (int)layer.Transitions.size())
	{
		m_SelNode = kNoNode;
		m_SelTransition = sel.Transition;
	}
	else
	{
		if (m_SelNode >= 0)
			m_SelNode = kNoNode;   // 특수 노드(Entry/Any/Exit) 선택만 유지
		m_SelTransition = -1;
	}
}

Animator* AnimatorEditorWindow::LiveAnimator() const
{
	if (!m_AutoLiveLink || !Application::IsPlaying() || m_Target == nullptr)
		return nullptr;
	Animator* animator = m_Target->GetComponent<Animator>();
	return animator && animator->GetController() == m_Controller ? animator : nullptr;
}

void AnimatorEditorWindow::SelectNode(int node)
{
	m_SelNode = node;
	m_SelTransition = -1;
	if (m_Controller)
		SelectionManager::SetSelectedAnimatorItem(m_Controller, m_Layer, node >= 0 ? node : -1, -1);
}

void AnimatorEditorWindow::SelectTransition(int index)
{
	m_SelNode = kNoNode;
	m_SelTransition = index;
	if (m_Controller)
		SelectionManager::SetSelectedAnimatorItem(m_Controller, m_Layer, -1, index);
}

void AnimatorEditorWindow::DeleteSelection()
{
	if (m_Controller == nullptr)
		return;
	if (m_SelNode >= 0)
		m_Controller->RemoveState(m_Layer, m_SelNode);
	else if (m_SelTransition >= 0)
		m_Controller->RemoveTransition(m_Layer, m_SelTransition);
	else
		return;
	m_Controller->Commit();
	SelectNode(kNoNode);
}

// ------------------------------------------------------------------ 그래프 좌표
ImVec2 AnimatorEditorWindow::GraphToScreen(ImVec2 g) const
{
	return ImVec2(m_CanvasCenter.x + (g.x + m_Pan.x) * m_Zoom, m_CanvasCenter.y + (g.y + m_Pan.y) * m_Zoom);
}

ImVec2 AnimatorEditorWindow::ScreenToGraph(ImVec2 s) const
{
	return ImVec2((s.x - m_CanvasCenter.x) / m_Zoom - m_Pan.x, (s.y - m_CanvasCenter.y) / m_Zoom - m_Pan.y);
}

ImVec2 AnimatorEditorWindow::NodePos(int node) const
{
	const AnimatorLayer& l = m_Controller->Layers[m_Layer];
	switch (node)
	{
	case kEntryNode: return ImVec2(l.EntryX, l.EntryY);
	case kAnyNode: return ImVec2(l.AnyX, l.AnyY);
	case kExitNode: return ImVec2(l.ExitX, l.ExitY);
	default: return node >= 0 && node < (int)l.States.size() ? ImVec2(l.States[node].PosX, l.States[node].PosY) : ImVec2(0, 0);
	}
}

ImVec2 AnimatorEditorWindow::NodeSize(int node) const
{
	return node >= 0 ? ImVec2(kStateW, kStateH) : ImVec2(kSpecialW, kSpecialH);
}

ImVec2 AnimatorEditorWindow::NodeCenterScreen(int node) const
{
	return GraphToScreen(Add(NodePos(node), Mul(NodeSize(node), 0.5f)));
}

void AnimatorEditorWindow::SetNodePos(int node, ImVec2 p)
{
	AnimatorLayer& l = m_Controller->Layers[m_Layer];
	switch (node)
	{
	case kEntryNode: l.EntryX = p.x; l.EntryY = p.y; break;
	case kAnyNode: l.AnyX = p.x; l.AnyY = p.y; break;
	case kExitNode: l.ExitX = p.x; l.ExitY = p.y; break;
	default:
		if (node >= 0 && node < (int)l.States.size()) { l.States[node].PosX = p.x; l.States[node].PosY = p.y; }
		break;
	}
}

std::string AnimatorEditorWindow::NodeName(int node) const
{
	switch (node)
	{
	case kEntryNode: return kEntry;
	case kAnyNode: return kAnyState;
	case kExitNode: return kExit;
	default: return m_Controller->Layers[m_Layer].States[node].Name;
	}
}

static int NodeFromName(const AnimatorLayer& l, const std::string& name)
{
	if (name == kEntry) return AnimatorEditorWindow::kEntryNode;
	if (name == kAnyState) return AnimatorEditorWindow::kAnyNode;
	if (name == kExit) return AnimatorEditorWindow::kExitNode;
	const int s = l.FindState(name);
	return s >= 0 ? s : AnimatorEditorWindow::kNoNode;
}

int AnimatorEditorWindow::HitNode(ImVec2 screen) const
{
	const AnimatorLayer& l = m_Controller->Layers[m_Layer];
	// 위에 그려진 것(나중 것)부터
	for (int i = (int)l.States.size() - 1; i >= -3; --i)
	{
		const ImVec2 a = GraphToScreen(NodePos(i));
		const ImVec2 b = Add(a, Mul(NodeSize(i), m_Zoom));
		if (screen.x >= a.x && screen.x <= b.x && screen.y >= a.y && screen.y <= b.y)
			return i;
	}
	return kNoNode;
}

bool AnimatorEditorWindow::TransitionSegment(int index, ImVec2& a, ImVec2& b) const
{
	const AnimatorLayer& l = m_Controller->Layers[m_Layer];
	const AnimatorTransition& t = l.Transitions[index];
	const int from = NodeFromName(l, t.From), to = NodeFromName(l, t.To);
	if (from == kNoNode || to == kNoNode || from == to)
		return false;
	a = NodeCenterScreen(from);
	b = NodeCenterScreen(to);
	// 반대 방향 전이가 있으면 두 선을 옆으로 벌린다
	bool reverse = false;
	for (const auto& o : l.Transitions)
		reverse |= o.From == t.To && o.To == t.From;
	if (reverse)
	{
		const float len = Len(Sub(b, a));
		if (len > 1.0f)
		{
			const ImVec2 perp(-(b.y - a.y) / len, (b.x - a.x) / len);
			const ImVec2 off = Mul(perp, 6.0f * m_Zoom);
			a = Add(a, off);
			b = Add(b, off);
		}
	}
	return true;
}

int AnimatorEditorWindow::HitTransition(ImVec2 screen) const
{
	const AnimatorLayer& l = m_Controller->Layers[m_Layer];
	for (int i = 0; i < (int)l.Transitions.size(); ++i)
	{
		ImVec2 a, b;
		if (TransitionSegment(i, a, b) && DistToSegment(screen, a, b) < 6.0f)
			return i;
	}
	return -1;
}

void AnimatorEditorWindow::FrameAll()
{
	if (m_Controller == nullptr)
		return;
	const AnimatorLayer& l = m_Controller->Layers[m_Layer];
	ImVec2 mn(FLT_MAX, FLT_MAX), mx(-FLT_MAX, -FLT_MAX);
	for (int i = -3; i < (int)l.States.size(); ++i)
	{
		const ImVec2 p = NodePos(i), s = NodeSize(i);
		mn = ImVec2((std::min)(mn.x, p.x), (std::min)(mn.y, p.y));
		mx = ImVec2((std::max)(mx.x, p.x + s.x), (std::max)(mx.y, p.y + s.y));
	}
	m_Pan = Mul(Add(mn, mx), -0.5f);
	// 전부 보이도록 (최대 1배)
	const float fitX = (m_CanvasSize.x - 60.0f) / (std::max)(1.0f, mx.x - mn.x);
	const float fitY = (m_CanvasSize.y - 60.0f) / (std::max)(1.0f, mx.y - mn.y);
	m_Zoom = std::clamp((std::min)(fitX, fitY), 0.7f, 1.0f);
}

// ------------------------------------------------------------------ 메인
void AnimatorEditorWindow::OnRender()
{
	ResolveTarget();
	if (m_Controller)
	{
		SyncSelection();
		std::weak_ptr<AnimatorController> weak = m_Controller;
		Undo::WatchAsset("controller:" + m_Controller->Path, m_Controller->Name(),
			[weak]() { auto c = weak.lock(); return c ? c->ToJsonString() : std::string(); },
			[weak](const std::string& text) { if (auto c = weak.lock()) { c->ApplyJson(text); c->Commit(); } });
	}

	const ImVec2 pos = ImGui::GetCursorScreenPos();
	const ImVec2 avail = ImGui::GetContentRegionAvail();
	if (avail.x < 50 || avail.y < 50)
		return;

	float left = m_ShowLeft ? std::clamp(m_LeftWidth, 160.0f, avail.x - 200.0f) : 0.0f;
	if (m_ShowLeft)
	{
		DrawLeftPanel(pos, ImVec2(left, avail.y));

		// 스플리터
		ImGui::SetCursorScreenPos(ImVec2(pos.x + left - 2, pos.y));
		ImGui::InvisibleButton("##animsplit", ImVec2(4, avail.y));
		if (ImGui::IsItemHovered() || ImGui::IsItemActive())
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
		if (ImGui::IsItemActive())
			m_LeftWidth = left + ImGui::GetIO().MouseDelta.x;
		ImGui::GetWindowDrawList()->AddLine(ImVec2(pos.x + left, pos.y), ImVec2(pos.x + left, pos.y + avail.y), kToolbarLine);
		left += 1.0f;
	}

	DrawGraph(ImVec2(pos.x + left, pos.y), ImVec2(avail.x - left, avail.y));
}

// ------------------------------------------------------------------ 왼쪽 패널
void AnimatorEditorWindow::DrawLeftPanel(ImVec2 pos, ImVec2 size)
{
	ImDrawList* dl = ImGui::GetWindowDrawList();
	dl->AddRectFilled(pos, Add(pos, size), kPanelBg);

	// 탭: Layers | Parameters   (오른쪽 눈 아이콘 = 패널 숨기기)
	dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + kToolbarH), kToolbarBg);
	const char* tabs[] = { "Layers", "Parameters" };
	float x = pos.x;
	for (int i = 0; i < 2; ++i)
	{
		const float w = ImGui::CalcTextSize(tabs[i]).x + 20.0f;
		ImGui::SetCursorScreenPos(ImVec2(x, pos.y));
		ImGui::PushID(i);
		if (ImGui::InvisibleButton("##tab", ImVec2(w, kToolbarH)))
		{
			m_LeftTab = i;
			m_RenameParam = m_RenameLayer = -1;
		}
		const bool hovered = ImGui::IsItemHovered();
		ImGui::PopID();
		const ImU32 bg = m_LeftTab == i ? kPanelBg : (hovered ? IM_COL32(68, 68, 68, 255) : kTabInactive);
		dl->AddRectFilled(ImVec2(x, pos.y), ImVec2(x + w, pos.y + kToolbarH), bg);
		dl->AddLine(ImVec2(x + w, pos.y), ImVec2(x + w, pos.y + kToolbarH), kToolbarLine);
		dl->AddText(ImVec2(x + 10.0f, pos.y + 3.0f), m_LeftTab == i ? kText : kTextDim, tabs[i]);
		x += w + 1.0f;
	}
	ImGui::SetCursorScreenPos(ImVec2(pos.x + size.x - 22, pos.y + 2));
	if (ImGui::InvisibleButton("##hideleft", ImVec2(18, 17)))
		m_ShowLeft = false;
	UnityGUI::DrawIcon(dl, "eye", ImVec2(pos.x + size.x - 20, pos.y + 3), 14.0f, ImGui::IsItemHovered() ? IM_COL32_WHITE : IM_COL32(180, 180, 180, 255));
	dl->AddLine(ImVec2(pos.x, pos.y + kToolbarH), ImVec2(pos.x + size.x, pos.y + kToolbarH), kToolbarLine);

	const ImVec2 bodyPos(pos.x, pos.y + kToolbarH + 1);
	const ImVec2 bodySize(size.x, size.y - kToolbarH - 1);
	ImGui::SetCursorScreenPos(bodyPos);
	ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
	ImGui::BeginChild("##animleft", bodySize, false, ImGuiWindowFlags_NoScrollbar);
	if (m_Controller)
	{
		if (m_LeftTab == 0)
			DrawLayersTab(bodyPos, bodySize);
		else
			DrawParametersTab(bodyPos, bodySize);
	}
	ImGui::EndChild();
	ImGui::PopStyleColor();
}

void AnimatorEditorWindow::DrawLayersTab(ImVec2 pos, ImVec2 size)
{
	ImDrawList* dl = ImGui::GetWindowDrawList();

	// 오른쪽 위 + (레이어 추가)
	ImGui::SetCursorScreenPos(ImVec2(pos.x + size.x - 26, pos.y + 4));
	if (ImGui::InvisibleButton("##addlayer", ImVec2(20, 18)))
	{
		m_Layer = m_Controller->AddLayer();
		m_Controller->Commit();
		m_NeedFrame = true;
	}
	UnityGUI::DrawIcon(dl, "plus", ImVec2(pos.x + size.x - 23, pos.y + 6), 14.0f, ImGui::IsItemHovered() ? IM_COL32_WHITE : IM_COL32(190, 190, 190, 255));

	static int s_CtxLayer = -1;
	float y = pos.y + 28.0f;
	for (int i = 0; i < (int)m_Controller->Layers.size(); ++i)
	{
		AnimatorLayer& layer = m_Controller->Layers[i];
		const float h = i == 0 ? 36.0f : 58.0f;
		const ImVec2 a(pos.x + 4, y), b(pos.x + size.x - 4, y + h);
		ImGui::PushID(i);
		ImGui::SetCursorScreenPos(a);
		ImGui::InvisibleButton("##layer", Sub(b, a));
		const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
		if (clicked && m_Layer != i)
		{
			m_Layer = i;
			m_SelNode = kNoNode;
			m_SelTransition = -1;
			m_NeedFrame = true;
		}
		if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
		{
			m_RenameLayer = i;
			strncpy_s(m_RenameBuf, layer.Name.c_str(), _TRUNCATE);
			m_RenameFocus = true;
		}
		if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
		{
			s_CtxLayer = i;
			ImGui::OpenPopup("##layerctx");
		}
		dl->AddRectFilled(a, b, m_Layer == i ? kRowSelected : IM_COL32(64, 64, 64, 255), 2.0f);
		dl->AddRect(a, b, kToolbarLine, 2.0f);

		if (m_RenameLayer == i)
		{
			ImGui::SetCursorScreenPos(ImVec2(a.x + 8, a.y + 8));
			ImGui::SetNextItemWidth(b.x - a.x - 16);
			if (m_RenameFocus) { ImGui::SetKeyboardFocusHere(); m_RenameFocus = false; }
			const bool done = ImGui::InputText("##rename", m_RenameBuf, sizeof(m_RenameBuf), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
			if (done || ImGui::IsItemDeactivated())
			{
				if (m_RenameBuf[0] && layer.Name != m_RenameBuf)
				{
					layer.Name = m_RenameBuf;
					m_Controller->Commit();
				}
				m_RenameLayer = -1;
			}
		}
		else
			dl->AddText(UnityGUI::BoldFont(), ImGui::GetFontSize(), ImVec2(a.x + 10, a.y + 10), kText, layer.Name.c_str());

		if (i > 0)
		{
			// Unity 는 톱니 팝업에서 Weight 를 바꾼다 — 여기서는 바로 슬라이더로
			dl->AddText(ImVec2(a.x + 10, a.y + 34), kTextDim, "Weight");
			ImGui::SetCursorScreenPos(ImVec2(a.x + 64, a.y + 32));
			ImGui::SetNextItemWidth(b.x - a.x - 74);
			ImGui::SliderFloat("##weight", &layer.Weight, 0.0f, 1.0f, "%.2f");
			if (ImGui::IsItemDeactivatedAfterEdit())
				m_Controller->Commit();
		}
		ImGui::PopID();
		y += h + 4.0f;
	}

	PushPopupStyle();
	if (ImGui::BeginPopup("##layerctx"))
	{
		if (ImGui::MenuItem("Rename") && s_CtxLayer >= 0 && s_CtxLayer < (int)m_Controller->Layers.size())
		{
			m_RenameLayer = s_CtxLayer;
			strncpy_s(m_RenameBuf, m_Controller->Layers[s_CtxLayer].Name.c_str(), _TRUNCATE);
			m_RenameFocus = true;
		}
		if (ImGui::MenuItem("Delete", nullptr, false, s_CtxLayer > 0))
		{
			m_Controller->RemoveLayer(s_CtxLayer);
			m_Controller->Commit();
			m_Layer = 0;
			m_NeedFrame = true;
		}
		ImGui::EndPopup();
	}
	PopPopupStyle();
}

void AnimatorEditorWindow::DrawParametersTab(ImVec2 pos, ImVec2 size)
{
	ImDrawList* dl = ImGui::GetWindowDrawList();
	Animator* live = LiveAnimator();

	// 검색 + [+▾]
	const float searchW = size.x - 48.0f;
	ImGui::SetCursorScreenPos(ImVec2(pos.x + 6, pos.y + 5));
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(20, 2));
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 9.0f);
	ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.16f, 0.16f, 0.16f, 1.0f));
	ImGui::SetNextItemWidth(searchW);
	ImGui::InputText("##psearch", m_Search, sizeof(m_Search));
	ImGui::PopStyleColor();
	ImGui::PopStyleVar(2);
	UnityGUI::DrawIcon(dl, "search", ImVec2(pos.x + 11, pos.y + 8), 12.0f, IM_COL32(170, 170, 170, 255));

	ImGui::SetCursorScreenPos(ImVec2(pos.x + size.x - 38, pos.y + 4));
	if (ImGui::InvisibleButton("##addparam", ImVec2(32, 19)))
		ImGui::OpenPopup("##addparampopup");
	UnityGUI::DrawIcon(dl, "plus", ImVec2(pos.x + size.x - 36, pos.y + 6), 14.0f, ImGui::IsItemHovered() ? IM_COL32_WHITE : IM_COL32(190, 190, 190, 255));
	UnityGUI::DrawIcon(dl, "dropdown", ImVec2(pos.x + size.x - 20, pos.y + 9), 9.0f, IM_COL32(190, 190, 190, 255));

	PushPopupStyle();
	if (ImGui::BeginPopup("##addparampopup"))
	{
		static const char* kTypes[] = { "Float", "Int", "Bool", "Trigger" };
		for (int t = 0; t < 4; ++t)
			if (ImGui::MenuItem(kTypes[t]))
			{
				m_RenameParam = m_Controller->AddParameter((ParamType)t);
				strncpy_s(m_RenameBuf, m_Controller->Parameters[m_RenameParam].Name.c_str(), _TRUNCATE);
				m_RenameFocus = true;
				m_Controller->Commit();
			}
		ImGui::EndPopup();
	}
	PopPopupStyle();

	float y = pos.y + 30.0f;
	dl->AddLine(ImVec2(pos.x, y - 2), ImVec2(pos.x + size.x, y - 2), kToolbarLine);
	if (m_Controller->Parameters.empty())
	{
		const char* empty = "List is Empty";
		const ImVec2 ts = ImGui::CalcTextSize(empty);
		dl->AddText(ImVec2(pos.x + (size.x - ts.x) * 0.5f, y + 8), kTextDim, empty);
	}

	static int s_CtxParam = -1;
	const float valueW = 64.0f;
	for (int i = 0; i < (int)m_Controller->Parameters.size(); ++i)
	{
		AnimatorParameter& p = m_Controller->Parameters[i];
		if (!ContainsNoCase(p.Name, m_Search) && m_RenameParam != i)
			continue;
		ImGui::PushID(i);
		const ImVec2 a(pos.x, y), b(pos.x + size.x, y + 24);

		// 이름 영역 (더블클릭 = 이름 바꾸기, 우클릭 = 메뉴)
		ImGui::SetCursorScreenPos(a);
		ImGui::InvisibleButton("##row", ImVec2(size.x - valueW - 16, 24));
		if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
		{
			m_RenameParam = i;
			strncpy_s(m_RenameBuf, p.Name.c_str(), _TRUNCATE);
			m_RenameFocus = true;
		}
		if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
		{
			s_CtxParam = i;
			ImGui::OpenPopup("##paramctx");
		}
		UnityGUI::DrawIcon(dl, "grip", ImVec2(a.x + 6, a.y + 6), 12.0f, IM_COL32(120, 120, 120, 255));

		if (m_RenameParam == i)
		{
			ImGui::SetCursorScreenPos(ImVec2(a.x + 22, a.y + 3));
			ImGui::SetNextItemWidth(size.x - valueW - 44);
			if (m_RenameFocus) { ImGui::SetKeyboardFocusHere(); m_RenameFocus = false; }
			const bool done = ImGui::InputText("##rename", m_RenameBuf, sizeof(m_RenameBuf), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
			if (done || ImGui::IsItemDeactivated())
			{
				if (m_Controller->RenameParameter(i, m_RenameBuf))
					m_Controller->Commit();
				m_RenameParam = -1;
			}
		}
		else
			dl->AddText(ImVec2(a.x + 24, a.y + 4), kText, p.Name.c_str());

		// 값 (Play 중 Live Link 면 런타임 값, 아니면 기본값)
		ImGui::SetCursorScreenPos(ImVec2(b.x - valueW - 8, a.y + 3));
		ImGui::SetNextItemWidth(valueW);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 1));
		switch (p.Type)
		{
		case ParamType::Float:
		{
			float v = live ? live->GetParameterValue(i) : p.DefaultFloat;
			if (ImGui::DragFloat("##v", &v, 0.01f, 0.0f, 0.0f, "%.3f"))
			{
				if (live) live->SetParameterValue(i, v);
				else p.DefaultFloat = v;
			}
			if (!live && ImGui::IsItemDeactivatedAfterEdit())
				m_Controller->Commit();
			break;
		}
		case ParamType::Int:
		{
			int v = live ? (int)live->GetParameterValue(i) : p.DefaultInt;
			if (ImGui::DragInt("##v", &v, 0.1f))
			{
				if (live) live->SetParameterValue(i, (float)v);
				else p.DefaultInt = v;
			}
			if (!live && ImGui::IsItemDeactivatedAfterEdit())
				m_Controller->Commit();
			break;
		}
		case ParamType::Bool:
		{
			ImGui::SetCursorScreenPos(ImVec2(b.x - 30, a.y + 3));
			bool v = live ? live->GetParameterValue(i) != 0.0f : p.DefaultBool;
			if (ImGui::Checkbox("##v", &v))
			{
				if (live) live->SetParameterValue(i, v ? 1.0f : 0.0f);
				else { p.DefaultBool = v; m_Controller->Commit(); }
			}
			break;
		}
		case ParamType::Trigger:
		{
			// Unity: 동그라미 라디오 버튼 (누르면 트리거 발동, Play 중에만 의미가 있다)
			const ImVec2 c(b.x - 21, a.y + 12);
			ImGui::SetCursorScreenPos(ImVec2(c.x - 8, c.y - 8));
			if (ImGui::InvisibleButton("##trig", ImVec2(16, 16)) && live)
				live->SetParameterValue(i, 1.0f);
			const bool on = live && live->GetParameterValue(i) != 0.0f;
			dl->AddCircleFilled(c, 7.0f, IM_COL32(42, 42, 42, 255));
			dl->AddCircle(c, 7.0f, IM_COL32(26, 26, 26, 255));
			if (on)
				dl->AddCircleFilled(c, 3.5f, IM_COL32(220, 220, 220, 255));
			break;
		}
		}
		ImGui::PopStyleVar();
		ImGui::PopID();
		y += 24.0f;
	}

	PushPopupStyle();
	if (ImGui::BeginPopup("##paramctx"))
	{
		if (ImGui::MenuItem("Rename") && s_CtxParam >= 0 && s_CtxParam < (int)m_Controller->Parameters.size())
		{
			m_RenameParam = s_CtxParam;
			strncpy_s(m_RenameBuf, m_Controller->Parameters[s_CtxParam].Name.c_str(), _TRUNCATE);
			m_RenameFocus = true;
		}
		if (ImGui::MenuItem("Delete"))
		{
			m_Controller->RemoveParameter(s_CtxParam);
			m_Controller->Commit();
			m_RenameParam = -1;
		}
		ImGui::EndPopup();
	}
	PopPopupStyle();
}

// ------------------------------------------------------------------ 그래프
void AnimatorEditorWindow::DrawGraph(ImVec2 pos, ImVec2 size)
{
	ImGui::SetCursorScreenPos(pos);
	ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
	ImGui::BeginChild("##animgraph", size, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMove);
	ImDrawList* dl = ImGui::GetWindowDrawList();
	ImGuiIO& io = ImGui::GetIO();

	const ImVec2 canvasPos(pos.x, pos.y + kToolbarH);
	const ImVec2 canvasSize(size.x, size.y - kToolbarH);
	const ImVec2 canvasEnd = Add(canvasPos, canvasSize);
	m_CanvasCenter = Add(canvasPos, Mul(canvasSize, 0.5f));
	m_CanvasSize = canvasSize;
	if (m_NeedFrame && m_Controller)
	{
		FrameAll();
		m_NeedFrame = false;
	}

	// ---- 배경 격자 ----
	dl->AddRectFilled(canvasPos, canvasEnd, kGraphBg);
	{
		const float minor = 12.0f * m_Zoom;
		if (minor > 4.0f)
		{
			const ImVec2 origin = GraphToScreen(ImVec2(0, 0));
			const float startX = canvasPos.x + fmodf(origin.x - canvasPos.x, minor) - minor;
			const float startY = canvasPos.y + fmodf(origin.y - canvasPos.y, minor) - minor;
			for (float x = startX; x < canvasEnd.x; x += minor)
				if (x >= canvasPos.x)
				{
					const int cell = (int)roundf((x - origin.x) / minor);
					dl->AddLine(ImVec2(x, canvasPos.y), ImVec2(x, canvasEnd.y), cell % 10 == 0 ? kGridMajor : kGridMinor);
				}
			for (float y = startY; y < canvasEnd.y; y += minor)
				if (y >= canvasPos.y)
				{
					const int cell = (int)roundf((y - origin.y) / minor);
					dl->AddLine(ImVec2(canvasPos.x, y), ImVec2(canvasEnd.x, y), cell % 10 == 0 ? kGridMajor : kGridMinor);
				}
		}
	}

	// ---- 입력 영역 ----
	ImGui::SetCursorScreenPos(canvasPos);
	ImGui::InvisibleButton("##canvas", canvasSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
	const bool hovered = ImGui::IsItemHovered();
	const ImVec2 mouse = io.MousePos;

	if (m_Controller == nullptr)
	{
		const char* hint = "Select an Animator or an Animator Controller";
		const ImVec2 ts = ImGui::CalcTextSize(hint);
		dl->AddText(ImVec2(m_CanvasCenter.x - ts.x * 0.5f, m_CanvasCenter.y - ts.y * 0.5f), kTextDim, hint);
	}
	else
	{
		AnimatorLayer& layer = m_Controller->Layers[m_Layer];
		Animator* live = LiveAnimator();
		const Animator::LayerRuntime* rt = live ? live->GetLayerRuntime(m_Layer) : nullptr;

		// ---- 입력 처리 ----
		if (hovered && io.MouseWheel != 0.0f)
		{
			const ImVec2 before = ScreenToGraph(mouse);
			m_Zoom = std::clamp(m_Zoom * (io.MouseWheel > 0 ? 1.1f : 1.0f / 1.1f), 0.25f, 1.5f);
			const ImVec2 after = ScreenToGraph(mouse);
			m_Pan = Add(m_Pan, Sub(after, before));
		}
		if (hovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Middle) || (io.KeyAlt && ImGui::IsMouseClicked(ImGuiMouseButton_Left))))
			m_Drag = DragMode::Pan;

		if (m_Drag == DragMode::Connect)
		{
			if (ImGui::IsKeyPressed(ImGuiKey_Escape) || (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)))
				m_Drag = DragMode::None;
			else if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			{
				const int target = HitNode(mouse);
				const bool valid = target >= 0 || (target == kExitNode && m_ConnectFrom >= 0);
				if (valid)
				{
					const int t = m_Controller->AddTransition(m_Layer, NodeName(m_ConnectFrom), NodeName(target));
					m_Controller->Commit();
					SelectTransition(t);
				}
				m_Drag = DragMode::None;
			}
		}
		else if (m_Drag == DragMode::None && hovered && !io.KeyAlt && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
		{
			const int node = HitNode(mouse);
			if (node != kNoNode)
			{
				SelectNode(node);
				m_Drag = DragMode::Node;
				m_DragNode = node;
				m_DragOffset = Sub(ScreenToGraph(mouse), NodePos(node));
				m_DragMoved = false;
			}
			else
			{
				const int t = HitTransition(mouse);
				if (t >= 0)
					SelectTransition(t);
				else
					SelectNode(kNoNode);
			}
		}

		if (m_Drag == DragMode::Pan)
		{
			m_Pan = Add(m_Pan, Mul(io.MouseDelta, 1.0f / m_Zoom));
			if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle) && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
				m_Drag = DragMode::None;
		}
		else if (m_Drag == DragMode::Node)
		{
			if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
			{
				if (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f)
				{
					SetNodePos(m_DragNode, Sub(ScreenToGraph(mouse), m_DragOffset));
					m_DragMoved = true;
				}
			}
			else
			{
				if (m_DragMoved)
					m_Controller->Commit();
				m_Drag = DragMode::None;
			}
		}

		// 우클릭 메뉴
		if (m_Drag == DragMode::None && hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
		{
			m_ContextNode = HitNode(mouse);
			m_ContextTransition = m_ContextNode == kNoNode ? HitTransition(mouse) : -1;
			m_ContextGraphPos = ScreenToGraph(mouse);
			if (m_ContextNode != kNoNode) SelectNode(m_ContextNode);
			else if (m_ContextTransition >= 0) SelectTransition(m_ContextTransition);
			ImGui::OpenPopup("##animgraphctx");
		}

		// 키보드
		if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !io.WantTextInput)
		{
			if (ImGui::IsKeyPressed(ImGuiKey_Delete))
				DeleteSelection();
			if (ImGui::IsKeyPressed(ImGuiKey_F) && !io.KeyCtrl)
				FrameAll();
		}

		dl->PushClipRect(canvasPos, canvasEnd, true);

		// ---- 전이 ----
		const int defaultState = layer.FindState(layer.DefaultState);
		if (defaultState >= 0)
			DrawArrowLine(dl, NodeCenterScreen(kEntryNode), NodeCenterScreen(defaultState), kLineDefault, m_Zoom, 1);
		for (int i = 0; i < (int)layer.Transitions.size(); ++i)
		{
			const AnimatorTransition& t = layer.Transitions[i];
			// 같은 방향 전이는 한 번만 그리고 (여러 개면 화살표 3개), 선택은 그 묶음 전체에 표시
			bool first = true;
			int count = 0;
			bool selected = false, active = false;
			for (int j = 0; j < (int)layer.Transitions.size(); ++j)
				if (layer.Transitions[j].From == t.From && layer.Transitions[j].To == t.To)
				{
					if (j < i) first = false;
					++count;
					selected |= j == m_SelTransition;
					active |= rt && rt->ActiveTransition == j;
				}
			if (!first)
				continue;
			const int fromNode = NodeFromName(layer, t.From), toNode = NodeFromName(layer, t.To);
			if (fromNode != kNoNode && fromNode == toNode)
			{
				// 자기 자신으로의 전이: 노드 오른쪽의 고리
				const ImVec2 p = GraphToScreen(NodePos(fromNode));
				const ImVec2 s = Mul(NodeSize(fromNode), m_Zoom);
				const ImVec2 a(p.x + s.x, p.y + s.y * 0.3f), b(p.x + s.x, p.y + s.y * 0.7f);
				const float r = 30.0f * m_Zoom;
				dl->AddBezierCubic(a, ImVec2(a.x + r, a.y - r * 0.6f), ImVec2(b.x + r, b.y + r * 0.6f), b, selected ? kSelect : kLine, (std::max)(1.0f, 2.0f * m_Zoom));
				continue;
			}
			ImVec2 a, b;
			if (!TransitionSegment(i, a, b))
				continue;
			const bool muted = t.Mute;
			ImU32 col = selected ? kSelect : (active ? kLive : (muted ? IM_COL32(200, 70, 70, 255) : kLine));
			DrawArrowLine(dl, a, b, col, m_Zoom, count > 1 ? 3 : 1);
		}

		// Make Transition 미리보기
		if (m_Drag == DragMode::Connect && m_Controller)
			DrawArrowLine(dl, NodeCenterScreen(m_ConnectFrom), mouse, kLine, m_Zoom, 1);

		// ---- 노드 ----
		ImFont* font = ImGui::GetFont();
		// 축소해도 글자는 읽을 수 있는 크기까지만 줄인다
		const float baseFont = ImGui::GetFontSize();
		const float fontSize = m_Zoom >= 0.5f ? (std::max)(baseFont * m_Zoom, (std::min)(baseFont, 12.0f)) : baseFont * m_Zoom;
		for (int n = -3; n < (int)layer.States.size(); ++n)
		{
			const ImVec2 a = GraphToScreen(NodePos(n));
			const ImVec2 b = Add(a, Mul(NodeSize(n), m_Zoom));
			if (b.x < canvasPos.x || a.x > canvasEnd.x || b.y < canvasPos.y || a.y > canvasEnd.y)
				continue;
			ImU32 fill = kNodeGrey;
			std::string label;
			switch (n)
			{
			case kEntryNode: fill = kNodeEntry; label = "Entry"; break;
			case kAnyNode: fill = kNodeAny; label = "Any State"; break;
			case kExitNode: fill = kNodeExit; label = "Exit"; break;
			default: fill = n == defaultState ? kNodeDefault : kNodeGrey; label = layer.States[n].Name; break;
			}
			const float round = 4.0f * m_Zoom;
			dl->AddRectFilled(Add(a, ImVec2(2, 3)), Add(b, ImVec2(2, 3)), IM_COL32(0, 0, 0, 70), round);
			dl->AddRectFilled(a, b, fill, round);
			// 위쪽 절반을 살짝 밝게 (Unity 노드의 그라디언트 느낌)
			dl->AddRectFilled(a, ImVec2(b.x, a.y + (b.y - a.y) * 0.5f), IM_COL32(255, 255, 255, 14), round, ImDrawFlags_RoundCornersTop);
			dl->AddRect(a, b, kNodeBorder, round);
			if (n == m_SelNode)
				dl->AddRect(Sub(a, ImVec2(1, 1)), Add(b, ImVec2(1, 1)), kSelect, round, 0, 2.0f);

			if (fontSize >= 6.0f)
			{
				const ImVec2 ts = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, label.c_str());
				const ImVec4 clip(a.x + 4, a.y, b.x - 4, b.y);
				dl->AddText(font, fontSize, ImVec2((std::max)(a.x + 6, (a.x + b.x - ts.x) * 0.5f), (a.y + b.y - ts.y) * 0.5f - (rt ? 2.0f * m_Zoom : 0.0f)),
					IM_COL32(235, 235, 235, 255), label.c_str(), nullptr, 0.0f, &clip);
			}

			// Live Link: 현재 상태 / 전이 목적 상태의 진행 막대
			if (rt && n >= 0 && (n == rt->Current || n == rt->Next))
			{
				const float time = n == rt->Next ? rt->NextTime : rt->Time;
				float norm = live->GetNormalizedTime(m_Layer, n, time);
				norm = layer.States[n].Loop ? norm - floorf(norm) : std::clamp(norm, 0.0f, 1.0f);
				const ImVec2 pa(a.x + 6 * m_Zoom, b.y - 9 * m_Zoom), pb(b.x - 6 * m_Zoom, b.y - 5 * m_Zoom);
				dl->AddRectFilled(pa, pb, IM_COL32(30, 30, 30, 255));
				dl->AddRectFilled(pa, ImVec2(pa.x + (pb.x - pa.x) * norm, pb.y), kLive);
			}
		}
		dl->PopClipRect();

		// ---- 오른쪽 아래 컨트롤러 경로 ----
		std::string path = m_Controller->Path;
		std::replace(path.begin(), path.end(), '\\', '/');
		const ImVec2 ts = ImGui::CalcTextSize(path.c_str());
		dl->AddText(ImVec2(canvasEnd.x - ts.x - 10, canvasEnd.y - ts.y - 14), kTextDim, path.c_str());

		DrawGraphContextMenu();
	}

	// ---- 툴바: [눈] Base Layer >            [Auto Live Link] ----
	dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + kToolbarH), kToolbarBg);
	dl->AddLine(ImVec2(pos.x, pos.y + kToolbarH - 1), ImVec2(pos.x + size.x, pos.y + kToolbarH - 1), kToolbarLine);
	float x = pos.x + 4;
	if (!m_ShowLeft)
	{
		ImGui::SetCursorScreenPos(ImVec2(x, pos.y + 2));
		if (ImGui::InvisibleButton("##showleft", ImVec2(18, 17)))
			m_ShowLeft = true;
		UnityGUI::DrawIcon(dl, "eye", ImVec2(x + 2, pos.y + 3), 14.0f, ImGui::IsItemHovered() ? IM_COL32_WHITE : IM_COL32(180, 180, 180, 255));
		x += 24;
	}
	if (m_Controller)
	{
		// 경로 표시 (breadcrumb): 화살표 모양 버튼
		const std::string& name = m_Controller->Layers[m_Layer].Name;
		const float w = ImGui::CalcTextSize(name.c_str()).x + 22.0f;
		const float top = pos.y + 2, bottom = pos.y + kToolbarH - 3, mid = (top + bottom) * 0.5f;
		const ImVec2 pts[5] = { ImVec2(x, top), ImVec2(x + w - 7, top), ImVec2(x + w, mid), ImVec2(x + w - 7, bottom), ImVec2(x, bottom) };
		dl->AddConvexPolyFilled(pts, 5, IM_COL32(88, 88, 88, 255));
		dl->AddText(ImVec2(x + 8, pos.y + 3), kText, name.c_str());
	}
	const float liveW = ImGui::CalcTextSize("Auto Live Link").x + 16.0f;
	if (ToolbarButton(dl, "##autolive", "Auto Live Link", ImVec2(pos.x + size.x - liveW - 4, pos.y), liveW, m_AutoLiveLink))
		m_AutoLiveLink = !m_AutoLiveLink;

	ImGui::EndChild();
	ImGui::PopStyleColor();
}

void AnimatorEditorWindow::DrawGraphContextMenu()
{
	PushPopupStyle();
	if (ImGui::BeginPopup("##animgraphctx"))
	{
		AnimatorLayer& layer = m_Controller->Layers[m_Layer];
		if (m_ContextNode == kNoNode && m_ContextTransition < 0)
		{
			if (ImGui::BeginMenu("Create State"))
			{
				if (ImGui::MenuItem("Empty"))
				{
					const ImVec2 p = Sub(m_ContextGraphPos, ImVec2(kStateW * 0.5f, kStateH * 0.5f));
					const int s = m_Controller->AddState(m_Layer, "New State", roundf(p.x), roundf(p.y));
					m_Controller->Commit();
					SelectNode(s);
				}
				ImGui::MenuItem("From Selected Clip", nullptr, false, false);
				if (ImGui::MenuItem("From New Blend Tree"))
				{
					const ImVec2 p = Sub(m_ContextGraphPos, ImVec2(kStateW * 0.5f, kStateH * 0.5f));
					const int s = m_Controller->AddState(m_Layer, "Blend Tree", roundf(p.x), roundf(p.y));
					m_Controller->MakeBlendTree(m_Layer, s);
					m_Controller->Commit();
					SelectNode(s);
				}
				ImGui::EndMenu();
			}
			ImGui::MenuItem("Create Sub-State Machine", nullptr, false, false);
			ImGui::Separator();
			if (ImGui::MenuItem("Frame All", "F"))
				FrameAll();
		}
		else if (m_ContextNode != kNoNode)
		{
			const bool canConnect = m_ContextNode != kExitNode;
			if (ImGui::MenuItem("Make Transition", nullptr, false, canConnect))
			{
				m_Drag = DragMode::Connect;
				m_ConnectFrom = m_ContextNode;
			}
			if (m_ContextNode >= 0)
			{
				const bool isDefault = layer.DefaultState == layer.States[m_ContextNode].Name;
				if (ImGui::MenuItem("Set as Layer Default State", nullptr, false, !isDefault))
				{
					layer.DefaultState = layer.States[m_ContextNode].Name;
					m_Controller->Commit();
				}
				ImGui::Separator();
				if (ImGui::MenuItem("Delete"))
				{
					m_SelNode = m_ContextNode;
					m_SelTransition = -1;
					DeleteSelection();
				}
			}
		}
		else if (m_ContextTransition >= 0)
		{
			if (ImGui::MenuItem("Delete"))
			{
				m_SelNode = kNoNode;
				m_SelTransition = m_ContextTransition;
				DeleteSelection();
			}
		}
		ImGui::EndPopup();
	}
	PopPopupStyle();
}
