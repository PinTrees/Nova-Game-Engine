#pragma once

class InstancedBasicEffect;

// Adaptive Probe Volume 의 실시간 계산 (굽기 없음). 컴포넌트는 AdaptiveProbeVolume (Scene)
//  - 단계 (최대 3): 카메라 둘레 32 x 16 x 32 프로브, 간격 Probe Spacing x 4^단계 (1 · 4 · 16 m → 32 · 128 · 512 m)
//  - 복셀 (단계마다 64 x 32 x 64, 앞 · 뒤 버퍼): 얇은 판을 6 방향 직교로 찍어 (알베도 · 노멀 · 깊이) 채운다 — 프레임마다 몇 판씩
//  - 매 프레임: 한 단계를 다시 비추고 (ShadeLit — 직접광 · 그림자 · 프로브 간접광), 한 단계의 프로브가 광선을 쏴 L1 SH 를 섞는다
//  - 셰이더 값: gGISH0..2 · gGIValid (프로브 아틀라스), gGIParams · gGIBias · gGICascade (32. InstancedBasic.fx)
namespace ProbeVolumes
{
	constexpr int kCascades = 3;

	// 장면 판 하나 찍기 (EditorApp 의 Game 뷰 그리기 — 빛 없이 알베도). 돌려주는 값 = 그 그리기의 노멀 · 깊이
	struct CaptureView
	{
		XMMATRIX View, Proj;
		GfxRenderTargetView* Target = nullptr;
		D3D11_VIEWPORT Viewport = {};
		XMFLOAT3 Position = {};
	};
	NOVA_API void SetCapture(std::function<GfxShaderResourceView*(const CaptureView&)> fn);

	// 뷰가 그릴 때 카메라 위치를 알린다 (단계가 이 둘레로 — 편집 중엔 Scene 뷰, Play · 게임은 Game 뷰)
	NOVA_API void SetFocus(const XMFLOAT3& eye, bool editorView);
	// 프레임마다 한 번 (뷰 전, Reflection Probe 찍기 전): 판 찍기 · 다시 비추기 · 프로브 갱신
	NOVA_API void Update();
	// 셰이더 값 (프로브 아틀라스 · 단계). 볼륨이 없으면 단계 수 0 (하늘)
	NOVA_API void Bind(InstancedBasicEffect* fx);
	NOVA_API bool Capturing();

	NOVA_API bool CascadeBox(int cascade, Vec3& min, Vec3& max);
	NOVA_API std::string StatusText();
	NOVA_API void RegisterEditor();   // CLI: nova probevolume info
}
