#include "pch.h"
#include "Upscaler.h"
#include "RenderPipelineSettings.h"
#include "EditorLog.h"
#include "Profiler.h"

namespace Upscaler
{
	namespace
	{
		ComPtr<FxEffect> s_Fx;
		bool s_LoadFailed = false;

		struct Target2D
		{
			ComPtr<GfxTexture2D> Tex;
			ComPtr<GfxRenderTargetView> RTV;
			ComPtr<GfxShaderResourceView> SRV;
			UINT W = 0, H = 0;

			bool Ensure(UINT w, UINT h)
			{
				if (RTV && W == w && H == h)
					return true;
				Tex.Reset(); RTV.Reset(); SRV.Reset();
				D3D11_TEXTURE2D_DESC d = {};
				d.Width = (std::max)(w, 1u);
				d.Height = (std::max)(h, 1u);
				d.MipLevels = 1;
				d.ArraySize = 1;
				d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
				d.SampleDesc.Count = 1;
				d.Usage = D3D11_USAGE_DEFAULT;
				d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
				auto device = Gfx::Device();
				if (FAILED(device->CreateTexture2D(&d, nullptr, Tex.GetAddressOf()))
					|| FAILED(device->CreateShaderResourceView(Tex.Get(), nullptr, SRV.GetAddressOf()))
					|| FAILED(device->CreateRenderTargetView(Tex.Get(), nullptr, RTV.GetAddressOf())))
				{
					Tex.Reset(); RTV.Reset(); SRV.Reset();
					W = H = 0;
					return false;
				}
				W = w;
				H = h;
				return true;
			}
		};
		Target2D s_Scaled;   // 배율 해상도 (Game 카메라가 여기에 그린다)
		Target2D s_Easu;     // FSR: EASU 결과 (출력 크기) → RCAS

		nlohmann::json s_Info = { { "active", false } };
		bool s_Active = false;

		bool Load()
		{
			if (s_Fx || s_LoadFailed)
				return s_Fx != nullptr;
			std::string error;
			s_Fx = FxEffect::Load(L"../Shaders/68. Upscale.fx", error);
			if (!s_Fx)
			{
				s_LoadFailed = true;
				EditorLog::Write("Upscaler", "68. Upscale.fx failed to load: %s", error.c_str());
			}
			return s_Fx != nullptr;
		}

		void SetV(const char* name, float x, float y, float z, float w)
		{
			const float f[4] = { x, y, z, w };
			FxVar* v = s_Fx->GetVariableByName(name);
			if (v && v->IsValid())
				v->SetFloatVector(f);
		}

		void SetR(GfxShaderResourceView* srv)
		{
			FxVar* v = s_Fx->GetVariableByName("gSource");
			if (v && v->IsValid())
				v->SetResource(srv);
		}

		void Draw(const char* tech, GfxRenderTargetView* rtv, UINT w, UINT h)
		{
			FxTechnique* t = s_Fx->GetTechniqueByName(tech);
			if (t == nullptr || !t->IsValid())
				return;
			auto ctx = Application::GetI()->GetDeviceContext();
			GfxRenderTargetView* rtvs[1] = { rtv };
			ctx->OMSetRenderTargets(1, rtvs, nullptr);
			D3D11_VIEWPORT vp = { 0.0f, 0.0f, (float)w, (float)h, 0.0f, 1.0f };
			ctx->RSSetViewports(1, &vp);
			t->GetPassByIndex(0)->Apply(0, ctx);
			ctx->Draw(3, 0);
			GfxShaderResourceView* nullSRV[1] = {};
			ctx->PSSetShaderResources(0, 1, nullSRV);
			ctx->OMSetRenderTargets(0, nullptr, nullptr);
		}
	}

	GfxRenderTargetView* Target(UINT width, UINT height)
	{
		return s_Scaled.Ensure(width, height) ? s_Scaled.RTV.Get() : nullptr;
	}

	void NoteNative(UINT width, UINT height)
	{
		s_Active = false;
		s_Info = { { "active", false }, { "input", { width, height } }, { "output", { width, height } }, { "filter", "None" } };
	}

	void Resolve(GfxRenderTargetView* output, UINT outW, UINT outH)
	{
		if (output == nullptr || !s_Scaled.SRV || outW == 0 || outH == 0 || !Load())
			return;
		PROFILE_GPU("Upscale");
		namespace RPS = RenderPipelineSettings;
		const UINT inW = s_Scaled.W, inH = s_Scaled.H;
		RPS::UpscalingFilter filter = RPS::GetUpscalingFilter();
		const bool upscaling = inW < outW || inH < outH;
		if (filter == RPS::UpscalingFilter::FSR && !upscaling)
			filter = RPS::UpscalingFilter::Bilinear;   // FSR 은 늘릴 때만 (URP)
		if (filter == RPS::UpscalingFilter::Automatic)
		{
			// 정수 배율 (가로 · 세로 같은 배수) 이면 Nearest, 아니면 Bilinear
			const bool integer = upscaling && outW % inW == 0 && outH % inH == 0 && outW / inW == outH / inH;
			filter = integer ? RPS::UpscalingFilter::NearestNeighbor : RPS::UpscalingFilter::Bilinear;
		}

		auto ctx = Application::GetI()->GetDeviceContext();
		ctx->IASetInputLayout(nullptr);
		ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		ctx->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
		ctx->OMSetDepthStencilState(nullptr, 0);
		ctx->OMSetBlendState(nullptr, nullptr, 0xffffffff);
		ctx->RSSetState(nullptr);

		const float rx = (float)inW / (float)outW, ry = (float)inH / (float)outH;
		SetV("gSize", (float)inW, (float)inH, (float)outW, (float)outH);
		SetV("gEasuCon", rx, ry, 0.5f * rx - 0.5f, 0.5f * ry - 0.5f);
		SetR(s_Scaled.SRV.Get());
		float sharpness = 0.0f;
		if (filter == RPS::UpscalingFilter::FSR && s_Easu.Ensure(outW, outH))
		{
			Draw("EasuTech", s_Easu.RTV.Get(), outW, outH);
			// URP: 선형 날카로움 → stops (최대 2.5) → exp2(-stops)
			sharpness = RPS::EffectiveFsrSharpness();
			const float stops = (1.0f - sharpness) * 2.5f;
			SetV("gRcasCon", std::exp2(-stops), 0.0f, 0.0f, 0.0f);
			SetR(s_Easu.SRV.Get());
			Draw("RcasTech", output, outW, outH);
		}
		else
		{
			if (filter == RPS::UpscalingFilter::FSR)
				filter = RPS::UpscalingFilter::Bilinear;   // EASU 타깃을 못 만들면
			Draw(filter == RPS::UpscalingFilter::NearestNeighbor ? "NearestTech" : "BilinearTech", output, outW, outH);
		}
		s_Active = true;
		s_Info = { { "active", true }, { "input", { inW, inH } }, { "output", { outW, outH } }, { "filter", RPS::UpscalingFilterName(filter) } };
		if (filter == RPS::UpscalingFilter::FSR)
			s_Info["sharpness"] = sharpness;
	}

	nlohmann::json Info() { return s_Info; }
	bool Active() { return s_Active; }
}
