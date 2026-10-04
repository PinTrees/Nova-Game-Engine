#include "pch.h"
#include "RenderStats.h"
#include "MeshRenderer.h"
#include "MeshFilter.h"
#include "UnityGUI.h"
#include "GameObjectFactory.h"
#include "Effects.h"
#include "ShaderSetting.h"
#include "MathHelper.h"
#include "InstancingBuffer.h"
#include "EditorGUI.h"
#include "MaterialInspector.h"

MeshRenderer::MeshRenderer()
	: m_pMaterials({}),
	m_MeshSubsetIndex(0),
	m_Mesh(nullptr),
	m_MaterialPaths({})
{
	m_InspectorTitleName = "Mesh Renderer";
	m_InspectorIconPath = L"mesh_renderer.png";
}

MeshRenderer::~MeshRenderer()
{
}

void MeshRenderer::Render()
{
	SyncMeshFromFilter();
	if (m_Mesh == nullptr)
		return;

	auto deviceContext = Application::GetI()->GetDeviceContext();
	ComPtr<FxTechnique> tech = Effects::InstancedBasicFX->Tech;

	D3DX11_TECHNIQUE_DESC techDesc;
	tech->GetDesc(&techDesc);

	XMMATRIX toTexSpace(
		0.5f, 0.0f, 0.0f, 0.0f,
		0.0f, -0.5f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.5f, 0.5f, 0.0f, 1.0f);

	deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	deviceContext->IASetInputLayout(InputLayouts::PosNormalTexTan.Get());

	for (uint32 p = 0; p < techDesc.Passes; ++p)
	{
		if (m_Mesh->Subsets.size() <= 0)
			break;

		Transform* transform = m_pGameObject->GetComponent<Transform>();
		
		XMMATRIX world = transform->GetWorldMatrix();
		XMMATRIX worldInvTranspose = MathHelper::InverseTranspose(world);
		XMMATRIX vi = RenderManager::GetI()->CameraViewMatrix;
		XMMATRIX pr = RenderManager::GetI()->CameraProjectionMatrix;
		XMMATRIX worldViewProj = world * RenderManager::GetI()->CameraViewProjectionMatrix;

		Effects::InstancedBasicFX->SetWorld(world);
		Effects::InstancedBasicFX->SetWorldInvTranspose(worldInvTranspose);
		Effects::InstancedBasicFX->SetViewProj(RenderManager::GetI()->CameraViewProjectionMatrix);
		Effects::InstancedBasicFX->SetWorldViewProj(worldViewProj);
		Effects::InstancedBasicFX->SetWorldViewProjTex(worldViewProj * toTexSpace);
		//Effects::InstancedBasicFX->SetShadowTransform(world * RenderManager::GetI()->shadowTransform);
		Effects::InstancedBasicFX->SetTexTransform(XMMatrixScaling(1.0f, 1.0f, 1.0f));

		for (int i = 0; i < m_Mesh->Subsets.size(); ++i)
		{
			// URP Lit 재질 (없으면 기본 재질)
			const UINT matIndex = m_Mesh->Subsets[i].MaterialIndex;
			UMaterial::ApplyOrDefault(matIndex < m_pMaterials.size() ? m_pMaterials[matIndex] : nullptr, Effects::InstancedBasicFX.get());

			tech->GetPassByIndex(p)->Apply(0, deviceContext);  
			m_Mesh->ModelMesh.Draw(deviceContext, i); 
		}
	}
}

