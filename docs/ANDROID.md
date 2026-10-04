# 안드로이드

NOVA 를 안드로이드에서 돌리는 작업. 지금은 **1 단계 첫 목표 완료**: 엔진의 렌더링 코드(RHI)를 NDK 로 빌드해
MuMu 플레이어(에뮬레이터) 안의 OpenGL ES 3.2 로 검사 장면을 그리고, PC 의 DirectX 11 그림과 화소 차이 **최대 1** 로 같다.

## 그래픽 API

- **MuMu 플레이어 12 (Android 12L, x86_64)** 의 게스트에는 Vulkan 이 없다 (로더 1.1, 물리 장치 0 개 — 에뮬레이터 드라이버 `vulkan.ranchu.so` 가 장치를 내주지 않음).
  대신 **OpenGL ES 3.2** (Adreno 640 에뮬레이션: geometry shader · cube map array · float 렌더 타깃 · ASTC/ETC2). 그래서 에뮬레이터 검사는 GLES 3.2 로 한다
- 실제 휴대폰용 Vulkan 경로는 그다음 (대부분 Vulkan 1.1 — 지금 Vulkan 백엔드가 쓰는 동적 렌더링 · synchronization2 를 확장으로 받거나 렌더 패스로 대신해야 한다)

## 구조

| 위치 | 하는 일 |
|---|---|
| `Android/CMakeLists.txt` | NDK 로 `libnova.so` (NativeActivity, Java 코드 없음). 엔진 소스는 `Source/` 에서 필요한 것만 |
| `Android/Include/` | 엔진의 `#include "pch.h"` 를 받는 안드로이드 pch · `WinCompat.h` (Windows · D3D11 설명 구조체 대체) · `Windows.h` · `PathManager.h` 대체 |
| `Android/Source/AndroidMain.cpp` | 진입점: 인텐트 값(JNI) → 화면 없는 EGL (pbuffer) → 검사 실행 → 결과 BMP · JSON, logcat `NOVA_TEST {json}` |
| `Android/Source/GLESRhi.cpp` | RHI 의 OpenGL ES 3.2 구현 (데스크톱 GL 구현과 같은 규칙, DSA 없이 바인딩 방식) |
| `Android/build.py` | Gradle 없이 APK: NDK CMake → aapt2 link (+ assets) → zipalign → apksigner (디버그 키) |
| `ThirdParty/DirectXMath/` | DirectXMath (MIT, Windows SDK 의 것) + `sal.h` 대체 — 안드로이드만 쓴다 |
| `Source/Build/AndroidTools.*` | 에디터 CLI `nova android shaders --out 폴더` |
| `Source/Graphics/Common/FxStates.*` | `.fx` 상태 블록 → D3D11 설명 (API 공용, 예전 GLState 안에 있던 것) |
| `Source/Graphics/ShaderCross/ShaderCrossJson.*` | 셰이더 변환 결과 ↔ JSON (PC 캐시 · 안드로이드 셰이더 묶음 공용) |

### 셰이더

휴대폰에는 셰이더 변환기(DXC)를 넣지 않는다. PC 에서 `nova android shaders --out Android/build/assets/Shaders` 가 모든 `.fx` 를
GLSL ES 3.20 으로 바꿔 `<이름>.json` 으로 쓰고, APK 의 `assets/Shaders` 에 들어간다 (`ShaderCross::CompileEffectGles`).

- 데스크톱 GL 과 같은 바인딩 · 이름 규칙. 좌표: `-fvk-invert-y` (텍스처 행 0 = D3D 의 위), ES 에는 `glClipControl` 이 없어 깊이 0..1 → -1..1 을
  셰이더가 바꾼다 (`fixup_clipspace` — 저장되는 깊이 값은 D3D 와 같다)
- ES 변환 때만 `NOVA_GLES` 가 정의된다. ES 에 없는 밉 개수 조회(`GetDimensions(0, w, h, mips)` = `textureQueryLevels`)는 크기로 계산
  (`Shaders/32 · 41 · 49 · 55`, 전체 밉 사슬일 때 같은 값). DX11 · GL · Vulkan 은 그대로
- 476 pass 중 1 개 (옛 compute 예제) 만 변환 실패

## 검사 (MuMu 플레이어, 창 없이 터미널로)

```bash
powershell -File Tools/tests/android.ps1
```

1. MuMu 의 검사 전용 VM **"NOVA Test"** 를 켜고 창을 숨긴다 (`MuMuManager control … launch / hide_window`, 없으면 만든다 — 사용자의 다른 VM 은 건드리지 않는다)
2. 에디터로 GLES 셰이더를 내보내고 DX11 RHI 검사 그림을 기준으로 남긴다
3. `Android/build.py` → `adb install` → `am start -n com.nova.engine/android.app.NativeActivity -e test rhi -e size 960x540`
4. logcat 의 `NOVA_TEST {json}` → `adb pull` 로 그림 → 화소 비교. 끝나면 VM 을 끈다 (`-KeepEmulator` 로 켜 둠)

2026-10-04: **7/7** — `OpenGL ES 3.2 V132 (Adreno (TM) 640)`, 그리기 18 ~ 28 ms, DX11 과 차이 최대 1, 기기 쪽 셰이더 오류 0.

필요: Android SDK (build-tools 36 · platforms android-34 · cmake 3.22.1 · NDK 28), Java 17+ (Android Studio 의 jbr), MuMu 플레이어 12.

## 다음

- 플레이어 실행: 장면 불러오기 · 메인 루프 · 창 표면(EGL window) · 터치 입력 · 파일(assets) — Gfx 층(GfxGL) 의 ES 판
- 실제 휴대폰: arm64-v8a 빌드, Vulkan 1.1 대응
- C# 스크립트 런타임 (Mono 등)
- 빌드 창(Build Settings)에서 안드로이드 APK 만들기
