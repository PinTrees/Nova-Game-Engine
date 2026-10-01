#pragma once

// 절차적 바위·절벽 메시 (텍스처 파일 없이 수학식으로, 나무 생성기와 같은 생각).
//  모양 = 부호 거리 함수(SDF): 둥근 상자를 크기 비례 도메인 워프로 비틀고 → 무작위 평면으로 잘라 각진 면·깨진 모서리 (모서리는 살짝 깎음)
//        (첨탑 = 거의 육각 주상절리, 절벽 = 세로 면 위주) → 높이마다 들고 나는 지층(턱 + 층 사이 홈)
//        → 세로 균열(겉에서 일정 깊이까지만 판 틈) → 리지 노이즈 거칠기. 아래는 땅에 묻히도록 살짝 넓게.
//  메시 = Surface Nets (격자 칸마다 정점 하나, 부호가 바뀌는 모서리마다 사각형) → 법선은 SDF 기울기, AO 는 SDF 를 법선 쪽으로 짚어 본다.
//  LOD = 격자 해상도 (LOD0 고운 격자 → LOD1 1/2 → LOD2 1/4)
struct RockParams
{
	enum class Shape { Boulder, Block, Cliff, Slab, Spire, Count };
	Shape Kind = Shape::Boulder;
	int Seed = 1;
	float SizeX = 3.0f, SizeY = 2.0f, SizeZ = 2.5f;   // m
	float Roundness = 0.35f;     // 모서리 둥글기 (짧은 변에 대한 비율)
	int Facets = 9;              // 자르는 평면 수 (각진 면)
	float FacetDepth = 0.18f;    // 얼마나 깊게 자르나 (반폭 비율)
	int Strata = 0;              // 지층 수 (0 = 없음)
	float StrataDepth = 0.0f;    // 층마다 들고 나는 정도 (m)
	float StrataGroove = 0.0f;   // 층 사이 홈 깊이 (m)
	float StrataTilt = 0.0f;     // 지층 기울기 (도)
	int Cracks = 0;              // 세로 균열 수
	float CrackDepth = 0.6f;     // 균열이 파고드는 깊이 (m)
	float CrackWidth = 0.08f;    // m
	float ColumnDepth = 0.0f;    // 균열로 나뉜 기둥마다 들고 나는 정도 (m) — 절벽의 블록감
	float Warp = 0.18f;          // 덩어리 비틀기 (짧은 반폭 비율) — 큰 비대칭 형태
	float EdgeSoftness = 0.04f;  // 잘린 모서리 깎기 (짧은 반폭 비율)
	float Noise = 0.12f;         // 표면 거칠기 (m)
	float NoiseScale = 1.2f;     // 거칠기 무늬 크기 (m)
	float Taper = 0.0f;          // 위로 갈수록 좁아짐 (0~0.8)
	float Detail = 1.0f;         // 격자 촘촘함 배율 (1 = 기본)
};

namespace RockGenerator
{
	struct Vertex
	{
		XMFLOAT3 Pos;
		XMFLOAT3 Normal;
		float AO;        // 0~1 (1 = 트임)
		float Cavity;    // -1 볼록 ~ +1 오목 (SDF 라플라시안)
	};

	struct Mesh
	{
		std::vector<Vertex> Vertices;
		std::vector<uint32_t> Indices;
		XMFLOAT3 BoundsMin = {}, BoundsMax = {};
		double Ms = 0.0;
	};

	// lod 0~2. 바닥(y = 0)이 물체 원점 (땅에 놓으면 아랫부분이 조금 묻힌다)
	Mesh Generate(const RockParams& p, int lod);
	float Distance(const RockParams& p, const XMFLOAT3& pos);   // SDF (검사·선택용)
}
