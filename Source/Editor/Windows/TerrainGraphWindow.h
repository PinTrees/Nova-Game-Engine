#pragma once
#include "EditorWindow.h"
#include "TerrainGraph.h"
#include <atomic>
#include <memory>
#include <mutex>

namespace ax { namespace NodeEditor { struct EditorContext; } }

// Window > Terrain Graph (QuadSpinner Gaea 처럼 노드로 지형을 만든다).
//  - 왼쪽 캔버스: 노드를 잇는다 (오른쪽 클릭 · Space = 추가, Delete = 지우기). 출력 핀이 여럿 (Erosion: Out · Flow · Deposits · Wear)
//  - 오른쪽 위 미리 보기: 고른 노드 (없으면 Output) 를 미리 보기 해상도로 바로 계산 (작업 스레드) — 높이 = 음영, 마스크 = 흑백, Output = 텍스처 색
//  - 오른쪽 아래 탭: Node (값) · World (월드 · 타일 · 디테일, 메모리 어림) · Texturing (스플랫 규칙 4 칸 + 계산으로 만든 텍스처) ·
//    Build (월드 해상도로 빌드 → World Terrain 이 50 x 50 타일로 스트리밍, 장면의 Terrain 여러 개에 적용, 타일 Terrain 만들기)
class TerrainGraphWindow : public EditorWindow
{
public:
	TerrainGraphWindow();
	~TerrainGraphWindow();
	static TerrainGraphWindow* Instance() { return s_Instance; }
	static void Open(const std::string& path, int selectNode = 0, int previewPin = 0);   // selectNode = 고를 노드 (CLI · 캡처)
	static void RegisterEditor();
	void OnRender() override;

	// 계산으로 만든 레이어 텍스처 (알베도 · 노멀 · 높이 PNG + .terrainlayer) 를 그래프 옆 폴더에 만들고 규칙 4 칸에 넣는다 (CLI 도)
	// photo = 프로젝트에 텍스처 팩이 있으면 사진 PBR 재질 (규칙 이름으로 찾는다 — "Forest Floor" · "Cliff" …), 없으면 식으로 굽는 PNG
	static bool GenerateLayerTextures(const std::string& graphPath, int size, std::string& message, bool photo = true);
	// 장면에 World Terrain (이 그래프) — 없으면 Open World 로 만든다 (PCG · 그림자 · 숲 화면 포함). 만든 / 고친 GameObject 이름
	static std::string AttachToWorld(const std::string& graphPath);
	// 타일 Terrain 오브젝트 만들기 (x0, z0 = 타일 번호, count x count) + 적용
	static int CreateTerrainTiles(const std::string& graphPath, int x0, int z0, int count, int resolution, std::string& message);
	// 빌드의 강 선 → Water Body (River) 오브젝트 (World Terrain 아래 "Rivers"). 만든 수
	static int CreateRivers(const std::string& graphPath, std::string& message);

protected:
	ImGuiWindowFlags ExtraWindowFlags() const override { return ImGuiWindowFlags_MenuBar; }
	ImVec2 WindowPaddingOverride() const override { return ImVec2(0.0f, 0.0f); }
	void BeforeBegin() override;

private:
	static TerrainGraphWindow* s_Instance;
	ax::NodeEditor::EditorContext* m_Ctx = nullptr;
	std::string m_Path;
	std::shared_ptr<TerrainGraph::Graph> m_Graph;
	bool m_FocusPending = false, m_LayoutPending = true, m_SavePending = false;
	int m_Selected = 0;
	int m_SelectPending = 0;
	int m_PreviewPin = 0;
	int m_Tab = 0;
	ImVec2 m_AddAt = ImVec2(0, 0);

	// 미리 보기 (작업 스레드)
	struct Preview
	{
		std::mutex Lock;
		std::shared_ptr<TerrainGraph::EvalResult> Result;
		uint64_t ForRevision = 0;
		std::atomic<bool> Running{ false };
		std::atomic<bool> Cancel{ false };
		double Ms = 0.0;
	};
	std::shared_ptr<Preview> m_Preview;
	uint64_t m_PreviewRequested = 0;
	double m_ChangedAt = 0.0;
	ComPtr<GfxTexture2D> m_PreviewTex;
	ComPtr<GfxShaderResourceView> m_PreviewSrv;
	int m_PreviewTexRes = 0;
	uint64_t m_ImageKey = 0;
	// 3D 미리 보기 (Gaea 뷰포트처럼 — CPU 광선 행진, 작업 스레드): 끌기 = 돌리기, 휠 = 확대
	bool m_View3D = true;
	float m_Yaw = 215.0f, m_Pitch = 32.0f, m_Zoom = 1.0f;
	struct View3D
	{
		std::mutex Lock;
		std::vector<uint8_t> Image;
		int W = 0, H = 0;
		uint64_t ForKey = 0;
		std::atomic<bool> Running{ false };
	};
	std::shared_ptr<View3D> m_3D = std::make_shared<View3D>();
	std::shared_ptr<TerrainGraph::Map> m_3DHeight;
	std::shared_ptr<std::vector<uint8_t>> m_3DAlbedo;
	uint64_t m_3DRequested = 0, m_3DUploaded = 0, m_3DSource = 0;
	ComPtr<GfxTexture2D> m_3DTex;
	ComPtr<GfxShaderResourceView> m_3DSrv;
	void Update3D(int w, int h);
	// 타일 만들기 값
	int m_TileX = 24, m_TileZ = 24, m_TileCount = 2, m_TileRes = 1025;
	std::string m_Message;

	void DrawCanvas(float width, float height);
	void DrawSide(float width, float height);
	void DrawNodeTab();
	void DrawWorldTab();
	void DrawTexturingTab();
	void DrawBuildTab();
	void DrawNode(TerrainGraph::Node& n);
	void UpdatePreview();
	void Save();
	void Changed();
};
