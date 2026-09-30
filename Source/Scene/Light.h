#pragma once
#include "Component.h"
#include "LightHelper.h"

class Light
	: public Component
{
private:
	LightType m_LightType = LightType::Directional;

	// ������ ������ �ִ� ����ü ������ Transform�̶� �����ؾ��ұ�?
	DirectionalLight m_DirectionalDesc;
	PointLight m_PointDesc;
	SpotLight m_SpotDesc;

	// Unity(URP) Light Inspector 항목. Color/Intensity/Type/Range 는 실제로 적용되고, 나머지는 값 저장용이다.
	float m_Intensity = 1.0f;
	float m_IndirectMultiplier = 1.0f;
	int   m_Mode = 0;            // Realtime / Mixed / Baked
	int   m_ColorMode = 0;       // 0 Color, 1 Filter and Temperature
	float m_Temperature = 6570.0f;
	float m_Filter[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	int   m_CullingMask = 0;
	int   m_ShadowType = 0;      // No Shadows / Hard / Soft
	// Unity URP Light > Shadows
	float m_ShadowStrength = 1.0f;
	int   m_ShadowBiasMode = 0;       // 0 = Use settings from Render Pipeline (Volume > Shadows), 1 = Custom
	float m_ShadowDepthBias = 1.0f;   // 텍셀 단위 (Custom)
	float m_ShadowNormalBias = 1.0f;
	float m_ShadowNearPlane = 0.2f;   // 스포트/점광 그림자 투영의 가까운 면

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

	// 렌더링에 넘기는 값은 Intensity 가 곱해진 복사본
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

	// Shadow Type 이 No Shadows 가 아니면 그림자를 드리운다
	bool CastsShadows() const { return m_ShadowType != 0; }
	bool SoftShadows() const { return m_ShadowType == 2; }
	void SetShadowType(int type) { m_ShadowType = type; }
	float GetShadowStrength() const { return m_ShadowStrength; }
	bool UsesCustomShadowBias() const { return m_ShadowBiasMode == 1; }
	float GetShadowDepthBias() const { return m_ShadowDepthBias; }
	float GetShadowNormalBias() const { return m_ShadowNormalBias; }
	float GetShadowNearPlane() const { return m_ShadowNearPlane; }
	float GetSpotAngle() const { return m_SpotDesc.Spot; }

	GENERATE_COMPONENT_BODY(Light)
};

REGISTER_COMPONENT(Light)