void MeshRenderer::RenderInstancing(shared_ptr<class InstancingBuffer>& buffer)
{
	SyncMeshFromFilter();
	if (m_Mesh == nullptr)
		return;

	auto deviceContext = Application::GetI()->GetDeviceContext();
	ComPtr<FxTechnique> tech = Effects::InstancedBasicFX->InstancingTech;
	//Light3TexTech;

	deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	deviceContext->IASetInputLayout(InputLayouts::InstancedBasic.Get());

	D3DX11_TECHNIQUE_DESC techDesc;
	tech->GetDesc(&techDesc);

	XMMATRIX toTexSpace(
		0.5f, 0.0f, 0.0f, 0.0f,
		0.0f, -0.5f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.5f, 0.5f, 0.0f, 1.0f);

	for (uint32 p = 0; p < techDesc.Passes; ++p)
	{
		if (m_MeshSubsetIndex >= m_Mesh->Subsets.size())
			break;

		Transform* transform = m_pGameObject->GetComponent<Transform>();

		XMMATRIX world = transform->GetWorldMatrix();
		XMMATRIX worldInvTranspose = MathHelper::InverseTranspose(world);
		XMMATRIX worldViewProj = world * RenderManager::GetI()->EditorCameraViewProjectionMatrix;
		XMMATRIX View = RenderManager::GetI()->EditorCameraViewMatrix;
		XMMATRIX Proj = RenderManager::GetI()->EditorCameraProjectionMatrix;
		XMMATRIX ViewProj = RenderManager::GetI()->EditorCameraViewProjectionMatrix;

		Effects::InstancedBasicFX->SetWorld(world);
		Effects::InstancedBasicFX->SetWorldInvTranspose(worldInvTranspose);
		Effects::InstancedBasicFX->SetWorldViewProj(worldViewProj);
		Effects::InstancedBasicFX->SetWorldViewProjTex(worldViewProj * toTexSpace);

		Effects::InstancedBasicFX->SetViewProj(View* Proj);
		Effects::InstancedBasicFX->SetViewProjTex(ViewProj * toTexSpace);
		//Effects::InstancedBasicFX->SetShadowTransform(RenderManager::GetI()->shadowTransform);
		Effects::InstancedBasicFX->SetTexTransform(XMMatrixScaling(1.0f, 1.0f, 1.0f));

		// 인스턴싱 묶음 = 같은 메시 부분: 그 부분의 재질 (없으면 기본 재질)
		{
			const UINT matIndex = m_Mesh->Subsets[m_MeshSubsetIndex].MaterialIndex;
			UMaterial::ApplyOrDefault(matIndex < m_pMaterials.size() ? m_pMaterials[matIndex] : (m_pMaterials.empty() ? nullptr : m_pMaterials[0]), Effects::InstancedBasicFX.get());
		}

		tech->GetPassByIndex(p)->Apply(0, deviceContext);

		buffer->PushData(deviceContext);

		// �ν��Ͻ�, ModelMesh Ŭ������ InstancingDraw�Լ� ����
		m_Mesh->ModelMesh.InstancingDraw(deviceContext, m_MeshSubsetIndex, buffer->GetCount());
	}
}

void MeshRenderer::RenderShadow()
{
	if (m_CastShadows == 1)
		return;   // Cast Shadows: Off
	SyncMeshFromFilter();
	if (m_Mesh == nullptr)
		return;
	RenderStats::AddShadowCaster();

	Transform* transform = m_pGameObject->GetComponent<Transform>();
	auto deviceContext = Application::GetI()->GetDeviceContext();

	ComPtr<FxTechnique> tech = Effects::BuildShadowMapFX->BuildShadowMapTech;
	ComPtr<FxTechnique> alphaClippedTech = Effects::BuildShadowMapFX->BuildShadowMapAlphaClipTech;

	XMMATRIX world;
	XMMATRIX worldInvTranspose;
	XMMATRIX worldViewProj;

	deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST); 
	deviceContext->IASetInputLayout(InputLayouts::PosNormalTexTan.Get()); 

	D3DX11_TECHNIQUE_DESC techDesc;
	tech->GetDesc(&techDesc);

	for (uint32 p = 0; p < techDesc.Passes; ++p)
	{
		if (m_Mesh->Subsets.size() <= 0) 
			break;

		world = transform->GetWorldMatrix();
		worldInvTranspose = MathHelper::InverseTranspose(world);
		worldViewProj = world * RenderManager::GetI()->LightViewProjection;

		Effects::BuildShadowMapFX->SetWorld(world);
		Effects::BuildShadowMapFX->SetWorldInvTranspose(worldInvTranspose);
		Effects::BuildShadowMapFX->SetWorldViewProj(worldViewProj);
		Effects::BuildShadowMapFX->SetTexTransform(::XMMatrixScaling(1.0f, 1.0f, 1.0f));

		for (int i = 0; i < m_Mesh->Subsets.size(); ++i)
		{
			tech->GetPassByIndex(p)->Apply(0, deviceContext);
			m_Mesh->ModelMesh.Draw(deviceContext, i);
		}
	}
}

