#pragma once
#include "Component.h"
#include "TerrainRenderer.h"

class TerrainData;

// Unity 의 Terrain 컴포넌트.
//  - TerrainData(.terraindata) 의 높이맵/스플랫맵을 LOD 렌더러로 그린다 (Scene 뷰·Game 뷰, 그림자, SSAO 모두).
//  - 위치는 Transform 의 위치만 쓴다 (Unity 지형처럼 회전/크기 무시).
//  - 편집 도구(Raise/Lower, Set Height, Smooth, Paint Texture)와 설정 UI 는 TerrainEditor 가 그린다.
class Terrain : public Component
{
public:
	// Unity 의 Shadow Casting Mode
	enum class ShadowCasting { Off, On, TwoSided, ShadowsOnly };

private:
	std::string m_DataPath;
	std::shared_ptr<TerrainData> m_Data;
	bool m_Draw = true;
	float m_PixelError = 5.0f;          // Unity 기본값
	float m_BasemapDistance = 1000.0f;  // 저장만 한다
	int m_GroupingID = 0;
	bool m_AutoConnect = true;
	bool m_ShowLodNodes = false;        // (에디터) Scene 뷰에 쿼드트리 노드 경계 표시
	ShadowCasting m_ShadowCasting = ShadowCasting::On;
	// 높이 기반 레이어 섞기 (HDRP TerrainLit 의 Height-based Blend): 레이어 경계에서 높이 맵이 높은 쪽 (돌) 이 먼저 드러난다
	bool m_HeightBlend = false;
	float m_HeightTransition = 0.2f;    // 전환 폭 (높이 0..1 기준)

	TerrainRenderer::Stats m_EditorStats, m_GameStats;

	static std::vector<Terrain*> s_Active;

public:
	Terrain();
	virtual ~Terrain();

	// ---- 데이터 ----
	void SetTerrainData(const std::string& path);
	void SetTerrainData(std::shared_ptr<TerrainData> data);
	std::shared_ptr<TerrainData> GetTerrainData() const { return m_Data; }
	const std::string& GetTerrainDataPath() const { return m_DataPath; }

	// ---- Unity API ----
	Vec3 GetPosition() const;
	// 월드 위치의 지형 높이 (Unity Terrain.SampleHeight 와 같이 지형 위치 기준 높이)
	float SampleHeight(const Vec3& worldPosition) const;
	Vec3 GetInterpolatedNormal(const Vec3& worldPosition) const;
	// 월드 광선과 지형 표면의 교차
	bool Raycast(const Vec3& origin, const Vec3& direction, float maxDistance, Vec3& hitPoint) const;
	static const std::vector<Terrain*>& GetActiveTerrains() { return s_Active; }

	// ---- 설정 ----
	bool GetDraw() const { return m_Draw; }
	void SetDraw(bool draw) { m_Draw = draw; }
	float GetPixelError() const { return m_PixelError; }
	void SetPixelError(float e) { m_PixelError = std::clamp(e, 1.0f, 200.0f); }
	float& PixelErrorRef() { return m_PixelError; }
	float& BasemapDistanceRef() { return m_BasemapDistance; }
	int& GroupingIDRef() { return m_GroupingID; }
	bool& AutoConnectRef() { return m_AutoConnect; }
	bool& DrawRef() { return m_Draw; }
	bool& ShowLodNodesRef() { return m_ShowLodNodes; }
	int& ShadowCastingRef() { return reinterpret_cast<int&>(m_ShadowCasting); }
	bool& HeightBlendRef() { return m_HeightBlend; }
	float& HeightTransitionRef() { return m_HeightTransition; }
	const TerrainRenderer::Stats& GetStats(bool editor) const { return editor ? m_EditorStats : m_GameStats; }

	// ---- 그리기 (Scene 이 부른다) ----
	virtual void Render() override;
	virtual void PrewarmStaged() override;   // 씬 스트리밍: 칠한 나무의 메시 · 위치 캐시를 바꿔 끼우기 전에
	virtual void _Editor_Render() override;
	void RenderShadow();
	void RenderShadowNormal();
	void _Editor_RenderShadowNormal();

	virtual void OnInspectorGUI() override;
	virtual void OnDrawGizmos() override;
	virtual bool UsesUnityInspector() const override { return true; }
	virtual const char* InspectorIconName() const override { return "terrain"; }

private:
	void DrawPass(TerrainRenderer::Pass pass, bool editor);

	GENERATE_COMPONENT_BODY(Terrain)
};

REGISTER_COMPONENT(Terrain)
