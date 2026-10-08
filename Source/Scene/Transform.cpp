#include "pch.h"
#include "Transform.h"
#include "UnityGUI.h"
#include "EditorGUI.h"
#include "RectTransform.h"
#include "TransformStore.h"

// Unity 와 같은 오일러 순서(Z → X → Y). 라디안 입력
Quaternion Transform::CreateQuaternion(double x, double y, double z)
{
	Quaternion q = XMQuaternionRotationRollPitchYaw((float)x, (float)y, (float)z);
	q.Normalize();
	return q;
}

Quaternion Transform::EulerToQuaternion(const Vec3& d)
{
	return CreateQuaternion(XMConvertToRadians(d.x), XMConvertToRadians(d.y), XMConvertToRadians(d.z));
}

Transform::Transform()
{
	m_InspectorTitleName = "Transform";
	m_InspectorIconPath = L"transform.png";
	m_Slot = TransformStore::Allocate(this);
}

Transform::~Transform()
{
	TransformStore::Release(m_Slot);   // 이 자리를 부모로 둔 자식은 루트가 된다 (세대 번호 — 예전 weak_ptr 부모가 사라진 것과 같다)
}

void Transform::Awake()
{
}

void Transform::Update()
{
}


Vec3 Transform::ToEulerRadians(Quaternion q)
{
	// 행렬 M = Rz * Rx * Ry (행 벡터) 에서: M32 = -sin(x), M31 = cos(x)sin(y), M33 = cos(x)cos(y), M12 = sin(z)cos(x), M22 = cos(z)cos(x)
	q.Normalize();
	Matrix m = Matrix::CreateFromQuaternion(q);
	Vec3 r;
	// asin 대신 atan2 로 x 를 구해 ±90° 부근에서도 정밀도를 유지한다
	const float cx = sqrtf(m._31 * m._31 + m._33 * m._33);
	r.x = atan2f(-m._32, cx);
	if (cx > 1e-6f)
	{
		r.y = atan2f(m._31, m._33);
		r.z = atan2f(m._12, m._22);
	}
	else
	{
		// 짐벌 락 (x = ±90°): z 를 0 으로 두고 y 에 합친다
		r.y = atan2f(-m._13, m._11);
		r.z = 0.0f;
	}
	return r;
}

Vec3 Transform::ToEulerAnglesNear(Quaternion q, const Vec3& hint)
{
	const Vec3 a = ToEulerAngles(q);
	// 같은 회전의 두 번째 표현: (180 - x, y + 180, z + 180)
	const Vec3 b(180.0f - a.x, a.y + 180.0f, a.z + 180.0f);
	auto wrapNear = [](float v, float h) {
		while (v - h > 180.0f) v -= 360.0f;
		while (v - h < -180.0f) v += 360.0f;
		return v;
	};
	auto nearest = [&](const Vec3& e) { return Vec3(wrapNear(e.x, hint.x), wrapNear(e.y, hint.y), wrapNear(e.z, hint.z)); };
	const Vec3 na = nearest(a), nb = nearest(b);
	const float da = (na - hint).LengthSquared(), db = (nb - hint).LengthSquared();
	Vec3 r = da <= db ? na : nb;
	// 부동소수점 잡음 정리 (-0.0 → 0)
	for (float* v : { &r.x, &r.y, &r.z })
		if (fabsf(*v) < 1e-4f) *v = 0.0f;
	return r;
}

Vec3 Transform::ToEulerAngles(Quaternion q)
{
	Vec3 angles;

	angles = ToEulerRadians(q);
	angles.x = XMConvertToDegrees(angles.x);
	angles.y = XMConvertToDegrees(angles.y);
	angles.z = XMConvertToDegrees(angles.z);

	return angles;
}


void Transform::UpdateTransform()
{
	// 로컬 값을 SoA 배열에 두고 이 Transform 과 아래 계층을 '더러움' 으로 — 월드 값은 읽을 때 또는 프레임마다 한 번에 계산된다
	//  (예전: 바꿀 때마다 하위 계층 전체의 행렬 · 회전 · 오일러 각을 즉시 다시 계산)
	TransformStore::SetLocal(m_Slot, m_LocalPosition, m_LocalRotation, m_LocalScale);
}

void Transform::SetParent(shared_ptr<Transform> parent)
{
	_parent = parent;
	TransformStore::SetParent(m_Slot, parent ? (int32_t)parent->m_Slot : -1);
}

Vec3 Transform::GetEulerAngle()
{
	// 월드 오일러 각은 요청할 때만 (월드 회전이 바뀐 뒤 처음 한 번)
	const Quaternion rotation = GetRotation();
	const bool fresh = !TransformStore::IsDirty(m_Slot);   // 메인이 아닌 스레드에서 더러운 값을 읽었으면 캐시하지 않는다
	const uint32_t version = TransformStore::Version(m_Slot);
	if (fresh && version == m_EulerVersion)
		return m_EulerAngles;
	Vec3 e = ToEulerAngles(rotation);
	for (float* v : { &e.x, &e.y, &e.z })
	{
		if (*v < 0.0f) *v += 360.0f;
		if (fabsf(*v) < 1e-4f || fabsf(*v - 360.0f) < 1e-4f) *v = 0.0f;
	}
	if (fresh)
	{
		m_EulerAngles = e;
		m_EulerVersion = version;
	}
	return e;
}

