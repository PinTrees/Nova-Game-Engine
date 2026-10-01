#pragma once
#include <vector>
#include <string>

// 걸을 수 있는 곳을 격자(층 여러 개)로 구운 내비게이션 데이터.
//  칸마다 최대 kLayers 개의 바닥 높이(위층 다리·아래층 땅 등) — 없으면 NaN. 이웃 칸과 높이 차가 Step Height 이하면 이어진다.
//  굽기 = 물리 레이캐스트(정적 콜라이더): 기울기 ≤ Max Slope, 머리 위 Agent Height 만큼 빈 곳, 가장자리·벽에서 Agent Radius 만큼 깎음.
//  길 찾기 = A*(8 방향, 모서리 가로지르기 없음) → 보이는 곳까지 건너뛰어 꺾이는 점만 남김.
//  (Recast 같은 다각형 내비 메시로 바꿀 때 NavMeshSurface / NavMeshAgent / C# API 는 그대로 둔다)
struct NavGridSettings
{
	float CellSize = 0.25f;     // m
	float AgentRadius = 0.5f;
	float AgentHeight = 2.0f;
	float MaxSlope = 45.0f;     // 도
	float StepHeight = 0.4f;    // m
};

class NavGrid
{
public:
	static constexpr int kLayers = 4;

	Vec3 Origin;                // 격자 (0,0) 칸의 모서리 (x, 최소 y, z)
	int Width = 0, Depth = 0;
	NavGridSettings Settings;
	std::vector<float> Heights; // (z * Width + x) * kLayers + l, NaN = 없음

	bool IsEmpty() const { return Width == 0 || Depth == 0; }
	int WalkableCount() const;
	float Height(int x, int z, int l) const { return Heights[((size_t)z * Width + x) * kLayers + l]; }
	bool Valid(int x, int z, int l) const;

	// 정적 콜라이더로 굽는다 (PhysicsManager 월드가 있어야 한다 — 편집 중이면 BeginEditQueries). log = 결과 한 줄
	bool Bake(const Vec3& boundsMin, const Vec3& boundsMax, const NavGridSettings& settings, std::string& log);

	bool Save(const std::wstring& path) const;
	bool Load(const std::wstring& path);

	// 점 → 가장 가까운 걸을 수 있는 칸 (수평 maxDistance 안, 높이는 가까운 층)
	bool Sample(const Vec3& p, float maxDistance, int& x, int& z, int& l) const;
	Vec3 CellCenter(int x, int z, int l) const;
	// p 아래(위) 바닥 높이 (가장 가까운 층). 없으면 false
	bool GroundHeight(const Vec3& p, float& y) const;

	// 길: start → end 꺾이는 점들 (처음 = 시작 점의 바닥, 끝 = 도착 점의 바닥). 못 가면 false
	bool FindPath(const Vec3& start, const Vec3& end, std::vector<Vec3>& corners) const;

private:
	int Index(int x, int z, int l) const { return (z * Width + x) * kLayers + l; }
	int ConnectedLayer(int x, int z, float h) const;   // 이웃 칸에서 높이 h 와 이어지는 층 (없으면 -1)
	bool LineOfSight(int x0, int z0, int l0, int x1, int z1) const;

	// A* 작업 공간 (칸 수만큼, 세대 번호로 매번 지우지 않고 다시 씀) — 메인 스레드에서만
	mutable std::vector<float> m_G;
	mutable std::vector<int> m_From;
	mutable std::vector<uint32_t> m_Stamp;
	mutable uint32_t m_Generation = 0;
};
