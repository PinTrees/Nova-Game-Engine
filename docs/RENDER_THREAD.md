# 렌더 스레드 (Multithreaded Rendering)

Unity Player Settings 의 Multithreaded Rendering 과 같은 기능입니다. 그리기 명령의 실행과 Present 를 별도 렌더 스레드가 맡고, 메인 스레드는 다음 프레임으로 넘어갑니다.
동시성 로드맵 4 단계입니다 ([CONCURRENCY_ROADMAP](CONCURRENCY_ROADMAP.md)). 지금은 DirectX 11 에서 됩니다.

## 켜기

- **Project Settings > Graphics > Rendering > Multithreaded Rendering** (`ProjectSettings/GraphicsSettings.json` 의 `multithreadedRendering`). 기본은 꺼짐이다.
- CLI:
  - `nova renderthread set --enabled true|false` — 바로 바뀌고 저장된다.
  - `nova renderthread info` — 프레임 · 명령 목록 · Sync · Flush 수, 프레임당 실행 · Present · 메인 대기 ms.
- 검사: `NOVA_RENDER_THREAD=1` 이면 설정과 상관없이 켠다. 그래픽 스위트를 렌더 스레드 모드로 돌릴 때 쓴다.

## 구조 (`Source/Graphics/DX11/RenderThread.*`, `GfxDx11.cpp`)

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

`Tools/tests/run_tests.ps1 -Only renderthread` (6 항목):
- 켜기 · 저장 · 프레임이 렌더 스레드에서 실행 · Present 된다.
- Materials · Forest · CityShowcase 가 켬 · 끔에서 같은 그림이다 (평균 차 0.01 ~ 0.03).
- 켠 채 Profiler GPU 시간 (타임스탬프 쿼리) 이 온다. 캡처 (Sync) 가 된다. 도시에서 프레임마다의 Sync · Flush 는 0 이다.
- 끄기 · 저장.

그래픽 스위트 전체를 `NOVA_RENDER_THREAD=1` 로도 돌린다.

## 성능 (Release, 이 PC: NVIDIA — 드라이버가 명령 목록을 직접 지원)

| 장면 | 렌더 스레드 | 끔 | |
|------|------|------|------|
| CityShowcase | 8.66 ms | 8.12 ms | 0.94 배 |
| Forest | 1.11 ms | 1.19 ms | 1.07 배 |
| Materials | 1.11 ms | 0.99 ms | 0.89 배 |

**이 PC 에서는 빨라지지 않는다.** 그래서 기본은 꺼짐이다.
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

## 아직

- DirectX 12 · Vulkan: 명령 기록은 이미 백엔드가 하고 있다 — 제출 (ExecuteCommandLists · vkQueueSubmit) · Present · 펜스 대기를 렌더 스레드로
- 매 프레임 Map READ 로 읽는 기능 (가상 텍스처 피드백) 은 Sync 를 부른다 → `DO_NOT_WAIT` 고리로 바꾸기
- 메인의 기록 자체를 여러 잡으로 나누기 (deferred context 여럿 — Graphics Jobs)
