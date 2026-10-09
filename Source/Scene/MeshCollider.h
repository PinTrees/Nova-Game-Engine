#pragma once
#include "Collider.h"

class Mesh;

// Unity 의 Mesh Collider. 메시는 같은 GameObject 의 MeshFilter 에서 가져온다.
// Convex 가 꺼져 있으면 삼각형 메시(정적/키네마틱 전용), 켜져 있으면 볼록 껍질로 시뮬레이션한다.
// (Unity 와 같이 Dynamic Rigidbody 에서는 Convex 만 지원 → 자동으로 볼록 껍질 사용)
class MeshCollider
	: public Collider
{
private:
	bool m_Convex = false;
	int m_CookingOptions = 0;

public:
	MeshCollider();
	~MeshCollider();

public:
	bool IsConvex() const { return m_Convex; }
	void SetConvex(bool convex) { MarkPhysicsDirty(); m_Convex = convex; }
	Mesh* GetMesh() const;
	int GetSubsetIndex() const;

public:
	virtual void OnDrawGizmos() override;
	virtual void OnInspectorGUI() override;
	virtual const char* InspectorIconName() const override { return "mesh_collider"; }

	GENERATE_COMPONENT_BODY(MeshCollider)
};

REGISTER_COMPONENT(MeshCollider)
