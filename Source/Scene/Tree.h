#pragma once
#include "Component.h"
#include "TreeGenerator.h"

// NOVA 나무 (Unity 의 Tree / SpeedTree 에 해당하는 우리 엔진 고유 컴포넌트).
//  - TreeParams(모양)로 메시를 절차 생성한다. 같은 모양은 메시를 같이 쓴다 (캐시)
//  - 수피·잎은 텍스처 없이 셰이더 식으로 그린다 (44. TreeCommon.fx): 텍스처 메모리/대역폭 0, 해상도 제한 없음
//  - 바람: 줄기 → 1차 가지 → 2차 가지 → 잎 떨림 계층 흔들림
//  - 본 패스 / 그림자 / SSAO 깊이 패스 모두 같은 위치 식을 쓴다
class Tree : public Component
{
public:
	struct GpuMesh;

	// ---- 모양 (바꾸면 다시 생성)
	TreeParams Params;
	int Preset = 0;

	// ---- 수피 (감마 색)
	XMFLOAT4 BarkColor = { 0.42f, 0.34f, 0.27f, 1.0f };
	XMFLOAT4 MossColor = { 0.33f, 0.42f, 0.16f, 1.0f };
	float Moss = 0.35f;
	float RidgeDepth = 0.6f;
	float RidgeFrequency = 16.0f;
	float BarkSmoothness = 0.15f;
	float BarkFleck = 0.2f;

	// ---- 잎 (감마 색)
	XMFLOAT4 LeafColor = { 0.26f, 0.45f, 0.12f, 1.0f };
	XMFLOAT4 LeafColor2 = { 0.47f, 0.56f, 0.14f, 1.0f };
	float LeafVariation = 0.6f;
	float LeafTransmission = 0.6f;
	float LeafSmoothness = 0.35f;
	float LeafLength = 0.34f;   // 카드 안 잎 길이 (카드 비율)

	// ---- 바람
	float WindStrength = 0.5f;
	float WindDirection = 30.0f;   // 도 (Y 축 회전, 0 = +Z)
	float TrunkSway = 0.25f;       // m (나무 높이 기준)
	float BranchSway = 0.2f;
	float LeafFlutter = 0.03f;

	// ---- 그리기
	bool CastShadows = true;

	Tree();
	virtual ~Tree();

	void ApplyPreset(int preset);   // 모양 + 색을 프리셋 값으로
	void MarkDirty() { m_Dirty = true; }

	// 로컬 범위 / 광선 검사 (Scene 뷰 선택·F 포커스)
	bool GetLocalBounds(Vec3& bmin, Vec3& bmax);
	bool RaycastLocal(const Vec3& origin, const Vec3& dir, float& t);
	int VertexCount();
	int TriangleCount();

	// 매 프레임 한 번 (App 루프): 바람 시간 — 같은 프레임의 모든 패스가 같은 값을 써야 깊이가 맞는다
	static void UpdateAll();

	// ---- 그리기 (Scene 이 부른다)
	virtual void Render() override;
	virtual void _Editor_Render() override;
	void RenderShadow();
	void RenderShadowNormal();
	void _Editor_RenderShadowNormal();

	virtual void OnInspectorGUI() override;
	virtual bool UsesUnityInspector() const override { return true; }
	virtual const char* InspectorIconName() const override { return "terrain_trees"; }

private:
	enum class Pass { Main, Shadow, NormalDepth };
	void EnsureMesh();
	void DrawPass(Pass pass, bool editor);

	std::shared_ptr<GpuMesh> m_Mesh;
	std::string m_MeshKey;
	bool m_Dirty = true;

	static float s_WindTime;

	GENERATE_COMPONENT_BODY(Tree)
};

REGISTER_COMPONENT(Tree)
