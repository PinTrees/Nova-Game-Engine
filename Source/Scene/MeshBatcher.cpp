#include "pch.h"
#include "MeshBatcher.h"
#include "Scene.h"
#include "MeshRenderer.h"
#include "Mesh.h"
#include "Transform.h"
#include "UMaterial.h"
#include "Effects.h"
#include "Vertex.h"
#include "RenderManager.h"
#include "SceneCulling.h"
#include "RenderStats.h"
#include "Profiler.h"
#include <unordered_map>

namespace
{
	struct Key
	{
		const Mesh* MeshPtr;
		int Subset;
		const UMaterial* Material;
		bool operator==(const Key& o) const { return MeshPtr == o.MeshPtr && Subset == o.Subset && Material == o.Material; }
	};
	struct KeyHash
	{
		size_t operator()(const Key& k) const
		{
			return std::hash<const void*>()(k.MeshPtr) ^ (std::hash<int>()(k.Subset) * 31) ^ (std::hash<const void*>()(k.Material) * 1099511628211ull);
		}
	};

	struct Batch
	{
		Mesh* MeshPtr = nullptr;
		int Subset = 0;
		shared_ptr<UMaterial> Material;
		std::vector<XMFLOAT4X4> Worlds;
	};

	// 화면 하나 동안 쓰는 렌더러 목록 (BeginView 뒤 첫 Draw 가 만든다)
	struct Caster
	{
		const Component* Renderer;
		XMFLOAT4X4 World;
		int Cast;               // 0 On, 1 Off, 2 Two Sided, 3 Shadows Only
		uint32_t First, Count;  // s_MainItems / s_DepthItems 범위 (서브셋마다 묶음 번호)
	};
	std::vector<Caster> s_Casters;
	std::vector<int> s_MainItems, s_DepthItems;
	// 묶음 배열: 본 패스 = (메시, 서브셋, 재질), 그림자·깊이 = (메시, 서브셋). 원소는 화면마다 다시 쓰고 Worlds 용량은 남긴다
	std::vector<Batch> s_MainBatches, s_DepthBatches;
	int s_MainCount = 0, s_DepthCount = 0;
	std::unordered_map<Key, int, KeyHash> s_MainIndex, s_DepthIndex;
	bool s_Collected = false;
	Scene* s_CollectedScene = nullptr;
	uint32_t s_CollectedFrame = 0;

	std::vector<int> s_Order;
	MeshBatcher::Stats s_Stats[2];

	int BatchIndex(std::unordered_map<Key, int, KeyHash>& index, std::vector<Batch>& batches, int& count, Mesh* mesh, int subset, const shared_ptr<UMaterial>& mat)
	{
		const Key key{ mesh, subset, mat.get() };
		if (auto it = index.find(key); it != index.end())
			return it->second;
		if (count >= (int)batches.size())
			batches.emplace_back();
		Batch& b = batches[count];
		b.MeshPtr = mesh;
		b.Subset = subset;
		b.Material = mat;
		b.Worlds.clear();
		index.emplace(key, count);
		return count++;
	}

