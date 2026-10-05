# 안드로이드

NOVA 를 안드로이드에서 돌리는 작업 (지금은 MuMu 플레이어 — x86_64 에뮬레이터). 엔진 전체를 NDK 로 빌드해 OpenGL ES 3.2 로 PC 플레이어와 같은 그림
(DirectX 11 과 화소 차이 최대 1), 텍스처 압축 (ASTC · ETC2) · 모델 메시 캐시 · 패키지 (Animator · Toon) · 터치 → UI · Input · 소리 (AAudio) · C# 스크립트 (Mono),
그리고 에디터의 **Build Settings → Android → Build And Run** 으로 APK 를 만들어 설치 · 실행한다.

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
| `Source/Build/AndroidTools.*` · `AndroidBuild.*` | 에디터 CLI `nova android shaders · export · reference · build`, Build Settings 의 Android 빌드 (APK) |
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

### C# 스크립트 (Mono)

PC 는 .NET (hostfxr) 을 띄우지만 기기에서는 Microsoft 의 **Mono** (.NET 8 의 모바일 런타임, MIT — NuGet `Microsoft.NETCore.App.Runtime.Mono.android-x64` 8.0.31) 를 쓴다.
`Android/Source/Engine/ScriptEngineAndroid.cpp` 가 Windows 의 ScriptEngine 과 같은 일을 한다 (빌드된 게임처럼 Assembly-CSharp.dll 을 읽기만).

- `libmonosgen-2.0.so` 는 `dlopen` (APK 에 없으면 스크립트 없이 돈다), 헤더 없이 dlsym 으로 임베딩 함수만
- `monovm_initialize_preparsed`: 믿을 수 있는 어셈블리 (TPA) = 게임 데이터의 `Managed/`, 문화권 데이터 없이 (`System.Globalization.Invariant`),
  **PINVOKE_OVERRIDE** — 패키지 C# 의 `DllImport("NovaAnimation")` 등을 libnova.so 안의 함수로 (패키지를 엔진에 함께 넣었으므로)
- 진입점은 PC 와 같은 `NovaEngine.Interop.Bridge` 의 `[UnmanagedCallersOnly]` 함수 (`mono_method_get_unmanaged_callers_only_ftnptr`), 엔진 API 표도 같다 (`ScriptBindings.cpp` 를 안드로이드도 빌드)
- `nova android export` 는 프로젝트에 `Library/ScriptAssemblies/Assembly-CSharp.dll` 이 있을 때만 `Managed/` 에: System.Private.CoreLib · NovaScriptCore · Assembly-CSharp 와
  **그것들이 닿는 BCL 만** (메타데이터의 AssemblyRef 표를 따라가며 — 모두 넣으면 21.9 MB, 검사 씬은 26 개 6.9 MB)
- APK 의 `lib/x86_64` 에 Mono 의 네이티브 라이브러리 (libmonosgen-2.0 · System.Native · 압축 · 암호 · marshal-ilgen — 디버거 · 진단 · 핫 리로드는 뺀다)
- 런타임 받기: `powershell -File Tools/fetch_android_mono.ps1` → `ThirdParty/MonoAndroid` (git 에 넣지 않음, nuget.org 의 SHA512 확인). 엔진 배포판은
  `Tools/package_release.ps1` 가 `Android/Player/<ABI>/mono/{lib,native}` 에 넣고, 에디터는 그 자리 → `ThirdParty/MonoAndroid` 순서로 찾는다
- MuMu: 런타임 시작 30 ~ 45 ms, Assembly-CSharp 읽기 약 140 ms (JIT)
- 런타임 자리 (에디터가 찾는 순서): 엔진 배포판 `Android/Player/<ABI>/mono` → NOVA Hub 의 **Android 빌드 지원** (`<AndroidTools>/mono/<ABI>` — Hub 가 nuget.org 에서 받고 SHA512 확인)
  → `ThirdParty/MonoAndroid` (엔진 개발). Build Settings 의 Android 칸에 찾은 위치가 보인다 (없으면 "C# scripts will not run on Android")

