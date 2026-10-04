# Vulkan 그래픽 백엔드

DirectX 11 · OpenGL 4.5 에 이은 세 번째 그래픽 API. 안드로이드 빌드로 가는 첫 단계다.
엔진 렌더러는 지금처럼 Gfx 층(`Gfx.h`, D3D11 모양)과 효과(`FxEffect`)만 쓰고, Vulkan 구현이 그 뜻을 Vulkan 1.3 명령으로 바꾼다.

## 진행 단계

| 단계 | 내용 | 상태 |
|---|---|---|
| 1 | 화면 없는 Vulkan 장치 + Gfx 검사 장면이 DX11 과 같다 (`nova vulkan gfx-test`) | **완료** — 화소 차이 최대 1, 검증 레이어 오류 0 |
| 2 | RHI 검사 장면 (`nova vulkan rhi-test`) | **완료** — 화소 차이 최대 1, 검증 레이어 오류 0 |
| 3 | 에디터가 Vulkan 으로 실행 (스왑체인 · ImGui · 창 크기 · `-force-vulkan` · 안 되면 DX11) | **완료** — 에디터 전체 화면이 DX11 과 같다, 창 크기 바꾸기 따라감 |
| 4 | 렌더 회귀 7 장면이 DX11 과 같다 · 빌드한 게임 · 성능 | **완료** — 장면 7/7, 빌드한 게임 실행, Release 성능 (아래) |

### 성능 (Release, GTX 1660 SUPER, `run_tests.ps1 -Only perf`, 240 프레임 평균 ms — 낮을수록 좋음)

| 장면 | DirectX 11 | OpenGL | Vulkan |
|---|---|---|---|
| Materials | 0.93 (GPU 0.81) | 1.32 (GPU 1.01) | 1.02 (GPU 0.76) |
| Trees | 2.27 (GPU 2.18) | 2.24 (GPU 1.98) | **1.40** (GPU 1.14) |

장벽을 서브리소스 단위로 바꾼 뒤 Vulkan 이 Materials 1.14 → 1.02, Trees 1.49 → 1.40. 가벼운 장면은 CPU 쪽 (그리기마다 디스크립터 키 · 파이프라인 키) 이 DX11 보다 조금 무겁다

- 고르기: Edit → Graphics API 메뉴의 Vulkan (시험 단계), 실행 인자 `-force-vulkan`, CLI `nova open <프로젝트> --graphics vulkan`, Player Settings 의 API 목록.
  Vulkan 장치를 못 만들면 DirectX 11 로 대체한다 (`[Graphics] Vulkan failed to start - using DirectX 11`)
- 창 표시 (`GfxVkSwapchain.cpp`): 엔진은 백버퍼 텍스처에 그리고 Present 가 스왑체인 이미지로 블릿 (Vulkan 은 행 0 = 위 → 뒤집지 않음).
  수직 동기 0 = MAILBOX (없으면 IMMEDIATE), 창 크기 · OUT_OF_DATE 면 다시 만든다. CPU 는 GPU 보다 2 프레임 넘게 앞서 가지 않는다
- 에디터 UI: `Source/Editor/ImGuiGfx.*` — Gfx 층 + `Shaders/56. ImGui.fx` 로 그리는 API 공용 ImGui 렌더러 (imgui_impl_dx11 과 같은 그림).
  창 밖으로 뺀 창 (ImGui 뷰포트 = OS 창) 은 창마다 그림 텍스처 + 자기 스왑체인 (`GfxVk::PresentWindow`, 창을 닫으면 `ReleaseWindow`)
- 빌드한 게임: Player Settings 에 Vulkan 이 있으면 셰이더 변환기 (dxcompiler) 와 `ShaderCache/SPIRV` 를 같이 넣는다

## SDK 없이 빌드 · 실행

- Vulkan C 헤더는 저장소의 `ThirdParty/Vulkan/include` (Khronos, Apache-2.0 OR MIT). 엔진은 `VK_NO_PROTOTYPES` 로 빌드하고
  함수는 실행 중에 `vulkan-1.dll` (그래픽 드라이버에 들어 있음) 에서 불러온다 (`VkLoader`). 빌드 · 실행 · 엔진 설치에 Vulkan SDK 가 필요 없다
