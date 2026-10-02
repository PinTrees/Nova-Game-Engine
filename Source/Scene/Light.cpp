#include "pch.h"
#include "TagsAndLayers.h"
#include "App.h"
#include "Light.h"
#include "UnityGUI.h"
#include "SceneViewOverlay.h"
#include "LightManager.h"
#include "Transform.h"
#include "SceneViewManager.h"
#include "SceneEditorWindow.h"
#include "EditorCamera.h"
#include "Camera.h"
#include "DisplayManager.h"

Light::Light()
{
	m_InspectorTitleName = "Light";
	m_InspectorIconPath = L"directional_right.png"; 

	m_DirectionalDesc.Init();
	m_PointDesc.Init();
	m_SpotDesc.Init();

	m_LightView.resize(6);
	m_EditorLightView.resize(6);
	ProjUpdate();
}

Light::~Light()
{
}

void Light::OnDestroy()
{
	// Manager ����
	LightManager::GetI()->DeleteLight(this->GetInstanceID());
}

// 방향광 그림자: 카메라 앞쪽 kShadowDistance 범위를 덮는 정사영 상자.
// 상자를 텍셀 단위로 맞춰 카메라가 움직여도 그림자 가장자리가 떨리지 않게 한다.
static void FitDirectionalShadow(XMVECTOR lightDir, const XMFLOAT3& cameraPos, const XMFLOAT3& cameraLook, XMMATRIX& view, XMMATRIX& proj)
{
	constexpr float kShadowDistance = 40.0f;   // 카메라에서 이 거리까지 그림자
	constexpr float kHalfExtent = 30.0f;       // 상자 반폭 (2048 텍셀 → 약 3cm/텍셀)
	constexpr float kDepthBack = 150.0f;       // 상자 중심에서 광원 쪽으로 물러난 거리 (높은 물체의 그림자 포함)
	constexpr float kShadowMapSize = 2048.0f;

	lightDir = XMVector3Normalize(lightDir);
	const XMVECTOR up = fabsf(XMVectorGetY(lightDir)) > 0.99f ? XMVectorSet(0, 0, 1, 0) : XMVectorSet(0, 1, 0, 0);
	XMVECTOR look = XMVectorSet(cameraLook.x, cameraLook.y, cameraLook.z, 0.0f);
	look = XMVectorGetX(XMVector3LengthSq(look)) > 1e-8f ? XMVector3Normalize(look) : XMVectorSet(0, 0, 1, 0);
	XMVECTOR center = XMVectorAdd(XMVectorSet(cameraPos.x, cameraPos.y, cameraPos.z, 1.0f), XMVectorScale(look, kShadowDistance * 0.4f));

	const XMMATRIX lightRot = XMMatrixLookToLH(XMVectorZero(), lightDir, up);
	XMVECTOR ls = XMVector3TransformCoord(center, lightRot);
	const float texel = kHalfExtent * 2.0f / kShadowMapSize;
	ls = XMVectorSet(floorf(XMVectorGetX(ls) / texel) * texel, floorf(XMVectorGetY(ls) / texel) * texel, XMVectorGetZ(ls), 1.0f);
	center = XMVector3TransformCoord(ls, XMMatrixInverse(nullptr, lightRot));

	const XMVECTOR eye = XMVectorSubtract(center, XMVectorScale(lightDir, kDepthBack));
	view = XMMatrixLookToLH(eye, lightDir, up);
	proj = XMMatrixOrthographicLH(kHalfExtent * 2.0f, kHalfExtent * 2.0f, 0.1f, kDepthBack * 2.0f);
}

// forward(+Z) 가 dir 을 향하는 회전 (왼손 좌표계)
static Quaternion LookRotation(const XMFLOAT3& dir)
{
	Vec3 f(dir.x, dir.y, dir.z);
	if (f.LengthSquared() < 1e-8f)
		return Quaternion::Identity;
	f.Normalize();
	Vec3 upRef = fabsf(f.y) > 0.99f ? Vec3(1, 0, 0) : Vec3(0, 1, 0);
	Vec3 right = upRef.Cross(f);
	right.Normalize();
	Vec3 up = f.Cross(right);
	Matrix m(right.x, right.y, right.z, 0, up.x, up.y, up.z, 0, f.x, f.y, f.z, 0, 0, 0, 0, 1);
	Quaternion q = Quaternion::CreateFromRotationMatrix(m);
	q.Normalize();
	return q;
}

