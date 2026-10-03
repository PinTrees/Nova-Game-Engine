#pragma once
#include "Component.h"

// Unity 6 의 Adaptive Probe Volume (URP 의 확산 간접광) — 굽지 않고 실시간으로 계산한다.
//  - 카메라 둘레의 단계 (32 x 16 x 32 프로브, 간격 Probe Spacing x 4^단계) — 큰 씬도 비용이 같다
//  - 장면을 얇은 판으로 직교로 찍어 복셀로 (몇 프레임에 한 바퀴), 매 프레임 복셀을 직접광 · 그림자 · 프로브 빛으로 다시 비추고
//    프로브가 복셀 속으로 광선을 쏴 L1 SH 를 모은다 (시간으로 섞음) → 빛 · 물체가 바뀌면 저절로 따라간다
//  - Mode Global: 카메라를 따라 어디든, Local: 상자 (Size) 안만
//  - 셰이더 쪽은 32. InstancedBasic.fx 의 ProbeVolumeAmbient (확산 환경광을 하늘 대신), 계산은 ProbeVolumes (Graphics)
//  - 레거시 Light Probe Group · Light Probe Proxy Volume · Lighting 창 굽기는 없다 (이것만)
class NOVA_API AdaptiveProbeVolume : public Component
{
public:
	enum class Mode { Global = 0, Local = 1 };

	AdaptiveProbeVolume();
	~AdaptiveProbeVolume() override;

	static const std::vector<AdaptiveProbeVolume*>& All();

	Mode GetMode() const { return m_Mode; }
	const Vec3& GetSize() const { return m_Size; }
	float GetProbeSpacing() const { return m_ProbeSpacing; }
	int GetCascades() const { return m_Cascades; }
	int GetRaysPerProbe() const { return m_Rays; }
	float GetUpdateSpeed() const { return m_UpdateSpeed; }
	float GetIntensity() const { return m_Intensity; }
	float GetNormalBias() const { return m_NormalBias; }
	float GetViewBias() const { return m_ViewBias; }
	float GetValidityThreshold() const { return m_ValidityThreshold; }
	bool IsEnabled() const { return m_Enabled; }
	bool IsActiveInHierarchy() const;
	void Bounds(Vec3& min, Vec3& max) const;   // Local 상자 (월드, 축 정렬)

	void OnInspectorGUI() override;
	bool UsesUnityInspector() const override { return true; }
	const char* InspectorIconName() const override { return "light_point"; }
	void OnDrawGizmos() override;

private:
	Mode m_Mode = Mode::Global;
	Vec3 m_Size = Vec3(20, 10, 20);
	float m_ProbeSpacing = 1.0f;        // 가장 촘촘한 단계의 간격 (Unity 의 Min Probe Spacing 기본 1 m)
	int m_Cascades = 3;
	int m_Rays = 32;
	float m_UpdateSpeed = 0.1f;         // 새 결과의 비중 (클수록 빨리 따라가고 더 흔들림)
	float m_Intensity = 1.0f;           // Probe Volumes Options 의 Intensity Multiplier
	float m_NormalBias = 0.33f;         // Unity 기본 (미터)
	float m_ViewBias = 0.0f;
	float m_ValidityThreshold = 0.5f;   // 광선이 이 비율 넘게 뒷면에 맞으면 물체 속 프로브 (이웃 값으로 채움). 복셀의 얇은 지붕 · 벽은 한쪽 노멀만 가져
	                                    //  그 아래 프로브도 위쪽 광선 절반이 뒷면 — 0.25 (Unity 굽기 기본) 면 방 안 천장 밑 프로브가 빠진다

	GENERATE_COMPONENT_BODY(AdaptiveProbeVolume)
};
REGISTER_COMPONENT(AdaptiveProbeVolume)
