#pragma once
#include "MemoryStats.h"
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
	void BeginView();                     // 화면(Game / Scene 뷰) 그리기 시작에 한 번: 나무 목록을 다시 모으게
	void DrawAll(Pass pass, bool editor); // Scene 의 각 패스에서 한 번

	struct MeshInfo
	{
		Vec3 BoundsMin, BoundsMax;
		int Vertices = 0, Triangles = 0, Branches = 0, LeafCards = 0;
		const std::vector<XMFLOAT3>* Positions = nullptr;   // 바람 전 위치 (선택용)
		const std::vector<uint32_t>* Indices = nullptr;
	};
	bool GetMeshInfo(const TreeDesc& desc, MeshInfo& out, int lod = 0);
	// 씬 스트리밍: 지형에 칠한 나무의 메시 (LOD 0 · 1) 와 위치 캐시를 바꿔 끼우기 전에 (처음 그리는 프레임이 만들던 것)
	void PrewarmTerrain(const class TerrainData& data, const Vec3& origin);
	// 씬 스트리밍: 나무 메시 데이터 (LOD 0 · 1) 를 백그라운드 잡에서 만들어 둔다 — 처음 그릴 때는 GPU 버퍼만
	void PrewarmAsync(const TreeDesc& desc);
	// 메인 (GPU): 메시 올리기 (LOD0 · 1) · 잎 · 껍질 텍스처 · 임포스터 굽기 — 처음 그리는 프레임에 하던 것 (씬 스트리밍의 미리 데우기)
	void PrewarmGpu(const TreeDesc& desc);

	// 바깥 나무 (PCG 의 절차적 나무 — 바이옴 식생): 화면마다 한 번 불린다. emit(desc, worlds, count) 로 넘기면 Tree 컴포넌트 · 지형 나무와 같이
	//  LOD · 임포스터 · 바람 · 그림자로 그린다. desc 는 그 프레임 동안 살아 있어야 한다 (캐시된 종 — 지우지 않는다)
	using TreeEmit = std::function<void(const TreeDesc& desc, const XMFLOAT4X4* worlds, size_t count)>;
	void SetExternalSource(const std::string& owner, std::function<void(const Vec3& eye, bool editor, const TreeEmit& emit)> source);

	// 바깥 셀 (PCG 셀 — 풀 · 꽃 · 덤불처럼 수만 그루): 셀마다 GPU 인스턴스 버퍼를 한 번 만들고 (CreateCellBuffer), 그릴 때는 셀마다
	//  절두체 · 거리 범위만 CPU 가 보고 LOD 단계 (LOD0 · LOD1 · 임포스터 · 사라짐) 는 정점 셰이더가 인스턴스마다 카메라 거리로 고른다
	//  (화면 디더 섞기 그대로 — 인스턴스마다 CPU 일이 없다). 가까운 큰 나무 셀 (키 4 m 넘게, 임포스터 거리 안) 은 인스턴스마다 (CPU · 오클루전)
	struct CellBuffer;
	std::shared_ptr<CellBuffer> CreateCellBuffer(const XMFLOAT4X4* worlds, size_t count);
	struct CellDraw
	{
		const TreeDesc* Desc = nullptr;              // 프레임 동안 살아 있어야 한다 (캐시된 종)
		std::shared_ptr<CellBuffer> Instances;       // CreateCellBuffer
		const XMFLOAT4X4* Worlds = nullptr;          // 같은 인스턴스 (CPU) — 가까운 큰 나무를 인스턴스마다 그릴 때
		size_t Count = 0;
		Vec3 Min, Max;                               // 인스턴스 뿌리 위치 범위
		float MinScale = 1.0f, MaxScale = 1.0f;      // 인스턴스 크기 배율 범위 (LOD 거리 = 카메라 거리 / 배율)
		float ShadowDistance = 1e9f;                 // 이 거리 (m) 너머는 그림자에 그리지 않는다 (PCG 스포너의 Shadow Distance, 0 = 그림자 없음)
	};
	using CellEmit = std::function<void(const CellDraw& cell)>;
	void SetExternalCells(const std::string& owner, std::function<void(const Vec3& eye, bool editor, const CellEmit& emit)> source);

	// 프로토타입 미리보기 = 임포스터 앞면 (없으면 굽는다). uv0/uv1 = 아틀라스 안 영역
	ImTextureID Thumbnail(const TreeDesc& desc, ImVec2& uv0, ImVec2& uv1);

	struct Stats
	{
		int Trees = 0;                      // 절두체 안 나무
		int Lod0 = 0, Lod1 = 0, Billboards = 0;
		int DrawCalls = 0;
		int CellDraws = 0;                  // 셀째 그린 묶음 (정점 셰이더가 LOD)
	};
	const Stats& LastStats(bool editor);   // 마지막 본 패스

	// Profiler 메모리: 나무 메시(GPU 버퍼 + 선택용 CPU 사본) / 임포스터 아틀라스
	void CollectMemory(std::vector<MemoryStats::Item>& gpu, std::vector<MemoryStats::Item>& cpu);
}
