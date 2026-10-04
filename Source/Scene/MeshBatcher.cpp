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
#include "RenderLayers.h"
#include "CustomShaders.h"
#include "RenderStates.h"
#include "LODGroup.h"
#include "OcclusionCulling.h"
#include "EditorLog.h"
#include <unordered_map>

namespace
{
	struct Key
	{
		const Mesh* MeshPtr;
		int Subset;
		const UMaterial* Material;
		uint32 Layer;   // 본 패스만 (빛의 Culling Mask 가 묶음마다 같게), 깊이 = 0
		bool operator==(const Key& o) const { return MeshPtr == o.MeshPtr && Subset == o.Subset && Material == o.Material && Layer == o.Layer; }
	};
	struct KeyHash
	{
		size_t operator()(const Key& k) const
		{
			return std::hash<const void*>()(k.MeshPtr) ^ (std::hash<int>()(k.Subset) * 31) ^ (std::hash<const void*>()(k.Material) * 1099511628211ull) ^ ((size_t)k.Layer * 2654435761ull);
		}
	};

	struct Batch
	{
		Mesh* MeshPtr = nullptr;
		int Subset = 0;
		shared_ptr<UMaterial> Material;
		uint32 Layer = 0xFFFFFFFFu;   // gObjectLayer (레이어 비트)
		std::vector<XMFLOAT4X4> Worlds;
	};

	// 화면 하나 동안 쓰는 렌더러 목록 (BeginView 뒤 첫 Draw 가 만든다)
	struct Caster
	{
		const Component* Renderer;
		XMFLOAT4X4 World;
		int Cast;               // 0 On, 1 Off, 2 Two Sided, 3 Shadows Only
		uint32 LayerBit;        // 1 << GameObject 레이어 (Culling Mask)
		uint32_t First, Count;  // s_MainItems / s_DepthItems 범위 (서브셋마다 묶음 번호)
	};
	std::vector<Caster> s_Casters;
	std::vector<int> s_MainItems, s_DepthItems;
	// 묶음 배열: 본 패스 = (메시, 서브셋, 재질), 그림자·깊이 = (메시, 서브셋). 원소는 화면마다 다시 쓰고 Worlds 용량은 남긴다
	std::vector<Batch> s_MainBatches, s_DepthBatches;
	int s_MainCount = 0, s_DepthCount = 0;
	std::unordered_map<Key, int, KeyHash> s_MainIndex, s_DepthIndex;
	bool s_Collected = false;
	bool s_Capture = false;
	Scene* s_CollectedScene = nullptr;
	uint32_t s_CollectedFrame = 0;

	std::vector<int> s_Order;
	std::vector<const Caster*> s_Fading;   // LOD 크로스페이드 중 (묶지 않고 gLodFade 를 넣어 하나씩)
	MeshBatcher::Stats s_Stats[2];

	// 오클루전 컬링 (OcclusionCulling): 이 뷰의 키 (카메라) 와 단계
	const void* s_OccView = nullptr;
	enum class Occ { Off, Phase1, Ready, Failed };
	Occ s_Occ = Occ::Off;
	OcclusionCulling::Frame s_OccFrame;
	std::vector<int> s_GpuOrder[2];   // 후보가 있는 묶음 (깊이, 본 패스)
	int s_GpuObjects = 0;

	// 투명 재질 (CustomShaders::Transparent): 묶지 않고 물체마다 — 투명 패스에서 먼 것부터
	struct TransparentItem
	{
		int Caster;
		Mesh* MeshPtr;
		int Subset;
		shared_ptr<UMaterial> Material;
	};
	std::vector<TransparentItem> s_Transparent;

	const CustomShaders::Shader* CustomOf(const UMaterial* m)
	{
		return m && m->IsCustom() ? CustomShaders::Find(m->CustomShader()) : nullptr;
	}

	bool IsTransparent(const UMaterial* m)
	{
		const CustomShaders::Shader* cs = CustomOf(m);
		return cs && cs->DrawInstanced && cs->Transparent && cs->Transparent(*m);
	}

