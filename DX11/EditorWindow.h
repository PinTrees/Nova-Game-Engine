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
	virtual void PushStyle() {}
	virtual void OnRender() {}
	virtual void PopStyle() {}
};

