#pragma once
#include "TerrainGenSettings.h"
#include <memory>

class TerrainData;

// 지형 생성기 (비파괴). TerrainData::Generator 가 켜진 지형의 높이·스플랫을 만든다.
//  순서: Base 노이즈 → 씬의 TerrainStamp (Order 순) → 필터 스택 → 재질 규칙
//  씬의 TerrainBiome 영역: 영역 안은 그 프리셋의 Base / 필터 / 재질로 따로 만들어 마스크로 섞는다
//  씬의 WaterBody(호수·강, Carve Terrain): 필터 뒤에 물 모양대로 판다 (물가 경사 → 바닥)
//  - 값·스탬프·지형 위치가 바뀌면 백그라운드 스레드에서 다시 생성하고, 끝나면 메인 스레드가 높이·스플랫을 바꿔 끼운다
//  - 마우스로 끄는 중(스탬프 이동, 슬라이더)에는 무거운 필터(침식)를 건너뛴 미리보기, 놓으면 전체 생성
namespace TerrainGenerator
{
	struct HeightImage
	{
		int Width = 0, Height = 0;
		std::vector<float> Pixels;   // 0~1
	};

	struct StampInput
	{
		int Shape = 0, Op = 0;
		float Cx = 0, Cz = 0;            // 지형 로컬 중심 (m)
		float HalfX = 50, HalfZ = 50;    // 반 너비 (m)
		float Cos = 1, Sin = 0;          // 회전 (y)
		float BaseY = 0;                 // 지형 로컬 기준 높이 (m)
		float Height = 100, Opacity = 1, Blend = 0.3f, Roundness = 1, Power = 1, Detail = 0.3f, DetailScale = 0.25f;
		int Seed = 1;
		std::shared_ptr<const HeightImage> Image;
	};

	struct BiomeInput
	{
		float Cx = 0, Cz = 0;            // 지형 로컬 중심 (m)
		float HalfX = 100, HalfZ = 100;  // 반 너비 (m)
		float Cos = 1, Sin = 0;
		float Opacity = 1, Blend = 0.4f, Roundness = 1, EdgeNoise = 0.5f;
		int Seed = 1;
		bool Heights = true, Materials = true;
		TerrainGenSettings Settings;     // 프리셋 (재질 규칙의 레이어 번호는 이 지형의 레이어로 바꿔 둔 것)
	};

	// 물 바디가 파는 모양 (지형 로컬 m). 호수 = 닫힌 윤곽 + 수면 높이, 강 = 가운데 선 점마다 수면 높이·폭·깊이
	struct WaterCarveInput
	{
		bool River = false;
		std::vector<XMFLOAT4> Points;    // x, z, 수면 y, 폭(강)
		std::vector<float> Depths;       // 강: 점마다 깊이
		float SurfaceY = 0;              // 호수 수면
		float Depth = 5;                 // 호수 가운데 깊이
		float Bank = 10;                 // 물가 경사 폭
	};

	struct Input
	{
		int Resolution = 513;
		float SizeX = 1000, SizeY = 600, SizeZ = 1000;
		int ControlResolution = 512;
		int LayerCount = 0;
		TerrainGenSettings Settings;
		std::vector<float> Snapshot;     // Base = Current Terrain 일 때 (정규화 높이)
		std::vector<StampInput> Stamps;  // 합칠 순서대로
		std::vector<BiomeInput> Biomes;  // 섞을 순서대로 (뒤의 것이 위)
		std::vector<WaterCarveInput> Water;   // 필터 뒤에 판다 (호수·강 바닥)
		bool Preview = false;
	};

	struct Output
	{
		std::vector<float> Heights;      // 정규화 0~1
		std::vector<uint8_t> Control;    // RGBA (PaintMaterials 일 때)
		std::vector<uint8_t> ColorMap;   // RGBA: rgb = 색(sRGB), a = 색이 텍스처 색을 대신하는 정도. 색 규칙이 없으면 비어 있음
		std::vector<float> Uncarved;     // 물로 파기 전 높이 (정규화, 판 물이 없으면 비어 있음)
		bool HasControl = false;
		double Ms[4] = {};               // Base, Stamps, Filters, Materials
	};

	Output Generate(const Input& input);

	// ---- 에디터 연결 ----
	void Update();                                        // 매 프레임 (App 루프): 바뀐 지형을 다시 생성하고 끝난 결과를 적용
	void Regenerate(const std::shared_ptr<TerrainData>& data);   // 자동 갱신이 꺼져 있어도 지금 다시 (전체)
	struct Status
	{
		bool Running = false;
		bool LastPreview = false;
		double LastMs = 0.0;
		double StageMs[4] = {};
		int StampCount = 0;
		int BiomeCount = 0;
	};
	Status GetStatus(const TerrainData* data);

	std::shared_ptr<const HeightImage> LoadHeightImage(const std::string& projectPath);   // 흑백 이미지 (캐시)
}
