#pragma once
#include <cstdint>
#include <string>
#include <vector>

class MeshGeometry;
class GfxContext;

// 오클루전 컬링 — 굽기 없이 매 프레임 GPU 에서 (Unity 기본은 Umbra 로 미리 굽는다. NOVA 는 사전 작업 없이 움직이는 물체도)
//  두 단계 Hi-Z (MeshBatcher 의 Mesh Renderer, 카메라 뷰의 깊이 프리패스 · 본 패스):
//   1. 깊이 프리패스 1 단계: 지난 프레임에 보였던 렌더러만 그린다 (지형 · 나무 · 스킨 메시도 이어서 그려 가리는 물체가 된다)
//   2. Finish: 깊이 버퍼 → Hi-Z 밉 → 렌더러 상자마다 가려졌는가 (compute) → 새로 보인 것만 깊이 프리패스 2 단계로
//   3. 본 패스: 지금 보이는 것만
//  고른 인스턴스는 GPU 가 월드 행렬을 묶음마다 이어 써서 DrawIndexedInstancedIndirect 로 그린다 — CPU 읽기 없음, 한 프레임 늦은 구멍 없음,
//  셰이더 (엔진 · Shader Graph · 패키지) 는 그대로 (인스턴스 정점 버퍼만 바뀐다)
//  DirectX 11 · OpenGL · Vulkan · OpenGL ES (안드로이드). GfxContext::SupportsGpuDriven 이 false 이면 Begin 이 false → 절두체 컬링만
namespace OcclusionCulling
{
	enum Set { DepthPhase1 = 0, DepthPhase2 = 1, Main = 2, ShadowSet = 3, SetCount = 4 };

	struct Caster
	{
		float Min[3];
		uint32_t Slot;          // SceneCulling::Slot (지난 프레임 기록 자리)
		float Max[3];
		uint32_t Pad;
	};
	struct Item
	{
		uint32_t Caster;
		uint32_t Batch;
		uint32_t Base;          // 묶음의 첫 인스턴스 자리 (Begin 이 채운다)
	};
	struct Batch                // 묶음 하나의 메시 서브셋 (간접 인자)
	{
		uint32_t IndexCount, StartIndex;
		int32_t BaseVertex;
		uint32_t Candidates;    // 이 묶음의 후보 인스턴스 수 (Begin 이 채운다)
		uint32_t Base = 0;      // 이 묶음의 첫 인스턴스 자리 (Begin 이 채운다 — OpenGL ES 는 간접 인자 대신 정점 버퍼 오프셋으로)
	};
	// 인스턴스 정점 버퍼 한 칸 (MeshBatcher · Compact): 월드 행렬 64 바이트 + 기본색 16 바이트 (32. InstancedBasic.fx 의 VertexIn_Batch)
	constexpr uint32_t InstanceBytes = 80;
	constexpr uint32_t InstanceFloats = InstanceBytes / 4;
	struct Frame
	{
		std::vector<Caster> Casters;
		std::vector<float> Worlds;              // 렌더러마다 InstanceFloats 개 (4x4 월드 + 기본색 — MeshBatcher 의 인스턴스 값)
		std::vector<Item> Items[2];             // 0 = 깊이 묶음, 1 = 본 패스 묶음
		std::vector<Batch> Batches[2];
	};

	inline bool Enabled = true;   // nova occlusion set --enabled false · NOVA_DEV_NOOCCLUSION=1 (비교 측정)

	// 깊이 프리패스를 시작할 때 (깊이 타깃이 묶인 상태): 올리고 1 단계 목록을 만든다. false = 이 뷰는 CPU 절두체 컬링만
	//  view = 카메라 (뷰마다 지난 프레임 기록 · Hi-Z), editor = Scene 뷰 (통계), slotCount = SceneCulling::SlotCount
	bool Begin(GfxContext* dc, const void* view, bool editor, const float viewProj[16], Frame& frame, uint32_t slotCount);
	// 깊이 프리패스 1 단계 뒤 (같은 깊이 타깃이 묶인 상태): Hi-Z · 가려짐 검사 · 2 단계와 본 패스 목록
	void Finish(GfxContext* dc);
	// 그 목록의 묶음 하나 (입력 배치 · 셰이더는 호출한 쪽이 이미 묶었다 — 인스턴스 정점 버퍼 = 1 번)
	void DrawIndirect(GfxContext* dc, Set set, uint32_t batch, MeshGeometry& geometry, uint32_t subset);

