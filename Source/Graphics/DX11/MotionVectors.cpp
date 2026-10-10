#include "pch.h"
#include "MotionVectors.h"
#include "VolumeProfile.h"
#include "Vertex.h"
#include "MeshRenderer.h"
#include "SkinnedMeshRenderer.h"
#include "SceneCulling.h"
#include "RenderLayers.h"
#include "EditorLog.h"
#include "SceneManager.h"
#include "GameObject.h"
#include "Profiler.h"
#include "TransformStore.h"
#include "SkinnedInstancing.h"

namespace MotionVectors
{
	namespace
	{
		ComPtr<FxEffect> s_Fx;
		bool s_LoadFailed = false;
		ComPtr<GfxInputLayout> s_ObjectLayout, s_SkinnedLayout;

		// 물체: 렌더러마다 마지막으로 본 월드 행렬 · 본 팔레트
		struct Track
		{
			XMFLOAT4X4 World = {}, PrevWorld = {};
			std::vector<XMFLOAT4X4> Bones, PrevBones;
			uint32_t Frame = 0;
		};

		// 뷰마다 (0 Game 뷰, 1 Scene 뷰 — Rendering Debugger 의 Motion Vectors 보기): 타깃 · 지난 카메라 · 렌더러 기록
		//  같은 프레임에 두 뷰가 그려도 서로의 "지난 프레임" 을 덮지 않게 따로 둔다
		struct ViewState
		{
			ComPtr<GfxTexture2D> Tex;
			ComPtr<GfxRenderTargetView> RTV;
			ComPtr<GfxShaderResourceView> SRV;
			UINT W = 0, H = 0;
			bool Valid = false;
			// 카메라: 지난 프레임 (지터 없는) 뷰 × 투영
			XMFLOAT4X4 CurViewProj = {}, PrevViewProj = {};
			bool HasPrev = false;
			uint32_t Frame = 0, PrevFrame = 0;   // 이번 · 지난 렌더 프레임 (SceneCulling::FrameIndex)
			std::unordered_map<const Component*, Track> Tracks;   // Skinned Mesh Renderer (본 팔레트)
			// Mesh Renderer: 컬링 자리마다 (주인 · 이 뷰가 마지막으로 본 월드 · 그 월드 번호). 번호가 그대로면 움직이지 않았다 —
			//  씬 전체를 훑어 렌더러마다 행렬을 비교하던 것 (도시 1 ms) 대신 연속 배열의 번호만 비교한다
			std::vector<const Component*> MeshOwner;
			std::vector<XMFLOAT4X4> MeshWorld;
			std::vector<uint32_t> MeshVersion;
			int ObjectsDrawn = 0, SkinnedDrawn = 0, ForcedStill = 0;
			Settings Last;
		};
		ViewState s_Views[2];
		ViewState* s_V = &s_Views[0];

		FxVar* Var(const char* name)
		{
			FxVar* v = s_Fx ? s_Fx->GetVariableByName(name) : nullptr;
			return v && v->IsValid() ? v : nullptr;
		}
		void SetM(const char* name, const XMFLOAT4X4& m) { if (FxVar* v = Var(name)) v->SetMatrix(&m._11); }
		void SetV(const char* name, float x, float y, float z, float w) { const float f[4] = { x, y, z, w }; if (FxVar* v = Var(name)) v->SetFloatVector(f); }
		void SetR(const char* name, GfxShaderResourceView* srv) { if (FxVar* v = Var(name)) v->SetResource(srv); }
		FxTechnique* Tech(const char* name)
		{
			FxTechnique* t = s_Fx ? s_Fx->GetTechniqueByName(name) : nullptr;
			return t && t->IsValid() ? t : nullptr;
		}

