#include "pch.h"
#include "ShadowMap.h"
#include "Utils.h"

ShadowMap::ShadowMap(ComPtr<ID3D11Device> device, uint32 width, uint32 height)
	: m_Device(device), m_DefaultSize((std::max)(width, height))
{
	m_Targets[(uint32)LightType::Directional].resize(LIGHT_SIZE);
	m_Targets[(uint32)LightType::Spot].resize(LIGHT_SIZE);
	m_Targets[(uint32)LightType::Point].resize(LIGHT_SIZE);
}

ShadowMap::~ShadowMap()
{
}

bool ShadowMap::Ensure(Target& t, uint32 size, uint32 slices)
{
	if (size == 0)
		size = m_DefaultSize;
	if (t.Texture && t.Size == size && t.Slices == slices)
		return true;

	t = Target();
	// DSV 는 D24_UNORM_S8_UINT, SRV 는 R24_UNORM_X8_TYPELESS 로 같은 비트를 읽는다
	D3D11_TEXTURE2D_DESC td = {};
	td.Width = td.Height = size;
	td.MipLevels = 1;
	td.ArraySize = slices;
	td.Format = DXGI_FORMAT_R24G8_TYPELESS;
	td.SampleDesc.Count = 1;
	td.Usage = D3D11_USAGE_DEFAULT;
	td.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
	if (FAILED(m_Device->CreateTexture2D(&td, nullptr, t.Texture.GetAddressOf())))
	{
		EditorLog::Write("Shadow", "shadow map create failed (%u x %u x %u)", size, size, slices);
		t = Target();
		return false;
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
	sd.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
	sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;   // 셰이더는 모두 Texture2DArray 로 읽는다
	sd.Texture2DArray.MipLevels = 1;
	sd.Texture2DArray.ArraySize = slices;
	HR(m_Device->CreateShaderResourceView(t.Texture.Get(), &sd, t.Srv.GetAddressOf()));

	t.Dsv.resize(slices);
	for (uint32 i = 0; i < slices; ++i)
	{
		D3D11_DEPTH_STENCIL_VIEW_DESC dd = {};
		dd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		dd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
		dd.Texture2DArray.FirstArraySlice = i;
		dd.Texture2DArray.ArraySize = 1;
		HR(m_Device->CreateDepthStencilView(t.Texture.Get(), &dd, t.Dsv[i].GetAddressOf()));
	}
	t.Size = size;
	t.Slices = slices;
	return true;
}

void ShadowMap::Bind(ID3D11DeviceContext* dc, Target& t, int slice)
{
	D3D11_VIEWPORT vp = { 0.0f, 0.0f, (float)t.Size, (float)t.Size, 0.0f, 1.0f };
	dc->RSSetViewports(1, &vp);
	// 깊이만 그린다 (색 타깃 없음)
	ID3D11RenderTargetView* renderTargets[1] = { nullptr };
	dc->OMSetRenderTargets(1, renderTargets, t.Dsv[slice].Get());
	dc->ClearDepthStencilView(t.Dsv[slice].Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
}

void ShadowMap::BindSlice(ID3D11DeviceContext* dc, LightType type, int lightIndex, int slice, uint32 resolution)
{
	const uint32 slices = type == LightType::Directional ? kMaxCascades : (type == LightType::Point ? 6 : 1);
	Target& t = m_Targets[(uint32)type][lightIndex];
	if (!Ensure(t, resolution, slices))
		return;
	Bind(dc, t, slice);
}

vector<ID3D11ShaderResourceView*> ShadowMap::DepthMapSRVArray(LightType type)
{
	vector<ID3D11ShaderResourceView*> rawSRVs;
	for (const Target& t : m_Targets[(uint32)type])
		rawSRVs.push_back(t.Srv.Get());
	return rawSRVs;
}
