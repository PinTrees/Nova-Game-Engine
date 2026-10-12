#pragma once
#include "TreeDesc.h"
#include <string>
#include <vector>

// 기후대별 식생 종 (모두 절차적 — TreeGenerator 의 꼴 · 잎 모양 · 꽃, 텍스처 · 모델 파일 없음).
//  열대 우림 · 열대 건조림 · 사바나 · 더운 사막 · 지중해 · 아열대 · 온대 낙엽수림 · 온대 우림 · 초원 · 냉대 (타이가) · 고산 · 툰드라 · 물가
//  처음 12 개는 예전 트리 프리셋 (Oak … Juniper) 번호 그대로 — 저장된 씬 · 지형 나무 프로토타입이 같은 나무를 그린다.
//  PCG 의 Static Mesh Spawner 가 "tree:종" 메시를 기후 (연평균 기온 · 습도 · 경사 · 물가) 로 골라 놓는다 (Climate Select)
namespace PlantLibrary
{
	// 숲의 층 (바이옴 식생 규칙이 층마다 스포너 하나)
	enum class Layer { Canopy, SmallTree, Shrub, Grass, Flower, Fern, Succulent, Count };
	const char* LayerName(Layer layer);

	// 사는 곳
	struct Climate
	{
		float TempMin = 0.0f, TempMax = 30.0f;   // 연평균 기온 (°C)
		float MoistMin = 0.0f, MoistMax = 1.0f;  // 습도 (0 사막 ~ 1 우림)
		float SlopeMax = 35.0f;                  // 도
		float Water = 0.0f;                      // 물가를 좋아하는 정도 (0 ~ 1 — 1 이면 물가에만)
	};

	struct Species
	{
		std::string Name;
		const char* Zone = "";      // 기후대 (표시)
		Layer Kind = Layer::Canopy;
		Climate Range;
		float Abundance = 1.0f;     // 같은 곳에 맞는 종들 사이의 흔함
		TreeDesc Desc;
	};

	const std::vector<Species>& All();
	const Species* At(int index);
	int IndexOf(const std::string& name);      // 대소문자 무시, 없으면 -1
	const char* const* Names(int& count);

	// 적합도 (0 = 못 산다 ~ 1): 기온 · 습도 범위 안이면 1 쪽, 범위 밖 여유 (기온 3 °C · 습도 0.08) 에서 부드럽게 0.
	//  water = 물가 정도 (0 ~ 1, 강 · 호수에 가까울수록 1)
	float Suitability(const Species& s, float temperatureC, float moisture, float slopeDeg, float water);
}
