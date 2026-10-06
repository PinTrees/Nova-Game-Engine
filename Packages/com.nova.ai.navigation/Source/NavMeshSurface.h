#pragma once
#include "Component.h"
#include "NavData.h"

// Unity AI Navigation 의 NavMeshSurface: 걸을 수 있는 곳을 굽는다 (Bake, Recast). 결과는 에셋 파일(.navmesh)로 저장하고
// 씬에는 경로만 둔다 → 게임 빌드가 그 파일을 같이 넣는다.
class NavMeshSurface : public Component
{
public:
	NavBakeSettings Settings;
	// 0 = 3D (XZ 바닥, 정적 콜라이더 삼각형), 1 = 2D (XY 평면 — 탑다운 2D: 상자 · 콜라이더 범위 전체가 바닥, 2D 콜라이더 · 타일맵 콜라이더가 벽)
	int Plane = 0;
	int CollectObjects = 0;            // 0 All Game Objects (모든 정적 콜라이더), 1 Volume (Center·Size 상자)
	Vec3 Center = Vec3(0.0f, 2.0f, 0.0f);
	Vec3 Size = Vec3(10.0f, 4.0f, 10.0f);
	bool ShowNavMesh = true;
	std::string DataPath;              // Assets/... .navmesh (프로젝트 기준)

	NavMeshSurface();
	~NavMeshSurface();

	static const std::vector<NavMeshSurface*>& All();
	// 점이 들어 있는 (없으면 아무) 구운 표면의 내비 메시
	static const NavData* FindData(const Vec3& p);
	static std::shared_ptr<NavData> FindDataShared(const Vec3& p);

	bool Bake(std::string& log);       // 편집 중이면 임시 물리 월드로
	void Clear();
	const NavData* GetData();          // 필요하면 파일에서 읽는다

	void Awake() override { GetData(); }
	// 매 프레임 (편집 중에도): NavMesh Link 가 바뀌었으면 반영, 장애물 깎기 처리
	void LastUpdate() override;
	void OnInspectorGUI() override;
	void OnDrawGizmos() override;
	bool UsesUnityInspector() const override { return true; }
	const char* InspectorIconName() const override { return "terrain_paint"; }

	GENERATE_COMPONENT_BODY(NavMeshSurface)

private:
	std::shared_ptr<NavData> m_Data;
	std::string m_LoadedPath;
	std::string m_LastLog;
	std::wstring DefaultDataPath() const;
	// 2D 굽기: 바닥 사각형 + 2D 콜라이더 윤곽 (삼각형으로) → Recast (내비 공간 (x, 0, y))
	bool Bake2D(NavData& grid, std::string& log);
};

REGISTER_PACKAGE_COMPONENT(NavMeshSurface)
