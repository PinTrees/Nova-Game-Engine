#pragma once
#include "Component.h"

class Model;
class Shader;
class Effect;
class UMaterial;
class InstancingBuffer;

class SkinnedMeshRenderer : public Component
{
	using Super = Component;

private:
	Shader*					m_Shader;
	wstring					m_ShaderPath;

	shared_ptr<Effect>		m_Effect;

	shared_ptr<SkinnedMesh> m_Mesh; 
	wstring					m_MeshPath;
	int						m_MeshSubsetIndex;		// 로드 시 사용 

	vector<shared_ptr<UMaterial>>	m_pMaterials;
	vector<wstring>					m_MaterialPaths;

	// Runtime Value
	vector<XMFLOAT4X4>				m_FinalTransforms;

public:
	SkinnedMeshRenderer();
	virtual ~SkinnedMeshRenderer();

	void AddMaterial(shared_ptr<UMaterial> mat) { m_pMaterials.push_back(mat); }
	void SetMesh(shared_ptr<SkinnedMesh> mesh) { m_Mesh = mesh; }
	void SetShader(Shader* shader) { m_Shader = shader; }
	void SetShader(shared_ptr<Effect> effect) { m_Effect = effect; }
	vector<XMFLOAT4X4>& GetFinalTransforms() { return m_FinalTransforms; }

	shared_ptr<SkinnedMesh> GetMesh() { return m_Mesh; }

	// 인스턴싱 ID, mesh and material and setting
	InstanceID GetInstanceID()
	{
		return make_tuple((uint64)m_Mesh.get(), 0, 0);
	}

public:
	virtual void OnInspectorGUI() override;

public:
	virtual void Render() override;
	void RenderShadow();
	void RenderShadowNormal();

public:
	virtual void _Editor_Render();
	virtual void _Editor_RenderShadowNormal();

	GENERATE_COMPONENT_BODY(SkinnedMeshRenderer)
};

REGISTER_COMPONENT(SkinnedMeshRenderer)