	// 씬을 한 번 훑는다: 켜진 Mesh Renderer 마다 월드 행렬과 서브셋별 묶음 번호
	void Collect(Scene* scene)
	{
		s_Casters.clear();
		s_MainItems.clear();
		s_DepthItems.clear();
		s_MainIndex.clear();
		s_DepthIndex.clear();
		s_MainCount = s_DepthCount = 0;
		for (GameObject* go : scene->GetAllGameObjects())
		{
			if (go == nullptr || !go->IsActive())
				continue;
			MeshRenderer* mr = go->GetComponent<MeshRenderer>();
			if (mr == nullptr || !mr->IsEnabled())
				continue;
			auto mesh = mr->GetMesh();
			if (!mesh || mesh->Subsets.empty())
				continue;
			Caster c;
			c.Renderer = mr;
			XMStoreFloat4x4(&c.World, go->GetTransform()->GetWorldMatrix());
			c.Cast = mr->GetCastShadows();
			c.First = (uint32_t)s_MainItems.size();
			c.Count = (uint32_t)mesh->Subsets.size();
			const auto& materials = mr->GetMaterials();
			for (int i = 0; i < (int)mesh->Subsets.size(); ++i)
			{
				const UINT matIndex = mesh->Subsets[i].MaterialIndex;
				static const shared_ptr<UMaterial> s_None;
				const shared_ptr<UMaterial>& mat = matIndex < materials.size() ? materials[matIndex] : s_None;
				s_MainItems.push_back(BatchIndex(s_MainIndex, s_MainBatches, s_MainCount, mesh.get(), i, mat));
				s_DepthItems.push_back(BatchIndex(s_DepthIndex, s_DepthBatches, s_DepthCount, mesh.get(), i, s_None));
			}
			s_Casters.push_back(c);
		}
		// 이번 화면에 안 쓰는 묶음은 재질·메시를 놓는다 (Worlds 용량은 남김)
		for (size_t i = (size_t)s_MainCount; i < s_MainBatches.size(); ++i) { s_MainBatches[i].Material.reset(); s_MainBatches[i].MeshPtr = nullptr; }
		for (size_t i = (size_t)s_DepthCount; i < s_DepthBatches.size(); ++i) s_DepthBatches[i].MeshPtr = nullptr;
		s_Collected = true;
		s_CollectedScene = scene;
		s_CollectedFrame = SceneCulling::FrameIndex();
	}

	ComPtr<ID3D11Buffer> s_InstanceBuffer;
	UINT s_Capacity = 0;

