#pragma once
#include <nlohmann/json.hpp>

struct IDXGISwapChain;

// 렌더 스레드 (Unity 의 Multithreaded Rendering — 동시성 로드맵 4 단계, docs/RENDER_THREAD.md). DirectX 11 · 12 · Vulkan.
//  DirectX 12 · Vulkan: 기록은 메인 그대로, 그래픽 큐 작업 (제출 · 신호 · Present) 을 렌더 스레드가 (GfxD3D12 · GfxVk 의 SetRenderThread).
//  DirectX 11: 메인 스레드는 그리기 명령을 D3D11 deferred context 에 기록하고, 프레임 끝에 명령 목록 (FinishCommandList) 을
//  무잠금 SPSC 링으로 렌더 스레드에 넘긴다. 렌더 스레드가 진짜 (immediate) 컨텍스트에서 실행 (ExecuteCommandList) 하고 Present 한다.
//  - 핑퐁: 렌더 스레드가 프레임 N 을 실행하는 동안 메인은 N+1 을 기록한다 (한 프레임까지만 앞선다)
//  - 읽기 (Map READ · 스테이징) 는 Sync — 기록한 것을 넘기고 렌더 스레드가 쉴 때까지 기다린 뒤 메인이 immediate 를 직접 쓴다.
//    쿼리 결과 (GetData) 는 ID3D11Multithread 보호 아래 immediate 에서 바로 (끝난 쿼리가 아직 기록 중이면 먼저 넘긴다)
//  - ImGui 의 따로 떠 있는 창 (뷰포트) Present 도 렌더 스레드가 그 프레임 명령 뒤에
namespace RenderThread
{
	bool Supported();          // DirectX 11 · 12 · Vulkan 장치일 때 (Windows)
	bool Enabled();
	bool SetEnabled(bool on);  // 메인 스레드. 켜면 deferred context 로 바꾸고 스레드를 띄운다 (실패하면 false)
	void InitFromSettings();   // App: 장치 · ImGui 다음 (설정 또는 NOVA_RENDER_THREAD=1)
	void Shutdown();
	void AfterPresent();   // App: Present 뒤 (렌더 스레드가 꺼져 있어도 NOVA_D3D11_DEBUGLOG=1 이면 D3D11 디버그 메시지를 Editor.log 로)

	// 프레임 끝 (Present 대신): 이번 프레임 명령 + Present 를 렌더 스레드로 (앞 프레임이 끝날 때까지만 기다린다)
	void SubmitFrame(IDXGISwapChain* swapChain, unsigned syncInterval, unsigned flags);
	void QueuePresent(IDXGISwapChain* swapChain, unsigned syncInterval, unsigned flags);   // 이번 프레임에 함께 (ImGui 뷰포트 창)
	void Flush();   // 지금까지 기록한 것을 넘긴다 (기다리지 않음)
	void Sync();    // 넘기고 렌더 스레드가 다 실행할 때까지 기다린다 (그 뒤 메인이 immediate 를 쓴다)

	// 명령 목록 번호: 지금 기록 중인 것이 받을 번호 · 렌더 스레드가 다 실행한 번호 (읽기 지연을 견디는 Map DO_NOT_WAIT 가 쓴다)
	uint64_t RecordingSerial();
	uint64_t CompletedSerial();

	nlohmann::json Info();
	void RegisterEditor();   // CLI: nova renderthread [info|set --enabled true|false]
}
