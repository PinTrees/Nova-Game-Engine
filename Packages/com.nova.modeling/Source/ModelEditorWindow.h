#pragma once
#include "EditorWindow.h"
#include "ModelRaster.h"

// Window > Model Editor — Blender 처럼 가볍게 모델링하는 창.
//  - 왼쪽: 뷰포트 (CPU 래스터 그림 + 마우스 · Blender 단축키), 오른쪽: Outliner · 속성 · Last Operation
//  - Object 모드 (오브젝트 고르기 · 옮기기) / Edit 모드 (Tab: 점 · 변 · 면)
//  - 단축키: G R S (+ X Y Z, 숫자 입력), E 돌출, I Inset, Ctrl+R Loop Cut, Ctrl+B Bevel, X 지우기, M 합치기, F 채우기,
//    A 모두, Alt+A 해제, Ctrl+I 반전, L 연결, Shift+D 복제, 1 2 3 고르기 모드, 숫자 패드 시점, F/Home 맞추기, Ctrl+Z 되돌리기
//  - 문서 · 연산은 CLI (nova model …) 와 같다 (ModelOps)
class ModelEditorWindow
	: public EditorWindow
{
public:
	ModelEditorWindow();
	~ModelEditorWindow();

	static ModelEditorWindow* Instance() { return s_Instance; }
	static void Focus();

	void Update() override;
	void OnRender() override;
	Modeling::ViewCamera& Camera() { return m_Cam; }
	void FrameAll();

protected:
	ImGuiWindowFlags ExtraWindowFlags() const override { return ImGuiWindowFlags_MenuBar; }
	ImVec2 WindowPaddingOverride() const override { return ImVec2(0.0f, 0.0f); }
	// 처음 열 때 (배치 파일에 자리가 없으면) Scene 탭 옆에 붙인다
	void BeforeBegin() override;

private:
	static ModelEditorWindow* s_Instance;

	// ---- 그림
	Modeling::Raster m_Raster;
	Modeling::ViewCamera m_Cam;
	Modeling::RasterOptions m_Opt;
	ComPtr<GfxTexture2D> m_Texture;
	ComPtr<GfxShaderResourceView> m_Srv;
	int m_TexW = 0, m_TexH = 0;
	uint64 m_DrawnRevision = 0;
	bool m_Dirty = true;
	ImVec2 m_ViewPos = ImVec2(0, 0), m_ViewSize = ImVec2(0, 0);
	bool m_FirstFrame = true;

	// ---- 끌기 연산 (G R S, 돌출 뒤 이동, Inset, Bevel)
	enum class Modal { None, Grab, Rotate, Scale, Inset, Bevel, LoopCut, Box };
	Modal m_Modal = Modal::None;
	int m_Axis = -1;                     // 축 제한 (0 x, 1 y, 2 z), -1 = 없음
	bool m_CustomAxis = false;           // 돌출: 법선 방향으로만
	Vec3 m_AxisDir = Vec3(0, 1, 0);
	Vec3 m_Pivot = Vec3(0, 0, 0);        // 월드
	ImVec2 m_StartMouse = ImVec2(0, 0);
	std::vector<Vec3> m_StartPositions;  // Edit: 활성 메시 점 (로컬), Object: 위치
	std::vector<Quaternion> m_StartRotations;
	std::vector<Vec3> m_StartScales;
	std::string m_ModalOp;               // 끝나면 Last Operation 으로 남길 연산 이름
	nlohmann::json m_ModalArgs;
	std::string m_Numeric;               // 끄는 중 숫자 입력 (Blender 처럼)
	int m_LoopCuts = 1;
	ImVec2 m_BoxStart = ImVec2(0, 0);
	bool m_BoxPending = false;           // 왼쪽 버튼을 눌렀다 (움직이면 상자, 그대로 떼면 클릭)

	// ---- 패널
	float m_PanelWidth = 280.0f;
	char m_RenameBuf[128] = {};
	std::string m_Status;                // 마지막 연산 결과 · 오류
	double m_StatusTime = 0.0;

	// 그리기
	void RenderViewport();
	void UploadTexture();
	void DrawMenuBar();
	void DrawSidePanel();
	void DrawOverlay(ImDrawList* dl);

	// 입력
	void HandleViewportInput(bool hovered);
	void HandleShortcuts();
	void HandleModal();
	void BeginModal(Modal m, const std::string& op = "");
	void ApplyModal();
	void EndModal(bool confirm);
	void UpdateHover();
	void ClickSelect(bool extend);
	void BoxSelect(ImVec2 a, ImVec2 b, bool extend, bool subtract);

	// 고르기
	int PickVertex(ImVec2 mouse, float radius) const;
	int PickEdge(ImVec2 mouse, float radius);
	int PickFace(ImVec2 mouse) const;
	int PickObject(ImVec2 mouse) const;

	// 도움
	bool Run(const std::string& op, const nlohmann::json& args = nlohmann::json::object());
	void SetStatus(const std::string& s);
	ImVec2 ToView(ImVec2 screen) const { return ImVec2(screen.x - m_ViewPos.x, screen.y - m_ViewPos.y); }
	Vec3 SelectionPivot() const;
	float PixelToWorld(const Vec3& at) const;
	void FrameSelected();
	void OpenFile(bool import);
	void SaveFile(bool saveAs);
	void ExportFile(const wchar_t* ext);
};