		bool Load()
		{
			if (s_Fx || s_LoadFailed)
				return s_Fx != nullptr;
			std::string error;
			s_Fx = FxEffect::Load(L"../Shaders/63. MotionVectors.fx", error);
			if (!s_Fx)
			{
				s_LoadFailed = true;
				EditorLog::Write("MotionVectors", "63. MotionVectors.fx failed to load: %s", error.c_str());
				return false;
			}
			// 정점 형식: Mesh Renderer (PosNormalTexTan2) · Skinned (PosNormalTexTanSkinned) — 셰이더는 위치 (· 가중치 · 본 번호) 만 읽는다
			D3DX11_PASS_DESC pd;
			if (FxTechnique* t = Tech("ObjectMotionTech"))
			{
				t->GetPassByIndex(0)->GetDesc(&pd);
				Gfx::Device()->CreateInputLayout(InputLayoutDesc::PosNormalTexTan2, 4, pd.pIAInputSignature, pd.IAInputSignatureSize, s_ObjectLayout.GetAddressOf());
			}
			if (FxTechnique* t = Tech("SkinnedMotionTech"))
			{
				t->GetPassByIndex(0)->GetDesc(&pd);
				Gfx::Device()->CreateInputLayout(InputLayoutDesc::PosNormalTexTanSkinned, 6, pd.pIAInputSignature, pd.IAInputSignatureSize, s_SkinnedLayout.GetAddressOf());
			}
			return true;
		}

		bool EnsureTarget(UINT w, UINT h)
		{
			if (s_V->RTV && s_V->W == w && s_V->H == h)
				return true;
			s_V->Tex.Reset(); s_V->RTV.Reset(); s_V->SRV.Reset();
			D3D11_TEXTURE2D_DESC d = {};
			d.Width = (std::max)(w, 1u);
			d.Height = (std::max)(h, 1u);
			d.MipLevels = 1;
			d.ArraySize = 1;
			d.Format = DXGI_FORMAT_R16G16_FLOAT;
			d.SampleDesc.Count = 1;
			d.Usage = D3D11_USAGE_DEFAULT;
			d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
			auto device = Gfx::Device();
			if (FAILED(device->CreateTexture2D(&d, nullptr, s_V->Tex.GetAddressOf()))
				|| FAILED(device->CreateShaderResourceView(s_V->Tex.Get(), nullptr, s_V->SRV.GetAddressOf()))
				|| FAILED(device->CreateRenderTargetView(s_V->Tex.Get(), nullptr, s_V->RTV.GetAddressOf())))
				return false;
			s_V->W = w;
			s_V->H = h;
			return true;
		}

		bool SameMatrix(const XMFLOAT4X4& a, const XMFLOAT4X4& b) { return memcmp(&a, &b, sizeof(XMFLOAT4X4)) == 0; }

		// 이번 값을 넣고 지난 값을 고른다: 지난 렌더 프레임에 본 렌더러만 그때 값, 처음 · 오래 안 보였으면 이번 값 (움직임 없음)
		Track& Remember(const Component* r, const XMFLOAT4X4& world, const std::vector<XMFLOAT4X4>* bones)
		{
			Track& t = s_V->Tracks[r];
			if (t.Frame != s_V->Frame)
			{
				const bool continuous = t.Frame != 0 && t.Frame == s_V->PrevFrame;
				t.PrevWorld = continuous ? t.World : world;
				if (bones)
					t.PrevBones = continuous && t.Bones.size() == bones->size() ? t.Bones : *bones;
				t.World = world;
				if (bones)
					t.Bones = *bones;
				t.Frame = s_V->Frame;
			}
			return t;
		}
	}

	Settings Settings::FromStack(const VolumeStack& stack)
	{
		Settings s;
		if (const VolumeComponent* c = stack.Get("MotionVectors"))
		{
			s.Enabled = c->B("enabled");
			s.ObjectMotion = c->B("objectMotion");
			s.SkinnedMotion = c->B("skinnedMotion");
		}
		return s;
	}

