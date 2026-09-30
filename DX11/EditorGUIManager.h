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

public:
	void Init();
	void Destroy();

	void Update();
	void RenderEditorWindows();
	void BuildDefaultLayout(ImGuiID dockspaceId, ImVec2 size);
	void RenderAfter();
	void OnResize(Vec2 size); 

	void SetStyle_Base();

public:
	void RegisterWindow(EditorWindow* window);
	void RegisterEditorDialog(EditorDialog* dialog);
	void RemoveEditorDialog(EditorDialog* dialog);
};

 