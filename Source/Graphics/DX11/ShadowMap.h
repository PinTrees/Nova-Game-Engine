#pragma once

// 그림자 깊이 맵 모음 (Game 뷰 / Scene 뷰가 하나씩 가진다).
//  - 방향광: 빛마다 Texture2DArray (조각 = 캐스케이드 4개), 해상도는 Volume > Shadows 의 Resolution
//  - 스포트광: 빛마다 Texture2DArray(조각 1), 점광: 빛마다 Texture2DArray(조각 6 = 큐브 면)
//  모두 처음 쓰일 때 만든다 (예전에는 모든 빛이 깊이 텍스처 하나를 같이 써서 서로 덮어썼다).
class ShadowMap
{
public:
	static constexpr int kMaxCascades = 4;

	ShadowMap(ComPtr<GfxDevice> device, uint32 width, uint32 height);
	~ShadowMap();

	// type 의 lightIndex 번째 맵에서 조각 slice 를 깊이 타깃으로 (지우고 뷰포트 설정).
	// 조각 = 방향광 캐스케이드 / 점광 면, 스포트광은 0
	void BindSlice(GfxContext* dc, LightType type, int lightIndex, int slice, uint32 resolution);

	// 셰이더에 넘길 SRV 목록 (아직 없는 칸은 nullptr)
	vector<GfxShaderResourceView*> DepthMapSRVArray(LightType type);

	// 맵을 (다시) 만들 때마다 바뀌는 번호 (캐시한 캐스케이드가 사라졌는지 판단). 아직 없으면 0
	uint32 Generation(LightType type, int lightIndex) const;
	// 만든 맵 전체 크기 (바이트, Profiler 메모리)
	size_t MemoryBytes() const;

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
	vector<Target> m_Targets[(uint32)LightType::End];
};
