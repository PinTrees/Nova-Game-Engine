#include "pch.h"
#include "RenderLayers.h"
#include "SceneCulling.h"
#include "OcclusionCulling.h"
#include "CustomShaders.h"
#include "MaterialInspector.h"
#include "RenderStats.h"
#include "SkinnedMeshRenderer.h"
#include "EditorGUI.h"
#include "UnityGUI.h"
#include "Effects.h"
#include "MathHelper.h"
#include "AnimationPose.h"
#include "SkinnedLod.h"

namespace
{
	constexpr size_t kMaxBones = 256;   // 셰이더 gBoneTransforms 크기
}

SkinnedMeshRenderer::SkinnedMeshRenderer()
{
	XMStoreFloat4x4(&m_MeshBind, XMMatrixIdentity());
	m_InspectorTitleName = "Skinned Mesh Renderer";
	SkipUpdate = true;   // Update · LateUpdate 가 비어 있다 (Start 는 부른다)
	m_InspectorIconPath = L"skinned_mesh_renderer.png";
	SceneCulling::RegisterRenderer(this, true);   // 컬링의 렌더러 목록
}

SkinnedMeshRenderer::~SkinnedMeshRenderer()
{
	SceneCulling::UnregisterRenderer(this);
}

// ------------------------------------------------------------------ 메시 / 스켈레톤 / 포즈
bool SkinnedMeshRenderer::SetMaterialPath(int index, const wstring& path)
{
	if (index < 0 || path.empty())
		return false;
	shared_ptr<UMaterial> material = ResourceManager::GetI()->LoadMaterial(wstring_to_string(path));
	if (!material)
		return false;
	while ((int)m_pMaterials.size() <= index)
	{
		m_pMaterials.push_back(UMaterial::GetDefault());
		m_MaterialPaths.push_back(L"builtin:Default-Material");
	}
	m_pMaterials[index] = material;
	m_MaterialPaths[index] = path;
	return true;
}

void SkinnedMeshRenderer::SetMaterialAt(int index, shared_ptr<UMaterial> material, const wstring& path)
{
	if (index < 0)
		return;
	while ((int)m_pMaterials.size() <= index)
	{
		m_pMaterials.push_back(UMaterial::GetDefault());
		m_MaterialPaths.push_back(L"builtin:Default-Material");
	}
	m_pMaterials[index] = material;
	if (!path.empty())
		m_MaterialPaths[index] = path;
}

void SkinnedMeshRenderer::SetSkinnedMesh(const wstring& path, int index)
{
	m_MeshPath = path;
	m_MeshSubsetIndex = index;
	m_Mesh = path.empty() ? nullptr : ResourceManager::GetI()->LoadSkinnedMesh(path, index);
	m_Skeleton = nullptr;
	ResetLod();
	if (m_Mesh != nullptr)
	{
		m_Skeleton = ResourceManager::GetI()->LoadSkeletonAvata(wstring_to_string(path), 0);

		// 재질: 서브메시 재질 수만큼 기본 재질 (Unity 가 FBX 재질을 붙이는 것과 같은 자리)
		size_t matCount = 1;
		for (const auto& subset : m_Mesh->Subsets)
			matCount = (std::max)(matCount, (size_t)subset.MaterialIndex + 1);
		while (m_pMaterials.size() < matCount)
		{
			m_pMaterials.push_back(UMaterial::GetDefault());
			m_MaterialPaths.push_back(L"builtin:Default-Material");
		}
	}
	RebuildPalette();
	ResetToBindPose();
}

uint32_t SkinnedMeshRenderer::s_MergeSerial = 0;

namespace
{
	// 메시 · 스켈레톤 · 바인드 방식마다 한 번: 메시 바인드 (후보 20 개 계산) · 바인드 자세 AABB (정점 전부 스키닝).
	//  같은 부위를 쓰는 렌더러가 많다 (모듈형 캐릭터 1 만 명 × 부위 17 개 — 씬 불러오기 · Stop 마다 렌더러마다 다시 계산해 수십 초)
	struct BindInfo
	{
		std::weak_ptr<SkinnedMesh> Mesh;
		std::weak_ptr<SkeletonAvataData> Skeleton;
		XMFLOAT4X4 MeshBind, Chain;
		bool BoundsOk = false;
		XMFLOAT3 Lo = {}, Hi = {};
	};
	struct BindKey
	{
		const void* Mesh;
		const void* Skeleton;
		int Mode;
		bool operator==(const BindKey& o) const { return Mesh == o.Mesh && Skeleton == o.Skeleton && Mode == o.Mode; }
	};
	struct BindKeyHash
	{
		size_t operator()(const BindKey& k) const { return std::hash<const void*>()(k.Mesh) * 31u ^ std::hash<const void*>()(k.Skeleton) * 7u ^ (size_t)(k.Mode + 1); }
	};
	std::mutex s_BindLock;
	std::unordered_map<BindKey, BindInfo, BindKeyHash> s_BindInfos;

	const BindInfo& BindInfoOf(const shared_ptr<SkinnedMesh>& mesh, const shared_ptr<SkeletonAvataData>& sk, int mode)
	{
		const BindKey key{ mesh.get(), sk.get(), mode };
		{
			std::lock_guard<std::mutex> lock(s_BindLock);
			auto it = s_BindInfos.find(key);
			if (it != s_BindInfos.end() && it->second.Mesh.lock() == mesh && it->second.Skeleton.lock() == sk)
				return it->second;
		}
		BindInfo info;
		info.Mesh = mesh;
		info.Skeleton = sk;
		XMMATRIX chain = XMMatrixIdentity();
		const XMMATRIX meshBind = mesh->PaletteMeshBind(*sk, &chain, mode);
		XMStoreFloat4x4(&info.MeshBind, meshBind);
		XMStoreFloat4x4(&info.Chain, chain);
		info.BoundsOk = mesh->BindBounds(*sk, meshBind, info.Lo, info.Hi);
		std::lock_guard<std::mutex> lock(s_BindLock);
		return s_BindInfos.insert_or_assign(key, info).first->second;
	}

	// 스켈레톤마다 바인드 자세 전역 행렬 (ResetToBindPose)
	struct BindGlobals { std::weak_ptr<SkeletonAvataData> Owner; vector<XMFLOAT4X4> Global; };
	std::unordered_map<const SkeletonAvataData*, BindGlobals> s_BindGlobals;
}