- 셰이더는 엔진에 들어 있는 DXC(`dxcompiler.dll`) 로 HLSL → SPIR-V. 결과는 `Binaries/ShaderCache/SPIRV/<이름>_<해시>.json`
- 드라이버가 없거나 Vulkan 1.3 · 동적 렌더링 · synchronization2 · 타임라인 세마포어가 없으면 장치 만들기가 실패한다 (3 단계에서 DX11 로 대체)
- 개발 PC 에 LunarG SDK 가 있으면 Debug 빌드는 검증 레이어(`VK_LAYER_KHRONOS_validation`)를 켠다. 환경 변수 `NOVA_VK_VALIDATION=0/1` 로 끄고 켠다.
  메시지는 `Editor.log` 의 `[Vulkan]` 줄 (같은 메시지는 한 번)
- `NOVA_VK_DEVICE=<이름 일부>` 로 GPU 를 고른다 (기본: 외장 > 내장)

## 셰이더 (ShaderCross::CompileEffectSpirv)

- .fx 하나에서 pass 마다 단계별 SPIR-V. GL 과 같은 DXC 옵션 (`-fvk-use-dx-layout` · `-fvk-invert-y` · `-fvk-use-dx-position-w`) +
  정점 셰이더 `-fvk-support-nonzero-base-instance` (D3D 의 SV_InstanceID 는 시작 인스턴스를 더하지 않는다)
- 장식만 고친다: 모든 자원 = 집합 0, 바인딩 번호 = 효과 안에서 이름마다 고정 (cbuffer · 텍스처 · 샘플러가 따로).
  단계 사이 location = 앞 단계 출력의 같은 의미(SEMANTIC). 반사 뒤 DXC 의 HLSL 장식(`SPV_GOOGLE_*`)을 지운다 — 그 확장이 없는 GPU(안드로이드)를 위해
- 비교 샘플러와 같이 쓰는 텍스처 = 깊이 텍스처 (빈 칸에 값 1 깊이 더미). 픽셀 셰이더가 쓰지 않는 색 타깃은 쓰기 마스크 0
- 검사: `nova vulkan shaders` 가 모든 .fx 를 변환하고 단계 모듈을 `ShaderCache/SPIRV/dump/*.spv` 로 — SDK 의 `spirv-val --target-env vulkan1.3` 로 검증
  (45 효과 · 475 pass · 1051 모듈, 옛 예제 `13. VecAdd` 의 compute 하나만 scalarBlockLayout 필요)

## 장치 구조 (`Source/Graphics/Vulkan/`)

| 파일 | 하는 일 |
|---|---|
| `VkLoader.*` | `vulkan-1.dll` · 인스턴스 · 검증 레이어 · 함수 목록 |
| `VkMap.*` | DXGI 형식 · D3D11 상태 → Vulkan 값 (타입 없는 형식 → MUTABLE_FORMAT, R24G8 → D24S8 깊이, A8 · BGRX 는 스위즐) |
| `GfxVkDevice.cpp` | 메모리 (64 MB 블록 안 나눠 쓰기) · 링 · 제출 · 자원 · 뷰 · 상태 · 파이프라인 캐시 |
| `GfxVkContext.cpp` | D3D11 즉시 컨텍스트의 뜻: 렌더링 · 배치 바꾸기 · 그리기 · 복사 · Map · 쿼리 · 텍스처 읽기 |
| `VkRhi.cpp` | Vulkan 효과(`Rhi::Effect`) · `Rhi::Device` (Gfx 위의 얇은 층) |
| `GfxVkShared.h` | 장치 ↔ 효과 (바인딩 배치 · 프로그램 · 상수 링 · SetProgram) |
| `VulkanTools.*` | CLI `nova vulkan shaders · gfx-test · rhi-test` |

- **제출**: 타임라인 세마포어 하나. 제출마다 값 +1. 링 조각 · 디스크립터 풀 · 명령 풀 · 지운 자원은 "마지막으로 쓴 제출 값" 이 끝난 뒤에 다시 쓰거나 없앤다.
  기다림은 10 초가 넘으면 장치 잃음으로 본다 (PC 가 굳지 않게)
- **기록**: 본 명령 버퍼 + 업로드 명령 버퍼 (새 자원의 처음 데이터, 같은 제출에서 먼저). 렌더링(동적 렌더링)은 그리기 때 시작하고
  타깃이 바뀌거나 복사 · 지우기 · 배치 바꾸기가 필요하면 끝낸다. 이미지 배치는 서브리소스마다 따라간다.
  **동기화**: 서브리소스마다 "쓴 뒤 아직 장벽 없음" (`Image::Written`) 을 따라가 배치가 바뀌거나 같은 배치로 다시 쓰고 읽을 때만 그 서브리소스에 장벽
  (그림자 캐스케이드처럼 다른 조각을 차례로 그리면 장벽이 없다). 버퍼 복사 쓰기는 앞뒤 전역 장벽, 제출 끝에 호스트 읽기 장벽.
  `NOVA_VK_SYNC_VALIDATION=1` 이면 검증 레이어의 동기화 검사(경쟁 탐지)를 켠다 — 검사 vulkan 10/10 에서 경쟁 0
