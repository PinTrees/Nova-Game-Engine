#pragma once

class EditorDialog; 
class EditorWindow;

class EditorGUIManager
{
	SINGLE_HEADER(EditorGUIManager)

private:
	vector<EditorWindow*> m_pEditorWindows;
	vector<EditorDialog*> m_pEditorDialogs;
	bool				  m_IsInit;
	float				  m_TopChromeHeight = 56.0f;   // 메뉴바 + 툴바 높이
	bool				  m_ResetLayout = false;
	int					  m_FocusDefaultTabs = 0;   // 기본 레이아웃 생성 직후 Scene/Project 탭을 활성화

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
	void RegisterEditorDialog(EditorDialog* dialog);
	void RemoveEditorDialog(EditorDialog* dialog);
};

 