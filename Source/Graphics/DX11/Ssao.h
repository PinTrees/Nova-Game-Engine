#pragma once

// Should we render AO at half resolution?

class Camera;
class EditorCamera;

class Ssao
{
public:
	Ssao();
	Ssao(ComPtr<ID3D11Device> device, ComPtr<ID3D11DeviceContext> dc, int32 width, int32 height, float fovy, float farZ);
	~Ssao();

	ComPtr<ID3D11ShaderResourceView> NormalDepthSRV();
	ComPtr<ID3D11ShaderResourceView> AmbientSRV();

	void Init(int32 width, int32 height, float fovy, float farZ);

	///<summary>
	/// Call when the backbuffer is resized.  
	///</summary>
	void OnSize(int32 width, int32 height, float fovy, float farZ);

	///<summary>
	/// Changes the render target to the NormalDepth render target.  Pass the 
	/// main depth buffer as the depth buffer to use when we render to the
	/// NormalDepth map.  This pass lays down the scene depth so that there in
	/// no overdraw in the subsequent rendering pass.
	///</summary>
	void SetNormalDepthRenderTarget(ComPtr<ID3D11DepthStencilView> dsv);

	///<summary>
	/// Changes the render target to the Ambient render target and draws a fullscreen
	/// quad to kick off the pixel shader to compute the AmbientMap.  We still keep the
	/// main depth buffer binded to the pipeline, but depth buffer read/writes
	/// are disabled, as we do not need the depth buffer computing the Ambient map.
	///</summary>
	void ComputeSsao(const Camera& camera);
	void ComputeSsao(const EditorCamera& camera);

	///<summary>
	/// Blurs the ambient map to smooth out the noise caused by only taking a
	/// few random samples per pixel.  We use an edge preserving blur so that 
	/// we do not blur across discontinuities--we want edges to remain edges.
	///</summary>
	void BlurAmbientMap(int32 blurCount);

	ComPtr<ID3D11ShaderResourceView> GetRandomVectorSRV() { return _randomVectorSRV; }
private:
	void BlurAmbientMap(ComPtr<ID3D11ShaderResourceView> inputSRV, ComPtr<ID3D11RenderTargetView> outputRTV, bool horzBlur);

	void BuildFrustumFarCorners(float fovy, float farZ);

	void BuildFullScreenQuad();

	void BuildTextureViews();

	void BuildRandomVectorTexture();

	void BuildOffsetVectors();

private:
	ComPtr<ID3D11Device> _device;
	ComPtr<ID3D11DeviceContext> _deviceContext;

	ComPtr<ID3D11Buffer> _screenQuadVB;
	ComPtr<ID3D11Buffer> _screenQuadIB;

	ComPtr<ID3D11ShaderResourceView> _randomVectorSRV;
	ComPtr<ID3D11RenderTargetView> _normalDepthRTV;
	ComPtr<ID3D11ShaderResourceView> _normalDepthSRV;

	// Need two for ping-ponging during blur.
	ComPtr<ID3D11RenderTargetView> _ambientRTV0;
	ComPtr<ID3D11ShaderResourceView> _ambientSRV0;
	ComPtr<ID3D11RenderTargetView> _ambientRTV1;
	ComPtr<ID3D11ShaderResourceView> _ambientSRV1;

	uint32 _renderTargetWidth;
	uint32 _renderTargetHeight;

	XMFLOAT4 _frustumFarCorner[4];
	XMFLOAT4 _offsets[14];

	D3D11_VIEWPORT _ambientMapViewport;
};