# NOVA Game Engine – 작업 인수인계 명세 (AI 에이전트용)

> 이 문서는 다음 작업을 이어받는 AI 에이전트를 위한 **현재 상태 + 규칙 + 우선순위 작업 목록**입니다.
> 과거 배경/원인 분석은 `PROJECT_HANDOVER.md`(1~5장은 이전 폴더 구조 기준의 기록, 6장이 최신 구조)를 참고하세요.

---

## 0. 한눈에 보기

| 항목 | 값 |
|---|---|
| 프로젝트 | **NOVA Game Engine** – Unity를 본뜬 학습용 C++ 3D 게임 엔진 + 에디터 |
| 목표 | Unity 에디터(6.x)와 **폰트/아이콘/창 디자인/동작까지 1:1에 가깝게** 재현 |
| 언어/도구 | C++20, DirectX 11, HLSL(FX11 이펙트), ImGui(docking) + Font Awesome, Assimp, DirectXTex, nlohmann/json |
| 빌드 | CMake(VS 2022 생성기) → `build.bat` (Debug x64, `/O2` 적용) |
| 결과물 | `Binaries/NovaEngine.exe` (작업 디렉터리는 반드시 `Binaries/`) |
| 로컬 경로 | `D:\GitHub\Nova-Game-Engine` |
| 원격 | `https://github.com/PinTrees/Nova-Game-Engine.git` (구 `PinTrees/DX11`, 이름 변경 완료, 자동 리다이렉트) |
| 브랜치 | `main` (기존 커밋은 push 완료. 새 커밋의 push는 사용자 지시가 있을 때만) |
| 사용자 언어 | **한국어** – 답변/커밋 설명은 한국어, 코드 식별자는 영어 |

### 실행 모드 (Unity Hub 방식)
| 실행 | 동작 |
|---|---|
| `NovaEngine.exe` (인자 없음) | **NOVA Hub**만 먼저 뜸 (가벼운 창: D3D+ImGui만 초기화, 셰이더/씬 로딩 없음). 프로젝트 생성/선택/추가 |
| `NovaEngine.exe --project "<프로젝트 폴더>"` | 해당 프로젝트를 **에디터**로 엶 (Hub가 프로젝트를 고르면 이 인자로 새 프로세스를 실행) |
| `NovaEngine.exe --editor` | 프로젝트 없이 엔진 폴더의 `Assets/`(샘플 프로젝트)로 에디터 실행 – 엔진 개발/디버깅용 |
| `NovaEngine.exe --create-project "<위치>" "<이름>"` | GUI 없이 프로젝트만 생성하고 종료(자동화/테스트용, 결과는 `Binaries/hub_log.txt`에 `CREATE_OK`/`CREATE_FAILED`). 이름은 ASCII 권장 |

개발/검증용 환경 변수(에디터): `NOVA_SELECT=<오브젝트 이름>`(시작 시 선택 → Inspector 확인), `NOVA_AUTOPLAY=1`(시작 직후 Play).

**Inspector(Unity 스타일)**: `Source/Editor/UnityGUI.*` 가 Unity Inspector 위젯(행=레이블 열 41% | 필드 열, 행 18px/간격 20px, Dropdown/Toggle/Float/Int/Slider/Vector3/Color/ObjectField/Foldout/ComponentHeader/GameObjectHeader/EmptyListBox)을 제공한다. `Component::RenderInspectorGUI`가 헤더(접기 화살표, 아이콘, 활성 체크, ?/프리셋/⋮ 메뉴)를 그리고, `UsesUnityInspector()`가 true 인 컴포넌트(Transform, Camera)만 본문에 UnityGUI 를 쓴다. 나머지 컴포넌트(Light, MeshRenderer 등)는 기존 EditorGUI 본문이라 **아직 Unity 스타일로 옮기지 않음**(같은 방식으로 `OnInspectorGUI`를 UnityGUI 로 교체하면 된다). Camera 는 URP 카메라 Inspector 항목(Render Type, Projection, Rendering, Stack, Environment, Output)을 모두 표시·저장하며, Projection/FOV/Size/Clipping/Background(Game 뷰 클리어 색)만 실제 동작하고 나머지는 값 저장용 UI 항목이다. GameObject 는 Tag/Layer/Static/Active 를 저장한다. 기본 카메라 값은 Unity 기본(FOV 60, Near 0.3, Far 1000).

