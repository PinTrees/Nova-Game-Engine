#pragma once

class VolumeStack;

// 화면 공간 앰비언트 오클루전 (Volume 의 Screen Space Ambient Occlusion — Unity URP 의 SSAO 와 같은 값들).
//  깊이 프리패스가 그린 뷰 노멀 · 깊이 → AO (반 또는 전체 해상도, 노멀 쪽 반구의 표본) → 시간 누적 (지난 프레임을 되돌려 섞기)
//  → 가장자리를 지키는 흐림 → 전체 해상도로 (깊이 · 노멀이 비슷한 텍셀만 — 윤곽 후광 없음).
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
		bool TemporalAccumulation = true;      // 프레임마다 표본을 돌려 지난 결과와 섞는다 (HDRP 와 같음 — TAA 와 함께면 윤곽도 부드럽다)
		bool FullResolution = false;           // AO 를 전체 해상도로 (기본 = 반 — URP 의 Downsample)

		bool Active() const { return Enabled && Intensity > 0.0f; }
		static int SampleCount(int samples) { return samples <= 0 ? 4 : (samples == 1 ? 8 : 14); }
		// 히스토리를 이어 써도 되는 같은 값인가 (바뀌면 버린다 — 옛 값의 AO 가 남아 흐르지 않게)
		bool SameHistory(const Settings& o) const;
		// Volume 을 섞은 값 (Screen Space Ambient Occlusion — 없으면 기본값)
		static Settings FromStack(const VolumeStack& stack);
	};

	Ssao();
	~Ssao();

	ComPtr<GfxShaderResourceView> NormalDepthSRV();
	// 전체 해상도 AO (32 가 읽는다)
	ComPtr<GfxShaderResourceView> AmbientSRV();
	GfxTexture2D* AmbientTexture() { return _aoFullTex.Get(); }

	void Init(int32 width, int32 height);
	// 뷰 크기가 바뀔 때 (타깃을 다시 만든다)
	void OnSize(int32 width, int32 height);

	// 깊이 프리패스의 타깃: 뷰 노멀 · 깊이 (뷰 공간 z) — 본 패스가 같은 깊이 버퍼를 쓴다
	void SetNormalDepthRenderTarget(ComPtr<GfxDepthStencilView> dsv);

	// 이 프레임의 뷰 · 투영 (TAA 면 지터한 것 — 깊이 프리패스와 같은 것) 으로 AO
	void Render(CXMMATRIX view, CXMMATRIX proj, const Settings& settings);
	const Settings& LastSettings() const { return _last; }
	uint32 MapWidth() const { return _renderTargetWidth; }
	uint32 MapHeight() const { return _renderTargetHeight; }
	uint32 AoWidth() const { return _aoW; }
	uint32 AoHeight() const { return _aoH; }
	bool HistoryValid() const { return _historyValid; }
	uint32 AccumulatedFrames() const { return _accumulated; }

	ComPtr<GfxShaderResourceView> GetRandomVectorSRV() { return _randomVectorSRV; }

private:
	struct Target
	{
		ComPtr<GfxTexture2D> Tex;
		ComPtr<GfxRenderTargetView> RTV;
		ComPtr<GfxShaderResourceView> SRV;
	};
	bool MakeTarget(Target& t, uint32 w, uint32 h, DXGI_FORMAT format);
	void BuildAoTargets();
	void Compute(CXMMATRIX proj, const Settings& s);
	void Temporal(CXMMATRIX view);
	void Blur(GfxShaderResourceView* input, GfxRenderTargetView* output, bool horzBlur);
	void Upsample(GfxShaderResourceView* input);
	void SetCorners(CXMMATRIX proj);
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

	// 전체 해상도 결과
	ComPtr<GfxTexture2D> _aoFullTex;
	ComPtr<GfxRenderTargetView> _aoFullRTV;
	ComPtr<GfxShaderResourceView> _aoFullSRV;

	// 계산 크기: 원시 AO · 흐림 사이 · 흐린 AO · 히스토리 둘 (r = AO, g = 뷰 깊이)
	Target _aoRaw, _aoTemp, _aoBlur, _history[2];
	uint32 _aoScale = 2;   // 전체 해상도 픽셀 / 계산 텍셀
	uint32 _aoW = 0, _aoH = 0;

	uint32 _renderTargetWidth = 0;
	uint32 _renderTargetHeight = 0;

	XMFLOAT4 _offsets[14];
	D3D11_VIEWPORT _aoViewport = {};
	D3D11_VIEWPORT _fullViewport = {};
	Settings _last;

	// 시간 누적
	XMFLOAT4X4 _prevView = {}, _prevProj = {};
	bool _historyValid = false;
	int _historyIndex = 0;     // 이번에 쓸 히스토리
	uint32 _frame = 0;
	uint32 _accumulated = 0;   // 히스토리를 이어 쓴 프레임 수 (진단)
	Settings _historySettings;
};