void SkinnedMeshRenderer::RebuildPalette()
{
	m_PaletteNode.clear();
	m_RootBone.clear();
	XMStoreFloat4x4(&m_MeshBind, XMMatrixIdentity());
	if (m_Mesh == nullptr || m_Skeleton == nullptr)
		return;
	m_PaletteNode.resize(m_Mesh->BoneNames.size(), -1);
	for (size_t k = 0; k < m_Mesh->BoneNames.size(); ++k)
		m_PaletteNode[k] = m_Skeleton->FindNode(m_Mesh->BoneNames[k]);

	// 루트 본: 팔레트 본 중 가장 위쪽 노드 (표시용)
	int best = -1;
	for (int node : m_PaletteNode)
		if (node >= 0 && (best < 0 || node < best))
			best = node;
	if (best >= 0)
		m_RootBone = m_Skeleton->NodeNames[best];

	// 최종 = MeshBind * Offset * BoneGlobal — 역바인드가 장면 공간 (PreRotation 이 있는 메시 노드) 이면 메시 노드의 바인드 전역,
	//  메시 노드 공간 (Unreal 내보내기 등 — 메시 노드 변환이 이미 들어 있다) 이면 단위 행렬 (SkinnedMesh::PaletteMeshBind 가 고른다)
	const BindInfo& bind = BindInfoOf(m_Mesh, m_Skeleton, GetBindMode());   // 메시 · 스켈레톤 · 방식마다 한 번
	const XMMATRIX meshBind = XMLoadFloat4x4(&bind.Chain);
	m_MeshBind = bind.MeshBind;
	const bool boundsOk = bind.BoundsOk;
	const XMFLOAT3 bindLo = bind.Lo, bindHi = bind.Hi;

	// 캐릭터 크기: 스켈레톤 노드 (바인드 전역 · 단위 포함) 범위 — 부위가 작아도 (지갑 · 모자) 캐릭터 단위로 LOD 를 고른다
	{
		// 스켈레톤마다 한 번 (모듈형 캐릭터 1 만 명 × 부위 15 개가 같은 스켈레톤을 쓴다)
		struct SkelSize { std::weak_ptr<SkeletonAvataData> Owner; Vec3 Center; float Size = 0.0f; };
		static std::unordered_map<const SkeletonAvataData*, SkelSize> s_SkelSizes;
		auto it = s_SkelSizes.find(m_Skeleton.get());
		if (it == s_SkelSizes.end() || it->second.Owner.lock() != m_Skeleton)
		{
			SkelSize ss;
			ss.Owner = m_Skeleton;
			std::vector<XMFLOAT4X4> g;
			AnimationPose::ComputeGlobals(*m_Skeleton, m_Skeleton->BindLocal, g);
			Vec3 lo(FLT_MAX, FLT_MAX, FLT_MAX), hi(-FLT_MAX, -FLT_MAX, -FLT_MAX);
			for (const auto& m : g)   // 스켈레톤 전체 (모든 노드)
			{
				const Vec3 p(m._41, m._42, m._43);
				lo = Vec3::Min(lo, p);
				hi = Vec3::Max(hi, p);
			}
			if (lo.x <= hi.x)
			{
				ss.Center = (lo + hi) * 0.5f;
				ss.Size = (std::max)({ hi.x - lo.x, hi.y - lo.y, hi.z - lo.z }) * 1.1f;   // 머리 · 발끝은 노드보다 조금 더
			}
			it = s_SkelSizes.insert_or_assign(m_Skeleton.get(), ss).first;
		}
		m_SkelCenter = it->second.Center;
		m_SkelSize = it->second.Size;
	}

	// 바운드: 바인드 자세로 스키닝한 메시 AABB (고른 메시 바인드 · 단위 반영 — 메시 노드 변환을 쓰지 않는 내보내기도 맞다)
	Vec3 mn(FLT_MAX, FLT_MAX, FLT_MAX), mx(-FLT_MAX, -FLT_MAX, -FLT_MAX);
	if (boundsOk)
	{
		mn = Vec3(bindLo.x, bindLo.y, bindLo.z);
		mx = Vec3(bindHi.x, bindHi.y, bindHi.z);
	}
	else
	{
		const XMMATRIX toModel = meshBind * XMMatrixScaling(m_Skeleton->UnitScale, m_Skeleton->UnitScale, m_Skeleton->UnitScale);
		for (const auto& v : m_Mesh->Vertices)
		{
			Vec3 p = XMVector3TransformCoord(XMLoadFloat3(&v.pos), toModel);
			mn = Vec3::Min(mn, p);
			mx = Vec3::Max(mx, p);
		}
	}
	if (!m_Mesh->Vertices.empty() && mn.x <= mx.x)
	{
		m_BoundsCenter = (mn + mx) * 0.5f;
		m_BoundsExtent = (mx - mn) * 0.5f;
		for (float* v : { &m_BoundsCenter.x, &m_BoundsCenter.y, &m_BoundsCenter.z })
			if (fabsf(*v) < 1e-5f) *v = 0.0f;   // 부동소수점 잡음 (8.9e-08 같은 값) 정리
	}
}

void SkinnedMeshRenderer::ApplyPose(const vector<XMFLOAT4X4>& nodeGlobals)
{
	if (m_Mesh == nullptr || m_Merged)   // 합쳐진 부위: 대표가 그린다
		return;
	++PoseSerial;
	const size_t count = (std::min)(m_Mesh->BoneNames.size(), kMaxBones);
	m_FinalTransforms.resize((std::max)(count, (size_t)1));
	if (count == 0)
	{
		XMStoreFloat4x4(&m_FinalTransforms[0], XMMatrixIdentity());
		return;
	}
	for (size_t k = 0; k < count; ++k)
	{
		const int node = k < m_PaletteNode.size() ? m_PaletteNode[k] : -1;
		if (node < 0 || node >= (int)nodeGlobals.size())
		{
			XMStoreFloat4x4(&m_FinalTransforms[k], XMMatrixIdentity());
			continue;
		}
		XMMATRIX offset = XMLoadFloat4x4(&m_Mesh->BoneOffsets[k]);
		XMStoreFloat4x4(&m_FinalTransforms[k], XMLoadFloat4x4(&m_MeshBind) * offset * XMLoadFloat4x4(&nodeGlobals[node]));
	}
	if (m_SimActive)   // 천 정점이 묶이는 단위 본
	{
		m_FinalTransforms.resize(count + 1);
		XMStoreFloat4x4(&m_FinalTransforms[count], XMMatrixIdentity());
	}
}

int SkinnedMeshRenderer::PaletteBoneCount() const
{
	return m_Mesh ? (int)(std::min)(m_Mesh->BoneNames.size(), kMaxBones) : 0;
}

XMMATRIX SkinnedMeshRenderer::BindToObject() const
{
	const float u = m_Skeleton ? m_Skeleton->UnitScale : 1.0f;
	return XMLoadFloat4x4(&m_MeshBind) * XMMatrixScaling(u, u, u);
}

void SkinnedMeshRenderer::SetSimulatedVertices(const vector<Vertex::PosNormalTexTanSkinned>* vertices)
{
	if (vertices == nullptr || m_Mesh == nullptr || vertices->size() != m_Mesh->Vertices.size())
	{
		if (m_SimActive)
		{
			m_SimActive = false;
			m_SimVerts.clear();
			m_MorphDirty = true;   // BlendShape 정점 (있으면) 으로 되돌린다
			m_MorphActive = false;
			const size_t count = (size_t)PaletteBoneCount();
			if (count > 0 && m_FinalTransforms.size() > count)
				m_FinalTransforms.resize(count);
		}
		return;
	}
	m_SimActive = true;
	m_SimVerts = *vertices;
	m_SimDirty = true;
	const size_t slot = (size_t)SimulatedBoneSlot();
	if (m_FinalTransforms.size() <= slot)
	{
		const size_t from = m_FinalTransforms.size();
		m_FinalTransforms.resize(slot + 1);
		for (size_t i = from; i <= slot; ++i)
			XMStoreFloat4x4(&m_FinalTransforms[i], XMMatrixIdentity());
	}
	XMStoreFloat4x4(&m_FinalTransforms[slot], XMMatrixIdentity());
}

bool SkinnedMeshRenderer::GetNodeGlobals(vector<XMFLOAT4X4>& out) const
{
	if (m_Mesh == nullptr || m_Skeleton == nullptr)
		return false;
	const SkeletonAvataData& s = *m_Skeleton;
	const size_t n = s.BoneHierarchy.size();
	vector<int> paletteOf(n, -1);
	const size_t count = (std::min)(m_Mesh->BoneNames.size(), kMaxBones);
	for (size_t k = 0; k < count && k < m_PaletteNode.size() && k < m_FinalTransforms.size(); ++k)
		if (m_PaletteNode[k] >= 0 && m_PaletteNode[k] < (int)n)
			paletteOf[m_PaletteNode[k]] = (int)k;
	out.resize(n);
	const XMMATRIX unit = XMMatrixScaling(s.UnitScale, s.UnitScale, s.UnitScale);
	for (size_t i = 0; i < n; ++i)
	{
		const int k = paletteOf[i];
		if (k >= 0)
		{
			// final = MeshBind · offset · global → global = (MeshBind · offset)⁻¹ · final
			const XMMATRIX bindOffset = XMLoadFloat4x4(&m_MeshBind) * XMLoadFloat4x4(&m_Mesh->BoneOffsets[k]);
			XMStoreFloat4x4(&out[i], XMMatrixInverse(nullptr, bindOffset) * XMLoadFloat4x4(&m_FinalTransforms[k]));
			continue;
		}
		const int parent = s.BoneHierarchy[i];
		const XMMATRIX local = i < s.BindLocal.size() ? XMLoadFloat4x4(&s.BindLocal[i]) : XMMatrixIdentity();
		XMStoreFloat4x4(&out[i], parent >= 0 && parent < (int)i ? local * XMLoadFloat4x4(&out[parent]) : local * unit);
	}
	return true;
}

