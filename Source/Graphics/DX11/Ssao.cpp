#include "pch.h"
#include "Ssao.h"
#include "Effects.h"
#include "Vertex.h"
#include "MathHelper.h"
#include "VolumeProfile.h"
#include <random>

namespace
{
	FxVar* Var(FxEffect* fx, const char* name)
	{
		FxVar* v = fx ? fx->GetVariableByName(name) : nullptr;
		return v && v->IsValid() ? v : nullptr;
	}
	void SetV(FxEffect* fx, const char* name, float x, float y, float z, float w) { const float f[4] = { x, y, z, w }; if (FxVar* v = Var(fx, name)) v->SetFloatVector(f); }
	void SetM(FxEffect* fx, const char* name, const XMFLOAT4X4& m) { if (FxVar* v = Var(fx, name)) v->SetMatrix(&m._11); }
	void SetR(FxEffect* fx, const char* name, GfxShaderResourceView* srv) { if (FxVar* v = Var(fx, name)) v->SetResource(srv); }
	FxTechnique* Tech(FxEffect* fx, const char* name)
	{
		FxTechnique* t = fx ? fx->GetTechniqueByName(name) : nullptr;
		return t && t->IsValid() ? t : nullptr;
	}
	const float kWhite[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
}

Ssao::Settings Ssao::Settings::FromStack(const VolumeStack& stack)
{
	Settings s;
	if (const VolumeComponent* c = stack.Get("AmbientOcclusion"))
	{
		s.Enabled = c->B("enabled");
		s.Intensity = (std::max)(0.0f, c->F("intensity"));
		s.Radius = (std::max)(0.01f, c->F("radius"));
		s.DirectLightingStrength = std::clamp(c->F("directLightingStrength"), 0.0f, 1.0f);
		s.Samples = std::clamp(c->I("samples"), 0, 2);
		s.FalloffDistance = (std::max)(0.1f, c->F("falloffDistance"));
		s.TemporalAccumulation = c->B("temporalAccumulation");
		s.FullResolution = c->B("fullResolution");
	}
	return s;
}

bool Ssao::Settings::SameHistory(const Settings& o) const
{
	return Enabled == o.Enabled && Intensity == o.Intensity && Radius == o.Radius && Samples == o.Samples && FalloffDistance == o.FalloffDistance
		&& TemporalAccumulation == o.TemporalAccumulation && FullResolution == o.FullResolution;
}

Ssao::Ssao()
{
}

Ssao::~Ssao()
{
}

ComPtr<GfxShaderResourceView> Ssao::NormalDepthSRV()
{
	return _normalDepthSRV;
}

ComPtr<GfxShaderResourceView> Ssao::AmbientSRV()
{
	return _aoFullSRV;
}

void Ssao::Init(int32 width, int32 height)
{
	_device = Application::GetI()->GetDevice();
	_deviceContext = Application::GetI()->GetDeviceContext();
	OnSize(width, height);
	BuildFullScreenQuad();
	BuildOffsetVectors();
	BuildRandomVectorTexture();
}

void Ssao::OnSize(int32 width, int32 height)
{
	_renderTargetWidth = (uint32)(std::max)(width, 2);
	_renderTargetHeight = (uint32)(std::max)(height, 2);
	_fullViewport = { 0.0f, 0.0f, (float)_renderTargetWidth, (float)_renderTargetHeight, 0.0f, 1.0f };
	BuildTextureViews();
	BuildAoTargets();
}

void Ssao::SetNormalDepthRenderTarget(ComPtr<GfxDepthStencilView> dsv)
{
	GfxRenderTargetView* renderTargets[1] = { _normalDepthRTV.Get() };
	_deviceContext->OMSetRenderTargets(1, renderTargets, dsv.Get());

	// 비운 자리 = 뷰 노멀 (0,0,-1) · 아주 먼 깊이 (가리지도 가려지지도 않는다)
	const float clearColor[] = { 0.0f, 0.0f, -1.0f, 1e5f };
	_deviceContext->ClearRenderTargetView(_normalDepthRTV.Get(), clearColor);
}

void Ssao::Render(CXMMATRIX view, CXMMATRIX proj, const Settings& settings)
{
	_last = settings;
	if (!settings.Active() || !_aoFullRTV)
	{
		// 꺼짐: 흰 맵 (가림 없음) — 셰이더는 그대로 곱한다
		_deviceContext->ClearRenderTargetView(_aoFullRTV.Get(), kWhite);
		_historyValid = false;
		return;
	}
	const uint32 scale = settings.FullResolution ? 1u : 2u;
	if (scale != _aoScale)
	{
		_aoScale = scale;
		BuildAoTargets();
	}
	if (!settings.SameHistory(_historySettings))
		_historyValid = false;
	_historySettings = settings;
	++_frame;

	SetCorners(proj);
	Compute(proj, settings);

	GfxShaderResourceView* src = _aoRaw.SRV.Get();
	if (settings.TemporalAccumulation)
	{
		Temporal(view);
		src = _history[_historyIndex].SRV.Get();
	}
	else
	{
		_historyValid = false;
		_accumulated = 0;
	}

	// 흐림: 시간 누적이면 한 번 (표본이 프레임마다 돌아 이미 고르다), 아니면 두 번
	const int passes = settings.TemporalAccumulation ? 1 : 2;
	for (int i = 0; i < passes; ++i)
	{
		Blur(src, _aoTemp.RTV.Get(), true);
		Blur(_aoTemp.SRV.Get(), _aoBlur.RTV.Get(), false);
		src = _aoBlur.SRV.Get();
	}
	Upsample(_aoBlur.SRV.Get());

	// 다음 프레임: 이 뷰 · 투영 · 히스토리
	XMStoreFloat4x4(&_prevView, view);
	XMStoreFloat4x4(&_prevProj, proj);
	if (settings.TemporalAccumulation)
	{
		_historyValid = true;
		_historyIndex ^= 1;
	}
}

void Ssao::DrawQuad(FxTechnique* tech)
{
	const uint32 stride = sizeof(Vertex::Basic32);
	const uint32 offset = 0;
	_deviceContext->IASetInputLayout(InputLayouts::Basic32.Get());
	_deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	_deviceContext->IASetVertexBuffers(0, 1, _screenQuadVB.GetAddressOf(), &stride, &offset);
	_deviceContext->IASetIndexBuffer(_screenQuadIB.Get(), DXGI_FORMAT_R16_UINT, 0);
	D3DX11_TECHNIQUE_DESC techDesc;
	tech->GetDesc(&techDesc);
	for (uint32 p = 0; p < techDesc.Passes; ++p)
	{
		tech->GetPassByIndex(p)->Apply(0, _deviceContext.Get());
		_deviceContext->DrawIndexed(6, 0, 0);
	}
}

void Ssao::SetCorners(CXMMATRIX proj)
{
	// 화면 네 구석의 뷰 방향 (z = 1) — 이 프레임의 투영에서 (시야각 · 화면비 · TAA 지터가 바뀌어도 맞다). 셰이더는 p = 깊이 / z × 방향
	XMFLOAT4X4 P;
	XMStoreFloat4x4(&P, proj);
	const float hw = P._11 != 0.0f ? 1.0f / P._11 : 1.0f;
	const float hh = P._22 != 0.0f ? 1.0f / P._22 : 1.0f;
	const float cx = P._31 * hw, cy = P._32 * hh;   // 지터 (투영 중심 이동) 만큼 방향도 옮긴다
	const XMFLOAT4 corners[4] = { { -hw - cx, -hh - cy, 1.0f, 0.0f }, { -hw - cx, +hh - cy, 1.0f, 0.0f }, { +hw - cx, +hh - cy, 1.0f, 0.0f }, { +hw - cx, -hh - cy, 1.0f, 0.0f } };
	Effects::SsaoFX->SetFrustumCorners(corners);
	FxEffect* fx = Effects::SsaoFX->GetFX();
	SetV(fx, "gAoSize", (float)_aoW, (float)_aoH, (float)_aoScale, 0.0f);
}

void Ssao::Compute(CXMMATRIX proj, const Settings& s)
{
	// 깊이 버퍼 없이 AO 타깃에만 (깊이 검사 없음)
	GfxRenderTargetView* renderTargets[1] = { _aoRaw.RTV.Get() };
	_deviceContext->OMSetRenderTargets(1, renderTargets, 0);
	_deviceContext->RSSetViewports(1, &_aoViewport);

	// NDC [-1,+1]^2 → 텍스처 [0,1]^2
	static const XMMATRIX T(
		0.5f, 0.0f, 0.0f, 0.0f,
		0.0f, -0.5f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.5f, 0.5f, 0.0f, 1.0f);
	Effects::SsaoFX->SetViewToTexSpace(XMMatrixMultiply(proj, T));
	Effects::SsaoFX->SetOffsetVectors(_offsets);

	// 반지름에 맞춘 가림 곡선 (예전 고정값 0.5 m · 0.2 · 2.0 · 0.05 와 같은 비율)
	Effects::SsaoFX->SetOcclusionRadius(s.Radius);
	Effects::SsaoFX->SetOcclusionFadeStart(s.Radius * 0.4f);
	Effects::SsaoFX->SetOcclusionFadeEnd(s.Radius * 4.0f);
	Effects::SsaoFX->SetSurfaceEpsilon(s.Radius * 0.1f);
	// 무작위 무늬: 시간 누적이면 프레임마다 옮긴다 (R2 수열 — 고르게 퍼진다), 아니면 고정
	float jx = 0.0f, jy = 0.0f;
	if (s.TemporalAccumulation)
	{
		jx = fmodf((float)_frame * 0.7548776662f, 1.0f);
		jy = fmodf((float)_frame * 0.5698402910f, 1.0f);
	}
	SetV(Effects::SsaoFX->GetFX(), "gSsaoParams", s.Intensity, s.FalloffDistance, jx, jy);
	Effects::SsaoFX->SetNormalDepthMap(_normalDepthSRV.Get());
	Effects::SsaoFX->SetRandomVecMap(_randomVectorSRV.Get());

	FxTechnique* tech = s.Samples <= 0 ? Effects::SsaoFX->SsaoLowTech.Get() : (s.Samples == 1 ? Effects::SsaoFX->SsaoTech.Get() : Effects::SsaoFX->SsaoHighTech.Get());
	DrawQuad(tech ? tech : Effects::SsaoFX->SsaoTech.Get());
}

void Ssao::Temporal(CXMMATRIX view)
{
	FxEffect* fx = Effects::SsaoFX->GetFX();
	FxTechnique* tech = Tech(fx, "Temporal");
	Target& out = _history[_historyIndex];
	if (!tech || !out.RTV)
		return;
	// 이번 뷰 공간 → 지난 프레임 클립 = 이번 뷰의 역 × 지난 뷰 × 지난 투영
	XMFLOAT4X4 m;
	XMStoreFloat4x4(&m, XMMatrixInverse(nullptr, view) * XMLoadFloat4x4(&_prevView) * XMLoadFloat4x4(&_prevProj));
	SetM(fx, "gCurViewToPrevClip", m);
	SetV(fx, "gTemporal", 0.9f, _historyValid ? 1.0f : 0.0f, 0.0f, 0.0f);
	_accumulated = _historyValid ? _accumulated + 1 : 0;

	GfxRenderTargetView* rtv[1] = { out.RTV.Get() };
	_deviceContext->OMSetRenderTargets(1, rtv, 0);
	_deviceContext->RSSetViewports(1, &_aoViewport);
	SetR(fx, "gAoRaw", _aoRaw.SRV.Get());
	SetR(fx, "gAoHistory", _history[_historyIndex ^ 1].SRV.Get());
	DrawQuad(tech);
	SetR(fx, "gAoRaw", nullptr);
	SetR(fx, "gAoHistory", nullptr);
	tech->GetPassByIndex(0)->Apply(0, _deviceContext.Get());
}

void Ssao::Blur(GfxShaderResourceView* input, GfxRenderTargetView* output, bool horzBlur)
{
	GfxRenderTargetView* renderTargets[1] = { output };
	_deviceContext->OMSetRenderTargets(1, renderTargets, 0);
	_deviceContext->RSSetViewports(1, &_aoViewport);

	Effects::SsaoBlurFX->SetTexelWidth(1.0f / (float)_aoW);
	Effects::SsaoBlurFX->SetTexelHeight(1.0f / (float)_aoH);
	SetV(Effects::SsaoBlurFX->GetFX(), "gBlurSize", (float)_aoW, (float)_aoH, (float)_aoScale, 0.0f);
	Effects::SsaoBlurFX->SetNormalDepthMap(_normalDepthSRV.Get());
	Effects::SsaoBlurFX->SetInputImage(input);
	FxTechnique* tech = horzBlur ? Effects::SsaoBlurFX->HorzBlurTech.Get() : Effects::SsaoBlurFX->VertBlurTech.Get();
	DrawQuad(tech);

	// 다음 흐림에서 출력이 되므로 입력을 뗀다
	Effects::SsaoBlurFX->SetInputImage(nullptr);
	tech->GetPassByIndex(0)->Apply(0, _deviceContext.Get());
}

void Ssao::Upsample(GfxShaderResourceView* input)
{
	FxEffect* fx = Effects::SsaoFX->GetFX();
	FxTechnique* tech = Tech(fx, "Upsample");
	if (!tech)
		return;
	GfxRenderTargetView* rtv[1] = { _aoFullRTV.Get() };
	_deviceContext->OMSetRenderTargets(1, rtv, 0);
	_deviceContext->RSSetViewports(1, &_fullViewport);
	SetR(fx, "gAoRaw", input);
	Effects::SsaoFX->SetNormalDepthMap(_normalDepthSRV.Get());
	DrawQuad(tech);
	SetR(fx, "gAoRaw", nullptr);
	Effects::SsaoFX->SetNormalDepthMap(nullptr);
	tech->GetPassByIndex(0)->Apply(0, _deviceContext.Get());
}

void Ssao::BuildFullScreenQuad()
{
	Vertex::Basic32 v[4];
	v[0].pos = XMFLOAT3(-1.0f, -1.0f, 0.0f);
	v[1].pos = XMFLOAT3(-1.0f, +1.0f, 0.0f);
	v[2].pos = XMFLOAT3(+1.0f, +1.0f, 0.0f);
	v[3].pos = XMFLOAT3(+1.0f, -1.0f, 0.0f);

	// 화면 구석 번호 (gFrustumCorners) 를 노멀 x 에
	v[0].normal = XMFLOAT3(0.0f, 0.0f, 0.0f);
	v[1].normal = XMFLOAT3(1.0f, 0.0f, 0.0f);
	v[2].normal = XMFLOAT3(2.0f, 0.0f, 0.0f);
	v[3].normal = XMFLOAT3(3.0f, 0.0f, 0.0f);

	v[0].tex = XMFLOAT2(0.0f, 1.0f);
	v[1].tex = XMFLOAT2(0.0f, 0.0f);
	v[2].tex = XMFLOAT2(1.0f, 0.0f);
	v[3].tex = XMFLOAT2(1.0f, 1.0f);

	D3D11_BUFFER_DESC vbd = {};
	vbd.Usage = D3D11_USAGE_IMMUTABLE;
	vbd.ByteWidth = sizeof(Vertex::Basic32) * 4;
	vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	D3D11_SUBRESOURCE_DATA vinitData = {};
	vinitData.pSysMem = v;
	HR(_device->CreateBuffer(&vbd, &vinitData, _screenQuadVB.GetAddressOf()));

	const USHORT indices[6] = { 0, 1, 2, 0, 2, 3 };
	D3D11_BUFFER_DESC ibd = {};
	ibd.Usage = D3D11_USAGE_IMMUTABLE;
	ibd.ByteWidth = sizeof(USHORT) * 6;
	ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
	D3D11_SUBRESOURCE_DATA iinitData = {};
	iinitData.pSysMem = indices;
	HR(_device->CreateBuffer(&ibd, &iinitData, _screenQuadIB.GetAddressOf()));
}

bool Ssao::MakeTarget(Target& t, uint32 w, uint32 h, DXGI_FORMAT format)
{
	D3D11_TEXTURE2D_DESC d = {};
	d.Width = (std::max)(w, 1u);
	d.Height = (std::max)(h, 1u);
	d.MipLevels = 1;
	d.ArraySize = 1;
	d.Format = format;
	d.SampleDesc.Count = 1;
	d.Usage = D3D11_USAGE_DEFAULT;
	d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
	t = Target();
	if (FAILED(_device->CreateTexture2D(&d, 0, t.Tex.GetAddressOf())))
		return false;
	return SUCCEEDED(_device->CreateShaderResourceView(t.Tex.Get(), 0, t.SRV.GetAddressOf()))
		&& SUCCEEDED(_device->CreateRenderTargetView(t.Tex.Get(), 0, t.RTV.GetAddressOf()));
}

void Ssao::BuildTextureViews()
{
	// 노멀 · 깊이 (전체 해상도) · 전체 해상도 AO (32 가 읽는다)
	Target nd, full;
	MakeTarget(nd, _renderTargetWidth, _renderTargetHeight, DXGI_FORMAT_R16G16B16A16_FLOAT);
	_normalDepthSRV = nd.SRV;
	_normalDepthRTV = nd.RTV;
	MakeTarget(full, _renderTargetWidth, _renderTargetHeight, DXGI_FORMAT_R16_FLOAT);
	_aoFullTex = full.Tex;
	_aoFullSRV = full.SRV;
	_aoFullRTV = full.RTV;
	// 처음 = 가림 없음 (계산 전에 그려도 어둡지 않게)
	if (_aoFullRTV)
		_deviceContext->ClearRenderTargetView(_aoFullRTV.Get(), kWhite);
}

void Ssao::BuildAoTargets()
{
	// 계산 크기 = 전체 또는 반 (반이면 2 x 2 픽셀마다 텍셀 하나)
	_aoW = (std::max)(_renderTargetWidth / _aoScale, 1u);
	_aoH = (std::max)(_renderTargetHeight / _aoScale, 1u);
	_aoViewport = { 0.0f, 0.0f, (float)_aoW, (float)_aoH, 0.0f, 1.0f };
	MakeTarget(_aoRaw, _aoW, _aoH, DXGI_FORMAT_R16_FLOAT);
	MakeTarget(_aoTemp, _aoW, _aoH, DXGI_FORMAT_R16_FLOAT);
	MakeTarget(_aoBlur, _aoW, _aoH, DXGI_FORMAT_R16_FLOAT);
	MakeTarget(_history[0], _aoW, _aoH, DXGI_FORMAT_R16G16_FLOAT);
	MakeTarget(_history[1], _aoW, _aoH, DXGI_FORMAT_R16G16_FLOAT);
	_historyValid = false;
	_accumulated = 0;
}

void Ssao::BuildRandomVectorTexture()
{
	D3D11_TEXTURE2D_DESC texDesc = {};
	texDesc.Width = 256;
	texDesc.Height = 256;
	texDesc.MipLevels = 1;
	texDesc.ArraySize = 1;
	texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	texDesc.SampleDesc.Count = 1;
	texDesc.Usage = D3D11_USAGE_IMMUTABLE;
	texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

	// 고정 씨앗 — 실행마다 같은 무늬 (검사 · 캡처가 흔들리지 않게). RGBA8 = 무작위 방향 (셰이더가 [-1, 1] 로 펴서 정규화)
	//  (예전엔 float4 를 RGBA8 텍스처에 그대로 올려 방향이 float 의 바이트였다)
	std::mt19937 gen(1337u);
	std::uniform_int_distribution<uint32> dis(0u, 255u);
	std::vector<uint32> packed(256 * 256);
	for (uint32& p : packed)
		p = dis(gen) | (dis(gen) << 8) | (dis(gen) << 16);

	D3D11_SUBRESOURCE_DATA initData = {};
	initData.pSysMem = packed.data();
	initData.SysMemPitch = 256 * sizeof(uint32);

	ComPtr<GfxTexture2D> tex;
	HR(_device->CreateTexture2D(&texDesc, &initData, tex.GetAddressOf()));
	HR(_device->CreateShaderResourceView(tex.Get(), 0, _randomVectorSRV.GetAddressOf()));
}

void Ssao::BuildOffsetVectors()
{
	// 고르게 퍼진 14 방향: 정육면체 꼭짓점 8 + 면 가운데 6. 반대쪽끼리 번갈아 두어 앞의 4 · 8 개만 써도 고르게 퍼진다
	_offsets[0] = XMFLOAT4(+1.0f, +1.0f, +1.0f, 0.0f);
	_offsets[1] = XMFLOAT4(-1.0f, -1.0f, -1.0f, 0.0f);
	_offsets[2] = XMFLOAT4(-1.0f, +1.0f, +1.0f, 0.0f);
	_offsets[3] = XMFLOAT4(+1.0f, -1.0f, -1.0f, 0.0f);
	_offsets[4] = XMFLOAT4(+1.0f, +1.0f, -1.0f, 0.0f);
	_offsets[5] = XMFLOAT4(-1.0f, -1.0f, +1.0f, 0.0f);
	_offsets[6] = XMFLOAT4(-1.0f, +1.0f, -1.0f, 0.0f);
	_offsets[7] = XMFLOAT4(+1.0f, -1.0f, +1.0f, 0.0f);
	_offsets[8] = XMFLOAT4(-1.0f, 0.0f, 0.0f, 0.0f);
	_offsets[9] = XMFLOAT4(+1.0f, 0.0f, 0.0f, 0.0f);
	_offsets[10] = XMFLOAT4(0.0f, -1.0f, 0.0f, 0.0f);
	_offsets[11] = XMFLOAT4(0.0f, +1.0f, 0.0f, 0.0f);
	_offsets[12] = XMFLOAT4(0.0f, 0.0f, -1.0f, 0.0f);
	_offsets[13] = XMFLOAT4(0.0f, 0.0f, +1.0f, 0.0f);

	// 길이 0.25 ~ 1 (고정 — 실행마다 같게): 가까운 표본이 더 많은 쪽으로
	static const float kLength[14] = { 0.31f, 0.92f, 0.55f, 0.27f, 0.78f, 0.44f, 0.66f, 0.35f, 0.98f, 0.5f, 0.6f, 0.3f, 0.85f, 0.4f };
	for (int32 i = 0; i < 14; ++i)
		XMStoreFloat4(&_offsets[i], kLength[i] * XMVector4Normalize(XMLoadFloat4(&_offsets[i])));
}
