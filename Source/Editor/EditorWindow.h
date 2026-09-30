#pragma once


class EditorWindow
{
private:
	string m_WindowTitleName;
	string m_Icon; 

	bool m_IsDocked; 
	bool m_IsOpened = true;
	ImVec2 dockedPos; 

public:
	EditorWindow(const string& name, const string& icon="");  
	~EditorWindow();

	void Render();
	virtual void Update() {}

	bool GetIsOpened() const { return m_IsOpened; }
	void SetIsOpened(bool opened) { m_IsOpened = opened; }
	const string& GetTitle() const { return m_WindowTitleName; }
	string GetImGuiName() const;

protected:
	// ImGui::Begin 직전 (SetNextWindowSize/DockID/Focus 등을 창이 직접 정할 때)
	virtual void BeforeBegin() {}
	virtual ImGuiWindowFlags ExtraWindowFlags() const { return 0; }
	virtual ImVec2 WindowPaddingOverride() const { return ImVec2(-1.0f, -1.0f); }   // x < 0 = 기본 여백
	virtual void PushStyle() {}
	virtual void OnRender() {}
	virtual void PopStyle() {}
};