void MeshRenderer::RenderShadowInstancing(shared_ptr<class InstancingBuffer>& buffer)
{
	if (m_CastShadows == 1)
		return;   // Cast Shadows: Off
	SyncMeshFromFilter();
	if (m_Mesh == nullptr)
		return;

	auto deviceContext = Application::GetI()->GetDeviceContext();

	ComPtr<FxTechnique> tech = Effects::BuildShadowMapFX->BuildShadowMapInstancingTech;
	ComPtr<FxTechnique> alphaClippedTech = Effects::BuildShadowMapFX->BuildShadowMapAlphaClipInstancingTech;

	XMMATRIX ViewProj;

	deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	deviceContext->IASetInputLayout(InputLayouts::InstancedBasic.Get());

	D3DX11_TECHNIQUE_DESC techDesc;
	tech->GetDesc(&techDesc);

	for (uint32 p = 0; p < techDesc.Passes; ++p)
	{
		if (m_MeshSubsetIndex >= m_Mesh->Subsets.size())
			break;

		ViewProj = RenderManager::GetI()->LightViewProjection;

		Effects::BuildShadowMapFX->SetViewProj(ViewProj);
		Effects::BuildShadowMapFX->SetTexTransform(::XMMatrixScaling(1.0f, 1.0f, 1.0f));

		tech->GetPassByIndex(p)->Apply(0, deviceContext);

		buffer->PushData(deviceContext);

		m_Mesh->ModelMesh.InstancingDraw(deviceContext, m_MeshSubsetIndex, buffer->GetCount());
	}
}

void MeshRenderer::RenderShadowNormal()
{
	SyncMeshFromFilter();
	if (m_Mesh == nullptr)
		return;

	auto deviceContext = Application::GetI()->GetDeviceContext();
	Transform* transform = m_pGameObject->GetComponent<Transform>();
	ComPtr<FxTechnique> tech = Effects::SsaoNormalDepthFX->NormalDepthTech;

	XMMATRIX world;
	XMMATRIX worldInvTranspose;
	XMMATRIX worldView;
	XMMATRIX worldInvTransposeView;
	XMMATRIX worldViewProj;

	deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	deviceContext->IASetInputLayout(InputLayouts::PosNormalTexTan.Get());

	D3DX11_TECHNIQUE_DESC techDesc;
	tech->GetDesc(&techDesc);
	for (uint32 p = 0; p < techDesc.Passes; ++p)
	{
		if (m_Mesh->Subsets.size() <= 0) 
			break;

		world = transform->GetWorldMatrix();
		worldInvTranspose = MathHelper::InverseTranspose(world);
		worldView = world * RenderManager::GetI()->CameraViewMatrix;
		worldInvTransposeView = worldInvTranspose * RenderManager::GetI()->CameraViewMatrix;
		worldViewProj = world * RenderManager::GetI()->CameraViewProjectionMatrix;

		Effects::SsaoNormalDepthFX->SetWorldView(worldView);
		Effects::SsaoNormalDepthFX->SetWorldInvTransposeView(worldInvTransposeView);
		Effects::SsaoNormalDepthFX->SetWorldViewProj(worldViewProj);
		Effects::SsaoNormalDepthFX->SetTexTransform(XMMatrixScaling(1.0f, 1.0f, 1.0f));

		for (int i = 0; i < m_Mesh->Subsets.size(); ++i)
		{
			tech->GetPassByIndex(p)->Apply(0, deviceContext);
			m_Mesh->ModelMesh.Draw(deviceContext, i); 
		}
	}
}

