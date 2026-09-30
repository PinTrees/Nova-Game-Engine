#pragma once

#include "Utils.h"

class RenderStates
{
public:
	static void InitAll(ComPtr<ID3D11Device> device);
	static void DestroyAll();

	static ComPtr<ID3D11RasterizerState> WireframeRS;
	static ComPtr<ID3D11RasterizerState> NoCullRS;
	static ComPtr<ID3D11RasterizerState> CullClockwiseRS;

	static ComPtr<ID3D11BlendState> AlphaToCoverageBS;
	static ComPtr<ID3D11BlendState> TransparentBS;
	static ComPtr<ID3D11BlendState> NoRenderTargetWritesBS;

	static ComPtr<ID3D11DepthStencilState> MarkMirrorDSS;
	static ComPtr<ID3D11DepthStencilState> DrawReflectionDSS;
	static ComPtr<ID3D11DepthStencilState> NoDoubleBlendDSS;
	static ComPtr<ID3D11DepthStencilState> EqualsDSS;

	static ComPtr<ID3D11DepthStencilState> LessEqualDSS;
};