void SkinnedMeshRenderer::ResetToBindPose()
{
	if (m_Mesh == nullptr)
		return;
	if (m_Skeleton == nullptr)
	{
		m_FinalTransforms.assign((std::max)(m_Mesh->BoneNames.size(), (size_t)1) + (m_SimActive ? 1 : 0), XMFLOAT4X4());
		for (auto& m : m_FinalTransforms) XMStoreFloat4x4(&m, XMMatrixIdentity());
		return;
	}
	// 바인드 자세 전역은 스켈레톤마다 한 번 (메인 스레드 — 렌더러 만들기 · 불러오기)
	auto it = s_BindGlobals.find(m_Skeleton.get());
	if (it == s_BindGlobals.end() || it->second.Owner.lock() != m_Skeleton)
	{
		BindGlobals bg;
		bg.Owner = m_Skeleton;
		vector<XMFLOAT4X4> local;
		AnimationPose::SampleLocal(*m_Skeleton, nullptr, {}, 0.0f, local);
		AnimationPose::ComputeGlobals(*m_Skeleton, local, bg.Global);
		it = s_BindGlobals.insert_or_assign(m_Skeleton.get(), std::move(bg)).first;
	}
	ApplyPose(it->second.Global);
}

void SkinnedMeshRenderer::EnsureBones()
{
	if (m_FinalTransforms.empty())
		ResetToBindPose();
	EnsureMorph();
}

// ------------------------------------------------------------------ BlendShape
int SkinnedMeshRenderer::BlendShapeCount() const { return m_Mesh ? (int)m_Mesh->BlendShapes.size() : 0; }

std::string SkinnedMeshRenderer::BlendShapeName(int index) const
{
	return index >= 0 && index < BlendShapeCount() ? m_Mesh->BlendShapes[index].Name : std::string();
}

int SkinnedMeshRenderer::BlendShapeIndex(const std::string& name) const { return m_Mesh ? m_Mesh->FindBlendShape(name) : -1; }

float SkinnedMeshRenderer::GetBlendShapeWeight(int index) const
{
	return index >= 0 && index < (int)m_BlendWeights.size() ? m_BlendWeights[index] : 0.0f;
}

void SkinnedMeshRenderer::SetBlendShapeWeight(int index, float weight)
{
	if (index < 0 || index >= BlendShapeCount())
		return;
	if ((int)m_BlendWeights.size() < BlendShapeCount())
		m_BlendWeights.resize(BlendShapeCount(), 0.0f);
	if (m_BlendWeights[index] == weight)
		return;
	m_BlendWeights[index] = weight;
	m_MorphDirty = true;
}

// 가중치가 바뀐 프레임에만: 원래 정점 + Σ 가중치 × 차이 → 동적 정점 버퍼 (WRITE_DISCARD)
void SkinnedMeshRenderer::EnsureMorph()
{
	if (m_SimActive)   // 천: 시뮬레이션 정점이 BlendShape 보다 먼저
	{
		if (m_SimDirty)
		{
			m_SimDirty = false;
			m_MorphActive = UploadDynamicVertices(m_SimVerts);
			m_MorphDirty = true;   // 천을 끄면 BlendShape 정점을 다시 올린다
		}
		return;
	}
	if (!m_Mesh || m_Mesh->BlendShapes.empty() || m_Mesh->Vertices.empty())
	{
		m_MorphActive = false;
		return;
	}
	if (!m_MorphDirty)
		return;
	m_MorphDirty = false;
	const auto& shapes = m_Mesh->BlendShapes;
	if ((int)m_BlendWeights.size() < (int)shapes.size())
		m_BlendWeights.resize(shapes.size(), 0.0f);
	bool any = false;
	for (size_t k = 0; k < shapes.size(); ++k) any = any || fabsf(m_BlendWeights[k]) > 1e-4f;
	m_MorphActive = any;
	if (!any)
		return;
	m_MorphVerts = m_Mesh->Vertices;
	const size_t count = m_MorphVerts.size();
	for (size_t k = 0; k < shapes.size(); ++k)
	{
		const float w = m_BlendWeights[k] * 0.01f;
		if (fabsf(w) <= 1e-6f)
			continue;
		const BlendShapeData& sh = shapes[k];
		for (size_t j = 0; j < sh.Index.size(); ++j)
		{
			if (sh.Index[j] >= count) continue;
			auto& v = m_MorphVerts[sh.Index[j]];
			v.pos.x += sh.DPos[j].x * w; v.pos.y += sh.DPos[j].y * w; v.pos.z += sh.DPos[j].z * w;
			v.normal.x += sh.DNrm[j].x * w; v.normal.y += sh.DNrm[j].y * w; v.normal.z += sh.DNrm[j].z * w;
		}
	}
	m_MorphActive = UploadDynamicVertices(m_MorphVerts);
}

