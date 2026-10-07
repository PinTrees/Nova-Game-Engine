#pragma once
#include "MaterialBlock.h"
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
	mutable MaterialBlock			m_Block;   // C# MaterialPropertyBlock
	const vector<shared_ptr<UMaterial>>& RenderMaterials() const { return m_Block.Apply(m_pMaterials); }   // 그릴 재질 (블록이면 파생 재질)

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
	bool UploadDynamicVertices(const vector<Vertex::PosNormalTexTanSkinned>& verts);
	void DrawSubset(GfxContext* dc, int subset);

	// 천 (Cloth): 시뮬레이션한 정점 (이 오브젝트 공간) 을 동적 버퍼로 — 정점은 팔레트 끝의 단위 본 (SimulatedBoneSlot) 하나에 묶는다
	bool							m_SimActive = false;
	bool							m_SimDirty = false;
	vector<Vertex::PosNormalTexTanSkinned> m_SimVerts;

	// ---- Unity Inspector 값 ----
	Vec3	m_BoundsCenter = Vec3::Zero;
	Vec3	m_BoundsExtent = Vec3::Zero;
	int		m_Quality = 0;                 // Auto, 1 Bone, 2 Bones, 4 Bones
	bool	m_UpdateWhenOffscreen = false;
	std::string m_RootBone;                // 루트 본 이름 (표시용)
	int		m_CastShadows = 0;
	int		m_SortingLayerId = 0;   // C# Renderer.sortingLayerID · sortingOrder (값만 — 이 렌더러는 정렬하지 않는다)
	int		m_SortingOrder = 0;
	bool	m_StaticShadowCaster = false;
	int		m_LightProbes = 1;
	bool	m_SkinnedMotionVectors = true;
	int		m_MotionVectors = 1;   // Camera Motion Only / Per Object Motion / Force No Motion (Unity)
	bool	m_DynamicOcclusion = true;
	int		m_RenderingLayerMask = 0;
	int		m_MaskInteraction = 0;

public:
	SkinnedMeshRenderer();
	virtual ~SkinnedMeshRenderer();

	void AddMaterial(shared_ptr<UMaterial> mat) { m_pMaterials.push_back(mat); }
	// index 번째 재질 칸을 이 .mat 으로 (프로젝트 상대 경로, 읽지 못하면 그대로)
	bool SetMaterialPath(int index, const wstring& path);

	// ---- 스크립트 (C# Renderer): 재질 · MaterialPropertyBlock · 그림자 (Mesh Renderer 와 같은 뜻)
	const vector<shared_ptr<UMaterial>>& GetMaterials() const { return m_pMaterials; }
	void SetMaterialAt(int index, shared_ptr<UMaterial> material, const wstring& path);   // path 가 비면 씬에 저장하는 경로는 그대로 (런타임 사본)
	void SetPropertyBlock(MaterialBlock::Values values) { m_Block.Set(std::move(values)); }
	const MaterialBlock::Values& GetPropertyBlock() const { return m_Block.Get(); }
	MaterialBlock& PropertyBlock() { return m_Block; }   // 재질 칸 블록 (SetAt · GetAt) 까지
	int GetCastShadows() const { return m_CastShadows; }
	void SetCastShadows(int mode) { m_CastShadows = mode; }
	int GetSortingLayerId() const { return m_SortingLayerId; }
	void SetSortingLayerId(int id) { m_SortingLayerId = id; }
	int GetSortingOrder() const { return m_SortingOrder; }
	void SetSortingOrder(int order) { m_SortingOrder = order; }

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

	// 모션 벡터 (MotionVectors): Unity 의 Motion Vectors · Skinned Motion Vectors, 이번 본 팔레트 (준비해서), 서브셋 그리기 (셰이더 값은 부른 쪽이)
	int GetMotionVectors() const { return m_MotionVectors; }
	bool GetSkinnedMotionVectors() const { return m_SkinnedMotionVectors; }
	const vector<XMFLOAT4X4>& MotionPalette();
	void DrawForMotionVectors(GfxContext* dc, FxTechnique* tech);

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
	// 지금 그리는 자세의 노드 전역 행렬 (ApplyPose 에 준 값 — 팔레트 본은 스키닝 행렬에서 되돌리고, 나머지는 부모 아래 바인드 로컬). 래그돌이 읽는다
	bool GetNodeGlobals(vector<XMFLOAT4X4>& out) const;

	// ---- 천 (Cloth 컴포넌트): 스킨 위의 천
	// 팔레트 본 수 (정점 boneIndices 가 가리키는 범위). 본 행렬 = 정점 (메시 바인드 공간) → 이 오브젝트 공간
	int PaletteBoneCount() const;
	// 메시 바인드 정점 → 이 오브젝트 공간 (바인드 포즈 — 메시 노드 변환 + 단위 변환)
	XMMATRIX BindToObject() const;
	// 천이 고친 정점으로 그린다 (정점 = 이 오브젝트 공간, boneIndices[0] = SimulatedBoneSlot, 가중치 1). nullptr = 원래대로
	void SetSimulatedVertices(const vector<Vertex::PosNormalTexTanSkinned>* vertices);
	int SimulatedBoneSlot() const { return PaletteBoneCount(); }
	bool CanSimulate() const { return m_Mesh != nullptr && PaletteBoneCount() < 256; }

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
