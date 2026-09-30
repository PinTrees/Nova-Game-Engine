#pragma once
#include "Component.h"
#include "LightHelper.h"

class Light
	: public Component
{
private:
	LightType m_LightType = LightType::Directional;

	// ∞¢µµ∏¶ ∞°¡ˆ∞Ì ¿÷¥¬ ±∏¡∂√º ¡§∫∏¥¬ Transform¿Ã∂˚ ø¨µø«ÿæﬂ«“±Ó?
	DirectionalLight m_DirectionalDesc;
	PointLight m_PointDesc;
	SpotLight m_SpotDesc;

	// Unity(URP) Light Inspector Ìï≠Î™©. Color/Intensity/Type/Range Îäî Ïã§Ï†úÎ°ú Ï†ÅÏö©ÎêòÍ≥†, ÎÇòÎ®∏ÏßÄÎäî Í∞í Ï†ÄÏû•Ïö©Ïù¥Îã§.
	float m_Intensity = 1.0f;
	float m_IndirectMultiplier = 1.0f;
	int   m_Mode = 0;            // Realtime / Mixed / Baked
	int   m_ColorMode = 0;       // 0 Color, 1 Filter and Temperature
	float m_Temperature = 6570.0f;
	float m_Filter[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	int   m_CullingMask = 0;
	int   m_ShadowType = 0;      // No Shadows / Hard / Soft

	vector<XMMATRIX> m_LightView;
	XMMATRIX m_LightProj;

	vector<XMMATRIX> m_EditorLightView;
	XMMATRIX m_EditorLightProj;

	// shadowTransform => world * (lightV * lightP * toTexSpace)

	void ProjUpdate();
	void ApplyIntensity(XMFLOAT4& c) const { c.x *= m_Intensity; c.y *= m_Intensity; c.z *= m_Intensity; }
	XMFLOAT4* CurrentDiffuse();
	void EditorProjUpdate();
	string GetStringLightType(LightType type);

public:
	Light();
	~Light();

	InstanceID GetInstanceID()
	{
		return make_tuple((uint64)&m_DirectionalDesc, (uint64)&m_PointDesc, (uint64)&m_SpotDesc);
	}

	virtual void Awake() override;
	virtual void Update() override;
	virtual void LateUpdate() override;
	virtual void FixedUpdate() override;
	virtual void LastUpdate() override;
	virtual void Render() override;

	virtual void OnDestroy() override;

	virtual void OnInspectorGUI() override;
	virtual bool UsesUnityInspector() const override { return true; }
	virtual const char* InspectorIconName() const override;
	virtual void OnDrawGizmos() override;

	void ViewUpdate();
	void EditorViewUpdate();

	void SetDirLight(DirectionalLight light);
	void SetPointLight(PointLight light);
	void SetSpotLight(SpotLight light);

	// Î†åÎçîÎßÅÏóê ÎÑòÍ∏∞Îäî Í∞íÏùÄ Intensity Í∞Ä Í≥±Ìï¥ÏßÑ Î≥µÏÇ¨Î≥∏
	DirectionalLight GetDirLight() { DirectionalLight d = m_DirectionalDesc; ApplyIntensity(d.Diffuse); ApplyIntensity(d.Specular); return d; }
	PointLight GetPointLight() { PointLight d = m_PointDesc; ApplyIntensity(d.Diffuse); ApplyIntensity(d.Specular); return d; }
	SpotLight GetSpotLight() { SpotLight d = m_SpotDesc; ApplyIntensity(d.Diffuse); ApplyIntensity(d.Specular); return d; }

	float GetRange();

	vector<XMMATRIX> GetEditorLightViewArray() { return m_EditorLightView; }
	XMMATRIX GetEditorLightView() { return m_EditorLightView[0]; }
	XMMATRIX GetEditorLightProjection() { return m_EditorLightProj; }
	XMMATRIX GetEditorLightViewProjection(int index) { return m_EditorLightView[index] * m_EditorLightProj; }

	vector<XMMATRIX> GetLightViewArray() { return m_LightView; }
	XMMATRIX GetLightView() { return m_LightView[0]; }
	XMMATRIX GetLightProjection() { return m_LightProj; }
	XMMATRIX GetLightViewProjection(int index) { return m_LightView[index] * m_LightProj; }

	// LightV * LightP * toTexSpace
	LightType GetLightType() { return m_LightType; }

	GENERATE_COMPONENT_BODY(Light)
};

REGISTER_COMPONENT(Light)
