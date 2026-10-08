#include "pch.h"
#include "EditorCamera.h"


EditorCamera::EditorCamera()
{
	SetLens(0.25f * XM_PI, 1.0f, 1.0f, 100.0f);
}

EditorCamera::~EditorCamera()
{
}

void EditorCamera::GetFrustumCulling()
{
	FrustumUpdate();
	
	vector<GameObject*> allObject = SceneManager::GetI()->GetCurrentScene()->GetAllGameObjects();
	vector<GameObject*> cullingObjects;
	vector<shared_ptr<Light>> lights = LightManager::GetI()->GetLights();
	vector<shared_ptr<Light>> cullingLights;

	bool check;

	for (const auto& gameObject : allObject)
	{
		auto meshRenderer = gameObject->GetComponent<MeshRenderer>();

		// meshRenderer ƒƒ∆˜≥Õ∆Æ∞° æ¯¥¬ ∞¥√º¿Ã∞≈≥™ mesh∏¶ ¿˚øÎ æ» Ω√ƒ◊¿ª ∞ÊøÏ
		if (!meshRenderer)
			continue;

		if (!(meshRenderer->GetMesh()))
			continue;

		Vec3 meshPos = meshRenderer->GetGameObject()->GetTransform()->GetPosition();
		BouncingBall ball = meshRenderer->GetMesh()->Ball;
		check = true;
		for (int i = 0; i < 6; i++)
		{
			// √Êµπ√º≈©
			//plane.normal.x * center.x + plane.normal.y * center.y + plane.normal.z * center.z + plane.d;
			float distance = m_Frustum.planes[i].normal.x * (ball.center.x + meshPos.x) +
				m_Frustum.planes[i].normal.y * (ball.center.y + meshPos.y) +
				m_Frustum.planes[i].normal.z * (ball.center.z + meshPos.z) +
				m_Frustum.planes[i].d;
			if (distance < -ball.radius)
				check = false; // ±∏∞° ∆Ú∏È¿« π€ø° ¿÷¿Ω
		}

		if (check)
			cullingObjects.push_back(gameObject);
	}

	// Point and Spot Light
	for (const auto& light : lights)
	{
		if (!LightManager::IsLit(light))
			continue;   // Í∫ºÏßÑ Îπõ ¬∑ Í∫ºÏßÑ Í≥ÑÏ∏µ (Unity)
		if (LightType::Directional == light->GetLightType())
		{
			cullingLights.push_back(light);
			continue;
		}

		Vec3 lightPos = light->GetGameObject()->GetTransform()->GetPosition();

		check = true;
		for (int i = 0; i < 6; i++)
		{
			// √Êµπ√º≈©
			//plane.normal.x * center.x + plane.normal.y * center.y + plane.normal.z * center.z + plane.d;
			float distance = m_Frustum.planes[i].normal.x * lightPos.x +
				m_Frustum.planes[i].normal.y * lightPos.y +
				m_Frustum.planes[i].normal.z * lightPos.z +
				m_Frustum.planes[i].d;
			if (distance < -light->GetRange())
				check = false; // ±∏∞° ∆Ú∏È¿« π€ø° ¿÷¿Ω
		}

		if (check)
			cullingLights.push_back(light);
	}

	SceneManager::GetI()->GetCurrentScene()->SetCullingEditorGameObjects(cullingObjects);
	LightManager::GetI()->SortingEditorLights(cullingLights, _position);
}

void EditorCamera::FrustumUpdate()
{
	// ≈¨∏≥ «‡∑ƒ¿ª ∞ËªÍ
	// ÌÅ¥Î¶Ω ÌñâÎ†¨ = Î∑∞ x Ìà¨ÏòÅ (Ìñâ Î≤°ÌÑ∞). ÌèâÎ©¥ÏùÄ Ïó¥ÏóêÏÑú (Gribb-Hartmann, D3D ÍπäÏù¥ 0..1):
	//  Ïôº c4+c1, Ïò§Î•∏ c4-c1, ÏïÑÎûò c4+c2, ÏúÑ c4-c2, Í∞ÄÍπåÏö¥ c3, Î®º c4-c3 ‚Äî ÏòàÏ†ÑÏóêÎäî Ìà¨ÏòÅ x Î∑∞ Ïùò ÌñâÏóêÏÑú ÎΩëÏïÑ ÏóâÎö±Ìïú Ï†àÎëêÏ≤¥ÏòÄÎã§ (ÎπõÏùÑ ÎåÄÎ∂ÄÎ∂Ñ Í±∏Îü¨ ÎÉÑ)
	::XMMATRIX clipMatrix = DirectX::XMMatrixMultiply(::XMLoadFloat4x4(&_view), ::XMLoadFloat4x4(&_proj));
	::XMFLOAT4X4 clip;
	::XMStoreFloat4x4(&clip, clipMatrix);
	m_Frustum.planes[0] = { {clip._14 + clip._11, clip._24 + clip._21, clip._34 + clip._31}, clip._44 + clip._41 }; // left
	m_Frustum.planes[1] = { {clip._14 - clip._11, clip._24 - clip._21, clip._34 - clip._31}, clip._44 - clip._41 }; // right
	m_Frustum.planes[2] = { {clip._14 + clip._12, clip._24 + clip._22, clip._34 + clip._32}, clip._44 + clip._42 }; // bottom
	m_Frustum.planes[3] = { {clip._14 - clip._12, clip._24 - clip._22, clip._34 - clip._32}, clip._44 - clip._42 }; // top
	m_Frustum.planes[4] = { {clip._13, clip._23, clip._33}, clip._43 }; // near
	m_Frustum.planes[5] = { {clip._14 - clip._13, clip._24 - clip._23, clip._34 - clip._33}, clip._44 - clip._43 }; // far

	// ∞¢ ∆Ú∏È¿« ¡§±‘»≠
	for (int i = 0; i < 6; ++i) 
	{
		float length = sqrtf(m_Frustum.planes[i].normal.x * m_Frustum.planes[i].normal.x +
			m_Frustum.planes[i].normal.y * m_Frustum.planes[i].normal.y +
			m_Frustum.planes[i].normal.z * m_Frustum.planes[i].normal.z);

		if (length > 0.0f)
		{
			m_Frustum.planes[i].normal.x /= length;
			m_Frustum.planes[i].normal.y /= length;
			m_Frustum.planes[i].normal.z /= length;
			m_Frustum.planes[i].d /= length;
		}
	}
}


