#pragma once
#include <vector>
#include <string>
#include <memory>
#include <cstdint>

class dtNavMesh;
class dtNavMeshQuery;
class dtTileCache;
struct dtTileCacheAlloc;
struct dtTileCacheCompressor;
struct dtTileCacheMeshProcess;

// 굽기 설정 (Unity NavMesh Surface 의 Agent / Voxel Size)
struct NavBakeSettings
{
	float CellSize = 0.25f;     // Voxel Size (m)
	float AgentRadius = 0.5f;
	float AgentHeight = 2.0f;
	float MaxSlope = 45.0f;     // 도
	float StepHeight = 0.4f;    // m
};

// Off-Mesh Link (Unity 의 NavMesh Link): 끊긴 두 곳을 잇는 연결 (점프·사다리·문). 굽기와 따로 — 실행 중에 바꿔도 된다
struct NavLink
{
	Vec3 Start, End;
	float Width = 0.0f;         // 끝점을 메시에 붙일 때 찾는 반경에 더한다
	bool Bidirectional = true;
};

// NavMesh Obstacle 의 깎기(Carve) 모양
struct NavObstacleShape
{
	bool Box = true;
	Vec3 Center;                // 상자: 가운데 / 원기둥: 아래 가운데
	Vec3 HalfExtents;           // 상자
	float YRadians = 0.0f;      // 상자: Y 회전
	float Radius = 0.5f;        // 원기둥
	float Height = 2.0f;        // 원기둥
};

// 구운 내비게이션 메시 = Recast / Detour (Unity 의 NavMesh 와 같은 라이브러리 계열).
//  굽기: 정적 콜라이더 삼각형 → 타일(64 복셀)마다 Recast(복셀화 → 걸을 수 있는 면 → 반지름만큼 깎기 → 높이 층(layer))
//        → 압축(FastLZ)한 층을 Detour TileCache 에 (여러 스레드). 층 → 영역 → 윤곽 → 다각형 → Detour 타일.
//  실행 중: 장애물(NavMesh Obstacle Carve)·Off-Mesh Link 가 바뀌면 닿은 타일만 층에서 다시 만든다 (몇 ms).
//  길 찾기: Detour findPath(A*, 다각형) + findStraightPath(꺾이는 점, Off-Mesh Link 표시).
class NavData
{
public:
	NavBakeSettings Settings;
	float BoundsMin[3] = {}, BoundsMax[3] = {};
	int TileCount = 0;          // 층 타일 수
	int PolyCount = 0;
	unsigned Revision = 0;      // 타일이 다시 만들어질 때마다 증가 (에이전트가 길을 다시 찾는다)

	// FindPath 의 꺾이는 점 표시
	enum CornerFlag : unsigned char { CornerLinkStart = 1 };

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
	// flags[i] & CornerLinkStart = i 에서 Off-Mesh Link 가 시작해 i+1 에서 끝난다
	bool FindPath(const Vec3& start, const Vec3& end, std::vector<Vec3>& corners, bool* partial = nullptr,
		std::vector<unsigned char>* flags = nullptr) const;
	// from → to 를 메시 위로 미끄러지며 (벽에서 멈춤), 바닥 높이까지
	bool MoveAlongSurface(const Vec3& from, const Vec3& to, Vec3& out) const;

	// ---- Off-Mesh Link: 목록이 바뀌면 닿는 타일만 다시 만든다
	void SetLinks(const std::vector<NavLink>& links);
	const std::vector<NavLink>& GetLinks() const { return m_Links; }

	// ---- 장애물 깎기 (NavMesh Obstacle Carve). 0 = 실패. UpdateObstacles 가 실제로 타일을 다시 만든다
	uint32_t AddObstacle(const NavObstacleShape& shape);
	void RemoveObstacle(uint32_t id);
	void UpdateObstacles();     // 매 프레임: 밀린 요청 처리 (바뀌었으면 Revision 증가)

	// Scene 뷰: 다각형을 반투명으로 (최대 maxTriangles 개) + Off-Mesh Link 곡선
	void DrawGizmo(unsigned int fillColor, unsigned int edgeColor, int maxTriangles) const;

	// TileCache 가 타일을 만들 때 부른다 (다각형 플래그 + 이 타일에서 시작하는 Off-Mesh Link)
	void ProcessTile(struct dtNavMeshCreateParams* params, unsigned char* polyAreas, unsigned short* polyFlags) const;

private:
	dtNavMesh* m_Mesh = nullptr;
	dtNavMeshQuery* m_Query = nullptr;
	dtTileCache* m_Cache = nullptr;
	std::unique_ptr<dtTileCacheAlloc> m_Alloc;
	std::unique_ptr<dtTileCacheCompressor> m_Compressor;
	std::unique_ptr<dtTileCacheMeshProcess> m_Process;

	std::vector<NavLink> m_Links;
	// Detour 에 넘기는 배열 (m_Links 에서)
	std::vector<float> m_LinkVerts, m_LinkRads;
	std::vector<unsigned char> m_LinkDirs, m_LinkAreas;
	std::vector<unsigned short> m_LinkFlags;
	std::vector<unsigned int> m_LinkIds;
	bool m_ObstaclesPending = false;

	void Reset();
	bool InitQuery();
	void RebuildLinkArrays();
	// 압축한 층들(타일 캐시) → Detour 타일 전부 (여러 스레드). 굽기·읽기 뒤
	bool BuildAllTiles();
	void CountPolys();
};
