#include "pch.h"
#include "Transform.h"
#include "UnityGUI.h"
#include "EditorGUI.h"
#include "RectTransform.h"

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
}

Transform::~Transform()
{

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
	// ���� ��ȯ ��� ���� 
	Matrix S = Matrix::CreateScale(m_LocalScale);
	// ���� ���Ϸ� ���� ���ʹϾ����� ��ȯ
	// 회전의 기준 값은 쿼터니언 (오일러 → 쿼터니언 왕복 오차/짐벌 락 없음)
	Matrix QR = Matrix::CreateFromQuaternion(m_LocalRotation);

	Matrix T = Matrix::CreateTranslation(m_LocalPosition);

	m_LocalMatrix = S * QR * T;

	if (HasParent())
	{
		m_WorldMatrix = m_LocalMatrix * _parent->GetWorldMatrix();
	}
	else
	{
		m_WorldMatrix = m_LocalMatrix;
	}

	// 월드 위치/회전/크기: 행렬 분해(Decompose)는 비균일 스케일 부모 아래에서 기울어진 행렬이면 실패하고
	// 값을 갱신하지 않으므로, Unity 처럼 계층을 따라 직접 합성한다.
	m_Position = Vec3(m_WorldMatrix._41, m_WorldMatrix._42, m_WorldMatrix._43);
	if (HasParent())
	{
		m_Rotation = m_LocalRotation * _parent->GetRotation();   // 로컬 회전 → 부모 회전 순서
		m_Rotation.Normalize();
		const Vec3 ps = _parent->GetScale();
		m_Scale = Vec3(m_LocalScale.x * ps.x, m_LocalScale.y * ps.y, m_LocalScale.z * ps.z);   // Unity 의 lossyScale 과 같은 근사
	}
	else
	{
		m_Rotation = m_LocalRotation;
		m_Scale = m_LocalScale;
	}
	m_EulerAngles = ToEulerAngles(m_Rotation);
	for (float* v : { &m_EulerAngles.x, &m_EulerAngles.y, &m_EulerAngles.z })
	{
		if (*v < 0.0f) *v += 360.0f;
		if (fabsf(*v) < 1e-4f || fabsf(*v - 360.0f) < 1e-4f) *v = 0.0f;
	}

	// Children
	for (const shared_ptr<Transform>& child : _children)
	{
		child->UpdateTransform();
	}
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
		Vec3 parentScale = _parent->GetScale();
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
		_parent->GetRotation().Inverse(parentInv);
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
		Matrix worldToParentLocalMatrix = _parent->GetWorldMatrix().Invert();
		Vec3 position = Vec3::Transform(worldPosition, worldToParentLocalMatrix);

		SetLocalPosition(position);
	}
	else
	{
		SetLocalPosition(worldPosition);
	}
}

/* ���� ��ǥ���� x, y, z �� �� �ϳ��� ���� ����ȭ�Ͽ� ��ȯ�Ѵ�. */
void Transform::SetWorldPose(const Vec3& position, const Quaternion& rotation, const Vec3& lossyScale)
{
	// 행렬 분해(비균일 스케일 + 회전이면 기울어짐 때문에 실패할 수 있음) 대신 위치/회전/크기를 따로 역변환한다 (Unity 와 같은 방식)
	if (HasParent())
	{
		m_LocalPosition = Vec3::Transform(position, _parent->GetWorldMatrix().Invert());
		Quaternion parentInv;
		_parent->GetRotation().Inverse(parentInv);
		Quaternion local = rotation * parentInv;
		const Vec3 ps = _parent->GetScale();
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

Vec3 Transform::GetAxis(int index) const
{
	// �Է°� �˻�
	if (index < 0 || index > 2)  // 3x3 �Ǵ� 4x4 ����� ��ȿ�� �� �ε����� 0, 1, 2
	{
		std::cout << "RigidBody::getAxis::Out of index" << std::endl;
		return Vector3();
	}

	// ���� ��Ʈ�������� �� ���� ����
	Vector3 axis(
		m_WorldMatrix(index, 0),
		m_WorldMatrix(index, 1),
		m_WorldMatrix(index, 2)
	);

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

	if (!j.contains("m_LocalRotation"))
		m_LocalRotation = EulerToQuaternion(m_LocalEulerAngles);
	m_LocalRotation.Normalize();
	if (fabsf(EulerToQuaternion(m_LocalEulerAngles).Dot(m_LocalRotation)) < 0.99999f)
		m_LocalEulerAngles = ToEulerAnglesNear(m_LocalRotation, m_LocalEulerAngles);   // 이전 버전(다른 오일러 규약)으로 저장된 씬
	m_LocalEulerRadians = Vec3(XMConvertToRadians(m_LocalEulerAngles.x), XMConvertToRadians(m_LocalEulerAngles.y), XMConvertToRadians(m_LocalEulerAngles.z));

	UpdateTransform();
}