	ID3D11Buffer* Upload(ID3D11DeviceContext* dc, const std::vector<XMFLOAT4X4>& worlds)
	{
		const UINT count = (UINT)worlds.size();
		if (count > s_Capacity)
		{
			s_Capacity = (std::max)(count, s_Capacity * 2 + 256);
			D3D11_BUFFER_DESC bd = {};
			bd.Usage = D3D11_USAGE_DYNAMIC;
			bd.ByteWidth = s_Capacity * sizeof(XMFLOAT4X4);
			bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
			bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			s_InstanceBuffer.Reset();
			Application::GetI()->GetDevice()->CreateBuffer(&bd, nullptr, s_InstanceBuffer.GetAddressOf());
		}
		D3D11_MAPPED_SUBRESOURCE mapped;
		if (!s_InstanceBuffer || FAILED(dc->Map(s_InstanceBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
			return nullptr;
		memcpy(mapped.pData, worlds.data(), count * sizeof(XMFLOAT4X4));
		dc->Unmap(s_InstanceBuffer.Get(), 0);
		return s_InstanceBuffer.Get();
	}

	void SetMatrix(ID3DX11Effect* fx, const char* name, CXMMATRIX m)
	{
		if (auto* v = fx->GetVariableByName(name)->AsMatrix(); v && v->IsValid())
			v->SetMatrix(reinterpret_cast<const float*>(&m));
	}
}

namespace MeshBatcher
{
	const Stats& LastStats(bool editor) { return s_Stats[editor ? 1 : 0]; }

	void BeginView() { s_Collected = false; }

	void Draw(Scene* scene, Pass pass, bool editor)
	{
		if (scene == nullptr)
			return;
		PROFILE_SCOPE("MeshBatcher");
		PROFILE_GPU("Mesh Renderers");

		// ---- 렌더러 목록: 화면마다 한 번 (같은 화면의 다른 패스·그림자 조각은 다시 쓴다)
		if (!s_Collected || s_CollectedScene != scene || s_CollectedFrame != SceneCulling::FrameIndex())
		{
			PROFILE_SCOPE("MeshBatcher.Collect");
			Collect(scene);
		}

		// ---- 이번 패스: 보이는 렌더러의 월드 행렬만 묶음에 쌓는다
		const bool main = pass == Pass::Main;
		std::vector<Batch>& batches = main ? s_MainBatches : s_DepthBatches;
		const std::vector<int>& items = main ? s_MainItems : s_DepthItems;
		const int batchCount = main ? s_MainCount : s_DepthCount;
		for (int i = 0; i < batchCount; ++i)
			batches[i].Worlds.clear();
		s_Order.clear();
		int objects = 0;
		for (const Caster& c : s_Casters)
		{
			if (!SceneCulling::IsVisible(c.Renderer))
				continue;
			if ((pass == Pass::Shadow && c.Cast == 1) || (pass != Pass::Shadow && c.Cast == 3))
				continue;
			for (uint32_t k = c.First; k < c.First + c.Count; ++k)
			{
				Batch& b = batches[items[k]];
				if (b.Worlds.empty())
					s_Order.push_back(items[k]);
				b.Worlds.push_back(c.World);
			}
			++objects;
			if (pass == Pass::Shadow)
				RenderStats::AddShadowCaster();
		}
		if (s_Order.empty())
		{
			if (main)
				s_Stats[editor ? 1 : 0] = Stats{ 0, 0 };
			return;
		}
		// 본 패스는 재질끼리 모아 재질 적용 횟수를 줄인다
		if (main)
			std::sort(s_Order.begin(), s_Order.end(), [&](int a, int b) { return batches[a].Material.get() < batches[b].Material.get(); });

		// ---- 패스 값
		ID3D11DeviceContext* dc = Application::GetI()->GetDeviceContext();
		RenderManager* rm = RenderManager::GetI();
		const XMMATRIX viewProj = editor ? rm->EditorCameraViewProjectionMatrix : rm->CameraViewProjectionMatrix;
		ID3DX11EffectTechnique* tech = nullptr;
		ID3DX11Effect* fx = nullptr;
		switch (pass)
		{
		case Pass::Main:
		{
			static const XMMATRIX toTex(0.5f, 0.0f, 0.0f, 0.0f, 0.0f, -0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.5f, 0.5f, 0.0f, 1.0f);
			fx = Effects::InstancedBasicFX->GetFX();
			tech = fx->GetTechniqueByName("BatchTech");
			Effects::InstancedBasicFX->SetViewProj(viewProj);
			SetMatrix(fx, "gViewProjTex", viewProj * toTex);
			Effects::InstancedBasicFX->SetTexTransform(XMMatrixIdentity());
			break;
		}
		case Pass::Shadow:
			// gViewProj 는 ShadowRenderer 가 조각마다 설정해 둔다
			fx = Effects::BuildShadowMapFX->GetFX();
			tech = Effects::BuildShadowMapFX->BuildShadowMapInstancingTech.Get();
			Effects::BuildShadowMapFX->SetTexTransform(XMMatrixIdentity());
			break;
		case Pass::NormalDepth:
			fx = Effects::SsaoNormalDepthFX->GetFX();
			tech = fx->GetTechniqueByName("NormalDepthBatchTech");
			SetMatrix(fx, "gView", editor ? rm->EditorCameraViewMatrix : rm->CameraViewMatrix);
			SetMatrix(fx, "gWorldViewProj", viewProj);
			SetMatrix(fx, "gTexTransform", XMMatrixIdentity());
			break;
		}
		if (tech == nullptr || !tech->IsValid())
			return;

		dc->IASetInputLayout(InputLayouts::InstancedBasic.Get());
		dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		const UMaterial* applied = reinterpret_cast<const UMaterial*>(1);   // 아직 아무 재질도 적용 안 함
		int drawn = 0;
		for (int index : s_Order)
		{
			Batch* b = &batches[index];
			ID3D11Buffer* inst = Upload(dc, b->Worlds);
			if (inst == nullptr)
				continue;
			if (pass == Pass::Main && b->Material.get() != applied)
			{
				UMaterial::ApplyOrDefault(b->Material, Effects::InstancedBasicFX.get());
				applied = b->Material.get();
			}
			const UINT stride = sizeof(XMFLOAT4X4), offset = 0;
			dc->IASetVertexBuffers(1, 1, &inst, &stride, &offset);
			tech->GetPassByIndex(0)->Apply(0, dc);
			b->MeshPtr->ModelMesh.InstancingDraw(dc, b->Subset, (UINT)b->Worlds.size());
			++drawn;
		}
		ID3D11Buffer* none = nullptr;
		UINT zero = 0;
		dc->IASetVertexBuffers(1, 1, &none, &zero, &zero);
		if (pass == Pass::Main)
			s_Stats[editor ? 1 : 0] = Stats{ objects, drawn };
	}
}
