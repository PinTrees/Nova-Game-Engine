#include "pch.h"
#include "ShadowMap.h"
#include "Utils.h"

ShadowMap::ShadowMap(ComPtr<ID3D11Device> device, uint32 width, uint32 height)
	: m_Width(width), m_Height(height)
{
	m_Viewport.TopLeftX = 0.0f;
	m_Viewport.TopLeftY = 0.0f;
	m_Viewport.Width = static_cast<float>(width);
	m_Viewport.Height = static_cast<float>(height);
	m_Viewport.MinDepth = 0.0f;
	m_Viewport.MaxDepth = 1.0f;

	for (int i = 0; i < (uint32)LightType::End; i++)
	{
		m_DepthMapDSV[i].resize(LIGHT_MAX_SIZE);
		m_DepthMapSRV[i].resize(LIGHT_MAX_SIZE);
	}

	// Use typeless format because the DSV is going to interpret
	// the bits as DXGI_FORMAT_D24_UNORM_S8_UINT, whereas the SRV is going to interpret
	// the bits as DXGI_FORMAT_R24_UNORM_X8_TYPELESS.
	D3D11_TEXTURE2D_DESC texDesc;
	texDesc.Width = m_Width;
	texDesc.Height = m_Height;
	texDesc.MipLevels = 1;
	texDesc.ArraySize = 1;
	texDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Usage = D3D11_USAGE_DEFAULT;
	texDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
	texDesc.CPUAccessFlags = 0;
	texDesc.MiscFlags = 0;

	ComPtr<ID3D11Texture2D> depthMap;
	HR(device->CreateTexture2D(&texDesc, 0, depthMap.GetAddressOf()));

	D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc;
	dsvDesc.Flags = 0;
	dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
	dsvDesc.Texture2D.MipSlice = 0;

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
	srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = texDesc.MipLevels;
	srvDesc.Texture2D.MostDetailedMip = 0;
	for (int i = 0; i < (uint32)LightType::End; i++)
	{
		for (int j = 0; j < LIGHT_MAX_SIZE; j++)
		{
			HR(device->CreateDepthStencilView(depthMap.Get(), &dsvDesc, m_DepthMapDSV[i][j].GetAddressOf()));
			HR(device->CreateShaderResourceView(depthMap.Get(), &srvDesc, m_DepthMapSRV[i][j].GetAddressOf()));
		}
	}
}

ShadowMap::~ShadowMap()
{
}

ComPtr<ID3D11ShaderResourceView> ShadowMap::DepthMapSRV()
{
	return m_DepthMapSRV[0][0];
}

vector<ID3D11ShaderResourceView*> ShadowMap::DepthMapSRVArray(LightType type)
{
	vector<ID3D11ShaderResourceView*> rawSRVs;
	for (const auto& srv : m_DepthMapSRV[(uint32)type])
	{
		rawSRVs.push_back(srv.Get());
	}
	return rawSRVs;
}

void ShadowMap::BindDsvAndSetNullRenderTarget(ComPtr<ID3D11DeviceContext> dc)
{
	dc->RSSetViewports(1, &m_Viewport);

	// Set null render target because we are only going to draw to depth buffer.
	// Setting a null render target will disable color writes.
	ID3D11RenderTargetView* renderTargets[1] = { 0 };
	dc->OMSetRenderTargets(1, renderTargets, m_DepthMapDSV[0][0].Get());

	dc->ClearDepthStencilView(m_DepthMapDSV[0][0].Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
}

void ShadowMap::BindDsvAndSetNullRenderTarget(ComPtr<ID3D11DeviceContext> dc, LightType type, int index)
{
	dc->RSSetViewports(1, &m_Viewport);

	// Set null render target because we are only going to draw to depth buffer.
	// Setting a null render target will disable color writes.
	ID3D11RenderTargetView* renderTargets[1] = { 0 };
	dc->OMSetRenderTargets(1, renderTargets, m_DepthMapDSV[(uint32)type][index].Get());

	dc->ClearDepthStencilView(m_DepthMapDSV[(uint32)type][index].Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
}