**아이콘(SVG)**: 원본 `ProjectSetting/icons/svg/*.svg`(수정은 `Tools/make_icons.py`의 문자열에서), `python Tools/make_icons.py` 가 자체 SVG 래스터라이저(`Tools/svg_raster.py`, path/circle/rect/polygon/line + fill/stroke 지원)로 `ProjectSetting/icons/svg/png/*.png`(4배)를 만든다. 엔진은 PNG 를 텍스처로 로드한다(`UnityGUI::Icon`). 폰트는 Segoe UI 13px(라틴) + 맑은 고딕(한글) + Font Awesome, 굵은 폰트는 `Fonts[1]`.

**로고**: `ProjectSetting/logo/nova-logo.svg`(아이콘), `nova-logo-horizontal.svg`(워드마크 포함), PNG(32~512). 디자인은 굵은 기하학적 "N" + 우상단 노바 버스트(8갈래 별)이며 `python Tools/make_logo.py`가 SVG와 PNG/ICO(`ProjectSetting/icon.ico`, `Source/Platform/icon.ico`)를 함께 다시 만든다(Pillow, numpy 필요; 도형 좌표는 스크립트 상수 한 곳). 작업 표시줄/제목 표시줄 아이콘은 `NovaEngine.rc`의 ICO 를 큰/작은 크기로 각각 로드(`App::InitMainWindow`), Hub 상단·설치 카드와 에디터 툴바 좌측에도 로고 표시. 아이콘을 바꾼 뒤에는 `NovaEngine.rc`를 touch 해야 exe 아이콘이 갱신되며, 탐색기/작업 표시줄은 아이콘 캐시 때문에 늦게 바뀔 수 있다.

**초기 프로젝트 화면(Unity 새 3D 프로젝트와 같은 첫 화면)**: 새 프로젝트를 처음 열면 `Assets/Scenes/SampleScene.scene`(Main Camera + Directional Light)을 자동 생성·저장하고 `EditorSettings.json`에 기록한다(`SceneManager::LoadStartupScene`). 화면 구성은 메뉴바(File/Edit/Assets/GameObject/Component/Window/Help) → 툴바(프로젝트 이름, Play/Pause/Step, Layout ▾) → Hierarchy(검색, 씬 이름 헤더, 트리) | Scene(하늘/지평선 그라디언트, 그리드, 카메라·라이트 기즈모) / Game | Inspector, 하단 Project(breadcrumb) / Console / Animator. 스타일 상수는 `Source/Editor/EditorTheme.h`(폰트 15px, 색상), 씬 뷰 오버레이는 `Source/Editor/SceneViewOverlay.*`. 저장된 도킹 배치는 `Binaries/nova_layout_v2.ini`(구조를 바꾸면 파일 이름의 버전을 올려 초기화). 창만 캡처해 확인할 때는 `PrintWindow`(PW_RENDERFULLCONTENT) 사용.

- 소스: `Source/Hub/` (`HubApp` UI, `HubProject` 프로젝트 목록·생성·프로세스 실행). 목록 저장 위치는 `%LOCALAPPDATA%/NOVA/Hub/projects.json`. Hub 로그는 `Binaries/hub_log.txt` (에디터는 `run_log.txt`).
- Hub가 만드는 프로젝트 구조: `Assets/`(+`Scenes/`, `EditorSettings.json`), `ProjectSettings/ProjectSettings.json`.
- `PathManager`는 **엔진 루트**(Shaders/Resources/ProjectSetting 리소스)와 **프로젝트 루트**(Assets 등)를 분리해 관리한다. `GetMovePath*()`는 `ProjectSetting`/`Resources`/`Shaders`로 시작하는 경로는 엔진 루트, 그 외(`Assets/...`)는 프로젝트 루트로 해석한다. 엔진 루트는 `GetEnginePath*()`.
- 에디터 `File > Project Hub...`는 Hub 프로세스를 새로 실행한다. (예전의 에디터 내부 ImGui `ProjectHubWindow`와 프로젝트별 CMake 빌드 버튼은 제거됨)
- Hub UI는 Unity Hub(한국어판) 스크린샷 기준: 좌측 사이드바(프로젝트/설치), 상단 검색·추가▾·새 프로젝트, 행에 즐겨찾기 별·이름/경로·플랫폼·에디터 버전·수정됨·⋯ 메뉴, 경로가 없으면 "프로젝트를 찾을 수 없음" 배지.