- **그리기**: 타깃 · 읽는 이미지 배치 → 파이프라인 (효과 pass · 입력 배치 · 래스터 · 블렌드 · 깊이 상태 · 타깃 형식 키 캐시) →
  디스크립터 집합 (내용 해시 캐시, 상수는 UNIFORM_BUFFER_DYNAMIC 오프셋) → 정점 · 인덱스 → 동적 상태 (뷰포트 · 가위 · 블렌드 상수 · 스텐실 기준 · 정점 간격)
- **DYNAMIC 버퍼**: Map(WRITE_DISCARD) = CPU 사본, Unmap 때 링으로. 다음 기록에서 링 위치가 사라졌으면 사본에서 다시 올린다 (GL 과 같은 방식)
- **좌표**: 셰이더의 `-fvk-invert-y` → 프레임버퍼 행 0 = D3D 의 위. 뷰포트 · 가위 숫자 그대로, D3D 앞면(시계) = `VK_FRONT_FACE_CLOCKWISE`
- D3D11 처럼 렌더 타깃으로 묶인 텍스처를 셰이더가 읽으면 빈 텍스처로 바꾼다 (읽기 전용 깊이 뷰는 그대로 읽는다)
- **compute** (오클루전 컬링 — [OCCLUSION_CULLING.md](OCCLUSION_CULLING.md)): (RW)StructuredBuffer · (RW)ByteAddressBuffer = 스토리지 버퍼
  (버퍼 SRV · UAV, DYNAMIC 은 링 자리), RWTexture2D = 스토리지 이미지 (배치 GENERAL, 서브리소스마다 따라감). compute pass 의 파이프라인은 프로그램을 만들 때.
  디스패치 앞뒤로 전역 장벽 (버퍼는 배치를 따라가지 않는다 — 앞의 정점 · 간접 인자 읽기, 뒤의 읽기). 간접 그리기는 `drawIndirectFirstInstance` 기능,
  `ClearUnorderedAccessViewUint` = `vkCmdFillBuffer` · `vkCmdClearColorImage`
- **오클루전 쿼리**: 쿼리마다 칸 4 개 고리 (호스트 리셋 — 앞 결과가 GPU 에 남은 칸을 기다리지 않게). `Begin` 은 다음 그리기가 렌더링을 시작한 뒤에 기록
  (쿼리는 한 렌더링 안에서 시작 · 끝, 중간에 렌더링이 끊기면 "보임"). **예측** (`SetPredication`) = `VK_EXT_conditional_rendering`:
  첫 SetPredication 때 끝난 예측 쿼리 결과를 한꺼번에 링으로 복사 (`vkCmdCopyQueryPoolResults` + WAIT — GPU 만 기다린다), 그리기마다 그 값으로 조건부 렌더링.
  확장이 없으면 예측 없이 그린다

## 아직 없는 것

- 그리기마다의 CPU 비용 줄이기 (가벼운 장면에서 DX11 보다 약 10 % 느림)
- 형식 버퍼 (`Buffer<T>` — texel buffer) · append · counter UAV · 스트림 출력 — 그런 자원을 쓰는 pass 는 로그를 남기고 그리지 않는다
- 인스턴스 간격 > 1, 테두리 색은 Vulkan 기본 세 가지 중 가까운 것

## 검사

```bash
nova vulkan gfx-test --out <폴더>
nova vulkan rhi-test --out <폴더>
```

DX11 엔진 장치와 화면 없는 Vulkan 장치에 같은 장면을 그려 PNG 와 차이 그림을 남긴다. 결과 JSON 의 `diff.max` (성분 차이 최대) 와 `validationErrors`.

`run_tests.ps1 -Only vulkan` = 위 두 검사 + 에디터를 DX11 · Vulkan 으로 띄워 렌더 7 장면 비교 (render 묶음과 같은 기준) + 로그에 검증 오류 없음.
2026-10-04, GTX 1660 SUPER (드라이버 560.94), Debug + 검증 레이어: **10/10** — gfx · rhi 차이 최대 1, 장면 최대 차이 1 ~ 14 (Trees 는 바람 잎만 4.3 %), 검증 오류 0.
