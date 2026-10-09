# 렌더 스레드 (Multithreaded Rendering)

Unity Player Settings 의 Multithreaded Rendering 과 같은 기능입니다. 그리기 명령의 실행과 Present 를 별도 렌더 스레드가 맡고, 메인 스레드는 다음 프레임으로 넘어갑니다.
동시성 로드맵 4 단계입니다 ([CONCURRENCY_ROADMAP](CONCURRENCY_ROADMAP.md)). DirectX 11 · DirectX 12 · Vulkan 에서 됩니다 (Windows).

| API | 메인 스레드 | 렌더 스레드 |
|------|------|------|
| DirectX 11 | deferred context 에 기록 → 명령 목록 | `ExecuteCommandList` · Present |
| DirectX 12 | 명령 목록 기록 · `Close` | `ExecuteCommandLists` · `Signal` · `Wait` · Present |
| Vulkan | 명령 버퍼 기록 · 스왑체인 이미지 받기 | `vkQueueSubmit2` · `vkQueuePresentKHR` |

## 켜기

- **Project Settings > Graphics > Rendering > Multithreaded Rendering** (`ProjectSettings/GraphicsSettings.json` 의 `multithreadedRendering`). 기본은 꺼짐이다.
- CLI:
  - `nova renderthread set --enabled true|false` — 바로 바뀌고 저장된다.
  - `nova renderthread info` — 프레임 · 명령 목록 · Sync · Flush 수, 프레임당 실행 · Present · 메인 대기 ms.
- 검사: `NOVA_RENDER_THREAD=1` 이면 설정과 상관없이 켠다. 그래픽 스위트를 렌더 스레드 모드로 돌릴 때 쓴다.

## DirectX 12 · Vulkan (`Source/Graphics/Common/SubmitThread.h`, 백엔드의 `SetRenderThread`)

두 API 는 명령 기록이 이미 백엔드의 명령 목록 (버퍼) 이라, 큐에 닿는 일만 렌더 스레드로 옮긴다.

**흐름**
- 메인이 목록을 닫으면 (제출 자리) 큐 작업을 무잠금 SPSC 링에 넣는다. 렌더 스레드가 넣은 순서대로 실행한다.
  - D3D12: 실행 + 펜스 신호, 펜스 기다림 (비동기 컴퓨트 결과), Present.
  - Vulkan: 제출 (기다릴 · 신호할 세마포어 값을 복사해 넘긴다), 표시.
- 메인은 GPU 펜스 · 타임라인 값으로 기다리던 그대로 기다린다. 신호가 아직 렌더 스레드의 링에 있어도 된다 (값이 오면 깨어난다).
- **핑퐁**: 다음 프레임의 Present 자리에서, 그 창의 앞 Present 작업이 끝났는지만 기다린다.
  - D3D12: 그래야 `GetCurrentBackBufferIndex` 가 맞고 `ResizeBuffers` 를 할 수 있다.
  - Vulkan: 그래야 `vkAcquireNextImageKHR` 를 부를 수 있다 (스왑체인은 한 스레드씩).
  - 그래서 메인은 한 프레임까지만 앞선다.

**고친 것**
- D3D12 명령 목록은 렌더 스레드가 실행하기 전에 `Reset` 하면 안 된다. 그래서 렌더 스레드가 켜져 있으면 제출한 목록을 명령 할당기 칸과 함께 묶어 둔다. 그 제출이 GPU 에서 끝나면 할당기와 함께 다시 쓰고, 메인은 그동안 다른 목록에 기록한다.
- 그래픽 큐 작업은 모두 링을 거친다. 메인이 큐를 직접 쓰면 앞 제출보다 먼저 들어가 순서가 깨진다 (예: D3D12 의 비동기 컴퓨트 기다림 `Queue->Wait`).
  - 컴퓨트 큐는 다른 큐라 메인이 그대로 쓴다. 아직 링에 있는 그래픽 신호를 미리 기다려도 된다 (D3D12 펜스 · Vulkan 타임라인 모두 허용).
- 창 크기 바꾸기 · 창 닫기 · `vkDeviceWaitIdle` 전에는 링을 비운다 (Sync). Vulkan 의 그 함수는 모든 큐를 혼자 써야 한다.
- 렌더 스레드가 본 장치 제거 · 잃음은 원자 값으로 넘겨, 메인이 다음 `Poll` 에서 처리한다.
- Vulkan Present 의 `OUT_OF_DATE` · `SUBOPTIMAL` 도 원자 값으로 넘긴다. 다음 프레임에 스왑체인을 다시 만든다.
- 켜고 끌 때마다 작업 번호가 1 부터다. 그래서 창마다 들고 있던 마지막 Present 번호를 지운다. 지우지 않으면 끄고 다시 켰을 때 오지 않을 번호를 기다려 멈췄다 (검사가 찾음).

