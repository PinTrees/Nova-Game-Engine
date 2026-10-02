#pragma once

class EditorDialog; 
class EditorWindow;

class NOVA_API EditorGUIManager
{
	SINGLE_HEADER(EditorGUIManager)

private:
	vector<EditorWindow*> m_pEditorWindows;
	vector<EditorDialog*> m_pEditorDialogs;
	bool				  m_IsInit;
	float				  m_TopChromeHeight = 56.0f;   // 메뉴바 + 툴바 높이
	bool				  m_ResetLayout = false;
	int					  m_FocusDefaultTabs = 0;   // 기본 레이아웃 생성 직후 Scene/Project 탭을 활성화
	std::string			  m_PendingTab;             // SelectTab
	int					  m_PendingTabFrames = 0;

public:
	void Init(bool hubMode = false);
	void Destroy();

	void Update();
	void RenderEditorWindows();
	void BuildDefaultLayout(ImGuiID dockspaceId, ImVec2 size);
	void DrawToolbar(float y);
	void ResetLayout() { m_ResetLayout = true; }
	void RenderAfter();
	void OnResize(Vec2 size); 

	void SetStyle_Base();

public:
	void RegisterWindow(EditorWindow* window);
	void UnregisterWindow(EditorWindow* window);   // 패키지를 내릴 때 (창은 부른 쪽이 지운다)
	EditorWindow* FindWindow(const std::string& title) const;
	// 도킹된 창의 탭을 앞으로 (몇 프레임 동안 그 노드의 선택 탭으로) — CLI nova window scene 등
	void SelectTab(const std::string& title) { m_PendingTab = title; m_PendingTabFrames = 3; }
	void RegisterEditorDialog(EditorDialog* dialog);
	void RemoveEditorDialog(EditorDialog* dialog);
};

 