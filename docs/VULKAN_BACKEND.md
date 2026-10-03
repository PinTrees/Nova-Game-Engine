# Vulkan 그래픽 백엔드

DirectX 11 · OpenGL 4.5 에 이은 세 번째 그래픽 API. 안드로이드 빌드로 가는 첫 단계다.
엔진 렌더러는 지금처럼 Gfx 층(`Gfx.h`, D3D11 모양)과 효과(`FxEffect`)만 쓰고, Vulkan 구현이 그 뜻을 Vulkan 1.3 명령으로 바꾼다.

## 진행 단계

| 단계 | 내용 | 상태 |
|---|---|---|
| 1 | 화면 없는 Vulkan 장치 + Gfx 검사 장면이 DX11 과 같다 (`nova vulkan gfx-test`) | **완료** — 화소 차이 최대 1, 검증 레이어 오류 0 |
| 2 | RHI 검사 장면 (`nova vulkan rhi-test`) | **완료** — 화소 차이 최대 1, 검증 레이어 오류 0 |
| 3 | 에디터가 Vulkan 으로 실행 (스왑체인 · ImGui · 창 크기 · `-force-vulkan` · 안 되면 DX11) | 다음 |
| 4 | 렌더 회귀 7 장면이 DX11 과 같다 · 성능 · 빌드한 게임 | 예정 |

3 단계 전까지 설정의 Graphics API 메뉴에서는 Vulkan 을 고를 수 없다 (`VulkanGraphicsBackend::IsSupported() = false`).

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
  동기화는 일부러 거칠다 — 렌더링을 끝낸 뒤 · 복사 뒤 전역 장벽 하나 (D3D11 드라이버가 하는 일)
- **그리기**: 타깃 · 읽는 이미지 배치 → 파이프라인 (효과 pass · 입력 배치 · 래스터 · 블렌드 · 깊이 상태 · 타깃 형식 키 캐시) →
  디스크립터 집합 (내용 해시 캐시, 상수는 UNIFORM_BUFFER_DYNAMIC 오프셋) → 정점 · 인덱스 → 동적 상태 (뷰포트 · 가위 · 블렌드 상수 · 스텐실 기준 · 정점 간격)
- **DYNAMIC 버퍼**: Map(WRITE_DISCARD) = CPU 사본, Unmap 때 링으로. 다음 기록에서 링 위치가 사라졌으면 사본에서 다시 올린다 (GL 과 같은 방식)
- **좌표**: 셰이더의 `-fvk-invert-y` → 프레임버퍼 행 0 = D3D 의 위. 뷰포트 · 가위 숫자 그대로, D3D 앞면(시계) = `VK_FRONT_FACE_CLOCKWISE`
- D3D11 처럼 렌더 타깃으로 묶인 텍스처를 셰이더가 읽으면 빈 텍스처로 바꾼다 (읽기 전용 깊이 뷰는 그대로 읽는다)

## 아직 없는 것

- 스왑체인 · ImGui 렌더러 · 창 크기 바꾸기 (3 단계)
- compute · UAV · 구조화 버퍼 · 스트림 출력 (OpenGL 과 같이 옛 예제만 쓴다) — 그런 자원을 쓰는 pass 는 로그를 남기고 그리지 않는다
- 오클루전 쿼리 (결과 1), 인스턴스 간격 > 1, 테두리 색은 Vulkan 기본 세 가지 중 가까운 것

## 검사

```bash
nova vulkan gfx-test --out <폴더>
nova vulkan rhi-test --out <폴더>
```

DX11 엔진 장치와 화면 없는 Vulkan 장치에 같은 장면을 그려 PNG 와 차이 그림을 남긴다. 결과 JSON 의 `diff.max` (성분 차이 최대) 와 `validationErrors`.
2026-10-04, GTX 1660 SUPER (드라이버 560.94), Debug + 검증 레이어: 두 장면 모두 차이 최대 1, 검증 오류 · 경고 0.