Vec3 Transform::GetLocalEulerAngles()
{
	//return ToEulerAngles(m_LocalRotation);
	return m_LocalEulerAngles;
}

Vec3 Transform::GetLocalEulerRadians()
{
	//return ToEulerRadians(m_LocalRotation);
	return m_LocalEulerRadians;
}

void Transform::SetLocalEulerAngles(const Vec3& angles)
{
	m_LocalEulerAngles = angles;
	m_LocalEulerRadians.x = ::XMConvertToRadians(angles.x);
	m_LocalEulerRadians.y = ::XMConvertToRadians(angles.y);
	m_LocalEulerRadians.z = ::XMConvertToRadians(angles.z);
	m_LocalRotation = CreateQuaternion(m_LocalEulerRadians.x, m_LocalEulerRadians.y, m_LocalEulerRadians.z);
	UpdateTransform();
}

void Transform::SetLocalEulerRadians(const Vec3& radians)
{
	m_LocalEulerRadians = radians;
	m_LocalEulerAngles.x = ::XMConvertToDegrees(radians.x);
	m_LocalEulerAngles.y = ::XMConvertToDegrees(radians.y);
	m_LocalEulerAngles.z = ::XMConvertToDegrees(radians.z);
	m_LocalRotation = CreateQuaternion(m_LocalEulerRadians.x, m_LocalEulerRadians.y, m_LocalEulerRadians.z);
	UpdateTransform();

}

void Transform::SetLocalRotation(Quaternion q)
{
	q.Normalize();
	m_LocalRotation = q;
	// 표시용 오일러 각은 이전 값과 가장 가까운 표현을 쓴다 (Unity Inspector 와 같은 동작)
	m_LocalEulerAngles = ToEulerAnglesNear(q, m_LocalEulerAngles);
	m_LocalEulerRadians = Vec3(XMConvertToRadians(m_LocalEulerAngles.x), XMConvertToRadians(m_LocalEulerAngles.y), XMConvertToRadians(m_LocalEulerAngles.z));
	UpdateTransform();
}

Vec3 Transform::GetLocalPosition()
{
	return m_LocalPosition;
}

void Transform::SetLocalPosition(const Vec3& localPosition)
{
	m_LocalPosition = localPosition;
	UpdateTransform();
}

void Transform::SetScale(const Vec3& worldScale)
{
	if (HasParent())
	{
		Vec3 parentScale = Parent()->GetScale();
		Vec3 scale = worldScale / parentScale;
		SetLocalScale(scale);
	}
	else
	{
		SetLocalScale(worldScale);
	}
}

void Transform::SetEulerAngle(const Vec3& worldRotation)
{
	SetRotation(EulerToQuaternion(worldRotation));
}

void Transform::SetRotation(Quaternion q)
{
	if (HasParent())
	{
		// world = local * parent  →  local = world * inverse(parent)
		Quaternion parentInv;
		Parent()->GetRotation().Inverse(parentInv);
		Quaternion local = q * parentInv;
		local.Normalize();
		SetLocalRotation(local);
	}
	else
	{
		SetLocalRotation(q);
	}
}

void Transform::SetPosition(const Vec3& worldPosition)
{
	if (HasParent())
	{
		Matrix worldToParentLocalMatrix = Parent()->GetWorldMatrix().Invert();
		Vec3 position = Vec3::Transform(worldPosition, worldToParentLocalMatrix);

		SetLocalPosition(position);
	}
	else
	{
		SetLocalPosition(worldPosition);
	}
}

void Transform::SetWorldPose(const Vec3& position, const Quaternion& rotation, const Vec3& lossyScale)
{
	// 행렬 분해(비균일 스케일 + 회전이면 기울어짐 때문에 실패할 수 있음) 대신 위치/회전/크기를 따로 역변환한다 (Unity 와 같은 방식)
	if (HasParent())
	{
		m_LocalPosition = Vec3::Transform(position, Parent()->GetWorldMatrix().Invert());
		Quaternion parentInv;
		Parent()->GetRotation().Inverse(parentInv);
		Quaternion local = rotation * parentInv;
		const Vec3 ps = Parent()->GetScale();
		auto safeDiv = [](float a, float b) { return fabsf(b) > 1e-6f ? a / b : a; };
		m_LocalScale = Vec3(safeDiv(lossyScale.x, ps.x), safeDiv(lossyScale.y, ps.y), safeDiv(lossyScale.z, ps.z));
		SetLocalRotation(local);   // UpdateTransform 포함
	}
	else
	{
		m_LocalPosition = position;
		m_LocalScale = lossyScale;
		SetLocalRotation(rotation);
	}
}