// 동적 정점 버퍼 (BlendShape · 천 함께 씀, WRITE_DISCARD). false = 실패 (원래 정점으로 그린다)
bool SkinnedMeshRenderer::UploadDynamicVertices(const vector<Vertex::PosNormalTexTanSkinned>& verts)
{
	const size_t count = verts.size();
	if (count == 0)
		return false;
	GfxContext* dc = Application::GetI()->GetDeviceContext();
	if (!m_MorphVB || m_MorphVBCount != (uint32)count)
	{
		D3D11_BUFFER_DESC bd = {};
		bd.Usage = D3D11_USAGE_DYNAMIC;
		bd.ByteWidth = (UINT)(count * sizeof(Vertex::PosNormalTexTanSkinned));
		bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		m_MorphVB.Reset();
		m_MorphVBCount = 0;
		if (FAILED(Application::GetI()->GetDevice()->CreateBuffer(&bd, nullptr, m_MorphVB.GetAddressOf())))
			return false;
		m_MorphVBCount = (uint32)count;
	}
	D3D11_MAPPED_SUBRESOURCE mapped;
	if (FAILED(dc->Map(m_MorphVB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
		return false;
	memcpy(mapped.pData, verts.data(), count * sizeof(Vertex::PosNormalTexTanSkinned));
	dc->Unmap(m_MorphVB.Get(), 0);
	return true;
}

void SkinnedMeshRenderer::DrawSubset(GfxContext* dc, int subset)
{
	// LOD 단계는 자기 정점 버퍼 (본 줄이기) — BlendShape 를 섞은 정점 버퍼와 맞지 않으니 그때는 원본
	MeshGeometry& geo = m_LodGeo != nullptr && !m_SimActive && !m_MorphActive ? *m_LodGeo : m_Mesh->ModelMesh;
	geo.Draw(dc, (uint32)subset, m_MorphActive ? m_MorphVB.Get() : nullptr);
}

bool SkinnedMeshRenderer::CanInstance() const
{
	if (m_Merged || !m_Enabled || !m_AutoLod || m_Mesh == nullptr || m_Mesh->Subsets.empty() || m_MorphActive || m_SimActive
		|| m_Mesh->BoneNames.empty() || m_Mesh->BoneNames.size() > kMaxBones)
		return false;
	// Alpha Clipping 재질도 된다 (깊이 · 그림자는 잘라내는 인스턴싱 기법). 패키지 셰이더는 그 패키지가 그린다
	const auto& mats = RenderMaterials();
	for (const auto& s : m_Mesh->Subsets)
	{
		const UMaterial* m = s.MaterialIndex < mats.size() ? mats[s.MaterialIndex].get() : nullptr;
		if (m && m->IsCustom())
			return false;
	}
	return true;
}

int SkinnedMeshRenderer::InstanceCheck() const
{
	if (!m_Block.Empty())
		return 2;
	return CanInstance() ? 1 : 0;   // 블록이 없으면 RenderMaterials 는 재질 목록을 그대로 (읽기만)
}

MeshGeometry* SkinnedMeshRenderer::PrepareLod(bool editor)
{
	SelectLod(editor);
	return CurrentGeometry();
}

MeshGeometry* SkinnedMeshRenderer::ShadowGeometry()
{
	// 그림자는 한 단계 거칠게 (Unity 의 LOD Bias 처럼 그림자는 작은 차이가 안 보인다)
	if (!m_AutoLod || m_SimActive || m_Mesh == nullptr)
		return CurrentGeometry();
	const int level = (std::min)(m_LodCurrent + 1, SkinnedLod::kLevels - 1);
	MeshGeometry* g = SkinnedLod::Get(m_Mesh, level, m_Skeleton);
	return g ? g : CurrentGeometry();
}

MeshGeometry* SkinnedMeshRenderer::CurrentGeometry()
{
	return m_LodGeo != nullptr && !m_SimActive ? m_LodGeo : &m_Mesh->ModelMesh;
}

// 자동 LOD: 경계 상자의 가장 긴 축이 화면 높이의 몇 % 인지 (Unity LOD Group 의 Screen Relative Height) 로 단계를 고른다
void SkinnedMeshRenderer::SelectLod(bool editor)
{
	if (!m_AutoLod || m_SimActive || m_Mesh == nullptr)
	{
		m_LodGeo = nullptr;
		m_LodCurrent = 0;
		return;
	}
	const int v = editor ? 1 : 0;
	const uint32_t frame = SceneCulling::FrameIndex();
	if (m_LodStamp[v] != frame)
	{
		m_LodStamp[v] = frame;
		// 크기 = 월드 상자의 가장 긴 축 (Unity LOD Group: 경계의 가장 큰 축 × 스케일).
		//  스켈레톤이 있으면 캐릭터 전체 (스켈레톤 범위) — 모듈형 캐릭터의 부위 (지갑 · 모자) 가 몸과 같은 단계를 고른다
		Vec3 center, mn, mx;
		float size = 0.0f;
		if (m_SkelSize > 0.0f)
		{
			Transform* tr = m_pGameObject->GetTransform();
			const XMMATRIX w = tr->GetWorldMatrix();
			center = Vec3(XMVector3TransformCoord(XMLoadFloat3(&m_SkelCenter), w));
			const Vec3 s = tr->GetScale();
			size = m_SkelSize * (std::max)({ std::abs(s.x), std::abs(s.y), std::abs(s.z) });
		}
		else if (CullTracked && SceneCulling::SlotBounds(CullSlot, mn, mx))
		{
			center = (mn + mx) * 0.5f;
			size = (std::max)({ mx.x - mn.x, mx.y - mn.y, mx.z - mn.z });
		}
		else
		{
			Transform* tr = m_pGameObject->GetTransform();
			center = tr->GetPosition();
			const Vec3 s = tr->GetScale();
			size = 2.0f * (std::max)({ m_BoundsExtent.x * std::abs(s.x), m_BoundsExtent.y * std::abs(s.y), m_BoundsExtent.z * std::abs(s.z) });
		}
		RenderManager* rm = RenderManager::GetI();
		const XMMATRIX& view = editor ? rm->EditorCameraViewMatrix : rm->CameraViewMatrix;
		const XMMATRIX& proj = editor ? rm->EditorCameraProjectionMatrix : rm->CameraProjectionMatrix;
		const float proj11 = XMVectorGetY(proj.r[1]);
		const bool ortho = XMVectorGetW(proj.r[2]) == 0.0f;
		// 카메라 공간 깊이 (뷰 행렬로 바로 — 역행렬 없이)
		const float depth = XMVectorGetZ(XMVector3TransformCoord(XMVectorSet(center.x, center.y, center.z, 1.0f), view));
		// 보이는 화면 높이 = 2 · 깊이 / proj11 (원근), 2 / proj11 (직교)
		const float h = ortho ? size * proj11 * 0.5f : size * proj11 * 0.5f / (std::max)(depth, 0.01f);
		m_LodLevel[v] = SkinnedLod::SelectLevel(h, m_LodLevel[v]);
		m_LodGeoView[v] = SkinnedLod::Get(m_Mesh, m_LodLevel[v], m_Skeleton);
	}
	m_LodGeo = m_LodGeoView[v];
	m_LodCurrent = m_LodGeo != nullptr ? m_LodLevel[v] : 0;
}

const vector<XMFLOAT4X4>& SkinnedMeshRenderer::MotionPalette()
{
	if (m_Mesh && !m_Mesh->Subsets.empty())
		EnsureBones();
	return m_FinalTransforms;
}

// 모션 벡터 패스: 깊이 프리패스와 같은 정점 (BlendShape · 천의 동적 버퍼 포함) 으로 서브셋마다
void SkinnedMeshRenderer::DrawForMotionVectors(GfxContext* dc, FxTechnique* tech)
{
	if (m_Merged || !m_Enabled || m_Mesh == nullptr || m_Mesh->Subsets.empty() || tech == nullptr)
		return;
	dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	for (int i = 0; i < (int)m_Mesh->Subsets.size(); ++i)
	{
		tech->GetPassByIndex(0)->Apply(0, dc);
		DrawSubset(dc, i);
	}
}

// ------------------------------------------------------------------ 그리기
// editor = true 면 Scene 뷰 카메라, false 면 게임 카메라
// Inspector 의 체크 상자 (C# Renderer.enabled) 가 꺼지면 본 · 그림자 · 노멀 깊이 모두 그리지 않는다
void SkinnedMeshRenderer::DrawSkinned(bool editor)
{
	if (!m_Enabled || m_Mesh == nullptr || m_Mesh->Subsets.empty() || m_CastShadows == 3)   // Shadows Only 는 본 패스에서 그리지 않음
		return;
	EnsureBones();
	SelectLod(editor);

	Transform* transform = m_pGameObject->GetTransform();
	auto deviceContext = Application::GetI()->GetDeviceContext();
	ComPtr<FxTechnique> tech = Effects::InstancedBasicFX->SkinnedTech;
	// Light.cullingMask: 이 물체의 레이어 (엔진 · 패키지 셰이더 모두), 끝나면 ~0
	const uint32 layerBit = 1u << (m_pGameObject->GetLayerIndex() & 31);
	RenderLayers::SetObjectLayer(Effects::InstancedBasicFX.get(), layerBit);
	CustomShaders::ForEachEffect([&](InstancedBasicEffect* fx) { RenderLayers::SetObjectLayer(fx, layerBit); });
	deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	deviceContext->IASetInputLayout(InputLayouts::PosNormalTexTanSkinned.Get());

	D3DX11_TECHNIQUE_DESC techDesc;
	tech->GetDesc(&techDesc);

	const XMMATRIX toTexSpace(
		0.5f, 0.0f, 0.0f, 0.0f,
		0.0f, -0.5f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.5f, 0.5f, 0.0f, 1.0f);
	const XMMATRIX viewProj = editor ? RenderManager::GetI()->EditorCameraViewProjectionMatrix : RenderManager::GetI()->CameraViewProjectionMatrix;

	for (uint32 p = 0; p < techDesc.Passes; ++p)
	{
		XMMATRIX world = transform->GetWorldMatrix();
		XMMATRIX worldInvTranspose = MathHelper::InverseTranspose(world);
		XMMATRIX worldViewProj = world * viewProj;

		Effects::InstancedBasicFX->SetWorld(world);
		Effects::InstancedBasicFX->SetWorldInvTranspose(worldInvTranspose);
		Effects::InstancedBasicFX->SetViewProj(viewProj);
		Effects::InstancedBasicFX->SetWorldViewProj(worldViewProj);
		Effects::InstancedBasicFX->SetWorldViewProjTex(worldViewProj * toTexSpace);
		Effects::InstancedBasicFX->SetTexTransform(XMMatrixScaling(1.0f, 1.0f, 1.0f));
		Effects::InstancedBasicFX->SetBoneTransforms(&m_FinalTransforms[0], (int)m_FinalTransforms.size());

		for (int i = 0; i < (int)m_Mesh->Subsets.size(); ++i)
		{
			const UINT matIndex = m_Mesh->Subsets[i].MaterialIndex;
			shared_ptr<UMaterial> material = matIndex < RenderMaterials().size() ? RenderMaterials()[matIndex] : nullptr;
			// 패키지 셰이더 (lilToon 등): 그 패키지가 그린다. 없으면 Fallback (Lit / Unlit) 으로 엔진이
			if (material && material->IsCustom())
				if (const CustomShaders::Shader* shader = CustomShaders::Find(material->CustomShader()); shader && shader->DrawSkinned)
				{
					CustomShaders::SkinnedDraw d = MakeCustomDraw(material.get(), world, viewProj, editor, i);
					shader->DrawSkinned(d);
					continue;
				}
			UMaterial::ApplyOrDefault(material, Effects::InstancedBasicFX.get());
			tech->GetPassByIndex(p)->Apply(0, deviceContext);
			DrawSubset(deviceContext, i);
		}
	}

	// 패키지 셰이더의 두 번째 패스 (툰 외곽선 등): 래스터 · 깊이 상태를 바꾸므로 끝나면 원래대로
	ComPtr<GfxRasterizerState> previousRS;
	ComPtr<GfxDepthStencilState> previousDSS;
	UINT previousRef = 0;
	bool saved = false;
	for (int i = 0; i < (int)m_Mesh->Subsets.size(); ++i)
	{
		const UINT matIndex = m_Mesh->Subsets[i].MaterialIndex;
		shared_ptr<UMaterial> material = matIndex < RenderMaterials().size() ? RenderMaterials()[matIndex] : nullptr;
		if (!material || !material->IsCustom())
			continue;
		const CustomShaders::Shader* shader = CustomShaders::Find(material->CustomShader());
		if (!shader || !shader->DrawSkinnedOutline || (shader->HasOutline && !shader->HasOutline(*material)))
			continue;
		if (!saved)
		{
			deviceContext->RSGetState(previousRS.GetAddressOf());
			deviceContext->OMGetDepthStencilState(previousDSS.GetAddressOf(), &previousRef);
			saved = true;
		}
		const XMMATRIX world = transform->GetWorldMatrix();
		CustomShaders::SkinnedDraw d = MakeCustomDraw(material.get(), world, viewProj, editor, i);
		shader->DrawSkinnedOutline(d);
	}
	if (saved)
	{
		deviceContext->RSSetState(previousRS.Get());
		deviceContext->OMSetDepthStencilState(previousDSS.Get(), previousRef);
	}
	RenderLayers::SetObjectLayer(Effects::InstancedBasicFX.get(), ~0u);
	CustomShaders::ForEachEffect([&](InstancedBasicEffect* fx) { RenderLayers::SetObjectLayer(fx, ~0u); });
}

CustomShaders::SkinnedDraw SkinnedMeshRenderer::MakeCustomDraw(UMaterial* material, FXMMATRIX world, CXMMATRIX viewProj, bool editor, int subset)
{
	CustomShaders::SkinnedDraw d;
	d.Context = Application::GetI()->GetDeviceContext();
	d.Material = material;
	d.World = world;
	d.ViewProj = viewProj;
	d.Bones = m_FinalTransforms.data();
	d.BoneCount = (int)m_FinalTransforms.size();
	d.Editor = editor;
	d.Draw = [this, subset]() {
		auto ctx = Application::GetI()->GetDeviceContext();
		ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		ctx->IASetInputLayout(InputLayouts::PosNormalTexTanSkinned.Get());
		DrawSubset(ctx, subset);
	};
	return d;
}

bool SkinnedMeshRenderer::DrawCustomClip(int subset, CustomShaders::DrawPass pass, FXMMATRIX world, CXMMATRIX viewProj, CXMMATRIX view, bool editor)
{
	const UINT matIndex = m_Mesh->Subsets[subset].MaterialIndex;
	UMaterial* material = matIndex < RenderMaterials().size() ? RenderMaterials()[matIndex].get() : nullptr;
	if (!material || !material->IsCustom())
		return false;
	const CustomShaders::Shader* shader = CustomShaders::Find(material->CustomShader());
	if (!shader || !shader->DrawSkinned || !shader->CustomDepth || !shader->CustomDepth(*material))
		return false;
	CustomShaders::SkinnedDraw d = MakeCustomDraw(material, world, viewProj, editor, subset);
	d.Pass = pass;
	d.View = view;
	shader->DrawSkinned(d);
	return true;
}

void SkinnedMeshRenderer::Render()
{
	if (m_Merged)
		return;
	RenderStats::AddSkinnedMesh();
	// 오클루전 컬링: 깊이 프리패스 뒤 상자 쿼리가 있으면 그 결과로 (가려졌으면 GPU 가 건너뛴다)
	GfxContext* dc = Application::GetI()->GetDeviceContext();
	const bool predicated = OcclusionCulling::BeginPredicated(dc, this);
	DrawSkinned(false);
	if (predicated)
		OcclusionCulling::EndPredicated(dc);
}

void SkinnedMeshRenderer::_Editor_Render()
{
	if (m_Merged)
		return;
	GfxContext* dc = Application::GetI()->GetDeviceContext();
	const bool predicated = OcclusionCulling::BeginPredicated(dc, this);
	DrawSkinned(true);
	if (predicated)
		OcclusionCulling::EndPredicated(dc);
}

void SkinnedMeshRenderer::RenderShadow()
{
	if (m_Merged || !m_Enabled || m_Mesh == nullptr || m_Mesh->Subsets.empty() || m_CastShadows == 1)   // Cast Shadows Off
		return;
	RenderStats::AddShadowCaster();
	EnsureBones();

	Transform* transform = m_pGameObject->GetTransform();
	auto deviceContext = Application::GetI()->GetDeviceContext();
	ComPtr<FxTechnique> tech = Effects::BuildShadowMapFX->BuildShadowMapSkinnedTech;
	deviceContext->IASetInputLayout(InputLayouts::PosNormalTexTanSkinned.Get());

	D3DX11_TECHNIQUE_DESC techDesc;
	tech->GetDesc(&techDesc);
	for (uint32 p = 0; p < techDesc.Passes; ++p)
	{
		XMMATRIX world = transform->GetWorldMatrix();
		Effects::BuildShadowMapFX->SetWorld(world);
		Effects::BuildShadowMapFX->SetWorldInvTranspose(MathHelper::InverseTranspose(world));
		Effects::BuildShadowMapFX->SetWorldViewProj(world * RenderManager::GetI()->LightViewProjection);
		Effects::BuildShadowMapFX->SetTexTransform(XMMatrixScaling(1.0f, 1.0f, 1.0f));
		Effects::BuildShadowMapFX->SetBoneTransforms(&m_FinalTransforms[0], (int)m_FinalTransforms.size());
		for (int i = 0; i < (int)m_Mesh->Subsets.size(); ++i)
		{
			if (DrawCustomClip(i, CustomShaders::DrawPass::Shadow, world, RenderManager::GetI()->LightViewProjection, XMMatrixIdentity(), false))
				continue;
			// Alpha Clipping 재질: 투명한 곳은 그림자도 지지 않는다 (VRoid 옷은 몸 전체 껍질 + 잘라내기 마스크)
			float cutoff = 0.0f;
			if (UMaterial* m = ClipMaterial(i, cutoff))
			{
				Effects::BuildShadowMapFX->SetDiffuseMap(m->GetBaseMapSRV());
				Effects::BuildShadowMapFX->SetAlphaCutoff(cutoff);
				Effects::BuildShadowMapFX->SetTexTransform(ClipTexTransform(*m));
				Effects::BuildShadowMapFX->BuildShadowMapAlphaClipSkinnedTech->GetPassByIndex(p)->Apply(0, deviceContext);
				DrawSubset(deviceContext, i);
				Effects::BuildShadowMapFX->SetTexTransform(XMMatrixScaling(1.0f, 1.0f, 1.0f));
				Effects::BuildShadowMapFX->SetAlphaCutoff(0.0f);   // 0 = 예전 고정값 (다른 렌더러용)
				continue;
			}
			tech->GetPassByIndex(p)->Apply(0, deviceContext);
			DrawSubset(deviceContext, i);
		}
	}
}

// Alpha Clipping 을 쓰는 서브셋의 재질 (기본 그림이 있을 때만). cutoff = 그림 알파와 비교할 값 (본 패스는 그림 × BaseColor 알파)
UMaterial* SkinnedMeshRenderer::ClipMaterial(int subset, float& cutoff) const
{
	const UINT matIndex = m_Mesh->Subsets[subset].MaterialIndex;
	UMaterial* m = matIndex < RenderMaterials().size() ? RenderMaterials()[matIndex].get() : nullptr;
	if (!m || !m->GetPbr().AlphaClip || !m->GetBaseMapSRV())
		return nullptr;
	cutoff = m->GetPbr().Cutoff / (std::max)(m->GetPbr().BaseColor.w, 1e-4f);
	return m;
}

XMMATRIX SkinnedMeshRenderer::ClipTexTransform(const UMaterial& m)
{
	const PbrMaterial& pbr = m.GetPbr();
	return XMMatrixScaling(pbr.Tiling.x, pbr.Tiling.y, 1.0f) * XMMatrixTranslation(pbr.Offset.x, pbr.Offset.y, 0.0f);
}

// 게임/에디터 공통 SSAO 노멀·깊이 패스 (= 깊이 사전 패스: 본 패스가 EQUAL 로 그린다)
void SkinnedMeshRenderer::DrawSkinnedNormalDepth(bool editor)
{
	SkinnedMesh* mesh = m_Mesh.get();
	SelectLod(editor);
	Transform* transform = m_pGameObject->GetTransform();
	const vector<XMFLOAT4X4>& bones = m_FinalTransforms;
	auto deviceContext = Application::GetI()->GetDeviceContext();
	ComPtr<FxTechnique> tech = Effects::SsaoNormalDepthFX->NormalDepthSkinnedTech;
	deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	deviceContext->IASetInputLayout(InputLayouts::PosNormalTexTanSkinned.Get());

	const XMMATRIX view = editor ? RenderManager::GetI()->EditorCameraViewMatrix : RenderManager::GetI()->CameraViewMatrix;
	const XMMATRIX viewProj = editor ? RenderManager::GetI()->EditorCameraViewProjectionMatrix : RenderManager::GetI()->CameraViewProjectionMatrix;

	D3DX11_TECHNIQUE_DESC techDesc;
	tech->GetDesc(&techDesc);
	for (uint32 p = 0; p < techDesc.Passes; ++p)
	{
		XMMATRIX world = transform->GetWorldMatrix();
		XMMATRIX worldInvTranspose = MathHelper::InverseTranspose(world);
		Effects::SsaoNormalDepthFX->SetWorldView(world * view);
		Effects::SsaoNormalDepthFX->SetWorldInvTransposeView(worldInvTranspose * view);
		Effects::SsaoNormalDepthFX->SetWorldViewProj(world * viewProj);
		Effects::SsaoNormalDepthFX->SetTexTransform(XMMatrixScaling(1.0f, 1.0f, 1.0f));
		Effects::SsaoNormalDepthFX->SetBoneTransforms(&bones[0], (int)bones.size());
		for (int i = 0; i < (int)mesh->Subsets.size(); ++i)
		{
			if (DrawCustomClip(i, CustomShaders::DrawPass::NormalDepth, world, viewProj, view, editor))
				continue;
			// Alpha Clipping 재질은 여기서도 같은 기준으로 잘라낸다 — 안 그러면 투명한 곳에 깊이만 남아 뒤가 지워진다 (카메라 배경색 구멍)
			float cutoff = 0.0f;
			if (UMaterial* m = ClipMaterial(i, cutoff))
			{
				Effects::SsaoNormalDepthFX->SetDiffuseMap(m->GetBaseMapSRV());
				Effects::SsaoNormalDepthFX->SetAlphaCutoff(cutoff);
				Effects::SsaoNormalDepthFX->SetTexTransform(ClipTexTransform(*m));
				Effects::SsaoNormalDepthFX->NormalDepthAlphaClipSkinnedTech->GetPassByIndex(p)->Apply(0, deviceContext);
				DrawSubset(deviceContext, i);
				Effects::SsaoNormalDepthFX->SetTexTransform(XMMatrixScaling(1.0f, 1.0f, 1.0f));
				Effects::SsaoNormalDepthFX->SetAlphaCutoff(0.0f);
				continue;
			}
			tech->GetPassByIndex(p)->Apply(0, deviceContext);
			DrawSubset(deviceContext, i);
		}
	}
}

void SkinnedMeshRenderer::RenderShadowNormal()
{
	if (m_Merged || !m_Enabled || m_Mesh == nullptr || m_Mesh->Subsets.empty())
		return;
	EnsureBones();
	DrawSkinnedNormalDepth(false);
}

void SkinnedMeshRenderer::_Editor_RenderShadowNormal()
{
	if (m_Merged || !m_Enabled || m_Mesh == nullptr || m_Mesh->Subsets.empty())
		return;
	EnsureBones();
	DrawSkinnedNormalDepth(true);
}

// ------------------------------------------------------------------ 부위 합치기 (모듈형 캐릭터)
namespace
{
	struct CombinedParts
	{
		shared_ptr<SkinnedMesh> Mesh;
		vector<shared_ptr<UMaterial>> Materials;
		Vec3 Min, Max;
	};
	std::unordered_map<std::string, std::weak_ptr<CombinedParts>> s_Combined;   // 부위 · 재질 조합 → 합친 메시 (쓰는 대표가 없으면 놓는다)
	constexpr size_t kMaxPaletteBones = 255;   // 정점 본 번호는 BYTE (256 번째 = 천 단위 본 자리)
}

void SkinnedMeshRenderer::Start()
{
	// Play 에서 한 번 (Unity 에는 없음 — NOVA 군중: 부위 15 ~ 20 개 캐릭터를 렌더러 하나로)
	if (Application::IsPlaying() && m_AutoLod)
		TryCombine();
}

void SkinnedMeshRenderer::TryCombine()
{
	if (m_CombineTried || m_pGameObject == nullptr || m_Mesh == nullptr || m_Skeleton == nullptr)
		return;
	m_CombineTried = true;
	GameObject* parent = m_pGameObject->GetParent();
	if (parent == nullptr)
		return;
	// 형제 부위: 부모의 직계 자식 중 켜져 있고 같은 스켈레톤 · Auto LOD · BlendShape 가중치 없음 · 천 없음
	std::vector<SkinnedMeshRenderer*> parts;
	for (GameObject* c : parent->Children())
	{
		if (c == nullptr || !c->IsActiveInHierarchy())
			continue;
		SkinnedMeshRenderer* s = c->GetComponent<SkinnedMeshRenderer>();
		if (s == nullptr || !s->m_Enabled || s->m_Merged || s->m_Combined || !s->m_AutoLod || s->m_Mesh == nullptr || s->m_Skeleton != m_Skeleton
			|| s->m_SimActive || s->m_MorphActive || s->m_FinalTransforms.size() > kMaxBones)
			continue;
		bool weights = false;
		for (float w : s->m_BlendWeights) weights = weights || w != 0.0f;
		if (weights)
			continue;
		parts.push_back(s);
	}
	if (parts.size() < 2 || parts.front() != this)
		return;   // 대표 = 첫 부위 (다른 부위는 대표의 Start 가 맡는다)
	for (SkinnedMeshRenderer* p : parts)
		p->m_CombineTried = true;

	// 열쇠: 스켈레톤 + 부위마다 (메시, 메시 바인드 후보, 재질)
	std::string key = std::to_string((uintptr_t)m_Skeleton.get());
	for (SkinnedMeshRenderer* p : parts)
	{
		key += "|" + std::to_string((uintptr_t)p->m_Mesh.get()) + ":" + std::to_string(p->GetBindMode());
		for (const auto& m : p->RenderMaterials())
			key += "," + std::to_string((uintptr_t)m.get());
	}
	shared_ptr<CombinedParts> combined = s_Combined[key].lock();
	if (!combined)
	{
		combined = std::make_shared<CombinedParts>();
		auto mesh = std::make_shared<SkinnedMesh>();
		// 팔레트: (노드, 메시 바인드 · 역바인드) 가 같으면 한 칸 — 메시 바인드를 미리 곱해 합친 메시의 메시 바인드는 단위 행렬
		struct Bone { int Node; XMFLOAT4X4 Off; };
		std::vector<Bone> bones;
		Vec3 mn(FLT_MAX, FLT_MAX, FLT_MAX), mx(-FLT_MAX, -FLT_MAX, -FLT_MAX);
		bool ok = true;
		for (SkinnedMeshRenderer* p : parts)
		{
			const SkinnedMesh& m = *p->m_Mesh;
			const XMMATRIX mb = XMLoadFloat4x4(&p->m_MeshBind);
			std::vector<int> remap(m.BoneNames.size(), 0);
			for (size_t k = 0; k < m.BoneNames.size() && ok; ++k)
			{
				const int node = k < p->m_PaletteNode.size() ? p->m_PaletteNode[k] : -1;
				XMFLOAT4X4 off;
				XMStoreFloat4x4(&off, mb * XMLoadFloat4x4(&m.BoneOffsets[k]));
				int found = -1;
				for (size_t b = 0; b < bones.size() && found < 0; ++b)
				{
					if (bones[b].Node != node)
						continue;
					float d = 0.0f;
					for (int i = 0; i < 16; ++i)
						d += fabsf((&bones[b].Off._11)[i] - (&off._11)[i]);
					if (d < 1e-4f)
						found = (int)b;
				}
				if (found < 0)
				{
					found = (int)bones.size();
					bones.push_back({ node, off });
				}
				remap[k] = found;
				ok = bones.size() <= kMaxPaletteBones;
			}
			if (!ok)
				break;
			const uint32 vertexBase = (uint32)mesh->Vertices.size();
			for (const auto& v0 : m.Vertices)
			{
				auto v = v0;
				for (int j = 0; j < 4; ++j)
					v.boneIndices[j] = (BYTE)(v0.boneIndices[j] < remap.size() ? remap[v0.boneIndices[j]] : 0);
				mesh->Vertices.push_back(v);
			}
			const auto& mats = p->RenderMaterials();
			for (const auto& s : m.Subsets)
			{
				MeshGeometry::Subset ns = s;
				ns.VertexStart = vertexBase + s.VertexStart;
				ns.FaceStart = (uint32)(mesh->Indices.size() / 3);
				ns.MaterialIndex = (uint32)combined->Materials.size();
				ns.Id = (uint32)mesh->Subsets.size();
				const size_t first = (size_t)s.FaceStart * 3, count = (size_t)s.FaceCount * 3;
				if (first + count <= m.Indices.size())
					mesh->Indices.insert(mesh->Indices.end(), m.Indices.begin() + first, m.Indices.begin() + first + count);   // 서브셋의 VertexStart 기준 그대로
				else
					ns.FaceCount = 0;
				mesh->Subsets.push_back(ns);
				combined->Materials.push_back(s.MaterialIndex < mats.size() && mats[s.MaterialIndex] ? mats[s.MaterialIndex] : UMaterial::GetDefault());
			}
			mn = Vec3::Min(mn, p->m_BoundsCenter - p->m_BoundsExtent);
			mx = Vec3::Max(mx, p->m_BoundsCenter + p->m_BoundsExtent);
		}
		if (!ok || mesh->Vertices.empty() || mesh->Indices.empty())
		{
			EditorLog::Write("Crowd", "combine skipped (%zu parts, %zu palette bones)", parts.size(), bones.size());
			return;
		}
		for (const Bone& b : bones)
		{
			mesh->BoneNames.push_back(b.Node >= 0 && b.Node < (int)m_Skeleton->NodeNames.size() ? m_Skeleton->NodeNames[(size_t)b.Node] : std::string());
			mesh->BoneOffsets.push_back(b.Off);
		}
		mesh->Name = "Combined (" + std::to_string(parts.size()) + " parts)";
		mesh->Path = m_Mesh->Path;
		mesh->Setup();
		combined->Mesh = mesh;
		combined->Min = mn;
		combined->Max = mx;
		s_Combined[key] = combined;
		EditorLog::Write("Crowd", "combined %zu parts: %zu vertices, %zu subsets, %zu palette bones", parts.size(), mesh->Vertices.size(), mesh->Subsets.size(), bones.size());
	}

	// 대표: 합친 메시로 (저장하는 경로 · 재질 경로는 그대로 — Play 에서만)
	m_OwnMesh = m_Mesh;
	m_OwnMaterials = m_pMaterials;
	m_Mesh = combined->Mesh;
	m_pMaterials = combined->Materials;
	m_Combined = true;
	m_CombinedRef = combined;
	RebuildPalette();
	m_BoundsCenter = (combined->Min + combined->Max) * 0.5f;
	m_BoundsExtent = (combined->Max - combined->Min) * 0.5f;
	ResetLod();
	ResetToBindPose();
	for (SkinnedMeshRenderer* p : parts)
		if (p != this)
		{
			p->m_Merged = true;
			++s_MergeSerial;
			SceneCulling::UnregisterRenderer(p);   // 컬링 · 그리기 · 팔레트 · 모션 벡터 루프에서 빠진다 (1 만 명 × 부위 15 개)
		}
}

// ------------------------------------------------------------------ Inspector (Unity 6)
void SkinnedMeshRenderer::OnInspectorGUI()
{
	using namespace UnityGUI;
	static const char* kQuality[] = { "Auto", "1 Bone", "2 Bones", "4 Bones" };
	static const char* kCast[] = { "On", "Off", "Two Sided", "Shadows Only" };
	static const char* kLayerMask[] = { "Default", "Nothing", "Everything" };
	static const char* kMask[] = { "None", "Visible Inside Mask", "Visible Outside Mask" };

	IconButtonRow("", "edit_collider");
	Label("Bounds");
	UnityGUI::Vector3("Center", &m_BoundsCenter.x, false, 1);
	UnityGUI::Vector3("Extent", &m_BoundsExtent.x, false, 1);

	std::string meshName = m_Mesh ? (m_Mesh->Name.empty() ? std::filesystem::path(m_MeshPath).stem().string() : m_Mesh->Name) : "None (Mesh)";
	ObjectField("Mesh", meshName.c_str(), 0, m_Mesh ? "mesh_small" : nullptr);
	Dropdown("Quality", &m_Quality, kQuality, 4);
	Toggle("Update When Offscreen", &m_UpdateWhenOffscreen);
	std::string root = m_RootBone.empty() ? "None (Transform)" : m_RootBone;
	ObjectField("Root Bone", root.c_str(), 0, m_RootBone.empty() ? nullptr : "transform");

	// ---- BlendShapes (Unity: 셰이프마다 0..100 슬라이더)
	if (BlendShapeCount() > 0 && FoldoutPlain("BlendShapes", 0, false))
	{
		if ((int)m_BlendWeights.size() < BlendShapeCount())
			m_BlendWeights.resize(BlendShapeCount(), 0.0f);
		for (int i = 0; i < BlendShapeCount(); ++i)
		{
			ImGui::PushID(i);
			float w = m_BlendWeights[i];
			if (UnityGUI::Slider(m_Mesh->BlendShapes[i].Name.c_str(), &w, 0.0f, 100.0f, 1))
				SetBlendShapeWeight(i, w);
			ImGui::PopID();
		}
	}

	// ---- Materials ----
	int count = (int)m_pMaterials.size();
	bool open = MaterialsHeader("Materials", &count);
	if (count != (int)m_pMaterials.size())
	{
		while ((int)m_pMaterials.size() < count) { m_pMaterials.push_back(UMaterial::GetDefault()); m_MaterialPaths.push_back(L"builtin:Default-Material"); }
		while ((int)m_pMaterials.size() > count) { m_pMaterials.pop_back(); m_MaterialPaths.pop_back(); }
	}
	if (open)
	{
		m_MaterialPaths.resize(m_pMaterials.size());
		for (int i = 0; i < (int)m_pMaterials.size(); ++i)
		{
			// ⊙ = Select Material 창, Project 의 .mat 끌어 놓기
			std::string label = "Element " + std::to_string(i);
			std::string key = "mat:" + std::to_string((uintptr_t)this) + ":" + std::to_string(i);
			ImGui::PushID(i);
			MaterialInspector::MaterialSlot(label.c_str(), key, m_pMaterials[i], m_MaterialPaths[i]);
			ImGui::PopID();
		}
		bool plus = false, minus = false;
		PlusMinus(&plus, &minus);
		if (plus) { m_pMaterials.push_back(UMaterial::GetDefault()); m_MaterialPaths.push_back(L"builtin:Default-Material"); }
		if (minus && !m_pMaterials.empty()) { m_pMaterials.pop_back(); m_MaterialPaths.pop_back(); }
	}

	if (FoldoutPlain("Lighting"))
	{
		Dropdown("Cast Shadows", &m_CastShadows, kCast, 4, 0);
		Toggle("Static Shadow Caster", &m_StaticShadowCaster);
	}
	if (FoldoutPlain("Probes"))
	{
		// 확산 간접광은 Adaptive Probe Volume 만 (레거시 Light Probe Group · Proxy Volume 없음)
		ValueLabel("Light Probes", "Adaptive Probe Volume");
	}
	if (FoldoutPlain("Additional Settings"))
	{
		static const char* kMotion[] = { "Camera Motion", "Per Object Motion", "Force No Motion" };
		Dropdown("Motion Vectors", &m_MotionVectors, kMotion, 3, 0);
		Toggle("Skinned Motion Vectors", &m_SkinnedMotionVectors);
		Toggle("Dynamic Occlusion", &m_DynamicOcclusion);
		Dropdown("Rendering Layer Mask", &m_RenderingLayerMask, kLayerMask, 3, 0);
		// NOVA: 멀리서 삼각형을 줄인다 (Unity 는 LOD Group 에 메시를 따로 둔다 — 군중용 자동 단순화)
		bool autoLod = m_AutoLod;
		if (Toggle("Auto LOD", &autoLod))
			SetAutoLod(autoLod);
		if (m_AutoLod)
			ValueLabel("Current LOD", std::to_string(m_LodCurrent).c_str());
	}
	if (FoldoutPlain("2D"))
		Dropdown("Mask Interaction", &m_MaskInteraction, kMask, 3, 0);
}

void SkinnedMeshRenderer::DrawMaterialInspectors()
{
	std::vector<UMaterial*> shown;
	for (auto& material : m_pMaterials)
	{
		if (material == nullptr || std::find(shown.begin(), shown.end(), material.get()) != shown.end())
			continue;
		shown.push_back(material.get());
		MaterialInspector::WatchUndo(material);
		material->OnInspectorGUI(true);
	}
}

GENERATE_COMPONENT_FUNC_TOJSON(SkinnedMeshRenderer)
{
	json j;
	SERIALIZE_TYPE(j, SkinnedMeshRenderer);
	SERIALIZE_INT(j, m_MeshSubsetIndex, "subsetIndex");
	SERIALIZE_WSTRING(j, m_MeshPath, "meshPath");
	SERIALIZE_WSTRING_ARRAY(j, m_MaterialPaths, "m_MaterialPaths");
	SERIALIZE_WSTRING(j, m_ShaderPath, "shaderPath");
	j["enabled"] = m_Enabled;
	j["boundsCenter"] = { m_BoundsCenter.x, m_BoundsCenter.y, m_BoundsCenter.z };
	j["boundsExtent"] = { m_BoundsExtent.x, m_BoundsExtent.y, m_BoundsExtent.z };
	j["quality"] = m_Quality;
	j["updateWhenOffscreen"] = m_UpdateWhenOffscreen;
	j["castShadows"] = m_CastShadows;
	if (m_SortingLayerId != 0) j["sortingLayerID"] = m_SortingLayerId;
	if (m_SortingOrder != 0) j["sortingOrder"] = m_SortingOrder;
	j["staticShadowCaster"] = m_StaticShadowCaster;
	j["lightProbes"] = m_LightProbes;
	j["skinnedMotionVectors"] = m_SkinnedMotionVectors;
	j["motionVectors"] = m_MotionVectors;
	j["dynamicOcclusion"] = m_DynamicOcclusion;
	j["renderingLayerMask"] = m_RenderingLayerMask;
	j["maskInteraction"] = m_MaskInteraction;
	if (!m_AutoLod) j["autoLod"] = false;   // 기본 켬 — 끈 것만 저장
	if (m_BindMode >= 0) j["bindMode"] = m_BindMode;
	bool anyWeight = false;
	for (float w : m_BlendWeights) anyWeight = anyWeight || w != 0.0f;
	if (anyWeight) j["blendShapeWeights"] = m_BlendWeights;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(SkinnedMeshRenderer)
{
	DE_SERIALIZE_WSTRING(j, m_ShaderPath, "shaderPath");
	DE_SERIALIZE_INT(j, m_MeshSubsetIndex, "subsetIndex");
	wstring meshPath;
	DE_SERIALIZE_WSTRING(j, meshPath, "meshPath");

	m_pMaterials.clear();
	m_MaterialPaths.clear();
	vector<wstring> materialPaths;
	DE_SERIALIZE_WSTRING_ARRAY(j, materialPaths, "m_MaterialPaths");
	for (const wstring& path : materialPaths)
	{
		// 빈 경로 = None (그릴 때는 기본 재질), 읽지 못한 재질은 기본 재질로
		shared_ptr<UMaterial> material = path.empty() ? nullptr : ResourceManager::GetI()->LoadMaterial(wstring_to_string(path));
		if (path.empty())
		{
			m_pMaterials.push_back(nullptr);
			m_MaterialPaths.push_back(L"");
			continue;
		}
		m_pMaterials.push_back(material ? material : UMaterial::GetDefault());
		m_MaterialPaths.push_back(material ? path : wstring(L"builtin:Default-Material"));
	}

	m_Enabled = j.value("enabled", true);
	m_Quality = j.value("quality", 0);
	m_UpdateWhenOffscreen = j.value("updateWhenOffscreen", false);
	m_CastShadows = j.value("castShadows", 0);
	m_SortingLayerId = j.value("sortingLayerID", 0);
	m_SortingOrder = j.value("sortingOrder", 0);
	m_StaticShadowCaster = j.value("staticShadowCaster", false);
	m_LightProbes = j.value("lightProbes", 1);
	m_SkinnedMotionVectors = j.value("skinnedMotionVectors", true);
	m_MotionVectors = j.value("motionVectors", 1);
	m_DynamicOcclusion = j.value("dynamicOcclusion", true);
	m_RenderingLayerMask = j.value("renderingLayerMask", 0);
	m_MaskInteraction = j.value("maskInteraction", 0);
	m_AutoLod = j.value("autoLod", true);
	m_BindMode = j.value("bindMode", -1);
	m_BlendWeights = j.value("blendShapeWeights", std::vector<float>());
	m_MorphDirty = true;

	// 메시 + 스켈레톤 (재질 수가 부족하면 기본 재질로 채움)
	SetSkinnedMesh(meshPath, m_MeshSubsetIndex);
}
