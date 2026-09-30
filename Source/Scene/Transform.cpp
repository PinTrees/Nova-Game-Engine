#include "pch.h"
#include "Transform.h"
#include "UnityGUI.h"
#include "EditorGUI.h"

// This is not in game format, it is in mathematical format.
Quaternion Transform::CreateQuaternion(double roll, double pitch, double yaw) // roll (x), pitch (y), yaw (z), angles are in radians
{
	// Abbreviations for the various angular functions

	double cr = cos(roll * 0.5);
	double sr = sin(roll * 0.5);
	double cp = cos(pitch * 0.5);
	double sp = sin(pitch * 0.5);
	double cy = cos(yaw * 0.5);
	double sy = sin(yaw * 0.5);

	Quaternion q;
	q.w = cr * cp * cy + sr * sp * sy;
	q.x = sr * cp * cy - cr * sp * sy;
	q.y = cr * sp * cy + sr * cp * sy;
	q.z = cr * cp * sy - sr * sp * cy;

	return q;
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
	Vec3 radians;

	//// roll (x-axis rotation)
	//double sinr_cosp = 2 * (q.w * q.x + q.y * q.z);
	//double cosr_cosp = 1 - 2 * (q.x * q.x + q.y * q.y);
	//radians.x = std::atan2(sinr_cosp, cosr_cosp);

	//// pitch (y-axis rotation)
	//double sinp = 2 * (q.w * q.y + q.x * q.z);
	////double cosp = 1 - 2 * (q.y * q.y + q.z * q.z);
	//radians.y = std::asin(sinp);

	//// yaw (z-axis rotation)
	//double siny_cosp = 2 * (q.w * q.z + q.x * q.y);
	//double cosy_cosp = 1 - 2 * (q.y * q.y + q.z * q.z);
	//radians.z = std::atan2(siny_cosp, cosy_cosp);

	double sinr_cosp = 2.0 * (q.w * q.x + q.y * q.z);
	double cosr_cosp = 1.0 - 2.0 * (q.x * q.x + q.y * q.y);
	radians.x = std::atan2(sinr_cosp, cosr_cosp);

	double sinp = 2.0 * (q.w * q.y - q.z * q.x);
	radians.y = std::asin(std::clamp(sinp, -1.0, 1.0));  // -1 <= sin(pitch) <= 1À» º¸Àå

	double siny_cosp = 2.0 * (q.w * q.z + q.x * q.y);
	double cosy_cosp = 1.0 - 2.0 * (q.y * q.y + q.z * q.z);
	radians.z = std::atan2(siny_cosp, cosy_cosp);

	return radians;
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
	Quaternion q = CreateQuaternion(m_LocalEulerRadians.x, m_LocalEulerRadians.y, m_LocalEulerRadians.z);
	Matrix QR = Matrix::CreateFromQuaternion(q);

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
	m_LocalRotation = q;
	m_LocalEulerAngles = ToEulerAngles(q);
	m_LocalEulerRadians = ToEulerRadians(q);
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
	if (HasParent())
	{
		Matrix inverseMatrix = _parent->GetWorldMatrix().Invert();
		Vec3 rotation = Vec3::TransformNormal(worldRotation, inverseMatrix);

		SetLocalEulerAngles(rotation);
	}
	else
	{
		SetLocalEulerAngles(worldRotation);
	}
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

	UpdateTransform();
}
