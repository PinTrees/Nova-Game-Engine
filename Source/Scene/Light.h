#pragma once
#include "Component.h"
#include "LightHelper.h"

class Light
	: public Component
{
private:
	LightType m_LightType = LightType::Directional;

	// 각도를 가지고 있는 구조체 정보는 Transform이랑 연동해야할까?
	DirectionalLight m_DirectionalDesc;
	PointLight m_PointDesc;
	SpotLight m_SpotDesc;

	vector<XMMATRIX> m_LightView;
	XMMATRIX m_LightProj;

	vector<XMMATRIX> m_EditorLightView;
	XMMATRIX m_EditorLightProj;

	// shadowTransform => world * (lightV * lightP * toTexSpace)

	void ProjUpdate();
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
	virtual void OnDrawGizmos() override;

	void ViewUpdate();
	void EditorViewUpdate();

	void SetDirLight(DirectionalLight light);
	void SetPointLight(PointLight light);
	void SetSpotLight(SpotLight light);

	DirectionalLight GetDirLight() { return m_DirectionalDesc; }
	PointLight GetPointLight() { return m_PointDesc; }
	SpotLight GetSpotLight() { return m_SpotDesc; }

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
