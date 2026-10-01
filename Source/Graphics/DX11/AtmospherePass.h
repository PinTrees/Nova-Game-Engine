#pragma once

class VolumeStack;
struct DirectionalLight;

// 대기·안개 (Volume 의 Fog / Atmosphere 오버라이드).
//  - FromStack: 카메라 위치에서 섞은 Volume 값 + 해(첫 방향광)로 셰이더 값을 만든다
//  - Draw: 불투명 물체·하늘 다음에 화면 전체 패스 (장면 색을 복사해 읽고, 깊이로 월드 위치를 되살려 선형 공간에서 입힌다)
//  - Bind: 다른 효과(물)가 같은 cbAtmosphere 를 쓰도록 값을 넣는다 (49. AtmosphereCommon.fx)
namespace AtmospherePass
{
	struct Params
	{
		bool Fog = false, Aerial = false;
		XMFLOAT4 FogA = {}, FogB = {}, FogColor = {}, Rayleigh = {}, Mie = {}, SunDir = { 0, 1, 0, 0 }, SunColor = {};
		bool Active() const { return Fog || Aerial; }
	};

	Params FromStack(const VolumeStack& stack, const DirectionalLight* sun);
	void Bind(FxEffect* fx, const Params& p, const XMFLOAT3& eye, GfxShaderResourceView* sky);
	// target 에 그린다. depthSRV = 장면 깊이 (DSV 는 잠시 풀고 끝나면 dsv 로 되돌린다)
	void Draw(GfxContext* dc, const Params& p, GfxRenderTargetView* target, GfxDepthStencilView* dsv, GfxShaderResourceView* depthSRV,
		const D3D11_VIEWPORT& viewport, CXMMATRIX viewProj, const XMFLOAT3& eye, GfxShaderResourceView* sky);
}
