#pragma once
#include <cstdint>
#include "Component.h"

class Scene;

// 절두체 컬링 (Mesh Renderer / Skinned Mesh Renderer).
//  - 렌더러의 월드 AABB 를 느슨한 옥트리(loose octree: 노드 영역을 2 배로 넓혀 물체를 중심이 든 칸 하나에만 넣는다)에 둔다
//  - 매 프레임 Update: 위치·메시가 바뀐 렌더러만 다시 넣고, 사라진 렌더러는 뺀다
//  - 패스마다 Cull(ViewProj): 노드 상자가 절두체 밖이면 통째로 버리고, 완전히 안이면 통째로 받는다
//    (그림자 패스는 빛의 절두체. 빛 앞쪽(가까운 면 밖) 물체도 그림자를 드리우므로 가까운 면은 검사하지 않는다)
//  - Scene 의 그리기 루프가 IsVisible 로 거른다. 지형(쿼드트리 LOD)·나무(TreeRenderer)는 따로 컬링한다
namespace SceneCulling
{
	inline uint32_t Stamp = 1;   // 마지막 Cull 번호 (Component::CullStamp 와 같으면 보임)
	inline bool Enabled = true;  // 끄면 모두 그린다 (비교 측정용: NOVA_DEV_NOCULL=1)

	void Update(Scene* scene);                          // 프레임마다 한 번 (그리기 전)
	void Cull(CXMMATRIX viewProj, bool shadowPass);     // 패스마다 (같은 절두체면 한 번으로 여러 패스)

	// 추적하지 않는 컴포넌트(렌더러가 아니거나 메시가 없음)는 늘 보인다
	inline bool IsVisible(const Component* c) { return !Enabled || !c->CullTracked || c->CullStamp == Stamp; }

	struct Stats
	{
		int Objects = 0;        // 옥트리에 든 렌더러
		int Visible = 0;        // 마지막 카메라 Cull 에서 보인 수
		int NodesVisited = 0;
		int Nodes = 0;
		int Depth = 0;          // 가장 깊은 노드
	};
	const Stats& LastStats(bool editorView);
	void SetEditorView(bool editorView);   // 다음 카메라 Cull 의 통계를 어느 뷰에 쌓을지
}
