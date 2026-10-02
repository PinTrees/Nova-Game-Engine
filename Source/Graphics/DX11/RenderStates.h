#pragma once

#include "Utils.h"

class RenderStates
{
public:
	static void InitAll(ComPtr<GfxDevice> device);
	static void DestroyAll();

	static ComPtr<GfxRasterizerState> WireframeRS;
	static ComPtr<GfxRasterizerState> NoCullRS;
	static ComPtr<GfxRasterizerState> CullClockwiseRS;

	static ComPtr<GfxBlendState> AlphaToCoverageBS;
	static ComPtr<GfxBlendState> TransparentBS;
	static ComPtr<GfxBlendState> NoRenderTargetWritesBS;

	static ComPtr<GfxDepthStencilState> MarkMirrorDSS;
	static ComPtr<GfxDepthStencilState> DrawReflectionDSS;
	static ComPtr<GfxDepthStencilState> NoDoubleBlendDSS;
	static ComPtr<GfxDepthStencilState> EqualsDSS;

	static ComPtr<GfxDepthStencilState> LessEqualDSS;
	static ComPtr<GfxDepthStencilState> DepthReadDSS;   // depth test LESS_EQUAL, no depth write (transparent meshes)
};