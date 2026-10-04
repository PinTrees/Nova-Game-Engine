#include "pch.h"
#include "RenderLayers.h"
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

namespace
{
	constexpr size_t kMaxBones = 256;   // 셰이더 gBoneTransforms 크기
}

SkinnedMeshRenderer::SkinnedMeshRenderer()
{
	XMStoreFloat4x4(&m_MeshBind, XMMatrixIdentity());
	m_InspectorTitleName = "Skinned Mesh Renderer";
	m_InspectorIconPath = L"skinned_mesh_renderer.png";
}

SkinnedMeshRenderer::~SkinnedMeshRenderer()
{
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

void SkinnedMeshRenderer::SetSkinnedMesh(const wstring& path, int index)
{
	m_MeshPath = path;
	m_MeshSubsetIndex = index;
	m_Mesh = path.empty() ? nullptr : ResourceManager::GetI()->LoadSkinnedMesh(path, index);
	m_Skeleton = nullptr;
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

	// FBX 의 스킨 역바인드(Offset)는 장면 공간 기준이라, 메시 노드에 PreRotation(예: Z-up → Y-up) 이 있으면
	// 정점을 먼저 메시 노드의 바인드 전역 변환으로 옮겨야 한다: 최종 = MeshBind * Offset * BoneGlobal
	XMMATRIX meshBind = XMMatrixIdentity();
	for (int node = m_Skeleton->FindNode(m_Mesh->Name); node >= 0; node = m_Skeleton->BoneHierarchy[node])
		meshBind = meshBind * XMLoadFloat4x4(&m_Skeleton->BindLocal[node]);
	XMStoreFloat4x4(&m_MeshBind, meshBind);

	// 바운드: 바인드 포즈 메시 AABB (메시 노드 변환 + 단위 변환 반영)
	const XMMATRIX toModel = meshBind * XMMatrixScaling(m_Skeleton->UnitScale, m_Skeleton->UnitScale, m_Skeleton->UnitScale);
	Vec3 mn(FLT_MAX, FLT_MAX, FLT_MAX), mx(-FLT_MAX, -FLT_MAX, -FLT_MAX);
	for (const auto& v : m_Mesh->Vertices)
	{
		Vec3 p = XMVector3TransformCoord(XMLoadFloat3(&v.pos), toModel);
		mn = Vec3::Min(mn, p);
		mx = Vec3::Max(mx, p);
	}
	if (!m_Mesh->Vertices.empty())
	{
		m_BoundsCenter = (mn + mx) * 0.5f;
		m_BoundsExtent = (mx - mn) * 0.5f;
		for (float* v : { &m_BoundsCenter.x, &m_BoundsCenter.y, &m_BoundsCenter.z })
			if (fabsf(*v) < 1e-5f) *v = 0.0f;   // 부동소수점 잡음 (8.9e-08 같은 값) 정리
	}
}

void SkinnedMeshRenderer::ApplyPose(const vector<XMFLOAT4X4>& nodeGlobals)
{
	if (m_Mesh == nullptr)
		return;
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
}

void SkinnedMeshRenderer::ResetToBindPose()
{
	if (m_Mesh == nullptr)
		return;
	if (m_Skeleton == nullptr)
	{
		m_FinalTransforms.assign((std::max)(m_Mesh->BoneNames.size(), (size_t)1), XMFLOAT4X4());
		for (auto& m : m_FinalTransforms) XMStoreFloat4x4(&m, XMMatrixIdentity());
		return;
	}
	vector<XMFLOAT4X4> local, global;
	AnimationPose::SampleLocal(*m_Skeleton, nullptr, {}, 0.0f, local);
	AnimationPose::ComputeGlobals(*m_Skeleton, local, global);
	ApplyPose(global);
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
	GfxContext* dc = Application::GetI()->GetDeviceContext();
	if (!m_MorphVB || m_MorphVBCount != (uint32)count)
	{
		D3D11_BUFFER_DESC bd = {};
		bd.Usage = D3D11_USAGE_DYNAMIC;
		bd.ByteWidth = (UINT)(count * sizeof(Vertex::PosNormalTexTanSkinned));
		bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		m_MorphVB.Reset();
		if (FAILED(Application::GetI()->GetDevice()->CreateBuffer(&bd, nullptr, m_MorphVB.GetAddressOf())))
		{
			m_MorphActive = false;
			return;
		}
		m_MorphVBCount = (uint32)count;
	}
	D3D11_MAPPED_SUBRESOURCE mapped;
	if (FAILED(dc->Map(m_MorphVB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
	{
		m_MorphActive = false;
		return;
	}
	memcpy(mapped.pData, m_MorphVerts.data(), count * sizeof(Vertex::PosNormalTexTanSkinned));
	dc->Unmap(m_MorphVB.Get(), 0);
}

void SkinnedMeshRenderer::DrawSubset(GfxContext* dc, int subset)
{
	m_Mesh->ModelMesh.Draw(dc, (uint32)subset, m_MorphActive ? m_MorphVB.Get() : nullptr);
}

// ------------------------------------------------------------------ 그리기
// editor = true 면 Scene 뷰 카메라, false 면 게임 카메라
void SkinnedMeshRenderer::DrawSkinned(bool editor)
{
	if (m_Mesh == nullptr || m_Mesh->Subsets.empty() || m_CastShadows == 3)   // Shadows Only 는 본 패스에서 그리지 않음
		return;
	EnsureBones();

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
			shared_ptr<UMaterial> material = matIndex < m_pMaterials.size() ? m_pMaterials[matIndex] : nullptr;
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
		shared_ptr<UMaterial> material = matIndex < m_pMaterials.size() ? m_pMaterials[matIndex] : nullptr;
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
	UMaterial* material = matIndex < m_pMaterials.size() ? m_pMaterials[matIndex].get() : nullptr;
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
	GfxContext* dc = Application::GetI()->GetDeviceContext();
	const bool predicated = OcclusionCulling::BeginPredicated(dc, this);
	DrawSkinned(true);
	if (predicated)
		OcclusionCulling::EndPredicated(dc);
}

void SkinnedMeshRenderer::RenderShadow()
{
	if (m_Mesh == nullptr || m_Mesh->Subsets.empty() || m_CastShadows == 1)   // Cast Shadows Off
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
	UMaterial* m = matIndex < m_pMaterials.size() ? m_pMaterials[matIndex].get() : nullptr;
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
	if (m_Mesh == nullptr || m_Mesh->Subsets.empty())
		return;
	EnsureBones();
	DrawSkinnedNormalDepth(false);
}

void SkinnedMeshRenderer::_Editor_RenderShadowNormal()
{
	if (m_Mesh == nullptr || m_Mesh->Subsets.empty())
		return;
	EnsureBones();
	DrawSkinnedNormalDepth(true);
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
		Toggle("Skinned Motion Vectors", &m_SkinnedMotionVectors);
		Toggle("Dynamic Occlusion", &m_DynamicOcclusion);
		Dropdown("Rendering Layer Mask", &m_RenderingLayerMask, kLayerMask, 3, 0);
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
	j["staticShadowCaster"] = m_StaticShadowCaster;
	j["lightProbes"] = m_LightProbes;
	j["skinnedMotionVectors"] = m_SkinnedMotionVectors;
	j["dynamicOcclusion"] = m_DynamicOcclusion;
	j["renderingLayerMask"] = m_RenderingLayerMask;
	j["maskInteraction"] = m_MaskInteraction;
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
	m_StaticShadowCaster = j.value("staticShadowCaster", false);
	m_LightProbes = j.value("lightProbes", 1);
	m_SkinnedMotionVectors = j.value("skinnedMotionVectors", true);
	m_DynamicOcclusion = j.value("dynamicOcclusion", true);
	m_RenderingLayerMask = j.value("renderingLayerMask", 0);
	m_MaskInteraction = j.value("maskInteraction", 0);
	m_BlendWeights = j.value("blendShapeWeights", std::vector<float>());
	m_MorphDirty = true;

	// 메시 + 스켈레톤 (재질 수가 부족하면 기본 재질로 채움)
	SetSkinnedMesh(meshPath, m_MeshSubsetIndex);
}
