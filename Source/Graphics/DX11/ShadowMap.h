#pragma once

// 그림자 깊이 맵 모음 (Game 뷰 / Scene 뷰가 하나씩 가진다). 빛 종류마다 Texture2DArray 하나:
//  - 방향광: 조각 = 빛 x 4 + 캐스케이드, 해상도는 Volume > Shadows 의 Resolution
//  - 스포트광: 조각 = 빛, 점광: 조각 = 빛 x 6 + 큐브 면
//  셰이더가 읽는 텍스처가 종류마다 하나 (예전에는 빛마다 하나 = 12 개) — OpenGL · GLES 의 샘플러 수 한도 (32) 에 여유를 둔다
//  Prepare 로 이번 프레임 빛 수만큼 조각을 마련한다 (늘 때만 다시 만든다)
class ShadowMap
{
public:
	static constexpr int kMaxCascades = 4;

	ShadowMap(ComPtr<GfxDevice> device, uint32 width, uint32 height);
	~ShadowMap();

	// 그리기 전에 종류마다: 빛 count 개를 담을 조각 (count = 0 이면 그대로)
	void Prepare(LightType type, int count, uint32 resolution);
	// type 의 lightIndex 번째 빛의 조각 slice 를 깊이 타깃으로 (지우고 뷰포트 설정).
	// 조각 = 방향광 캐스케이드 / 점광 면, 스포트광은 0
	void BindSlice(GfxContext* dc, LightType type, int lightIndex, int slice, uint32 resolution);

	// 셰이더에 넘길 SRV (아직 없으면 nullptr)
	GfxShaderResourceView* DepthMapSRV(LightType type);

	// 맵을 (다시) 만들 때마다 바뀌는 번호 (캐시한 캐스케이드가 사라졌는지 판단). 아직 없으면 0
	uint32 Generation(LightType type, int lightIndex) const;
	// 만든 맵 전체 크기 (바이트, Profiler 메모리)
	size_t MemoryBytes() const;

	static int SlicesPerLight(LightType type) { return type == LightType::Directional ? kMaxCascades : (type == LightType::Point ? 6 : 1); }

private:
	struct Target
	{
		ComPtr<GfxTexture2D> Texture;
		ComPtr<GfxShaderResourceView> Srv;
		vector<ComPtr<GfxDepthStencilView>> Dsv;   // 조각마다
		uint32 Size = 0;
		uint32 Slices = 0;
		uint32 Generation = 0;
		ULONGLONG RetryAt = 0;   // 만들기 실패 → 이 시각까지 다시 시도하지 않음
	};
	bool Ensure(Target& t, uint32 size, uint32 slices);
	void Bind(GfxContext* dc, Target& t, int slice);

	ComPtr<GfxDevice> m_Device;
	uint32 m_DefaultSize;
	Target m_Targets[(uint32)LightType::End];
};
