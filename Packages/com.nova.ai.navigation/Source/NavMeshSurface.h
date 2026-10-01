#pragma once
#include "Component.h"
#include "NavGrid.h"

// Unity AI Navigation 의 NavMeshSurface: 걸을 수 있는 곳을 굽는다 (Bake). 결과는 에셋 파일(.navgrid)로 저장하고
// 씬에는 경로만 둔다 → 게임 빌드가 그 파일을 같이 넣는다.
class NavMeshSurface : public Component
{
public:
	NavGridSettings Settings;
	int CollectObjects = 0;            // 0 All Game Objects (모든 정적 콜라이더), 1 Volume (Center·Size 상자)
	Vec3 Center = Vec3(0.0f, 2.0f, 0.0f);
	Vec3 Size = Vec3(10.0f, 4.0f, 10.0f);
	bool ShowNavMesh = true;
	std::string DataPath;              // Assets/... .navgrid (프로젝트 기준)

	NavMeshSurface();
	~NavMeshSurface();

	static const std::vector<NavMeshSurface*>& All();
	// 점이 들어 있는 (없으면 아무) 구운 표면의 격자
	static const NavGrid* FindGrid(const Vec3& p);

	bool Bake(std::string& log);       // 편집 중이면 임시 물리 월드로
	void Clear();
	const NavGrid* GetGrid();          // 필요하면 파일에서 읽는다

	void Awake() override { GetGrid(); }
	void OnInspectorGUI() override;
	void OnDrawGizmos() override;
	bool UsesUnityInspector() const override { return true; }
	const char* InspectorIconName() const override { return "terrain_paint"; }

	GENERATE_COMPONENT_BODY(NavMeshSurface)

private:
	std::shared_ptr<NavGrid> m_Grid;
	std::string m_LoadedPath;
	std::string m_LastLog;
	std::wstring DefaultDataPath() const;
};

REGISTER_PACKAGE_COMPONENT(NavMeshSurface)