## DirectX 11 구조 (`Source/Graphics/DX11/RenderThread.*`, `GfxDx11.cpp`)

**프레임 흐름 (핑퐁)**

```
메인:   [기록 N]  FinishCommandList → 링 ─┐  [기록 N+1] ...  (N 이 끝났나만 기다린다)
렌더:                                     └→ ExecuteCommandList(N) → Present
```

- 메인 스레드는 Gfx 컨텍스트 감싸개 (`DxContext`) 를 통해 **D3D11 deferred context** 에 기록한다. 엔진 · Effects · ImGui 모두 그대로다.
- 프레임 끝 (Present 자리) 에서 `FinishCommandList(TRUE)` 로 명령 목록을 만든다. 상태는 다음 기록으로 이어진다.
- 그 목록과 Present 를 **무잠금 SPSC 링**으로 렌더 스레드에 넘긴다.
- 렌더 스레드가 immediate context 에서 `ExecuteCommandList` → `Present` 한다.
- 메인은 앞 프레임이 끝났는지만 기다린다. 한 프레임까지만 앞선다.

**읽기**

| 경우 | 처리 |
|------|------|
| `Map` (DISCARD 가 아님 — 읽기 · 스테이징) | Sync: 기록한 것을 넘기고 렌더 스레드가 쉴 때까지 기다린 뒤, 메인이 immediate 에서 |
| `Map(READ, DO_NOT_WAIT)` (오클루전 · VFX 통계의 스테이징 고리) | 복사한 목록을 렌더 스레드가 이미 실행했으면 바로, 아니면 `WAS_STILL_DRAWING`. 기다리지 않는다 |
| 쿼리 결과 `GetData` | immediate 에서 바로. `ID3D11Multithread` 보호로 렌더 스레드와 함께 써도 된다 |
| 아직 넘기지 않은 목록에서 끝낸 쿼리 | `DONOTFLUSH` 면 '아직', 기다리는 확인이면 넘긴다 |
| 캡처 (`CaptureTexture`) | Sync 뒤 immediate |

**그 밖**

- Profiler 의 GPU 프레임 쿼리 (TIMESTAMP_DISJOINT) 는 Present 직전에 닫는다. 쿼리가 명령 목록 사이에 걸치면 결과가 오지 않는다.
- ImGui DX11 백엔드:
  - 그리는 컨텍스트를 deferred 로 바꾼다.
  - 따로 떠 있는 뷰포트 창의 Present 는 렌더 스레드가 그 프레임 명령 뒤에 한다.
  - 창 크기 · 지우기 전에는 묶음을 풀고 Sync 한다.
- RHI 층 (`Dx11Rhi`) 은 잡아 둔 immediate 대신 지금 기록하는 컨텍스트를 그때그때 쓴다. 효과 (셰이더 · 상수 버퍼) 적용이 그리기와 같은 목록에 들어가야 한다.
- 드라이버가 명령 목록을 직접 지원하지 않으면, deferred `UpdateSubresource` (box) 의 알려진 어긋남을 Microsoft 문서의 방법으로 고쳐 넘긴다.
- 창 크기 바꾸기 (`ResizeBuffers`) 전에는 Sync 한다. 끝낼 때는 남은 목록을 실행하고 끈다.
- 진단 (Editor.log):
  - 렌더 스레드가 켜진 동안 D3D11 디버그 층의 오류 · 경고를 남긴다.
  - 메인이 2 초 넘게 기다리면 렌더 스레드가 무엇을 하던 중인지 (실행 · Present · 쉼) 남긴다.
  - 처음 12 번은 어떤 자원이 Sync 를 불렀는지 남긴다.
  - `NOVA_D3D11_DEBUGLOG=1` 이면 렌더 스레드가 꺼져 있어도 디버그 메시지를 남긴다.

## 검사

`Tools/tests/run_tests.ps1 -Only renderthread` (DirectX 11 6 항목 + DirectX 12 · Vulkan 각 4 항목):
- 켜기 · 저장 · 프레임이 렌더 스레드에서 실행 · Present 된다.
- Materials · Forest · CityShowcase 가 켬 · 끔에서 같은 그림이다 (평균 차 0.01 ~ 0.03).
- 켠 채 Profiler GPU 시간 (타임스탬프 쿼리) 이 온다. 캡처 (Sync) 가 된다. 도시에서 프레임마다의 Sync · Flush 는 0 이다.
- 끄기 · 저장.
- DirectX 12 · Vulkan:
  - 큐 제출 · Present 가 렌더 스레드에서 된다.
  - Materials · CityShowcase 가 켬 · 끔에서 같은 그림이다.
  - 켠 채 GPU 시간 · 캡처 · Play → Stop 이 된다.

