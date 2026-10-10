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
struct AnimationClip;
namespace CrowdAnimation { struct Baked; }

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
	XMFLOAT4X4				m_MeshBind;           // 팔레트 앞에 곱하는 메시 바인드 (SkinnedMesh::PaletteMeshBind)
	int						m_BindMode = -1;      // 메시 바인드 후보 고정 (-1 자동 — Unity 가져오기가 정답과 맞춰 넣는다)

	// 부위 합치기 (모듈형 캐릭터 — Play): 같은 부모 · 같은 스켈레톤의 스킨 부위들을 메시 하나로 (첫 부위가 대표로 그린다).
	//  같은 부위 · 재질 조합은 합친 메시를 같이 쓴다 (같은 프리팹 1 만 개 = 메시 하나 → 인스턴싱 한 묶음)
	bool					m_Merged = false;     // 대표 렌더러에 합쳐졌다 (그리기 · 자세 계산을 건너뛴다)
	bool					m_Combined = false;   // 대표: 합친 메시로 그린다 (메시 바인드 = 단위 — 역바인드에 미리 곱했다)
	bool					m_CombineTried = false;
	shared_ptr<SkinnedMesh>	m_OwnMesh;            // 합치기 전 (되돌리기)
	vector<shared_ptr<UMaterial>> m_OwnMaterials;
	shared_ptr<void>		m_CombinedRef;        // 합친 메시 캐시를 붙잡는다 (같은 조합의 대표가 다 사라지면 놓는다)
	void TryCombine();

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

	// 자동 LOD (NOVA — 군중): 화면에 작게 보이면 줄인 지오메트리 (SkinnedLod). 뷰 (게임 · Scene) 마다 프레임에 한 번 고른다
	//  — 깊이 프리패스와 본 패스 (EQUAL) 가 같은 단계로 그려야 한다. 그림자 · 모션 벡터는 마지막에 고른 것
	bool							m_AutoLod = true;   // 기본 켬 — 캐릭터를 수천 개 놓아도 따로 설정 없이 (LOD · 인스턴싱 · 임포스터 · 애니메이션 갱신 간격)
	int								m_LodLevel[2] = { 0, 0 };
	uint32_t						m_LodStamp[2] = { ~0u, ~0u };
	MeshGeometry*					m_LodGeoView[2] = { nullptr, nullptr };
	MeshGeometry*					m_LodGeo = nullptr;   // DrawSubset 이 쓰는 것 (nullptr = 원본)
	int								m_LodCurrent = 0;
	// 캐릭터 크기 (스켈레톤 바인드 자세 노드 범위, 이 오브젝트 공간): 같은 스켈레톤의 부위들 (모듈형 캐릭터) 이 같은 단계를 고르게
	Vec3							m_SkelCenter = Vec3::Zero;
	float							m_SkelSize = 0.0f;
	void SelectLod(bool editor);
	void ResetLod() { m_LodStamp[0] = m_LodStamp[1] = ~0u; m_LodGeoView[0] = m_LodGeoView[1] = m_LodGeo = nullptr; m_LodCurrent = 0; }

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
	void SetMesh(shared_ptr<SkinnedMesh> mesh) { m_Mesh = mesh; ResetLod(); }
	bool GetAutoLod() const { return m_AutoLod; }
	void SetAutoLod(bool on) { m_AutoLod = on; ResetLod(); }
	int CurrentLod() const { return m_LodCurrent; }   // 마지막에 그린 단계 (0 = 원본)
	bool GetUpdateWhenOffscreen() const { return m_UpdateWhenOffscreen; }
	int GetBindMode() const { return m_Combined ? 1 : m_BindMode; }
	bool IsMerged() const { return m_Merged; }
	static uint32_t s_MergeSerial;   // 어느 렌더러가 합쳐질 때마다 1 씩 (Animator 패키지가 그릴 렌더러 목록을 다시 거른다 — NovaCore 의 값 하나)
	bool IsCombined() const { return m_Combined; }
	virtual void Start() override;

	// ---- 스킨드 인스턴싱 (SkinnedInstancing — 군중)
	bool CanInstance() const;                        // Auto LOD · BlendShape 없음 · 천 없음 · 패키지 셰이더 · Alpha Clipping 없음
	int InstanceCheck() const;                       // 작업 스레드용: 0 아님, 1 됨, 2 메인에서 CanInstance (재질 블록이 있으면 파생 재질을 만든다)
	MeshGeometry* PrepareLod(bool editor);           // 이 뷰의 LOD 를 골라 그릴 지오메트리
	MeshGeometry* CurrentGeometry();                 // 마지막에 고른 지오메트리 (모션 벡터)
	MeshGeometry* ShadowGeometry();                  // 그림자: 한 단계 거칠게
	const vector<int>& PaletteNodes() const { return m_PaletteNode; }   // 팔레트 본 → 스켈레톤 노드 (Animator 가 필요한 노드만 계산)
	const vector<shared_ptr<UMaterial>>& DrawMaterials() const { return RenderMaterials(); }
	bool HasMaterialBlock() const { return !m_Block.Empty(); }   // 블록이 있으면 DrawMaterials 가 파생 재질을 만들 수 있다 (메인에서만)
	uint32_t InstPaletteFrame = ~0u;                 // 이번 프레임 팔레트를 올렸으면 SceneCulling::FrameIndex
	uint32_t InstPalette = 0;                        // 팔레트 버퍼의 시작 칸 (float4)
	uint32_t InstPrevPalette = ~0u;                  // 지난 팔레트 버퍼의 시작 칸 (지난 프레임에 없었으면 ~0)
	bool InstNoPalette = false;                      // 이번 프레임 팔레트를 쓰지 않았다 (임포스터로만 그린다)
	uint32_t InstPoseSerial = 0;                     // 팔레트를 올릴 때의 PoseSerial
	bool InstPoseChanged = false;                    // 지난 팔레트 뒤로 자세가 바뀌었나 (모션 벡터)
	XMFLOAT4X4 InstWorld, InstPrevWorld;             // 팔레트를 올릴 때의 월드 · 지난 월드
	uint32_t PoseSerial = 0;                         // ApplyPose 마다 1 씩

	// 재생 중인 클립 (Animator · Animation 이 프레임마다 알려 준다) — 멀리서 그 클립의 애니메이션 임포스터로 그린다
	std::shared_ptr<AnimationClip> AnimClip;
	float AnimTime = 0.0f;                           // 클립 안 초
	bool AnimLoop = true;
	void SetAnimationHint(const std::shared_ptr<AnimationClip>& clip, float time, bool loop) { AnimClip = clip; AnimTime = time; AnimLoop = loop; }
	std::shared_ptr<CrowdAnimation::Baked> InstImpostor;   // 자동 임포스터 (재생 중인 클립 · 재질로 구운 것)
	const void* InstImpostorClip = nullptr;
	const void* InstImpostorMats = nullptr;
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