---

## 1. 현재 상태 (검증됨 / 미검증 구분)

### 검증됨 (직접 빌드·실행으로 확인)
- `build.bat` 0 에러 빌드, `NovaEngine.exe` 실행 유지(창 제목 `NOVA Game Engine Editor`), 응답함.
- 실행 시작 시간: **캐시 적중 시 ≈1.3초** (기존 ≈14초).
- 기본 도킹 레이아웃(Hierarchy | Scene/Game | Inspector, 하단 Project/Console/Animator)이 `imgui.ini` 없이도 생성됨.
- 메뉴바(File/Edit/GameObject/Window/Help) + 중앙 Play/Stop 버튼 정상 표시.
- 씬 로딩(`Assets/TestScene.scene`) 성공, Hierarchy에 오브젝트 표시.

### 미검증 (수정했지만 실행 확인 못 함)
- `Edit > Graphics API` 서브메뉴 (컴파일만 확인, 클릭 동작/JSON 저장 미확인).
- NOVA Hub 실제 조작: 새 프로젝트 만들기 → 에디터 자동 실행, 디스크에서 추가, ⋯ 메뉴, 검색. (창 렌더링과 `--project`로 빈 프로젝트가 기본 씬으로 열리는 것까지만 확인함. GUI 자동 클릭은 다른 창을 건드릴 수 있어 쓰지 말고 사람이 확인하거나 창 전용 캡처만 사용할 것)
- `NovaEngine/NovaEngine.vcxproj`, `NovaEngine.sln` (Visual Studio 직접 빌드 안 해봄. CMake 기준으로만 검증).
- 루트 폴더 이름 변경 후 `build/` 재생성 및 재빌드(변경 작업은 예약 스크립트가 수행했을 수 있음. `D:\GitHub\_nova_rename.log` 확인. 다음 세션 시작 시 **`build.bat`부터 다시 실행해 확인**할 것).

---

## 2. 저장소 구조

```
Nova-Game-Engine/
├─ Source/                     # 모든 C++ 소스 (CMake가 GLOB_RECURSE, 모든 하위 폴더가 include 경로에 등록됨)
│  ├─ Core/        Types, define, JsonUtility, PathManager, EngineInfo.h(엔진 이름/버전 단일 정의), Time, Input, Debug, Task, Utils, File
│  ├─ Math/        MathHelper, SimpleMath, Matrix3, VectorUtils, vector_ex
│  ├─ Graphics/
│  │   ├─ Common/  API 중립 코드 (GraphicsAPI, IGraphicsBackend, GraphicsBackendFactory, GraphicsSettings, GeometryGenerator, LightHelper …)
│  │   ├─ DX11/    D3D11에 묶인 코드 (Effects, Shader, ShaderCache, RenderStates, Vertex, Mesh, Sky, Ssao, ShadowMap, Terrain …)
│  │   └─ OpenGL/  자리 표시자(미구현 스텁)
│  ├─ Animation/   SkinnedData/Mesh/Model, FBXLoader, LoadM3d, AnimationHelper
│  ├─ Physics/     PhysicsManager, CollisionDetector/Resolver, Octree
│  ├─ Scene/       Scene, SceneManager, GameObject(+Factory), Component(+Factory), Transform, Camera, Light, MeshRenderer, Collider 등
│  ├─ Scripting/   MonoBehaviour, ScriptField(Field<T> 자동 리플렉션), SampleScripts
│  ├─ Editor/      EditorApp, EditorGUI(+Manager/Style/ResourceManager), EditorWindow, Gizmo, SelectionManager …
│  │   ├─ Windows/ Scene/Hierarchy/Inspector/Game/Project/Console/Animator/ProjectHub 창
│  │   ├─ Dialogs/ 선택 다이얼로그
│  │   └─ NodeEditor/ builders, drawing, widgets
│  ├─ ThirdParty/ImGui/  imgui 원본 + ImGuiNodeEditor/
│  └─ Platform/    App, Application, Main, pch, resource.h, NovaEngine.rc, icon.ico
├─ NovaEngine/                 # 레거시 Visual Studio 프로젝트 파일(.vcxproj) – CMake가 주 빌드 경로
├─ NovaEngine.sln
├─ CMakeLists.txt, build.bat, clean.bat
├─ Shaders/  Resources/  Assets/  ProjectSetting/  Libraries/(Include, Lib)  Demos/(LegacyDemos)
└─ Binaries/  build/  Intermediate/    # 산출물·캐시 (git 무시 또는 untracked)
```

