#pragma once
#include "Component.h"

class UMaterial;

// Unity URP 의 Decal Projector: 상자 안의 표면 (벽 · 바닥 · 지형 · 캐릭터) 에 재질을 투영한다 — 총알 자국 · 얼룩 · 표지 · 발자국.
//  - 투영 방향 = GameObject 의 앞 (+Z), 상자 = Width × Height × Projection Depth, Pivot = 상자 중심 (기본 (0, 0, 깊이/2) — 투영기 앞에 상자)
//  - 재질: 보통 재질 (Lit / Unlit — Base Map × Base Color (알파 = 불투명), Normal Map, Metallic · Smoothness, Emission) 또는
//    Shader Graph 의 Material = Decal 그래프. 빛 · 그림자 · 안개는 엔진 Lit 과 같다
//  - UV Scale / Offset, Opacity (Fade Factor), Angle Fade (투영기를 마주보는 면만 — 비스듬할수록 흐리게), Draw Distance + Start Fade
//  - 그리기는 DecalRenderer (불투명 다음, 하늘 · 투명 · 물 · 입자 전 — 투명한 것에는 안 묻는다)
class NOVA_API DecalProjector : public Component
{
public:
	DecalProjector();
	~DecalProjector() override;

	// 지금 있는 모든 Decal Projector (DecalRenderer 가 훑는다)
	static const std::vector<DecalProjector*>& All();

	UMaterial* GetMaterial();
	const Vec3& GetSize() const { return m_Size; }
	const Vec3& GetPivot() const { return m_Pivot; }
	const Vec2& GetUVScale() const { return m_UVScale; }
	const Vec2& GetUVOffset() const { return m_UVOffset; }
	float GetFadeFactor() const { return m_FadeFactor; }
	float GetDrawDistance() const { return m_DrawDistance; }
	float GetStartFade() const { return m_StartFade; }
	const Vec2& GetAngleFade() const { return m_AngleFade; }   // 도 (시작, 끝). 0 · 180 = 없음
	bool IsEnabled() const { return m_Enabled; }
	// 단위 상자 (-0.5..0.5) → 월드
	Matrix BoxMatrix() const;

	void OnInspectorGUI() override;
	bool UsesUnityInspector() const override { return true; }
	const char* InspectorIconName() const override { return "material_ball"; }
	void OnDrawGizmos() override;

private:
	std::wstring m_MaterialPath;
	std::shared_ptr<UMaterial> m_Material;
	bool m_MaterialLoaded = false;
	Vec3 m_Size = Vec3(1, 1, 1);         // Width, Height, Projection Depth
	Vec3 m_Pivot = Vec3(0, 0, 0.5f);
	Vec2 m_UVScale = Vec2(1, 1);
	Vec2 m_UVOffset = Vec2(0, 0);
	float m_FadeFactor = 1.0f;           // Opacity
	float m_DrawDistance = 1000.0f;
	float m_StartFade = 0.9f;            // Draw Distance 의 이 비율부터 흐려짐
	Vec2 m_AngleFade = Vec2(0, 180);

	GENERATE_COMPONENT_BODY(DecalProjector)
};
REGISTER_COMPONENT(DecalProjector)
