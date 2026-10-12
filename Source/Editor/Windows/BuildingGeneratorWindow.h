#pragma once
#include "EditorWindow.h"
#include "BuildingDesc.h"
#include <memory>

class Mesh;
class UMaterial;
class Building;

// Window > Building Generator: 건축물을 단계별 대화상자로 만든다 (중세 판타지 · 유럽 양식).
//  왼쪽 = 단계 (1 양식 갤러리 → 2 형태 · 크기 → 3 벽 → 4 창 · 문 → 5 지붕 → 6 장식 → 7 색 → 8 완성), 아래 Back / Next.
//  오른쪽 = 3D 미리 보기 (끌기 = 돌리기, 휠 = 확대, 땅 · 해). 값이 바뀌면 바로 다시 짓는다 (생성기 = BuildingGenerator).
//  완성: 장면에 만들기 · 고른 건물에 적용 (Live = 고른 건물을 바로 고친다) · 프리셋 (.building, Assets/Buildings) 저장 · 불러오기
class BuildingGeneratorWindow : public EditorWindow
{
public:
	BuildingGeneratorWindow();
	~BuildingGeneratorWindow();
	static BuildingGeneratorWindow* Instance() { return s_Instance; }
	// target = 이 건물의 값으로 열고 Live 로 고친다 (nullptr = 지금 값 그대로)
	static void Open(Building* target, int step = -1);
	static void RegisterEditor();
	void OnRender() override;

	// CLI · 검사
	BuildingDesc& Desc() { return m_Desc; }
	void Changed() { ++m_Revision; }
	void SetView(float yaw, float pitch, float zoom) { m_Yaw = yaw; m_Pitch = pitch; m_Zoom = zoom; }
	int Step() const { return m_Step; }

	static bool SavePreset(const std::string& name, const BuildingDesc& d, std::string& path);   // Assets/Buildings/<name>.building
	static bool LoadPreset(const std::string& relPath, BuildingDesc& d);
	static std::vector<std::string> ListPresets();   // 프로젝트 기준 경로

protected:
	ImVec2 WindowPaddingOverride() const override { return ImVec2(0.0f, 0.0f); }
	void BeforeBegin() override;

private:
	static BuildingGeneratorWindow* s_Instance;
	BuildingDesc m_Desc;
	int m_Step = 0;
	int m_FocusFrames = 0;   // 도킹된 탭이 앞으로 오도록 몇 프레임 이어서
	bool m_Live = false;          // 고른 건물을 바로 고친다
	uint64_t m_Revision = 1, m_BuiltRevision = 0, m_LiveRevision = 0;
	double m_LastBuild = 0.0;
	std::string m_Name = "Building";
	std::string m_PresetName = "My Building";
	std::string m_Message;
	float m_PlaceX = 0.0f;

	// 미리 보기
	std::shared_ptr<Mesh> m_Mesh, m_Ground;
	std::vector<std::shared_ptr<UMaterial>> m_Mats;
	std::shared_ptr<UMaterial> m_GroundMat;
	XMFLOAT3 m_Min = { 0, 0, 0 }, m_Max = { 0, 0, 0 };
	int m_Tris = 0, m_Windows = 0, m_Doors = 0;
	float m_BuildMs = 0.0f;
	float m_Yaw = -32.0f, m_Pitch = 18.0f, m_Zoom = 1.0f;
	bool m_AutoRotate = false;

	struct Target
	{
		int W = 0, H = 0;
		ComPtr<GfxTexture2D> Color, Depth;
		ComPtr<GfxRenderTargetView> Rtv;
		ComPtr<GfxShaderResourceView> Srv;
		ComPtr<GfxDepthStencilView> Dsv;
		bool Ensure(int w, int h);
	};
	Target m_View;
	// 양식 갤러리 썸네일 (양식마다 한 장, 보일 때 한 프레임에 하나씩 굽는다)
	struct Thumb { Target T; bool Done = false; };
	std::vector<Thumb> m_Thumbs;

	void Rebuild();
	void ApplyLive();
	void DrawSteps(float width, float height);
	void DrawPreview(float width, float height);
	void DrawStyleStep();
	void DrawShapeStep();
	void DrawWallsStep();
	void DrawWindowsStep();
	void DrawRoofStep();
	void DrawDetailsStep();
	void DrawColorsStep();
	void DrawFinishStep();
	void RenderTo(Target& t, const std::shared_ptr<Mesh>& mesh, const std::vector<std::shared_ptr<UMaterial>>& mats,
		const XMFLOAT3& mn, const XMFLOAT3& mx, float yaw, float pitch, float zoom);
	void EnsureGround();
	Building* SelectedBuilding() const;
};
