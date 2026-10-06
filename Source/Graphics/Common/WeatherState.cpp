#include "pch.h"
#include "WeatherState.h"
#include "DayNightState.h"

namespace
{
	WeatherState s_State;
}

WeatherState& WeatherState::Get() { return s_State; }

void WeatherState::Reset()
{
	// 날씨 값만 기본으로 (엔진이 채우는 카메라 기록은 그대로)
	const WeatherState old = s_State;
	s_State = WeatherState();
	s_State.GameViewPosition = old.GameViewPosition;
	s_State.SceneViewPosition = old.SceneViewPosition;
	s_State.GameViewForward = old.GameViewForward;
	s_State.SceneViewForward = old.SceneViewForward;
	s_State.GameViewFrame = old.GameViewFrame;
	s_State.SceneViewFrame = old.SceneViewFrame;
	s_State.Frame = old.Frame;
}

bool WeatherState::AffectsLight() const
{
	return DayNightState::Get().Enabled || SunIntensity != 1.0f || AmbientIntensity != 1.0f || Flash > 0.0f || SunTint.x != 1.0f || SunTint.y != 1.0f || SunTint.z != 1.0f;
}

XMFLOAT3 WeatherState::SkyScale() const
{
	const DayNightState& d = DayNightState::Get();
	const float k = SkyBrightness * (d.Enabled ? d.SkyBrightness : 1.0f);
	return { SkyTint.x * (d.Enabled ? d.SkyTint.x : 1.0f) * k, SkyTint.y * (d.Enabled ? d.SkyTint.y : 1.0f) * k, SkyTint.z * (d.Enabled ? d.SkyTint.z : 1.0f) * k };
}

XMFLOAT3 WeatherState::AmbientScale() const
{
	const DayNightState& d = DayNightState::Get();
	const float k = (std::max)(0.0f, AmbientIntensity) * (d.Enabled ? (std::max)(0.0f, d.AmbientIntensity) : 1.0f);
	return { AmbientTint.x * (d.Enabled ? d.AmbientTint.x : 1.0f) * k, AmbientTint.y * (d.Enabled ? d.AmbientTint.y : 1.0f) * k, AmbientTint.z * (d.Enabled ? d.AmbientTint.z : 1.0f) * k };
}

void WeatherState::ApplySun(XMFLOAT4& diffuse, XMFLOAT4& specular) const
{
	// 번개는 해 방향으로 오지 않지만, 순간적으로 그림자가 생기는 밝은 빛이 장면을 비추는 느낌을 준다 (하늘 · 환경광이 주인공)
	// 낮 · 밤: 해 (밤 = 달빛) 의 세기 · 색. 번개는 그와 상관없이 더한다
	const DayNightState& d = DayNightState::Get();
	const float dk = d.Enabled ? (std::max)(0.0f, d.SunIntensity) : 1.0f;
	const XMFLOAT3 dt = d.Enabled ? d.SunTint : XMFLOAT3(1, 1, 1);
	const float k = (std::max)(0.0f, SunIntensity) * dk + Flash * 0.6f;
	diffuse.x *= SunTint.x * dt.x * k; diffuse.y *= SunTint.y * dt.y * k; diffuse.z *= SunTint.z * dt.z * k;
	specular.x *= SunTint.x * dt.x * k; specular.y *= SunTint.y * dt.y * k; specular.z *= SunTint.z * dt.z * k;
}

void WeatherState::ApplyAmbient(XMFLOAT4& v) const
{
	const XMFLOAT3 a = AmbientScale();
	v.x = v.x * a.x + Flash * 0.9f;
	v.y = v.y * a.y + Flash * 0.95f;
	v.z = v.z * a.z + Flash * 1.1f;
	v.w = v.w + Flash * 0.5f;   // 반사의 어두움 · 잿빛은 32 의 WeatherSkyGrade (하늘과 같은 값)
}