그래픽 스위트 전체를 `NOVA_RENDER_THREAD=1` 로도 돌린다 (DirectX 12 · Vulkan 스위트 포함).

## 성능 (Release, 이 PC: NVIDIA — 드라이버가 명령 목록을 직접 지원)

| 장면 | 렌더 스레드 | 끔 | |
|------|------|------|------|
| CityShowcase | 8.66 ms | 8.12 ms | 0.94 배 |
| Forest | 1.11 ms | 1.19 ms | 1.07 배 |
| Materials | 1.11 ms | 0.99 ms | 0.89 배 |

**DirectX 11 은 이 PC 에서는 빨라지지 않는다.** 그래서 기본은 꺼짐이다.

**DirectX 12 · Vulkan (Release, CityShowcase Game 뷰, 켬 · 끔 번갈아 4 번 중앙값)**

| API | 렌더 스레드 | 끔 | | 렌더 스레드의 Present · 제출 (프레임당) |
|------|------|------|------|------|
| Vulkan | 5.39 ms | 8.88 ms | **1.65 배** | 3.38 ms · 0.05 ms |
| DirectX 12 | 5.19 ms | 5.55 ms | 1.07 배 | 0.20 ms · 0.09 ms |

- Vulkan 의 `vkQueuePresentKHR` 는 이 드라이버에서 프레임당 3 ms 넘게 막힌다. 그 시간이 메인 스레드에서 빠진다.
- 두 API 모두 기록 (드라이버 일) 은 이미 명령 목록에서 하고 있어, 큐 작업만 옮겨도 메인이 거의 기다리지 않는다 (검사: 메인 대기 0 ms).
- 드라이버가 명령 목록을 직접 지원하면, 드라이버 일은 deferred context 에 *기록할 때* 메인 스레드에서 한다. 렌더 스레드의 실행은 프레임당 0.03 ms 뿐이다.
- 수직 동기 없는 Present 는 0.2 ~ 0.6 ms 라 옮겨서 얻는 것이 작고, 기록 비용과 비슷하다.
- 이득이 기대되는 경우 (검증은 아직):
  - Present 가 오래 막힐 때 (수직 동기 · GPU 큐가 가득).
  - 드라이버가 명령 목록을 흉내 낼 때. 이때는 기록이 가볍고 실행 (드라이버 일) 이 렌더 스레드로 간다.

## 함께 고친 D3D11 디버그 층 오류 (렌더 스레드와 상관없이 예전부터 나던 것)

- **#343** (IA → VS 서명: `SV_InstanceID` 가 다른 레지스터).
  - 원인: 인스턴싱 입력 레이아웃 `InstancedBasic` 은 11 칸 (INSTCOLOR · INSTSURFACE · INSTEMISSION 이 v8 ~ v10) 인데, 그림자 (26) · 노멀 깊이 (28) · 기본 (32) 의 `VertexIn_Instancing` 에는 그 셋이 없었다. 그래서 `SV_InstanceID` 가 v8 에 앉아 레이아웃과 겹쳤다.
  - 고침: 같은 세 칸을 넣었다 (읽지는 않는다).
- **#388** (렌더 타깃과 깊이 뷰의 크기가 다름).
  - 원인: 뷰 깊이 버퍼가 창 크기 이상으로 커지기만 하는 하나였다.
  - 고침: 그리는 렌더 타깃 (그 밉) 과 꼭 같은 크기의 버퍼를 크기마다 하나씩 둔다 (4 개까지, 오래 안 쓴 것부터 놓는다).
  - 뷰포트 크기가 아니라 타깃 크기에 맞춘다. 뷰포트 크기로 하면 APV 찍기 (아틀라스의 한 칸 — 뷰포트에 오프셋) 에서 깊이가 칸을 덮지 못해, probevolume 5 항목이 실패했다.
- 확인: 도시 장면에서 디버그 층 메시지가 40 개 → 0 개. render · occlusion · material · gfx 33/33.

## Graphics Jobs 조사: 메인 스레드의 시간은 기록이 아니라 준비에 있었다

2026-10-09. "명령 기록을 여러 스레드로 (DX12 · Vulkan)" 를 하려고 먼저 쟀다 (Release, CityShowcase Game 뷰, DX12, 렌더 스레드 켬).

