#pragma once
#include "EditorWindow.h"

class AnimatorController;
class Animator;
class GameObject;

// Unity 의 Animator 창.
//  - 왼쪽: Layers / Parameters 탭
//  - 오른쪽: 레이어 상태 머신 그래프 (Entry / Any State / Exit, 상태 노드, 전이 화살표)
//  - 선택한 GameObject 의 Animator 컨트롤러, 또는 Project 에서 고른 .controller 를 편집한다.
//  - Play 중에는 현재 상태의 진행 막대와 진행 중인 전이를 표시한다 (Live Link).
class AnimatorEditorWindow
	: public EditorWindow
{
public:
	static constexpr int kNoNode = -100;
	static constexpr int kEntryNode = -1;
	static constexpr int kAnyNode = -2;
	static constexpr int kExitNode = -3;

private:
	std::shared_ptr<AnimatorController> m_Controller;
	GameObject* m_Target = nullptr;      // 컨트롤러를 가져온 GameObject (Live Link 대상)
	int m_Layer = 0;
	int m_LeftTab = 0;                   // 0 = Layers, 1 = Parameters
	bool m_AutoLiveLink = true;
	bool m_ShowLeft = true;              // 눈 아이콘으로 왼쪽 패널 숨기기
	float m_LeftWidth = 240.0f;

	// 그래프 보기
	ImVec2 m_Pan = ImVec2(0, 0);
	float m_Zoom = 1.0f;
	bool m_NeedFrame = true;
	ImVec2 m_CanvasCenter = ImVec2(0, 0);
	ImVec2 m_CanvasSize = ImVec2(0, 0);

	// 선택 / 조작
	int m_SelNode = kNoNode;
	int m_SelTransition = -1;
	enum class DragMode { None, Pan, Node, Connect };
	DragMode m_Drag = DragMode::None;
	int m_DragNode = kNoNode;
	ImVec2 m_DragOffset = ImVec2(0, 0);
	bool m_DragMoved = false;
	int m_ConnectFrom = kNoNode;
	int m_ContextNode = kNoNode;
	int m_ContextTransition = -1;
	ImVec2 m_ContextGraphPos = ImVec2(0, 0);

	// Parameters 탭
	char m_Search[64] = {};
	int m_RenameParam = -1;
	int m_RenameLayer = -1;
	char m_RenameBuf[64] = {};
	bool m_RenameFocus = false;

public:
	AnimatorEditorWindow();
	~AnimatorEditorWindow();

	void Open(std::shared_ptr<AnimatorController> controller);
	// 창을 열고 앞으로 가져온다 (Project 에서 .controller 더블클릭)
	static void Focus();
	static AnimatorEditorWindow* Instance() { return s_Instance; }

private:
	static AnimatorEditorWindow* s_Instance;
public:

public:
	virtual void Update() override;
	virtual void OnRender() override;

private:
	void ResolveTarget();
	void SyncSelection();
	Animator* LiveAnimator() const;

	void DrawLeftPanel(ImVec2 pos, ImVec2 size);
	void DrawLayersTab(ImVec2 pos, ImVec2 size);
	void DrawParametersTab(ImVec2 pos, ImVec2 size);
	void DrawGraph(ImVec2 pos, ImVec2 size);
	void DrawGraphContextMenu();

	// 그래프 좌표 (노드 PosX/PosY = 왼쪽 위, 그래프 단위)
	ImVec2 GraphToScreen(ImVec2 g) const;
	ImVec2 ScreenToGraph(ImVec2 s) const;
	ImVec2 NodePos(int node) const;
	ImVec2 NodeSize(int node) const;
	ImVec2 NodeCenterScreen(int node) const;
	void SetNodePos(int node, ImVec2 p);
	std::string NodeName(int node) const;
	int HitNode(ImVec2 screen) const;
	int HitTransition(ImVec2 screen) const;
	bool TransitionSegment(int index, ImVec2& a, ImVec2& b) const;

	void SelectNode(int node);
	void SelectTransition(int index);
	void DeleteSelection();
	void FrameAll();
};