	void Render(const Frame& f, const Settings& settings)
	{
		s_V = &s_Views[f.SceneView ? 1 : 0];
		s_V->Last = settings;
		s_V->Valid = false;
		if (!settings.Enabled || f.Width == 0 || f.Height == 0 || f.NormalDepth == nullptr || !Load() || !EnsureTarget(f.Width, f.Height))
		{
			s_V->HasPrev = false;
			return;
		}
		PROFILE_SCOPE("Motion Vectors");
		GfxContext* ctx = Gfx::Context();

		// 새 프레임이면 지난 카메라를 넘긴다 (같은 프레임에 다시 그리면 그대로)
		const uint32_t frame = SceneCulling::FrameIndex();
		XMFLOAT4X4 viewProj;
		XMStoreFloat4x4(&viewProj, XMLoadFloat4x4(&f.View) * XMLoadFloat4x4(&f.Proj));
		if (frame != s_V->Frame)
		{
			s_V->PrevFrame = s_V->Frame;
			s_V->Frame = frame;
			s_V->PrevViewProj = s_V->HasPrev ? s_V->CurViewProj : viewProj;
			s_V->HasPrev = true;
		}
		s_V->CurViewProj = viewProj;

		XMFLOAT4X4 viewProjJ, invView;
		XMStoreFloat4x4(&viewProjJ, XMLoadFloat4x4(&f.View) * XMLoadFloat4x4(&f.ProjJittered));
		XMStoreFloat4x4(&invView, XMMatrixInverse(nullptr, XMLoadFloat4x4(&f.View)));
		const XMFLOAT4X4& pj = f.ProjJittered;
		const bool ortho = fabsf(pj._34) < 1e-6f;
		SetM("gViewProjJ", viewProjJ);
		SetM("gViewProj", s_V->CurViewProj);
		SetM("gPrevViewProj", s_V->PrevViewProj);
		SetM("gView", f.View);
		SetM("gInvView", invView);
		SetV("gProjInfo", pj._11, pj._22, ortho ? pj._41 : pj._31, ortho ? pj._42 : pj._32);
		SetR("gNormalDepth", f.NormalDepth);

		const D3D11_VIEWPORT vp = { 0.0f, 0.0f, (float)f.Width, (float)f.Height, 0.0f, 1.0f };
		GfxRenderTargetView* rtv[1] = { s_V->RTV.Get() };
		ctx->OMSetRenderTargets(1, rtv, nullptr);
		ctx->RSSetViewports(1, &vp);
		ctx->OMSetDepthStencilState(nullptr, 0);
		ctx->OMSetBlendState(nullptr, nullptr, 0xffffffff);
		ctx->RSSetState(nullptr);

		// 1) 카메라 — 모든 픽셀
		if (FxTechnique* cam = Tech("CameraMotionTech"))
		{
			SetV("gMotionFlags", ortho ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
			ctx->IASetInputLayout(nullptr);
			ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			cam->GetPassByIndex(0)->Apply(0, ctx);
			ctx->Draw(3, 0);
		}

		// 2) 물체 — 움직인 것만 다시 그린다
		s_V->ObjectsDrawn = s_V->SkinnedDrawn = s_V->ForcedStill = 0;
		FxTechnique* objTech = Tech("ObjectMotionTech");
		FxTechnique* skinTech = Tech("SkinnedMotionTech");
		Scene* scene = SceneManager::GetI()->GetCurrentScene();
		if (scene && objTech && skinTech)
		{
			ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			// 컬링이 추적하는 렌더러 (이 씬의 것) 를 자리 차례로 — 움직인 것 (월드 번호가 바뀜) 과 Force No Motion 만 그린다
			const uint32_t sceneStamp = SceneCulling::FrameIndex();
			const size_t count = SceneCulling::EntryCount();
			const size_t slots = (size_t)SceneCulling::SlotCount();   // 기록은 진짜 자리 번호로 (살아 있는 차례는 자리가 빠지면 밀린다)
			if (s_V->MeshOwner.size() < slots)
			{
				s_V->MeshOwner.resize(slots, nullptr);
				s_V->MeshWorld.resize(slots);
				s_V->MeshVersion.resize(slots, 0);
			}
			auto drawable = [&](Component* r) {
				GameObject* go = r->GetGameObject();
				return go && go->CullSceneStamp == sceneStamp && go->IsActiveInHierarchy() && RenderLayers::Visible(go) && r->IsEnabled() && SceneCulling::IsVisible(r);
			};
			SceneCulling::EntryView e;
			for (size_t index = 0; index < count; ++index)
			{
				if (!SceneCulling::EntryAt(index, e) || e.Skinned)
					continue;
				MeshRenderer* mr = static_cast<MeshRenderer*>(e.Renderer);
				const size_t slot = e.Slot;
				const int mode = mr->GetMotionVectors();   // 0 Camera Motion, 1 Per Object Motion, 2 Force No Motion
				// 이 뷰가 처음 보는 자리 (새 렌더러 · 다시 쓴 자리): 지금 월드가 지난 월드 (움직임 없음 — 예전 Remember 와 같다)
				bool versionChanged = false;
				XMFLOAT4X4 prev;
				if (s_V->MeshOwner[slot] != mr)
				{
					s_V->MeshOwner[slot] = mr;
					s_V->MeshWorld[slot] = TransformStore::WorldRef(e.TrSlot);
					s_V->MeshVersion[slot] = e.TrVersion;
				}
				else if (s_V->MeshVersion[slot] != e.TrVersion)
				{
					versionChanged = true;
					prev = s_V->MeshWorld[slot];   // 이 뷰가 지난번 본 월드 (그 뒤로 번호가 바뀌지 않았으면 그때 월드 그대로)
					s_V->MeshWorld[slot] = TransformStore::WorldRef(e.TrSlot);
					s_V->MeshVersion[slot] = e.TrVersion;
				}
				const bool still = mode == 2;
				const bool moved = settings.ObjectMotion && mode == 1 && versionChanged && !SameMatrix(prev, s_V->MeshWorld[slot]);
				if (!still && !moved)
					continue;
				if (!drawable(mr))
					continue;
				auto mesh = mr->GetMesh();
				if (!mesh || mesh->Subsets.empty())
					continue;
				SetM("gWorld", s_V->MeshWorld[slot]);
				SetM("gPrevWorld", moved ? prev : s_V->MeshWorld[slot]);
				SetV("gMotionFlags", ortho ? 1.0f : 0.0f, still ? 1.0f : 0.0f, 0.0f, 0.0f);
				ctx->IASetInputLayout(s_ObjectLayout.Get());
				for (uint32 i = 0; i < (uint32)mesh->Subsets.size(); ++i)
				{
					objTech->GetPassByIndex(0)->Apply(0, ctx);
					mesh->ModelMesh.Draw(ctx, i, nullptr);
				}
				still ? ++s_V->ForcedStill : ++s_V->ObjectsDrawn;
			}
			// Skinned Mesh Renderer (본 팔레트가 프레임마다 바뀐다 — 드물다). 군중 (Auto LOD) 은 모아서 인스턴싱 (SkinnedInstancing)
			const bool canInstance = Tech("SkinnedInstancedMotionTech") != nullptr;
			for (size_t index = 0; index < count; ++index)
			{
				if (!SceneCulling::EntryAt(index, e) || !e.Skinned)
					continue;
				SkinnedMeshRenderer* smr = static_cast<SkinnedMeshRenderer*>(e.Renderer);
				GameObject* go = smr->GetGameObject();
				if (smr->IsMerged() || go == nullptr || go->CullSceneStamp != sceneStamp || !go->IsActiveInHierarchy() || !RenderLayers::Visible(go))
					continue;
				// 군중: 지난 월드 · 팔레트는 인스턴싱이 GPU 에 둔 지난 프레임 것 (본을 CPU 로 복사 · 비교하지 않는다)
				if (canInstance && SkinnedInstancing::CanDrawMotion(smr))
				{
					if (SceneCulling::IsVisible(smr) && SkinnedInstancing::AddMotion(smr, smr->GetMotionVectors(), settings.ObjectMotion,
						settings.SkinnedMotion && smr->GetSkinnedMotionVectors()))
						smr->GetMotionVectors() == 2 ? ++s_V->ForcedStill : ++s_V->SkinnedDrawn;
					continue;
				}
				XMFLOAT4X4 world = TransformStore::WorldRef(e.TrSlot);
				if (smr->IsEnabled() && smr->GetMesh())
				{
					const int mode = smr->GetMotionVectors();
					const std::vector<XMFLOAT4X4>& bones = smr->MotionPalette();
					Track& t = Remember(smr, world, &bones);
					if (bones.empty() || mode == 0 || !SceneCulling::IsVisible(smr))
						continue;
					const bool still = mode == 2;
					const bool skin = settings.SkinnedMotion && smr->GetSkinnedMotionVectors();
					const bool boneMoved = skin && (t.Bones.size() != t.PrevBones.size() || memcmp(t.Bones.data(), t.PrevBones.data(), t.Bones.size() * sizeof(XMFLOAT4X4)) != 0);
					const bool moved = settings.ObjectMotion && (boneMoved || !SameMatrix(t.World, t.PrevWorld));
					if (!still && !moved)
						continue;
					const UINT n = (UINT)(std::min)(bones.size(), (size_t)256);
					SetM("gWorld", t.World);
					SetM("gPrevWorld", t.PrevWorld);
					if (FxVar* v = Var("gBones")) v->SetMatrixArray(&t.Bones[0]._11, 0, n);
					// 본 애니메이션을 끄면 지난 팔레트 = 이번 (물체의 이동만)
					const std::vector<XMFLOAT4X4>& prevBones = skin && t.PrevBones.size() == t.Bones.size() ? t.PrevBones : t.Bones;
					if (FxVar* v = Var("gPrevBones")) v->SetMatrixArray(&prevBones[0]._11, 0, n);
					SetV("gMotionFlags", ortho ? 1.0f : 0.0f, still ? 1.0f : 0.0f, 0.0f, 0.0f);
					ctx->IASetInputLayout(s_SkinnedLayout.Get());
					smr->DrawForMotionVectors(ctx, skinTech);
					still ? ++s_V->ForcedStill : ++s_V->SkinnedDrawn;
				}
			}
			ctx->IASetInputLayout(s_SkinnedLayout.Get());
			SkinnedInstancing::FlushMotion(s_Fx.Get());
		}

		// 오래 안 본 렌더러는 잊는다 (지운 물체)
		for (auto it = s_V->Tracks.begin(); it != s_V->Tracks.end();)
			it = (it->second.Frame != s_V->Frame && it->second.Frame != s_V->PrevFrame) ? s_V->Tracks.erase(it) : std::next(it);

		SetR("gNormalDepth", nullptr);
		if (FxTechnique* cam = Tech("CameraMotionTech"))
			cam->GetPassByIndex(0)->Apply(0, ctx);
		GfxRenderTargetView* none[1] = {};
		ctx->OMSetRenderTargets(1, none, nullptr);
		s_V->Valid = true;
	}

	bool Valid(bool sceneView) { return s_Views[sceneView ? 1 : 0].Valid; }
	GfxShaderResourceView* SRV(bool sceneView) { const ViewState& v = s_Views[sceneView ? 1 : 0]; return v.Valid ? v.SRV.Get() : nullptr; }
	GfxTexture2D* Texture(bool sceneView) { const ViewState& v = s_Views[sceneView ? 1 : 0]; return v.Valid ? v.Tex.Get() : nullptr; }
	void Invalidate(bool sceneView) { s_Views[sceneView ? 1 : 0].Valid = false; }

	nlohmann::json Info(bool sceneView)
	{
		s_V = &s_Views[sceneView ? 1 : 0];
		return {
			{ "enabled", s_V->Last.Enabled }, { "objectMotion", s_V->Last.ObjectMotion }, { "skinnedMotion", s_V->Last.SkinnedMotion },
			{ "valid", s_V->Valid }, { "size", { s_V->W, s_V->H } },
			{ "objectsDrawn", s_V->ObjectsDrawn }, { "skinnedDrawn", s_V->SkinnedDrawn }, { "forcedNoMotion", s_V->ForcedStill },
			{ "tracked", s_V->Tracks.size() }, { "loaded", s_Fx != nullptr } };
	}
}
