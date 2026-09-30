#include "pch.h"
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
	// Manager ªË¡¶
	LightManager::GetI()->DeleteLight(this->GetInstanceID());
}

void Light::SetDirLight(DirectionalLight light)
{
	m_DirectionalDesc = light;
	m_pGameObject->GetTransform()->SetLocalEulerRadians(light.Direction);
}

void Light::SetPointLight(PointLight light)
{
	m_PointDesc = light;
	m_pGameObject->GetTransform()->SetLocalPosition(light.Position);
}

void Light::SetSpotLight(SpotLight light)
{
	m_SpotDesc = light;
	m_pGameObject->GetTransform()->SetLocalEulerRadians(light.Direction);
	m_pGameObject->GetTransform()->SetLocalPosition(light.Position);
}

void Light::Awake()
{
	// Manager √ﬂ∞°
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
		// Pos = GameCameraPosition + LightRotationπÊ«‚¬ ¿« Camera π¸¿ß ∞Ê∞Ëº±ø° ¿ßƒ°
		// light pos = camera position + ((camerafarZ * 2) * light.dir)
		// light pos¥¬ camera position¿ª ¡ﬂΩ…¿∏∑Œ CameraFarZ(π›¡ˆ∏ß) ≥°ø° ¿ßƒ°«ÿæﬂ«‘

		r = m_pGameObject->GetTransform()->GetLocalEulerRadians();
		tempPos = XMLoadFloat3(&gameCameraPos);
		cameraFarZ = SceneViewManager::GetI()->m_LastActiveSceneEditorWindow->GetSceneCamera()->GetFarZ();
	
		XMVECTOR scsPos = XMVectorSet(
			-cameraFarZ * std::cosf(r.x) * std::sinf(r.y),
			cameraFarZ * std::sinf(r.x),
			-cameraFarZ * std::cosf(r.x) * std::cosf(r.y),
			0);

		pos = scsPos + tempPos;

		m_LightView[0] = ::XMMatrixLookAtLH(pos, pos + lookDir, upDir);

		XMStoreFloat3(&m_DirectionalDesc.Direction, r);
		break;
	case LightType::Point:
		// ¿¸πÊ«‚¿Ãπ«∑Œ 6∞≥¿« ∏≈∆Æ∏ØΩ∫ « ø‰, proj¥¬ «œ≥™∏∏¿Ãø©µµ ªÛ∞¸æ¯(π¸¿ß)

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
		r = m_pGameObject->GetTransform()->GetLocalEulerRadians();

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
		// Pos = GameCameraPosition + LightRotationπÊ«‚¬ ¿« Camera π¸¿ß ∞Ê∞Ëº±ø° ¿ßƒ°
		// light pos = camera position + ((camerafarZ * 2) * light.dir)
		// light pos¥¬ camera position¿ª ¡ﬂΩ…¿∏∑Œ CameraFarZ(π›¡ˆ∏ß) ≥°ø° ¿ßƒ°«ÿæﬂ«‘

		r = m_pGameObject->GetTransform()->GetLocalEulerRadians();
		tempPos = XMLoadFloat3(&editorCameraPos);
		cameraFarZ = SceneViewManager::GetI()->m_LastActiveSceneEditorWindow->GetSceneCamera()->GetFarZ();
		
		XMVECTOR scsPos = XMVectorSet(
			-cameraFarZ * std::cosf(r.x) * std::sinf(r.y), 
			cameraFarZ * std::sinf(r.x),
			-cameraFarZ * std::cosf(r.x) * std::cosf(r.y),
			0);

		pos = scsPos + tempPos;

		m_EditorLightView[0] = ::XMMatrixLookAtLH(pos, pos + lookDir, upDir);

		XMStoreFloat3(&m_DirectionalDesc.Direction, r);
	}
		
		break;
	case LightType::Point:
		// ¿¸πÊ«‚¿Ãπ«∑Œ 6∞≥¿« ∏≈∆Æ∏ØΩ∫ « ø‰, proj¥¬ «œ≥™∏∏¿Ãø©µµ ªÛ∞¸æ¯(π¸¿ß)

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
		r = m_pGameObject->GetTransform()->GetLocalEulerRadians();

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
		// ¡˜±≥
		// X, Y π¸¿ß «—¡§, FarZ ¿Ã∑–ªÛ π´«—¿Ã¡ˆ∏∏ ∏Æº“Ω∫ »ø¿≤¿ª ¿ß«ÿ ∫˚¿« ¿ßƒ°øÕ æ¿ ¿ßƒ° ªÁ¿Ã¿« ∞≈∏Æ + æ¿¿« ≈©±‚∑Œ
		m_LightProj = ::XMMatrixOrthographicLH(gameFarZ * 2, gameFarZ * 2, 0.001f, gameFarZ * 2);
		break;
	case LightType::Point:
		// ø¯±Ÿ
		// float fov = XMConvertToRadians(90.0f);
		// XMMatrixOrthographicLH(D3DXToRadian(90), 1, near, range)
		// X, Y ¿«πÃ æ¯¿∏π«∑Œ ¿”¿«¿« ∞™¿∏∑Œ, FarZ¥¬ π¸¿ß «—¡§
		//m_lightProj = ::XMMatrixOrthographicLH(10.0f, 10.0f, 0.001f, m_pointDesc.Range);
		m_LightProj = ::XMMatrixPerspectiveFovLH(XMConvertToRadians(90), gameAspect, 0.001f, m_PointDesc.Range);
		break;
	case LightType::Spot:
		// ø¯±Ÿ
		// XMMatrixOrthographicLH(D3DXToRadian(angle) <- fov, aspectRatio <- camera ∫Ò¿≤, near, range)
		// 1920x1080¿Œ ∞ÊøÏ : aspectRatio = 1920/1080 = 1.78
		// X, Y, FarZ π¸¿ß «—¡§
		m_LightProj = ::XMMatrixPerspectiveFovLH(XMConvertToRadians(m_SpotDesc.Spot), gameAspect, 0.001f, m_SpotDesc.Range);
		break;
	default:
		break;
	}
}

void Light::EditorProjUpdate()
{
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

// ÏÉâÏò®ÎèÑ(K) ‚Üí RGB (Tanner Helland Í∑ºÏÇ¨)
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
		UnityGUI::Dropdown("Culling Mask", &m_CullingMask, kCulling, 7, 1);
	}

	if (UnityGUI::Foldout("Shadows"))
		UnityGUI::Dropdown("Shadow Type", &m_ShadowType, kShadow, 3, 1);

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
	XMFLOAT3 dir(0.0f, -1.0f, 0.0f);
	if (m_LightType == LightType::Directional)
		dir = m_DirectionalDesc.Direction;
	else if (m_LightType == LightType::Spot)
		dir = m_SpotDesc.Direction;

	const bool selected = (SelectionManager::GetSelectedGameObject() == GetGameObject());
	SceneViewOverlay::DrawLightGizmo(XMFLOAT3(pos.x, pos.y, pos.z), dir, selected ? 3.0f : 2.0f,
		selected ? IM_COL32(255, 244, 180, 255) : IM_COL32(255, 235, 150, 200));
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
	j["cullingMask"] = m_CullingMask;
	j["shadowType"] = m_ShadowType;

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
	m_CullingMask = j.value("cullingMask", 0);
	m_ShadowType = j.value("shadowType", 0);
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