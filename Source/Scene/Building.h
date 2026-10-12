#pragma once
#include "Component.h"
#include "BuildingDesc.h"
#include <functional>

class Mesh;
class UMaterial;
namespace BuildingGenerator { struct Output; }

// 건축물 (Building Generator): BuildingDesc 값으로 집 · 성 · 교회 · 탑을 식으로 짓는다 (PrototypeShape 와 같은 길).
//  같은 오브젝트의 Mesh Filter 에 메시를 넣고 (저장하지 않는다 — 값만 저장하고 다시 짓는다) Mesh Renderer 재질 칸 11 개
//  (BuildingGenerator::Slot) 에 식으로 구운 재질 (BuildingTextures) 을 넣는다. Mesh Collider 가 있으면 그 메시로 부딪힌다.
//  값은 Inspector 또는 Window > Building Generator (양식 갤러리 · 단계별 대화상자 · 3D 미리 보기) 로 바꾼다.
class Building : public Component
{
public:
	BuildingDesc Desc;
	int Revision = 1;

	// 마지막으로 지은 결과 (Inspector · CLI)
	struct Stats
	{
		int Vertices = 0, Triangles = 0, Subsets = 0, Windows = 0, Doors = 0, Wings = 0, Towers = 0;
		float Ms = 0.0f;
		XMFLOAT3 Size = { 0, 0, 0 };
	};
	const Stats& GetStats() const { return m_Stats; }

	Building();
	virtual ~Building();

	void SetDesc(const BuildingDesc& d);   // 값 바꾸기 + 다시 짓기 예약
	void Rebuild();

	// 생성 결과 → 엔진 메시 (재질 칸마다 서브셋, 정점 65535 개마다 나눈다) · 재질 (칸마다)
	static std::shared_ptr<Mesh> MakeMesh(const BuildingGenerator::Output& out, const std::string& name);
	static std::vector<std::shared_ptr<UMaterial>> MakeMaterials(const BuildingDesc& d);

	// Window > Building Generator 를 이 건물로 연다 (편집기가 등록)
	static std::function<void(Building*)> s_OpenGenerator;

	virtual void Awake() override { EnsureBuilt(); }
	virtual void Update() override { EnsureBuilt(); }
	virtual void _Editor_Update() override { EnsureBuilt(); }
	virtual void OnInspectorGUI() override;
	virtual bool UsesUnityInspector() const override { return true; }
	virtual bool AffectsPhysics() const override { return true; }
	virtual const char* InspectorIconName() const override { return "mesh_filter"; }
	virtual std::string InspectorTitle() const override { return "Building"; }

	GENERATE_COMPONENT_BODY(Building)

private:
	int m_BuiltRevision = 0;
	Stats m_Stats;
	void EnsureBuilt();
};

REGISTER_COMPONENT(Building)