void MeshRenderer::RenderShadowNormalInstancing(shared_ptr<class InstancingBuffer>& buffer)
{
	SyncMeshFromFilter();
	if (m_Mesh == nullptr)
		return;

	auto deviceContext = Application::GetI()->GetDeviceContext();
	ComPtr<FxTechnique> tech = Effects::SsaoNormalDepthFX->NormalDepthInstancingTech;

	XMMATRIX View;
	XMMATRIX Proj;

	deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	deviceContext->IASetInputLayout(InputLayouts::InstancedBasic.Get());

	D3DX11_TECHNIQUE_DESC techDesc;
	tech->GetDesc(&techDesc);
	for (uint32 p = 0; p < techDesc.Passes; ++p)
	{
		if (m_MeshSubsetIndex >= m_Mesh->Subsets.size())
			break;

		XMMATRIX world;
		XMMATRIX worldInvTranspose;
		XMMATRIX worldView;
		XMMATRIX worldInvTransposeView;
		XMMATRIX worldViewProj;
		Transform* transform = m_pGameObject->GetComponent<Transform>();

		world = transform->GetWorldMatrix();
		worldInvTranspose = MathHelper::InverseTranspose(world);
		worldView = world * RenderManager::GetI()->CameraViewMatrix;
		worldInvTransposeView = worldInvTranspose * RenderManager::GetI()->CameraViewMatrix;
		worldViewProj = world * RenderManager::GetI()->CameraViewProjectionMatrix;

		Effects::SsaoNormalDepthFX->SetWorldView(worldView);
		Effects::SsaoNormalDepthFX->SetWorldInvTransposeView(worldInvTransposeView);
		Effects::SsaoNormalDepthFX->SetWorldViewProj(worldViewProj);
		Effects::SsaoNormalDepthFX->SetTexTransform(XMMatrixScaling(1.0f, 1.0f, 1.0f));

		View = RenderManager::GetI()->CameraViewMatrix;
		Proj = RenderManager::GetI()->CameraProjectionMatrix;

		Effects::SsaoNormalDepthFX->SetView(View);
		Effects::SsaoNormalDepthFX->SetProj(Proj);
		Effects::SsaoNormalDepthFX->SetTexTransform(XMMatrixScaling(1.0f, 1.0f, 1.0f));

		tech->GetPassByIndex(p)->Apply(0, deviceContext);

		buffer->PushData(deviceContext);

		m_Mesh->ModelMesh.InstancingDraw(deviceContext, m_MeshSubsetIndex, buffer->GetCount());
	}
}

void MeshRenderer::_Editor_Render()
{
	SyncMeshFromFilter();
	if (m_Mesh == nullptr)
		return;

	auto deviceContext = Application::GetI()->GetDeviceContext();
	ComPtr<FxTechnique> tech = Effects::InstancedBasicFX->Tech;

	D3DX11_TECHNIQUE_DESC techDesc;
	tech->GetDesc(&techDesc);

	XMMATRIX toTexSpace(
		0.5f, 0.0f, 0.0f, 0.0f,
		0.0f, -0.5f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.5f, 0.5f, 0.0f, 1.0f);

	deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	deviceContext->IASetInputLayout(InputLayouts::PosNormalTexTan.Get());

	for (uint32 p = 0; p < techDesc.Passes; ++p)
	{
		if (m_Mesh->Subsets.size() <= 0)
			break;

		Transform* transform = m_pGameObject->GetComponent<Transform>();

		XMMATRIX world = transform->GetWorldMatrix();
		XMMATRIX worldInvTranspose = MathHelper::InverseTranspose(world);
		XMMATRIX vi = RenderManager::GetI()->EditorCameraViewMatrix;
		XMMATRIX pr = RenderManager::GetI()->EditorCameraProjectionMatrix;
		XMMATRIX worldViewProj = world * RenderManager::GetI()->EditorCameraViewProjectionMatrix;

		Effects::InstancedBasicFX->SetWorld(world);
		Effects::InstancedBasicFX->SetWorldInvTranspose(worldInvTranspose);
		Effects::InstancedBasicFX->SetViewProj(RenderManager::GetI()->EditorCameraViewProjectionMatrix);
		Effects::InstancedBasicFX->SetWorldViewProj(worldViewProj);
		Effects::InstancedBasicFX->SetWorldViewProjTex(worldViewProj * toTexSpace);
		//Effects::InstancedBasicFX->SetShadowTransform(world * RenderManager::GetI()->shadowTransform);
		Effects::InstancedBasicFX->SetTexTransform(XMMatrixScaling(1.0f, 1.0f, 1.0f));

		for (int i = 0; i < m_Mesh->Subsets.size(); ++i)
		{
			// URP Lit 재질 (없으면 기본 재질)
			const UINT matIndex = m_Mesh->Subsets[i].MaterialIndex;
			UMaterial::ApplyOrDefault(matIndex < m_pMaterials.size() ? m_pMaterials[matIndex] : nullptr, Effects::InstancedBasicFX.get());

			tech->GetPassByIndex(p)->Apply(0, deviceContext);
			m_Mesh->ModelMesh.Draw(deviceContext, i);
		}
	}
}