	// 깊이 · 그림자 패스에서도 재질이 필요한가 (잘라내기): 사용자 셰이더면 그 셰이더가, 아니면 엔진 Lit 의 Alpha Clipping (기본 그림이 있을 때)
	enum class Clip { None, Engine, Custom };
	Clip ClipOf(UMaterial* m)
	{
		if (m == nullptr)
			return Clip::None;
		if (const CustomShaders::Shader* cs = CustomOf(m); cs && cs->DrawInstanced)
			return cs->CustomDepth && cs->CustomDepth(*m) ? Clip::Custom : Clip::None;
		return m->GetPbr().AlphaClip && m->GetBaseMapSRV() ? Clip::Engine : Clip::None;
	}

	// 엔진 Alpha Clipping: 그림 알파와 비교할 값 (본 패스는 그림 × BaseColor 알파) · Tiling/Offset (SkinnedMeshRenderer 와 같은 식)
	float EngineCutoff(UMaterial& m)
	{
		return m.GetPbr().Cutoff / (std::max)(m.GetPbr().BaseColor.w, 1e-4f);
	}
	XMMATRIX EngineClipTexTransform(UMaterial& m)
	{
		const PbrMaterial& pbr = m.GetPbr();
		return XMMatrixScaling(pbr.Tiling.x, pbr.Tiling.y, 1.0f) * XMMatrixTranslation(pbr.Offset.x, pbr.Offset.y, 0.0f);
	}

	int BatchIndex(std::unordered_map<Key, int, KeyHash>& index, std::vector<Batch>& batches, int& count, Mesh* mesh, int subset, const shared_ptr<UMaterial>& mat, uint32 layer)
	{
		const Key key{ mesh, subset, mat.get(), layer };
		if (auto it = index.find(key); it != index.end())
			return it->second;
		if (count >= (int)batches.size())
			batches.emplace_back();
		Batch& b = batches[count];
		b.MeshPtr = mesh;
		b.Subset = subset;
		b.Material = mat;
		b.Layer = layer == 0 ? 0xFFFFFFFFu : layer;
		b.Worlds.clear();
		index.emplace(key, count);
		return count++;
	}

