#pragma once
#include <chrono>
#include "VolumeProfile.h"
#include "RetryGate.h"

// URP 식 후처리 실행기 (뷰마다 하나: Scene 뷰 / Game 뷰).
//  1) Begin(): 씬을 그릴 HDR(R16G16B16A16F) 타깃을 뷰 크기로 준비해 돌려준다
//  2) Execute(): (Depth Of Field → Motion Blur) → Bloom 밉 체인 → Uber(색 보정/톤매핑/비네트/...) → (FXAA) → 출력 타깃
// 설정은 VolumeStack(섞인 Volume 값) + 카메라 옵션.
class PostProcessPass
{
public:
	struct CameraOptions
	{
		bool PostProcessing = true;   // false 면 Volume 효과 없이 (FXAA/디더링만)
		bool Fxaa = false;
		bool Dithering = false;
		bool StopNaNs = false;
		// Depth Of Field · Motion Blur: 깊이 프리패스 (뷰 노멀 + 뷰 깊이 w) 와 이 화면의 카메라
		GfxShaderResourceView* Depth = nullptr;
		XMFLOAT4X4 View = {}, Proj = {};
		bool SceneView = false;   // Scene 뷰: Motion Blur 없음 (Unity 와 같음)
	};

	PostProcessPass();
	~PostProcessPass();

	// 뷰 크기의 HDR 타깃을 만들거나 재사용한다. 씬은 여기에 그린다.
	GfxRenderTargetView* Begin(UINT width, UINT height);
	void Execute(const VolumeStack& stack, const CameraOptions& options, GfxRenderTargetView* output);

	// 이 설정으로 실행할 필요가 있는지 (효과가 하나도 없고 FXAA/디더링도 없으면 false → 기존처럼 바로 그림)
	static bool IsNeeded(const VolumeStack& stack, const CameraOptions& options);

private:
	struct Target
	{
		ComPtr<GfxTexture2D> Tex;
		ComPtr<GfxRenderTargetView> RTV;
		ComPtr<GfxShaderResourceView> SRV;
		UINT W = 0, H = 0;
	};
	bool CreateTarget(Target& t, UINT w, UINT h, DXGI_FORMAT format);
	void Draw(const char* tech, GfxRenderTargetView* rtv, UINT w, UINT h);
	void SetSRV(const char* name, GfxShaderResourceView* srv);
	void SetVec(const char* name, float x, float y, float z, float w);
	bool InitEffect();

	std::unique_ptr<class Effect> m_Effect;
	bool m_EffectFailed = false;
	Target m_Scene;          // HDR 씬
	Target m_Ldr;            // FXAA 입력 (Uber 출력)
	std::vector<Target> m_Down, m_Up;   // Bloom 밉 체인
	Target m_Dof, m_Motion;              // Depth Of Field · Motion Blur 결과 (전체 해상도 HDR)
	Target m_Half[2];                    // Depth Of Field 반 해상도 (선형 색 + CoC)
	// Motion Blur: 지난 프레임 카메라 — 프레임마다 한 번만 넘긴다 (스크린샷처럼 한 프레임에 다시 그려도 속도가 0 이 되지 않게)
	XMFLOAT4X4 m_PrevViewProj = {}, m_CurViewProj = {};
	uint32_t m_CurFrame = 0;
	bool m_PrevValid = false, m_CurValid = false;
	GfxShaderResourceView* DepthOfField(const VolumeComponent& dof, const CameraOptions& options, GfxShaderResourceView* src);
	GfxShaderResourceView* MotionBlur(const VolumeComponent& mb, const CameraOptions& options, GfxShaderResourceView* src);
	UINT m_Width = 0, m_Height = 0;
	RetryGate m_Retry;   // 타깃 만들기 실패 → 1 초 뒤 다시 (매 프레임 다시 만들지 않게)
	float m_Time = 0.0f;
	// 자동 노출: 로그 휘도(256², 밉) + 배율 1x1 두 장(지난 / 이번)
	Target m_Lum;
	Target m_Exposure[2];
	int m_ExposureIndex = 0;
	bool m_ExposureValid = false;
	std::chrono::steady_clock::time_point m_LastExecute;
	void UpdateAutoExposure(const VolumeComponent& exposure, float dt);
};