void Light::SetDirLight(DirectionalLight light)
{
	m_DirectionalDesc = light;
	m_pGameObject->GetTransform()->SetLocalRotation(LookRotation(light.Direction));
}

void Light::SetPointLight(PointLight light)
{
	m_PointDesc = light;
	m_pGameObject->GetTransform()->SetLocalPosition(light.Position);
}

void Light::SetSpotLight(SpotLight light)
{
	m_SpotDesc = light;
	m_pGameObject->GetTransform()->SetLocalRotation(LookRotation(light.Direction));
	m_pGameObject->GetTransform()->SetLocalPosition(light.Position);
}

void Light::Awake()
{
	// Manager �߰�
	//LightManager::GetI()->SetLight(shared_from_this());
}

float Light::GetRange()
{
	switch (m_LightType)
	{
	case LightType::Point:
		return m_PointDesc.Range;
	case LightType::Spot:
		return m_SpotDesc.Range;
	}
}

void Light::Update()
{
}

void Light::LateUpdate()
{
}

void Light::FixedUpdate()
{
}

void Light::LastUpdate()
{
}

void Light::ViewUpdate()
{
	XMVECTOR pos = m_pGameObject->GetTransform()->GetPosition();
	XMVECTOR lookDir = m_pGameObject->GetTransform()->GetLook();
	XMVECTOR target = pos + lookDir;
	XMVECTOR upDir = m_pGameObject->GetTransform()->GetUp();

	Vec3 r;

	XMVECTOR backwardDir;
	XMVECTOR leftDir;
	XMVECTOR rightDir;
	XMVECTOR downDir;

	XMVECTOR tempPos;
	XMVECTOR defaultPos;
	float cameraFarZ;

	XMFLOAT3 gameCameraPos = DisplayManager::GetI()->GetActiveCamera()->GetPosition();

	switch (m_LightType)
	{
	case LightType::Directional:
		// Pos = GameCameraPosition + LightRotation�������� Camera ���� ��輱�� ��ġ
		// light pos = camera position + ((camerafarZ * 2) * light.dir)
		// light pos�� camera position�� �߽����� CameraFarZ(������) ���� ��ġ�ؾ���

		// 그림자용 광원 위치: 카메라 위치에서 빛이 오는 방향(forward 의 반대)으로 FarZ 만큼 떨어진 곳
		r = XMVector3Normalize(lookDir);
		FitDirectionalShadow(lookDir, gameCameraPos, DisplayManager::GetI()->GetActiveCamera()->GetLook(), m_LightView[0], m_LightProj);
		XMStoreFloat3(&m_DirectionalDesc.Direction, r);
		return;   // 투영도 FitDirectionalShadow 가 정한다
	case LightType::Point:
		// �������̹Ƿ� 6���� ��Ʈ���� �ʿ�, proj�� �ϳ����̿��� �����(����)

		backwardDir = m_pGameObject->GetTransform()->GetBackward();
		leftDir = m_pGameObject->GetTransform()->GetLeft();
		rightDir = m_pGameObject->GetTransform()->GetRight();
		downDir = m_pGameObject->GetTransform()->GetDown();

		m_LightView[0] = ::XMMatrixLookAtLH(pos, pos + lookDir, upDir);
		m_LightView[1] = ::XMMatrixLookAtLH(pos, pos + backwardDir, upDir);
		m_LightView[2] = ::XMMatrixLookAtLH(pos, pos + leftDir, upDir);
		m_LightView[3] = ::XMMatrixLookAtLH(pos, pos + rightDir, upDir);
		m_LightView[4] = ::XMMatrixLookAtLH(pos, pos + upDir, backwardDir);
		m_LightView[5] = ::XMMatrixLookAtLH(pos, pos + downDir, lookDir);

		XMStoreFloat3(&m_PointDesc.Position, pos);
		break;
	case LightType::Spot:
		r = XMVector3Normalize(lookDir);   // 스포트라이트 방향 = forward

		m_LightView[0] = ::XMMatrixLookAtLH(pos, target, upDir);

		XMStoreFloat3(&m_SpotDesc.Position, pos);
		XMStoreFloat3(&m_SpotDesc.Direction, r);
		break;
	default:
		break;
	}

	ProjUpdate();
}

