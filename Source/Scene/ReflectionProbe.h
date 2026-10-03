#pragma once
#include "Component.h"

// Unity 의 Reflection Probe: 한 점에서 본 주변을 큐브맵으로 찍어, 상자 (Box Size) 안의 물체가 하늘 대신 그것을 반사한다.
//  - Type: Baked (굽기 — 씬 옆 폴더의 ReflectionProbe-<n>.dds), Custom (직접 고른 큐브맵), Realtime (실행 중 다시 찍기)
//  - 상자는 월드 축 정렬 (회전 · 크기는 무시 — Unity 와 같다), 중심 = 위치 + Box Offset. 찍는 점 = GameObject 위치
//  - Blend Distance: 상자 안쪽 이 폭에서 다음 프로브 · 하늘로 섞임. Importance 가 높은 것부터, 같으면 작은 상자부터
//  - Box Projection: 반사 방향을 상자 벽에 맞춰 (방 안의 가까운 벽이 제자리에 비침)
//  - 반사 (스페큘러) 만 바꾼다 — 확산 환경광은 하늘 그대로 (Unity 도 확산은 Light Probe / 하늘)
//  - 찍기 · 필터 · 셰이더 값은 ReflectionProbes (Graphics)
class NOVA_API ReflectionProbe : public Component
{
public:
	enum class Mode { Baked = 0, Realtime = 1, Custom = 2 };          // Unity ReflectionProbeMode
	enum class Refresh { OnAwake = 0, EveryFrame = 1, ViaScripting = 2 };
	enum class TimeSlicing { AllFacesAtOnce = 0, IndividualFaces = 1, NoTimeSlicing = 2 };

	ReflectionProbe();
	~ReflectionProbe() override;

	static const std::vector<ReflectionProbe*>& All();

	Mode GetMode() const { return m_Mode; }
	Refresh GetRefreshMode() const { return m_Refresh; }
	TimeSlicing GetTimeSlicing() const { return m_TimeSlicing; }
	int GetImportance() const { return m_Importance; }
	float GetIntensity() const { return m_Intensity; }
	bool GetBoxProjection() const { return m_BoxProjection; }
	float GetBlendDistance() const { return m_BlendDistance; }
	const Vec3& GetSize() const { return m_Size; }
	const Vec3& GetCenter() const { return m_Center; }     // Box Offset
	int GetResolution() const { return m_Resolution; }
	bool GetHdr() const { return m_Hdr; }
	float GetShadowDistance() const { return m_ShadowDistance; }
	bool UsesSolidColor() const { return m_ClearFlags == 1; }
	const float* GetBackgroundColor() const { return m_Background; }
	uint32 GetCullingMask() const { return m_CullingMask; }
	float GetNearClip() const { return m_Near; }
	float GetFarClip() const { return m_Far; }
	const std::string& GetCustomCubemap() const { return m_CustomCubemap; }
	const std::string& GetBakedTexture() const { return m_BakedTexture; }
	void SetBakedTexture(const std::string& path) { m_BakedTexture = path; ++m_SettingsVersion; }
	bool IsEnabled() const { return m_Enabled; }
	bool IsActiveInHierarchy() const;
	Vec3 CapturePosition() const;
	// 영향 상자 (월드, 축 정렬)
	void Bounds(Vec3& min, Vec3& max) const;
	// 찍기 설정 (해상도 · HDR · 배경 · 마스크 · 클립 · 그림자) 이 바뀔 때마다 늘어남 → 실시간 프로브를 다시 찍는다
	uint32 SettingsVersion() const { return m_SettingsVersion; }

	void OnInspectorGUI() override;
	bool UsesUnityInspector() const override { return true; }
	const char* InspectorIconName() const override { return "scene_light"; }
	void OnDrawGizmos() override;

private:
	Mode m_Mode = Mode::Baked;
	Refresh m_Refresh = Refresh::OnAwake;
	TimeSlicing m_TimeSlicing = TimeSlicing::AllFacesAtOnce;
	int m_Importance = 1;
	float m_Intensity = 1.0f;
	bool m_BoxProjection = false;
	float m_BlendDistance = 1.0f;
	Vec3 m_Size = Vec3(10, 10, 10);
	Vec3 m_Center = Vec3(0, 0, 0);
	int m_Resolution = 128;
	bool m_Hdr = true;
	float m_ShadowDistance = 100.0f;
	int m_ClearFlags = 0;                 // 0 Skybox, 1 Solid Color
	float m_Background[4] = { 49.0f / 255.0f, 77.0f / 255.0f, 121.0f / 255.0f, 0.0f };
	uint32 m_CullingMask = 0xFFFFFFFFu;
	float m_Near = 0.3f;
	float m_Far = 1000.0f;
	std::string m_CustomCubemap;          // Custom: Assets/...dds (큐브맵)
	std::string m_BakedTexture;           // Baked: 굽기 결과 (Assets/.../<씬>/ReflectionProbe-<n>.dds)
	uint32 m_SettingsVersion = 1;

	GENERATE_COMPONENT_BODY(ReflectionProbe)
};
REGISTER_COMPONENT(ReflectionProbe)
