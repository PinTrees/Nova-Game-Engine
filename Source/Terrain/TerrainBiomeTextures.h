#pragma once
#include <array>
#include <string>

// 바이옴 텍스처 (World Terrain): 지형 레이어 넷의 "역할" (풀 · 흙 · 바위 · 눈 / 모래 — 레이어 이름) 마다 바이옴에 맞는 텍스처 팩 사진 재질.
//  타일마다 기후 맵 (기온 · 습도 · 물가 — 그래프 · WorldGen 의 BiomeAt) 을 셰이더가 읽어 가장 가까운 바이옴 둘을 골라 섞는다 (40 · 61 셰이더).
//  재질은 배열 두 장: 색 (RGBA8 — rgb 색, a 거칠기) · 높이 (RGBA8 — r 높이, gb 노멀 xy). 바이옴 × 역할 → 슬라이스 표
//  텍스처 팩 (com.nova.textures) 이 프로젝트에 없으면 쓰지 않는다 (레이어 텍스처 그대로)
namespace TerrainBiomeTextures
{
	constexpr int kBiomes = 8;
	constexpr int kKinds = 5;     // TerrainTextureGen::Kind — Grass · Dirt · Rock · Snow · Sand
	constexpr int kMaxSlices = 32;

	struct Biome
	{
		const char* Name;
		float Temperature, Moisture;      // 가운데 (°C, 0..1)
		float TempWidth, MoistWidth;      // 폭
		const char* Material[kKinds];     // 종류마다 텍스처 팩 id
	};
	NOVA_API const Biome* Biomes();      // kBiomes 개

	struct Set
	{
		GfxShaderResourceView* Albedo = nullptr;    // Texture2DArray
		GfxShaderResourceView* Heights = nullptr;   // Texture2DArray (61 의 gTerrainHeights 와 같은 꼴)
		float Slices[kBiomes][4] = {};               // 바이옴 × 역할 (레이어 0..3) → 슬라이스
		float Params[kMaxSlices][4] = {};            // x 1 / 타일 (m), y 높이 (m), z Base, w Normal Scale
		int SliceCount = 0;
		float MaxAmplitude = 0.0f;
		size_t GpuBytes = 0;
	};

	// 레이어 넷의 종류 (Kind 번호, 없으면 -1) 에 맞는 세트. 처음엔 작업 스레드가 사진을 읽고 (그동안 nullptr — 레이어 텍스처 그대로),
	//  다 되면 다음 호출에서 GPU 배열을 만든다. albedoSize = 색 한 변 (512 · 1024 · 2048), 높이는 그 절반
	NOVA_API const Set* Get(const std::array<int, 4>& kinds, int albedoSize);
	NOVA_API void Clear();   // 패키지 · 재질이 바뀌었을 때
	NOVA_API std::string Status();   // CLI · Inspector 용 한 줄
}
