#pragma once
#include <cstdint>
#include <string>
#include <vector>

// 지형 레이어 텍스처를 계산으로 만든다 (Gaea · Substance 처럼): 이음매 없이 반복되는 (주기 노이즈) 알베도 · 노멀 맵 · 높이 맵.
//  Grass (풀잎 결 · 마른 얼룩) · Dirt (흙 + 자갈) · Rock (층리 + 금 + 거친 면) · Snow (부드러운 눈더미 · 반짝임) · Sand (물결 무늬)
namespace TerrainTextureGen
{
	enum class Kind { Grass, Dirt, Rock, Snow, Sand, Count };
	const char* KindName(Kind k);
	Kind KindFromName(const std::string& name);   // 이름에 낱말이 들어 있으면 (기본 Grass)

	struct Result
	{
		int Size = 0;
		std::vector<uint8_t> Albedo;   // RGBA8 (sRGB)
		std::vector<uint8_t> Normal;   // RGBA8 (접공간 xy → 0..1, z, DirectX 와 같은 +Y)
		std::vector<uint8_t> Height;   // RGBA8 (회색)
		float TileSize = 8.0f;         // 권장 타일 크기 (m)
		float NormalScale = 1.0f;
		float HeightAmplitude = 0.08f; // m
	};
	// size = 2 의 거듭제곱 (256 ~ 2048), 작업 스레드 여럿
	Result Generate(Kind kind, int size, int seed);
}
