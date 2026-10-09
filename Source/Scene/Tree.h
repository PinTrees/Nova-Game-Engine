#pragma once
#include "Component.h"
#include "TreeDesc.h"

// NOVA 나무 (Unity 의 Tree / SpeedTree 에 해당하는 우리 엔진 고유 컴포넌트).
//  - TreeDesc(모양 + 색 + 바람 + LOD)로 메시를 절차 생성한다 (TreeGenerator)
//  - 그리기는 TreeRenderer 가 맡는다: 같은 설정의 나무(컴포넌트·지형 나무)를 인스턴싱으로 한 번에,
//    거리에 따라 전체 메시 → 중간 메시 → 구운 빌보드(임포스터)
class Tree : public Component
{
public:
	TreeDesc Desc;

	Tree();
	virtual ~Tree();

	void ApplyPreset(int preset) { Desc.ApplyPreset(preset); }

	// 로컬 범위 / 광선 검사 (Scene 뷰 선택·F 포커스). 전체 메시(LOD0) 기준
	bool GetLocalBounds(Vec3& bmin, Vec3& bmax);
	bool RaycastLocal(const Vec3& origin, const Vec3& dir, float& t);

	// 켜진 Tree 컴포넌트 전부 (TreeRenderer 가 모은다)
	static const std::vector<Tree*>& All();
	bool IsDrawable() const;

	// 매 프레임 한 번 (App 루프): 바람 시간 — 같은 프레임의 모든 패스가 같은 값을 써야 깊이가 맞는다
	static void UpdateAll();

	virtual void PrewarmStaged() override;   // 씬 스트리밍: 나무 메시 (LOD 0 · 1) 를 바꿔 끼우기 전에 생성

	virtual void OnInspectorGUI() override;
	virtual bool UsesUnityInspector() const override { return true; }
	virtual const char* InspectorIconName() const override { return "terrain_trees"; }

	GENERATE_COMPONENT_BODY(Tree)
};

REGISTER_COMPONENT(Tree)
