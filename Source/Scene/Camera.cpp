#include "pch.h"
#include "Camera.h"
#include "UnityGUI.h"
#include "SceneViewOverlay.h"
#include "Application.h"
#include "App.h"
#include "GameObject.h"
#include "Mesh.h"
#include "MeshRenderer.h"
#include "SceneManager.h"
#include "Scene.h"
#include "Light.h"
#include "LightManager.h"
#include "EditorGUI.h"

Camera::Camera()
{
	m_InspectorTitleName = "Camera";
	m_InspectorIconPath = L"camera.png";
	SetLens(XM_PI / 3.0f, 1.0f, 0.3f, 1000.0f);   // Unity ê¸°ë³¸ê°’: FOV 60, Near 0.3, Far 1000
}

Camera::~Camera()
{
}

XMVECTOR Camera::GetPositionXM()
{
	auto position = GetGameObject()->GetTransform()->GetPosition(); 
	return ::XMLoadFloat3(&position);
}

XMFLOAT3 Camera::GetPosition()const
{
	Transform* transform = m_pGameObject->GetComponent<Transform>();
	return transform->GetPosition(); 
}

XMVECTOR Camera::GetRightXM()const 
{
	return ::XMLoadFloat3(&_right);
} 

XMFLOAT3 Camera::GetRight()const
{
	return _right;
}

XMVECTOR Camera::GetUpXM()const
{
	return ::XMLoadFloat3(&_up);
}

XMFLOAT3 Camera::GetUp()const
{
	return _up;
}

XMVECTOR Camera::GetLookXM()const
{
	return ::XMLoadFloat3(&_look);
}

XMFLOAT3 Camera::GetLook()const
{
	return _look;
}

float Camera::GetNearZ()const
{
	return m_nearZ;
}

float Camera::GetFarZ()const
{
	return m_farZ;
}

float Camera::GetAspect()const
{
	return m_aspect;
}

float Camera::GetFovY()const
{
	return m_fovY;
}

float Camera::GetFovX()const
{
	float halfWidth = 0.5f * GetNearWindowWidth();
	return 2.0f * atan(halfWidth / m_nearZ);
}

float Camera::GetNearWindowWidth()const
{
	return m_aspect * m_nearWindowHeight;
}

float Camera::GetNearWindowHeight()const
{
	return m_nearWindowHeight;
}

float Camera::GetFarWindowWidth()const
{
	return m_aspect * m_farWindowHeight;
}

float Camera::GetFarWindowHeight()const
{
	return m_farWindowHeight;
}

void Camera::SetLens(float fovY, float aspect, float zn, float zf)
{
	// cache properties
	if (!(aspect > 1e-4f) || !std::isfinite(aspect))
	{
		EditorLog::Write("Camera", "SetLens: invalid aspect %f replaced with 1", aspect);
		aspect = 1.0f;   // í¬ê¸° 0 ì¸ ë·°ì—ì„œ ì˜¨ ê°’ (íˆ¬ì˜ í–‰ë ¬ assert ë°©ì§€)
	}
	m_fovY = fovY;
	m_aspect = aspect;
	m_nearZ = zn;
	m_farZ = zf;

	ProjUpdate();
}


void Camera::LookAt(FXMVECTOR pos, FXMVECTOR target, FXMVECTOR worldUp)
{
	XMVECTOR L = ::XMVector3Normalize(::XMVectorSubtract(target, pos));
	XMVECTOR R = ::XMVector3Normalize(::XMVector3Cross(worldUp, L));
	XMVECTOR U = ::XMVector3Cross(L, R);

	auto position = GetGameObject()->GetTransform()->GetPosition();

	::XMStoreFloat3(&position, pos); 
	::XMStoreFloat3(&_look, L);
	::XMStoreFloat3(&_right, R);
	::XMStoreFloat3(&_up, U);
}

void Camera::LookAt(const XMFLOAT3& pos, const XMFLOAT3& target, const XMFLOAT3& up)
{
	XMVECTOR P = ::XMLoadFloat3(&pos);
	XMVECTOR T = ::XMLoadFloat3(&target);
	XMVECTOR U = ::XMLoadFloat3(&up);

	LookAt(P, T, U);
}

