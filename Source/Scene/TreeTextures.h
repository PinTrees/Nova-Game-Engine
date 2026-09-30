#pragma once

// 나무 텍스처를 실행 중에 수학식으로 굽는다 (파일 없음). 셰이더는 한 번 읽고 알파 컷만 한다.
//  - 잎 아틀라스 (2x2 변형): 카드 한 장 = 잔가지 + 잎 N 장. 잎 하나하나를 SDF 로 그려
//      r = 잎맥에서의 거리(±, 0.5 = 가운데), g = 잎 안 위치(꼭지 0 → 끝 1), b = 잎 번호(0 = 잔가지), a = 덮임
//    밉맵은 알파 덮임 비율을 유지하도록 굽는다 (멀리서 잎이 가늘어져 사라지지 않게)
//  - 수피 (가로·세로 모두 이어지는 타일): 세로 균열 높이 → rg = 기울기, b = 높이
namespace TreeTextures
{
	// shape: 0 Broad, 1 Oval, 2 Needle. leafLength = 카드 비율
	ID3D11ShaderResourceView* Leaf(int shape, int leavesPerCard, float leafLength);
	ID3D11ShaderResourceView* Bark();

	// 수피 타일 한 장이 담는 균열 칸 수 (가로·세로). 셰이더 44. TreeCommon.fx 와 같아야 한다
	constexpr int kBarkCells = 8;
}
