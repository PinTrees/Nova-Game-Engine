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

namespace MotionVectors
{
	namespace
	{
		ComPtr<FxEffect> s_Fx;
		bool s_LoadFailed = false;
		ComPtr<GfxInputLayout> s_ObjectLayout, s_SkinnedLayout;

		ComPtr<GfxTexture2D> s_Tex;
		ComPtr<GfxRenderTargetView> s_RTV;
		ComPtr<GfxShaderResourceView> s_SRV;
		UINT s_W = 0, s_H = 0;
		bool s_Valid = false;

		// 카메라: 지난 프레임 (지터 없는) 뷰 × 투영
		XMFLOAT4X4 s_CurViewProj = {}, s_PrevViewProj = {};
		bool s_HasPrev = false;
		uint32_t s_Frame = 0, s_PrevFrame = 0;   // 이번 · 지난 렌더 프레임 (SceneCulling::FrameIndex)

		// 물체: 렌더러마다 마지막으로 본 월드 행렬 · 본 팔레트
		struct Track
		{
			XMFLOAT4X4 World = {}, PrevWorld = {};
			std::vector<XMFLOAT4X4> Bones, PrevBones;
			uint32_t Frame = 0;
		};
		std::unordered_map<const Component*, Track> s_Tracks;
		int s_ObjectsDrawn = 0, s_SkinnedDrawn = 0, s_ForcedStill = 0;
		Settings s_Last;

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
			if (s_RTV && s_W == w && s_H == h)
				return true;
			s_Tex.Reset(); s_RTV.Reset(); s_SRV.Reset();
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
			if (FAILED(device->CreateTexture2D(&d, nullptr, s_Tex.GetAddressOf()))
				|| FAILED(device->CreateShaderResourceView(s_Tex.Get(), nullptr, s_SRV.GetAddressOf()))
				|| FAILED(device->CreateRenderTargetView(s_Tex.Get(), nullptr, s_RTV.GetAddressOf())))
				return false;
			s_W = w;
			s_H = h;
			return true;
		}

		bool SameMatrix(const XMFLOAT4X4& a, const XMFLOAT4X4& b) { return memcmp(&a, &b, sizeof(XMFLOAT4X4)) == 0; }

