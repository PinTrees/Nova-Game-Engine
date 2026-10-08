# 동시성 · 멀티스레딩 로드맵 (Job System · 무잠금 큐 · 메인-렌더-물리 핑퐁)

2026-10-08 사용자 요청: "Task/Fiber 기반 Work-Stealing Job System, 무잠금(Lock-Free) 큐, 메인-렌더-물리 스레드 간 락 없는 데이터 핑퐁/이중 버퍼링 아키텍처 — 동시성 & 멀티스레딩 개선".
단계마다 검사 스위트 · 문서 · 커밋.

## 지금 구조 (시작점)

- 프레임은 메인 스레드 하나에서 차례로 돈다: 입력 → 스크립트 Update → 물리 (고정 스텝 + FixedUpdate) → 2D 물리 → 에디터 · 오디오 · 입자 · UI → 카메라 · 빛 · 컬링 → 렌더 (Render Graph) → ImGui → Present
- 스레드는 제각각이다:
  - Jolt `JobSystemThreadPool` (코어 − 1 개 스레드 — 물리만 쓴다)
  - 가상 텍스처 읽기 스레드 하나
  - 지형 생성 · 씬 읽기 · 셰이더 그래프 컴파일의 `std::async`
  - C# 컴파일 `std::thread`
  - → 코어보다 스레드가 많고, 나머지 일은 메인 스레드 하나에 몰린다
- `TaskSystem::mainThreadTasks` = 동기화 없는 `std::queue` (다른 스레드에서 넣으면 경합 — 깨질 수 있다)
- Profiler 는 메인 스레드 구간만 기록한다

## 단계

| # | 무엇 | 왜 이 순서 | 상태 |
|---|------|------|------|
| 1 | **무잠금 도구 + Job System**: 작업 훔치기 덱 (Chase-Lev) · MPMC 링 (Vyukov) · SPSC 링 · 삼중 버퍼, 일꾼 = 코어 − 1 (메인도 돕는다), 우선순위 (High · Normal · Background), Counter 기다리기 = **파이버** (Windows — 기다리는 작업은 파이버를 내려놓고 일꾼은 다른 일), 다른 플랫폼은 돕기. ParallelFor. Profiler Timeline 에 일꾼 줄 | 2 ~ 4 가 이 위에 선다 | **완료** ([JOB_SYSTEM](JOB_SYSTEM.md)) |
| 2 | **엔진에 적용**: Jolt 를 Nova 잡 위로 (스레드 풀 하나), 무거운 반복을 ParallelFor (프로파일로 고른다), 백그라운드 일 (VT 읽기 · 지형 · 씬 읽기 · 셰이더 그래프) 을 Background 잡으로, 메인 스레드 작업 큐를 MPSC 무잠금으로 | 1 필요 | **완료** ([JOB_SYSTEM](JOB_SYSTEM.md#엔진에-적용-동시성-로드맵-2-단계)) |
| 3 | **물리 ↔ 메인 핑퐁**: 마지막 고정 스텝의 시뮬레이션을 잡으로 돌려 프레임 나머지 (2D 물리 · 입자 · UI · 컬링 · 렌더) 와 겹친다. 바디 상태는 이중 버퍼 (물리가 뒤 버퍼에 쓰고 원자 교환), 다음 프레임 시작 (스크립트 Update 앞) 에 적용 · 충돌 콜백. 물리 API 는 들어올 때 진행 중인 스텝을 끝낸다 | 1 · 2 필요 | **완료** ([ASYNC_PHYSICS](ASYNC_PHYSICS.md)) — Project Settings > Physics > Simulate During Rendering (기본 꺼짐) |
| 4 | **메인 ↔ 렌더 스레드** (Unity 의 Multithreaded Rendering): GfxContext 를 기록 프록시로 — 메인 스레드는 명령 스트림 (무잠금 SPSC 링, 프레임 단위 핑퐁) 에 쓰고 렌더 스레드가 진짜 컨텍스트로 실행 · Present. 읽기 (Map READ · 쿼리 결과 · 캡처) 만 기다린다. 먼저 DX11 (장치가 스레드에 안전), 그다음 DX12 · Vulkan | 1 필요, 가장 크다 | |

## 원칙

- 모든 플랫폼이 계속 돌아야 한다: 웹 (스레드 없는 wasm) 은 일꾼 0 — 잡은 부른 스레드에서 차례로. 파이버는 Windows 만, 안드로이드는 돕기
- 메인 스레드는 파이버로 바꾸지 않는다 (ImGui · 창 · 그래픽 장치 · C# 런타임이 메인 스레드에 묶여 있다)
- 잡 안에서는 Unity 처럼 엔진 객체 (GameObject · 컴포넌트) 를 만들거나 지우지 않는다 — 읽기 · 자기 몫 쓰기만
- 결과가 단일 스레드와 같아야 한다 (물리 · 렌더 그림 회귀). 검사는 창 없는 편집기 + CLI
