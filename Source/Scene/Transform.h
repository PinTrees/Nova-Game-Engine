#pragma once
#include "Component.h"
#include "TransformStore.h"



// 데이터 지향 Transform (docs/TRANSFORM_SOA.md): 월드 행렬 · 회전 · 크기는 TransformStore 의 SoA 배열에 있다 (이 객체는 자리 번호만).
//  로컬 값을 바꾸면 배열에 복사하고 아래 계층에 '더러움' 표시만 한다 — 월드 값은 읽을 때 또는 프레임마다 한 번에 (Job System) 계산된다.
//  공개 함수 (Get* · Set*) 의 뜻은 예전과 같다: Set 직후의 Get 도 늘 새 값이다
class NOVA_API Transform : public Component
{
	using Super = Component;

private:
	// 부모는 약한 참조 (자식은 부모의 _children 에 강하게 잡힌다 — 둘 다 강하면 부모 ↔ 자식 순환이라 둘 다 해제되지 않는다.
	//  씬 힙의 누수 보고가 찾았다: 도시 장면을 지운 뒤 Transform 32 개)
	weak_ptr<Transform> _parent;
	Transform* Parent() const { return _parent.lock().get(); }   // 부모는 그 GameObject 가 잡고 있다
	vector<shared_ptr<Transform>> _children;

	uint32_t m_Slot = 0;   // TransformStore 의 자리

	// 로컬 값 (기준 값 — 인스펙터 · 저장이 이 멤버를 쓴다. 바꾸면 UpdateTransform 으로 배열에)
	Vec3 m_LocalScale = Vec3::One;
	Vec3 m_LocalPosition = Vec3::Zero;
	Vec3 m_LocalEulerAngles = Vec3::Zero;
	Vec3 m_LocalEulerRadians = Vec3::Zero;
	Quaternion m_LocalRotation = Quaternion::Identity;

	// 월드 값의 사본 — 저장 (toJson) 과 오일러 각 캐시용. 기준 값은 TransformStore
	Vec3 m_Scale = Vec3::One;
	Vec3 m_EulerAngles;
	Vec3 m_Position;
	uint32_t m_EulerVersion = UINT32_MAX;   // m_EulerAngles 를 계산한 월드 번호 (오일러 각은 요청할 때만 — atan2 가 비싸다)


public:
	Transform();
	~Transform();
	Transform(const Transform&) = delete;
	Transform& operator=(const Transform&) = delete;

	virtual void Awake() override;
	virtual void Update() override;

	// 로컬 멤버 → 배열 + 아래 계층 더러움 (멤버를 직접 바꾼 뒤 부른다)
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

	// World (배열에서 — 더러우면 그 사슬만 계산)
	Vec3 GetScale() { return Vec3(TransformStore::WorldScale(m_Slot)); }
	void SetScale(const Vec3& scale);
	Vec3 GetEulerAngle();
	void SetEulerAngle(const Vec3& rotation);
	Quaternion GetRotation() { return Quaternion(TransformStore::WorldRotation(m_Slot)); }
	void SetRotation(Quaternion q);

	Vec3 GetPosition() { return Vec3(TransformStore::WorldPosition(m_Slot)); }
	void SetPosition(const Vec3& position);
	// 월드 위치/회전/크기가 주어진 값이 되도록 로컬 값을 다시 계산한다 (부모 변경 시 제자리 유지용)
	void SetWorldPose(const Vec3& position, const Quaternion& rotation, const Vec3& lossyScale);
	void SetPosition(float x, float y, float z) { SetPosition(Vec3(x, y, z)); }

	void Translate(const Vec3& position) { SetPosition(GetPosition() + position); }
	void Rotate(const Vec3& angle) { SetEulerAngle(GetEulerAngle() + angle); }

	Vec3 GetAxis(int index);
	Vec3 GetRight() { return XMVector3Normalize(GetWorldMatrix().Right()); }
	Vec3 GetLeft() { return XMVector3Normalize(GetWorldMatrix().Left()); }
	Vec3 GetUp() { return XMVector3Normalize(GetWorldMatrix().Up()); }
	Vec3 GetDown() { return XMVector3Normalize(GetWorldMatrix().Down()); }
	Vec3 GetLook() { return XMVector3Normalize(GetWorldMatrix().Backward()); }
	Vec3 GetForward() { return GetLook(); }
	Vec3 GetBackward() { return XMVector3Normalize(GetWorldMatrix().Forward()); }
	Matrix GetWorldMatrix() { return Matrix(TransformStore::World(m_Slot)); }

	// 배열의 자리 · 월드 번호 (월드가 다시 계산될 때마다 +1 — 컬링이 행렬 대신 번호를 비교한다)
	uint32_t Slot() const { return m_Slot; }
	uint32_t WorldVersion() const { return TransformStore::Version(m_Slot); }

	// 계층 관계
	bool HasParent() { return !_parent.expired(); }

	shared_ptr<Transform> GetParent() { return _parent.lock(); }
	void SetParent(shared_ptr<Transform> parent);

	const vector<shared_ptr<Transform>>& GetChildren() const { return _children; }
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