런타임 경로 규칙: 실행 파일 작업 디렉터리 = `Binaries/`. 셰이더는 `../Shaders/…`, 프로젝트 설정은 `../ProjectSetting/…`로 접근. `Main.cpp`가 시작 시 `SetCurrentDirectory`로 exe 폴더를 강제.

핵심 런타임 캐시(모두 `Binaries/` 아래, git 무시, 지워도 자동 재생성):
- `ShaderCache/*.fxo` – 컴파일된 `.fx` 이펙트 (`Graphics/DX11/ShaderCache.cpp`, 모든 `Shaders/*.fx`의 최신 수정 시각으로 무효화)
- `TextureCache/*.dds` – 디코딩+밉맵 생성된 PNG/JPG 텍스처 (`Utils::LoadTexture`)
- `run_log.txt` – 초기화 단계 로그(임시 디버그용, 아래 정리 대상)

---

## 3. 반드시 지켜야 할 규칙 / 함정 (이전 세션에서 실제로 겪음)

1. **인코딩**: 원본 베이스 커밋(`a3640d4`)의 소스는 **CP949(EUC-KR)** 한글 주석. 현재 파일은 **UTF-8 with BOM**을 목표로 하며 CMake에 `/utf-8`이 설정돼 있음. 파일을 **UTF-8(BOM 유지)** 로만 저장할 것. 일부 파일(`FBXLoader.cpp` 등)은 아직 깨진 한글 주석(mojibake)이 남아 있음 → 4.4 참조. 절대 `errors='replace'`로 읽고 저장하지 말 것(영구 손상).
2. **줄바꿈**: 저장소 파일은 CRLF 혼용 상태. 스크립트로 수정할 때 원본 개행을 감지해 보존할 것.
3. **셸/도구 주의(Windows)**: 이 환경의 셸 도구는 heredoc 안의 백슬래시가 유실되는 경우가 있었음. 경로/문자열에 `\`가 필요한 Python 스크립트는 `BS = chr(92)`로 조합하거나 파일로 저장해 실행. 또한 PowerShell 도구는 `Remove-Item`/`cmd /c` 조합이 차단될 수 있으니 `[IO.File]::Delete`, `[IO.Directory]::Delete`, `& 스크립트` 사용.
4. **PCH**: 모든 엔진 `.cpp`는 첫 줄에 `#include "pch.h"`. ImGui 서드파티 파일은 PCH 제외(CMake `SKIP_PRECOMPILE_HEADERS`).
5. **include 방식**: 폴더 이동 후에도 `#include "Xxx.h"`(파일명만)로 충분함 – 모든 모듈 폴더가 include 경로에 있음. 새 폴더를 만들면 `CMakeLists.txt`의 `ENGINE_MODULE_DIRS`에 추가.
6. **새 파일 추가**: CMake는 glob이므로 **`build.bat`(재-configure) 실행**해야 인식됨. 레거시 `NovaEngine.vcxproj`에도 넣으려면 수동 추가(선택).
7. **Debug 최적화**: `CMakeLists.txt`에서 Debug에 `/O2`, `/RTC1`·`/JMC` 제거(시작 속도 목적). 정밀 디버깅 시 해당 `if(MSVC)` 블록을 임시 주석 처리.
8. **이름 변경 시**: `CMakeLists.txt`의 `ENGINE_NAME`과 `Source/Core/EngineInfo.h` 두 곳 + 솔루션/vcxproj 이름.
9. **외부 공개 작업은 사용자 승인 후**: `git push`, 레포 설정 변경 등은 사용자가 지시했을 때만. **커밋 메시지는 한글로 작성**(사용자 요청; 예: `feat: 씬 단축키 구현`, 제목·본문 모두 한국어, `feat/fix/refactor/chore` 접두어는 유지). 끝에 `Co-Authored-By: <에이전트 표기>` 트레일러 사용(기존 커밋 참고).
10. **UI 검증은 스크린샷으로**: 실행 후 창만 캡처해서 확인. 전체 화면 캡처 시 사용자의 다른 창(개인 정보)이 찍힐 수 있으니 **엔진 창 영역만** 캡처하고 확인 후 파일 삭제. 캡처 전 창을 foreground로 올릴 것.

