#pragma once
#include <cstdint>
#include <functional>
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
	inline uint32_t LodStamp = 1;   // LOD Group 이 이번 뷰에 매긴 번호 (Component::LodStamp 와 같으면 LOD 숨김을 따른다)
	inline bool ShadowPass = false; // 마지막 Cull 이 그림자 패스 (LOD 는 그림자를 따로 고른다)

	void Update(Scene* scene);                          // 프레임마다 한 번 (그리기 전)
	// Mesh Renderer · Skinned Mesh Renderer 의 생성자 · 소멸자 (프레임마다 모든 GameObject 를 훑지 않고 이 목록만 본다)
	void RegisterRenderer(Component* renderer, bool skinned);
	void UnregisterRenderer(Component* renderer);
	nlohmann::json Info();   // 렌더러 수 · 추적 · 노드 · 건너뛴 (바뀌지 않은) 렌더러 · 다시 계산한 렌더러
	void Cull(CXMMATRIX viewProj, bool shadowPass);     // 패스마다 (같은 절두체면 한 번으로 여러 패스)
	uint32_t FrameIndex();                              // Update 마다 1 씩 (프레임 안에서만 쓰는 목록의 유효성 검사용)

	// 추적하지 않는 컴포넌트(렌더러가 아니거나 메시가 없음)는 늘 보인다
	//  LOD Group 이 이 뷰에서 숨긴 렌더러 (다른 LOD) 도 안 보인다
	inline bool IsVisible(const Component* c)
	{
		if (c->LodStamp == LodStamp && (ShadowPass ? c->LodShadowHidden : c->LodHidden))
			return false;
		return !Enabled || !c->CullTracked || c->CullStamp == Stamp;
	}

	struct Stats
	{
		int Objects = 0;        // 옥트리에 든 렌더러
		int Visible = 0;        // 마지막 카메라 Cull 에서 보인 수
		int NodesVisited = 0;
		int Nodes = 0;
		int Depth = 0;          // 가장 깊은 노드
	};
	const Stats& LastStats(bool editorView);
	// 이번 Update 에서 옮겨지거나 생기거나 지워진 렌더러의 월드 상자 (예전 · 새 자리 모두, 최소 · 최대)
	//  — 실시간 간접광 (Adaptive Probe Volume) 이 그 근처 단계만 다시 찍는다
	const std::vector<std::pair<Vec3, Vec3>>& ChangedBounds();
	void SetEditorView(bool editorView);   // 다음 카메라 Cull 의 통계를 어느 뷰에 쌓을지
	// 추적 중인 렌더러의 월드 상자 (마지막 Update 기준). 추적하지 않으면 false — 발광 렌더러 찾기 (Adaptive Probe Volume)
	bool TrackedBounds(const Component* renderer, Vec3& mn, Vec3& mx);
	// 추적 중인 렌더러의 자리 번호 (지워질 때까지 같다) + 월드 상자 — 오클루전 컬링이 지난 프레임 기록을 이 번호로 둔다
	bool TrackedSlot(const Component* renderer, uint32_t& slot, Vec3& mn, Vec3& mx);
	uint32_t SlotCount();
	bool SlotBounds(uint32_t slot, Vec3& mn, Vec3& mx);   // Component::CullSlot 의 상자 (해시 찾기 없음)
	// 추적 중인 Skinned Mesh Renderer 와 월드 상자 (마지막 Update 기준) — 오클루전 쿼리
	void ForEachSkinned(const std::function<void(Component*, const Vec3&, const Vec3&)>& f);
}