	// 씬을 한 번 훑는다: 켜진 Mesh Renderer 마다 월드 행렬과 서브셋별 묶음 번호
	void Collect(Scene* scene, bool editor)
	{
		// LOD Group: 이 뷰의 카메라로 LOD 를 골라 렌더러에 숨김 · 페이드를 매긴다 (그림자 · 깊이 · 본 패스 모두 같은 값)
		LODGroup::SelectForView(scene, editor, s_Capture);
		s_Casters.clear();
		s_MainItems.clear();
		s_DepthItems.clear();
		s_MainIndex.clear();
		s_DepthIndex.clear();
		s_Transparent.clear();
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
			c.LayerBit = 1u << (go->GetLayerIndex() & 31);
			c.First = (uint32_t)s_MainItems.size();
			c.Count = (uint32_t)mesh->Subsets.size();
			const auto& materials = mr->GetMaterials();
			for (int i = 0; i < (int)mesh->Subsets.size(); ++i)
			{
				const UINT matIndex = mesh->Subsets[i].MaterialIndex;
				static const shared_ptr<UMaterial> s_None;
				const shared_ptr<UMaterial>& mat = matIndex < materials.size() ? materials[matIndex] : s_None;
				if (IsTransparent(mat.get()))
				{
					// 투명: 본 패스 · 프리패스 · 그림자에서 빼고 투명 패스로
					s_MainItems.push_back(-1);
					s_DepthItems.push_back(-1);
					s_Transparent.push_back({ (int)s_Casters.size(), mesh.get(), i, mat });
					continue;
				}
				s_MainItems.push_back(BatchIndex(s_MainIndex, s_MainBatches, s_MainCount, mesh.get(), i, mat, c.LayerBit));
				// 깊이 · 그림자: 보통은 (메시, 서브셋) 만으로, 잘라내는 재질은 재질마다 (구멍이 본 패스와 같게)
				s_DepthItems.push_back(BatchIndex(s_DepthIndex, s_DepthBatches, s_DepthCount, mesh.get(), i, ClipOf(mat.get()) != Clip::None ? mat : s_None, 0));
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

	ComPtr<GfxBuffer> s_InstanceBuffer;
	UINT s_Capacity = 0;

	GfxBuffer* Upload(GfxContext* dc, const std::vector<XMFLOAT4X4>& worlds)
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

	void SetMatrix(FxEffect* fx, const char* name, CXMMATRIX m)
	{
		if (auto* v = fx->GetVariableByName(name)->AsMatrix(); v && v->IsValid())
			v->SetMatrix(reinterpret_cast<const float*>(&m));
	}
}

namespace MeshBatcher
{
	const Stats& LastStats(bool editor) { return s_Stats[editor ? 1 : 0]; }

	namespace
	{
		// 투명 패스: 먼 것부터 하나씩 (같은 메시 · 재질이라도 순서가 섞이면 안 된다), 알파 섞기 · 깊이 읽기만
		void DrawTransparent(bool editor)
		{
			if (s_Transparent.empty())
				return;
			RenderManager* rm = RenderManager::GetI();
			const XMMATRIX view = editor ? rm->EditorCameraViewMatrix : rm->CameraViewMatrix;
			const XMMATRIX viewProj = editor ? rm->EditorCameraViewProjectionMatrix : rm->CameraViewProjectionMatrix;
			struct Item { const TransparentItem* T; float Depth; };
			std::vector<Item> list;
			list.reserve(s_Transparent.size());
			for (const TransparentItem& t : s_Transparent)
			{
				const Caster& c = s_Casters[t.Caster];
				if (!SceneCulling::IsVisible(c.Renderer) || !(c.LayerBit & RenderLayers::ActiveMask()) || c.Cast == 3)
					continue;
				// 물체 중심의 뷰 깊이 (Unity 도 렌더러 중심 거리로 정렬)
				const XMVECTOR center = XMVector3TransformCoord(XMVectorSet(c.World._41, c.World._42, c.World._43, 1.0f), view);
				list.push_back({ &t, XMVectorGetZ(center) });
			}
			if (list.empty())
				return;
			std::stable_sort(list.begin(), list.end(), [](const Item& a, const Item& b) { return a.Depth > b.Depth; });
			GfxContext* dc = Application::GetI()->GetDeviceContext();
			const float blendFactor[4] = { 0, 0, 0, 0 };
			dc->OMSetBlendState(RenderStates::TransparentBS.Get(), blendFactor, 0xFFFFFFFF);
			dc->OMSetDepthStencilState(RenderStates::DepthReadDSS.Get(), 0);
			std::vector<XMFLOAT4X4> one(1);
			for (const Item& it : list)
			{
				const TransparentItem& t = *it.T;
				const CustomShaders::Shader* cs = CustomOf(t.Material.get());
				if (!cs || !cs->DrawInstanced)
					continue;
				one[0] = s_Casters[t.Caster].World;
				GfxBuffer* inst = Upload(dc, one);
				if (!inst)
					continue;
				CustomShaders::InstancedDraw d;
				d.Context = dc;
				d.Material = t.Material.get();
				d.ViewProj = viewProj;
				d.View = view;
				d.Editor = editor;
				d.LayerBit = s_Casters[t.Caster].LayerBit;
				d.Pass = CustomShaders::DrawPass::Transparent;
				d.Draw = [&]() {
					const UINT stride = sizeof(XMFLOAT4X4), offset = 0;
					dc->IASetInputLayout(InputLayouts::InstancedBasic.Get());
					dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
					dc->IASetVertexBuffers(1, 1, &inst, &stride, &offset);
					t.MeshPtr->ModelMesh.InstancingDraw(dc, t.Subset, 1);
				};
				cs->DrawInstanced(d);
			}
			GfxBuffer* none = nullptr;
			UINT zero = 0;
			dc->IASetVertexBuffers(1, 1, &none, &zero, &zero);
			dc->OMSetBlendState(nullptr, blendFactor, 0xFFFFFFFF);
			dc->OMSetDepthStencilState(nullptr, 0);
		}

		// 깊이 · 본 패스에 들어가는 렌더러 (그림자 패스는 Shadows Only 를 뺀 쪽 대신 Off 를 뺀다)
		bool Candidate(const Caster& c, Pass pass)
		{
			if (!SceneCulling::IsVisible(c.Renderer) || !(c.LayerBit & RenderLayers::ActiveMask()))
				return false;   // 절두체 · Camera / Light 의 Culling Mask
			return pass == Pass::Shadow ? c.Cast != 1 : c.Cast != 3;
		}
		bool Fading(const Caster& c)
		{
			return c.Renderer->LodStamp == SceneCulling::LodStamp && c.Renderer->LodFade > 0.0f;
		}

		// 오클루전 컬링 시작: 이 뷰의 후보 렌더러 (절두체 안, 크로스페이드 아님) 를 GPU 에 올리고 1 단계 목록을 만든다
		bool StartOcclusion(bool editor)
		{
			GfxContext* dc = Application::GetI()->GetDeviceContext();
			OcclusionCulling::Frame& f = s_OccFrame;
			f.Casters.clear();
			f.Worlds.clear();
			for (int s = 0; s < 2; ++s)
			{
				f.Items[s].clear();
				s_GpuOrder[s].clear();
			}
			if (s_OccView != nullptr)
			{
				for (int s = 0; s < 2; ++s)
				{
					const std::vector<Batch>& batches = s ? s_MainBatches : s_DepthBatches;
					const int count = s ? s_MainCount : s_DepthCount;
					f.Batches[s].resize(count);
					for (int i = 0; i < count; ++i)
					{
						const MeshGeometry::Subset& sub = batches[i].MeshPtr->ModelMesh.GetSubset(batches[i].Subset);
						f.Batches[s][i] = { sub.FaceCount * 3, sub.FaceStart * 3, (int32_t)sub.VertexStart, 0 };
					}
				}
				const uint32_t untracked = SceneCulling::SlotCount();   // 상자가 없는 렌더러: 늘 보임 (아주 큰 상자)
				for (const Caster& c : s_Casters)
				{
					if (!Candidate(c, Pass::Main) || Fading(c))
						continue;
					OcclusionCulling::Caster oc = {};
					Vec3 mn, mx;
					if (!SceneCulling::TrackedSlot(c.Renderer, oc.Slot, mn, mx))
					{
						oc.Slot = untracked;
						mn = Vec3(-1e30f, -1e30f, -1e30f);
						mx = Vec3(1e30f, 1e30f, 1e30f);
					}
					oc.Min[0] = mn.x; oc.Min[1] = mn.y; oc.Min[2] = mn.z;
					oc.Max[0] = mx.x; oc.Max[1] = mx.y; oc.Max[2] = mx.z;
					const uint32_t index = (uint32_t)f.Casters.size();
					f.Casters.push_back(oc);
					f.Worlds.insert(f.Worlds.end(), &c.World._11, &c.World._11 + 16);
					for (uint32_t k = c.First; k < c.First + c.Count; ++k)
					{
						if (s_DepthItems[k] >= 0) f.Items[0].push_back({ index, (uint32_t)s_DepthItems[k], 0 });
						if (s_MainItems[k] >= 0) f.Items[1].push_back({ index, (uint32_t)s_MainItems[k], 0 });
					}
				}
			}
			RenderManager* rm = RenderManager::GetI();
			XMFLOAT4X4 vp;
			XMStoreFloat4x4(&vp, editor ? rm->EditorCameraViewProjectionMatrix : rm->CameraViewProjectionMatrix);
			if (!OcclusionCulling::Begin(dc, s_OccView, editor, &vp._11, f, SceneCulling::SlotCount()))
				return false;
			// 후보가 있는 묶음만 그린다 (본 패스는 재질끼리 모아 재질 적용 횟수를 줄인다)
			for (int s = 0; s < 2; ++s)
				for (int i = 0; i < (int)f.Batches[s].size(); ++i)
					if (f.Batches[s][i].Candidates > 0)
						s_GpuOrder[s].push_back(i);
			std::sort(s_GpuOrder[1].begin(), s_GpuOrder[1].end(), [](int a, int b) {
				if (s_MainBatches[a].Material.get() != s_MainBatches[b].Material.get()) return s_MainBatches[a].Material.get() < s_MainBatches[b].Material.get();
				return s_MainBatches[a].Layer < s_MainBatches[b].Layer;
			});
			s_GpuObjects = (int)f.Casters.size();
			return true;
		}

		// 패스 하나. gpuSet >= 0 = 오클루전 컬링 목록 (GPU 가 고른 인스턴스, 간접 그리기), -1 = CPU 목록
		void DrawPass(Pass pass, bool editor, int gpuSet)
		{
			// ---- 이번 패스: 보이는 렌더러의 월드 행렬만 묶음에 쌓는다 (GPU 목록이면 크로스페이드만)
			const bool main = pass == Pass::Main;
			std::vector<Batch>& batches = main ? s_MainBatches : s_DepthBatches;
			const std::vector<int>& items = main ? s_MainItems : s_DepthItems;
			const int batchCount = main ? s_MainCount : s_DepthCount;
			s_Order.clear();
			s_Fading.clear();
			int objects = 0;
			if (gpuSet >= 0)
			{
				s_Order = s_GpuOrder[main ? 1 : 0];
				objects = s_GpuObjects;
				if (gpuSet != OcclusionCulling::DepthPhase2)   // 크로스페이드는 1 단계 · 본 패스에서 CPU 로
					for (const Caster& c : s_Casters)
						if (Candidate(c, pass) && Fading(c))
						{
							s_Fading.push_back(&c);
							++objects;
						}
			}
			else
			{
				for (int i = 0; i < batchCount; ++i)
					batches[i].Worlds.clear();
				for (const Caster& c : s_Casters)
				{
					if (!Candidate(c, pass))
						continue;
					if (pass != Pass::Shadow && Fading(c))
					{
						s_Fading.push_back(&c);   // 그림자는 LOD Group 이 고른 한 쪽만 (LodShadowHidden)
						++objects;
						continue;
					}
					for (uint32_t k = c.First; k < c.First + c.Count; ++k)
					{
						if (items[k] < 0)
							continue;   // 투명 (투명 패스)
						Batch& b = batches[items[k]];
						if (b.Worlds.empty())
							s_Order.push_back(items[k]);
						b.Worlds.push_back(c.World);
					}
					++objects;
					if (pass == Pass::Shadow)
						RenderStats::AddShadowCaster();
				}
				// 본 패스는 재질끼리 모아 재질 적용 횟수를 줄인다
				if (main)
					std::sort(s_Order.begin(), s_Order.end(), [&](int a, int b) {
						if (batches[a].Material.get() != batches[b].Material.get()) return batches[a].Material.get() < batches[b].Material.get();
						return batches[a].Layer < batches[b].Layer;
					});
			}
			if (s_Order.empty() && s_Fading.empty())
			{
				if (main)
					s_Stats[editor ? 1 : 0] = Stats{ 0, 0 };
				return;
			}

			// ---- 패스 값
			GfxContext* dc = Application::GetI()->GetDeviceContext();
			RenderManager* rm = RenderManager::GetI();
			const XMMATRIX viewProj = editor ? rm->EditorCameraViewProjectionMatrix : rm->CameraViewProjectionMatrix;
			FxTechnique* tech = nullptr;
			FxEffect* fx = nullptr;
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
			default:
				break;
			}
			if (tech == nullptr || !tech->IsValid())
				return;

			dc->IASetInputLayout(InputLayouts::InstancedBasic.Get());
			dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			const UMaterial* applied = reinterpret_cast<const UMaterial*>(1);   // 아직 아무 재질도 적용 안 함
			uint32 appliedLayer = 0;
			int drawn = 0;
			// gpu = GPU 목록의 묶음 번호 (-1 = b->Worlds 를 올려 그린다)
			auto drawBatch = [&](Batch* b, int gpu)
			{
				GfxBuffer* inst = nullptr;
				if (gpu < 0 && (inst = Upload(dc, b->Worlds)) == nullptr)
					return;
				auto drawInstances = [&]()
				{
					if (gpu >= 0)
						OcclusionCulling::DrawIndirect(dc, (OcclusionCulling::Set)gpuSet, (uint32_t)gpu, b->MeshPtr->ModelMesh, b->Subset);
					else
					{
						const UINT stride = sizeof(XMFLOAT4X4), offset = 0;
						dc->IASetVertexBuffers(1, 1, &inst, &stride, &offset);
						b->MeshPtr->ModelMesh.InstancingDraw(dc, b->Subset, (UINT)b->Worlds.size());
					}
				};
				// 패키지 · Shader Graph 셰이더 (CustomShaders::DrawInstanced): 그 셰이더가 값을 넣고 그린다
				if (pass == Pass::Main && b->Material && b->Material->IsCustom())
					if (const CustomShaders::Shader* cs = CustomShaders::Find(b->Material->CustomShader()); cs && cs->DrawInstanced)
					{
						CustomShaders::InstancedDraw d;
						d.Context = dc;
						d.Material = b->Material.get();
						d.ViewProj = viewProj;
						d.Editor = editor;
						d.LayerBit = b->Layer;
						d.Draw = [&]() {
							dc->IASetInputLayout(InputLayouts::InstancedBasic.Get());
							dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
							drawInstances();
						};
						cs->DrawInstanced(d);
						applied = reinterpret_cast<const UMaterial*>(1);   // 다음 엔진 묶음은 재질 · 레이어를 다시
						appliedLayer = 0;
						++drawn;
						return;
					}
				// 깊이 · 그림자: 잘라내는 재질 (사용자 셰이더 → 그 셰이더, 엔진 Lit → 그림 알파로 자르는 기법)
				if (pass != Pass::Main && b->Material)
				{
					const Clip clip = ClipOf(b->Material.get());
					if (clip == Clip::Custom)
					{
						const CustomShaders::Shader* cs = CustomOf(b->Material.get());
						CustomShaders::InstancedDraw d;
						d.Context = dc;
						d.Material = b->Material.get();
						d.Editor = editor;
						d.Pass = pass == Pass::Shadow ? CustomShaders::DrawPass::Shadow : CustomShaders::DrawPass::NormalDepth;
						d.ViewProj = pass == Pass::Shadow ? rm->LightViewProjection : viewProj;
						d.View = editor ? rm->EditorCameraViewMatrix : rm->CameraViewMatrix;
						d.Draw = [&]() {
							dc->IASetInputLayout(InputLayouts::InstancedBasic.Get());
							dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
							drawInstances();
						};
						cs->DrawInstanced(d);
						++drawn;
						return;
					}
					if (clip == Clip::Engine)
					{
						UMaterial& m = *b->Material;
						FxTechnique* clipTech = nullptr;
						if (pass == Pass::Shadow)
						{
							Effects::BuildShadowMapFX->SetDiffuseMap(m.GetBaseMapSRV());
							Effects::BuildShadowMapFX->SetAlphaCutoff(EngineCutoff(m));
							Effects::BuildShadowMapFX->SetTexTransform(EngineClipTexTransform(m));
							clipTech = Effects::BuildShadowMapFX->BuildShadowMapAlphaClipInstancingTech.Get();
						}
						else
						{
							Effects::SsaoNormalDepthFX->SetDiffuseMap(m.GetBaseMapSRV());
							Effects::SsaoNormalDepthFX->SetAlphaCutoff(EngineCutoff(m));
							SetMatrix(fx, "gTexTransform", EngineClipTexTransform(m));
							clipTech = fx->GetTechniqueByName("NormalDepthAlphaClipBatchTech");
						}
						if (clipTech && clipTech->IsValid())
						{
							dc->IASetInputLayout(InputLayouts::InstancedBasic.Get());
							clipTech->GetPassByIndex(0)->Apply(0, dc);
							drawInstances();
							++drawn;
						}
						// 다음 묶음 (보통 재질) 을 위해 되돌린다
						if (pass == Pass::Shadow)
						{
							Effects::BuildShadowMapFX->SetTexTransform(XMMatrixIdentity());
							Effects::BuildShadowMapFX->SetAlphaCutoff(0.0f);
						}
						else
						{
							SetMatrix(fx, "gTexTransform", XMMatrixIdentity());
							Effects::SsaoNormalDepthFX->SetAlphaCutoff(0.0f);
						}
						return;
					}
				}
				if (pass == Pass::Main && b->Material.get() != applied)
				{
					UMaterial::ApplyOrDefault(b->Material, Effects::InstancedBasicFX.get());
					applied = b->Material.get();
				}
				if (pass == Pass::Main && b->Layer != appliedLayer)
				{
					RenderLayers::SetObjectLayer(Effects::InstancedBasicFX.get(), b->Layer);   // Light.cullingMask
					appliedLayer = b->Layer;
				}
				tech->GetPassByIndex(0)->Apply(0, dc);
				drawInstances();
				++drawn;
			};
			for (int index : s_Order)
				drawBatch(&batches[index], gpuSet >= 0 ? index : -1);

			// LOD 크로스페이드 중인 렌더러: 서브셋마다 하나씩, gLodFade 로 화면 디더 (깊이 프리패스와 본 패스가 같은 무늬 — EQUAL 깊이 검사가 맞는다)
			if (!s_Fading.empty())
			{
				FxEffect* fadeFx = main ? Effects::InstancedBasicFX->GetFX() : Effects::SsaoNormalDepthFX->GetFX();
				auto* fadeVar = fadeFx->GetVariableByName("gLodFade")->AsVector();
				Batch one;
				for (const Caster* c : s_Fading)
				{
					const float fade[4] = { c->Renderer->LodFade, c->Renderer->LodFadeBelow ? 1.0f : 0.0f, 1.0f, 0.0f };
					if (fadeVar && fadeVar->IsValid())
						fadeVar->SetFloatVector(fade);
					for (uint32_t k = c->First; k < c->First + c->Count; ++k)
					{
						if (items[k] < 0)
							continue;
						const Batch& src = batches[items[k]];
						one.MeshPtr = src.MeshPtr;
						one.Subset = src.Subset;
						one.Material = src.Material;
						one.Layer = src.Layer;
						one.Worlds.assign(1, c->World);
						drawBatch(&one, -1);
					}
				}
				const float off[4] = { 0, 0, 0, 0 };
				if (fadeVar && fadeVar->IsValid())
					fadeVar->SetFloatVector(off);
			}
			GfxBuffer* none = nullptr;
			UINT zero = 0;
			dc->IASetVertexBuffers(1, 1, &none, &zero, &zero);
			if (pass == Pass::Main)
			{
				s_Stats[editor ? 1 : 0] = Stats{ objects, drawn };
				RenderLayers::SetObjectLayer(Effects::InstancedBasicFX.get(), ~0u);   // 다른 그리기는 모든 빛
			}
		}
	}

	void BeginView(bool capture, const void* occlusionView)
	{
		s_Collected = false;
		s_Capture = capture;
		s_OccView = capture ? nullptr : occlusionView;
		s_Occ = Occ::Off;
	}

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
			Collect(scene, editor);
			s_Occ = Occ::Off;
		}
		if (pass == Pass::Transparent)
		{
			DrawTransparent(editor);
			return;
		}
		// 오클루전 컬링: 깊이 프리패스 1 단계 (지난 프레임에 보인 것) → FinishDepthPrepass → 본 패스 (지금 보이는 것)
		if (pass == Pass::NormalDepth && s_Occ == Occ::Off && !s_Capture)
		{
			PROFILE_SCOPE("MeshBatcher.Occlusion");
			s_Occ = StartOcclusion(editor) ? Occ::Phase1 : Occ::Failed;
			if (s_Occ == Occ::Phase1)
			{
				DrawPass(pass, editor, OcclusionCulling::DepthPhase1);
				return;
			}
		}
		if (pass == Pass::Main && s_Occ == Occ::Ready)
		{
			DrawPass(pass, editor, OcclusionCulling::Main);
			return;
		}
		if (pass == Pass::Main && s_Occ == Occ::Phase1)
			EditorLog::Write("Occlusion", "main pass before FinishDepthPrepass — CPU culling (depth prepass is incomplete)");
		DrawPass(pass, editor, -1);
	}

	void FinishDepthPrepass(Scene* scene, bool editor)
	{
		if (scene == nullptr || s_Occ != Occ::Phase1 || s_CollectedScene != scene)
			return;
		PROFILE_SCOPE("MeshBatcher.Occlusion");
		PROFILE_GPU("Occlusion Culling");
		OcclusionCulling::Finish(Application::GetI()->GetDeviceContext());
		s_Occ = Occ::Ready;
		DrawPass(Pass::NormalDepth, editor, OcclusionCulling::DepthPhase2);   // 새로 보인 렌더러의 깊이
	}
}
