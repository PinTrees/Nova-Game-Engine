#pragma once


class EditorWindow
{
private:
	string m_WindowTitleName;
	string m_Icon; 

	bool m_IsDocked; 
	bool m_IsOpened = true;
	ImVec2 dockedPos;
	ImVec2 m_FloatPos, m_FloatSize;   // RequestFloat
	int m_FloatFrames = 0;
	bool m_OwnViewport = false;       // 메인 창 안에 있어도 합치지 않고 따로 OS 창 (NoAutoMerge)

public:
	EditorWindow(const string& name, const string& icon="");  
	~EditorWindow();

	void Render();
	virtual void Update() {}

	bool GetIsOpened() const { return m_IsOpened; }
	void SetIsOpened(bool opened) { m_IsOpened = opened; }
	const string& GetTitle() const { return m_WindowTitleName; }
	string GetImGuiName() const;
	// 도킹에서 떼어 따로 떠 있는 OS 창으로 (메인 창 위치 기준 pos, 크기 size) — CLI nova window <이름> --float
	void RequestFloat(const ImVec2& pos, const ImVec2& size) { m_FloatPos = pos; m_FloatSize = size; m_FloatFrames = 3; m_OwnViewport = true; }

protected:
	// ImGui::Begin 직전 (SetNextWindowSize/DockID/Focus 등을 창이 직접 정할 때)
	virtual void BeforeBegin() {}
	virtual ImGuiWindowFlags ExtraWindowFlags() const { return 0; }
	virtual ImVec2 WindowPaddingOverride() const { return ImVec2(-1.0f, -1.0f); }   // x < 0 = 기본 여백
	virtual void PushStyle() {}
	virtual void OnRender() {}
	virtual void PopStyle() {}
};

