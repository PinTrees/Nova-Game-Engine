#pragma once

// Should we render AO at half resolution?

class Camera;
class EditorCamera;

class Ssao
{
public:
	Ssao();
	Ssao(ComPtr<GfxDevice> device, ComPtr<GfxContext> dc, int32 width, int32 height, float fovy, float farZ);
	~Ssao();

	ComPtr<GfxShaderResourceView> NormalDepthSRV();
	ComPtr<GfxShaderResourceView> AmbientSRV();

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
	void SetNormalDepthRenderTarget(ComPtr<GfxDepthStencilView> dsv);

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

	ComPtr<GfxShaderResourceView> GetRandomVectorSRV() { return _randomVectorSRV; }
private:
	void BlurAmbientMap(ComPtr<GfxShaderResourceView> inputSRV, ComPtr<GfxRenderTargetView> outputRTV, bool horzBlur);

	void BuildFrustumFarCorners(float fovy, float farZ);

	void BuildFullScreenQuad();

	void BuildTextureViews();

	void BuildRandomVectorTexture();

	void BuildOffsetVectors();

private:
	ComPtr<GfxDevice> _device;
	ComPtr<GfxContext> _deviceContext;

	ComPtr<GfxBuffer> _screenQuadVB;
	ComPtr<GfxBuffer> _screenQuadIB;

	ComPtr<GfxShaderResourceView> _randomVectorSRV;
	ComPtr<GfxRenderTargetView> _normalDepthRTV;
	ComPtr<GfxShaderResourceView> _normalDepthSRV;

	// Need two for ping-ponging during blur.
	ComPtr<GfxRenderTargetView> _ambientRTV0;
	ComPtr<GfxShaderResourceView> _ambientSRV0;
	ComPtr<GfxRenderTargetView> _ambientRTV1;
	ComPtr<GfxShaderResourceView> _ambientSRV1;

	uint32 _renderTargetWidth;
	uint32 _renderTargetHeight;

	XMFLOAT4 _frustumFarCorner[4];
	XMFLOAT4 _offsets[14];

	D3D11_VIEWPORT _ambientMapViewport;
};