Vec3 Transform::GetAxis(int index)
{
	if (index < 0 || index > 2)
	{
		std::cout << "Transform::GetAxis: index out of range" << std::endl;
		return Vector3();
	}
	const Matrix world = GetWorldMatrix();
	Vector3 axis(world(index, 0), world(index, 1), world(index, 2));
	axis.Normalize();  // ���͸� ����ȭ

	return axis;
}

void Transform::RemoveChild(shared_ptr<Transform> child)
{
	auto it = std::find(_children.begin(), _children.end(), child);
	if (it != _children.end())
	{
		_children.erase(it);
	}
}

void Transform::OnInspectorGUI()
{
	bool positionChanged = UnityGUI::Vector3("Position", &m_LocalPosition.x);
	bool rotationChanged = UnityGUI::Vector3("Rotation", &m_LocalEulerAngles.x);
	bool scaleChanged = UnityGUI::Vector3("Scale", &m_LocalScale.x, true);

	if (rotationChanged)
		SetLocalEulerAngles(m_LocalEulerAngles);

	if (positionChanged || rotationChanged || scaleChanged)
		UpdateTransform();
}

GENERATE_COMPONENT_FUNC_TOJSON(Transform)
{
	json j;

	// 월드 값의 기준은 TransformStore — 저장할 사본을 채운다
	Transform* self = const_cast<Transform*>(this);
	self->m_Position = self->GetPosition();
	self->m_Scale = self->GetScale();
	self->m_EulerAngles = self->GetEulerAngle();

	SERIALIZE_TYPE(j, Transform);
	SERIALIZE_QUATERNION(j, m_LocalRotation);
	SERIALIZE_VECTOR3(j, m_LocalEulerAngles);
	SERIALIZE_VECTOR3(j, m_LocalEulerRadians);
	SERIALIZE_VECTOR3(j, m_LocalPosition);
	SERIALIZE_VECTOR3(j, m_LocalScale);

	SERIALIZE_VECTOR3(j, m_Scale);
	SERIALIZE_VECTOR3(j, m_EulerAngles);
	SERIALIZE_VECTOR3(j, m_Position);

	// UI 오브젝트: 위치(x, y)는 RectTransform 이 Game 뷰 크기로 매 프레임 계산하는 값이므로 크기와 무관한 값으로 저장한다
	// (창 크기만 바뀌어도 씬이 "*" 로 바뀌지 않게. 불러오면 첫 레이아웃에서 다시 계산된다)
	if (m_pGameObject)
		if (RectTransform* rt = m_pGameObject->GetComponent<RectTransform>())
		{
			j["m_LocalPosition"] = { 0.0f, 0.0f, m_LocalPosition.z };
			j["m_Position"] = { 0.0f, 0.0f, 0.0f };
			if (rt->IsDrivenByCanvas())   // 루트 캔버스: 크기(배율)도 Canvas Scaler 가 정한다
			{
				j["m_LocalScale"] = { 1.0f, 1.0f, 1.0f };
				j["m_Scale"] = { 1.0f, 1.0f, 1.0f };
			}
			else
				j["m_Scale"] = { m_LocalScale.x, m_LocalScale.y, m_LocalScale.z };
		}

	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(Transform)
{
	DE_SERIALIZE_QUATERNION(j, m_LocalRotation);
	DE_SERIALIZE_VECTOR3(j, m_LocalEulerAngles);
	DE_SERIALIZE_VECTOR3(j, m_LocalEulerRadians);
	DE_SERIALIZE_VECTOR3(j, m_LocalPosition);
	DE_SERIALIZE_VECTOR3_D(j, m_LocalScale, Vec3::One);

	DE_SERIALIZE_VECTOR3(j, m_EulerAngles);
	DE_SERIALIZE_VECTOR3(j, m_Position);
	DE_SERIALIZE_VECTOR3_D(j, m_Scale, Vec3::One);
	// 월드 값 (m_Position · m_Scale · m_EulerAngles) 은 읽기만 하고 버린다 — 아래 UpdateTransform 뒤 로컬 값과 부모로 다시 계산된다
	m_EulerVersion = UINT32_MAX;

	if (!j.contains("m_LocalRotation"))
		m_LocalRotation = EulerToQuaternion(m_LocalEulerAngles);
	m_LocalRotation.Normalize();
	if (fabsf(EulerToQuaternion(m_LocalEulerAngles).Dot(m_LocalRotation)) < 0.99999f)
		m_LocalEulerAngles = ToEulerAnglesNear(m_LocalRotation, m_LocalEulerAngles);   // 이전 버전(다른 오일러 규약)으로 저장된 씬
	m_LocalEulerRadians = Vec3(XMConvertToRadians(m_LocalEulerAngles.x), XMConvertToRadians(m_LocalEulerAngles.y), XMConvertToRadians(m_LocalEulerAngles.z));

	UpdateTransform();
}
