#pragma once
#include <chrono>
#include "VolumeProfile.h"

// URP 식 후처리 실행기 (뷰마다 하나: Scene 뷰 / Game 뷰).
//  1) Begin(): 씬을 그릴 HDR(R16G16B16A16F) 타깃을 뷰 크기로 준비해 돌려준다
//  2) Execute(): Bloom 밉 체인 → Uber(색 보정/톤매핑/비네트/...) → (FXAA) → 출력 타깃
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
	};

	PostProcessPass();
	~PostProcessPass();

	// 뷰 크기의 HDR 타깃을 만들거나 재사용한다. 씬은 여기에 그린다.
	ID3D11RenderTargetView* Begin(UINT width, UINT height);
	void Execute(const VolumeStack& stack, const CameraOptions& options, ID3D11RenderTargetView* output);

	// 이 설정으로 실행할 필요가 있는지 (효과가 하나도 없고 FXAA/디더링도 없으면 false → 기존처럼 바로 그림)
	static bool IsNeeded(const VolumeStack& stack, const CameraOptions& options);

private:
	struct Target
	{
		ComPtr<ID3D11Texture2D> Tex;
		ComPtr<ID3D11RenderTargetView> RTV;
		ComPtr<ID3D11ShaderResourceView> SRV;
		UINT W = 0, H = 0;
	};
	bool CreateTarget(Target& t, UINT w, UINT h, DXGI_FORMAT format);
	void Draw(const char* tech, ID3D11RenderTargetView* rtv, UINT w, UINT h);
	void SetSRV(const char* name, ID3D11ShaderResourceView* srv);
	void SetVec(const char* name, float x, float y, float z, float w);
	bool InitEffect();

	std::unique_ptr<class Effect> m_Effect;
	bool m_EffectFailed = false;
	Target m_Scene;          // HDR 씬
	Target m_Ldr;            // FXAA 입력 (Uber 출력)
	std::vector<Target> m_Down, m_Up;   // Bloom 밉 체인
	UINT m_Width = 0, m_Height = 0;
	float m_Time = 0.0f;
	// 자동 노출: 로그 휘도(256², 밉) + 배율 1x1 두 장(지난 / 이번)
	Target m_Lum;
	Target m_Exposure[2];
	int m_ExposureIndex = 0;
	bool m_ExposureValid = false;
	std::chrono::steady_clock::time_point m_LastExecute;
	void UpdateAutoExposure(const VolumeComponent& exposure, float dt);
};
