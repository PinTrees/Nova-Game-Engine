#pragma once

class EditorDialog
{
private:
	string m_WindowName;

	bool m_WindowOpen = true;			// 창 열림 상태 변수
	bool m_PreviousWindowOpen = true;	// 이전 창 열림 상태 변수

public:
	EditorDialog(const std::string& name); 
	~EditorDialog();

	void Render();
	virtual void Show();
	virtual void Close();

	virtual void Update() {}

protected:
	virtual void OnRender() {}

private:
	void PushStyle();
	void PopStyle();

	void PushStyle_TitleBar();
	void PopStyle_TitleBar();

protected:
	void PushStyle_Body(); 
	void PopStyle_Body(); 
};

