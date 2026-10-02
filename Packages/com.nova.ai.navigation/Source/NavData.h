#pragma once
#include <vector>
#include <string>
#include <cstdint>

class dtNavMesh;
class dtNavMeshQuery;

// 굽기 설정 (Unity NavMesh Surface 의 Agent / Voxel Size)
struct NavBakeSettings
{
	float CellSize = 0.25f;     // Voxel Size (m)
	float AgentRadius = 0.5f;
	float AgentHeight = 2.0f;
	float MaxSlope = 45.0f;     // 도
	float StepHeight = 0.4f;    // m
};

// 구운 내비게이션 메시 = Recast / Detour (Unity 의 NavMesh 와 같은 라이브러리 계열).
//  굽기: 정적 콜라이더 삼각형 → 타일(128 복셀)마다 Recast(복셀화 → 걸을 수 있는 면 → 반지름만큼 깎기 → 영역 → 윤곽 → 다각형 + 높이 세부)
//        → Detour 타일 (여러 스레드). 길 찾기: Detour findPath(A*, 다각형) + findStraightPath(꺾이는 점).
class NavData
{
public:
	NavBakeSettings Settings;
	float BoundsMin[3] = {}, BoundsMax[3] = {};
	int TileCount = 0;
	int PolyCount = 0;

	NavData();
	~NavData();
	NavData(const NavData&) = delete;
	NavData& operator=(const NavData&) = delete;

	bool IsEmpty() const { return m_Mesh == nullptr || PolyCount == 0; }
	bool ContainsXZ(const Vec3& p) const;

	// 삼각형(verts x,y,z / tris 3 개씩)으로 굽는다. log = 결과 한 줄
	bool Bake(const std::vector<float>& verts, const std::vector<int>& tris, const Vec3& boundsMin, const Vec3& boundsMax,
		const NavBakeSettings& settings, std::string& log);
	bool Save(const std::wstring& path) const;
	bool Load(const std::wstring& path);

	// 가장 가까운 메시 위 점 (수평 maxDistance, 수직 max(maxDistance, Agent Height) 안)
	bool Sample(const Vec3& p, float maxDistance, Vec3& out) const;
	// 길: 꺾이는 점들 (처음 = 시작 점, 끝 = 도착 점 — 둘 다 메시 위로). 도착 점에 못 가면 가장 가까운 곳까지 (partial = true)
	bool FindPath(const Vec3& start, const Vec3& end, std::vector<Vec3>& corners, bool* partial = nullptr) const;
	// from → to 를 메시 위로 미끄러지며 (벽에서 멈춤), 바닥 높이까지
	bool MoveAlongSurface(const Vec3& from, const Vec3& to, Vec3& out) const;

	// Scene 뷰: 다각형을 반투명으로 (최대 maxTriangles 개)
	void DrawGizmo(unsigned int fillColor, unsigned int edgeColor, int maxTriangles) const;

private:
	dtNavMesh* m_Mesh = nullptr;
	dtNavMeshQuery* m_Query = nullptr;
	void Reset();
	bool InitQuery();
};