	// 낱개로 그리는 렌더러 (Skinned Mesh Renderer): 깊이 프리패스가 끝난 뒤 상자를 오클루전 예측 쿼리로 그려 두고,
	//  본 패스에서 그 쿼리로 그리기를 감싼다 (SetPredication — 가려졌으면 GPU 가 건너뛴다. CPU 는 기다리지 않는다)
	struct BoxQuery
	{
		const void* Renderer;
		float Min[3], Max[3];
	};
	void QueryBoxes(GfxContext* dc, const std::vector<BoxQuery>& boxes);   // FinishDepthPrepass 안 (같은 깊이 타깃)
	bool BeginPredicated(GfxContext* dc, const void* renderer);           // true 면 그린 뒤 EndPredicated
	void EndPredicated(GfxContext* dc);

	// CPU 가 만든 인스턴스 목록 (나무처럼 LOD 마다 인스턴싱): 이 뷰의 Hi-Z 로 인스턴스마다 경계 구를 검사해
	//  보이는 것만 GPU 버퍼에 이어 쓰고 간접 그리기 인자를 센다 (FinishDepthPrepass 뒤 — 본 패스)
	struct ListDraw
	{
		uint32_t Args[5];   // DrawIndexedInstancedIndirect (IndexCount, -, StartIndex, BaseVertex, 0) 또는 DrawInstancedIndirect (VertexCount, -, StartVertex, 0)
		bool Indexed;
	};
	bool HasHiZ();   // 지금 뷰의 Hi-Z 가 있다 (깊이 프리패스 뒤)
	// 그림자 캐스터 (방향광 캐스케이드 — 매 프레임 다시 그리는 것만): frame.Casters 의 상자 = 빛 방향으로 쓸어 늘린 상자 (그림자가 떨어질 수 있는 곳),
	//  Items[0] / Batches[0] = 깊이 묶음. 그 상자가 카메라 Hi-Z 에 모두 가려진 캐스터는 빼고 ShadowSet 으로 간접 그리기. false = CPU 목록
	bool BeginShadow(GfxContext* dc, Frame& frame);
	// 반환 = 목록 번호, -1 = 못 했다 (호출한 쪽이 CPU 목록으로 그린다). spheres = 인스턴스마다 xyz 중심 + 반지름
	int CullList(GfxContext* dc, const void* instances, uint32_t stride, uint32_t count, const float* spheres, const ListDraw* draws, int drawCount);
	// 걸러진 인스턴스를 instanceSlot 정점 버퍼에 묶고 draw 번째 그리기 (입력 배치 · 셰이더 · 0 번 버퍼는 호출한 쪽이)
	void DrawList(GfxContext* dc, int list, int draw, uint32_t instanceSlot);

	struct Stats
	{
		int Tested = 0;     // 절두체 안 렌더러 (GPU 가 검사)
		int Visible = 0;    // 그중 가려지지 않은 것
		bool Active = false;   // 마지막 뷰가 오클루전 컬링을 썼다
		uint64_t Frames = 0;   // GPU 결과를 읽은 횟수
		int Queries = 0;       // 마지막 뷰의 오클루전 쿼리 (Skinned Mesh Renderer) 수
		int QueriesHidden = 0; // 그중 가려져 GPU 가 그리기를 건너뛴 수 (한 프레임 전 결과)
		int ListTested = 0, ListVisible = 0;   // 인스턴스 목록 (나무) 검사 · 보임
		int ShadowTested = 0, ShadowVisible = 0;   // 그림자 캐스터 (캐스케이드를 모두 더함) 검사 · 남김
	};
	// 몇 프레임 늦은 GPU 결과 (기다리지 않고 읽는다)
	const Stats& LastStats(bool editor);
	bool Supported(GfxContext* dc);
	void RegisterEditor();   // nova occlusion
	std::string InfoJson();  // nova occlusion info 와 같은 JSON (안드로이드 기기 검사의 NOVA_TEST 줄)
}
