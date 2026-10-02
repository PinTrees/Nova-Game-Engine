#include "pch.h"
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
			UMaterial::ApplyOrDefault(material, Effects::InstancedBasicFX.get());
			tech->GetPassByIndex(p)->Apply(0, deviceContext);
			m_Mesh->ModelMesh.Draw(deviceContext, i);
		}
	}
}

void SkinnedMeshRenderer::Render()
{
	RenderStats::AddSkinnedMesh();
	DrawSkinned(false);
}

void SkinnedMeshRenderer::_Editor_Render()
{
	DrawSkinned(true);
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
			tech->GetPassByIndex(p)->Apply(0, deviceContext);
			m_Mesh->ModelMesh.Draw(deviceContext, i);
		}
	}
}

// 게임/에디터 공통 SSAO 노멀·깊이 패스
static void DrawSkinnedNormalDepth(SkinnedMesh* mesh, Transform* transform, const vector<XMFLOAT4X4>& bones, bool editor)
{
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
			tech->GetPassByIndex(p)->Apply(0, deviceContext);
			mesh->ModelMesh.Draw(deviceContext, i);
		}
	}
}

void SkinnedMeshRenderer::RenderShadowNormal()
{
	if (m_Mesh == nullptr || m_Mesh->Subsets.empty())
		return;
	EnsureBones();
	DrawSkinnedNormalDepth(m_Mesh.get(), m_pGameObject->GetTransform(), m_FinalTransforms, false);
}

void SkinnedMeshRenderer::_Editor_RenderShadowNormal()
{
	if (m_Mesh == nullptr || m_Mesh->Subsets.empty())
		return;
	EnsureBones();
	DrawSkinnedNormalDepth(m_Mesh.get(), m_pGameObject->GetTransform(), m_FinalTransforms, true);
}

// ------------------------------------------------------------------ Inspector (Unity 6)
void SkinnedMeshRenderer::OnInspectorGUI()
{
	using namespace UnityGUI;
	static const char* kQuality[] = { "Auto", "1 Bone", "2 Bones", "4 Bones" };
	static const char* kCast[] = { "On", "Off", "Two Sided", "Shadows Only" };
	static const char* kProbes[] = { "Off", "Blend Probes", "Use Proxy Volume", "Custom Provided" };
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
		Dropdown("Light Probes", &m_LightProbes, kProbes, 4, 0);
		ObjectField("Anchor Override", "None (Transform)", 0);
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

	// 메시 + 스켈레톤 (재질 수가 부족하면 기본 재질로 채움)
	SetSkinnedMesh(meshPath, m_MeshSubsetIndex);
}