XMMATRIX Camera::View()const
{
	return ::XMLoadFloat4x4(&_view);
}

XMMATRIX Camera::Proj()const
{
	return ::XMLoadFloat4x4(&_proj);
}

XMMATRIX Camera::ViewProj()const
{
	return ::XMMatrixMultiply(View(), Proj());
}

void Camera::Strafe(float d)
{
	auto position = GetGameObject()->GetTransform()->GetPosition();

	XMVECTOR s = ::XMVectorReplicate(d);
	XMVECTOR r = ::XMLoadFloat3(&_right);
	XMVECTOR p = ::XMLoadFloat3(&position);
	::XMStoreFloat3(&position, XMVectorMultiplyAdd(s, r, p));
}

void Camera::Walk(float d)
{
	auto position = GetGameObject()->GetTransform()->GetPosition();

	XMVECTOR s = ::XMVectorReplicate(d);
	XMVECTOR l = ::XMLoadFloat3(&_look);
	XMVECTOR p = ::XMLoadFloat3(&position);
	::XMStoreFloat3(&position, XMVectorMultiplyAdd(s, l, p)); 
}

void Camera::Pitch(float angle)
{
	// Rotate up and look vector about the right vector.

	XMMATRIX R = ::XMMatrixRotationAxis(::XMLoadFloat3(&_right), angle);

	::XMStoreFloat3(&_up, ::XMVector3TransformNormal(XMLoadFloat3(&_up), R));
	::XMStoreFloat3(&_look, ::XMVector3TransformNormal(XMLoadFloat3(&_look), R));
}

void Camera::RotateY(float angle)
{
	// Rotate the basis vectors about the world y-axis.

	XMMATRIX R  = XMMatrixRotationY(angle);

	::XMStoreFloat3(&_right, ::XMVector3TransformNormal(::XMLoadFloat3(&_right), R));
	::XMStoreFloat3(&_up, ::XMVector3TransformNormal(::XMLoadFloat3(&_up), R));
	::XMStoreFloat3(&_look, ::XMVector3TransformNormal(::XMLoadFloat3(&_look), R));
}

void Camera::UpdateViewMatrix()
{
	auto position = GetGameObject()->GetTransform()->GetPosition();

	XMVECTOR R = ::XMLoadFloat3(&_right); 
	XMVECTOR U = ::XMLoadFloat3(&_up);
	XMVECTOR L = ::XMLoadFloat3(&_look);
	XMVECTOR P = ::XMLoadFloat3(&position); 

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
}

void Camera::Update()
{
}

void Camera::LateUpdate()
{
	
}

void Camera::ViewUpdate()
{
	XMVECTOR pos = m_pGameObject->GetTransform()->GetPosition();
	XMVECTOR dir = m_pGameObject->GetTransform()->GetLook();
	XMVECTOR target = pos + dir;
	XMVECTOR up = m_pGameObject->GetTransform()->GetUp();

	XMMATRIX V = ::XMMatrixLookAtLH(pos, target, up);
	::XMStoreFloat4x4(&_view, V);

	ProjUpdate();
	// OnPreCull ±ÇÀå(Unity »ı¸íÁÖ±â)
	GetFrustumCulling();
}

