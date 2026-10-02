#pragma once
#include "EditorWindow.h"
#include "Anim2DRaster.h"

// Window > 2D Animator — Spine 처럼 2D 뼈대 애니메이션을 만드는 창.
//  - 위: Setup / Animate 모드, 도구 (R 회전 · T 이동 · S 크기 · B 본 만들기), 그림 추가
//  - 가운데: 뷰포트 (휠 = 확대, 가운데 · 오른쪽 끌기 = 화면 이동, 클릭 = 본 · 그림 고르기, 끌기 = 도구), Project 의 PNG 를 끌어 놓으면 고른 본에 그림
//  - 오른쪽: 본 · 슬롯 나무, 고른 것의 값, 아래: 타임라인 (애니메이션 · 재생 · 시각 · 키 · Auto Key)
//  - 문서 · 연산은 CLI (nova anim2d …) 와 같다 (Anim2DOps)
class Anim2DWindow : public EditorWindow
{
public:
	Anim2DWindow();
	~Anim2DWindow();
	static Anim2DWindow* Instance() { return s_Instance; }
	static void Focus();
	void Update() override;
	void OnRender() override;
	void FrameAll();
	Anim2D::RenderOptions& ViewOptions() { return m_Opt; }

protected:
	ImGuiWindowFlags ExtraWindowFlags() const override { return ImGuiWindowFlags_MenuBar; }
	ImVec2 WindowPaddingOverride() const override { return ImVec2(0.0f, 0.0f); }
	void BeforeBegin() override;

private:
	static Anim2DWindow* s_Instance;
	enum class Tool { Rotate, Translate, Scale, CreateBone };

	Anim2D::Raster m_Raster;
	Anim2D::View2D m_View;
	Anim2D::RenderOptions m_Opt;
	ComPtr<GfxTexture2D> m_Texture;
	ComPtr<GfxShaderResourceView> m_Srv;
	int m_TexW = 0, m_TexH = 0;
	uint64 m_DrawnRevision = 0;
	bool m_Dirty = true, m_FirstFrame = true;
	ImVec2 m_ViewPos = ImVec2(0, 0), m_ViewSize = ImVec2(0, 0);

	Tool m_Tool = Tool::Rotate;
	bool m_Dragging = false, m_PressPending = false;
	ImVec2 m_PressMouse = ImVec2(0, 0);
	float m_StartWorld[2] = { 0, 0 };
	float m_StartValue[5] = {};          // x y rot sx sy (드래그 시작)
	float m_StartAngle = 0, m_StartDist = 1;
	bool m_AutoKey = true;
	bool m_Playing = false;
	int m_Curve = 0;
	float m_PanelWidth = 290.0f, m_TimelineHeight = 150.0f;
	char m_NewAnim[64] = "idle";
	char m_Rename[64] = {};
	std::string m_Status;
	double m_StatusTime = 0;

	bool Run(const std::string& op, const nlohmann::json& args = nlohmann::json::object());
	void SetStatus(const std::string& s) { m_Status = s; m_StatusTime = ImGui::GetTime(); }
	void RenderViewport();
	void UploadTexture();
	void DrawMenuBar();
	void DrawToolbar();
	void DrawTree();
	void DrawProperties();
	void DrawTimeline(float height);
	void HandleViewport(bool hovered);
	void HandleShortcuts();
	int PickBone(ImVec2 mouse) const;
	void BeginDrag();
	void ApplyDrag(ImVec2 mouse);
	void EndDrag();
	void OpenFile();
	void SaveFile(bool as);
	void ExportSheet();
	void AddImage(const std::string& path, const float* world);
	ImVec2 ToView(ImVec2 s) const { return ImVec2(s.x - m_ViewPos.x, s.y - m_ViewPos.y); }
};