---

## 4. 작업 목록 (우선순위 순)

각 작업은 끝나면 **빌드 성공 + 실행 확인(창 유지/응답)** 후 커밋. 서로 다른 성격은 커밋을 분리.

### 4.0 (P0) 환경 재확인
- `D:\GitHub\Nova-Game-Engine`에서 `build.bat` 실행 → `Binaries\NovaEngine.exe` 생성 확인 → 실행해 창/도킹/씬 정상인지 확인.
- 문제가 있으면 `build/` 삭제 후 재-configure(폴더 이름 변경으로 CMake 캐시 경로가 stale일 수 있음).
- 사용자가 승인하면 `git push origin main`(6커밋 선행).

### 4.1 (P1) 씬 파일 라이프사이클 마무리 (`PROJECT_HANDOVER.md` Task 3 잔여)
완료: `MeshFilter` 제거, 미등록 컴포넌트 null 가드, Project Hub에서 하드코딩 씬 경로 제거.
남음:
- `File > New Scene (Ctrl+N)`, `Open Scene (Ctrl+O)`, `Save Scene (Ctrl+S)`, `Save Scene As (Ctrl+Shift+S)` **단축키 동작**(메뉴 항목은 있음, 단축키 처리 미구현). 경로 없는 씬 저장은 `Scene::SaveNewScene`.
- `SceneManager::CreateDefaultScene("Untitled")`: `Main Camera`(0,2,-10) + `Directional Light`(0,3,0) 자동 생성. 마지막 씬 경로가 비었거나 파일이 없으면 이 씬으로 시작(현재는 `CreateScene()` 사용 – 동작 확인 후 정리).
- Project 창 우클릭 `Create > Scene` → 현재 폴더에 `New Scene.scene` 생성, `.scene` 더블클릭 시 로드.
- Hierarchy 최상단에 `▼ <씬 이름>` 헤더 행 표시(Unity와 동일).
- 수락 기준: 씬 새로 만들기→오브젝트 추가→저장→재실행 시 복원.

### 4.2 (P1) Unity 에디터와 1:1 디자인 대조 (사용자 핵심 요구)
기준 화면: **Unity 6.3 LTS (6000.3.x) 에디터** 스크린샷(사용자 제공. **다음 에이전트가 볼 수 있도록 사용자에게 `Docs/Reference/` 폴더에 이미지를 넣어 달라고 요청할 것**). 스크린샷에서 관찰된 사항:
- 상단 타이틀 바 뒤에 **메뉴바**: `File Edit Assets GameObject Component Services Jobs … Tools … Window Help`(현재는 File/Edit/GameObject/Window/Help만 있음 → `Assets`, `Component`, `Services` 등 추가 검토).
- 그 아래 **툴바 행**: 좌측 Unity 계정/`Unity 6` 로고 버튼·브랜치 표시, **중앙 Play / Pause / Step 3개 버튼**(현재 Play/Stop 2개 → Pause/Step 추가), 우측 클라우드·히스토리·검색·`Layout ▾` 드롭다운.
- 탭 구성: `Game`(해상도/Display 드롭다운 툴바), `Scene`(Pivot/Local, 그리드/기즈모 툴 스트립), `Animator`, `Project`(검색+아이콘 필터+breadcrumb), `Hierarchy`(`+ ▾` + 검색, 씬 이름 루트, 접기 화살표, 큐브 아이콘 트리), `Inspector`(오브젝트 이름/Static/Tag/Layer 행, 컴포넌트 접기 헤더+체크박스+`?`,`⋮`, `Add Component` 버튼), `Console`(Clear/Collapse/Error Pause/Editor 필터, 에러/경고/로그 카운트).
- 전반: 어두운 회색 계열 패널(대략 `#282828`~`#383838`), 얇은 구분선, 작은 폰트(약 12px 계열, Inter 계열), 탭은 상단 좌측 정렬 + 잠금(자물쇠)/`⋮` 아이콘.
- 작업 방식: 창 하나씩(툴바 → Hierarchy → Inspector → Project → Console → Scene/Game 툴바) 스크린샷 대조, **동일 해상도(1920×1080)에서 나란히 비교**하고 색·간격·폰트 크기·아이콘을 수치로 맞춘다. 현재 스타일 코드는 `Source/Editor/EditorGUIManager.cpp`(테마·메뉴바·도킹), `EditorWindow.cpp`(창 스타일 push/pop), `EditorGUI.cpp/EditorGUIStyle.cpp`(위젯), `EditorGUIResourceManager.cpp`(폰트 로드: Malgun Gothic + FontAwesome).
- 폰트: 현재 `C:\Windows\Fonts\malgun.ttf` + Font Awesome 6. Unity는 **Inter**(에디터 UI) 계열이므로 라이선스가 허용되는 Inter를 프로젝트에 포함해 교체 검토(한글은 Malgun/Noto Sans KR 폴백).
- 아이콘: 폴더 `ProjectSetting/icons/`(예: `icon_editor_play.png`). Unity 아이콘 원본은 **저작권 자료이므로 복사하지 말고**, 유사한 자체/오픈소스 아이콘 세트(예: Font Awesome, Lucide, Material Symbols)로 대체.
- 수락 기준: 같은 씬을 연 상태에서 두 스크린샷을 겹쳐 봤을 때 레이아웃 비율·색·텍스트 크기가 눈에 띄게 어긋나지 않음. 사용자 확인 필수.

