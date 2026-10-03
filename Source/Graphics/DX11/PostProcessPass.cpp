#include "pch.h"
#include "PostProcessPass.h"
#include "Effects.h"
#include "SceneCulling.h"

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

GfxRenderTargetView* PostProcessPass::Begin(UINT width, UINT height)
{
	width = (std::max)(1u, width);
	height = (std::max)(1u, height);
	if (width != m_Width || height != m_Height || (m_Scene.RTV == nullptr && m_Retry.Ready()))
	{
		m_Width = width;
		m_Height = height;
		const bool ok = CreateTarget(m_Scene, width, height, DXGI_FORMAT_R16G16B16A16_FLOAT) && CreateTarget(m_Ldr, width, height, DXGI_FORMAT_R8G8B8A8_UNORM);
		m_Down.clear();
		m_Up.clear();
		if (ok)
			m_Retry.Succeeded();
		else
		{
			m_Scene = Target();
			m_Retry.Failed();
		}
		EditorLog::Write("Volume", "post process targets %u x %u%s", width, height, ok ? "" : " (failed, retry in 1 s)");
	}
	return m_Scene.RTV.Get();
}

bool PostProcessPass::IsNeeded(const VolumeStack& stack, const CameraOptions& options)
{
	if (options.Fxaa || options.Dithering)
		return true;
	if (!options.PostProcessing)
		return false;
	// 후처리(HDR 타깃 → 합성)가 필요한 효과만. 그림자·안개·대기·환경광은 장면을 그리며 적용한다
	for (const std::string& type : VolumeComponent::Types())
		if (type != "Shadows" && type != "Fog" && type != "Atmosphere" && type != "IndirectLighting" && stack.IsActive(type))
			return true;
	return false;
}

void PostProcessPass::SetSRV(const char* name, GfxShaderResourceView* srv)
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

void PostProcessPass::Draw(const char* tech, GfxRenderTargetView* rtv, UINT w, UINT h)
{
	auto ctx = Application::GetI()->GetDeviceContext();
	GfxRenderTargetView* rtvs[1] = { rtv };
	ctx->OMSetRenderTargets(1, rtvs, nullptr);
	D3D11_VIEWPORT vp = { 0.0f, 0.0f, (float)w, (float)h, 0.0f, 1.0f };
	ctx->RSSetViewports(1, &vp);
	FxTechnique* t = m_Effect->GetFX()->GetTechniqueByName(tech);
	if (t == nullptr || !t->IsValid())
		return;
	t->GetPassByIndex(0)->Apply(0, ctx);
	ctx->Draw(3, 0);
	// 다음 단계에서 이 타깃을 입력으로 쓰므로 바인딩을 풀어 둔다
	GfxShaderResourceView* nullSRV[8] = {};
	ctx->PSSetShaderResources(0, 8, nullSRV);
	ctx->OMSetRenderTargets(0, nullptr, nullptr);
}

// 자동 노출: 장면 → 로그 휘도(256², 밉 자동 생성) → 1x1 배율 (지난 배율에서 시간에 따라 따라감)
void PostProcessPass::UpdateAutoExposure(const VolumeComponent& exposure, float dt)
{
	auto ctx = Application::GetI()->GetDeviceContext();
	auto device = Application::GetI()->GetDevice();
	if (!m_Lum.Tex)
	{
		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = desc.Height = 256;
		desc.MipLevels = 0;   // 끝까지 (1x1)
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R16G16_FLOAT;
		desc.SampleDesc.Count = 1;
		desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
		if (FAILED(device->CreateTexture2D(&desc, nullptr, m_Lum.Tex.GetAddressOf())))
			return;
		D3D11_RENDER_TARGET_VIEW_DESC rd = {};
		rd.Format = desc.Format;
		rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
		device->CreateRenderTargetView(m_Lum.Tex.Get(), &rd, m_Lum.RTV.GetAddressOf());
		device->CreateShaderResourceView(m_Lum.Tex.Get(), nullptr, m_Lum.SRV.GetAddressOf());
		m_Lum.W = m_Lum.H = 256;
		CreateTarget(m_Exposure[0], 1, 1, DXGI_FORMAT_R32_FLOAT);
		CreateTarget(m_Exposure[1], 1, 1, DXGI_FORMAT_R32_FLOAT);
		m_ExposureValid = false;
	}
	if (!m_Lum.RTV || !m_Exposure[0].RTV || !m_Exposure[1].RTV)
		return;
	SetSRV("gSource", m_Scene.SRV.Get());
	Draw("LuminanceTech", m_Lum.RTV.Get(), 256, 256);
	ctx->GenerateMips(m_Lum.SRV.Get());

	const float speedUp = (std::max)(exposure.F("speedUp"), 0.0f), speedDown = (std::max)(exposure.F("speedDown"), 0.0f);
	SetVec("gAutoExposure", std::clamp(exposure.F("middleGray"), 0.02f, 0.6f), powf(2.0f, exposure.F("limitMin")), powf(2.0f, (std::max)(exposure.F("limitMax"), exposure.F("limitMin"))),
		m_ExposureValid ? 1.0f : 0.0f);
	SetVec("gAdapt", dt * speedUp, dt * speedDown, 0.0f, 0.0f);
	const int next = 1 - m_ExposureIndex;
	SetSRV("gLumTex", m_Lum.SRV.Get());
	SetSRV("gPrevExposure", m_Exposure[m_ExposureIndex].SRV.Get());
	Draw("AdaptTech", m_Exposure[next].RTV.Get(), 1, 1);
	m_ExposureIndex = next;
	m_ExposureValid = true;
}

