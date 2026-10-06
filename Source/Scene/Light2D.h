#pragma once
#include "Component.h"

// Unity (URP 2D) 의 Light 2D · Shadow Caster 2D: 스프라이트 (Sprite Renderer · Tilemap · 2D 뼈대) 를 비추는 2D 빛.
//  - 씬에 켜진 Light 2D 가 하나라도 있으면 스프라이트는 빛을 받는다 (Unity 의 Sprite-Lit-Default) — 빛이 없는 곳은 어둡다 (Global Light 로 바탕 밝기)
//  - Global: 화면 전체 색 × 세기. Point (Unity 6 의 Spot): Inner/Outer Radius 사이에서 줄고, Inner/Outer Angle 로 원뿔 (360 = 원), 방향 = 오브젝트의 위 (Y)
//  - Falloff Strength: 바깥으로 갈수록 빨리 어두워진다. Normal Map Distance: 노멀 맵이 있는 스프라이트에서 빛의 높이 (클수록 평평하게)
//  - 그림자: Shadows 켠 빛 (8 개까지) 이 Shadow Caster 2D 의 모양 (2D 콜라이더 윤곽, 없으면 스프라이트 사각형) 뒤를 Shadow Strength 만큼 가린다
//  - 그리기는 SpriteBatch (Shaders/51. Sprite.fx 의 Lit 기법)
class NOVA_API Light2D : public Component
{
public:
	enum class Type { Global = 0, Point = 1 };

	Light2D();
	~Light2D() override;

	Type LightType = Type::Point;
	float Color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };   // a 는 쓰지 않는다 (Inspector 색 칸)
	float Intensity = 1.0f;
	float InnerRadius = 0.0f;
	float OuterRadius = 3.0f;
	float InnerAngle = 360.0f;
	float OuterAngle = 360.0f;
	float Falloff = 0.5f;            // Falloff Strength (0 = 고르게, 1 = 가운데만)
	bool Shadows = false;
	float ShadowStrength = 0.75f;
	float NormalMapDistance = 3.0f;
	int TargetSortingLayers = -1;    // 비트 = Sorting Layer 순서 (-1 = 모두) — 지금은 모두 비춘다

	static const std::vector<Light2D*>& All();
	bool ActiveAndEnabled() const;

	void OnDrawGizmos() override;
	void OnInspectorGUI() override;
	bool UsesUnityInspector() const override { return true; }
	const char* InspectorIconName() const override { return LightType == Type::Global ? "light_directional" : "light_spot"; }

	GENERATE_COMPONENT_BODY(Light2D)
};
REGISTER_COMPONENT(Light2D)

class NOVA_API ShadowCaster2D : public Component
{
public:
	ShadowCaster2D();
	~ShadowCaster2D() override;

	bool CastsShadows = true;
	bool SelfShadows = false;        // 켜면 자기 모양도 그림자 (꺼짐 = 빛을 받는 앞면은 밝게)

	// 월드 윤곽 (닫힌 고리들, 반시계로 맞춤): 2D 콜라이더 → 스프라이트 사각형
	bool WorldOutline(std::vector<std::vector<Vec2>>& loops, float& z) const;
	static const std::vector<ShadowCaster2D*>& All();
	bool ActiveAndEnabled() const;

	void OnDrawGizmos() override;
	void OnInspectorGUI() override;
	bool UsesUnityInspector() const override { return true; }
	const char* InspectorIconName() const override { return "scene_light"; }

	GENERATE_COMPONENT_BODY(ShadowCaster2D)
};
REGISTER_COMPONENT(ShadowCaster2D)
