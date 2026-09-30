#pragma once
#include "Component.h"



class Transform : public Component
{
	using Super = Component;

private:
	shared_ptr<Transform> _parent;
	vector<shared_ptr<Transform>> _children;

	Vec3 m_LocalScale = Vec3::One;
	Vec3 m_LocalPosition = Vec3::Zero;
	Vec3 m_LocalEulerAngles = Vec3::Zero;
	Vec3 m_LocalEulerRadians = Vec3::Zero;
	Quaternion m_LocalRotation = Quaternion::Identity;

	// Cache
	Matrix m_LocalMatrix = Matrix::Identity;
	Matrix m_WorldMatrix = Matrix::Identity;

	Vec3 m_Scale;
	Vec3 m_EulerAngles;
	Vec3 m_Position;
	Quaternion m_Rotation;


public:
	Transform();
	~Transform();

	virtual void Awake() override;
	virtual void Update() override;

	void UpdateTransform();

	// ---- 회전 규약 (Unity 와 동일) ----
	// 오일러 각 (x, y, z) 는 Z → X → Y 순서로 적용한다 (= XMQuaternionRotationRollPitchYaw, Unity 의 Quaternion.Euler).
	// 회전의 기준 값은 쿼터니언(m_LocalRotation)이고, 오일러 각은 표시/입력용이다.
	static Quaternion EulerToQuaternion(const Vec3& degrees);
	static Quaternion CreateQuaternion(double x, double y, double z);   // 라디안
	static Vec3 ToEulerRadians(Quaternion q);
	static Vec3 ToEulerAngles(Quaternion q);
	// q 를 오일러 각으로 바꾸되, 같은 회전을 나타내는 값 중 hint 와 가장 가까운 값을 고른다 (179° → -179° 같은 튐 방지)
	static Vec3 ToEulerAnglesNear(Quaternion q, const Vec3& hint);

	// Local
	Vec3 GetLocalScale() { return m_LocalScale; }
	void SetLocalScale(const Vec3& localScale) { m_LocalScale = localScale; UpdateTransform(); }
	Vec3 GetLocalEulerAngles();
	Vec3 GetLocalEulerRadians();
	void SetLocalEulerAngles(const Vec3& angles);
	void SetLocalEulerRadians(const Vec3& radians);
	void SetLocalRotation(Quaternion q);
	Quaternion GetLocalRotation() { return m_LocalRotation; }
	Vec3 GetLocalPosition();
	void SetLocalPosition(const Vec3& localPosition);

	// World
	Vec3 GetScale() { return m_Scale; }
	void SetScale(const Vec3& scale);
	Vec3 GetEulerAngle() { return m_EulerAngles; }
	void SetEulerAngle(const Vec3& rotation);
	Quaternion GetRotation() { return m_Rotation; }
	void SetRotation(Quaternion q);

	Vec3 GetPosition() { return m_Position; }
	void SetPosition(const Vec3& position);
	// 월드 위치/회전/크기가 주어진 값이 되도록 로컬 값을 다시 계산한다 (부모 변경 시 제자리 유지용)
	void SetWorldPose(const Vec3& position, const Quaternion& rotation, const Vec3& lossyScale);
	void SetPosition(float x, float y, float z) { SetPosition(Vec3(x, y, z)); }

	void Translate(const Vec3& position) { SetPosition(GetPosition() + position); }
	void Rotate(const Vec3& angle) { SetEulerAngle(GetEulerAngle() + angle); }

	Vec3 GetAxis(int index) const;
	Vec3 GetRight() { return XMVector3Normalize(m_WorldMatrix.Right()); }
	Vec3 GetLeft() { return XMVector3Normalize(m_WorldMatrix.Left()); }
	Vec3 GetUp() { return XMVector3Normalize(m_WorldMatrix.Up()); } 
	Vec3 GetDown() { return XMVector3Normalize(m_WorldMatrix.Down()); }
	Vec3 GetLook() { return XMVector3Normalize(m_WorldMatrix.Backward()); }
	Vec3 GetForward() { return GetLook(); }
	Vec3 GetBackward() { return XMVector3Normalize(m_WorldMatrix.Forward()); }
	Matrix GetWorldMatrix() { return m_WorldMatrix; }

	// ���� ����
	bool HasParent() { return _parent != nullptr; }

	shared_ptr<Transform> GetParent() { return _parent; }
	void SetParent(shared_ptr<Transform> parent) { _parent = parent; }

	const vector<shared_ptr<Transform>>& GetChildren() { return _children; }
	void AddChild(shared_ptr<Transform> child) { _children.push_back(child); }
	void RemoveChild(shared_ptr<Transform> child);
public:
	virtual void OnInspectorGUI() override;
	virtual bool UsesUnityInspector() const override { return true; }
	virtual bool HasEnabledToggle() const override { return false; }

public:
	GENERATE_COMPONENT_BODY(Transform)
};

REGISTER_COMPONENT(Transform)