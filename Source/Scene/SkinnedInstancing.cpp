#include "pch.h"
#include "SkinnedInstancing.h"
#include <optional>
#include "SkinnedMeshRenderer.h"
#include "SkinnedMesh.h"
#include "SkinnedLod.h"
#include "CrowdAnimation.h"
#include "SceneCulling.h"
#include "SceneManager.h"
#include "Scene.h"
#include "GameObject.h"
#include "Transform.h"
#include "UMaterial.h"
#include "Effects.h"
#include "Vertex.h"
#include "RenderManager.h"
#include "RenderStats.h"
#include "RenderLayers.h"
#include "Profiler.h"
#include "JobSystem.h"
#include "EditorLog.h"
#include "CustomShaders.h"

namespace SkinnedInstancing
{
	bool Enabled = true;

	// ================================================================ 인스턴스 버퍼 · 쓰기 (66. SkinInstancing.fx 와 같은 꼴)
	bool InstanceBuffer::Upload(const XMFLOAT4* data, size_t count)
	{
		GfxContext* dc = Application::GetI()->GetDeviceContext();
		GfxDevice* device = Application::GetI()->GetDevice();
		if (!dc || !device)
			return false;
		const UINT need = (UINT)(std::max)(count, (size_t)1);
		if (!Buffer || Elements < need)
		{
			const UINT elements = (std::max)(need + need / 2, 1024u);
			D3D11_BUFFER_DESC d = {};
			d.ByteWidth = elements * 16;
			d.Usage = D3D11_USAGE_DYNAMIC;
			d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			d.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
			d.StructureByteStride = 16;
			ComPtr<GfxBuffer> b;
			ComPtr<GfxShaderResourceView> v;
			if (FAILED(device->CreateBuffer(&d, nullptr, b.GetAddressOf())))
				return false;
			D3D11_SHADER_RESOURCE_VIEW_DESC s = {};
			s.Format = DXGI_FORMAT_UNKNOWN;
			s.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
			s.Buffer.NumElements = elements;
			if (FAILED(device->CreateShaderResourceView(b.Get(), &s, v.GetAddressOf())))
				return false;
			Buffer = b;
			Srv = v;
			Elements = elements;
		}
		D3D11_MAPPED_SUBRESOURCE m;
		if (FAILED(dc->Map(Buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
			return false;
		if (count > 0)
			memcpy(m.pData, data, count * 16);
		dc->Unmap(Buffer.Get(), 0);
		return true;
	}

	namespace
	{
		// 인스턴스 하나 (CrowdInstanceStride 칸) 를 dst 에
		void WriteInstanceTo(XMFLOAT4* dst, const XMFLOAT4X4& world, uint32_t palA, uint32_t palB, float lerp, float row,
			const XMFLOAT4X4& prevWorld, uint32_t prevA, uint32_t prevB, float prevLerp, uint32_t flags, const XMFLOAT4& tint)
		{
			auto columns = [&](const XMFLOAT4X4& m) {
				*dst++ = XMFLOAT4(m._11, m._21, m._31, m._41);
				*dst++ = XMFLOAT4(m._12, m._22, m._32, m._42);
				*dst++ = XMFLOAT4(m._13, m._23, m._33, m._43);
			};
			auto bits = [](uint32_t u) { float f; memcpy(&f, &u, 4); return f; };   // 셰이더 asuint
			columns(world);
			*dst++ = XMFLOAT4(bits(palA), bits(palB), lerp, row);
			columns(prevWorld);
			*dst++ = XMFLOAT4(bits(prevA), bits(prevB), prevLerp, bits(flags));
			*dst++ = tint;
		}
	}

	void WriteInstance(std::vector<XMFLOAT4>& out, const XMFLOAT4X4& world, uint32_t palA, uint32_t palB, float lerp, float row,
		const XMFLOAT4X4& prevWorld, uint32_t prevA, uint32_t prevB, float prevLerp, uint32_t flags, const XMFLOAT4& tint)
	{
		const size_t at = out.size();
		out.resize(at + CrowdInstanceStride);
		WriteInstanceTo(&out[at], world, palA, palB, lerp, row, prevWorld, prevA, prevB, prevLerp, flags, tint);
	}


	namespace
	{
		const XMFLOAT4 kWhite(1, 1, 1, 1);

		// 팔레트는 두 벌 (이번 · 지난 프레임) — 모션 벡터가 지난 것을 GPU 에서 읽는다
		InstanceBuffer s_Palettes[2], s_Instances;
		int s_PalIdx = 0;
		std::vector<XMFLOAT4> s_PaletteData, s_InstData;
		std::vector<SkinnedMeshRenderer*> s_Cand, s_All;
		std::vector<int> s_Check;
		uint32_t s_PaletteFrame = ~0u;
		bool s_PaletteOk = false;
		InstanceBuffer& CurPalette() { return s_Palettes[s_PalIdx]; }
		InstanceBuffer& PrevPalette() { return s_Palettes[s_PalIdx ^ 1]; }

		struct MotionEntry { SkinnedMeshRenderer* R; bool Still; bool UsePrev; };
		std::vector<MotionEntry> s_Motion;

		struct Item
		{
			SkinnedMeshRenderer* R;
			MeshGeometry* Geo;
			CrowdAnimation::Baked* Impostor;   // 있으면 임포스터로
		};
		std::vector<Item> s_Items[3];
		// 지난 Flush 가 묶은 모음: 다음 패스 (깊이 프리패스 → 본 패스) 가 같은 프레임에 같은 렌더러 · 지오메트리 · 임포스터를 모으면
		//  묶기 · 인스턴스 쓰기 · 올리기를 건너뛰고 s_Groups 와 s_Instances 를 그대로 쓴다 (군중 1 만 명: 패스마다 10 ms)
		std::vector<Item> s_PrevItems;
		uint32_t s_PrevItemsFrame = ~0u;
		bool s_InstancesHoldGroups = false;   // s_Instances · s_Groups 가 s_PrevItems 의 것 (모션 벡터가 덮으면 false)
		bool SameItems(const std::vector<Item>& a, const std::vector<Item>& b)
		{
			if (a.size() != b.size())
				return false;
			for (size_t i = 0; i < a.size(); ++i)
				if (a[i].R != b[i].R || a[i].Geo != b[i].Geo || a[i].Impostor != b[i].Impostor)
					return false;
			return true;
		}

		// 마지막 프레임 통계 (CLI crowd info)
		struct Stats { int Palettes = 0, Instances[4] = {}, Draws[4] = {}, Impostors = 0; };
		Stats s_Cur, s_Last;
		uint32_t s_StatsFrame = ~0u;
		void RollStats(uint32_t frame)
		{
			if (s_StatsFrame == frame)
				return;
			s_Last = s_Cur;
			s_Cur = Stats();
			s_StatsFrame = frame;
		}

		bool PassReady(Pass pass, bool impostor);

		void WritePalette(XMFLOAT4* out, const std::vector<XMFLOAT4X4>& bones)
		{
			// 본 행렬 (행 벡터 곱) 의 열 0 · 1 · 2 — 셰이더는 dot(float4(p, 1), 열)
			for (const XMFLOAT4X4& m : bones)
			{
				*out++ = XMFLOAT4(m._11, m._21, m._31, m._41);
				*out++ = XMFLOAT4(m._12, m._22, m._32, m._42);
				*out++ = XMFLOAT4(m._13, m._23, m._33, m._43);
			}
		}

		// 프레임마다 한 번: 이번 프레임에 그릴 만한 렌더러 (지난 카메라 · 그림자 컬링에 보인 것) 의 팔레트 · 월드
		//  — 이번 패스에 처음 보이는데 팔레트가 없는 렌더러는 예전처럼 따로 그린다 (Add 가 false)
		//  자리 매기기는 메인 (재질 확인 · 처음 본 팔레트 만들기), 팔레트 · 월드 쓰기는 작업 스레드로 나눈다
		void EnsurePalettes()
		{
			const uint32_t frame = SceneCulling::FrameIndex();
			RollStats(frame);
			if (s_PaletteFrame == frame)
				return;
			PROFILE_SCOPE("SkinnedInstancing.Palettes");
			const uint32_t prevFrame = s_PaletteFrame;
			s_PaletteFrame = frame;
			s_PalIdx ^= 1;
			s_Cand.clear();
			s_All.clear();
			size_t elements = 0;
			// 1) 이번 프레임 씬 · 보였던 스킨 렌더러 (메인 — 컬링 자리를 차례로)
			SceneCulling::EntryView e;
			for (size_t i = 0, n = SceneCulling::EntryCount(); i < n; ++i)
			{
				if (!SceneCulling::EntryAt(i, e) || !e.Skinned)
					continue;
				SkinnedMeshRenderer* r = static_cast<SkinnedMeshRenderer*>(e.Renderer);
				if (SceneCulling::Enabled && r->CullStamp != SceneCulling::Stamp && r->ShadowCullStamp != SceneCulling::ShadowStamp)
					continue;
				s_All.push_back(r);
			}
			// 2) 대상인지 (작업 스레드 — 재질 · 메시 읽기만). 재질 블록이 있는 렌더러는 메인에서
			s_Check.assign(s_All.size(), 0);
			Jobs::ParallelFor((int)s_All.size(), 128, [&](int b, int e2) {
				for (int i = b; i < e2; ++i)
				{
					SkinnedMeshRenderer* r = s_All[(size_t)i];
					GameObject* go = r->GetGameObject();
					if (go != nullptr && go->CullSceneStamp == frame && go->IsActiveInHierarchy())
						s_Check[(size_t)i] = r->InstanceCheck();
				}
			}, "SkinnedInstancing Check");
			// 3) 자리 매기기 (메인)
			for (size_t i = 0; i < s_All.size(); ++i)
			{
				SkinnedMeshRenderer* r = s_All[i];
				if (s_Check[i] == 0 || (s_Check[i] == 2 && !r->CanInstance()))
					continue;
				const std::vector<XMFLOAT4X4>& bones = r->MotionPalette();   // 처음이면 바인드 포즈로 채운다
				if (bones.empty())
					continue;
				// 임포스터로만 그리는 렌더러 (그림자도 임포스터 — 또는 그림자에 안 보임) 는 팔레트가 필요 없다 — 자리만 (월드 · 클립 시간으로 그린다)
				const bool impostorOnly = r->CurrentLod() == SkinnedLod::kImpostorLevel &&
					(PassReady(Pass::Shadow, true) || !(SceneCulling::Enabled ? r->ShadowCullStamp == SceneCulling::ShadowStamp : true));
				const bool hadPrev = prevFrame != ~0u && r->InstPaletteFrame == prevFrame && !r->InstNoPalette;   // 지난 프레임에 팔레트를 썼다
				r->InstPrevPalette = hadPrev && !impostorOnly ? r->InstPalette : ~0u;
				r->InstPoseChanged = r->PoseSerial != r->InstPoseSerial;
				r->InstPoseSerial = r->PoseSerial;
				r->InstPalette = (uint32_t)elements;
				r->InstPaletteFrame = frame;
				if (!impostorOnly)
					elements += bones.size() * 3;
				r->InstNoPalette = impostorOnly;
				s_Cand.push_back(r);
			}
			s_Cur.Palettes += (int)s_Cand.size();
			s_PaletteData.resize(elements);
			Jobs::ParallelFor((int)s_Cand.size(), 64, [&](int b, int e2) {
				for (int i = b; i < e2; ++i)
				{
					SkinnedMeshRenderer* r = s_Cand[(size_t)i];
					if (!r->InstNoPalette)
						WritePalette(&s_PaletteData[r->InstPalette], r->GetFinalTransforms());
					const bool hadPrev = r->InstPrevPalette != ~0u;
					if (hadPrev)
						r->InstPrevWorld = r->InstWorld;
					XMStoreFloat4x4(&r->InstWorld, r->GetGameObject()->GetTransform()->GetWorldMatrix());
					if (!hadPrev)
						r->InstPrevWorld = r->InstWorld;
				}
			}, "SkinnedInstancing Palettes");
			s_PaletteOk = CurPalette().Upload(s_PaletteData.data(), s_PaletteData.size());
		}

		FxVar* Var(FxEffect* fx, const char* name)
		{
			FxVar* v = fx ? fx->GetVariableByName(name) : nullptr;
			return v && v->IsValid() ? v : nullptr;
		}
		void SetBase(FxEffect* fx, uint32_t base)
		{
			if (FxVar* v = Var(fx, "gSkinInstanceBase"))
				v->SetRawValue(&base, 0, 4);
		}
		void SetVec(FxEffect* fx, const char* name, const XMFLOAT4& v)
		{
			if (FxVar* var = Var(fx, name))
				var->SetFloatVector(&v.x);
		}
		void SetMatrix(FxEffect* fx, const char* name, CXMMATRIX m)
		{
			if (FxVar* v = Var(fx, name))
				v->SetMatrix(reinterpret_cast<const float*>(&m));
		}
		void BindBuffers(FxEffect* fx, GfxShaderResourceView* instances, GfxShaderResourceView* palettes, GfxShaderResourceView* prev)
		{
			if (FxVar* v = Var(fx, "gSkinInstances")) v->SetResource(instances);
			if (FxVar* v = Var(fx, "gSkinPalettes")) v->SetResource(palettes);
			if (FxVar* v = Var(fx, "gSkinPrevPalettes")) v->SetResource(prev);
		}
		// gSkinView: 본 · 깊이 패스 = 카메라 위치 (w 0), 그림자 = 지금 그리는 조각의 빛 (방향광은 빛 쪽 방향 w 1, 스포트 · 점광은 위치 w 0)
		void BindImpostor(FxEffect* fx, const CrowdAnimation::Baked* b, CXMMATRIX view, bool shadow)
		{
			if (FxVar* v = Var(fx, "gSkinImpAlbedo")) v->SetResource(b ? b->ImpostorAlbedo.Get() : nullptr);
			if (FxVar* v = Var(fx, "gSkinImpNormal")) v->SetResource(b ? b->ImpostorNormal.Get() : nullptr);
			if (!b)
				return;
			XMFLOAT4 from;
			if (shadow)
			{
				const XMFLOAT4 l = CustomShaders::CurrentShadow().Light;
				from = XMFLOAT4(l.x, l.y, l.z, l.w > 0.5f ? 0.0f : 1.0f);
			}
			else
			{
				XMVECTOR det;
				const XMMATRIX inv = XMMatrixInverse(&det, view);
				XMStoreFloat4(&from, inv.r[3]);
				from.w = 0.0f;
			}
			SetVec(fx, "gSkinImpostor", XMFLOAT4((float)CrowdAnimation::Baked::ImpostorYaws, (float)b->ImpostorRows, b->HalfWidth, b->Height * 0.5f));
			SetVec(fx, "gSkinImpostor2", XMFLOAT4(b->CenterY, 0.5f, 0.0f, 0.0f));
			SetVec(fx, "gSkinView", from);
		}

		// 묶음: 같은 지오메트리 (또는 임포스터) · 재질 목록 · 레이어 (본 패스의 빛 Culling Mask)
		struct Group
		{
			MeshGeometry* Geo = nullptr;
			CrowdAnimation::Baked* Impostor = nullptr;
			const std::vector<shared_ptr<UMaterial>>* Materials = nullptr;
			uint32 Layer = 0;
			std::vector<SkinnedMeshRenderer*> Renderers;
			uint32_t Base = 0;
		};
		bool SameMaterials(const std::vector<shared_ptr<UMaterial>>& a, const std::vector<shared_ptr<UMaterial>>& b)
		{
			if (&a == &b)
				return true;
			if (a.size() != b.size())
				return false;
			for (size_t i = 0; i < a.size(); ++i)
				if (a[i].get() != b[i].get())
					return false;
			return true;
		}
		std::vector<Group> s_Groups;
		// Flush 의 렌더러마다: 묶음 키 · 묶음 번호 · 묶음 안 차례
		struct Keyed { uint64_t Key = 0; uint32 Layer = 0; uint32_t Group = 0; uint32_t Slot = 0; bool Deferred = false; };
		std::vector<Keyed> s_Keyed;
		// 키 충돌 때: 재질 목록을 직접 비교해 다시 묶는다 (s_Keyed 의 Group · Slot 을 고친다)
		void GroupExact(const std::vector<Item>& items)
		{
			s_Groups.clear();
			for (size_t i = 0; i < items.size(); ++i)
			{
				const Item& it = items[i];
				Keyed& k = s_Keyed[i];
				const auto& mats = it.R->DrawMaterials();
				int gi = -1;
				for (size_t c = 0; c < s_Groups.size() && gi < 0; ++c)
				{
					const Group& g = s_Groups[c];
					if (g.Impostor == it.Impostor && (it.Impostor || g.Geo == it.Geo) && g.Layer == k.Layer && SameMaterials(*g.Materials, mats))
						gi = (int)c;
				}
				if (gi < 0)
				{
					gi = (int)s_Groups.size();
					s_Groups.push_back(Group());
					Group& g = s_Groups.back();
					g.Geo = it.Geo;
					g.Impostor = it.Impostor;
					g.Materials = &mats;
					g.Layer = k.Layer;
				}
				k.Group = (uint32_t)gi;
				k.Slot = (uint32_t)s_Groups[(size_t)gi].Renderers.size();
				s_Groups[(size_t)gi].Renderers.push_back(it.R);
			}
		}

		// 패스의 기법 (셰이더를 다시 읽는 중 · 아직 컴파일 중이면 없다 → 렌더러마다 그린다)
		FxEffect* PassEffect(Pass pass)
		{
			switch (pass)
			{
			case Pass::Main: return Effects::InstancedBasicFX ? Effects::InstancedBasicFX->GetFX() : nullptr;
			case Pass::Shadow: return Effects::BuildShadowMapFX ? Effects::BuildShadowMapFX->GetFX() : nullptr;
			case Pass::NormalDepth: return Effects::SsaoNormalDepthFX ? Effects::SsaoNormalDepthFX->GetFX() : nullptr;
			}
			return nullptr;
		}
		FxTechnique* Tech(FxEffect* fx, const char* name)
		{
			FxTechnique* t = fx ? fx->GetTechniqueByName(name) : nullptr;
			return t && t->IsValid() ? t : nullptr;
		}
		FxTechnique* PassTech(Pass pass, bool impostor)
		{
			static const char* kMesh[3] = { "SkinnedInstancedTech", "BuildShadowMapSkinnedInstancedTech", "NormalDepthSkinnedInstancedTech" };
			static const char* kImp[3] = { "SkinnedImpostorTech", "BuildShadowMapSkinnedImpostorTech", "NormalDepthSkinnedImpostorTech" };
			const char* name = impostor ? kImp[(int)pass] : kMesh[(int)pass];
			return name ? Tech(PassEffect(pass), name) : nullptr;
		}
		// 프레임마다 한 번 확인
		bool PassReady(Pass pass, bool impostor)
		{
			static uint32_t s_Frame[2][3] = { { ~0u, ~0u, ~0u }, { ~0u, ~0u, ~0u } };
			static bool s_Ready[2][3] = {};
			const uint32_t frame = SceneCulling::FrameIndex();
			const int k = impostor ? 1 : 0;
			if (s_Frame[k][(int)pass] != frame)
			{
				s_Frame[k][(int)pass] = frame;
				s_Ready[k][(int)pass] = PassTech(pass, impostor) != nullptr;
			}
			return s_Ready[k][(int)pass];
		}

		// 자동 임포스터: 렌더러가 재생 중인 클립 (없으면 지금 자세 대신 바인드 포즈) 의 아틀라스. 렌더러마다 마지막 것을 기억 (프레임마다 찾지 않게)
		CrowdAnimation::Baked* ImpostorFor(SkinnedMeshRenderer* r)
		{
			const AnimationClip* clip = r->AnimClip.get();
			const auto& mats = r->DrawMaterials();
			if (r->InstImpostor && r->InstImpostorClip == clip && r->InstImpostorMats == (const void*)mats.data())
				return r->InstImpostor->ImpostorAlbedo ? r->InstImpostor.get() : nullptr;
			CrowdAnimation::ClipSource src;
			src.Name = "Clip";
			src.Loop = r->AnimLoop;
			src.Source = r->AnimClip;
			auto baked = CrowdAnimation::Bake(r->GetMesh(), r->GetSkeleton(), mats, { src }, r->GetBindMode());
			r->InstImpostor = baked;
			r->InstImpostorClip = clip;
			r->InstImpostorMats = (const void*)mats.data();
			if (!baked)
				return nullptr;
			if (!baked->ImpostorTried)
				BakeImpostor(*baked);
			return baked->ImpostorAlbedo ? baked.get() : nullptr;
		}
	}

	// ================================================================ 임포스터 굽기 (CrowdAnimation.h)
	//  클립마다 ImpostorFrames 줄 × 8 방향 칸 — 물체 공간 (단위 월드) 정사영으로 알베도 · 법선 (TreeRenderer::BakeImpostor 와 같은 방식)
	bool BakeImpostor(CrowdAnimation::Baked& baked)
	{
		using CrowdAnimation::Baked;
		using CrowdAnimation::Clip;
		baked.ImpostorTried = true;
		FxEffect* fx = Effects::InstancedBasicFX ? Effects::InstancedBasicFX->GetFX() : nullptr;
		FxTechnique* tech = Tech(fx, "SkinnedBakeTech");
		if (!tech || !baked.Mesh || !baked.PaletteSrv || baked.Clips.empty())
			return false;
		const auto t0 = std::chrono::steady_clock::now();
		GfxDevice* device = Application::GetI()->GetDevice();
		GfxContext* dc = Application::GetI()->GetDeviceContext();
		const UINT cols = Baked::ImpostorYaws, rows = (UINT)baked.Clips.size() * Baked::ImpostorFrames;
		const UINT W = cols * Baked::CellW, H = rows * Baked::CellH;

		ComPtr<GfxTexture2D> tex[2], depth;
		ComPtr<GfxRenderTargetView> rtv[2];
		ComPtr<GfxShaderResourceView> srv[2];
		ComPtr<GfxDepthStencilView> dsv;
		D3D11_TEXTURE2D_DESC td = {};
		td.Width = W; td.Height = H; td.MipLevels = 5; td.ArraySize = 1;
		td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT;
		td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		td.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
		for (int i = 0; i < 2; ++i)
		{
			if (FAILED(device->CreateTexture2D(&td, nullptr, tex[i].GetAddressOf())))
				return false;
			device->CreateRenderTargetView(tex[i].Get(), nullptr, rtv[i].GetAddressOf());
			device->CreateShaderResourceView(tex[i].Get(), nullptr, srv[i].GetAddressOf());
		}
		td.MipLevels = 1; td.MiscFlags = 0; td.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
		if (FAILED(device->CreateTexture2D(&td, nullptr, depth.GetAddressOf())))
			return false;
		device->CreateDepthStencilView(depth.Get(), nullptr, dsv.GetAddressOf());

		// 지금 상태 보관
		ComPtr<GfxRenderTargetView> oldRtv[2];
		ComPtr<GfxDepthStencilView> oldDsv;
		dc->OMGetRenderTargets(2, oldRtv[0].GetAddressOf(), oldDsv.GetAddressOf());
		UINT vpCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
		D3D11_VIEWPORT oldVp[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
		dc->RSGetViewports(&vpCount, oldVp);
		ComPtr<GfxBlendState> oldBlend; float oldFactor[4]; UINT oldMask = 0;
		dc->OMGetBlendState(oldBlend.GetAddressOf(), oldFactor, &oldMask);
		ComPtr<GfxDepthStencilState> oldDss; UINT oldRef = 0;
		dc->OMGetDepthStencilState(oldDss.GetAddressOf(), &oldRef);
		ComPtr<GfxRasterizerState> oldRs;
		dc->RSGetState(oldRs.GetAddressOf());

		GfxRenderTargetView* targets[2] = { rtv[0].Get(), rtv[1].Get() };
		dc->OMSetRenderTargets(2, targets, dsv.Get());
		const float clearAlbedo[4] = { 0.5f, 0.5f, 0.5f, 0.0f };   // 빈 곳: 회색 (알파 0) — 밉에서 가장자리가 검게 번지지 않게
		const float clearNormal[4] = { 0.5f, 1.0f, 0.5f, 1.0f };
		dc->ClearRenderTargetView(rtv[0].Get(), clearAlbedo);
		dc->ClearRenderTargetView(rtv[1].Get(), clearNormal);
		dc->ClearDepthStencilView(dsv.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
		dc->OMSetBlendState(nullptr, nullptr, 0xffffffff);

		// 줄마다 인스턴스 하나 (원점 · 단위 월드, 그 프레임 팔레트)
		std::vector<XMFLOAT4> inst;
		XMFLOAT4X4 id;
		XMStoreFloat4x4(&id, XMMatrixIdentity());
		for (int c = 0; c < (int)baked.Clips.size(); ++c)
		{
			const Clip& clip = baked.Clips[(size_t)c];
			for (int k = 0; k < Baked::ImpostorFrames; ++k)
			{
				const float u = clip.Loop ? (float)k / Baked::ImpostorFrames : (float)k / (Baked::ImpostorFrames - 1);
				const float f = u * (clip.Loop ? clip.Frames : clip.Frames - 1);
				const uint32_t fa = (std::min)((uint32_t)f, clip.Frames - 1);
				const uint32_t fb = clip.Loop ? (fa + 1) % clip.Frames : (std::min)(fa + 1, clip.Frames - 1);
				const uint32_t a = baked.FrameElement(clip.FirstFrame + fa), b = baked.FrameElement(clip.FirstFrame + fb);
				WriteInstance(inst, id, a, b, f - floorf(f), 0.0f, id, a, b, f - floorf(f), 0u, kWhite);
			}
			baked.Clips[(size_t)c].ImpostorRow = (uint32_t)c * Baked::ImpostorFrames;
		}
		static InstanceBuffer s_BakeInstances;
		if (!s_BakeInstances.Upload(inst.data(), inst.size()))
			return false;
		BindBuffers(fx, s_BakeInstances.Srv.Get(), baked.PaletteSrv.Get(), baked.PaletteSrv.Get());
		Effects::InstancedBasicFX->SetTexTransform(XMMatrixIdentity());
		SetVec(fx, "gLodFade", XMFLOAT4(0, 0, 0, 0));
		dc->IASetInputLayout(InputLayouts::PosNormalTexTanSkinned.Get());
		dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		const float halfW = baked.HalfWidth, halfH = baked.Height * 0.5f, cy = baked.CenterY;
		const float dist = (std::max)(halfW, halfH) * 4.0f;
		const auto& subsets = baked.Mesh->Subsets;
		for (UINT r = 0; r < rows; ++r)
		{
			SetBase(fx, r);
			for (UINT k = 0; k < cols; ++k)
			{
				const D3D11_VIEWPORT vp = { (float)(k * Baked::CellW), (float)(r * Baked::CellH), (float)Baked::CellW, (float)Baked::CellH, 0.0f, 1.0f };
				dc->RSSetViewports(1, &vp);
				const float a = XM_2PI * k / cols;   // 0 = 물체 +Z 쪽에서 봄 (셰이더 SkinImpostorVertex 와 같음)
				const XMVECTOR eye = XMVectorSet(sinf(a) * dist, cy, cosf(a) * dist, 1.0f);
				const XMMATRIX view = XMMatrixLookAtLH(eye, XMVectorSet(0, cy, 0, 1), XMVectorSet(0, 1, 0, 0));
				const XMMATRIX proj = XMMatrixOrthographicLH(halfW * 2.0f, halfH * 2.0f, 0.01f, dist * 2.0f + halfW * 2.0f);
				Effects::InstancedBasicFX->SetViewProj(view * proj);
				for (uint32 i = 0; i < (uint32)subsets.size(); ++i)
				{
					const UINT matIndex = subsets[i].MaterialIndex;
					UMaterial::ApplyOrDefault(matIndex < baked.Materials.size() ? baked.Materials[matIndex] : nullptr, Effects::InstancedBasicFX.get());
					tech->GetPassByIndex(0)->Apply(0, dc);
					baked.Mesh->ModelMesh.InstancingDraw(dc, i, 1);
				}
			}
		}
		BindBuffers(fx, nullptr, nullptr, nullptr);
		tech->GetPassByIndex(0)->Apply(0, dc);

		// 되돌리기
		GfxRenderTargetView* restore[2] = { oldRtv[0].Get(), oldRtv[1].Get() };
		dc->OMSetRenderTargets(2, restore, oldDsv.Get());
		if (vpCount > 0)
			dc->RSSetViewports(vpCount, oldVp);
		dc->OMSetBlendState(oldBlend.Get(), oldFactor, oldMask);
		dc->OMSetDepthStencilState(oldDss.Get(), oldRef);
		dc->RSSetState(oldRs.Get());
		dc->GenerateMips(srv[0].Get());
		dc->GenerateMips(srv[1].Get());

		baked.ImpostorAlbedo = srv[0];
		baked.ImpostorNormal = srv[1];
		baked.ImpostorRows = rows;
		EditorLog::Write("Crowd", "impostor baked %s: %u x %u (%u rows, %u yaws), cell %.2f x %.2f m (%.1f ms)", baked.Mesh->Name.c_str(), W, H, rows, cols,
			halfW * 2.0f, halfH * 2.0f, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
		return true;
	}

	// ================================================================ 패스
	bool ShadowImpostors() { return Enabled && PassReady(Pass::Shadow, true); }

	bool Add(SkinnedMeshRenderer* r, Pass pass, bool editor)
	{
		// 대상인지는 이번 프레임 팔레트를 매길 때 봤다 (팔레트가 있으면 대상)
		if (!Enabled || r == nullptr)
			return false;
		const int cast = r->GetCastShadows();
		if ((pass == Pass::Main && cast == 3) || (pass == Pass::Shadow && cast == 1))
			return false;   // 그리지 않는 패스: 부른 쪽 (Render · RenderShadow) 이 건너뛴다
		if (!PassReady(pass, false))
			return false;
		EnsurePalettes();
		if (!s_PaletteOk || r->InstPaletteFrame != s_PaletteFrame)
			return false;
		MeshGeometry* geo = pass == Pass::Shadow ? r->ShadowGeometry() : r->PrepareLod(editor);
		// 멀리 (화면에 작게) — 재생 중인 클립의 애니메이션 임포스터 (따로 설정 없이). 그림자도 임포스터 (빛을 바라보는 사각형 — 자세가 필요 없다)
		CrowdAnimation::Baked* imp = nullptr;
		if (r->CurrentLod() == SkinnedLod::kImpostorLevel && PassReady(pass, true))
			imp = ImpostorFor(r);
		if (r->InstNoPalette && imp == nullptr)
			return false;   // 팔레트를 만들지 않았다 (임포스터 단계) — 이번만 렌더러마다
		s_Items[(int)pass].push_back({ r, geo, imp });
		return true;
	}

	void Flush(Pass pass, bool editor)
	{
		std::vector<Item>& items = s_Items[(int)pass];
		if (items.empty())
			return;
		PROFILE_SCOPE("SkinnedInstancing.Flush");
		RenderManager* rm = RenderManager::GetI();
		GfxContext* dc = Application::GetI()->GetDeviceContext();
		const bool main = pass == Pass::Main;

		const int pi = (int)pass;
		const bool reuse = s_InstancesHoldGroups && s_PrevItemsFrame == s_PaletteFrame && SameItems(items, s_PrevItems);
		if (reuse)
		{
			// 앞 패스와 같은 모음: 통계만
			for (size_t k = 0; k < items.size(); ++k)
			{
				if (main)
					RenderStats::AddSkinnedMesh();
				else if (pass == Pass::Shadow)
					RenderStats::AddShadowCaster();
			}
			for (const Group& g : s_Groups)
				if (g.Impostor)
					s_Cur.Impostors += (int)g.Renderers.size();
			s_Cur.Instances[pi] += (int)items.size();
			items.clear();
		}
		else
		{
			// ---- 묶기 (렌더러 순서 그대로 — 묶음 수는 지오메트리 LOD · 임포스터 × 재질 조합으로 몇 개뿐).
			//  레이어 (본 패스의 빛 Culling Mask) 는 모든 패스에서 나눈다 — 다음 패스가 이 묶음을 다시 쓸 수 있게
			std::optional<Profiler::Scope> stage(std::in_place, "SkinnedInstancing.Group");
			const int n = (int)items.size();
			// 1) 병렬: 렌더러마다 묶음 키 (지오메트리 또는 임포스터 · 레이어 · 재질 포인터 전부) — 렌더러 · 오브젝트 · 재질 목록을 읽는 캐시 미스를 나눠서
			s_Keyed.resize((size_t)n);
			auto keyOf = [&](int i) {
				const Item& it = items[(size_t)i];
				const auto& mats = it.R->DrawMaterials();
				Keyed& k = s_Keyed[(size_t)i];
				k.Layer = 1u << (it.R->GetGameObject()->GetLayerIndex() & 31);
				uint64_t key = it.Impostor ? (uint64_t)(uintptr_t)it.Impostor * 31u + 1u : (uint64_t)(uintptr_t)it.Geo;
				key = key * 0x9E3779B97F4A7C15ull + k.Layer;
				key = key * 0x9E3779B97F4A7C15ull + mats.size();
				for (const auto& m : mats)
					key = (key ^ (uint64_t)(uintptr_t)m.get()) * 0x100000001B3ull;
				k.Key = key;
				k.Deferred = false;
			};
			std::atomic<bool> anyDeferred{ false };
			Jobs::ParallelFor(n, 256, [&](int b, int e) {
				for (int i = b; i < e; ++i)
				{
					// MaterialPropertyBlock 이 있는 렌더러: 파생 재질을 만들 수 있다 (공유 표) — 메인에서 아래
					if (items[(size_t)i].R->HasMaterialBlock())
					{
						s_Keyed[(size_t)i].Deferred = true;
						anyDeferred.store(true, std::memory_order_relaxed);
						continue;
					}
					keyOf(i);
				}
			}, "SkinnedInstancing Keys");
			if (anyDeferred.load())
				for (int i = 0; i < n; ++i)
					if (s_Keyed[(size_t)i].Deferred)
						keyOf(i);
			// 2) 차례로: 키로 묶음 번호 (앞 렌더러와 같은 키가 대부분 — 표를 찾지 않는다). 렌더러 순서 그대로
			s_Groups.clear();
			static std::unordered_map<uint64_t, int> s_GroupIndex;
			s_GroupIndex.clear();
			int last = -1;
			uint64_t lastKey = 0;
			for (int i = 0; i < n; ++i)
			{
				Keyed& k = s_Keyed[(size_t)i];
				int gi = last;
				if (last < 0 || k.Key != lastKey)
				{
					auto [pos, added] = s_GroupIndex.try_emplace(k.Key, (int)s_Groups.size());
					if (added)
					{
						const Item& it = items[(size_t)i];
						s_Groups.push_back(Group());
						Group& g = s_Groups.back();
						g.Geo = it.Geo;
						g.Impostor = it.Impostor;
						g.Materials = &it.R->DrawMaterials();
						g.Layer = k.Layer;
					}
					gi = pos->second;
					last = gi;
					lastKey = k.Key;
				}
				Group& g = s_Groups[(size_t)gi];
				k.Group = (uint32_t)gi;
				k.Slot = (uint32_t)g.Renderers.size();
				g.Renderers.push_back(items[(size_t)i].R);
			}
			// 3) 병렬 확인: 키가 같은데 실제로 다른 묶음 (64 비트 키 충돌) 이면 정확한 비교로 다시 묶는다
			std::atomic<bool> collided{ false };
			Jobs::ParallelFor(n, 256, [&](int b, int e) {
				for (int i = b; i < e && !collided.load(std::memory_order_relaxed); ++i)
				{
					const Item& it = items[(size_t)i];
					const Keyed& k = s_Keyed[(size_t)i];
					const Group& g = s_Groups[k.Group];
					if (g.Impostor != it.Impostor || (!it.Impostor && g.Geo != it.Geo) || g.Layer != k.Layer || !SameMaterials(*g.Materials, it.R->DrawMaterials()))
						collided.store(true, std::memory_order_relaxed);
				}
			}, "SkinnedInstancing Verify");
			if (collided.load())
				GroupExact(items);
			for (int i = 0; i < n; ++i)
			{
				if (main)
					RenderStats::AddSkinnedMesh();
				else if (pass == Pass::Shadow)
					RenderStats::AddShadowCaster();
			}
			stage.reset();

			// ---- 인스턴스 (월드 + 팔레트 자리, 임포스터면 아틀라스 줄): 묶음마다 시작 칸을 정하고 병렬로 쓴다
			stage.emplace("SkinnedInstancing.Write");
			uint32_t total = 0;
			for (Group& g : s_Groups)
			{
				g.Base = total;
				total += (uint32_t)g.Renderers.size();
				if (g.Impostor)
					s_Cur.Impostors += (int)g.Renderers.size();
			}
			s_InstData.resize((size_t)total * CrowdInstanceStride);
			const int groupCount = (int)s_Groups.size();
			Jobs::ParallelFor(groupCount == 0 ? 0 : n, 512, [&](int b, int e) {
				for (int i = b; i < e; ++i)
				{
					const Group& g = s_Groups[s_Keyed[(size_t)i].Group];
					SkinnedMeshRenderer* r = g.Renderers[s_Keyed[(size_t)i].Slot];
					const float row = g.Impostor ? CrowdAnimation::ImpostorRow(*g.Impostor, 0, r->AnimTime) : 0.0f;
					WriteInstanceTo(&s_InstData[((size_t)g.Base + s_Keyed[(size_t)i].Slot) * CrowdInstanceStride], r->InstWorld, r->InstPalette, r->InstPalette, 0.0f, row,
						r->InstWorld, r->InstPalette, r->InstPalette, 0.0f, 0u, kWhite);
				}
			}, "SkinnedInstancing Write");
			s_PrevItems.swap(items);
			items.clear();
			s_PrevItemsFrame = s_PaletteFrame;
			s_InstancesHoldGroups = false;
			const bool uploaded = s_Instances.Upload(s_InstData.data(), s_InstData.size());
			stage.reset();
			if (!uploaded)
				return;
			s_InstancesHoldGroups = true;
			s_Cur.Instances[pi] += (int)(s_InstData.size() / CrowdInstanceStride);
		}

		// ---- 패스 값
		FxEffect* fx = PassEffect(pass);
		const XMMATRIX viewProj = editor ? rm->EditorCameraViewProjectionMatrix : rm->CameraViewProjectionMatrix;
		const XMMATRIX view = editor ? rm->EditorCameraViewMatrix : rm->CameraViewMatrix;
		switch (pass)
		{
		case Pass::Main:
		{
			static const XMMATRIX toTex(0.5f, 0.0f, 0.0f, 0.0f, 0.0f, -0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.5f, 0.5f, 0.0f, 1.0f);
			Effects::InstancedBasicFX->SetViewProj(viewProj);
			Effects::InstancedBasicFX->SetViewProjTex(viewProj * toTex);
			Effects::InstancedBasicFX->SetTexTransform(XMMatrixIdentity());
			SetVec(fx, "gLodFade", XMFLOAT4(0, 0, 0, 0));
			break;
		}
		case Pass::Shadow:
			// gViewProj 는 ShadowRenderer 가 조각마다 넣어 둔다 (렌더러마다 그릴 때와 같다)
			Effects::BuildShadowMapFX->SetTexTransform(XMMatrixIdentity());
			break;
		case Pass::NormalDepth:
			SetMatrix(fx, "gView", view);
			SetMatrix(fx, "gWorldViewProj", viewProj);   // posW × ViewProj (본 패스와 같은 식)
			SetMatrix(fx, "gTexTransform", XMMatrixIdentity());
			SetVec(fx, "gLodFade", XMFLOAT4(0, 0, 0, 0));
			break;
		}
		FxTechnique* meshTech = PassTech(pass, false);
		FxTechnique* impTech = PassTech(pass, true);
		if (fx == nullptr || meshTech == nullptr)
			return;
		// Alpha Clipping 재질의 깊이 · 그림자: 잘라내는 기법 + 그 재질의 그림 · 기준 (렌더러마다 그릴 때와 같다)
		FxTechnique* clipTech = pass == Pass::NormalDepth ? Tech(fx, "NormalDepthAlphaClipSkinnedInstancedTech")
			: pass == Pass::Shadow ? Tech(fx, "BuildShadowMapAlphaClipSkinnedInstancedTech") : nullptr;
		auto clipMaterial = [&](const shared_ptr<UMaterial>& m, float& cutoff) -> UMaterial* {
			if (!clipTech || !m || !m->GetPbr().AlphaClip || !m->GetBaseMapSRV())
				return nullptr;
			cutoff = m->GetPbr().Cutoff / (std::max)(m->GetPbr().BaseColor.w, 1e-4f);
			return m.get();
		};

		dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		BindBuffers(fx, s_Instances.Srv.Get(), CurPalette().Srv.Get(), PrevPalette().Srv.Get());
		// 임포스터 기법은 깊이 (EQUAL) · 래스터 (양면) 상태를 바꾼다 → 끝나면 되돌린다
		ComPtr<GfxDepthStencilState> oldDss;
		ComPtr<GfxRasterizerState> oldRs;
		UINT oldRef = 0;
		bool saved = false;
		for (const Group& g : s_Groups)
		{
			if (g.Impostor && !saved)
			{
				dc->OMGetDepthStencilState(oldDss.GetAddressOf(), &oldRef);
				dc->RSGetState(oldRs.GetAddressOf());
				saved = true;
			}
			if (main)
				RenderLayers::SetObjectLayer(Effects::InstancedBasicFX.get(), g.Layer);
			SetBase(fx, g.Base);
			const UINT count = (UINT)g.Renderers.size();
			SkinnedMeshRenderer* first = g.Renderers.front();
			const auto& subsets = first->GetMesh()->Subsets;
			if (g.Impostor)
			{
				if (impTech == nullptr)
					continue;
				// 재질 값 (금속성 · 매끄러움) 은 첫 서브셋 것
				if (main)
					UMaterial::ApplyOrDefault(!g.Materials->empty() ? (*g.Materials)[0] : nullptr, Effects::InstancedBasicFX.get());
				BindImpostor(fx, g.Impostor, view, pass == Pass::Shadow);
				dc->IASetInputLayout(nullptr);
				impTech->GetPassByIndex(0)->Apply(0, dc);
				RenderStats::AddDraw(6, 4, count);
				dc->DrawInstanced(6, count, 0, 0);
				BindImpostor(fx, nullptr, view, false);
				dc->OMSetDepthStencilState(oldDss.Get(), oldRef);
				dc->RSSetState(oldRs.Get());
				++s_Cur.Draws[pi];
				continue;
			}
			dc->IASetInputLayout(InputLayouts::PosNormalTexTanSkinned.Get());
			for (uint32 i = 0; i < (uint32)subsets.size(); ++i)
			{
				const UINT matIndex = subsets[i].MaterialIndex;
				const shared_ptr<UMaterial> mat = matIndex < g.Materials->size() ? (*g.Materials)[matIndex] : nullptr;
				if (main)
					UMaterial::ApplyOrDefault(mat, Effects::InstancedBasicFX.get());
				float cutoff = 0.0f;
				if (UMaterial* cm = main ? nullptr : clipMaterial(mat, cutoff))
				{
					const PbrMaterial& pbr = cm->GetPbr();
					const XMMATRIX tt = XMMatrixScaling(pbr.Tiling.x, pbr.Tiling.y, 1.0f) * XMMatrixTranslation(pbr.Offset.x, pbr.Offset.y, 0.0f);
					if (FxVar* v = Var(fx, "gDiffuseMap")) v->SetResource(cm->GetBaseMapSRV());
					if (FxVar* v = Var(fx, "gAlphaCutoff")) v->SetFloat(cutoff);
					SetMatrix(fx, "gTexTransform", tt);
					clipTech->GetPassByIndex(0)->Apply(0, dc);
					g.Geo->InstancingDraw(dc, i, count);
					SetMatrix(fx, "gTexTransform", XMMatrixIdentity());
					if (FxVar* v = Var(fx, "gAlphaCutoff")) v->SetFloat(0.0f);   // 0 = 예전 고정값 (다른 렌더러용)
					++s_Cur.Draws[pi];
					continue;
				}
				meshTech->GetPassByIndex(0)->Apply(0, dc);
				g.Geo->InstancingDraw(dc, i, count);
				++s_Cur.Draws[pi];
			}
		}
		BindBuffers(fx, nullptr, nullptr, nullptr);
		meshTech->GetPassByIndex(0)->Apply(0, dc);   // 풀어 둔 자원을 실제로 (다음 패스가 같은 버퍼를 쓰기 전에)
		if (main)
			RenderLayers::SetObjectLayer(Effects::InstancedBasicFX.get(), ~0u);
	}

	bool CanDrawMotion(SkinnedMeshRenderer* r)
	{
		if (!Enabled || r == nullptr)
			return false;
		EnsurePalettes();
		return s_PaletteOk && r->InstPaletteFrame == s_PaletteFrame;
	}

	bool AddMotion(SkinnedMeshRenderer* r, int mode, bool objectMotion, bool skinnedMotion)
	{
		// 0 Camera Motion: 물체 움직임을 그리지 않는다 (카메라 패스가 맡는다). 임포스터는 멀어서 카메라 움직임으로
		if (mode == 0 || r->CurrentLod() == SkinnedLod::kImpostorLevel)
			return false;
		const bool hasPrev = r->InstPrevPalette != ~0u;
		const bool still = mode == 2;
		const bool skin = skinnedMotion && hasPrev;
		bool moved = false;
		if (objectMotion && hasPrev)
			moved = (skin && r->InstPoseChanged) || memcmp(&r->InstWorld, &r->InstPrevWorld, sizeof(XMFLOAT4X4)) != 0;
		if (!still && !moved)
			return false;
		s_Motion.push_back({ r, still, skin });
		return true;
	}

	int FlushMotion(FxEffect* fx)
	{
		if (s_Motion.empty())
			return 0;
		FxTechnique* tech = Tech(fx, "SkinnedInstancedMotionTech");
		if (tech == nullptr)
		{
			s_Motion.clear();
			return 0;
		}
		PROFILE_SCOPE("SkinnedInstancing.Motion");
		// 지오메트리마다 묶는다
		s_Groups.clear();
		std::vector<int> groupOf(s_Motion.size());
		for (size_t k = 0; k < s_Motion.size(); ++k)
		{
			MeshGeometry* geo = s_Motion[k].R->CurrentGeometry();
			int gi = -1;
			for (int c = 0; c < (int)s_Groups.size(); ++c)
				if (s_Groups[(size_t)c].Geo == geo) { gi = c; break; }
			if (gi < 0)
			{
				gi = (int)s_Groups.size();
				s_Groups.push_back(Group());
				s_Groups.back().Geo = geo;
			}
			groupOf[k] = gi;
			s_Groups[(size_t)gi].Renderers.push_back(s_Motion[k].R);
		}
		s_InstData.clear();
		for (int gi = 0; gi < (int)s_Groups.size(); ++gi)
		{
			s_Groups[(size_t)gi].Base = (uint32_t)(s_InstData.size() / CrowdInstanceStride);
			for (size_t k = 0; k < s_Motion.size(); ++k)
				if (groupOf[k] == gi)
				{
					const MotionEntry& m = s_Motion[k];
					// 지난 팔레트: 지난 프레임 버퍼 (표시 2), 없으면 이번 것
					const uint32_t prev = m.UsePrev ? m.R->InstPrevPalette : m.R->InstPalette;
					const uint32_t flags = (m.Still ? kInstanceStill : 0u) | (m.UsePrev ? kInstancePrevBuffer : 0u);
					WriteInstance(s_InstData, m.R->InstWorld, m.R->InstPalette, m.R->InstPalette, 0.0f, 0.0f,
						m.R->InstPrevWorld, prev, prev, 0.0f, flags, kWhite);
				}
		}
		const int count = (int)s_Motion.size();
		s_Motion.clear();
		s_InstancesHoldGroups = false;   // s_Groups · s_Instances 를 모션 벡터 것으로 바꾼다
		if (!s_Instances.Upload(s_InstData.data(), s_InstData.size()))
			return 0;
		GfxContext* dc = Application::GetI()->GetDeviceContext();
		dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		BindBuffers(fx, s_Instances.Srv.Get(), CurPalette().Srv.Get(), PrevPalette().Srv.Get());
		for (const Group& g : s_Groups)
		{
			SetBase(fx, g.Base);
			const UINT n = (UINT)g.Renderers.size();
			const size_t subsets = g.Renderers.front()->GetMesh()->Subsets.size();
			for (uint32 i = 0; i < (uint32)subsets; ++i)
			{
				tech->GetPassByIndex(0)->Apply(0, dc);
				g.Geo->InstancingDraw(dc, i, n);
				++s_Cur.Draws[3];
			}
		}
		BindBuffers(fx, nullptr, nullptr, nullptr);
		tech->GetPassByIndex(0)->Apply(0, dc);
		s_Cur.Instances[3] += count;
		return count;
	}

	nlohmann::json Info()
	{
		RollStats(SceneCulling::FrameIndex());
		auto pass = [](const Stats& s, int i) { return nlohmann::json{ { "instances", s.Instances[i] }, { "draws", s.Draws[i] } }; };
		return { { "enabled", Enabled }, { "palettes", s_Last.Palettes }, { "impostors", s_Last.Impostors },
			{ "main", pass(s_Last, 0) }, { "shadow", pass(s_Last, 1) }, { "normalDepth", pass(s_Last, 2) }, { "motion", pass(s_Last, 3) } };
	}
}