void Light::EditorViewUpdate()
{
	// 빌드된 게임(플레이어)에는 Scene 창이 없다
	if (SceneViewManager::GetI()->m_LastActiveSceneEditorWindow == nullptr)
		return;
	XMVECTOR pos = m_pGameObject->GetTransform()->GetPosition();
	XMVECTOR lookDir = m_pGameObject->GetTransform()->GetLook();
	XMVECTOR target = pos + lookDir;
	XMVECTOR upDir = m_pGameObject->GetTransform()->GetUp();

	Vec3 r; Vec3 angleTest;

	XMVECTOR backwardDir;
	XMVECTOR leftDir;
	XMVECTOR rightDir;
	XMVECTOR downDir;

	XMVECTOR tempPos;
	XMVECTOR defaultPos;
	float cameraFarZ;

	XMFLOAT3 editorCameraPos = SceneViewManager::GetI()->m_LastActiveSceneEditorWindow->GetSceneCamera()->GetPosition();

	switch (m_LightType)
	{
	case LightType::Directional:
	{
		// Pos = GameCameraPosition + LightRotation�������� Camera ���� ��輱�� ��ġ
		// light pos = camera position + ((camerafarZ * 2) * light.dir)
		// light pos�� camera position�� �߽����� CameraFarZ(������) ���� ��ġ�ؾ���

		// 그림자용 광원 위치: 카메라 위치에서 빛이 오는 방향(forward 의 반대)으로 FarZ 만큼 떨어진 곳
		r = XMVector3Normalize(lookDir);
		FitDirectionalShadow(lookDir, editorCameraPos, SceneViewManager::GetI()->m_LastActiveSceneEditorWindow->GetSceneCamera()->GetLook(), m_EditorLightView[0], m_EditorLightProj);
		XMStoreFloat3(&m_DirectionalDesc.Direction, r);
		return;   // 투영도 FitDirectionalShadow 가 정한다
	}
		
		break;
	case LightType::Point:
		// �������̹Ƿ� 6���� ��Ʈ���� �ʿ�, proj�� �ϳ����̿��� �����(����)

		backwardDir = m_pGameObject->GetTransform()->GetBackward();
		leftDir = m_pGameObject->GetTransform()->GetLeft();
		rightDir = m_pGameObject->GetTransform()->GetRight();
		downDir = m_pGameObject->GetTransform()->GetDown();

		m_EditorLightView[0] = ::XMMatrixLookAtLH(pos, pos + lookDir, upDir);
		m_EditorLightView[1] = ::XMMatrixLookAtLH(pos, pos + backwardDir, upDir);
		m_EditorLightView[2] = ::XMMatrixLookAtLH(pos, pos + leftDir, upDir);
		m_EditorLightView[3] = ::XMMatrixLookAtLH(pos, pos + rightDir, upDir);
		m_EditorLightView[4] = ::XMMatrixLookAtLH(pos, pos + upDir, backwardDir);
		m_EditorLightView[5] = ::XMMatrixLookAtLH(pos, pos + downDir, lookDir);

		XMStoreFloat3(&m_PointDesc.Position, pos);
		break;
	case LightType::Spot:
		r = XMVector3Normalize(lookDir);   // 스포트라이트 방향 = forward

		m_EditorLightView[0] = ::XMMatrixLookAtLH(pos, target, upDir);

		XMStoreFloat3(&m_SpotDesc.Position, pos);
		XMStoreFloat3(&m_SpotDesc.Direction, r);
		break;
	default:
		break;
	}

	EditorProjUpdate();
}

void Light::Render()
{
	
}