// Depth Of Field: 반 해상도에서 흐리게 → 전체 해상도로 섞기 (Gaussian = 먼 곳만, Bokeh = 얇은 렌즈 + 조리개 날)
GfxShaderResourceView* PostProcessPass::DepthOfField(const VolumeComponent& dof, const CameraOptions& options, GfxShaderResourceView* src)
{
	const int mode = dof.I("mode");
	const UINT hw = (std::max)(1u, m_Width / 2), hh = (std::max)(1u, m_Height / 2);
	if (m_Dof.W != m_Width || m_Dof.H != m_Height)
		CreateTarget(m_Dof, m_Width, m_Height, DXGI_FORMAT_R16G16B16A16_FLOAT);
	for (Target& t : m_Half)
		if (t.W != hw || t.H != hh)
			CreateTarget(t, hw, hh, DXGI_FORMAT_R16G16B16A16_FLOAT);
	if (!m_Dof.RTV || !m_Half[0].RTV || !m_Half[1].RTV)
		return src;
	SetSRV("gDepth", options.Depth);
	if (mode == 1)
	{
		// 반 해상도 한 칸 간격: Max Radius x 1.5 (1080p 기준) — 해상도가 달라도 같은 화면 비율로 흐리게
		const float step = dof.F("gaussianMaxRadius") * 1.5f * (float)hh / 540.0f;
		const float start = (std::max)(0.0f, dof.F("gaussianStart"));
		SetVec("gDofParams", start, (std::max)(start + 0.01f, dof.F("gaussianEnd")), step, dof.B("highQualitySampling") ? 1.0f : 0.0f);
		SetSRV("gSource", src);
		Draw("DofGaussianPrefilterTech", m_Half[0].RTV.Get(), hw, hh);
		SetSRV("gSource", m_Half[0].SRV.Get());
		Draw("DofGaussianBlurHTech", m_Half[1].RTV.Get(), hw, hh);
		SetSRV("gSource", m_Half[1].SRV.Get());
		Draw("DofGaussianBlurVTech", m_Half[0].RTV.Get(), hw, hh);
		SetSRV("gSource", src);
		SetSRV("gDofBlur", m_Half[0].SRV.Get());
		SetSRV("gDepth", options.Depth);
		Draw("DofGaussianCompositeTech", m_Dof.RTV.Get(), m_Width, m_Height);
		return m_Dof.SRV.Get();
	}
	if (mode == 2)
	{
		// 얇은 렌즈: 흐림 원 지름 (센서) = A · f / (P - f) · |1 - P / z|, A = f / 조리개. 센서 높이 24 mm (35 mm 풀프레임)
		const float f = std::clamp(dof.F("focalLength"), 1.0f, 300.0f) / 1000.0f;
		const float N = std::clamp(dof.F("aperture"), 1.0f, 32.0f);
		const float P = (std::max)(dof.F("focusDistance"), f + 0.01f);
		const float A = f / N;
		const float cocInf = A * f / (P - f);                                     // 무한대에서의 지름 (m, 센서)
		const float pxPerMeter = (float)m_Height / 0.024f;                        // 센서 → 화면 px
		const float maxRadiusFull = 32.0f * (float)m_Height / 1080.0f;            // 최대 반지름 (1080p 에서 32 px)
		const float scale = cocInf * 0.5f * pxPerMeter / (std::max)(maxRadiusFull, 1.0f);   // CoC 1 = 최대 반지름
		// 표본: 고리 4 개 (7 · 14 · 21 · 28) + 가운데 = 71 — 조리개 날 수 · 곡률 · 회전으로 모양 (곡률 1 = 원)
		XMFLOAT4 kernel[72] = {};
		int count = 0;
		kernel[count++] = XMFLOAT4(0, 0, 0, 0);
		const int blades = std::clamp(dof.I("bladeCount"), 3, 9);
		const float curvature = Clamp01(dof.F("bladeCurvature"));
		const float rotation = XMConvertToRadians(dof.F("bladeRotation"));
		const float nt = cosf(XM_PI / blades);
		for (int ring = 1; ring <= 4; ++ring)
		{
			const float r = (float)ring / 4.0f;
			const int points = ring * 7;
			for (int i = 0; i < points && count < 72; ++i)
			{
				const float phi = XM_2PI * (float)i / (float)points;
				const float dt = cosf(phi - (XM_2PI / blades) * floorf((blades * phi + XM_PI) / XM_2PI));
				const float shaped = r * (curvature + (1.0f - curvature) * nt / (std::max)(dt, 1e-3f));
				const float a = phi + rotation;
				kernel[count++] = XMFLOAT4(shaped * cosf(a), shaped * sinf(a), shaped, 0);
			}
		}
		if (auto* v = m_Effect->GetFX()->GetVariableByName("gDofKernel")->AsVector(); v && v->IsValid())
			v->SetFloatVectorArray(&kernel[0].x, 0, 72);
		SetVec("gDofParams", P, scale, maxRadiusFull * 0.5f, (float)count);
		SetSRV("gSource", src);
		Draw("DofBokehPrefilterTech", m_Half[0].RTV.Get(), hw, hh);
		SetSRV("gSource", m_Half[0].SRV.Get());
		Draw("DofBokehBlurTech", m_Half[1].RTV.Get(), hw, hh);
		SetSRV("gSource", src);
		SetSRV("gDofBlur", m_Half[1].SRV.Get());
		Draw("DofBokehCompositeTech", m_Dof.RTV.Get(), m_Width, m_Height);
		return m_Dof.SRV.Get();
	}
	return src;
}

