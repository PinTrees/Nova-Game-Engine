#include "pch.h"
#include "Transform.h"
#include "UnityGUI.h"
#include "EditorGUI.h"

// Unity ì™€ ê°™ì€ ì˜¤ì¼ëŸ¬ ìˆœì„œ(Z â†’ X â†’ Y). ë¼ë””ì•ˆ ìž…ë ¥
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
	// í–‰ë ¬ M = Rz * Rx * Ry (í–‰ ë²¡í„°) ì—ì„œ: M32 = -sin(x), M31 = cos(x)sin(y), M33 = cos(x)cos(y), M12 = sin(z)cos(x), M22 = cos(z)cos(x)
	q.Normalize();
	Matrix m = Matrix::CreateFromQuaternion(q);
	Vec3 r;
	// asin ëŒ€ì‹  atan2 ë¡œ x ë¥¼ êµ¬í•´ Â±90Â° ë¶€ê·¼ì—ì„œë„ ì •ë°€ë„ë¥¼ ìœ ì§€í•œë‹¤
	const float cx = sqrtf(m._31 * m._31 + m._33 * m._33);
	r.x = atan2f(-m._32, cx);
	if (cx > 1e-6f)
	{
		r.y = atan2f(m._31, m._33);
		r.z = atan2f(m._12, m._22);
	}
	else
	{
		// ì§ë²Œ ë½ (x = Â±90Â°): z ë¥¼ 0 ìœ¼ë¡œ ë‘ê³  y ì— í•©ì¹œë‹¤
		r.y = atan2f(-m._13, m._11);
		r.z = 0.0f;
	}
	return r;
}

Vec3 Transform::ToEulerAnglesNear(Quaternion q, const Vec3& hint)
{
	const Vec3 a = ToEulerAngles(q);
	// ê°™ì€ íšŒì „ì˜ ë‘ ë²ˆì§¸ í‘œí˜„: (180 - x, y + 180, z + 180)
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
	// ë¶€ë™ì†Œìˆ˜ì  ìž¡ìŒ ì •ë¦¬ (-0.0 â†’ 0)
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
	// ·ÎÄÃ º¯È¯ Çà·Ä »ý¼º 
	Matrix S = Matrix::CreateScale(m_LocalScale);
	// ·ÎÄÃ ¿ÀÀÏ·¯ °¢À» ÄõÅÍ´Ï¾ðÀ¸·Î º¯È¯
	// íšŒì „ì˜ ê¸°ì¤€ ê°’ì€ ì¿¼í„°ë‹ˆì–¸ (ì˜¤ì¼ëŸ¬ â†’ ì¿¼í„°ë‹ˆì–¸ ì™•ë³µ ì˜¤ì°¨/ì§ë²Œ ë½ ì—†ìŒ)
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

	m_WorldMatrix.Decompose(m_Scale, m_Rotation, m_Position);
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
	// í‘œì‹œìš© ì˜¤ì¼ëŸ¬ ê°ì€ ì´ì „ ê°’ê³¼ ê°€ìž¥ ê°€ê¹Œìš´ í‘œí˜„ì„ ì“´ë‹¤ (Unity Inspector ì™€ ê°™ì€ ë™ìž‘)
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
		// world = local * parent  â†’  local = world * inverse(parent)
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

/* ¿ùµå ÁÂÇ¥°èÀÇ x, y, z Ãà Áß ÇÏ³ª¸¦ ´ÜÀ§ º¤ÅÍÈ­ÇÏ¿© ¹ÝÈ¯ÇÑ´Ù. */
Vec3 Transform::GetAxis(int index) const
{
	// ÀÔ·Â°ª °Ë»ç
	if (index < 0 || index > 2)  // 3x3 ¶Ç´Â 4x4 Çà·ÄÀÇ À¯È¿ÇÑ Ãà ÀÎµ¦½º´Â 0, 1, 2
	{
		std::cout << "RigidBody::getAxis::Out of index" << std::endl;
		return Vector3();
	}

	// ¿ùµå ¸ÅÆ®¸¯½º¿¡¼­ Ãà º¤ÅÍ ÃßÃâ
	Vector3 axis(
		m_WorldMatrix(index, 0),
		m_WorldMatrix(index, 1),
		m_WorldMatrix(index, 2)
	);

	axis.Normalize();  // º¤ÅÍ¸¦ Á¤±ÔÈ­

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
		m_LocalEulerAngles = ToEulerAnglesNear(m_LocalRotation, m_LocalEulerAngles);   // ì´ì „ ë²„ì „(ë‹¤ë¥¸ ì˜¤ì¼ëŸ¬ ê·œì•½)ìœ¼ë¡œ ì €ìž¥ëœ ì”¬
	m_LocalEulerRadians = Vec3(XMConvertToRadians(m_LocalEulerAngles.x), XMConvertToRadians(m_LocalEulerAngles.y), XMConvertToRadians(m_LocalEulerAngles.z));

	UpdateTransform();
}
