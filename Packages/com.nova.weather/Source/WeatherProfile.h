#pragma once
#include <string>
#include <vector>

// 날씨 한 가지의 값 (Weather Profile). 기본 프로필 (Clear · Cloudy · Rain · Storm · Snow · Blizzard) 또는 .weather 파일 (같은 JSON).
//  Weather Controller 가 두 프로필 사이를 시간에 걸쳐 섞는다 (값마다 선형)
struct WeatherParams
{
	float Rain = 0.0f;              // 0..1 비 세기
	float Snow = 0.0f;              // 0..1 눈 세기
	float Wind = 2.0f;              // m/s 평균 바람
	float WindDirection = 30.0f;    // 도 (바람이 불어 가는 쪽, +Z 기준 시계 방향 — 나무 · 풀과 같은 기준)
	float Gust = 0.2f;              // 0..1 돌풍 (바람이 오르내리는 정도)
	float Clouds = 0.0f;            // 0..1 먹구름 (해 · 하늘 · 환경광이 어두워진다)
	float Fog = 0.0f;               // 0..1 날씨 안개
	float FogDistance = 400.0f;     // m: 안개 63 % 가려지는 거리
	float Lightning = 0.0f;         // 분당 번개 수
	float Wetness = 0.0f;           // 0..1 젖음 (2 단계)
	float SnowCover = 0.0f;         // 0..1 눈 덮임 (3 단계)

	static WeatherParams Lerp(const WeatherParams& a, const WeatherParams& b, float t);
	nlohmann::json ToJson() const;
	static WeatherParams FromJson(const nlohmann::json& j);
};

namespace WeatherProfiles
{
	// 기본 프로필 (이름 순서 = Inspector 단추 순서)
	const std::vector<std::pair<std::string, WeatherParams>>& Presets();
	// 이름 (기본 프로필, 대소문자 무시) 또는 .weather 파일 (프로젝트 기준 경로)
	bool Find(const std::string& nameOrPath, WeatherParams& out, std::string& error);
	bool Save(const std::string& path, const WeatherParams& p, std::string& error);
	std::vector<std::string> FindAssets();   // 프로젝트의 .weather
	constexpr const char* kExtension = ".weather";
}
