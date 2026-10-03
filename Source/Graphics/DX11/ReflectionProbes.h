#pragma once

class ReflectionProbe;
class InstancedBasicEffect;

// Reflection Probe 의 찍기 · 필터 · 셰이더 값 (Unity URP 의 Forward+ 반사 프로브 블렌드와 같은 방식)
//  - 찍기: 한 면 = EditorApp 의 Game 뷰 그리기를 90° 카메라로 (SetCapture) — 그동안 프로브 반사는 끔 (Unity 의 1 바운스)
//  - 실시간 프로브: 자기 큐브 (밉 = GenerateMips). 굽기: 같은 큐브를 DDS 로 저장, Baked · Custom 은 DDS 를 읽음
//  - 뷰마다 (Select) 보이는 프로브를 Importance · 크기 순으로 최대 8 개 → 큐브 배열의 칸에 GGX 로 거칠기별 밉을 필터 (내용이 바뀐 칸만)
//  - 셰이더 (32. InstancedBasic.fx 의 ProbeReflection): 픽셀마다 상자 안 가중치 (Blend Distance) 로 섞고, 남는 몫은 하늘
namespace ReflectionProbes
{
	constexpr int kMaxProbes = 8;

	// 프로브 한 면 그리기 요청 (EditorApp 이 Game 뷰와 같은 길로 그린다)
	struct CaptureView
	{
		XMMATRIX View, Proj;
		GfxRenderTargetView* Target = nullptr;
		D3D11_VIEWPORT Viewport = {};
		XMFLOAT3 Position = {};
		uint32 CullingMask = 0xFFFFFFFFu;
		bool SolidColor = false;
		float Background[4] = {};
		float ShadowDistance = 100.0f;
	};
	NOVA_API void SetCapture(std::function<void(const CaptureView&)> fn);

	// 프레임마다 한 번, 뷰를 그리기 전: 굽기 요청 · 실시간 프로브 다시 찍기
	NOVA_API void Update();
	// 이 뷰에 쓸 프로브를 고르고 큐브 배열 칸을 채운다 — 뷰의 렌더 타깃을 묶기 전에 부른다
	NOVA_API void Select(const XMFLOAT3& eye, CXMMATRIX viewProj);
	// Select 결과를 이펙트에 (gProbeCubes · gProbeData · gProbeParams). 변수가 없는 이펙트는 그냥 넘어간다
	NOVA_API void Bind(InstancedBasicEffect* fx);
	NOVA_API int SelectedCount();
	NOVA_API bool Capturing();

	NOVA_API void RequestBake(ReflectionProbe* p);     // 다음 프레임에 굽기 (Inspector 의 Bake)
	NOVA_API void RequestRender(ReflectionProbe* p);   // 실시간 프로브 다시 찍기 (Via Scripting · 설정 변경)
	// 지금 찍어 Assets/.../<씬 이름>/ReflectionProbe-<n>.dds 로 저장 (씬이 저장돼 있어야 — Unity 와 같음)
	NOVA_API bool Bake(ReflectionProbe* p, std::string& error);
	NOVA_API void Forget(ReflectionProbe* p);

	NOVA_API void RegisterEditor();   // CLI: nova probe <info | bake | render>
}
