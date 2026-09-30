#pragma once

class BlurFilter
{
public:
	BlurFilter();
	~BlurFilter();

	ComPtr<ID3D11ShaderResourceView> GetBlurredOutput();

	// Generate Gaussian blur weights.
	void SetGaussianWeights(float sigma);

	// Manually specify blur weights.
	void SetWeights(const float weights[9]);

	///<summary>
	/// The width and height should match the dimensions of the input texture to blur.
	/// It is OK to call Init() again to reinitialize the blur filter with a different 
	/// dimension or format.
	///</summary>
	void Init(ComPtr<ID3D11Device> device, uint32 width, uint32 height, DXGI_FORMAT format);

	///<summary>
	/// Blurs the input texture blurCount times.  Note that this modifies the input texture, not a copy of it.
	///</summary>
	void BlurInPlace(ComPtr<ID3D11DeviceContext> dc, ComPtr<ID3D11ShaderResourceView> inputSRV, ComPtr<ID3D11UnorderedAccessView> inputUAV, int32 blurCount);

private:

	uint32 _width = 0;
	uint32 _height = 0;
	DXGI_FORMAT _format = {};

	ComPtr<ID3D11ShaderResourceView> _blurredOutputTexSRV;
	ComPtr<ID3D11UnorderedAccessView> _blurredOutputTexUAV;
};