### 4.3 (P2) 남은 UX 이슈 / 확인 필요 항목
- Scene 뷰에 흰색 사각 영역 + 와이어프레임이 보이는 현상: 의도인지(터레인/그리드/스카이) 렌더 버그인지 분석. `EditorApp::_Editor_OnSceneRender` 경로 확인.
- NOVA Hub 개선 후보: 에디터 실행 후 Hub 자동 최소화/종료 옵션, 즐겨찾기 필터, 템플릿 미리보기, 프로젝트별 엔진 버전(설치 탭). 새 프로젝트의 `3D` 템플릿은 현재 빈 씬과 같고 에디터가 기본 씬(Main Camera + Directional Light)을 임시로 만든다 → 4.1의 `CreateDefaultScene`/`SampleScene.scene` 저장 구현 후 템플릿에 반영.
- **주의(빌드)**: 루트 폴더 이름을 바꾼 뒤에는 `build/`의 CMake 캐시가 옛 경로를 가리켜 `build.bat`가 `CMakeCache.txt directory is different` 오류로 실패한다 → `build/`를 지우고(내부에 링크가 있어 `[IO.Directory]::Delete`가 일부 실패할 수 있으니 `clean.bat` 또는 탐색기 사용) 다시 실행.
- `Edit > Graphics API` 실제 클릭 동작·`ProjectSetting/GraphicsSettings.json` 저장 검증.
- `run_log.txt` 진단 로그(`App.cpp`, `EditorApp.cpp`, `Main.cpp`): 사용자가 원치 않으면 `#ifdef _DEBUG`/로거로 정리. 시작 실패 진단 목적이라 삭제 전 사용자 의견 확인.

### 4.4 (P2) 인코딩 정리 (`PROJECT_HANDOVER.md` Task 2 잔여)
- 깨진 한글 주석이 남은 파일 탐지: 파일을 UTF-8로 디코드했을 때 U+FFFD가 있거나 `??` 패턴. 예: `Animation/FBXLoader.cpp`.
- 복구 방법(이전에 성공): `git show a3640d4:DX11/<원본파일>`을 **cp949로 디코드** → 현재 파일과 `difflib`로 라인 정렬 → 코드 부분(`//` 앞)이 같은 라인만 주석 라인을 원본으로 교체. **이동/개명된 파일의 원본 경로**는 `git log --follow`로 추적(예: `EditorApp.cpp` ← `29. MeshViewDemo.cpp`, 폴더가 `DX11/`이던 시절 경로).
- 이후 모든 `.h/.cpp/.inl/.rc`를 UTF-8 **with BOM**으로 통일(내용 손상 없이 확인 후).