// private function
void Light::ProjUpdate()
{
	if (!DisplayManager::GetI()->GetActiveCamera())
		return;

	float gameAspect = Application::GetI()->GetApp()->AspectRatio();
	float gameFarZ = DisplayManager::GetI()->GetActiveCamera()->GetFarZ();
	switch (m_LightType)
	{
	case LightType::Directional:
		// ����
		// X, Y ���� ����, FarZ �̷л� ���������� ���ҽ� ȿ���� ���� ���� ��ġ�� �� ��ġ ������ �Ÿ� + ���� ũ���
		m_LightProj = ::XMMatrixOrthographicLH(gameFarZ * 2, gameFarZ * 2, 0.001f, gameFarZ * 2);
		break;
	case LightType::Point:
		// ����
		// float fov = XMConvertToRadians(90.0f);
		// XMMatrixOrthographicLH(D3DXToRadian(90), 1, near, range)
		// X, Y �ǹ� �����Ƿ� ������ ������, FarZ�� ���� ����
		//m_lightProj = ::XMMatrixOrthographicLH(10.0f, 10.0f, 0.001f, m_pointDesc.Range);
		m_LightProj = ::XMMatrixPerspectiveFovLH(XMConvertToRadians(90), gameAspect, 0.001f, m_PointDesc.Range);
		break;
	case LightType::Spot:
		// ����
		// XMMatrixOrthographicLH(D3DXToRadian(angle) <- fov, aspectRatio <- camera ����, near, range)
		// 1920x1080�� ��� : aspectRatio = 1920/1080 = 1.78
		// X, Y, FarZ ���� ����
		m_LightProj = ::XMMatrixPerspectiveFovLH(XMConvertToRadians(m_SpotDesc.Spot), gameAspect, 0.001f, m_SpotDesc.Range);
		break;
	default:
		break;
	}
}

void Light::EditorProjUpdate()
{
	if (SceneViewManager::GetI()->m_LastActiveSceneEditorWindow == nullptr)
		return;
	float editorAspect = SceneViewManager::GetI()->m_LastActiveSceneEditorWindow->GetSceneCamera()->GetAspect();
	float editorFarZ = SceneViewManager::GetI()->m_LastActiveSceneEditorWindow->GetSceneCamera()->GetFarZ();
	switch (m_LightType)
	{
	case LightType::Directional:
		m_EditorLightProj = ::XMMatrixOrthographicLH(editorFarZ * 2, editorFarZ * 2, 0.001f, editorFarZ * 2);
		break;
	case LightType::Point:
		m_EditorLightProj = ::XMMatrixPerspectiveFovLH(XMConvertToRadians(90), editorAspect, 0.001f, m_PointDesc.Range);
		break;
	case LightType::Spot:
		m_EditorLightProj = ::XMMatrixPerspectiveFovLH(XMConvertToRadians(m_SpotDesc.Spot), editorAspect, 0.001f, m_SpotDesc.Range);
		break;
	default:
		break;
	}
}

string Light::GetStringLightType(LightType type)
{
	switch (type)
	{
	case LightType::Directional: return "Directional";
	case LightType::Point: return "Point";
	case LightType::Spot: return "Spot";
	default: return "Unknown";
	}
}

const char* Light::InspectorIconName() const
{
	switch (m_LightType)
	{
	case LightType::Point: return "light_point";
	case LightType::Spot: return "light_spot";
	default: return "light_directional";
	}
}

XMFLOAT4* Light::CurrentDiffuse()
{
	switch (m_LightType)
	{
	case LightType::Point: return &m_PointDesc.Diffuse;
	case LightType::Spot: return &m_SpotDesc.Diffuse;
	default: return &m_DirectionalDesc.Diffuse;
	}
}

// 색온도(K) → RGB (Tanner Helland 근사)
static void KelvinToRGB(float kelvin, float rgb[3])
{
	float t = std::clamp(kelvin, 1000.0f, 40000.0f) / 100.0f;
	float r = t <= 66.0f ? 255.0f : 329.698727446f * powf(t - 60.0f, -0.1332047592f);
	float g = t <= 66.0f ? 99.4708025861f * logf(t) - 161.1195681661f : 288.1221695283f * powf(t - 60.0f, -0.0755148492f);
	float b = t >= 66.0f ? 255.0f : (t <= 19.0f ? 0.0f : 138.5177312231f * logf(t - 10.0f) - 305.0447927307f);
	rgb[0] = std::clamp(r, 0.0f, 255.0f) / 255.0f;
	rgb[1] = std::clamp(g, 0.0f, 255.0f) / 255.0f;
	rgb[2] = std::clamp(b, 0.0f, 255.0f) / 255.0f;
}

