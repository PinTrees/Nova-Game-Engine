#include "pch.h"
#include "Ssao.h"
#include "Effects.h"
#include "Vertex.h"
#include "MathHelper.h"
#include "VolumeProfile.h"
#include <random>

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
	}
	return s;
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
	return _ambientSRV0;
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

	// AO 는 반 해상도
	_ambientMapViewport.TopLeftX = 0.0f;
	_ambientMapViewport.TopLeftY = 0.0f;
	_ambientMapViewport.Width = (float)(_renderTargetWidth / 2);
	_ambientMapViewport.Height = (float)(_renderTargetHeight / 2);
	_ambientMapViewport.MinDepth = 0.0f;
	_ambientMapViewport.MaxDepth = 1.0f;

	BuildTextureViews();
}

void Ssao::SetNormalDepthRenderTarget(ComPtr<GfxDepthStencilView> dsv)
{
	GfxRenderTargetView* renderTargets[1] = { _normalDepthRTV.Get() };
	_deviceContext->OMSetRenderTargets(1, renderTargets, dsv.Get());

	// 비운 자리 = 뷰 노멀 (0,0,-1) · 아주 먼 깊이 (가리지도 가려지지도 않는다)
	const float clearColor[] = { 0.0f, 0.0f, -1.0f, 1e5f };
	_deviceContext->ClearRenderTargetView(_normalDepthRTV.Get(), clearColor);
}

