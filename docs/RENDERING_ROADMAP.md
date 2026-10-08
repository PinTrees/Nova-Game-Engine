# 렌더링 현대화 로드맵 (DirectX 12 · Vulkan · Render Graph · Async Compute · Virtual Texturing · Clustered)

2026-10-08 사용자 요청: "DirectX 12 / Vulkan 최신 명시적 API, Render Graph 기반 동적 패스 스케줄링, 비동기 컴퓨트, 가상 텍스처링, 클러스터드 포워드/디퍼드 셰이딩".
서로 기대는 것이 있어 아래 순서로 한다. 단계마다 검사 스위트 · 문서 · 커밋.

## 지금 구조 (시작점)

- 그래픽 층 `Source/Graphics/RHI/Gfx.h`: D3D11 API 를 그대로 흉내 낸 추상 클래스 (GfxDevice · GfxContext …) — DX11 · OpenGL 4.5 · Vulkan 1.3 · GLES 3.2 · WebGPU 가 모두 이것을 구현 (즉시 컨텍스트, 암묵적 상태 · 배리어)
- 셰이더: `.fx` (Effects11) → DX11 은 fxc, 나머지는 ShaderCross (DXC → SPIR-V → GLSL · WGSL)
- 프레임: `EditorApp::RenderGameView` · `_Editor_OnSceneRender` 에 손으로 쓴 같은 순서 두 벌 (플레이어 · 안드로이드 · 웹은 RenderGameView)
- 조명: 포워드, 빛 4 개, 컬링 없음 → **1 단계에서 Forward+**

## 단계

| # | 무엇 | 왜 이 순서 | 상태 |
|---|------|------|------|
| 1 | **Clustered Forward+** — 4 개 밖의 빛을 클러스터로 (CPU 로 짓고 텍스처 3 개, 모든 백엔드) | 가장 눈에 띄고 다른 것에 기대지 않는다 | **완료** ([FORWARD_PLUS](FORWARD_PLUS.md)) |
| 2 | **Render Graph** — 패스를 노드로 (읽기 · 쓰기 선언), 쓰이지 않는 패스 빼기, 수명으로 임시 텍스처 재사용, Game · Scene 두 벌을 한 그래프로 | 3 · 4 · 6 이 이 위에 선다 (큐 · 배리어 · 비동기를 그래프가 정한다) | **완료** ([RENDER_GRAPH](RENDER_GRAPH.md)) |
| 3 | **DirectX 12 백엔드** — Gfx 층을 D3D12 로 (Vulkan 백엔드처럼): 명령 목록 · 디스크립터 힙 · 루트 시그니처 · 리소스 상태 추적, 셰이더는 ShaderCross 에 DXC → DXIL | 명시적 API — 4 의 바탕 | **완료** ([DIRECTX12_BACKEND](DIRECTX12_BACKEND.md)) |
| 4 | **Async Compute** — DX12 · Vulkan 에 컴퓨트 큐 + 펜스 / 타임라인 세마포어. Render Graph 가 컴퓨트 패스 (VFX 시뮬레이션 · Hi-Z · 클러스터 GPU 짓기 …) 를 다른 큐로, DX11 · GL 은 같은 큐로 | 2 · 3 필요 | **완료** — DX12 · Vulkan ([ASYNC_COMPUTE](ASYNC_COMPUTE.md)) |
| 5 | **Virtual Texturing** (소프트웨어 — Unity Streaming Virtual Texturing 처럼): 페이지 표 + 물리 캐시 아틀라스 + 피드백 버퍼, 지형 · 큰 텍스처 | 스파스 리소스 없이 모든 백엔드 | **완료** ([VIRTUAL_TEXTURING](VIRTUAL_TEXTURING.md)) |
| 6 | **Clustered Deferred** — Rendering Path = Forward / Forward+ / Deferred (URP 와 같은 선택), G-Buffer + 1 단계 클러스터로 조명 패스 | 2 의 패스 · 1 의 클러스터 재사용 | |

## 원칙

- 모든 백엔드 (DX11 · GL · Vulkan · GLES · WebGPU) 가 계속 돌아야 한다 — 새 기능은 되는 곳에서 켜고, 안 되는 곳은 예전 길로
- 검사는 창 없는 편집기 + CLI (Tools/tests/run_tests.ps1 -Only …), 그래픽 변경은 넓은 회귀 스위트
- Unity 이름 · 동작을 따른다 (Rendering Path, Rendering Debugger, Streaming Virtual Texturing …)
