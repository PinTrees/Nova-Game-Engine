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
| `Android/Include/` | 엔진의 `#include "pch.h"` 를 받는 안드로이드 pch · `WinCompat.h` (Windows · D3D11 · DXGI 타입, COM 의 IUnknown · ComPtr · `__uuidof` 대체) · `d3d11.h` · `Windows.h` · `PathManager.h` 대체 |
| `Android/Source/AndroidMain.cpp` | 진입점 (`android_main`, NDK native_app_glue): `-e test` 이면 화면 없는 EGL (pbuffer) 검사 → 결과 BMP · JSON, logcat `NOVA_TEST {json}`. 없으면 플레이어 셸 |
| `Android/Source/AndroidPlatform.*` | EGL (컨텍스트는 앱 수명 동안, 창 표면은 생겼다 없어졌다 — 창이 없으면 pbuffer 에 묶어 GPU 자원 유지), 생명 주기 (pause · resume · 창 잃음), 터치 (여러 손가락), logcat `NOVA_EVENT {json}` |
| `Android/Source/GLESRhi.cpp` | RHI 의 OpenGL ES 3.2 구현 (데스크톱 GL 구현과 같은 규칙, DSA 없이 바인딩 방식) |
| `Android/Source/GfxGLES.*` | **Gfx 층** (엔진 렌더러가 쓰는 D3D11 모양 층) 의 OpenGL ES 3.2 구현 — 데스크톱 GfxGL 을 바인딩 방식으로. 텍스처 뷰가 없어 부분 뷰 (밉 · 조각 범위, 큐브 ↔ 배열, 다른 형식, 스텐실 읽기) 는 사본 텍스처를 원본이 바뀌었을 때만 새로 고침. base instance 는 인스턴스 버퍼 시작 위치로 |
| `Android/Source/GLESState.*` | D3D11 상태 · DXGI 형식 → GLES (GfxGLES · GLESRhi 공용). ES 에 없는 BGRA · 16 비트 UNORM 은 올릴 때 바꿈 |
| `Android/Include/DirectXTex/` · `Android/Source/DirectXTexLite.cpp` | DirectXTex 의 일부 (ScratchImage · 메타데이터 · 행 간격 · DDS 읽기) — Windows 의 DirectXTex 는 미리 빌드된 Windows 라이브러리라 |
| `Android/Source/Engine/` | **엔진 런타임**의 안드로이드 판: Windows 전용 파일 대신 (`PathManagerAndroid` — `/` 경로 · 앱 파일 폴더의 game/, `EngineAndroid` — EditorLog · MemoryStats · ShaderCache, `AndroidWin32` — `GetAsyncKeyState` · `GetCursorPos` 등 Win32 입력을 터치 · 키 상태로, UTF-8 변환, `ApplicationAndroid`), `EditorStubs` — 런타임이 부르는 에디터 함수 (Inspector · 선택 · Undo · 창) 의 빈 구현 · C# 스크립트 · 패키지 · Assimp 없음 |
| `Android/build.py` | Gradle 없이 APK: NDK CMake → aapt2 link (+ assets) → zipalign → apksigner (디버그 키) |
| `ThirdParty/DirectXMath/` | DirectXMath (MIT, Windows SDK 의 것) + `sal.h` 대체 — 안드로이드만 쓴다 |
| `Source/Build/AndroidTools.*` | 에디터 CLI `nova android shaders --out 폴더` |
| `Source/Graphics/Common/FxStates.*` | `.fx` 상태 블록 → D3D11 설명 (API 공용, 예전 GLState 안에 있던 것) |
| `Source/Graphics/ShaderCross/ShaderCrossJson.*` | 셰이더 변환 결과 ↔ JSON (PC 캐시 · 안드로이드 셰이더 묶음 공용) |

### 엔진 빌드

`Android/CMakeLists.txt` 의 `nova_engine` = Windows 와 **같은 엔진 소스** (Core · Math · Scene · Graphics · Animation · Effects · UI · Water · Terrain ·
Physics (Jolt) · Physics2D (box2d) · Audio · ShaderGraph) 를 고치지 않고 그대로 빌드한다. 엔진 파일은 바꾸지 않고:

- `Android/Include/pch.h` 가 Windows pch 와 같은 목록, Windows 헤더 (`windows.h` · `d3d11.h` · `wrl.h` · `xaudio2.h` · DirectXTex …) 는 대체 헤더
  (`WinCompat.h` — 타입 · Win32 함수의 안드로이드 판, MSVC 처럼 `min` · `max` 매크로, `std::execution::par` 은 차례로)
- Windows 전용 파일 6 개는 빼고 `Android/Source/Engine` 의 안드로이드 판, 런타임이 부르는 에디터 함수는 빈 구현
- `--whole-archive` 로 모두 링크 (컴포넌트가 정적 초기화로 스스로 등록). 오디오는 XAudio2 가 없어 소리 없이 돈다 (AudioManager 의 규칙)

### 엔진 플레이어

