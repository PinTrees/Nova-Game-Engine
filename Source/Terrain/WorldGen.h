#pragma once
#include <cstdint>

// 초대형 월드의 높이 · 바이옴 함수 (World Terrain · PCG 가 같이 쓴다).
//  - 위치 (월드 x, z — 미터) 만으로 정해지는 순수 함수 (스레드 어디서나, 같은 값). 타일 · 해상도와 상관없이 같은 땅
//  - 높이 = 대륙 (아주 낮은 주파수) + 산맥 (리지 노이즈, 대륙이 높은 곳) + 언덕 + 잔결 — 도메인 휨으로 자연스럽게
//  - 바이옴 = 온도 (노이즈 − 고도) · 습도 노이즈 → 숲 · 초원 · 사막, 가파르거나 높으면 바위
namespace WorldGen
{
	struct Settings
	{
		int Seed = 1337;
		float WorldSize = 32768.0f;     // 정사각형 한 변 (m), 가운데가 원점
		float MaxHeight = 1400.0f;      // 높이 0 ~ 1 → 0 ~ MaxHeight (m)
		float ContinentScale = 11000.0f;
		float MountainScale = 2800.0f;
		float Mountains = 0.55f;        // 산맥 세기 (0 ~ 1)
		float HillScale = 650.0f;
		float Hills = 0.3f;
		float DetailScale = 70.0f;
		float Detail = 0.012f;
		float Warp = 0.35f;             // 도메인 휨 (0 = 반듯한 노이즈)
		float BiomeScale = 5200.0f;     // 바이옴 덩어리 크기 (m)
		float Desert = 0.42f;           // 사막 비중 (0 ~ 1)
		float Forest = 0.55f;           // 숲 비중 (0 ~ 1)

		uint64_t Hash() const;          // 값이 바뀌면 다른 값 (타일 · PCG 셀 다시 만들기)
	};

	// 바이옴 비중 (합 1)
	struct Biome
	{
		float Forest = 0.0f, Meadow = 0.0f, Desert = 0.0f, Rock = 0.0f;
	};

	float Height01(const Settings& s, float x, float z);   // 0 ~ 1
	inline float Height(const Settings& s, float x, float z) { return Height01(s, x, z) * s.MaxHeight; }
	// 월드 법선 (eps = 차분 간격, m)
	void Normal(const Settings& s, float x, float z, float eps, float& nx, float& ny, float& nz);
	// slopeCos = 법선의 y (1 = 평평). height01 = Height01 값
	Biome BiomeAt(const Settings& s, float x, float z, float height01, float slopeCos);
	// 지형 스플랫 4 칸 (합 255): 0 풀 · 이끼, 1 숲 바닥 (낙엽 · 흙), 2 바위, 3 사막 흙
	void Splat(const Biome& b, float x, float z, int seed, uint8_t out[4]);

	// 노이즈 (PCG 의 Density Noise 도 쓴다): -1 ~ 1
	float Noise(int seed, float x, float z);
	float Fbm(int seed, float x, float z, int octaves, float lacunarity = 2.0f, float gain = 0.5f);
}
