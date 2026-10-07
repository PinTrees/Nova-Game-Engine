#pragma once

// 낮 · 밤 순환이 장면 그리기에 주는 값 (com.nova.daynight 의 Day Night Cycle 이 프레임마다 쓴다).
//  날씨 (WeatherState) 와 곱해서 쓴다 — 기본값 (Enabled = false) 이면 아무것도 바뀌지 않는다 (패키지가 없을 때).
//  해의 방향은 장면의 Directional Light 를 컴포넌트가 직접 돌린다 (그림자 · 모든 화면이 그대로 따라온다)
struct NOVA_API DayNightState
{
	bool Enabled = false;

	// ---- 빛 (날씨 값에 곱한다)
	float SunIntensity = 1.0f;          // 방향광 (해 · 달) 배율
	XMFLOAT3 SunTint = { 1, 1, 1 };
	float AmbientIntensity = 1.0f;      // 환경광 · 반사 배율
	XMFLOAT3 AmbientTint = { 1, 1, 1 };
	float SkyBrightness = 1.0f;         // 스카이박스 (큐브맵) 밝기
	XMFLOAT3 SkyTint = { 1, 1, 1 };
	XMFLOAT3 FogTint = { 1, 1, 1 };     // 장면 · 날씨 안개 색에 곱한다

	// ---- 하늘 (21. Sky.fx): 큐브맵 위에 그리는 그라데이션 · 빛 · 별
	XMFLOAT3 SunDirection = { 0, 1, 0 };   // 해가 있는 쪽 (단위, 달 = 반대)
	XMFLOAT4 Zenith = { 0, 0, 0, 0 };      // rgb 천정 색, a = 그라데이션 섞기 (0 = 큐브맵 그대로)
	XMFLOAT3 Horizon = { 0, 0, 0 };        // 지평 색
	XMFLOAT4 Glow = { 0, 0, 0, 0 };        // 해 쪽 지평 빛 (새벽 · 노을) rgb, a = 세기
	XMFLOAT4 SunDisk = { 0, 0, 0, 0 };     // 해 원반 rgb, a = 세기
	float Stars = 0.0f;                    // 별 밝기
	float MilkyWay = 0.0f;                 // 은하수 밝기
	float Moon = 0.0f;                     // 달 원반 세기
	float Time = 0.0f;                     // 별 반짝임 시계 (초)

	// ---- 반사 프로브 (ReflectionProbes): 시각이 ProbeRefreshMinutes (게임 분) 넘게 흐를 때마다 프로브를 다시 찍는다
	//  (Baked · Custom 도 실행 중의 큐브로 — 파일은 그대로). 0 = 안 함. ProbeRefreshSerial 이 바뀌면 바로 다시
	float TimeOfDay = 12.0f;               // 시 (0 ~ 24)
	float ProbeRefreshMinutes = 0.0f;
	uint32 ProbeRefreshSerial = 0;

	static DayNightState& Get();
	static void Reset();                   // 패키지를 내릴 때 · Day Night Cycle 이 사라지거나 꺼질 때
};