void Ssao::Render(CXMMATRIX proj, const Settings& settings, int32 blurCount)
{
	_last = settings;
	if (!settings.Active() || !_ambientRTV0)
	{
		// 꺼짐: 흰 맵 (가림 없음) — 셰이더는 그대로 곱한다
		const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		_deviceContext->ClearRenderTargetView(_ambientRTV0.Get(), white);
		return;
	}
	Compute(proj, settings);
	for (int32 i = 0; i < blurCount; ++i)
	{
		BlurAmbientMap(_ambientSRV0, _ambientRTV1, true);
		BlurAmbientMap(_ambientSRV1, _ambientRTV0, false);
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

void Ssao::Compute(CXMMATRIX proj, const Settings& s)
{
	// 깊이 버퍼 없이 AO 타깃에만 (깊이 검사 없음)
	GfxRenderTargetView* renderTargets[1] = { _ambientRTV0.Get() };
	_deviceContext->OMSetRenderTargets(1, renderTargets, 0);
	const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	_deviceContext->ClearRenderTargetView(_ambientRTV0.Get(), white);
	_deviceContext->RSSetViewports(1, &_ambientMapViewport);

	// NDC [-1,+1]^2 → 텍스처 [0,1]^2
	static const XMMATRIX T(
		0.5f, 0.0f, 0.0f, 0.0f,
		0.0f, -0.5f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.5f, 0.5f, 0.0f, 1.0f);
	Effects::SsaoFX->SetViewToTexSpace(XMMatrixMultiply(proj, T));

	// 화면 네 구석의 뷰 방향 (z = 1) — 이 프레임의 투영에서 (시야각 · 화면비가 바뀌어도 맞다). 셰이더는 p = 깊이 / z × 방향
	XMFLOAT4X4 P;
	XMStoreFloat4x4(&P, proj);
	const float hw = P._11 != 0.0f ? 1.0f / P._11 : 1.0f;
	const float hh = P._22 != 0.0f ? 1.0f / P._22 : 1.0f;
	const XMFLOAT4 corners[4] = { { -hw, -hh, 1.0f, 0.0f }, { -hw, +hh, 1.0f, 0.0f }, { +hw, +hh, 1.0f, 0.0f }, { +hw, -hh, 1.0f, 0.0f } };
	Effects::SsaoFX->SetFrustumCorners(corners);
	Effects::SsaoFX->SetOffsetVectors(_offsets);

	// 반지름에 맞춘 가림 곡선 (예전 고정값 0.5 m · 0.2 · 2.0 · 0.05 와 같은 비율)
	Effects::SsaoFX->SetOcclusionRadius(s.Radius);
	Effects::SsaoFX->SetOcclusionFadeStart(s.Radius * 0.4f);
	Effects::SsaoFX->SetOcclusionFadeEnd(s.Radius * 4.0f);
	Effects::SsaoFX->SetSurfaceEpsilon(s.Radius * 0.1f);
	Effects::SsaoFX->SetParams(s.Intensity, s.FalloffDistance);
	Effects::SsaoFX->SetNormalDepthMap(_normalDepthSRV.Get());
	Effects::SsaoFX->SetRandomVecMap(_randomVectorSRV.Get());

	FxTechnique* tech = s.Samples <= 0 ? Effects::SsaoFX->SsaoLowTech.Get() : (s.Samples == 1 ? Effects::SsaoFX->SsaoTech.Get() : Effects::SsaoFX->SsaoHighTech.Get());
	DrawQuad(tech ? tech : Effects::SsaoFX->SsaoTech.Get());
	Effects::SsaoFX->SetNormalDepthMap(nullptr);
}

void Ssao::BlurAmbientMap(ComPtr<GfxShaderResourceView> inputSRV, ComPtr<GfxRenderTargetView> outputRTV, bool horzBlur)
{
	GfxRenderTargetView* renderTargets[1] = { outputRTV.Get() };
	_deviceContext->OMSetRenderTargets(1, renderTargets, 0);
	_deviceContext->RSSetViewports(1, &_ambientMapViewport);

	Effects::SsaoBlurFX->SetTexelWidth(1.0f / _ambientMapViewport.Width);
	Effects::SsaoBlurFX->SetTexelHeight(1.0f / _ambientMapViewport.Height);
	Effects::SsaoBlurFX->SetNormalDepthMap(_normalDepthSRV.Get());
	Effects::SsaoBlurFX->SetInputImage(inputSRV.Get());
	FxTechnique* tech = horzBlur ? Effects::SsaoBlurFX->HorzBlurTech.Get() : Effects::SsaoBlurFX->VertBlurTech.Get();
	DrawQuad(tech);

	// 다음 흐림에서 출력이 되므로 입력을 뗀다
	Effects::SsaoBlurFX->SetInputImage(nullptr);
	Effects::SsaoBlurFX->SetNormalDepthMap(nullptr);
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

void Ssao::BuildTextureViews()
{
	D3D11_TEXTURE2D_DESC texDesc = {};
	texDesc.Width = _renderTargetWidth;
	texDesc.Height = _renderTargetHeight;
	texDesc.MipLevels = 1;
	texDesc.ArraySize = 1;
	texDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	texDesc.SampleDesc.Count = 1;
	texDesc.Usage = D3D11_USAGE_DEFAULT;
	texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

	ComPtr<GfxTexture2D> normalDepthTex;
	HR(_device->CreateTexture2D(&texDesc, 0, normalDepthTex.GetAddressOf()));
	HR(_device->CreateShaderResourceView(normalDepthTex.Get(), 0, _normalDepthSRV.ReleaseAndGetAddressOf()));
	HR(_device->CreateRenderTargetView(normalDepthTex.Get(), 0, _normalDepthRTV.ReleaseAndGetAddressOf()));

	// AO: 반 해상도
	texDesc.Width = _renderTargetWidth / 2;
	texDesc.Height = _renderTargetHeight / 2;
	texDesc.Format = DXGI_FORMAT_R16_FLOAT;

	HR(_device->CreateTexture2D(&texDesc, 0, _ambientTex0.ReleaseAndGetAddressOf()));
	HR(_device->CreateShaderResourceView(_ambientTex0.Get(), 0, _ambientSRV0.ReleaseAndGetAddressOf()));
	HR(_device->CreateRenderTargetView(_ambientTex0.Get(), 0, _ambientRTV0.ReleaseAndGetAddressOf()));

	ComPtr<GfxTexture2D> ambientTex1;
	HR(_device->CreateTexture2D(&texDesc, 0, ambientTex1.GetAddressOf()));
	HR(_device->CreateShaderResourceView(ambientTex1.Get(), 0, _ambientSRV1.ReleaseAndGetAddressOf()));
	HR(_device->CreateRenderTargetView(ambientTex1.Get(), 0, _ambientRTV1.ReleaseAndGetAddressOf()));

	// 처음 = 가림 없음 (계산 전에 그려도 어둡지 않게)
	const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	_deviceContext->ClearRenderTargetView(_ambientRTV0.Get(), white);
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
