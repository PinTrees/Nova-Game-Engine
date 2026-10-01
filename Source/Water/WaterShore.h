#pragma once

// 활성 지형들의 높이 지도 (512², 월드 높이) + 바다 마스크.
//  - 높이 지도: 바다의 얕은 물 파도 감쇠(셰이더), 지형 범위 = 활성 지형들을 합친 사각형. 지형이 바뀔 때만 다시 만든다
//  - 바다 마스크: 해수면보다 낮은 칸 중 지도 가장자리(열린 바다)와 4 방향으로 이어진 칸만 바다 (Unreal 의 Water Zone / 마스크 역할).
//    칸은 네 모서리·가운데 중 가장 낮은 높이로 판정 (칸보다 작은 웅덩이도 잡는다). 땅 칸 = 1, 0 = 내륙 웅덩이 → 바닷물이 차지 않는다. 화면(셰이더)과 CPU(부력·수중)가 같은 마스크를 쓴다
namespace WaterShore
{
	constexpr int kRes = 512;

	void Update();                                  // 매 프레임 (그리기 전): 지형이 바뀌었으면 다시
	bool Has();                                     // 지형이 있나
	uint64_t Revision();                            // 높이 지도가 바뀔 때마다 증가
	const std::vector<float>& Heights();            // kRes², 지형 밖 = -1e4
	XMFLOAT4 Rect();                                // minX, minZ, 1/폭, 1/깊이
	float HeightAt(float x, float z);               // 지도에서 (지형 밖이면 -1e4)

	// 해수면 seaLevel 의 바다 마스크 (kRes², 0/255). 해수면·지도가 같으면 캐시
	const std::vector<uint8_t>& OceanMask(float seaLevel);
	bool OceanCovers(float seaLevel, float x, float z);   // (x, z) 가 열린 바다 쪽인가 (지형 밖이면 true)
}
