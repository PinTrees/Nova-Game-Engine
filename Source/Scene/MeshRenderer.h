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
	// MaterialPropertyBlock 값 하나 (float 은 V.x)
	struct BlockValue { XMFLOAT4 V = XMFLOAT4(0, 0, 0, 0); bool Color = false; };

private:
	std::vector<std::pair<std::string, BlockValue>> m_Block;   // 이름 순
	uint64 m_BlockHash = 0;
	vector<shared_ptr<UMaterial>> m_RenderMaterials;   // 블록을 입힌 파생 재질 (GetRenderMaterials)
	uint64 m_RenderStamp = 0;

public:
	MeshRenderer();
	virtual ~MeshRenderer();

	void AddMaterial(shared_ptr<UMaterial> mat) { m_pMaterials.push_back(mat); }
	// 재질 칸 index 를 경로로 (모자란 칸은 Default-Material). "builtin:Default-Material" = 기본 재질 — 모델 배치 (ModelPlacement)
	bool SetMaterialPath(int index, const wstring& path);
	const vector<shared_ptr<UMaterial>>& GetMaterials() const { return m_pMaterials; }

	// ---- 스크립트 (C#): Renderer.material · sharedMaterial · MaterialPropertyBlock
	// 재질 칸에 재질 객체 (런타임 사본이면 path 를 비워 씬에 저장하는 경로는 그대로 — Unity 처럼 사본은 저장되지 않는다)
	void SetMaterialAt(int index, shared_ptr<UMaterial> material, const wstring& path);
	// MaterialPropertyBlock (SetPropertyBlock): 재질은 공유한 채 이 렌더러만 값을 바꾼다. 같은 재질 · 같은 블록 값의 렌더러는
	//  파생 재질 하나를 같이 써서 인스턴싱 묶음이 유지된다 (값이 렌더러마다 다르면 그만큼 묶음이 나뉜다). 빈 목록 = 블록 없음
	void SetPropertyBlock(std::vector<std::pair<std::string, BlockValue>> values);
	const std::vector<std::pair<std::string, BlockValue>>& GetPropertyBlock() const { return m_Block; }
	// 그릴 재질: 블록이 없으면 GetMaterials 그대로, 있으면 파생 재질 (재질 값이 바뀌면 다시 만든다)
	const vector<shared_ptr<UMaterial>>& GetRenderMaterials();
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