void MeshRenderer::_Editor_RenderShadowNormal()
{
	SyncMeshFromFilter();
	if (m_Mesh == nullptr)
		return;

	auto deviceContext = Application::GetI()->GetDeviceContext();
	Transform* transform = m_pGameObject->GetComponent<Transform>();
	ComPtr<FxTechnique> tech = Effects::SsaoNormalDepthFX->NormalDepthTech;

	XMMATRIX world;
	XMMATRIX worldInvTranspose;
	XMMATRIX worldView;
	XMMATRIX worldInvTransposeView;
	XMMATRIX worldViewProj;

	deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	deviceContext->IASetInputLayout(InputLayouts::PosNormalTexTan.Get());

	D3DX11_TECHNIQUE_DESC techDesc;
	tech->GetDesc(&techDesc);
	for (uint32 p = 0; p < techDesc.Passes; ++p)
	{
		if (m_Mesh->Subsets.size() <= 0)
			break;

		world = transform->GetWorldMatrix();
		worldInvTranspose = MathHelper::InverseTranspose(world);
		worldView = world * RenderManager::GetI()->EditorCameraViewMatrix;
		worldInvTransposeView = worldInvTranspose * RenderManager::GetI()->EditorCameraViewMatrix;
		worldViewProj = world * RenderManager::GetI()->EditorCameraViewProjectionMatrix;

		Effects::SsaoNormalDepthFX->SetWorldView(worldView);
		Effects::SsaoNormalDepthFX->SetWorldInvTransposeView(worldInvTransposeView);
		Effects::SsaoNormalDepthFX->SetWorldViewProj(worldViewProj);
		Effects::SsaoNormalDepthFX->SetTexTransform(XMMatrixScaling(1.0f, 1.0f, 1.0f));

		for (int i = 0; i < m_Mesh->Subsets.size(); ++i)
		{
			tech->GetPassByIndex(p)->Apply(0, deviceContext);
			m_Mesh->ModelMesh.Draw(deviceContext, i);
		}
	}
}

void MeshRenderer::SetMaterialAt(int index, shared_ptr<UMaterial> material, const wstring& path)
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

namespace
{
	uint64 MixHash(uint64 h, uint64 v)
	{
		h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
		return h;
	}

	// 블록 파생 재질: (원본 재질, 원본 값, 블록 값) 마다 하나 — 같은 값의 렌더러가 같은 객체를 써서 MeshBatcher 가 한 묶음으로 그린다
	struct Variant { shared_ptr<UMaterial> Base, Material; };
	std::map<std::tuple<const UMaterial*, uint64, uint64>, Variant> s_Variants;

	shared_ptr<UMaterial> VariantOf(const shared_ptr<UMaterial>& base, uint64 blockHash, const std::vector<std::pair<std::string, MeshRenderer::BlockValue>>& block)
	{
		const shared_ptr<UMaterial> src = base ? base : UMaterial::GetDefault();
		const auto key = std::make_tuple(src.get(), src->StateHash(), blockHash);
		auto it = s_Variants.find(key);
		if (it != s_Variants.end())
			return it->second.Material;
		if (s_Variants.size() > 4096)
			for (auto v = s_Variants.begin(); v != s_Variants.end();)   // 아무도 쓰지 않는 파생 재질 (캐시만 잡고 있다)
				v = v->second.Material.use_count() == 1 ? s_Variants.erase(v) : std::next(v);
		shared_ptr<UMaterial> m = src->CloneInstance();
		for (const auto& [name, value] : block)
		{
			if (value.Color) m->SetColorProperty(name, value.V);
			else m->SetFloatProperty(name, value.V.x);
		}
		s_Variants[key] = { src, m };
		return m;
	}
}