// Motion Blur (카메라): 깊이로 되살린 월드 위치가 지난 프레임에 화면 어디였는지 → 그 방향으로 표본
GfxShaderResourceView* PostProcessPass::MotionBlur(const VolumeComponent& mb, const CameraOptions& options, GfxShaderResourceView* src)
{
	if (m_Motion.W != m_Width || m_Motion.H != m_Height)
		CreateTarget(m_Motion, m_Width, m_Height, DXGI_FORMAT_R16G16B16A16_FLOAT);
	if (!m_Motion.RTV)
		return src;
	const XMMATRIX view = XMLoadFloat4x4(&options.View);
	if (auto* v = m_Effect->GetFX()->GetVariableByName("gMBInvView")->AsMatrix(); v && v->IsValid())
	{
		const XMMATRIX m = XMMatrixInverse(nullptr, view);
		v->SetMatrix(reinterpret_cast<const float*>(&m));
	}
	if (auto* v = m_Effect->GetFX()->GetVariableByName("gMBPrevViewProj")->AsMatrix(); v && v->IsValid())
		v->SetMatrix(reinterpret_cast<const float*>(&m_PrevViewProj));
	static const int kSamples[3] = { 8, 12, 16 };   // Low · Medium · High
	SetVec("gMBParams", Clamp01(mb.F("intensity")), std::clamp(mb.F("clamp"), 0.0f, 0.2f), (float)kSamples[std::clamp(mb.I("quality"), 0, 2)], 0.0f);
	SetSRV("gDepth", options.Depth);
	SetSRV("gSource", src);
	Draw("MotionBlurTech", m_Motion.RTV.Get(), m_Width, m_Height);
	return m_Motion.SRV.Get();
}

