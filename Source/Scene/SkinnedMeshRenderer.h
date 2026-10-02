#pragma once
#include "Component.h"

class Model;
class Shader;
class Effect;
class UMaterial;
class InstancingBuffer;
class SkeletonAvataData;

// Unity 의 Skinned Mesh Renderer.
//  - 메시와 스켈레톤(노드 계층)은 같은 FBX 에서 가져온다.
//  - 본 행렬(팔레트)은 Animation 컴포넌트가 ApplyPose 로 넣어 주며, 없으면 바인드 포즈로 그린다.
class NOVA_API SkinnedMeshRenderer : public Component
{
	using Super = Component;

private:
	Shader*					m_Shader = nullptr;
	wstring					m_ShaderPath;
	shared_ptr<Effect>		m_Effect;

	shared_ptr<SkinnedMesh> m_Mesh;
	wstring					m_MeshPath;
	int						m_MeshSubsetIndex = 0;

	shared_ptr<SkeletonAvataData> m_Skeleton;     // 같은 FBX 의 노드 계층
	vector<int>				m_PaletteNode;        // 팔레트 본 k → 스켈레톤 노드 인덱스
	XMFLOAT4X4				m_MeshBind;           // 메시 노드의 바인드 전역 행렬 (단위 변환 제외) - 정점을 장면 공간으로

	vector<shared_ptr<UMaterial>>	m_pMaterials;
	vector<wstring>					m_MaterialPaths;

	// Runtime Value
	vector<XMFLOAT4X4>				m_FinalTransforms;   // 셰이더로 넘기는 본 행렬 (Offset * 전역)

	// ---- Unity Inspector 값 ----
	Vec3	m_BoundsCenter = Vec3::Zero;
	Vec3	m_BoundsExtent = Vec3::Zero;
	int		m_Quality = 0;                 // Auto, 1 Bone, 2 Bones, 4 Bones
	bool	m_UpdateWhenOffscreen = false;
	std::string m_RootBone;                // 루트 본 이름 (표시용)
	int		m_CastShadows = 0;
	bool	m_StaticShadowCaster = false;
	int		m_LightProbes = 1;
	bool	m_SkinnedMotionVectors = true;
	bool	m_DynamicOcclusion = true;
	int		m_RenderingLayerMask = 0;
	int		m_MaskInteraction = 0;

public:
	SkinnedMeshRenderer();
	virtual ~SkinnedMeshRenderer();

	void AddMaterial(shared_ptr<UMaterial> mat) { m_pMaterials.push_back(mat); }
	void SetMesh(shared_ptr<SkinnedMesh> mesh) { m_Mesh = mesh; }
	// FBX 경로의 index 번째 스킨 메시를 쓴다 (스켈레톤, 기본 재질, 바운드, 바인드 포즈를 함께 설정)
	void SetSkinnedMesh(const wstring& path, int index);
	void SetShader(Shader* shader) { m_Shader = shader; }
	void SetShader(shared_ptr<Effect> effect) { m_Effect = effect; }
	vector<XMFLOAT4X4>& GetFinalTransforms() { return m_FinalTransforms; }

	shared_ptr<SkinnedMesh> GetMesh() { return m_Mesh; }
	const wstring& GetMeshPath() const { return m_MeshPath; }
	shared_ptr<SkeletonAvataData> GetSkeleton() { return m_Skeleton; }

	// 스켈레톤 노드 전역 행렬로 본 팔레트를 계산한다
	void ApplyPose(const vector<XMFLOAT4X4>& nodeGlobals);
	void ResetToBindPose();

	// 인스턴싱용 ID (메시 기준)
	InstanceID GetInstanceID()
	{
		return make_tuple((uint64)m_Mesh.get(), 0, 0);
	}

public:
	virtual void OnInspectorGUI() override;
	virtual bool UsesUnityInspector() const override { return true; }
	virtual const char* InspectorIconName() const override { return "skinned_mesh_renderer"; }
	void DrawMaterialInspectors();

public:
	virtual void Render() override;
	void RenderShadow();
	void RenderShadowNormal();

public:
	virtual void _Editor_Render();
	virtual void _Editor_RenderShadowNormal();

private:
	void RebuildPalette();
	void EnsureBones();
	void DrawSkinned(bool editor);

	GENERATE_COMPONENT_BODY(SkinnedMeshRenderer)
};

REGISTER_COMPONENT(SkinnedMeshRenderer)
