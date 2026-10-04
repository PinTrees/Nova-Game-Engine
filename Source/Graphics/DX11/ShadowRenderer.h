#pragma once
#include <functional>

class ShadowMap;
class Light;
class VolumeStack;
class InstancedBasicEffect;

// 실시간 그림자 (Unity URP 방식)
//  - 방향광: Cascaded Shadow Maps — 카메라 절두체를 Max Distance 까지 거리별로 1~4 조각으로 나누고
//    조각마다 그것을 감싸는 구(반지름 고정 → 카메라가 돌아도 크기가 안 변함)에 정사영 맵을 맞춘다.
//    구의 중심은 텍셀 단위로 맞춰 카메라가 움직여도 그림자 가장자리가 떨리지 않는다.
//  - 스포트광: 원근 맵 하나, 점광: 큐브 6 면 (+X, -X, +Y, -Y, +Z, -Z)
//  - 바이어스: 캐스터 VS 에서 월드 공간으로 (깊이 = 빛 반대쪽, 노멀 = 표면 안쪽, 텍셀 크기 비례)
//  - 설정: Volume 의 Shadows 효과 (Max Distance, Cascade Count, Split, Last Border, Resolution, Bias, Soft Shadows)
//          + 빛마다 Shadow Type(Hard/Soft), Strength, Bias(파이프라인 값 / Custom), Near Plane
namespace ShadowRenderer
{
	struct Settings
	{
		float MaxDistance = 50.0f;
		int CascadeCount = 4;
		float Splits[3] = { 0.067f, 0.2f, 0.467f };   // Max Distance 에 대한 비율
		float LastBorder = 0.2f;
		uint32 Resolution = 2048;
		float DepthBias = 1.0f;
		float NormalBias = 1.0f;
		bool SoftShadows = true;
		int SoftQuality = 1;   // 0 Low, 1 Medium, 2 High
		int FarCascadeUpdate = 1;   // 0 매 프레임, 1 Staggered (3 번째 2 프레임·4 번째 4 프레임마다), 2 Slow (4 / 8)

		static Settings FromStack(const VolumeStack& stack);
	};

	// 한 화면(Game 뷰 / Scene 뷰)의 그림자 결과 — 받는 쪽 셰이더에 넘긴다
	struct FrameData
	{
		int DirCount = 0, SpotCount = 0, PointCount = 0;
		XMMATRIX Dir[LIGHT_SIZE * 4];
		XMMATRIX Spot[LIGHT_SIZE];
		XMMATRIX Point[LIGHT_MAX_SIZE];
		XMFLOAT4 Spheres[4];
		XMFLOAT4 Params;   // x 캐스케이드 수, y Max Distance, z 흐려지기 시작 거리, w 1 / 흐려지는 폭
		XMFLOAT4 DirData[LIGHT_SIZE], SpotData[LIGHT_SIZE], PointData[LIGHT_SIZE];   // x Strength, y 필터

		// ---- 먼 캐스케이드 캐시 (화면마다). 다시 그리지 않는 프레임에는 그 맵을 그린 때의 행렬·구를 그대로 쓴다
		//  → 받는 쪽이 맵과 같은 행렬로 읽어 정적 물체 그림자는 그대로, 움직이는 물체만 몇 프레임 늦는다
		struct CascadeCache
		{
			bool Valid = false;
			XMFLOAT4 Sphere = {};          // 그린 때의 구 (중심, r²)
			XMMATRIX Dir[LIGHT_SIZE];      // 빛마다 그린 때의 행렬 (텍스처 공간까지)
			uint32 Generation[LIGHT_SIZE] = {};
			const Light* Lights[LIGHT_SIZE] = {};
			XMFLOAT3 LightDir[LIGHT_SIZE] = {};
			uint64_t SettingsKey = 0;
		};
		CascadeCache Cache[4];
		uint64_t FrameCounter = 0;
		int CascadesDrawn = 0;             // 이번 프레임에 다시 그린 캐스케이드 수 (통계)
	};

	// 정렬된 빛(방향 → 스포트 → 점광 순)의 그림자 맵을 그린다. drawCasters = 장면의 그림자 캐스터 그리기
	void Render(GfxContext* dc, ShadowMap& maps, const vector<shared_ptr<Light>>& sortedLights,
		int dirCount, int spotCount, int pointCount,
		const XMFLOAT3& eye, CXMMATRIX view, CXMMATRIX proj, const Settings& settings,
		FrameData& out, const std::function<void()>& drawCasters);

	// 받는 쪽(InstancedBasic) 변수 설정
	void Bind(InstancedBasicEffect* fx, ShadowMap& maps, const FrameData& data);

	// 지금 그리는 그림자 패스 (drawCasters 안에서만 — MeshBatcher 가 그림자 캐스터 오클루전 컬링에 쓴다)
	struct CasterPass
	{
		bool Directional = false;   // 방향광 캐스케이드
		bool EveryFrame = false;    // 매 프레임 다시 그리는 캐스케이드 (몇 프레임 캐시하는 먼 캐스케이드는 카메라에 따라 빼면 안 된다)
		XMFLOAT3 Direction = {};    // 빛이 나아가는 방향 (단위)
		float Reach = 0.0f;         // 그림자가 닿을 수 있는 거리 = 캐스케이드 구의 지름 (받는 표면은 그 구 안)
	};
	inline CasterPass Current;
}
