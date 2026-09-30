#include "pch.h"
#include "PostProcessPass.h"
#include "Effects.h"

namespace
{
	float Clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }

	// Unity 의 ColorUtils.ColorBalanceToLMSCoeffs (White Balance: 온도/색조 → LMS 계수)
	void WhiteBalanceCoeffs(float temperature, float tint, float out[3])
	{
		const float t1 = temperature / 65.0f;
		const float t2 = tint / 65.0f;
		const float x = 0.31271f - t1 * (t1 < 0.0f ? 0.1f : 0.05f);
		const float standardIlluminantY = 2.87f * x - 3.0f * x * x - 0.27509507f;
		const float y = standardIlluminantY + t2 * 0.05f;

		// 기준 백색(D65)과 목표 백색의 LMS
		auto CIExyToLMS = [](float x, float y, float lms[3]) {
			const float Y = 1.0f;
			const float X = Y * x / y;
			const float Z = Y * (1.0f - x - y) / y;
			lms[0] = 0.7328f * X + 0.4296f * Y - 0.1624f * Z;
			lms[1] = -0.7036f * X + 1.6975f * Y + 0.0061f * Z;
			lms[2] = 0.0030f * X + 0.0136f * Y + 0.9834f * Z;
		};
		float w1[3], w2[3];
		CIExyToLMS(0.31271f, 0.32902f, w1);
		CIExyToLMS(x, y, w2);
		out[0] = w1[0] / w2[0];
		out[1] = w1[1] / w2[1];
		out[2] = w1[2] / w2[2];
	}
}

PostProcessPass::PostProcessPass() {}
PostProcessPass::~PostProcessPass() {}

bool PostProcessPass::InitEffect()
{
	if (m_Effect || m_EffectFailed)
		return m_Effect != nullptr;
	m_Effect = std::make_unique<Effect>(Application::GetI()->GetDevice(), L"../Shaders/41. PostProcess.fx");
	if (m_Effect->GetFX() == nullptr)
	{
		m_Effect.reset();
		m_EffectFailed = true;
		EditorLog::Write("Volume", "PostProcess.fx failed to load - post processing disabled");
		return false;
	}
	return true;
}

bool PostProcessPass::CreateTarget(Target& t, UINT w, UINT h, DXGI_FORMAT format)
{
	t = Target();
	auto device = Application::GetI()->GetDevice();
	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = (std::max)(1u, w);
	desc.Height = (std::max)(1u, h);
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = format;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	if (FAILED(device->CreateTexture2D(&desc, nullptr, t.Tex.GetAddressOf())))
		return false;
	if (FAILED(device->CreateRenderTargetView(t.Tex.Get(), nullptr, t.RTV.GetAddressOf())))
		return false;
	if (FAILED(device->CreateShaderResourceView(t.Tex.Get(), nullptr, t.SRV.GetAddressOf())))
		return false;
	t.W = desc.Width;
	t.H = desc.Height;
	return true;
}

ID3D11RenderTargetView* PostProcessPass::Begin(UINT width, UINT height)
{
	width = (std::max)(1u, width);
	height = (std::max)(1u, height);
	if (width != m_Width || height != m_Height || m_Scene.RTV == nullptr)
	{
		m_Width = width;
		m_Height = height;
		CreateTarget(m_Scene, width, height, DXGI_FORMAT_R16G16B16A16_FLOAT);
		CreateTarget(m_Ldr, width, height, DXGI_FORMAT_R8G8B8A8_UNORM);
		m_Down.clear();
		m_Up.clear();
		EditorLog::Write("Volume", "post process targets %u x %u", width, height);
	}
	return m_Scene.RTV.Get();
}

bool PostProcessPass::IsNeeded(const VolumeStack& stack, const CameraOptions& options)
{
	if (options.Fxaa || options.Dithering)
		return true;
	if (!options.PostProcessing)
		return false;
	for (const std::string& type : VolumeComponent::Types())
		if (stack.IsActive(type))
			return true;
	return false;
}

void PostProcessPass::SetSRV(const char* name, ID3D11ShaderResourceView* srv)
{
	if (auto* v = m_Effect->GetFX()->GetVariableByName(name)->AsShaderResource(); v && v->IsValid())
		v->SetResource(srv);
}

void PostProcessPass::SetVec(const char* name, float x, float y, float z, float w)
{
	const float f[4] = { x, y, z, w };
	if (auto* v = m_Effect->GetFX()->GetVariableByName(name)->AsVector(); v && v->IsValid())
		v->SetFloatVector(f);
}