PC 플레이어와 **같은 렌더 경로** (`EditorApp` 의 게임 뷰 그리기 + `PlayerRuntime::Render`) 를 그대로 쓴다.

| 위치 | 하는 일 |
|---|---|
| `Android/Source/Engine/AppAndroid.cpp` | `App` 의 안드로이드 판: GfxGLES 장치 (지금 EGL 컨텍스트), Windows 판과 같은 초기화 순서, `Run()` = **한 프레임** (안드로이드 루프가 프레임마다 부름), ImGui 는 입력용 (터치 = 마우스) |
| `Android/Source/Engine/PlayerRuntimeAndroid.cpp` | 게임 데이터 = 앱 파일 폴더의 `game/` (`player.json`), 작업 폴더 = `game/Binaries` (Windows 플레이어의 `_Data` 와 같은 배치) |
| `Source/Build/AndroidTools.cpp` | 에디터 CLI `nova android export --out Android/build/assets --scenes A.scene,…` — 플레이어 빌드와 같은 에셋 모음 (`BuildPipeline::CollectGameFiles`), JSON 안의 경로 `\` → `/`, `player.json` · `files.txt`. `nova android reference --out x.png --width --height --frames` — 열린 씬을 플레이어 순서로 그린 DX11 기준 그림 |
| `Android/Source/AndroidMain.cpp` | APK 의 `assets/game` → 앱 파일 폴더 (`files.txt` 가 바뀌었을 때만), 게임 데이터가 있으면 엔진 플레이어 (`-e mode shell` 이면 셸), `-e test scene` = 화면 없이 첫 씬을 N 프레임 그려 BMP |

아직: 텍스처 (PNG · JPG) 와 모델 (FBX) 을 기기에서 읽지 못한다 (Windows 전용 DirectXTex · Assimp) → PC 가 미리 구운 캐시를 넣는 것이 다음.
C# 스크립트 · 패키지 DLL · 소리 (XAudio2) 없음.

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

5. 엔진 장면: 에디터가 `android export` 로 게임 데이터 → APK, `android reference` 로 DX11 기준 → 기기에서 `-e test scene` (화면 없이)
   와 그냥 실행 (앱 창) 을 같은 크기로 비교
6. 플레이어 셸 (`-e mode shell`): 창 표면 → 화면 캡처 (`screencap`) 로 배경색 · 프레임마다 움직이는 막대, `input tap` → 터치 위치에 주황 표시,
   HOME → 다시 열기 (창 잃음 · 다시 생김, 상태 유지). MuMu 가 켜진 직후 띄우는 광고 창이 앞에 있으면 뒤로 가기로 닫는다.
   회전은 검사하지 않는다 (MuMu 태블릿 모드는 `user_rotation` · `wm size` 로 앱 창 크기를 바꾸지 않음)

2026-10-04: 1 단계 **7/7** — `OpenGL ES 3.2 V132 (Adreno (TM) 640)`, 그리기 18 ~ 28 ms, DX11 과 차이 최대 1, 기기 쪽 셰이더 오류 0.
2 단계 첫 조각 (플레이어 셸) 포함 **15/15**. Gfx 층 GLES 구현 뒤 **17/17** — Gfx 층 검사 장면 (그림자 맵 R24G8 배열 · 비교 샘플러 · 큐브맵 · 밉) 도 DX11 과 차이 최대 1.
엔진 플레이어 뒤 **24/24** — `Shadows.scene` (기본 메시 32 개 · PBR 재질 · 4 단 그림자 · 하늘 · Volume 후처리) 을 엔진 전체로:
화면 없는 검사 (960x540) 와 앱 창 (1600x900, 60 fps) 모두 PC DX11 기준과 차이 최대 1.

필요: Android SDK (build-tools 36 · platforms android-34 · cmake 3.22.1 · NDK 28), Java 17+, MuMu 플레이어 12.
SDK · JDK 는 NOVA Hub 의 **Android 빌드 지원** 모듈이 `%LOCALAPPDATA%\NOVA\AndroidTools` 에 설치한다 ([NOVA_HUB.md](NOVA_HUB.md)).
`build.py` 는 `ANDROID_HOME` · `JAVA_HOME` → Hub 의 AndroidTools → Android Studio 기본 위치 (`%LOCALAPPDATA%\Android\Sdk`, jbr) 순서로 찾는다.

## 다음

- 2 단계 (진행 중): 창 표면 · 메인 루프 · 생명 주기 · 터치 **완료** → Gfx 층의 GLES 구현 **완료** →
  엔진 코어 (씬 · 컴포넌트 · 렌더러) 를 NDK 로, 에셋은 PC 에서 미리 굽기 (텍스처 · 메시 캐시) → 씬 하나를 PC 플레이어와 같은 그림으로 → 터치를 Input 에
- 실제 휴대폰 (arm64 · Vulkan) 은 한참 뒤 (사용자 결정)
- C# 스크립트 런타임 (Mono 등)
- 빌드 창(Build Settings)에서 안드로이드 APK 만들기
