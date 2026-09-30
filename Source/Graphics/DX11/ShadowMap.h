#pragma once

class ShadowMap
{
public:
	ShadowMap(ComPtr<ID3D11Device> device, uint32 width, uint32 height);
	~ShadowMap();

	ComPtr<ID3D11ShaderResourceView> DepthMapSRV();
	vector<ID3D11ShaderResourceView*> DepthMapSRVArray(LightType type);

	void BindDsvAndSetNullRenderTarget(ComPtr<ID3D11DeviceContext> dc);
	void BindDsvAndSetNullRenderTarget(ComPtr<ID3D11DeviceContext> dc, LightType type, int index);

private:
	uint32 m_Width;
	uint32 m_Height;

	vector<ComPtr<ID3D11ShaderResourceView>> m_DepthMapSRV[(uint32)LightType::End];
	vector<ComPtr<ID3D11DepthStencilView>> m_DepthMapDSV[(uint32)LightType::End];

	D3D11_VIEWPORT m_Viewport;
};
