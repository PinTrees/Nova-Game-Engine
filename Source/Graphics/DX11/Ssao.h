#pragma once

class VolumeStack;

// 화면 공간 앰비언트 오클루전 (Volume 의 Screen Space Ambient Occlusion — Unity URP 의 SSAO 와 같은 값들).
//  깊이 프리패스가 그린 뷰 노멀 · 깊이 → 반 해상도 AO (노멀 쪽 반구의 표본) → 가장자리를 지키는 흐림.
//  32. InstancedBasic 의 ShadeLit 이 환경광에 곱하고, Direct Lighting Strength 만큼 직접광에도 곱한다.
//  꺼져 있으면 AO 맵을 흰색으로 (가림 없음) 지우고 계산하지 않는다
class Ssao
{
public:
	struct Settings
	{
		bool Enabled = true;
		float Intensity = 1.0f;                // 가려짐 세기 (0 = 없음)
		float Radius = 0.5f;                   // 표본 반지름 (m, 뷰 공간)
		float DirectLightingStrength = 0.25f;  // 직접광에도 곱하는 몫 (0 = 환경광만, 1 = 모두)
		int Samples = 1;                       // 0 Low (4) · 1 Medium (8) · 2 High (14)
		float FalloffDistance = 100.0f;        // 카메라에서 이 거리까지 (끝 20 % 에서 사라진다)

		bool Active() const { return Enabled && Intensity > 0.0f; }
		static int SampleCount(int samples) { return samples <= 0 ? 4 : (samples == 1 ? 8 : 14); }
		// Volume 을 섞은 값 (Screen Space Ambient Occlusion — 없으면 기본값)
		static Settings FromStack(const VolumeStack& stack);
	};

	Ssao();
	~Ssao();

	ComPtr<GfxShaderResourceView> NormalDepthSRV();
	ComPtr<GfxShaderResourceView> AmbientSRV();
	GfxTexture2D* AmbientTexture() { return _ambientTex0.Get(); }

	void Init(int32 width, int32 height);
	// 뷰 크기가 바뀔 때 (타깃을 다시 만든다)
	void OnSize(int32 width, int32 height);

	// 깊이 프리패스의 타깃: 뷰 노멀 · 깊이 (뷰 공간 z) — 본 패스가 같은 깊이 버퍼를 쓴다
	void SetNormalDepthRenderTarget(ComPtr<GfxDepthStencilView> dsv);

	// AO 계산 + 흐림 (proj = 이 프레임 카메라의 투영 — 시야각 · 화면비가 바뀌어도 위치를 바르게 되짚는다)
	void Render(CXMMATRIX proj, const Settings& settings, int32 blurCount = 4);
	const Settings& LastSettings() const { return _last; }
	uint32 MapWidth() const { return _renderTargetWidth / 2; }
	uint32 MapHeight() const { return _renderTargetHeight / 2; }

	ComPtr<GfxShaderResourceView> GetRandomVectorSRV() { return _randomVectorSRV; }

private:
	void Compute(CXMMATRIX proj, const Settings& s);
	void BlurAmbientMap(ComPtr<GfxShaderResourceView> inputSRV, ComPtr<GfxRenderTargetView> outputRTV, bool horzBlur);
	void DrawQuad(FxTechnique* tech);
	void BuildFullScreenQuad();
	void BuildTextureViews();
	void BuildRandomVectorTexture();
	void BuildOffsetVectors();

	ComPtr<GfxDevice> _device;
	ComPtr<GfxContext> _deviceContext;

	ComPtr<GfxBuffer> _screenQuadVB;
	ComPtr<GfxBuffer> _screenQuadIB;

	ComPtr<GfxShaderResourceView> _randomVectorSRV;
	ComPtr<GfxRenderTargetView> _normalDepthRTV;
	ComPtr<GfxShaderResourceView> _normalDepthSRV;

	// 흐림의 주고받기 (가로 → 세로)
	ComPtr<GfxTexture2D> _ambientTex0;
	ComPtr<GfxRenderTargetView> _ambientRTV0;
	ComPtr<GfxShaderResourceView> _ambientSRV0;
	ComPtr<GfxRenderTargetView> _ambientRTV1;
	ComPtr<GfxShaderResourceView> _ambientSRV1;

	uint32 _renderTargetWidth = 0;
	uint32 _renderTargetHeight = 0;

	XMFLOAT4 _offsets[14];
	D3D11_VIEWPORT _ambientMapViewport = {};
	Settings _last;
};
