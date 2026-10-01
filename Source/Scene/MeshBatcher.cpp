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

	std::unordered_map<Key, Batch, KeyHash> s_Batches;
	std::vector<Batch*> s_Order;
	MeshBatcher::Stats s_Stats[2];

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

	void Draw(Scene* scene, Pass pass, bool editor)
	{
		if (scene == nullptr)
			return;
		PROFILE_SCOPE("MeshBatcher");
		PROFILE_GPU("Mesh Renderers");

		// ---- 모으기 (월드 행렬만 쌓는다)
		for (auto& kv : s_Batches)
			kv.second.Worlds.clear();
		s_Order.clear();
		int objects = 0;
		for (GameObject* go : scene->GetAllGameObjects())
		{
			if (go == nullptr || !go->IsActive())
				continue;
			MeshRenderer* mr = go->GetComponent<MeshRenderer>();
			if (mr == nullptr || !mr->IsEnabled() || !SceneCulling::IsVisible(mr))
				continue;
			const int cast = mr->GetCastShadows();   // 0 On, 1 Off, 2 Two Sided, 3 Shadows Only
			if ((pass == Pass::Shadow && cast == 1) || (pass != Pass::Shadow && cast == 3))
				continue;
			auto mesh = mr->GetMesh();
			if (!mesh || mesh->Subsets.empty())
				continue;
			XMFLOAT4X4 world;
			XMStoreFloat4x4(&world, go->GetTransform()->GetWorldMatrix());
			const auto& materials = mr->GetMaterials();
			for (int i = 0; i < (int)mesh->Subsets.size(); ++i)
			{
				shared_ptr<UMaterial> mat;
				if (pass == Pass::Main)
				{
					const UINT matIndex = mesh->Subsets[i].MaterialIndex;
					mat = matIndex < materials.size() ? materials[matIndex] : nullptr;
				}
				const Key key{ mesh.get(), i, mat.get() };
				Batch& b = s_Batches[key];
				if (b.Worlds.empty())
				{
					b.MeshPtr = mesh.get();
					b.Subset = i;
					b.Material = mat;
					s_Order.push_back(&b);
				}
				b.Worlds.push_back(world);
			}
			++objects;
			if (pass == Pass::Shadow)
				RenderStats::AddShadowCaster();
		}
		// 쓰이지 않는 묶음은 버린다 (메시·재질 포인터가 사라졌을 수 있다)
		for (auto it = s_Batches.begin(); it != s_Batches.end();)
			it = it->second.Worlds.empty() ? s_Batches.erase(it) : std::next(it);
		if (s_Order.empty())
		{
			if (pass == Pass::Main)
				s_Stats[editor ? 1 : 0] = Stats{ 0, 0 };
			return;
		}
		// 본 패스는 재질끼리 모아 재질 적용 횟수를 줄인다
		if (pass == Pass::Main)
			std::sort(s_Order.begin(), s_Order.end(), [](const Batch* a, const Batch* b) { return a->Material.get() < b->Material.get(); });

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
		int batches = 0;
		for (Batch* b : s_Order)
		{
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
			++batches;
		}
		ID3D11Buffer* none = nullptr;
		UINT zero = 0;
		dc->IASetVertexBuffers(1, 1, &none, &zero, &zero);
		if (pass == Pass::Main)
			s_Stats[editor ? 1 : 0] = Stats{ objects, batches };
	}
}