XMVECTOR EditorCamera::GetPositionXM()const
{
	return ::XMLoadFloat3(&_position);
}

XMFLOAT3 EditorCamera::GetPosition()const
{
	return _position;
}

void EditorCamera::SetPosition(float x, float y, float z)
{
	_position = XMFLOAT3(x, y, z);
}

void EditorCamera::SetPosition(const XMFLOAT3& v)
{
	_position = v;
}

XMVECTOR EditorCamera::GetRightXM()const
{
	return ::XMLoadFloat3(&_right);
}

XMFLOAT3 EditorCamera::GetRight()const
{
	return _right;
}

XMVECTOR EditorCamera::GetUpXM()const
{
	return ::XMLoadFloat3(&_up);
}

XMFLOAT3 EditorCamera::GetUp()const
{
	return _up;
}

XMVECTOR EditorCamera::GetLookXM()const
{
	return ::XMLoadFloat3(&_look);
}

XMFLOAT3 EditorCamera::GetLook()const
{
	return _look;
}

float EditorCamera::GetNearZ()const
{
	return _nearZ;
}

float EditorCamera::GetFarZ()const
{
	return _farZ;
}

float EditorCamera::GetAspect()const
{
	return _aspect;
}

float EditorCamera::GetFovY()const
{
	return _fovY;
}

float EditorCamera::GetFovX()const
{
	float halfWidth = 0.5f * GetNearWindowWidth();
	return 2.0f * atan(halfWidth / _nearZ);
}

float EditorCamera::GetNearWindowWidth()const
{
	return _aspect * _nearWindowHeight;
}

float EditorCamera::GetNearWindowHeight()const
{
	return _nearWindowHeight;
}

float EditorCamera::GetFarWindowWidth()const
{
	return _aspect * _farWindowHeight;
}

float EditorCamera::GetFarWindowHeight()const
{
	return _farWindowHeight;
}

void EditorCamera::SetLens(float fovY, float aspect, float zn, float zf)
{
	// cache properties
	if (!(aspect > 1e-4f) || !std::isfinite(aspect))
		aspect = 1.0f;   // ÌÅ¨Í∏∞ 0 Ïù∏ Î∑∞ÏóêÏÑú Ïò® Í∞í (Ìà¨ÏòÅ ÌñâÎ†¨ assert Î∞©ÏßÄ)
	_fovY = fovY;
	_aspect = aspect;
	_nearZ = zn;
	_farZ = zf;

	_nearWindowHeight = 2.0f * _nearZ * tanf(0.5f * _fovY);
	_farWindowHeight = 2.0f * _farZ * tanf(0.5f * _fovY);

	XMMATRIX P = ::XMMatrixPerspectiveFovLH(_fovY, _aspect, _nearZ, _farZ);
	::XMStoreFloat4x4(&_proj, P);
}

void EditorCamera::LookAt(FXMVECTOR pos, FXMVECTOR target, FXMVECTOR worldUp)
{
	XMVECTOR L = ::XMVector3Normalize(::XMVectorSubtract(target, pos));
	XMVECTOR R = ::XMVector3Normalize(::XMVector3Cross(worldUp, L));
	XMVECTOR U = ::XMVector3Cross(L, R);

	::XMStoreFloat3(&_position, pos);
	::XMStoreFloat3(&_look, L);
	::XMStoreFloat3(&_right, R);
	::XMStoreFloat3(&_up, U);
}