void PostProcessPass::Draw(const char* tech, ID3D11RenderTargetView* rtv, UINT w, UINT h)
{
	auto ctx = Application::GetI()->GetDeviceContext();
	ID3D11RenderTargetView* rtvs[1] = { rtv };
	ctx->OMSetRenderTargets(1, rtvs, nullptr);
	D3D11_VIEWPORT vp = { 0.0f, 0.0f, (float)w, (float)h, 0.0f, 1.0f };
	ctx->RSSetViewports(1, &vp);
	ID3DX11EffectTechnique* t = m_Effect->GetFX()->GetTechniqueByName(tech);
	if (t == nullptr || !t->IsValid())
		return;
	t->GetPassByIndex(0)->Apply(0, ctx);
	ctx->Draw(3, 0);
	// 다음 단계에서 이 타깃을 입력으로 쓰므로 바인딩을 풀어 둔다
	ID3D11ShaderResourceView* nullSRV[8] = {};
	ctx->PSSetShaderResources(0, 8, nullSRV);
	ctx->OMSetRenderTargets(0, nullptr, nullptr);
}

void PostProcessPass::Execute(const VolumeStack& stack, const CameraOptions& options, ID3D11RenderTargetView* output)
{
	auto ctx = Application::GetI()->GetDeviceContext();
	if (!InitEffect() || m_Scene.SRV == nullptr || output == nullptr)
		return;
	m_Time += 0.016f;

	ctx->IASetInputLayout(nullptr);
	ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	ctx->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
	ctx->OMSetDepthStencilState(nullptr, 0);
	ctx->OMSetBlendState(nullptr, nullptr, 0xffffffff);
	ctx->RSSetState(nullptr);

	const bool usePost = options.PostProcessing;
	const VolumeComponent* bloom = stack.Get("Bloom");
	const VolumeComponent* tone = stack.Get("Tonemapping");
	const VolumeComponent* color = stack.Get("ColorAdjustments");
	const VolumeComponent* wb = stack.Get("WhiteBalance");
	const VolumeComponent* vig = stack.Get("Vignette");
	const VolumeComponent* ca = stack.Get("ChromaticAberration");
	const VolumeComponent* grain = stack.Get("FilmGrain");

	// ---------------- Bloom
	const bool bloomOn = usePost && bloom && stack.IsActive("Bloom");
	if (bloomOn)
	{
		const float thresholdGamma = (std::max)(0.0f, bloom->F("threshold"));
		const float threshold = powf(thresholdGamma, 2.2f);   // Unity: 임계값은 감마 공간 값
		const float scatter = 0.05f + Clamp01(bloom->F("scatter")) * 0.9f;   // Unity: lerp(0.05, 0.95, scatter)
		SetVec("gBloomParams", threshold, threshold * 0.5f, (std::max)(0.0f, bloom->F("clamp")), scatter);

		// 절반(또는 1/4) 해상도에서 시작해 한 변이 2px 이 될 때까지, 최대 Max Iterations 단계
		const UINT div = bloom->I("downscale") == 1 ? 4u : 2u;
		UINT w = (std::max)(1u, m_Width / div), h = (std::max)(1u, m_Height / div);
		int levels = std::clamp(bloom->I("maxIterations"), 2, 8);
		const int maxByRes = (int)floorf(log2f((float)(std::max)(w, h))) - 1;
		levels = (std::max)(1, (std::min)(levels, maxByRes));
		if ((int)m_Down.size() != levels || m_Down[0].W != w || m_Down[0].H != h)
		{
			m_Down.assign(levels, Target());
			m_Up.assign(levels, Target());
			UINT lw = w, lh = h;
			for (int i = 0; i < levels; ++i)
			{
				CreateTarget(m_Down[i], lw, lh, DXGI_FORMAT_R16G16B16A16_FLOAT);
				CreateTarget(m_Up[i], lw, lh, DXGI_FORMAT_R16G16B16A16_FLOAT);
				lw = (std::max)(1u, lw / 2);
				lh = (std::max)(1u, lh / 2);
			}
		}

		SetSRV("gSource", m_Scene.SRV.Get());
		Draw("BloomPrefilterTech", m_Down[0].RTV.Get(), m_Down[0].W, m_Down[0].H);
		for (int i = 1; i < levels; ++i)
		{
			SetSRV("gSource", m_Down[i - 1].SRV.Get());
			Draw("BloomDownTech", m_Down[i].RTV.Get(), m_Down[i].W, m_Down[i].H);
		}
		// 가장 작은 단계부터 올라오며 섞는다: up[i] = lerp(down[i], up[i+1], scatter)
		ID3D11ShaderResourceView* lowSRV = m_Down[levels - 1].SRV.Get();
		for (int i = levels - 2; i >= 0; --i)
		{
			SetSRV("gSource", m_Down[i].SRV.Get());
			SetSRV("gBloomLow", lowSRV);
			Draw("BloomUpTech", m_Up[i].RTV.Get(), m_Up[i].W, m_Up[i].H);
			lowSRV = m_Up[i].SRV.Get();
		}
		const float* tint = bloom->V("tint");
		const float intensity = (std::max)(0.0f, bloom->F("intensity"));
		SetVec("gBloomTint", tint[0] * intensity, tint[1] * intensity, tint[2] * intensity, 1.0f);
		SetSRV("gBloomTex", levels > 1 ? m_Up[0].SRV.Get() : m_Down[0].SRV.Get());
	}
	else
		SetVec("gBloomTint", 0, 0, 0, 0);

	// ---------------- Uber
	const bool gradingOn = usePost && (stack.IsActive("ColorAdjustments") || stack.IsActive("WhiteBalance"));
	{
		const float exposure = color ? powf(2.0f, color->F("postExposure")) : 1.0f;
		const float contrast = color ? 1.0f + color->F("contrast") / 100.0f : 1.0f;
		const float hue = color ? color->F("hueShift") / 360.0f : 0.0f;
		const float sat = color ? 1.0f + color->F("saturation") / 100.0f : 1.0f;
		SetVec("gGrading", exposure, contrast, hue, sat);
		// 컬러 필터는 감마 공간에서 고른 색 → 선형
		const float* cf = color ? color->V("colorFilter") : nullptr;
		SetVec("gColorFilter", cf ? powf((std::max)(0.0f, cf[0]), 2.2f) : 1.0f, cf ? powf((std::max)(0.0f, cf[1]), 2.2f) : 1.0f, cf ? powf((std::max)(0.0f, cf[2]), 2.2f) : 1.0f, 1.0f);
		float lms[3] = { 1, 1, 1 };
		const bool wbOn = usePost && stack.IsActive("WhiteBalance");
		if (wbOn)
			WhiteBalanceCoeffs(wb->F("temperature"), wb->F("tint"), lms);
		SetVec("gWhiteBalance", lms[0], lms[1], lms[2], wbOn ? 1.0f : 0.0f);
	}
	if (usePost && stack.IsActive("Vignette"))
	{
		const float* c = vig->V("color");
		const float* center = vig->V("center");
		SetVec("gVignetteColor", powf(c[0], 2.2f), powf(c[1], 2.2f), powf(c[2], 2.2f), Clamp01(vig->F("intensity")) * 3.0f);
		const float aspect = (float)m_Width / (float)(std::max)(1u, m_Height);
		SetVec("gVignetteParams", center[0], center[1], (std::max)(0.01f, vig->F("smoothness")) * 5.0f, vig->B("rounded") ? aspect : 1.0f);
	}
	else
		SetVec("gVignetteColor", 0, 0, 0, 0);
	const float caAmount = (usePost && stack.IsActive("ChromaticAberration")) ? Clamp01(ca->F("intensity")) * 0.05f : 0.0f;
	const float grainAmount = (usePost && stack.IsActive("FilmGrain")) ? Clamp01(grain->F("intensity")) : 0.0f;
	// 그레인 종류: 번호가 클수록 거친 입자 → 세기에 약간 반영
	const float grainScale = grain ? 1.0f + grain->I("type") * 0.08f : 1.0f;
	SetVec("gGrain", grainAmount * grainScale, grain ? Clamp01(grain->F("response")) : 0.8f, fmodf(m_Time * 60.0f, 1000.0f), caAmount);
	const int toneMode = (usePost && tone) ? tone->I("mode") : 0;
	SetVec("gFlags", (float)toneMode, options.Dithering ? 1.0f : 0.0f, options.StopNaNs ? 1.0f : 0.0f, gradingOn ? 1.0f : 0.0f);

	SetSRV("gSource", m_Scene.SRV.Get());
	if (options.Fxaa)
	{
		Draw("UberTech", m_Ldr.RTV.Get(), m_Width, m_Height);
		SetSRV("gSource", m_Ldr.SRV.Get());
		Draw("FxaaTech", output, m_Width, m_Height);
	}
	else
		Draw("UberTech", output, m_Width, m_Height);
}
