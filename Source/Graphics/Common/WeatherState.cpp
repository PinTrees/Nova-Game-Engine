#include "pch.h"
#include "WeatherState.h"

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
	return SunIntensity != 1.0f || AmbientIntensity != 1.0f || Flash > 0.0f || SunTint.x != 1.0f || SunTint.y != 1.0f || SunTint.z != 1.0f;
}

void WeatherState::ApplySun(XMFLOAT4& diffuse, XMFLOAT4& specular) const
{
	// 번개는 해 방향으로 오지 않지만, 순간적으로 그림자가 생기는 밝은 빛이 장면을 비추는 느낌을 준다 (하늘 · 환경광이 주인공)
	const float k = (std::max)(0.0f, SunIntensity) + Flash * 0.6f;
	diffuse.x *= SunTint.x * k; diffuse.y *= SunTint.y * k; diffuse.z *= SunTint.z * k;
	specular.x *= SunTint.x * k; specular.y *= SunTint.y * k; specular.z *= SunTint.z * k;
}

void WeatherState::ApplyAmbient(XMFLOAT4& v) const
{
	const float k = (std::max)(0.0f, AmbientIntensity);
	v.x = v.x * AmbientTint.x * k + Flash * 0.9f;
	v.y = v.y * AmbientTint.y * k + Flash * 0.95f;
	v.z = v.z * AmbientTint.z * k + Flash * 1.1f;
	v.w = v.w + Flash * 0.5f;   // 반사의 어두움 · 잿빛은 32 의 WeatherSkyGrade (하늘과 같은 값)
}