void EditorCamera::LookAt(const XMFLOAT3& pos, const XMFLOAT3& target, const XMFLOAT3& up)
{
	XMVECTOR P = ::XMLoadFloat3(&pos);
	XMVECTOR T = ::XMLoadFloat3(&target);
	XMVECTOR U = ::XMLoadFloat3(&up);

	LookAt(P, T, U);
}

XMMATRIX EditorCamera::View()const
{
	return ::XMLoadFloat4x4(&_view);
}

XMMATRIX EditorCamera::Proj()const
{
	return ::XMLoadFloat4x4(&_proj);
}

XMMATRIX EditorCamera::ViewProj()const
{
	return ::XMMatrixMultiply(View(), Proj());
}

void EditorCamera::Pedestal(float d)
{
	XMVECTOR s = ::XMVectorReplicate(d);
	XMVECTOR u = ::XMLoadFloat3(&_up);
	XMVECTOR p = ::XMLoadFloat3(&_position);
	::XMStoreFloat3(&_position, XMVectorMultiplyAdd(s, u, p));
}

void EditorCamera::Strafe(float d)
{
	// mPosition += d*mRight
	XMVECTOR s = ::XMVectorReplicate(d);
	XMVECTOR r = ::XMLoadFloat3(&_right);
	XMVECTOR p = ::XMLoadFloat3(&_position);
	::XMStoreFloat3(&_position, XMVectorMultiplyAdd(s, r, p));
}

void EditorCamera::Walk(float d)
{
	// mPosition += d*mLook
	XMVECTOR s = ::XMVectorReplicate(d);
	XMVECTOR l = ::XMLoadFloat3(&_look);
	XMVECTOR p = ::XMLoadFloat3(&_position);
	::XMStoreFloat3(&_position, ::XMVectorMultiplyAdd(s, l, p));
}

void EditorCamera::Pitch(float angle)
{
	// Rotate up and look vector about the right vector.

	XMMATRIX R = ::XMMatrixRotationAxis(::XMLoadFloat3(&_right), angle);

	::XMStoreFloat3(&_up, ::XMVector3TransformNormal(XMLoadFloat3(&_up), R));
	::XMStoreFloat3(&_look, ::XMVector3TransformNormal(XMLoadFloat3(&_look), R));
}

void EditorCamera::RotateY(float angle)
{
	// Rotate the basis vectors about the world y-axis.

	XMMATRIX R = XMMatrixRotationY(angle);

	::XMStoreFloat3(&_right, ::XMVector3TransformNormal(::XMLoadFloat3(&_right), R));
	::XMStoreFloat3(&_up, ::XMVector3TransformNormal(::XMLoadFloat3(&_up), R));
	::XMStoreFloat3(&_look, ::XMVector3TransformNormal(::XMLoadFloat3(&_look), R));
}

void EditorCamera::UpdateViewMatrix()
{
	XMVECTOR R = ::XMLoadFloat3(&_right);
	XMVECTOR U = ::XMLoadFloat3(&_up);
	XMVECTOR L = ::XMLoadFloat3(&_look);
	XMVECTOR P = ::XMLoadFloat3(&_position);

	// Keep camera's axes orthogonal to each other and of unit length.
	L = ::XMVector3Normalize(L);
	U = ::XMVector3Normalize(::XMVector3Cross(L, R));

	// U, L already ortho-normal, so no need to normalize cross product.
	R = ::XMVector3Cross(U, L);

	// Fill in the view matrix entries.
	float x = -::XMVectorGetX(::XMVector3Dot(P, R));
	float y = -::XMVectorGetX(::XMVector3Dot(P, U));
	float z = -::XMVectorGetX(::XMVector3Dot(P, L));

	::XMStoreFloat3(&_right, R);
	::XMStoreFloat3(&_up, U);
	::XMStoreFloat3(&_look, L);

	_view(0, 0) = _right.x;
	_view(1, 0) = _right.y;
	_view(2, 0) = _right.z;
	_view(3, 0) = x;

	_view(0, 1) = _up.x;
	_view(1, 1) = _up.y;
	_view(2, 1) = _up.z;
	_view(3, 1) = y;

	_view(0, 2) = _look.x;
	_view(1, 2) = _look.y;
	_view(2, 2) = _look.z;
	_view(3, 2) = z;

	_view(0, 3) = 0.0f;
	_view(1, 3) = 0.0f;
	_view(2, 3) = 0.0f;
	_view(3, 3) = 1.0f;
	
	GetFrustumCulling();
}

XMMATRIX EditorCamera::GetWorldMatrix()
{
	// Create the world matrix using the camera's position and orientation vectors
	XMFLOAT4X4 world;
	world._11 = _right.x; world._12 = _right.y; world._13 = _right.z; world._14 = 0.0f;
	world._21 = _up.x; world._22 = _up.y; world._23 = _up.z; world._24 = 0.0f;
	world._31 = _look.x; world._32 = _look.y; world._33 = _look.z; world._34 = 0.0f;
	world._41 = _position.x; world._42 = _position.y; world._43 = _position.z; world._44 = 1.0f;

	return XMLoadFloat4x4(&world); 
}