- 그리기 명령 기록 (MeshBatcher 의 배치 그리기) 은 합해 **0.12 ms** 뿐이다 — 본 패스 0.055 · 그림자 0.036 · 깊이 0.033 ms.
  이것을 잡 여러 개로 나누어도 얻을 것이 거의 없고, 이펙트 값 · 링 버퍼 · PSO 캐시 · 리소스 상태 추적을 스레드마다 나누는 큰 공사가 든다.
- 메인 스레드 시간은 그리기 *준비* 에 있었다: 씬 전체 순회 (오브젝트 · 컴포넌트마다 `dynamic_cast` + 가상 Render), 모션 벡터의 렌더러마다 행렬 비교,
  그림자 뒤 카메라 컬링 다시 하기.

그래서 기록은 그대로 두고 준비를 데이터 지향으로 바꿨다.

| 고친 것 | 내용 |
|------|------|
| 불투명 패스 | 씬의 모든 오브젝트 · 컴포넌트를 훑던 것을 등록 목록으로. Render 를 따로 가진 것은 Skinned Mesh Renderer · 지형뿐이라 (Mesh Renderer 는 배치, Light 는 빈 함수) 그 둘만 컬링 자리 · 활성 지형 목록에서 모은다 |
| 모션 벡터 | 컬링 자리 (연속 배열) 를 차례로 읽고, 자리마다 '이 뷰가 마지막으로 본 월드 번호' 를 둔다. 번호가 그대로면 행렬을 비교하지 않는다 |
| 그림자 컬링 | 빛 컬링 결과를 다른 칸 (`ShadowCullStamp`) 에 표시한다. 그림자 뒤 카메라 컬링을 다시 하지 않고 카메라 결과로 돌아간다 (`SceneCulling::EndShadowPass`) |
| 절두체 검사 | 병렬로 나누는 기준을 8192 → 2048 개로 (도시 2852 개도 잡으로) |

**결과 (Release, CityShowcase Game 뷰, DX12, 렌더 스레드 켬)**

| | 전 | 뒤 |
|------|------|------|
| CPU (프레임) | 7.2 ms | **4.51 ms** |
| GameView 렌더 | 5.4 ms | 2.96 ms |
| Motion Vectors | 1.05 ms | 0.09 ms |
| 불투명 (Opaque) | 0.91 ms | 0.48 ms |
| 깊이 프리패스 | 2.06 ms | 1.22 ms |
| 컬링 | 0.46 ms | 0.156 ms |
| 그림자 | 0.82 ms | 0.65 ms |
| fps | 134 | **200** |

GPU 는 그대로 약 2.8 ms 다. 이제 CPU 와 GPU 가 비슷하다.

`nova perf --top N` 으로 CPU 구간을 N 개까지 (기본 24), 구간마다 프레임당 불린 수 (`calls`) 도 나온다.

## 아직

- 매 프레임 Map READ 로 읽는 기능 (가상 텍스처 피드백) 은 Sync 를 부른다 → `DO_NOT_WAIT` 고리로 바꾸기
- 그리기 목록 모으기 · 오클루전 · 나무 — 2026-10-10 다시 쟀다 (Release 도시, 측정 흔들림이 커서 같은 빌드에서 켬 · 끔을 번갈아):
  - Collect 0.5 ms = 오브젝트 훑기 0.2 (병렬) + 재질 · 묶음 번호 0.3 (차례로). 훑기를 컬링 자리로 바꿔도 차이가 흔들림 (프레임의 9 % 안팎) 보다 작아 되돌렸다.
  - 오클루전 준비 = 목록 0.09 + 올리기 0.13 + 마무리 0.29 + 그림자 0.21 ms. 내용이 같을 때 올리기를 건너뛰는 것은 DX12 · Vulkan 의 Dynamic 버퍼가 Map 마다 업로드 링의 새 자리라 (건너뛰면 덮인 링을 읽는다) 버퍼 종류부터 바꿔야 한다 — 얻는 것 약 0.1 ms.
  - 나무 0.46 ms = 그리기 (오클루전 목록 · 호출) 0.29 + 목록 0.15.
  - 더 줄이려면 그리기 목록을 프레임 사이에 다시 쓰는 것인데, 재질 · 속성 블록 · 렌더러 설정이 바뀌는 모든 경로에 번호가 필요하다 (놓치면 낡은 재질로 그린다).
- 명령 기록을 여러 잡으로 나누기 (Graphics Jobs) — 위 측정으로는 지금 이득이 작다. 그리기가 수만 개로 늘면 다시 잰다