void Light::OnInspectorGUI()
{
	static const char* kTypes[] = { "Spot", "Directional", "Point" };
	static const LightType kTypeMap[] = { LightType::Spot, LightType::Directional, LightType::Point };
	static const char* kModes[] = { "Realtime", "Mixed", "Baked" };
	static const char* kAppearance[] = { "Color", "Filter and Temperature" };
	static const char* kRenderingLayers[] = { "Default" };
	static const char* kCulling[] = { "Everything", "Nothing", "Default", "TransparentFX", "Ignore Raycast", "Water", "UI" };
	static const char* kShadow[] = { "No Shadows", "Hard Shadows", "Soft Shadows" };

	bool projectionChanged = false;

	if (UnityGUI::Foldout("General"))
	{
		int typeIndex = 1;
		for (int i = 0; i < 3; ++i)
			if (kTypeMap[i] == m_LightType) typeIndex = i;
		if (UnityGUI::Dropdown("Type", &typeIndex, kTypes, 3, 1))
		{
			m_LightType = kTypeMap[typeIndex];
			projectionChanged = true;
		}
		UnityGUI::Dropdown("Mode", &m_Mode, kModes, 3, 1);
	}

	if (UnityGUI::Foldout("Emission"))
	{
		UnityGUI::Dropdown("Light Appearance", &m_ColorMode, kAppearance, 2, 1);

		XMFLOAT4* diffuse = CurrentDiffuse();
		if (m_ColorMode == 0)
		{
			float c[4] = { diffuse->x, diffuse->y, diffuse->z, 1.0f };
			if (UnityGUI::Color("Color", c, 1))
			{
				diffuse->x = c[0]; diffuse->y = c[1]; diffuse->z = c[2];
			}
		}
		else
		{
			bool changed = UnityGUI::Color("Filter", m_Filter, 2);
			changed |= UnityGUI::TemperatureBar("Temperature", &m_Temperature, 1000.0f, 12000.0f, 2);
			if (changed)
			{
				float k[3];
				KelvinToRGB(m_Temperature, k);
				diffuse->x = k[0] * m_Filter[0];
				diffuse->y = k[1] * m_Filter[1];
				diffuse->z = k[2] * m_Filter[2];
			}
		}

		if (UnityGUI::Float("Intensity", &m_Intensity, 1))
			m_Intensity = (std::max)(m_Intensity, 0.0f);
		UnityGUI::Float("Indirect Multiplier", &m_IndirectMultiplier, 1);

		if (m_LightType != LightType::Directional)
			UnityGUI::HelpBox("Realtime indirect bounce shadowing is only supported for Directional lights.", true, 0);

		if (m_LightType == LightType::Point)
		{
			if (UnityGUI::Float("Range", &m_PointDesc.Range, 1)) projectionChanged = true;
		}
		else if (m_LightType == LightType::Spot)
		{
			if (UnityGUI::Float("Range", &m_SpotDesc.Range, 1)) projectionChanged = true;
			if (UnityGUI::Float("Spot Angle", &m_SpotDesc.Spot, 1)) projectionChanged = true;
		}

		bool cookie = false;
		UnityGUI::ToggleLeft("Cookie", &cookie, 1, true, true);
	}

	if (UnityGUI::Foldout("Rendering"))
	{
		int layers = 0;
		UnityGUI::Dropdown("Rendering Layers", &layers, kRenderingLayers, 1, 1, true);
		UnityGUI::MaskField("Culling Mask", &m_CullingMask, 1);   // 이 빛이 비추는 레이어 (그림자도)
	}

	if (UnityGUI::Foldout("Shadows"))
	{
		// Unity URP: Shadow Type 아래 Realtime Shadows (Strength / Bias / Near Plane)
		static const char* kBias[] = { "Use settings from Render Pipeline", "Custom" };
		UnityGUI::Dropdown("Shadow Type", &m_ShadowType, kShadow, 3, 1);
		if (m_ShadowType != 0)
		{
			UnityGUI::Label("Realtime Shadows", 1, true);
			UnityGUI::Slider("Strength", &m_ShadowStrength, 0.0f, 1.0f, 2);
			UnityGUI::Dropdown("Bias", &m_ShadowBiasMode, kBias, 2, 2);
			if (m_ShadowBiasMode == 1)
			{
				UnityGUI::Slider("Depth", &m_ShadowDepthBias, 0.0f, 10.0f, 3);
				UnityGUI::Slider("Normal", &m_ShadowNormalBias, 0.0f, 10.0f, 3);
			}
			if (m_LightType != LightType::Directional)
				UnityGUI::Slider("Near Plane", &m_ShadowNearPlane, 0.1f, 10.0f, 2);
			else
				UnityGUI::HelpBox("Cascades, Max Distance and Soft Shadows quality come from the Shadows override of the Volume.", false, 1);
		}
	}

	if (projectionChanged)
	{
		ProjUpdate();
		EditorProjUpdate();
	}
}

