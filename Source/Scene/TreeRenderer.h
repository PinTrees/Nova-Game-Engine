#pragma once
#include "TreeDesc.h"

// 나무 그리기 (Tree 컴포넌트 + 지형에 칠한 나무 전부).
//  - 같은 설정(TreeDesc::Hash)의 나무를 한데 모아 인스턴싱으로 그린다 (LOD 단계마다 한 번)
//  - 패스마다 절두체 밖 나무는 뺀다 (본 패스·깊이 패스 = 카메라, 그림자 = 빛)
//  - LOD: 거리 / 나무 크기 배율로 전체 메시(LOD0) → 중간 메시(LOD1) → 임포스터(8 방향 빌보드) → 안 그림.
//    경계 앞뒤 10% 는 두 단계를 화면 디더로 나눠 그린다 (갑자기 바뀌어 보이지 않게). 그림자는 섞지 않고 한 단계만
//  - 임포스터는 처음 필요할 때 LOD0 을 8 방향에서 알베도·법선(물체 공간) 아틀라스로 굽고, 그릴 때 다시 조명한다
namespace TreeRenderer
{
	enum class Pass { Main, Shadow, NormalDepth };

	void UpdateTime();                    // 프레임마다 한 번 (바람 시간)
	void DrawAll(Pass pass, bool editor); // Scene 의 각 패스에서 한 번

	struct MeshInfo
	{
		Vec3 BoundsMin, BoundsMax;
		int Vertices = 0, Triangles = 0, Branches = 0, LeafCards = 0;
		const std::vector<XMFLOAT3>* Positions = nullptr;   // 바람 전 위치 (선택용)
		const std::vector<uint32_t>* Indices = nullptr;
	};
	bool GetMeshInfo(const TreeDesc& desc, MeshInfo& out, int lod = 0);

	// 프로토타입 미리보기 = 임포스터 앞면 (없으면 굽는다). uv0/uv1 = 아틀라스 안 영역
	ImTextureID Thumbnail(const TreeDesc& desc, ImVec2& uv0, ImVec2& uv1);

	struct Stats
	{
		int Trees = 0;                      // 절두체 안 나무
		int Lod0 = 0, Lod1 = 0, Billboards = 0;
		int DrawCalls = 0;
	};
	const Stats& LastStats(bool editor);   // 마지막 본 패스
}