void PostProcessPass::Execute(const VolumeStack& stack, const CameraOptions& options, GfxRenderTargetView* output)
{
	auto ctx = Application::GetI()->GetDeviceContext();
	if (!InitEffect() || m_Scene.SRV == nullptr || output == nullptr)
		return;
	m_Time += 0.016f;
	const auto now = std::chrono::steady_clock::now();
	const float dt = m_LastExecute.time_since_epoch().count() == 0 ? 0.0f : std::clamp(std::chrono::duration<float>(now - m_LastExecute).count(), 0.0f, 0.25f);
	m_LastExecute = now;

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

	// ---------------- Depth Of Field → Motion Blur (Bloom · Uber 는 그 결과를 받는다)
	GfxShaderResourceView* src = m_Scene.SRV.Get();
	if (options.Depth)
	{
		// 새 프레임이면 지난 프레임 카메라를 넘긴다 (같은 프레임에 다시 그리면 그대로)
		const uint32_t frame = SceneCulling::FrameIndex();
		if (frame != m_CurFrame)
		{
			m_PrevViewProj = m_CurViewProj;
			m_PrevValid = m_CurValid;
			m_CurFrame = frame;
		}
		XMStoreFloat4x4(&m_CurViewProj, XMMatrixMultiply(XMLoadFloat4x4(&options.View), XMLoadFloat4x4(&options.Proj)));
		m_CurValid = true;
		const XMFLOAT4X4& p = options.Proj;
		const bool ortho = fabsf(p._34) < 1e-6f;
		SetVec("gProjParams", p._11, p._22, ortho ? p._41 : p._31, ortho ? p._42 : p._32);
		SetVec("gProjFlags", ortho ? 1.0f : 0.0f, (float)m_Width, (float)m_Height, 0.0f);
		const VolumeComponent* dof = stack.Get("DepthOfField");
		if (usePost && dof && stack.IsActive("DepthOfField"))
			src = DepthOfField(*dof, options, src);
		const VolumeComponent* mb = stack.Get("MotionBlur");
		if (usePost && mb && stack.IsActive("MotionBlur") && !options.SceneView && m_PrevValid)
			src = MotionBlur(*mb, options, src);
	}

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

		SetSRV("gSource", src);
		Draw("BloomPrefilterTech", m_Down[0].RTV.Get(), m_Down[0].W, m_Down[0].H);
		for (int i = 1; i < levels; ++i)
		{
			SetSRV("gSource", m_Down[i - 1].SRV.Get());
			Draw("BloomDownTech", m_Down[i].RTV.Get(), m_Down[i].W, m_Down[i].H);
		}
		// 가장 작은 단계부터 올라오며 섞는다: up[i] = lerp(down[i], up[i+1], scatter)
		GfxShaderResourceView* lowSRV = m_Down[levels - 1].SRV.Get();
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
	// ---------------- 노출 (Volume > Exposure)
	const VolumeComponent* exposure = stack.Get("Exposure");
	const bool exposureOn = usePost && exposure && stack.IsActive("Exposure");
	const bool autoExposure = exposureOn && exposure->I("mode") == 1;
	if (autoExposure)
		UpdateAutoExposure(*exposure, dt);
	else
		m_ExposureValid = false;   // 다시 켜면 바로 목표로
	SetVec("gExposure", exposureOn ? powf(2.0f, exposure->F("compensation")) : 1.0f, autoExposure ? 1.0f : 0.0f, 0.0f, 0.0f);
	SetSRV("gExposureTex", autoExposure ? m_Exposure[m_ExposureIndex].SRV.Get() : nullptr);

	const int toneMode = (usePost && tone) ? tone->I("mode") : 0;
	SetVec("gFlags", (float)toneMode, options.Dithering ? 1.0f : 0.0f, options.StopNaNs ? 1.0f : 0.0f, gradingOn ? 1.0f : 0.0f);

	SetSRV("gSource", src);
	if (options.Fxaa)
	{
		Draw("UberTech", m_Ldr.RTV.Get(), m_Width, m_Height);
		SetSRV("gSource", m_Ldr.SRV.Get());
		Draw("FxaaTech", output, m_Width, m_Height);
	}
	else
		Draw("UberTech", output, m_Width, m_Height);
}