void MeshRenderer::SetPropertyBlock(std::vector<std::pair<std::string, BlockValue>> values)
{
	std::sort(values.begin(), values.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
	m_Block = std::move(values);
	uint64 h = 1469598103934665603ull;
	for (const auto& [name, value] : m_Block)
	{
		h = MixHash(h, std::hash<std::string>()(name));
		h = MixHash(h, value.Color ? 1 : 2);
		uint32_t bits[4];
		memcpy(bits, &value.V, sizeof(bits));
		for (uint32_t b : bits) h = MixHash(h, b);
	}
	m_BlockHash = h;
	m_RenderStamp = 0;
}

const vector<shared_ptr<UMaterial>>& MeshRenderer::GetRenderMaterials()
{
	if (m_Block.empty())
		return m_pMaterials;
	uint64 stamp = MixHash(m_BlockHash, m_pMaterials.size());
	for (const auto& m : m_pMaterials)
		stamp = MixHash(MixHash(stamp, (uint64)(uintptr_t)m.get()), m ? m->StateHash() : 0);
	if (stamp != m_RenderStamp || m_RenderMaterials.size() != m_pMaterials.size())
	{
		m_RenderMaterials.resize(m_pMaterials.size());
		for (size_t i = 0; i < m_pMaterials.size(); ++i)
			m_RenderMaterials[i] = VariantOf(m_pMaterials[i], m_BlockHash, m_Block);
		m_RenderStamp = stamp;
	}
	return m_RenderMaterials;
}

bool MeshRenderer::SetMaterialPath(int index, const wstring& path)
{
	if (index < 0 || path.empty())
		return false;
	shared_ptr<UMaterial> material = path == L"builtin:Default-Material" ? UMaterial::GetDefault() : ResourceManager::GetI()->LoadMaterial(wstring_to_string(path));
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

void MeshRenderer::SyncMeshFromFilter()
{
	if (m_pGameObject == nullptr)
		return;
	MeshFilter* filter = m_pGameObject->GetComponent<MeshFilter>();
	if (filter != nullptr)
	{
		m_Mesh = filter->GetMesh();
		m_MeshSubsetIndex = filter->GetSubsetIndex();
	}
}

void MeshRenderer::OnInspectorGUI()
{
	using namespace UnityGUI;
	static const char* kCast[] = { "On", "Off", "Two Sided", "Shadows Only" };
	static const char* kGI[] = { "Light Probes" };
	static const char* kMotion[] = { "Camera Motion", "Per Object Motion", "Force No Motion" };
	static const char* kLayerMask[] = { "Default", "Nothing", "Everything" };
	static const char* kMask[] = { "None", "Visible Inside Mask", "Visible Outside Mask" };

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

	// ---- Lighting ----
	if (FoldoutPlain("Lighting"))
	{
		Dropdown("Cast Shadows", &m_CastShadows, kCast, 4, 0);
		Toggle("Static Shadow Caster", &m_StaticShadowCaster);
		Toggle("Contribute Global Illumination", &m_ContributeGI);
		Dropdown("Receive Global Illumination", &m_ReceiveGI, kGI, 1, 1, true);
	}

	// ---- Probes ----
	if (FoldoutPlain("Probes"))
	{
		// 확산 간접광은 Adaptive Probe Volume 만 (레거시 Light Probe Group · Proxy Volume 없음)
		ValueLabel("Light Probes", "Adaptive Probe Volume");
	}

	// ---- Additional Settings ----
	if (FoldoutPlain("Additional Settings"))
	{
		Dropdown("Motion Vectors", &m_MotionVectors, kMotion, 3, 0);
		Toggle("Dynamic Occlusion", &m_DynamicOcclusion);
		Dropdown("Rendering Layer Mask", &m_RenderingLayerMask, kLayerMask, 3, 0);
	}

	// ---- 2D ----
	if (FoldoutPlain("2D"))
		Dropdown("Mask Interaction", &m_MaskInteraction, kMask, 3, 0);
}

void MeshRenderer::DrawMaterialInspectors()
{
	std::vector<UMaterial*> shown;   // 같은 재질은 패널을 한 번만 (Unity 와 동일)
	for (auto& material : m_pMaterials)
	{
		if (material == nullptr || std::find(shown.begin(), shown.end(), material.get()) != shown.end())
			continue;
		shown.push_back(material.get());
		// Unity 처럼 컴포넌트 아래에 재질 Inspector (URP Lit)
		MaterialInspector::WatchUndo(material);
		material->OnInspectorGUI(true);
	}
}

GENERATE_COMPONENT_FUNC_TOJSON(MeshRenderer)
{
	json j;

	SERIALIZE_TYPE(j, MeshRenderer);
	SERIALIZE_INT(j, m_MeshSubsetIndex, "subsetIndex");
	// 같은 GameObject 에 MeshFilter 가 있으면 메시는 필터가 저장한다
	if (m_pGameObject != nullptr && m_pGameObject->GetComponent<MeshFilter>() != nullptr)
		j["meshPath"] = "";
	else
		SERIALIZE_WSTRING(j, m_MeshPath, "meshPath");
	SERIALIZE_WSTRING_ARRAY(j, m_MaterialPaths, "m_MaterialPaths");

	// Shader
	SERIALIZE_WSTRING(j, m_ShaderPath, "shaderPath");

	j["enabled"] = m_Enabled;
	j["castShadows"] = m_CastShadows;
	j["staticShadowCaster"] = m_StaticShadowCaster;
	j["contributeGI"] = m_ContributeGI;
	j["receiveGI"] = m_ReceiveGI;
	j["lightProbes"] = m_LightProbes;
	j["motionVectors"] = m_MotionVectors;
	j["dynamicOcclusion"] = m_DynamicOcclusion;
	j["renderingLayerMask"] = m_RenderingLayerMask;
	j["maskInteraction"] = m_MaskInteraction;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(MeshRenderer)
{
	DE_SERIALIZE_WSTRING(j, m_ShaderPath, "shaderPath");

	m_Enabled = j.value("enabled", true);
	m_CastShadows = j.value("castShadows", 0);
	m_StaticShadowCaster = j.value("staticShadowCaster", false);
	m_ContributeGI = j.value("contributeGI", false);
	m_ReceiveGI = j.value("receiveGI", 0);
	m_LightProbes = j.value("lightProbes", 1);
	m_MotionVectors = j.value("motionVectors", 1);
	m_DynamicOcclusion = j.value("dynamicOcclusion", true);
	m_RenderingLayerMask = j.value("renderingLayerMask", 0);
	m_MaskInteraction = j.value("maskInteraction", 0);

	DE_SERIALIZE_INT(j, m_MeshSubsetIndex, "subsetIndex");
	DE_SERIALIZE_WSTRING(j, m_MeshPath, "meshPath");
	if (m_MeshPath != L"")
		m_Mesh = ResourceManager::GetI()->LoadMesh(m_MeshPath, m_MeshSubsetIndex);  
	 
	DE_SERIALIZE_WSTRING_ARRAY(j, m_MaterialPaths, "m_MaterialPaths");
	m_pMaterials.clear();   // Undo 등으로 기존 컴포넌트에 다시 읽을 때 중복되지 않게
	if (m_MaterialPaths.size() > 0)
	{
		for (int i = 0; i < m_MaterialPaths.size(); ++i)
		{
			// 빈 경로 = None, 읽지 못한 재질도 슬롯과 경로는 남긴다 (m_MaterialPaths 와 순서를 맞춤, 그릴 때는 기본 재질)
			auto material = m_MaterialPaths[i].empty() ? nullptr : ResourceManager::GetI()->LoadMaterial(wstring_to_string(m_MaterialPaths[i]));
			m_pMaterials.push_back(material);
		}
	}

	// 내장 도형인데 재질이 없으면(이전 버전에서 저장한 씬 등) 기본 재질을 붙인다
	if (m_pMaterials.empty() && GameObjectFactory::IsBuiltinMeshPath(m_MeshPath))
	{
		m_pMaterials.push_back(UMaterial::GetDefault());
		m_MaterialPaths.clear();
		m_MaterialPaths.push_back(L"builtin:Default-Material");
	}
}