### 4.5 (P3) 렌더링 추상화(RHI) – 장기
현재 상태(1단계 완료): 폴더 분리 + `IGraphicsBackend`(이름/지원 여부만) + 팩토리 + 설정 저장 + 메뉴. OpenGL 선택 시 DX11로 대체.
목표 순서:
1. 디바이스/스왑체인/백버퍼(RTV/DSV) 생성·리사이즈·Present를 `App`에서 백엔드로 이동.
2. `IBuffer / ITexture / IShader / IPipelineState / ICommandContext`류 추상 인터페이스 도입.
3. Scene/Editor에 직접 노출된 `ID3D11*`·`ComPtr<...>` 사용 제거(현재 Scene 7파일, Editor 7파일, Animation 2파일, Platform 5파일 등 약 50개 파일에서 사용).
4. `OpenGLGraphicsBackend` 구현(GLFW/WGL + GLAD 등), 셰이더는 FX11 → HLSL/GLSL 크로스 컴파일 전략 결정(SPIRV-Cross 등).
5. 그 위에 Unity SRP를 닮은 렌더 파이프라인 계층(카메라별 렌더 요청 → 패스 구성).
- 이 단계는 큰 작업이므로 시작 전에 **범위/일정을 사용자와 합의**하고 작은 PR 단위로 쪼갤 것.

### 4.6 (P3) 오픈소스 엔진 참고 업그레이드 (사용자 허용)
사용자가 "GitHub의 좋은 오픈소스 엔진 예제를 참고해 업그레이드해도 된다"고 승인함. 후보: Hazel(TheCherno), Wicked Engine, The Forge, Godot(에디터 UX), Flax/Stride(구조), bgfx(RHI 참고), Dear ImGui 예제·ImGuizmo(기즈모)·ImGuiFileDialog.
- **라이선스를 확인하고**(MIT/Apache/zlib만 코드 차용, GPL 코드는 복사 금지) 출처를 `THIRD_PARTY_NOTICES.md`에 기록.
- 우선 후보: ImGuizmo로 Scene 기즈모 개선, 에셋 파이프라인(메타 파일/GUID), 프리팹, Undo/Redo, 셀렉션 하이라이트, 물리(예: Jolt/Bullet 연동 검토).

---

## 5. 빌드·실행·검증 절차 (복붙용)

```bash
# 빌드 (프로젝트 루트)
.\build.bat                 # cmake configure + Debug x64 빌드, 성공 시 Binaries\NovaEngine.exe

# 캐시 정리 (문제 시)
.\clean.bat                 # build/, Intermediate/ 정리

# 실행 (작업 디렉터리 = Binaries)
cd Binaries; .\NovaEngine.exe
```

검증 체크리스트(변경 후 매번):
1. `build.bat` 에러 0.
2. 실행 후 6~8초 대기 → 프로세스 생존 + 응답(`Responding == True`) + 창 제목 `NOVA Game Engine Editor`.
3. 엔진 창만 캡처해 UI 확인(개인 정보 보호를 위해 전체화면 캡처 금지, 확인 후 삭제).
4. `Binaries/run_log.txt` 마지막 줄이 `theApp.Run() ...`/`Init finished successfully!` 근처인지.
5. 씬 관련 변경 시: 새로 만들기→저장→재실행 왕복 테스트.

첫 실행(캐시 없음)은 셰이더/텍스처 캐시 생성으로 **수 초~십수 초 더 오래 걸리는 것이 정상**.

---

## 6. 이번 세션까지의 커밋 이력 (최신순)

| 커밋 | 내용 |
|---|---|
| `06d30d0` (환경에 따라 해시 상이) | NOVA Game Engine으로 개명, `DX11/`→`NovaEngine/`, `.sln` 개명 |
| `ca15515` | 렌더러를 Common/DX11/OpenGL로 분리 + `IGraphicsBackend`/설정/메뉴 (당시 이름 Mimic) |
| `4aa4338` | 플랫 `DX11/` → `Source/` 모듈 구조 |
| `20c1e1f` | MeshFilter 제거·null 가드·주석 복구·메뉴바/도킹 수정·셰이더/텍스처 캐시·Debug `/O2` |
| `6d3696c` | MonoBehaviour 자동 리플렉션(`Field<T>`) + 앱 아이콘 |
| `73992a9` | Unity 스타일 에디터 전환 + CMake + Project Hub |

---

## 7. 사용자에게 확인/요청할 것
1. Unity 레퍼런스 스크린샷을 `Docs/Reference/`에 저장해 달라고 요청(창별 확대본, 폰트 확인용 포함이면 더 좋음).
2. `git push origin main` 승인 여부.
3. `run_log.txt` 진단 로그를 유지할지 제거할지.
4. RHI/OpenGL 작업 착수 시점과 범위.
