#pragma once
#include "Component.h"

class Scene;

// Unity 의 LOD Group: 카메라에서 본 화면 높이로 LOD 0, 1, 2 … 의 렌더러 중 하나만 그린다 (멀수록 단순한 메시).
//  - 화면 높이 (Screen Relative Height) = Object Size (월드 — 가장 큰 축 배율) / (2 · 거리 · tan(FOV / 2)), 직교 카메라는 Size / (2 · Orthographic Size)
//    거리 = 카메라 → Local Reference Point (월드). Unity 와 같은 식 (LOD Bias 1)
//  - LOD i = 화면 높이 ≥ Screen Relative Transition Height 인 첫 LOD, 마지막 LOD 보다 작으면 Culled (아무것도 안 그림)
//  - Fade Mode = Cross Fade: 전환 근처에서 두 LOD 를 디더로 섞는다 — Fade Transition Width = 그 LOD 범위 끝에서 섞는 비율,
//    Animate Cross-fading 이면 바뀌는 순간부터 0.5 초 (LODGroup.crossFadeAnimationDuration)
//  - 그림자는 더 많이 보이는 LOD 하나. Skinned Mesh Renderer 는 디더 없이 바뀐다
//  - 렌더러는 GameObject fileID 로 가리킨다 (그 GameObject 의 Mesh Renderer · Skinned Mesh Renderer — Unity 의 Renderer 목록)
//  - 뷰 (Game · Scene · 프로브 찍기) 마다 따로 고른다 → Component::LodStamp · LodHidden · LodFade (SceneCulling::IsVisible · MeshBatcher 가 따른다)
class NOVA_API LODGroup : public Component
{
public:
	struct LOD
	{
		float ScreenRelativeTransitionHeight = 0.0f;   // 0 ~ 1
		float FadeTransitionWidth = 0.0f;              // 0 ~ 1
		std::vector<uint64> Renderers;                 // GameObject fileID
	};
	enum FadeMode { FadeNone = 0, FadeCrossFade = 1 };
	static constexpr int kMaxLODs = 8;
	static constexpr float kCrossFadeAnimationDuration = 0.5f;

	LODGroup();
	~LODGroup() override;
	static const std::vector<LODGroup*>& All();

	// 뷰마다 한 번, 첫 그리기 전 (MeshBatcher 가 렌더러 목록을 모을 때): LOD 를 골라 렌더러에 숨김 · 페이드를 매긴다
	//  capture = 프로브 · GI 찍기 (애니메이션 상태를 건드리지 않고 바로 고름)
	static void SelectForView(Scene* scene, bool editor, bool capture);
	static void RegisterEditor();   // nova lod

	const std::vector<LOD>& GetLODs() const { return m_LODs; }
	void SetLODs(const std::vector<LOD>& lods);
	int GetFadeMode() const { return m_FadeMode; }
	bool GetAnimateCrossFading() const { return m_AnimateCrossFading; }
	const Vec3& GetLocalReferencePoint() const { return m_LocalReferencePoint; }
	float GetSize() const { return m_Size; }
	// 모든 LOD 렌더러를 감싸는 상자로 Local Reference Point · Size (Unity 의 RecalculateBounds)
	void RecalculateBounds();
	// 이 카메라에서의 화면 높이
	float ScreenRelativeHeight(CXMMATRIX view, CXMMATRIX proj) const;
	// 마지막 Game (false) · Scene (true) 뷰에서 고른 LOD (-1 = Culled, -2 = 아직 안 그림) 와 화면 높이
	int CurrentLOD(bool editor) const;
	float CurrentHeight(bool editor) const { return m_View[editor ? 1 : 0].Height; }

	void OnInspectorGUI() override;
	bool UsesUnityInspector() const override { return true; }
	const char* InspectorIconName() const override { return "mesh_renderer"; }
	void RemapFileIDs(const std::unordered_map<uint64, uint64>& map) override;

private:
	void DrawLODBar();
	void AddRenderers(int lod, GameObject* go);

	std::vector<LOD> m_LODs;
	int m_FadeMode = FadeNone;
	bool m_AnimateCrossFading = false;
	Vec3 m_LocalReferencePoint = Vec3(0, 0, 0);
	float m_Size = 1.0f;

	// 뷰마다 (0 = Game, 1 = Scene): 고른 LOD (LOD 수 = Culled, -2 = 아직), 애니메이션 크로스페이드 상태
	struct ViewState
	{
		int Current = -2;
		int Previous = -2;
		double FadeStart = 0.0;
		float Height = 0.0f;
	};
	ViewState m_View[2];
	int m_SelectedLOD = 0;    // Inspector 에서 고른 LOD
	int m_DragBoundary = -1;  // 끄는 중인 경계 (LOD i 의 끝)

	GENERATE_COMPONENT_BODY(LODGroup)
};
REGISTER_COMPONENT(LODGroup)
