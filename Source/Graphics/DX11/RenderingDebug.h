#pragma once
#include <nlohmann/json.hpp>

// Rendering Debugger (Unity URP 의 Window > Analysis > Rendering Debugger 의 Fullscreen Debug Mode):
//  Scene · Game 뷰를 깊이 · 월드 노멀 · SSAO 맵 · 모션 벡터 · Adaptive Probe Volume (프로브 빛만 · 섞은 방법) 로 바꿔 본다.
//  편집기 전용 (게임 빌드에는 없음). 고르는 곳: Window > Analysis > Rendering Debugger, Scene 뷰 툴바의 Debug 드롭다운, CLI nova debugview
namespace RenderingDebug
{
	enum Mode { None = 0, Depth, Normals, AmbientOcclusion, MotionVectors, ProbeVolumeLighting, ProbeVolumeSampling, ModeCount };

	struct State
	{
		int Mode = None;
		float DepthRange = 50.0f;     // Depth: 이 거리 (m) 에서 흰색
		float MotionScale = 1.0f;     // Motion Vectors: 밝기 배율 (16 픽셀 / Scale 에서 가장 밝다)
	};

	NOVA_API const char* const* ModeNames();   // 화면 이름 (Unity 와 같은 말)
	NOVA_API const char* ModeKey(int mode);     // CLI 이름: none depth normals ao motion apv apv-sampling
	NOVA_API State& Get();
	NOVA_API void SetMode(int mode);            // APV 보기는 ProbeVolumes 의 진단 보기를 켠다 (32. InstancedBasic 이 그린다)
	bool NeedsMotionVectors();                  // Scene 뷰도 모션 벡터를 그려야 하는지

	struct Inputs
	{
		GfxShaderResourceView* NormalDepth = nullptr;   // 깊이 프리패스 (xyz 뷰 노멀, w 뷰 깊이)
		GfxShaderResourceView* Ao = nullptr;            // SSAO 맵
		GfxShaderResourceView* Motion = nullptr;        // 모션 벡터 (없으면 검정)
		XMFLOAT4X4 View = {};
		UINT Width = 0, Height = 0;
		bool SceneView = false;
	};
	// 뷰의 마지막 (후처리 뒤, UI 앞): 전체 화면 보기면 target 을 덮어 그리고 true
	bool Draw(const Inputs& in, GfxRenderTargetView* target, const D3D11_VIEWPORT& viewport);

	nlohmann::json Info();
	void RegisterEditor();   // CLI: nova debugview <mode> [--range m] [--scale s] | info
}
