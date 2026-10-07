#pragma once
#include <nlohmann/json.hpp>

class VolumeStack;

// 모션 벡터 (화면 속도) — Volume 의 Motion Vectors 가 켜고 끈다 (Unity URP 의 모션 벡터 패스와 같은 두 단계).
//  값 = 지터 뺀 화면 좌표 (uv) 의 이번 − 지난 프레임, R16G16F, Game 뷰 크기.
//  1) 카메라: 깊이 프리패스의 뷰 깊이로 위치를 되짚어 지난 카메라로 (모든 픽셀, 하늘은 회전만)
//  2) 물체: 지난 프레임보다 움직인 Mesh Renderer (Motion Vectors = Per Object Motion), 애니메이션 중인 Skinned Mesh Renderer
//     (Skinned Motion Vectors) 를 지난 월드 행렬 · 지난 본 팔레트로 다시 그린다. Force No Motion = 0
//  쓰는 곳: TAA (히스토리 자리), Motion Blur (Mode = Camera And Objects), SSAO 시간 누적. 꺼지면 모두 예전처럼 카메라만 되돌린다
namespace MotionVectors
{
	struct Settings
	{
		bool Enabled = true;        // 모션 벡터를 만든다 (끄면 TAA · Motion Blur · SSAO 가 카메라만)
		bool ObjectMotion = true;   // 움직인 물체의 속도 (끄면 카메라만)
		bool SkinnedMotion = true;  // Skinned Mesh Renderer 의 본 애니메이션 (끄면 그 물체의 이동만)
		static Settings FromStack(const VolumeStack& stack);
	};

	struct Frame
	{
		XMFLOAT4X4 View = {};
		XMFLOAT4X4 Proj = {};          // 지터 없는 투영
		XMFLOAT4X4 ProjJittered = {};  // 깊이 프리패스가 쓴 투영 (TAA 면 지터, 아니면 Proj 와 같음)
		UINT Width = 0, Height = 0;
		GfxShaderResourceView* NormalDepth = nullptr;   // 깊이 프리패스 (xyz 뷰 노멀, w 뷰 깊이)
	};

	// Game 뷰: 깊이 프리패스 뒤, SSAO 앞. 꺼져 있으면 그리지 않고 Valid() = false
	void Render(const Frame& frame, const Settings& settings);
	bool Valid();
	GfxShaderResourceView* SRV();
	GfxTexture2D* Texture();
	// 진단: 켜짐 · 크기 · 이번 프레임에 다시 그린 물체 수
	nlohmann::json Info();
}
