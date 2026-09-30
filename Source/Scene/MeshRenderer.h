#pragma once
#include "Component.h"
#include "UMaterial.h"

class Model;
class Shader;
class Effect;
class UMaterial;
class InstancingBuffer;

class MeshRenderer : public Component
{
	using Super = Component;

private:
	Shader*				m_Shader;
	wstring				m_ShaderPath;

	shared_ptr<Mesh>	m_Mesh;
	wstring				m_MeshPath;
	int					m_MeshSubsetIndex;

	// Unity(URP) MeshRenderer Inspector 항목. Cast Shadows 는 실제로 적용되고 나머지는 값 저장용이다.
	int  m_CastShadows = 0;             // 0 On, 1 Off, 2 Two Sided, 3 Shadows Only
	bool m_StaticShadowCaster = false;
	bool m_ContributeGI = false;
	int  m_ReceiveGI = 0;
	int  m_LightProbes = 1;             // Off / Blend Probes / Use Proxy Volume / Custom Provided
	int  m_MotionVectors = 1;           // Camera Motion / Per Object Motion / Force No Motion
	bool m_DynamicOcclusion = true;
	int  m_RenderingLayerMask = 0;
	int  m_MaskInteraction = 0;

	vector<shared_ptr<UMaterial>>	m_pMaterials;
	vector<wstring>					m_MaterialPaths;

public:
	MeshRenderer();
	virtual ~MeshRenderer();

	void AddMaterial(shared_ptr<UMaterial> mat) { m_pMaterials.push_back(mat); }
	const vector<shared_ptr<UMaterial>>& GetMaterials() const { return m_pMaterials; }
	void SetMesh(shared_ptr<Mesh> mesh) { m_Mesh = mesh; }
	// 엔진 내장 메시("builtin:Cube" 등)를 지정: 경로를 저장해 두었다가 씬을 다시 열 때 복원한다
void SetBuiltinMesh(const wstring& builtinPath, shared_ptr<Mesh> mesh)
	{
		m_Mesh = mesh;
		m_MeshPath = builtinPath;
		m_MeshSubsetIndex = 0;
		// Unity 기본 도형처럼 Default-Material 이 붙은 상태로 생성
		m_pMaterials.clear();
		m_MaterialPaths.clear();
		m_pMaterials.push_back(UMaterial::GetDefault());
		m_MaterialPaths.push_back(L"builtin:Default-Material");
	}
	void SetShader(Shader* shader) { m_Shader = shader; }

// 같은 GameObject 의 MeshFilter 에서 메시를 가져와 렌더링에 사용한다 (없으면 자체 메시)
	void SyncMeshFromFilter();
	bool HasOwnMesh() const { return m_Mesh != nullptr; }
	shared_ptr<Mesh> TakeOwnMesh(wstring& outPath, int& outSubset) { outPath = m_MeshPath; outSubset = m_MeshSubsetIndex; return m_Mesh; }

	shared_ptr<Mesh> GetMesh() { SyncMeshFromFilter(); if (m_Mesh) return m_Mesh; else return nullptr; }
	int GetCastShadows() const { return m_CastShadows; }

	// 이 렌더러가 사용하는 재질들의 Inspector (컴포넌트 아래에 표시)
	void DrawMaterialInspectors();

	// �ν��Ͻ� ID, mesh and material and setting
	InstanceID GetInstanceID()
	{
		return make_tuple((uint64)m_Mesh.get(), 0, 0);
	}

public:
	virtual void OnInspectorGUI() override;
	virtual bool UsesUnityInspector() const override { return true; }
	virtual const char* InspectorIconName() const override { return "mesh_renderer"; }

public:
	virtual void Render() override;
	void RenderShadow();
	void RenderShadowNormal();

	void RenderInstancing(shared_ptr<class InstancingBuffer>& buffer);
	void RenderShadowInstancing(shared_ptr<class InstancingBuffer>& buffer);
	void RenderShadowNormalInstancing(shared_ptr<class InstancingBuffer>& buffer);

public:
	virtual void _Editor_Render();
	virtual void _Editor_RenderShadowNormal();

	GENERATE_COMPONENT_BODY(MeshRenderer)
};

REGISTER_COMPONENT(MeshRenderer)