### C# 의 플랫폼 API (Unity 와 같은 이름)

엔진이 이름으로 내보낸 함수 (`Source/Scripting/PlatformBindings.cpp`) 를 C# 이 `DllImport("NovaCore")` 로 부른다 (안드로이드는 PINVOKE_OVERRIDE 로 libnova.so).

| C# | 안드로이드 | PC |
|---|---|---|
| `Input.touchCount` · `GetTouch(i)` · `touches` (`Touch` — fingerId · position · deltaPosition · phase · tapCount) | 손가락 (좌표는 Unity 처럼 왼쪽 아래 기준) | 0 개 (Unity 와 같음) |
| `Application.platform` · `isMobilePlatform` | `RuntimePlatform.Android` (11) | WindowsEditor (7) · WindowsPlayer (2) |
| `Screen.safeArea` | DisplayCutout (API 28+, JNI) 의 안전 영역 | 화면 전체 |
| `Screen.orientation` (get / set) | 지금 방향, 넣으면 `Activity.setRequestedOrientation` | 화면 비율 |
| `OnApplicationPause(bool)` · `OnApplicationFocus(bool)` | 앱이 뒤로 / 앞으로 (C# `AppEvents`) | 빌드된 게임의 창 활성 (Run In Background 를 끄면 Pause 도) |

앱이 뒤에 있던 동안은 게임 시간이 흐르지 않는다 (돌아온 첫 프레임의 deltaTime 이 튀지 않게 타이머를 새로).

### 스토어 배포: 서명 키 · 아이콘 · 버전 코드 · App Bundle (Unity 의 Publishing Settings · Build App Bundle)

| 설정 | 위치 | 하는 일 |
|---|---|---|
| Bundle Version Code | Player Settings → Android | manifest `versionCode` (스토어에 올릴 때마다 올린다) |
| Icon | Player Settings → Android (Project 창의 그림을 끌어 놓기, 오른쪽 클릭 = 비우기) | 48 · 72 · 96 · 144 · 192 px 로 줄여 `res/mipmap-<밀도>/ic_launcher.png` → `aapt2 compile` → `android:icon`. 비면 NOVA 로고 |
| Custom Keystore · Keystore · Alias · 비밀번호 | Player Settings → Android → **Publishing Settings** | 배포 키로 서명. 비밀번호는 Unity 와 같이 저장하지 않는다 (에디터를 켤 때마다 넣는다). 끄면 디버그 키 |
| Create New Keystore | 같은 곳 | `keytool -genkeypair` (PKCS12, RSA 2048, 25 년, CN = 회사 이름) — 키와 비밀번호를 잃으면 Google Play 에 업데이트를 올릴 수 없다 |
| Build App Bundle (Google Play) | Build Settings → Android | `.aab` 를 만든다. Build And Run 은 같은 내용의 `.apk` 를 설치해 실행 (Unity 는 bundletool 로 같은 일) |

- 서명 비밀번호는 명령줄 대신 환경 변수로 도구에 넘긴다 (`apksigner --ks-pass env:` · `jarsigner -storepass:env` · `keytool -storepass:env`) — 프로세스 목록에 보이지 않게
- **AAB 만들기 (Gradle · bundletool 없이)**: `aapt2 link --proto-format` (같은 manifest · assets · 아이콘) → 엔진이 zip 항목을 압축된 그대로 옮겨
  `base/manifest/AndroidManifest.xml` · `base/resources.pb` · `base/res/…` · `base/assets/…` · `base/lib/<ABI>/…` + `BundleConfig.pb` → `jarsigner`
- 검사는 Google 의 **bundletool** (`Tools/fetch_bundletool.ps1` → `ThirdParty/bundletool`, git 밖, GitHub 의 SHA-256 확인): `validate` · `build-apks` (+ MuMu 에 `install-apks` 해서 엔진 · Mono 가 도는 것을 확인)
- CLI: `nova android keystore-create --path x.keystore --pass … --alias …`, `nova android build … --app-bundle --keystore … --keystore-pass … --alias … --key-pass …`

### Player Settings → Android

Texture Compression · **Package Name** · **Default Orientation** · **Bundle Version Code** · **Icon** · **Publishing Settings** (Portrait · Portrait Upside Down · Landscape Right · Landscape Left · Auto Rotation —
manifest 의 `screenOrientation` = portrait · reversePortrait · reverseLandscape · landscape · fullUser)

### 모델 · 캐릭터 (메시 캐시)

기기에는 Assimp 가 없다. `nova android export` 가 모델 (fbx · gltf · glb · vrm) 마다 에디터와 같은 길 (`ResourceManager::LoadMeshFile` — 캐시가 없거나
가져오기 설정이 바뀌었으면 여기서 가져온다) 로 **메시 캐시** (`.mesh` · `.animations` · `.skeletons`) 를 만들어 그것만 넣는다 (원본 모델은 빼서 용량도 줄임).
기기의 `MeshFile::LoadFromMetaFile` 은 원본이 없으면 시각 비교를 건너뛰고 머리의 가져오기 설정 해시만 본다.

- 해시는 **FNV-1a 64** 로 고정 (`std::hash` 는 MSVC · libc++ 가 다르다). MSVC 의 `std::hash<std::string>` 과 같은 값이라 PC 에 있던 캐시도 그대로 맞는다
- 캐시 안의 값은 모두 `size_t` · `XMFLOAT*` · `uint32` (x64 와 x86_64 · arm64 가 같은 크기 · 정렬)

### 패키지 (Animator · Toon …)

Windows 는 공식 패키지 (`Packages/<이름>/Source`) 를 DLL 로 불러오지만 안드로이드는 **엔진에 함께 넣는다** (`Android/CMakeLists.txt` 가 패키지마다 정적 라이브러리,
같은 이름인 진입점 `NovaPackage_OnLoad` 는 패키지 이름을 붙여 바꾸고, 만든 `nova_packages.cpp` 가 `App::Init` 에서 씬보다 먼저 차례로 부른다).
패키지의 셰이더 (`Packages/*/Shaders/*.fx`, 예: Toon 의 lilToon) 도 `nova android shaders` 가 GLES 로 바꾼다 (기기는 이름으로 찾는다).
Windows 전용 호출 (파일 대화 상자 · 모듈 경로 · PNG 저장) 은 `Android/Include` 의 대체 (`commdlg.h`, `GetModuleHandleExW`, `SaveToWICFile` = 늘 실패) 로 컴파일만.

### 컴포넌트 등록

엔진 컴포넌트는 헤더의 `REGISTER_COMPONENT` (inline 정적 변수) 로 스스로 등록한다. 안드로이드도 `NOVA_ENGINE_BUILD` 를 켜고 (Windows 의 NovaCore.dll 과 같게),
clang 이 쓰지 않는 inline 변수를 지우지 않게 `__attribute__((used))` (`define.h` 의 `NOVA_KEEP_REGISTRATION`). 전에는 ComponentFactory 의 기본 29 개만 있어
UI · 오디오 · 물리 … 컴포넌트가 씬에서 빠졌다 (지금 83 개).

### 터치 · UI · Input

- 첫 손가락 = 마우스 왼쪽 (`GetAsyncKeyState(VK_LBUTTON)` · `GetCursorPos`) — UI (Button · Toggle · Slider · ScrollRect …) 와 `Input.GetMouseButton` 이 그대로 받는다
- 한 프레임보다 짧은 탭도 잃지 않게: 손가락 이벤트를 큐에 모아 ImGui 입력 큐로 (UI 는 '눌림 → 뗌' 을 다음 프레임들에 차례로), `Input` 의 버튼은 프레임마다 고정
- Unity 의 `Input.touchCount` · `Input.GetTouch` (`Touch` — fingerId · position · deltaPosition · phase Began / Moved / Stationary / Ended / Canceled):
  네이티브 `Input::TouchCount()` · `Input::GetTouch(i)` (`InputManager` 가 프레임마다 받는다, PC 는 Unity 처럼 0). C# 쪽은 C# 런타임과 함께
- 게임 화면 좌표는 Windows 의 Game 뷰와 같이 왼쪽 아래 (0,0) — UI 의 맞히기 · 끌기가 같은 식
- 기본 글꼴 (Pretendard) 경로를 `fs::path` 로 (전에는 `\` 로 이어 안드로이드에서 글자가 안 보였다)

### 소리 (XAudio2 → AAudio)

엔진의 소리 코드 (AudioManager · AudioSource · AudioMixer) 는 XAudio2 로 쓰여 있다. 그래픽의 GfxGLES 처럼 **XAudio2 의 안드로이드 판**
(`Android/Source/Engine/XAudio2Android.cpp`) 을 두어 엔진 코드는 그대로:

- 소프트웨어 믹서: 소스 보이스 (PCM 8 · 16 · 24 · 32 · float, 버퍼 대기열 · 반복 구간 · END_OF_STREAM, 재생 속도 · 샘플 레이트는 선형 보간,
  출력 행렬 = 팬 · 3D), 서브믹스 (믹서 그룹 — 처리 순서, XAudio2 의 상태 변수 필터, 레벨 측정기 · 에코 · 리버브 (단순 Schroeder)), 마스터 (2 채널)
- 출력: AAudio (float, 저지연, 장치 레이트 — MuMu 는 48000 Hz · 512 프레임 버스트). 앱이 뒤로 가면 스트림을 멈추고 돌아오면 다시
- 리버브 (`Android/Source/Engine/AudioReverb.h`, 플랫폼 코드 없음): I3DL2 프리셋 (Windows SDK 와 같은 값) 을 따른다 — 앞 지연 (ReflectionsDelay) 뒤
  초기 반사 6 탭, 그 뒤 (ReverbDelay) 늦은 잔향 = 채널마다 빗살 8 개 (60 dB 감쇠 = DecayTime, 고역 감쇠 = DecayHFRatio) + 전역 통과 4 개 (Diffusion),
  방 필터 (Room / RoomHF @ HFReference), WetDryMix. PC 검사 (`Tools/tests/android_reverb_test.cpp`): 임펄스 응답의 RT60 이 DecayTime 과 맞는다
  (Bathroom 1.49 → 1.49 s, Concert Hall 3.92 → 3.88 s, Hangar 10.05 → 9.99 s)
- mp3 · ogg · wav 디코더는 PC 와 같은 코드 (스트리밍 스레드 포함)

### Build Settings → Android (Unity 의 Build / Build And Run)

`Source/Build/AndroidBuild.*`: Build Settings 창에서 **Android** 를 고르고 Build (폴더를 묻고 `<제품>.apk`) / Build And Run.

1. 에디터 (프레임마다 한 단계, 진행 창): 셰이더 → GLSL ES, 게임 데이터 (위의 텍스처 굽기 · 메시 캐시) → `<프로젝트>/Library/AndroidBuild/assets`
2. 작업 스레드: AndroidManifest (Player Settings 의 **Package Name** — 비면 `com.<회사>.<제품>`, 제품 이름, 버전) → `aapt2 link -A assets` →
   플레이어 라이브러리 `libnova.so` (+ Mono) 를 zip 에 직접 — 엔진의 DEFLATE (LZ77 + 고정 허프만, zlib 없이 · zlib 의 약 1.15 배 크기), 항목 이름의 `\` 는 `/` 로 → `zipalign` → `apksigner` (디버그 키, 없으면 `keytool` 로 만든다)
3. Build And Run: `adb devices` (없으면 켜진 MuMu VM 의 adb 포트로 `adb connect` — `MuMuManager info`) → `install -r` → `am start`

- 창: Texture Compression, **Run Device** (Default device · 연결된 장치, Refresh), Development Build, Package Name, Android SDK · JDK 위치 (없으면 Hub 안내)
- 도구: `ANDROID_HOME` · `JAVA_HOME` → Hub 의 AndroidTools (엔진 옆 / `%LOCALAPPDATA%\NOVA\AndroidTools`) → Android Studio 기본 위치
- 플레이어 라이브러리: `<엔진>/Android/Player/<ABI>/libnova.so` (배포판 — `Tools/package_release.ps1` 가 넣는다) → `<엔진>/Android/build/cmake/<ABI>-Release/libnova.so` (엔진 개발)
- CLI: `nova android build --out x.apk [--run] [--device 시리얼] [--texture-compression …]`, `nova android build-status`
- 지금은 x86_64 (MuMu) 만

### 텍스처 압축 (Unity 의 Android Texture Compression)

기기에는 그림 디코더 · 압축기가 없다. `nova android export` 가 그림 (png · jpg · bmp · tga · tif · gif) 을 **가져오기 설정대로 구워**
`<이름>.png.dds` 로 넣고, 기기의 `LoadFromWICFile` 은 `경로 + ".dds"` 를 읽어 압축된 그대로 GPU 에 올린다.

| 설정 | 위치 | 값 |
|---|---|---|
| 프로젝트 기본 | Project Settings → Player → Android → **Texture Compression** (`androidTextureCompression`) | **ASTC** (기본, Unity 와 같음) · ETC2 · DXT (BC, 에뮬레이터용) · None (RGBA32) |
| 텍스처마다 | 가져오기 설정 → **Override for Android** (`.meta` 의 `"android"`) | Max Size, Format = Automatic · ASTC 4x4 ~ 12x12 · ETC2 · RGBA32 |
| 내보내기 | `nova android export … --texture-compression astc\|etc2\|dxt\|none` | 프로젝트 기본을 이번만 바꿈 |

- Automatic: 프로젝트 기본 형식. ASTC 는 블록 6x6 (High Quality 압축이면 4x4), 압축 None 이면 RGBA32. 크기 · 밉 · sRGB · 선형은 PC 와 같은 규칙
- ASTC: ARM **astc-encoder 5.7.0** (`ThirdParty/astcenc`, Apache-2.0, 정적 라이브러리, 여러 스레드). ETC2: 엔진 자체 인코더 (`Source/Build/Etc2Codec.*` —
  ETC1 개별 · 차분 + ETC2 평면 모드, 알파는 EAC). 색 그림은 눈 가중치 (초록), 노멀맵 · 선형은 성분 똑같이
- DDS: DX10 머리. ASTC 는 옛 `DXGI_FORMAT_ASTC_*` 값 (133 + 4·블록), ETC2 는 엔진 값 240 ~ 243 (`Source/Graphics/Common/MobileTextureFormats.h`)
- 내보내기 결과의 `textures[]` 에 형식 · 크기 · 밉 · 바이트 · **PSNR** (밉 0, 원본 대비). 가져오기 설정 창의 Android 칸에 결과 형식이 보인다
- 예 (ScriptTest `Materials.scene`): Checker — ASTC 6x6 55.4 dB, ETC2 55.9 dB, BC1 41.1 dB. Bumps_Normal (노멀맵) — ASTC 6x6 34.0, ETC2 26.2 (Texture Type 이 Normal Map 일 때, Default 면 25.7), BC1 30.2.
  ETC2 는 블록 안 색 변화가 큰 노멀맵에 약하다 (밝기만 바꾸는 방식) — 그래서 Unity 처럼 ASTC 가 기본. T · H 모드는 아직 안 씀
- `files.txt` 첫 줄은 내보낸 시각 (`# export …`) — 목록이 같아도 (형식만 바꿈) 기기가 다시 푼다

### 셰이더

휴대폰에는 셰이더 변환기(DXC)를 넣지 않는다. PC 에서 `nova android shaders --out Android/build/assets/Shaders` 가 모든 `.fx` 를
GLSL ES 3.20 으로 바꿔 `<이름>.json` 으로 쓰고, APK 의 `assets/Shaders` 에 들어간다 (`ShaderCross::CompileEffectGles`).

- 데스크톱 GL 과 같은 바인딩 · 이름 규칙. 좌표: `-fvk-invert-y` (텍스처 행 0 = D3D 의 위), ES 에는 `glClipControl` 이 없어 깊이 0..1 → -1..1 을
  셰이더가 바꾼다 (`fixup_clipspace` — 저장되는 깊이 값은 D3D 와 같다)
- ES 변환 때만 `NOVA_GLES` 가 정의된다. ES 에 없는 밉 개수 조회(`GetDimensions(0, w, h, mips)` = `textureQueryLevels`)는 크기로 계산
  (`Shaders/32 · 41 · 49 · 55`, 전체 밉 사슬일 때 같은 값). DX11 · GL · Vulkan 은 그대로
- 476 pass 중 1 개 (옛 compute 예제) 만 변환 실패
- 기기는 효과를 읽을 때 GLSL 을 컴파일하지 않고 **pass 를 처음 쓸 때** 컴파일 · 링크한다 (안 쓰는 technique 은 만들지 않는다). 그림자 샘플러 종류는 uniform 선언을 한 번 훑어 찾는다
  (예전에는 샘플러 × pass 마다 std::regex — lilToon · InstancedBasic 에서 각 4 초). 엔진 시작 (MuMu) 약 10 초 → **0.3 초**
- 드라이버가 프로그램 바이너리를 주면 (`GL_NUM_PROGRAM_BINARY_FORMATS` > 0) 앱 파일 폴더의 `glcache/` 에 저장해 다음 실행부터 컴파일 없이 (MuMu 는 0 — 쓰지 않음)

### 그리기 CPU (GL 상태 · 바인딩 기억)

에뮬레이터 (MuMu) 는 GL 호출마다 번역 비용이 들고, 휴대폰 드라이버도 호출 수에 비례해 CPU 를 쓴다. 예전에는 효과 `Apply` 가
프로그램 · 상수 블록 · **텍스처 유닛 전부** (셰이더 32 는 수십 개) · 샘플러 · SSBO 를 매번 다시 묶었고, 상태 (블렌드 한 번 = GL 호출 약 30 개) 도 매번 다시 썼다.

- `GLESState` 가 마지막에 GL 에 넣은 값을 기억해 같으면 부르지 않는다: 프로그램, 상수 블록 바인딩, 유닛마다 텍스처 · 샘플러, SSBO · image,
  래스터 · 블렌드 · 깊이 상태, VAO (와 VAO 안의 정점 · 인덱스 버퍼 — `GLLayout::Bound`). 상수 블록은 **바뀐 범위만** 올린다
- 기억 밖에서 GL 을 바꾼 곳은 알린다: 지우기 (쓰기 마스크 · 가위) · Present · `RestoreState` → `InvalidateStates`, 텍스처 만들기 · 임시 유닛 → `InvalidateBindings` · `ForgetUnit`,
  GL 객체를 지우면 (이름이 다시 쓰인다) `Deleted` (모든 기억을 버리고 VAO 기억의 Epoch 를 올린다)

도시 장면 (`Tools/tests/android_city_perf.ps1 -Profile`, 1280x720, 렌더러 2128) 의 프레임마다 GL 호출:

| | 전 | 후 |
|---|---|---|
| glUseProgram | 205 | 37 |
| 상수 블록 바인딩 · 올리기 | 1240 · 87 KB | 33 · 31 KB |
| 텍스처 · 샘플러 바인딩 | 3528 · 3528 | 79 · 48 |
| 정점 · 인덱스 버퍼 | 554 | 95 |
| 상태 적용 | 104 | 49 |

프레임 (MuMu, 중앙값): 오클루전 켬 11.6 → 11.0 ms, 끔 13.9 → 11.6 ms. MuMu 는 실행마다 차이가 크다 (같은 APK 로 10 ~ 19 ms) — 여러 번 재 중앙값으로 본다.
`-Profile` 의 구간 (켬, 9 ~ 10 ms): 그리기 (GameView render) 약 4 ms, **물리 갱신 약 3.3 ms** (정적 콜라이더 2200 개의 동기화를 고정 스텝마다 처음부터 —
GL 과 상관없는 엔진 CPU), 카메라 · 빛 1.1 ms, 컬링 갱신 0.9 ms.

물리 동기화 (`PhysicsManager::StepSimulation` 2): 오브젝트마다 컴포넌트를 **한 번만** 훑어 콜라이더 · Rigidbody · Character Controller · Joint 를 함께 모으고
(예전: 동기화 · 캐릭터 · Joint 가 각자 모든 오브젝트를 다시 훑음), 소유자 · id 맵을 스텝마다 새로 만들지 않고 버퍼를 다시 쓴다. 콜라이더 표는 지우지 않고
그 자리에서 고치며 (보지 못한 항목만 뺀다), 콜라이더 종류 (Box · Sphere …) 는 한 번 판정해 표에 기억한다 (서명 계산의 `dynamic_cast` 를 없앰).
PC Release 도시 (Play): 동기화가 2D 물리 (그대로인 코드) 대비 3.7 배 → 1.7 배 — 약 2 배 빨라짐, 캐릭터 · Joint 훑기 (0.29 ms) 는 없어짐.
이어서 (2026-10-05): **오브젝트마다 컴포넌트 분류를 기억** 한다 (`Source/Scene/ComponentIndex.*` — 오브젝트 InstanceID + 컴포넌트 InstanceID 목록이 같으면
dynamic_cast 없이, InstanceID 는 다시 쓰이지 않아 지운 주소의 재사용에도 안전). 3D 동기화 · 2D 동기화 · 2D Joint 가 쓴다. 또 **활성 카메라 찾기** 가 부를 때마다
씬 전체를 훑던 것 (`DisplayManager::GetCameraForDisplay` — 그림자 · UI · 소리 · Camera.main 이 프레임마다 여러 번) 을 카메라가 스스로 등록한 목록에서 고르게 했다
(`Camera::All`, 규칙 그대로 — 지워진 오브젝트의 카메라는 `GameObject::IsAlive` 로 뺀다).
PC Release 도시 (Play, 같은 조건 A/B): 프레임 7.95 → 6.6 ms, 카메라 · 빛 1.01 → 0.08 ms 아래, 물리 동기화 0.64 → 0.46 ms, 2D 물리 0.36 → 0.08 ms (Showcase 212).
남은 동기화 (소유자마다 서명 · 표 갱신) 를 없애려면 Transform · 콜라이더 · Rigidbody 값에 변경 번호가 필요하다 (setter · Inspector · 불러오기 · Undo).

- 진단: `-e profile on` → 결과 (`result_scene.json`) 의 `gl` (GL 호출 종류별 수, 늘 센다 — `GlesCounters`) · `scopes` (Profiler 구간, 깊이 3 까지 프레임마다 평균).
  logcat 의 `NOVA_TEST` 줄은 1024 자에서 잘려 검사 스크립트는 파일을 받는다. `-SkipBuild` = 에디터의 장면 단계를 건너뛰고 APK 만 다시

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
7. 텍스처 압축: `-TextureScene` (기본 `Materials.scene`) 을 ASTC · ETC2 로 구워 형식마다 APK → `-e test scene` → DX11 기준 (PC 는 BC) 과 비교.
   압축 형식 차이만큼은 허용 (평균 < 2, 8 넘는 화소 < 5 %). APK 의 게임 데이터는 마지막 형식으로 남는다
8. 모델: 기본 캐릭터 (FBX · Animator) + VRM 캐릭터 (lilToon) 씬 → 메시 캐시만 넣은 APK → DX11 기준과 비교 (기기는 Idle 이 움직이므로 평균 < 3),
   패키지가 엔진에 들어갔는지 (logcat `[Packages]`), 10 · 90 프레임 그림이 다른지 (Animator 가 돈다)
9. 터치 · 소리: Toggle · Slider · AudioSource 씬 → `input tap` 으로 Toggle, `input swipe` 로 Slider 손잡이 → 화면 영역이 바뀌는지, 엔진 Input 의 Touch (logcat `[Input] touch`),
   frame 이벤트의 `audioFrames` 가 늘고 레벨 > 0, `dumpsys media.audio_flinger` 의 재생 중 트랙 → HOME 이면 줄어든다
10. C# 스크립트: 검사 스크립트 (LINQ · Dictionary · Transform · Time) 를 붙인 씬 → Mono 런타임 · BCL 이 들어갔는지, 기기 logcat 에 Start · Update 의 Debug.Log,
    Application.platform · Screen.safeArea · Screen.orientation 요청, `input tap` → C# Input.GetTouch (왼쪽 아래 기준 좌표), HOME → OnApplicationPause · Focus 와 돌아온 뒤 deltaTime
11. 엔진 시작 시간 (loadMs < 3 초), 리버브 임펄스 응답 (PC)
12. 배포: 검사용 키 (무작위 비밀번호) 를 `keystore-create` 로 만들고 `--app-bundle` 로 빌드 → APK 서명 (apksigner 의 DN) · AAB 서명 (jarsigner) · 아이콘 · versionCode (aapt2 badging) ·
    bundletool `validate` · `build-apks`
13. 에디터의 Build And Run: `nova android build --run` → 다른 패키지 이름 (`com.<회사>.<제품>`) 의 APK 가 설치 · 실행되어 엔진이 시작하는지, Build Settings 창 (Android) 캡처

2026-10-04: 1 단계 **7/7** — `OpenGL ES 3.2 V132 (Adreno (TM) 640)`, 그리기 18 ~ 28 ms, DX11 과 차이 최대 1, 기기 쪽 셰이더 오류 0.
2 단계 첫 조각 (플레이어 셸) 포함 **15/15**. Gfx 층 GLES 구현 뒤 **17/17** — Gfx 층 검사 장면 (그림자 맵 R24G8 배열 · 비교 샘플러 · 큐브맵 · 밉) 도 DX11 과 차이 최대 1.
엔진 플레이어 뒤 **24/24** — `Shadows.scene` (기본 메시 32 개 · PBR 재질 · 4 단 그림자 · 하늘 · Volume 후처리) 을 엔진 전체로:
화면 없는 검사 (960x540) 와 앱 창 (1600x900, 60 fps) 모두 PC DX11 기준과 차이 최대 1.

필요: Android SDK (build-tools 36 · platforms android-34 · cmake 3.22.1 · NDK 28), Java 17+, MuMu 플레이어 12.
SDK · JDK 는 NOVA Hub 의 **Android 빌드 지원** 모듈이 `%LOCALAPPDATA%\NOVA\AndroidTools` 에 설치한다 ([NOVA_HUB.md](NOVA_HUB.md)).
`build.py` 는 `ANDROID_HOME` · `JAVA_HOME` → Hub 의 AndroidTools → Android Studio 기본 위치 (`%LOCALAPPDATA%\Android\Sdk`, jbr) 순서로 찾는다.

## 다음

- 2 단계: 창 표면 · 메인 루프 · 생명 주기 · 터치 → Gfx 층의 GLES 구현 → 엔진 런타임 · 엔진 플레이어 → 텍스처 압축 (ASTC · ETC2) →
  모델 메시 캐시 · 패키지 → Build Settings 의 APK 빌드 → 터치 → UI · Input → 소리 (AAudio) **모두 완료**
- C# 스크립트 (Mono) · 엔진 시작 시간 (약 10 초 → 0.3 초) · 리버브 (I3DL2) · C# 터치 · 플랫폼 API · 앱 일시 정지 · 화면 방향 · 안전 영역 · APK 압축 · Hub 의 Mono **완료**
- 서명 키 (Custom Keystore) · 앱 아이콘 · Bundle Version Code · App Bundle (.aab) **완료**
- 다음 후보: arm64 (실제 휴대폰 — 사용자 결정 뒤 — Play 는 arm64 를 요구한다), Play Asset Delivery (base 모듈이 200 MB 를 넘는 큰 게임)
- 실제 휴대폰 (arm64 · Vulkan) 은 한참 뒤 (사용자 결정)
