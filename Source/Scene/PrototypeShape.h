#pragma once
#include "Component.h"

// 블록아웃 도형 (ProBuilder · UE 모델링 도구의 기본 도형처럼): 크기를 숫자로 정하면 메시를 만든다.
//  Transform 크기로 늘리는 것과 달리 계단은 단 높이가 그대로 (단 수가 높이에 맞춰), 아치 · 원호 벽은 두께가 그대로다.
//  같은 오브젝트의 Mesh Filter 에 메시를 넣고 (저장하지 않는다 — 값만 저장하고 다시 만든다), Mesh Collider 가 있으면 그 메시로 부딪힌다.
//  UV 는 미터 단위 (면마다 그 면의 평면에 투영) — Prototype 재질 (World Space UV) 이 아니어도 격자가 1 m
class PrototypeShape : public Component
{
public:
	enum class Shape { Box, Stairs, Ramp, Cylinder, Cone, ArchWall, CurvedWall, Count };

	Shape Kind = Shape::Box;
	Vec3 Size = Vec3(4, 3, 4);        // 너비 X · 높이 Y · 깊이 Z (m). 원기둥 · 원뿔은 X · Z 가 지름, 원호 벽은 Z = 두께
	float StepHeight = 0.2f;          // 계단 한 단 높이 (m)
	int Segments = 24;                // 원기둥 · 원뿔 · 아치 · 원호 둘레 나누기
	float OpeningWidth = 1.6f;        // 아치 문벽: 구멍 너비 · 높이 (반원 꼭대기까지)
	float OpeningHeight = 2.6f;
	float Radius = 10.0f;             // 원호 벽: 안쪽 반지름 (m)
	float Angle = 90.0f;              // 원호 벽: 각도 (도, 360 = 원통 껍질)
	int Revision = 1;

	PrototypeShape();
	virtual ~PrototypeShape();

	void Rebuild();
	static const char* ShapeName(Shape s);

	virtual void Awake() override { EnsureBuilt(); }
	virtual void Update() override { EnsureBuilt(); }
	virtual void _Editor_Update() override { EnsureBuilt(); }
	virtual void OnInspectorGUI() override;
	virtual bool UsesUnityInspector() const override { return true; }
	virtual bool AffectsPhysics() const override { return true; }   // 크기를 바꾸면 Mesh Collider 바디를 다시
	virtual const char* InspectorIconName() const override { return "mesh_filter"; }
	virtual std::string InspectorTitle() const override { return "Prototype Shape"; }

	GENERATE_COMPONENT_BODY(PrototypeShape)

private:
	int m_BuiltRevision = 0;
	void EnsureBuilt();
};

REGISTER_COMPONENT(PrototypeShape)