		// 이번 값을 넣고 지난 값을 고른다: 지난 렌더 프레임에 본 렌더러만 그때 값, 처음 · 오래 안 보였으면 이번 값 (움직임 없음)
		Track& Remember(const Component* r, const XMFLOAT4X4& world, const std::vector<XMFLOAT4X4>* bones)
		{
			Track& t = s_Tracks[r];
			if (t.Frame != s_Frame)
			{
				const bool continuous = t.Frame != 0 && t.Frame == s_PrevFrame;
				t.PrevWorld = continuous ? t.World : world;
				if (bones)
					t.PrevBones = continuous && t.Bones.size() == bones->size() ? t.Bones : *bones;
				t.World = world;
				if (bones)
					t.Bones = *bones;
				t.Frame = s_Frame;
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
		s_Last = settings;
		s_Valid = false;
		if (!settings.Enabled || f.Width == 0 || f.Height == 0 || f.NormalDepth == nullptr || !Load() || !EnsureTarget(f.Width, f.Height))
		{
			s_HasPrev = false;
			return;
		}
		PROFILE_SCOPE("Motion Vectors");
		GfxContext* ctx = Gfx::Context();

		// 새 프레임이면 지난 카메라를 넘긴다 (같은 프레임에 다시 그리면 그대로)
		const uint32_t frame = SceneCulling::FrameIndex();
		XMFLOAT4X4 viewProj;
		XMStoreFloat4x4(&viewProj, XMLoadFloat4x4(&f.View) * XMLoadFloat4x4(&f.Proj));
		if (frame != s_Frame)
		{
			s_PrevFrame = s_Frame;
			s_Frame = frame;
			s_PrevViewProj = s_HasPrev ? s_CurViewProj : viewProj;
			s_HasPrev = true;
		}
		s_CurViewProj = viewProj;

		XMFLOAT4X4 viewProjJ, invView;
		XMStoreFloat4x4(&viewProjJ, XMLoadFloat4x4(&f.View) * XMLoadFloat4x4(&f.ProjJittered));
		XMStoreFloat4x4(&invView, XMMatrixInverse(nullptr, XMLoadFloat4x4(&f.View)));
		const XMFLOAT4X4& pj = f.ProjJittered;
		const bool ortho = fabsf(pj._34) < 1e-6f;
		SetM("gViewProjJ", viewProjJ);
		SetM("gViewProj", s_CurViewProj);
		SetM("gPrevViewProj", s_PrevViewProj);
		SetM("gView", f.View);
		SetM("gInvView", invView);
		SetV("gProjInfo", pj._11, pj._22, ortho ? pj._41 : pj._31, ortho ? pj._42 : pj._32);
		SetR("gNormalDepth", f.NormalDepth);

		const D3D11_VIEWPORT vp = { 0.0f, 0.0f, (float)f.Width, (float)f.Height, 0.0f, 1.0f };
		GfxRenderTargetView* rtv[1] = { s_RTV.Get() };
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
		s_ObjectsDrawn = s_SkinnedDrawn = s_ForcedStill = 0;
		FxTechnique* objTech = Tech("ObjectMotionTech");
		FxTechnique* skinTech = Tech("SkinnedMotionTech");
		Scene* scene = SceneManager::GetI()->GetCurrentScene();
		if (scene && objTech && skinTech)
		{
			ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			for (GameObject* go : scene->GetAllGameObjects())
			{
				if (go == nullptr || !go->IsActiveInHierarchy() || !RenderLayers::Visible(go))
					continue;
				XMFLOAT4X4 world;
				XMStoreFloat4x4(&world, go->GetTransform()->GetWorldMatrix());
				if (MeshRenderer* mr = go->GetComponent<MeshRenderer>(); mr && mr->IsEnabled())
				{
					auto mesh = mr->GetMesh();
					const int mode = mr->GetMotionVectors();   // 0 Camera Motion, 1 Per Object Motion, 2 Force No Motion
					Track& t = Remember(mr, world, nullptr);
					const bool still = mode == 2;
					const bool moved = settings.ObjectMotion && mode == 1 && !SameMatrix(t.World, t.PrevWorld);
					if (mesh && !mesh->Subsets.empty() && (still || moved) && SceneCulling::IsVisible(mr))
					{
						SetM("gWorld", t.World);
						SetM("gPrevWorld", t.PrevWorld);
						SetV("gMotionFlags", ortho ? 1.0f : 0.0f, still ? 1.0f : 0.0f, 0.0f, 0.0f);
						ctx->IASetInputLayout(s_ObjectLayout.Get());
						for (uint32 i = 0; i < (uint32)mesh->Subsets.size(); ++i)
						{
							objTech->GetPassByIndex(0)->Apply(0, ctx);
							mesh->ModelMesh.Draw(ctx, i, nullptr);
						}
						still ? ++s_ForcedStill : ++s_ObjectsDrawn;
					}
				}
				if (SkinnedMeshRenderer* smr = go->GetComponent<SkinnedMeshRenderer>(); smr && smr->IsEnabled() && smr->GetMesh())
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
					still ? ++s_ForcedStill : ++s_SkinnedDrawn;
				}
			}
		}

		// 오래 안 본 렌더러는 잊는다 (지운 물체)
		for (auto it = s_Tracks.begin(); it != s_Tracks.end();)
			it = (it->second.Frame != s_Frame && it->second.Frame != s_PrevFrame) ? s_Tracks.erase(it) : std::next(it);

		SetR("gNormalDepth", nullptr);
		if (FxTechnique* cam = Tech("CameraMotionTech"))
			cam->GetPassByIndex(0)->Apply(0, ctx);
		GfxRenderTargetView* none[1] = {};
		ctx->OMSetRenderTargets(1, none, nullptr);
		s_Valid = true;
	}

	bool Valid() { return s_Valid; }
	GfxShaderResourceView* SRV() { return s_Valid ? s_SRV.Get() : nullptr; }
	GfxTexture2D* Texture() { return s_Valid ? s_Tex.Get() : nullptr; }

	nlohmann::json Info()
	{
		return {
			{ "enabled", s_Last.Enabled }, { "objectMotion", s_Last.ObjectMotion }, { "skinnedMotion", s_Last.SkinnedMotion },
			{ "valid", s_Valid }, { "size", { s_W, s_H } },
			{ "objectsDrawn", s_ObjectsDrawn }, { "skinnedDrawn", s_SkinnedDrawn }, { "forcedNoMotion", s_ForcedStill },
			{ "tracked", s_Tracks.size() }, { "loaded", s_Fx != nullptr } };
	}
}
