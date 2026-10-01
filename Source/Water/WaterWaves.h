#pragma once
#include "WaterProfile.h"

// Gerstner 파도 묶음 (GPU Gems 1 장 + Unreal WaterWaves 의 생성기 방식).
//  바람 속도로 첫 파장(마루 파장)을 정하고, 파장을 로그 간격으로 나눠 방향·위상을 씨앗으로 흩는다.
//  같은 식을 셰이더(46. Water.fx)와 CPU 가 같이 써서 물 위 높이(부력, 스크립트)를 화면과 똑같이 얻는다.
namespace WaterWaves
{
	constexpr int kMaxWaves = 32;
	constexpr float kGravity = 9.81f;

	struct Wave
	{
		float DirX = 1, DirZ = 0;     // 진행 방향 (단위)
		float K = 1;                  // 파수 2π/L
		float Amplitude = 0;          // m
		float Omega = 1;              // 각속도 sqrt(gk)
		float Phase = 0;
		float Q = 0;                  // 마루 뾰족함 (가로 이동 = Q·A)
	};

	struct Set
	{
		std::vector<Wave> Waves;
		float PeakWavelength = 30.0f;
		float MaxAmplitude = 0.0f;    // 모든 파도 진폭 합 (경계 상자 여유)
	};

	Set Build(const WaterProfile& p, float scale = 1.0f);

	// 물결 시간 (초, 에디터에서도 흐른다)
	float Time();

	// 물 위 높이 (기준 수면 0 에 대한 m). 수평 이동을 몇 번 되짚어 (x, z) 바로 위의 높이를 찾는다
	float Height(const Set& set, float x, float z, float time, Vec3* normal = nullptr);
}