void Light::OnDrawGizmos()
{
	if (!SceneViewOverlay::IsActive())
		return;

	Transform* transform = GetGameObject()->GetTransform();
	Vec3 pos = transform->GetPosition();
	// 빛 방향 = Transform 의 forward (회전을 바꾸면 바로 반영)
	XMFLOAT3 dir;
	XMStoreFloat3(&dir, XMVector3Normalize(transform->GetLook()));

	const int kind = m_LightType == LightType::Directional ? 0 : (m_LightType == LightType::Point ? 1 : 2);
	const bool selected = (SelectionManager::GetSelectedGameObject() == GetGameObject());
	SceneViewOverlay::DrawLightGizmo(XMFLOAT3(pos.x, pos.y, pos.z), dir, kind, selected);
}



GENERATE_COMPONENT_FUNC_TOJSON(Light)
{
	json j = {};
	j["type"] = "Light";
	j["lightType"] = m_LightType;

	j["directionalLightAmbient"] = { m_DirectionalDesc.Ambient.x, m_DirectionalDesc.Ambient.y, m_DirectionalDesc.Ambient.z, m_DirectionalDesc.Ambient.w };
	j["directionalLightDiffuse"] = { m_DirectionalDesc.Diffuse.x, m_DirectionalDesc.Diffuse.y, m_DirectionalDesc.Diffuse.z, m_DirectionalDesc.Diffuse.w };
	j["directionalLightSpecular"] = { m_DirectionalDesc.Specular.x, m_DirectionalDesc.Specular.y, m_DirectionalDesc.Specular.z, m_DirectionalDesc.Specular.w };

	j["pointLightAmbient"] = { m_PointDesc.Ambient.x, m_PointDesc.Ambient.y, m_PointDesc.Ambient.z, m_PointDesc.Ambient.w };
	j["pointLightDiffuse"] = { m_PointDesc.Diffuse.x, m_PointDesc.Diffuse.y, m_PointDesc.Diffuse.z, m_PointDesc.Diffuse.w };
	j["pointLightSpecular"] = { m_PointDesc.Specular.x, m_PointDesc.Specular.y, m_PointDesc.Specular.z, m_PointDesc.Specular.w };
	j["pointLightRange"] = m_PointDesc.Range;
	j["pointLightAtt"] = { m_PointDesc.Att.x,m_PointDesc.Att.y,m_PointDesc.Att.z };

	j["spotLightAmbient"] = { m_SpotDesc.Ambient.x, m_SpotDesc.Ambient.y, m_SpotDesc.Ambient.z, m_SpotDesc.Ambient.w };
	j["spotLightDiffuse"] = { m_SpotDesc.Diffuse.x, m_SpotDesc.Diffuse.y, m_SpotDesc.Diffuse.z, m_SpotDesc.Diffuse.w };
	j["spotLightSpecular"] = { m_SpotDesc.Specular.x, m_SpotDesc.Specular.y, m_SpotDesc.Specular.z, m_SpotDesc.Specular.w };
	j["spotLightRange"] = m_SpotDesc.Range;
	j["spotLightSpot"] = m_SpotDesc.Spot;
	j["spotLightAtt"] = { m_SpotDesc.Att.x,m_SpotDesc.Att.y,m_SpotDesc.Att.z };

	j["enabled"] = m_Enabled;
	j["intensity"] = m_Intensity;
	j["indirectMultiplier"] = m_IndirectMultiplier;
	j["mode"] = m_Mode;
	j["colorMode"] = m_ColorMode;
	j["temperature"] = m_Temperature;
	j["filter"] = { m_Filter[0], m_Filter[1], m_Filter[2], m_Filter[3] };
	j["cullingMaskBits"] = m_CullingMask;
	j["shadowType"] = m_ShadowType;
	j["shadowStrength"] = m_ShadowStrength;
	j["shadowBiasMode"] = m_ShadowBiasMode;
	j["shadowDepthBias"] = m_ShadowDepthBias;
	j["shadowNormalBias"] = m_ShadowNormalBias;
	j["shadowNearPlane"] = m_ShadowNearPlane;
	j["shadowVersion"] = 2;

	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(Light)
{
	if (j.contains("lightType"))
	{
		m_LightType = j.at("lightType").get<LightType>();
	}

	m_Enabled = j.value("enabled", true);
	m_Intensity = j.value("intensity", 1.0f);
	m_IndirectMultiplier = j.value("indirectMultiplier", 1.0f);
	m_Mode = j.value("mode", 0);
	m_ColorMode = j.value("colorMode", 0);
	m_Temperature = j.value("temperature", 6570.0f);
	m_CullingMask = j.contains("cullingMaskBits") ? j["cullingMaskBits"].get<uint32>() : LegacyCullingMask(j.value("cullingMask", 0));
	m_ShadowType = j.value("shadowType", 0);
	m_ShadowStrength = j.value("shadowStrength", 1.0f);
	m_ShadowBiasMode = j.value("shadowBiasMode", 0);
	m_ShadowDepthBias = j.value("shadowDepthBias", 1.0f);
	m_ShadowNormalBias = j.value("shadowNormalBias", 1.0f);
	m_ShadowNearPlane = j.value("shadowNearPlane", 0.2f);
	// 그림자가 실제로 그려지기 전(shadowVersion 없음)에 저장된 Directional Light 는 Unity 기본값인 Soft Shadows 로 옮긴다
	if (!j.contains("shadowVersion") && m_LightType == LightType::Directional && m_ShadowType == 0)
		m_ShadowType = 2;
	if (j.contains("filter") && j.at("filter").is_array() && j.at("filter").size() == 4)
		for (int i = 0; i < 4; ++i) m_Filter[i] = j.at("filter")[i].get<float>();

	// Directional
	{
		DE_SERIALIZE_FLOAT4(j, m_DirectionalDesc.Ambient, "directionalLightAmbient");
		DE_SERIALIZE_FLOAT4(j, m_DirectionalDesc.Diffuse, "directionalLightDiffuse");
		DE_SERIALIZE_FLOAT4(j, m_DirectionalDesc.Specular, "directionalLightSpecular");

	}

	// Point
	{
		DE_SERIALIZE_FLOAT4(j, m_PointDesc.Ambient, "pointLightAmbient");
		DE_SERIALIZE_FLOAT4(j, m_PointDesc.Diffuse, "pointLightDiffuse");
		DE_SERIALIZE_FLOAT4(j, m_PointDesc.Specular, "pointLightSpecular");
		DE_SERIALIZE_FLOAT(j, m_PointDesc.Range, "pointLightRange");
		DE_SERIALIZE_FLOAT3(j, m_PointDesc.Att, "pointLightAtt");
	}

	// Spot
	{
		DE_SERIALIZE_FLOAT4(j, m_SpotDesc.Ambient, "spotLightAmbient");
		DE_SERIALIZE_FLOAT4(j, m_SpotDesc.Diffuse, "spotLightDiffuse");
		DE_SERIALIZE_FLOAT4(j, m_SpotDesc.Specular, "spotLightSpecular");
		DE_SERIALIZE_FLOAT(j, m_SpotDesc.Range, "spotLightRange");
		DE_SERIALIZE_FLOAT(j, m_SpotDesc.Spot, "spotLightSpot");
		DE_SERIALIZE_FLOAT3(j, m_SpotDesc.Att, "spotLightAtt");

	}

	ProjUpdate();
	EditorProjUpdate();
}