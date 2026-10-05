#pragma once

// 날씨가 장면 그리기에 주는 값 (com.nova.weather 의 Weather Controller 가 프레임마다 쓴다).
//  엔진은 이 값을 모든 화면 (Game · Scene · 프로브 찍기) 에 곱하거나 섞는다 — 기본값이면 아무것도 바뀌지 않는다 (패키지가 없을 때).
//  장면의 컴포넌트 (Light · Volume) 값을 바꾸지 않으므로 씬 파일에 날씨가 저장되지 않는다
struct NOVA_API WeatherState
{
	// ---- 빛 · 하늘 (흐림 · 폭풍)
	float SunIntensity = 1.0f;          // 방향광 (해 · 달) 배율
	XMFLOAT3 SunTint = { 1, 1, 1 };
	float AmbientIntensity = 1.0f;      // 하늘 환경광 · 반사 배율
	XMFLOAT3 AmbientTint = { 1, 1, 1 };
	float SkyBrightness = 1.0f;         // 스카이박스 밝기
	float SkyDesaturate = 0.0f;         // 0 = 그대로, 1 = 회색 (먹구름)
	XMFLOAT3 SkyTint = { 1, 1, 1 };
	float Flash = 0.0f;                 // 번개: 하늘 · 환경광 · 해에 더하는 밝기 (0 → 몇)

	// ---- 안개 (장면 Volume 의 Fog 와 섞는다)
	float FogAmount = 0.0f;             // 0 = 장면 그대로, 1 = 날씨 안개
	float FogDistance = 200.0f;         // 날씨 안개의 63 % 가려지는 거리 (m)
	XMFLOAT3 FogColor = { 0.55f, 0.58f, 0.62f };
	float FogHeight = 60.0f;            // 그 위로 옅어지는 높이 (m, 지면 기준이 아니라 월드 y)

	// ---- 바람 (나무 · 풀 · 물결 배율)
	float WindStrength = 1.0f;          // 각 에셋의 바람 세기에 곱한다 (돌풍 포함)

	// ---- 2 · 3 단계 (젖음 · 눈) — 아직 쓰지 않는다
	float Wetness = 0.0f;
	float SnowCover = 0.0f;

	// ---- 엔진이 채운다: 이번 프레임 화면들의 카메라 자리 (날씨가 따라갈 곳)
	XMFLOAT3 GameViewPosition = { 0, 0, 0 };
	XMFLOAT3 SceneViewPosition = { 0, 0, 0 };
	XMFLOAT3 GameViewForward = { 0, 0, 1 };    // 보는 방향 (번개를 앞에 치는 연출)
	XMFLOAT3 SceneViewForward = { 0, 0, 1 };
	uint64 GameViewFrame = 0, SceneViewFrame = 0;   // 마지막으로 그 화면을 그린 프레임 번호 (Frame)
	uint64 Frame = 0;

	static WeatherState& Get();
	static void Reset();                // 패키지를 내릴 때 · Weather Controller 가 사라질 때
	bool AffectsLight() const;

	// 엔진 쪽 적용 (EditorApp · Sky · AtmospherePass)
	void ApplySun(XMFLOAT4& diffuse, XMFLOAT4& specular) const;
	void ApplyAmbient(XMFLOAT4& indirect) const;   // xyz 환경광, w 반사
};
