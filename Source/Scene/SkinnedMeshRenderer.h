#pragma once
#include "CustomShaders.h"
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

	// Alpha Clipping 재질: 깊이 사전 패스 · 그림자에서도 잘라낸다
	UMaterial* ClipMaterial(int subset, float& cutoff) const;
	static XMMATRIX ClipTexTransform(const UMaterial& m);
	void DrawSkinnedNormalDepth(bool editor);
	vector<wstring>					m_MaterialPaths;

	// Runtime Value
	vector<XMFLOAT4X4>				m_FinalTransforms;   // 셰이더로 넘기는 본 행렬 (Offset * 전역)

	// BlendShape (Unity 와 같이 0..100): 가중치가 있으면 CPU 로 섞은 정점을 렌더러 자기 동적 버퍼에 (GPU 스키닝은 그대로)
	vector<float>					m_BlendWeights;
	vector<Vertex::PosNormalTexTanSkinned> m_MorphVerts;
	ComPtr<GfxBuffer>				m_MorphVB;
	uint32							m_MorphVBCount = 0;
	bool							m_MorphDirty = true;
	bool							m_MorphActive = false;
	void EnsureMorph();
	void DrawSubset(GfxContext* dc, int subset);

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
	// index 번째 재질 칸을 이 .mat 으로 (프로젝트 상대 경로, 읽지 못하면 그대로)
	bool SetMaterialPath(int index, const wstring& path);

private:
	// 패키지 셰이더 (CustomShaders) 에 넘길 그리기 정보
	CustomShaders::SkinnedDraw MakeCustomDraw(UMaterial* material, FXMMATRIX world, CXMMATRIX viewProj, bool editor, int subset);
	// 잘라내는 사용자 셰이더 (Shader Graph Alpha Clipping): 그림자 · 깊이 패스도 그 셰이더가 그린다 (true = 그렸다)
	bool DrawCustomClip(int subset, CustomShaders::DrawPass pass, FXMMATRIX world, CXMMATRIX viewProj, CXMMATRIX view, bool editor);
public:
	void SetMesh(shared_ptr<SkinnedMesh> mesh) { m_Mesh = mesh; }
	// FBX 경로의 index 번째 스킨 메시를 쓴다 (스켈레톤, 기본 재질, 바운드, 바인드 포즈를 함께 설정)
	void SetSkinnedMesh(const wstring& path, int index);
	void SetShader(Shader* shader) { m_Shader = shader; }
	void SetShader(shared_ptr<Effect> effect) { m_Effect = effect; }
	vector<XMFLOAT4X4>& GetFinalTransforms() { return m_FinalTransforms; }

	shared_ptr<SkinnedMesh> GetMesh() { return m_Mesh; }
	// Unity 의 localBounds: 바인드 포즈 상자 (메시 노드 변환 + 단위 변환 — 이 렌더러 GameObject 공간). 없으면 false
	bool LocalBounds(Vec3& center, Vec3& extent) const
	{
		center = m_BoundsCenter;
		extent = m_BoundsExtent;
		return extent.x > 0.0f || extent.y > 0.0f || extent.z > 0.0f;
	}

	// ---- BlendShape (Unity: SkinnedMeshRenderer.SetBlendShapeWeight · sharedMesh.blendShapeCount …)
	int BlendShapeCount() const;
	std::string BlendShapeName(int index) const;
	int BlendShapeIndex(const std::string& name) const;   // 없으면 -1
	float GetBlendShapeWeight(int index) const;            // 0..100
	void SetBlendShapeWeight(int index, float weight);
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