void Camera::GetFrustumCulling()
{
	FrustumUpdate();

	vector<GameObject*> allObject = SceneManager::GetI()->GetCurrentScene()->GetAllGameObjects();
	vector<GameObject*> cullingObjects;
	vector<shared_ptr<Light>> lights = LightManager::GetI()->GetLights();
	vector<shared_ptr<Light>> cullingLights;

	bool check;

	// ¸Ş½¬·»´õ·¯ ÄÃ¸µÀÌ Á¦´ë·Î ¾ÈµÇ´Â ¹®Á¦!
	for (const auto& gameObject : allObject)
	{
		auto meshRenderer = gameObject->GetComponent<MeshRenderer>();

		// meshRenderer ÄÄÆ÷³ÍÆ®°¡ ¾ø´Â °´Ã¼ÀÌ°Å³ª mesh¸¦ Àû¿ë ¾È ½ÃÄ×À» °æ¿ì, editor and game Camera¿¡ SkinnedMeshµµ Æ÷ÇÔ½ÃÄÑ¾ßÇÔ
		if (!meshRenderer || !meshRenderer->GetMesh())
			continue;

		Vec3 meshPos = meshRenderer->GetGameObject()->GetTransform()->GetPosition();
		BouncingBall ball = meshRenderer->GetMesh()->Ball;

		Vec3 ballCenter = ball.center + meshPos;

		check = true;
		for (int i = 0; i < 6; i++)
		{
			// Ãæµ¹Ã¼Å©
			//plane.normal.x * center.x + plane.normal.y * center.y + plane.normal.z * center.z + plane.d;
			float distance = m_Frustum.planes[i].normal.x * ballCenter.x +
				m_Frustum.planes[i].normal.y * ballCenter.y +
				m_Frustum.planes[i].normal.z * ballCenter.z +
				m_Frustum.planes[i].d;

			if (distance < -ball.radius)
			{
				check = false; // ±¸°¡ Æò¸éÀÇ ¹Û¿¡ ÀÖÀ½
				break;
			}
		}

		if (check)
			cullingObjects.push_back(gameObject);
	}

	// Point and Spot Light
	for (const auto& light : lights)
	{
		if (LightType::Directional == light->GetLightType())
		{
			cullingLights.push_back(light);
			continue;
		}

		Vec3 lightPos = light->GetGameObject()->GetTransform()->GetPosition();

		check = true;
		for (int i = 0; i < 6; i++)
		{
			// Ãæµ¹Ã¼Å©
			//plane.normal.x * center.x + plane.normal.y * center.y + plane.normal.z * center.z + plane.d;
			float distance = m_Frustum.planes[i].normal.x * lightPos.x +
				m_Frustum.planes[i].normal.y * lightPos.y +
				m_Frustum.planes[i].normal.z * lightPos.z +
				m_Frustum.planes[i].d;

			if (distance < -light->GetRange())
			{
				check = false; // ±¸°¡ Æò¸éÀÇ ¹Û¿¡ ÀÖÀ½
				break;
			}
		}

		if (check)
			cullingLights.push_back(light);
	}

	SceneManager::GetI()->GetCurrentScene()->SetCullingGameObjects(cullingObjects);
	LightManager::GetI()->SortingLights(cullingLights, m_pGameObject->GetTransform()->GetPosition());
}

