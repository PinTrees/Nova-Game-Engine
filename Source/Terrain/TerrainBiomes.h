#pragma once
#include "TerrainGenSettings.h"
#include <memory>

class TerrainData;

// 바이옴 프리셋 패키지 (Resources/Packages/Terrain/Biomes/*.biome, JSON).
//  프리셋 하나 = 지형 특성(Base 노이즈 + 필터 스택) + 재질(색·그라디언트 규칙) + 레이어 텍스처 4 장.
//  Generate 도구에서 썸네일을 눌러 지형 전체에 적용하거나, 바이옴 영역(TerrainBiome 오브젝트)에 지정한다.
//  썸네일은 프리셋마다 129² 로 실제 생성한 결과를 위에서 본 음영 이미지 (백그라운드, 한 번)
namespace TerrainBiomes
{
	struct Preset
	{
		std::string Name;
		std::string Description;
		std::string Path;              // 프로젝트 기준 경로
		nlohmann::json Data;
		std::vector<std::string> Layers;   // 지형 레이어 에셋 경로 4 개
	};

	const std::vector<Preset>& List();
	const Preset* Find(const std::string& name);
	void Reload();

	// 프리셋의 지형·재질 설정 (Enabled / AutoUpdate 는 base 의 값 유지)
	TerrainGenSettings ToSettings(const Preset& preset, const TerrainGenSettings& base);
	// 지형 전체에 적용: 설정 + 레이어 4 장을 바꾼다
	void Apply(TerrainData& data, const Preset& preset);

	// (개발용) NOVA_DEV_BIOMEDUMP=<폴더> 면 한 번: 프리셋마다 513² 생성 결과를 그 폴더에 이미지로, 통계는 Editor.log 에
	void DevDump();

	// 썸네일 (없으면 백그라운드 생성을 시작하고 nullptr)
	ID3D11ShaderResourceView* Thumbnail(const Preset& preset);
}