void Camera::FrustumUpdate()
{
	// Å¬¸³ Çà·ÄÀ» °è»ê
	::XMMATRIX clipMatrix = DirectX::XMMatrixMultiply(::XMLoadFloat4x4(&_proj), ::XMLoadFloat4x4(&_view));
	::XMFLOAT4X4 clip;
	::XMStoreFloat4x4(&clip, clipMatrix);

	// Æò¸é °è»ê (°¢ ¸éÀÇ °è¼ö °è»ê)
	m_Frustum.planes[0] = { {clip._41 + clip._11, clip._42 + clip._12, clip._43 + clip._13}, clip._44 + clip._14 }; // left
	m_Frustum.planes[1] = { {clip._41 - clip._11, clip._42 - clip._12, clip._43 - clip._13}, clip._44 - clip._14 }; // right
	m_Frustum.planes[2] = { {clip._41 + clip._21, clip._42 + clip._22, clip._43 + clip._23}, clip._44 + clip._24 }; // bottom
	m_Frustum.planes[3] = { {clip._41 - clip._21, clip._42 - clip._22, clip._43 - clip._23}, clip._44 - clip._24 }; // top
	m_Frustum.planes[4] = { {clip._41 + clip._31, clip._42 + clip._32, clip._43 + clip._33}, clip._44 + clip._34 }; // near
	m_Frustum.planes[5] = { {clip._41 - clip._31, clip._42 - clip._32, clip._43 - clip._33}, clip._44 - clip._34 }; // far

	// °¢ Æò¸éÀÇ Á¤±ÔÈ­
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

// private funtion
void Camera::ProjUpdate()
{
	m_nearWindowHeight = 2.0f * m_nearZ * tanf(0.5f * m_fovY);
	m_farWindowHeight = 2.0f * m_farZ * tanf(0.5f * m_fovY);

	XMMATRIX P;
	if (ProjectionType::Perspective == m_cameraType)
	{
		P = ::XMMatrixPerspectiveFovLH(m_fovY, m_aspect, m_nearZ, m_farZ);
	}
	else // Orthographic
	{
		// Unity ì™€ ê°™ì´ m_orthoSize ëŠ” í™”ë©´ ë†’ì´ì˜ ì ˆë°˜(ì›”ë“œ ë‹¨ìœ„)
		P = ::XMMatrixOrthographicLH(2.0f * m_orthoSize * m_aspect, 2.0f * m_orthoSize, m_nearZ, m_farZ);
	}
	
	::XMStoreFloat4x4(&_proj, P);
}

string Camera::GetStringCameraType(ProjectionType type)
{
	switch (type)
	{
	case ProjectionType::Orthographic: return "Orthographic";
	case ProjectionType::Perspective: return "Perspective";
	default: return "Unknown";
	}
}

void Camera::OnInspectorGUI()
{
	static const char* kRenderType[] = { "Base", "Overlay" };
	static const char* kProjection[] = { "Perspective", "Orthographic" };
	static const char* kFovAxis[] = { "Vertical", "Horizontal" };
	static const char* kRenderer[] = { "Default Renderer (Forward)" };
	static const char* kAA[] = { "No Anti-aliasing", "Fast Approximate Anti-aliasing (FXAA)", "Subpixel Morphological Anti-aliasing (SMAA)" };
	static const char* kTexture[] = { "Off", "On", "Use settings from Render Pipeline Asset" };
	static const char* kCulling[] = { "Everything", "Nothing", "Default", "TransparentFX", "Ignore Raycast", "Water", "UI" };
	static const char* kBackground[] = { "Skybox", "Solid Color", "Uninitialized" };
	static const char* kVolumeUpdate[] = { "Use Pipeline Settings", "Every Frame", "Via Scripting" };
	static const char* kVolumeMask[] = { "Default", "Nothing", "Everything" };
	static const char* kDisplay[] = { "Display 1", "Display 2", "Display 3", "Display 4", "Display 5", "Display 6", "Display 7", "Display 8" };
	static const char* kEye[] = { "Both", "Left", "Right", "None (Main Display)" };

	bool projectionChanged = false;

	UnityGUI::Dropdown("Render Type", &m_renderType, kRenderType, 2);

	// ---- Projection ----
	if (UnityGUI::Foldout("Projection"))
	{
		int projection = (int)m_cameraType;
		if (UnityGUI::Dropdown("Projection", &projection, kProjection, 2, 1))
		{
			m_cameraType = (ProjectionType)projection;
			projectionChanged = true;
		}

		if (m_cameraType == ProjectionType::Perspective)
		{
			int axis = m_fovVerticalAxis ? 0 : 1;
			if (UnityGUI::Dropdown("Field of View Axis", &axis, kFovAxis, 2, 1))
				m_fovVerticalAxis = (axis == 0);

			float fov = XMConvertToDegrees(m_fovY);
			if (UnityGUI::Slider("Field of View", &fov, 1.0f, 179.0f, 1))
			{
				fov = std::clamp(fov, 1.0f, 179.0f);
				m_fovY = XMConvertToRadians(fov);
				projectionChanged = true;
			}
			UnityGUI::Toggle("Physical Camera", &m_physicalCamera, 1);
		}
		else
		{
			if (UnityGUI::Float("Size", &m_orthoSize, 1))
			{
				m_orthoSize = max(m_orthoSize, 0.01f);
				projectionChanged = true;
			}
		}

		if (UnityGUI::Float("Clipping Planes", &m_nearZ, 1, "Near"))
		{
			m_nearZ = max(m_nearZ, 0.001f);
			projectionChanged = true;
		}
		if (UnityGUI::Float("", &m_farZ, 1, "Far"))
		{
			m_farZ = max(m_farZ, m_nearZ + 0.01f);
			projectionChanged = true;
		}
	}

	// ---- Rendering ----
	if (UnityGUI::Foldout("Rendering"))
	{
		UnityGUI::Dropdown("Renderer", &m_renderer, kRenderer, 1, 1);
		UnityGUI::Toggle("Post Processing", &m_postProcessing, 1);
		UnityGUI::Dropdown("Anti-aliasing", &m_antiAliasing, kAA, 3, 1);
		UnityGUI::Toggle("Stop NaNs", &m_stopNaNs, 1);
		UnityGUI::Toggle("Dithering", &m_dithering, 1);
		UnityGUI::Toggle("Render Shadows", &m_renderShadows, 1);
		UnityGUI::Int("Priority", &m_priority, 1);
		UnityGUI::Dropdown("Opaque Texture", &m_opaqueTexture, kTexture, 3, 1);
		UnityGUI::Dropdown("Depth Texture", &m_depthTexture, kTexture, 3, 1);
		UnityGUI::Dropdown("Culling Mask", &m_cullingMask, kCulling, 7, 1);
		UnityGUI::Toggle("Occlusion Culling", &m_occlusionCulling, 1);
	}

	// ---- Stack ----
	if (UnityGUI::Foldout("Stack"))
		UnityGUI::EmptyListBox("Cameras", "List is Empty");

	// ---- Environment ----
	if (UnityGUI::Foldout("Environment"))
	{
		UnityGUI::Dropdown("Background Type", &m_backgroundType, kBackground, 3, 1);
		if (m_backgroundType == 1)
			UnityGUI::Color("Background", m_backgroundColor, 2);
		UnityGUI::Label("Volumes", 1, true);
		UnityGUI::Dropdown("Update Mode", &m_volumeUpdateMode, kVolumeUpdate, 3, 2);
		UnityGUI::Dropdown("Volume Mask", &m_volumeMask, kVolumeMask, 3, 2);
		UnityGUI::ObjectField("Volume Trigger", "None (Transform)", 2);
	}

	// ---- Output ----
	if (UnityGUI::Foldout("Output"))
	{
		UnityGUI::ObjectField("Output Texture", "None (Render Texture)", 1);
		UnityGUI::Dropdown("Target Display", &m_targetDisplay, kDisplay, 8, 1);
		UnityGUI::Dropdown("Target Eye", &m_targetEye, kEye, 4, 1);
		UnityGUI::Vector2Pair("Viewport Rect", "X", &m_viewportRect[0], "Y", &m_viewportRect[1], 1);
		UnityGUI::Vector2Pair("", "W", &m_viewportRect[2], "H", &m_viewportRect[3], 1);
	}

	if (projectionChanged)
		ProjUpdate();
}

// Ä«¸Ş¶ó ·»´õ ¹üÀ§Draw
void Camera::OnDrawGizmos()
{
	// Scene ë·° ì˜¤ë²„ë ˆì´ê°€ í™œì„±ì¼ ë•Œë§Œ ê·¸ë¦°ë‹¤. ì„ íƒë˜ì§€ ì•Šì€ ì¹´ë©”ë¼ëŠ” ì§§ì€ í”„ëŸ¬ìŠ¤í…€, ì„ íƒë˜ë©´ ë” ê¸¸ê²Œ í‘œì‹œ.
	if (!SceneViewOverlay::IsActive())
		return;

	Transform* transform = GetGameObject()->GetTransform();
	const bool selected = (SelectionManager::GetSelectedGameObject() == GetGameObject());
	const float drawFar = selected ? min(m_farZ, 6.0f) : min(m_farZ, 1.8f);
	const ImU32 color = selected ? IM_COL32(255, 255, 255, 235) : IM_COL32(200, 200, 200, 170);
	SceneViewOverlay::DrawFrustum(transform->GetWorldMatrix(), m_nearZ, drawFar, XMConvertToDegrees(m_fovY), color);
}

GENERATE_COMPONENT_FUNC_TOJSON(Camera)
{
	json j = {};

	SERIALIZE_TYPE(j, Camera);
	SERIALIZE_ENUM(j, m_cameraType, "cameraType");
	 
	SERIALIZE_FLOAT(j, m_nearZ, "nearZ");
	SERIALIZE_FLOAT(j, m_farZ, "farZ"); 
	// aspect ëŠ” í™”ë©´ í¬ê¸°ì—ì„œ ë§¤ í”„ë ˆì„ ì •í•´ì§€ëŠ” ê°’ì´ë¼ ì €ì¥í•˜ì§€ ì•ŠëŠ”ë‹¤ (Unity ì™€ ê°™ìŒ, ì €ì¥í•˜ë©´ ì—´ìë§ˆì ì”¬ì´ "*" ë¡œ ë°”ë€ë‹¤)
	SERIALIZE_FLOAT(j, m_fovY, "fovY");

	j["enabled"] = m_Enabled;
	j["orthoSize"] = m_orthoSize;
	j["fovVerticalAxis"] = m_fovVerticalAxis;
	j["physicalCamera"] = m_physicalCamera;
	j["renderType"] = m_renderType;
	j["renderer"] = m_renderer;
	j["postProcessing"] = m_postProcessing;
	j["antiAliasing"] = m_antiAliasing;
	j["stopNaNs"] = m_stopNaNs;
	j["dithering"] = m_dithering;
	j["renderShadows"] = m_renderShadows;
	j["priority"] = m_priority;
	j["opaqueTexture"] = m_opaqueTexture;
	j["depthTexture"] = m_depthTexture;
	j["cullingMask"] = m_cullingMask;
	j["occlusionCulling"] = m_occlusionCulling;
	j["backgroundType"] = m_backgroundType;
	j["backgroundColor"] = { m_backgroundColor[0], m_backgroundColor[1], m_backgroundColor[2], m_backgroundColor[3] };
	j["volumeUpdateMode"] = m_volumeUpdateMode;
	j["volumeMask"] = m_volumeMask;
	j["targetDisplay"] = m_targetDisplay;
	j["targetEye"] = m_targetEye;
	j["viewportRect"] = { m_viewportRect[0], m_viewportRect[1], m_viewportRect[2], m_viewportRect[3] };

	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(Camera) 
{
	DE_SERIALIZE_ENUM(j, m_cameraType, "cameraType", ProjectionType);

	DE_SERIALIZE_FLOAT(j, m_nearZ, "nearZ");
	DE_SERIALIZE_FLOAT(j, m_farZ, "farZ");
	// aspect ëŠ” ì €ì¥í•˜ì§€ ì•ŠëŠ”ë‹¤ (í™”ë©´ í¬ê¸°ì—ì„œ ì •í•´ì§)
	DE_SERIALIZE_FLOAT(j, m_fovY, "fovY");

	m_Enabled = j.value("enabled", true);
	m_orthoSize = j.value("orthoSize", 5.0f);
	m_fovVerticalAxis = j.value("fovVerticalAxis", true);
	m_physicalCamera = j.value("physicalCamera", false);
	m_renderType = j.value("renderType", 0);
	m_renderer = j.value("renderer", 0);
	m_postProcessing = j.value("postProcessing", false);
	m_antiAliasing = j.value("antiAliasing", 0);
	m_stopNaNs = j.value("stopNaNs", false);
	m_dithering = j.value("dithering", false);
	m_renderShadows = j.value("renderShadows", true);
	m_priority = j.value("priority", 0);
	m_opaqueTexture = j.value("opaqueTexture", 2);
	m_depthTexture = j.value("depthTexture", 2);
	m_cullingMask = j.value("cullingMask", 0);
	m_occlusionCulling = j.value("occlusionCulling", true);
	m_backgroundType = j.value("backgroundType", 0);
	m_volumeUpdateMode = j.value("volumeUpdateMode", 0);
	m_volumeMask = j.value("volumeMask", 0);
	m_targetDisplay = j.value("targetDisplay", 0);
	m_targetEye = j.value("targetEye", 0);
	if (j.contains("backgroundColor") && j.at("backgroundColor").is_array() && j.at("backgroundColor").size() == 4)
		for (int i = 0; i < 4; ++i) m_backgroundColor[i] = j.at("backgroundColor")[i].get<float>();
	if (j.contains("viewportRect") && j.at("viewportRect").is_array() && j.at("viewportRect").size() == 4)
		for (int i = 0; i < 4; ++i) m_viewportRect[i] = j.at("viewportRect")[i].get<float>();

	m_nearZ = max(m_nearZ, 0.001f);
	m_nearZ = min(m_nearZ, 9999); 
	float fov_degree = XMConvertToDegrees(m_fovY);
	fov_degree = max(fov_degree, 1); 
	fov_degree = min(fov_degree, 360); 
	m_fovY = XMConvertToRadians(fov_degree); 

	ProjUpdate();
}