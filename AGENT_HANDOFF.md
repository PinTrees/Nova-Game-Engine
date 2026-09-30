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

개발/검증용 환경 변수(에디터): `NOVA_SELECT=<오브젝트 이름>`(시작 시 선택 → Inspector 확인), `NOVA_AUTOPLAY=1`(시작 직후 Play), `NOVA_TOOL=0..5`(시작 도구), `NOVA_DEV_PHYSICS=1`(물리 확인용 오브젝트 추가, 저장 안 함), `NOVA_PHYSICS_LOG=<파일>`(충돌/트리거 이벤트와 바디 위치 기록), `NOVA_DEV_ADDCOMP=1|<카테고리>|?<검색어>`(Add Component 팝업 자동 열기), `NOVA_PHYSICS_TEST=<로그 파일>`(물리 자체 검사 장면 `Source/Platform/PhysicsSelfTest.cpp` 추가 — 토크/충격량/짐벌 락/경사면 구름·미끄러짐/탑/혼합 낙하를 기록, `NOVA_AUTOPLAY=1` 과 함께 사용).

**Inspector(Unity 스타일)**: `Source/Editor/UnityGUI.*` 가 Unity Inspector 위젯(행=레이블 열 41% | 필드 열, 행 18px/간격 20px, Dropdown/Toggle/Float/Int/Slider/Vector3/Color/ObjectField/Foldout/ComponentHeader/GameObjectHeader/EmptyListBox)을 제공한다. `Component::RenderInspectorGUI`가 헤더(접기 화살표, 아이콘, 활성 체크, ?/프리셋/⋮ 메뉴)를 그리고, `UsesUnityInspector()`가 true 인 컴포넌트(Transform, Camera)만 본문에 UnityGUI 를 쓴다. 나머지 컴포넌트(Light, MeshRenderer 등)는 기존 EditorGUI 본문이라 **아직 Unity 스타일로 옮기지 않음**(같은 방식으로 `OnInspectorGUI`를 UnityGUI 로 교체하면 된다). Camera 는 URP 카메라 Inspector 항목(Render Type, Projection, Rendering, Stack, Environment, Output)을 모두 표시·저장하며, Projection/FOV/Size/Clipping/Background(Game 뷰 클리어 색)만 실제 동작하고 나머지는 값 저장용 UI 항목이다. GameObject 는 Tag/Layer/Static/Active 를 저장한다. 기본 카메라 값은 Unity 기본(FOV 60, Near 0.3, Far 1000).

**Light Inspector**: URP Light Inspector 항목(General: Type/Mode, Emission: Light Appearance(Color / Filter and Temperature), Color, Filter, Temperature 바 + Kelvin, Intensity, Indirect Multiplier, 경고 박스, Range, Spot Angle, Cookie, Rendering, Shadows)을 표시한다. Color(Diffuse)·Intensity(렌더링 시 Diffuse/Specular 에 곱함)·Type·Range·Temperature→색 변환은 실제로 적용되고, Mode/Indirect/Cookie/Rendering Layers/Culling Mask 는 저장만 한다. **Shadow Type 은 실제 적용**(No Shadows 면 그 광원의 그림자맵을 비워 둠, Hard/Soft 는 같은 3x3 PCF). 새 Directional Light 기본값 = Unity 와 같은 회전 (50, -30, 0) + Soft Shadows(`GameObjectFactory::CreateDirectionalLight`). 저장 파일에 `shadowVersion` 이 없는 옛 Directional Light 의 shadowType 0 은 로드 시 Soft 로 옮긴다. 방향광 그림자는 카메라 앞 40m 를 덮는 60m 정사영 상자(텍셀 스냅, 깊이 범위 300m, `Light.cpp` 의 `FitDirectionalShadow`), 그림자맵 래스터 바이어스 DepthBias 1500 / Slope 2 (`Shaders/26. BuildShadowMap.fx`). 이전에는 상자가 카메라 Far×2(2000m) + 바이어스 100000 이라 그림자가 사실상 보이지 않았다. 새 재질의 `UseShadowMap` 기본값은 켜짐. 씬 기즈모(`SceneViewOverlay::DrawLightGizmo`): 화면 크기 고정 해 아이콘(어두운 테두리) + 거리 비례 크기의 방향 표시(원 + 평행선 8개 + 화살표, Transform forward 기준, 선택 시 굵고 밝게), Point 는 아이콘만. Unity 의 "Universal Additional Light Data (Script)" 컴포넌트는 표시하지 않는다.

**내장(Built-in) 리소스 / 기본 도형**: Cube·Sphere·Capsule·Cylinder·Plane·Quad 메시는 코드로 만드는 엔진 내장 자원이며(`GameObjectFactory::GetPrimitiveMesh`), 프로젝트 폴더와 무관하게 어떤 프로젝트에서도 만들 수 있다. 씬에는 `builtin:Cube` 같은 경로로 저장되어 다시 열 때 복원된다(`ResourceManager::LoadMesh`가 가로챔). 기본 재질은 `UMaterial::GetDefault()`(`builtin:Default-Material`, 파일로 저장되지 않음). 도형은 Unity 와 같이 **Transform + MeshFilter + MeshRenderer(+ Box/SphereCollider)** 로 만들어지고, 이전 형식의 씬(MeshRenderer 가 메시를 직접 보유)은 로드 시 MeshFilter 로 자동 이전된다. 새 내장 메시/재질을 추가하려면 `PrimitiveType`과 `BuiltinName()`에 항목을 넣는다.

**캐릭터 / 스킨 애니메이션 (엔진 패키지)**: 기본 캐릭터와 애니메이션은 `Resources/Packages/Character/`(엔진 기준 경로 `Resources\\Packages\\...` → 모든 프로젝트에서 사용)에 있다: `Model_Unity_Ver1.FBX`(스킨 메시 1, 본 56, 노드 81) + `Animations/GhostSamurai_APose_Idle.FBX`(같은 리그, 기본 Idle), `Animations/Rapier_Idle.fbx`(자체 메시 + Bip001 리그 + idle), `GreatSword_*.FBX`(Bip001 리그 클립 - Rapier 캐릭터용), `base_body_model.fbx`(정적 메시). 이전 엔진 Assets 의 레거시 파일(Troll, ENV, Factory, 스캔 텍스처, 옛 씬)은 삭제(git 이력에 있음). GameObject > 3D Object > **Character** 가 `GameObjectFactory::CreateAnimatedCharacter()` 로 루트(Animator + `Resources/Packages/Character/DefaultCharacter.controller`, 기본 상태 Idle) + 자식(Skinned Mesh Renderer) 을 만든다. `CreateCharacter()` 는 Animation 컴포넌트(단일 클립) 버전으로 남아 있다(검사용). 이 폴더는 `.gitignore` 의 `**/[Pp]ackages/*` 에 걸리므로 새 파일은 `git add -f` 로 추가한다.
가져오기(`FBXLoader`, Assimp): 모델/스켈레톤/클립 모두 같은 설정(왼손 좌표계, 삼각형화, 가중치 4, 65000 정점 분할, **FBX 피벗 보조 노드 끔** → 노드 변환과 키가 PreRotation 포함 같은 값). 스켈레톤 = 노드 계층 전체(이름/부모/바인드 로컬, `UnitScale` = FBX UnitScaleFactor×0.01 → cm 를 미터로). 스킨 메시 = 본 팔레트(이름 + Assimp OffsetMatrix), 정점 본 인덱스는 팔레트 번호. 클립 = 노드 이름별 위치/회전/크기 키(초). **주의: 포함된 Assimp 헤더와 DLL 의 aiQuatKey 크기가 다르다(DLL 32바이트)** → `DetectQuatKeyStride` 로 실제 간격을 찾아 읽는다(헤더를 DLL 버전에 맞게 교체하면 제거 가능). 캐시(.mesh/.animations/.skeletons)는 FBX 옆에 쓰고 맨 앞에 버전 매직(`kMeshCacheMagic`, 현재 NVC7)이 있어 형식이 바뀌면 자동으로 다시 가져온다(Resources/Packages 의 캐시는 .gitignore).
재생: `AnimationPose`(샘플 → 전역, 루트에 UnitScale), 본 행렬 = `MeshBind(메시 노드 바인드 전역) * Offset * 본 전역` (메시 노드의 PreRotation 을 반영해야 서 있는 자세가 된다), 셰이더 본 배열 256. `AnimationPlayer`(Inspector 이름 "Animation"): Animation/Animations 목록/Play Automatically/Animate Physics/Update Mode/Culling Type, 클립은 "FBX 경로 + 클립 번호", 자기와 자식의 Skinned Mesh Renderer 에 포즈 적용, 채널은 노드 이름으로 연결(리그가 다르면 뒤틀림 - Unity Humanoid 리타깃 없음). Edit 모드에서는 재생하지 않는다. 개발용 `NOVA_ANIM_TEST=<로그>`(가져오기/스키닝 AABB 검사), `NOVA_FBX_DUMP=<fbx;...>` + `NOVA_FBX_DUMP_LOG`(FBX 구조 덤프), `NOVA_SCENE_CAM="x y z tx ty tz"`(Scene 뷰 시작 시점).

**Animator (Unity Mecanim 상태 머신)**:
- 에셋 `AnimatorController`(`Source/Animation/AnimatorController.*`, `.controller` JSON): Parameters(Float/Int/Bool/Trigger), Layers(Base Layer + 추가 레이어 Weight=Override), 레이어마다 States(이름, Motion = FBX 경로 + 클립 번호, Speed, Cycle Offset, Loop Time, Foot IK, Write Defaults, 그래프 위치), Transitions(From/To = 상태 이름 또는 `<Any State>`/`<Entry>`/`<Exit>`, Has Exit Time, Exit Time, Fixed Duration, Duration, Offset, Can Transition To Self, Mute/Solo, Conditions). `Load(path)` 는 경로별 공유 캐시(weak_ptr, 경로는 `\` 로 정규화), 편집 도우미(AddState/RemoveState/RenameState/AddTransition/AddParameter/RenameParameter/…) + `Commit()`(Revision 증가 + 저장). 런타임은 Revision 이 바뀌면 파라미터/레이어 수를 맞춘다.
- 컴포넌트 `Animator`(`Source/Scene/Animator.*`): Unity API `SetFloat/SetInteger/SetBool/SetTrigger/ResetTrigger/Get*`, `Play`, `CrossFade`, `Update(dt)`(수동 진행), `Rebind()`, `IsInTransition`, `GetCurrentStateName`. 매 스텝: 전이 중이면 두 상태 시간을 진행하고 Duration 이 지나면 목적 상태로 → 아니면 Any State 전이 먼저, 그다음 현재 상태 전이(목록 순서가 우선순위, Solo/Mute 반영). Exit Time 은 루프마다 그 지점을 지날 때 참, 조건만 있는 전이는 즉시. 조건에 쓴 Trigger 는 소비. `<Exit>` 로 가면 Entry 전이(조건) 또는 기본 상태로. 전이가 시작된 프레임에는 섞지 않고 다음 프레임부터 진행(Unity 와 같음). 포즈 = 상태 샘플 → `AnimationPose::Blend`(위치/크기 선형, 회전 Slerp) → 레이어 Weight → 자기·자식 Skinned Mesh Renderer. Inspector 는 Controller(선택 팝업/Create New Controller)/Avatar/Apply Root Motion/Animate Physics/Update Mode/Culling Mode + Unity 와 같은 정보 상자(Clip Count, Curves …), Play 중 Current State. Root Motion/Avatar/Humanoid/Blend Tree/Sub-State Machine/IK 는 아직 없음(필드만 저장).
- Animator 창(`Source/Editor/Windows/AnimatorEditorWindow.*`, imgui-node-editor 미사용, DrawList 직접): 왼쪽 Layers/Parameters 탭(+ 추가, 더블클릭/우클릭 이름 바꾸기·삭제, 검색, 값 편집 — Play 중 Live 대상이면 런타임 값), 눈 아이콘으로 패널 숨김, breadcrumb(레이어 이름), Auto Live Link 토글, 격자 그래프(휠 확대/축소, 가운데 버튼 또는 Alt+왼쪽 드래그 이동, F 전체 보기, 처음 열 때 자동 맞춤), 노드 색(Entry 초록, Any State 청록, Exit 빨강, 기본 상태 주황, 나머지 회색, 선택 파랑 테두리), 전이 화살표(반대 방향은 옆으로 벌림, 같은 방향 여러 개면 화살표 3개, Entry→기본 상태 주황), 우클릭 메뉴(Create State > Empty, 상태: Make Transition / Set as Layer Default State / Delete, 전이: Delete), Delete 키, 노드 드래그(놓을 때 저장), Play 중 현재/전이 목적 상태 진행 막대 + 진행 중 전이 파란색, 오른쪽 아래 컨트롤러 경로. 대상: 선택한 GameObject 의 Animator 컨트롤러 또는 Project 에서 고른 `.controller`(선택이 바뀌어도 유지, Play 중 대상이 없으면 같은 컨트롤러를 쓰는 첫 Animator 를 Live 대상으로).
- 선택/Inspector: `SelectionType::ANIMATOR` + `AnimatorSelection{Controller, Layer, State, Transition}` → `Source/Editor/AnimatorInspector.*` 가 상태(이름, Motion 선택 = `AnimationClipLibrary` 팝업, Speed, Loop Time, Cycle Offset, Foot IK, Write Defaults, Length, Transitions 목록 Solo/Mute, Set as Layer Default State)와 전이(Transitions 목록, Has Exit Time, Settings, 경고 HelpBox, Conditions 목록: 파라미터/비교/값 + +/-)를 그린다. 입력이 끝나면(활성 위젯 없음) 파일 저장. `.controller` 파일은 `SelectionSubType::ANIMATOR_CONTROLLER`(요약 Inspector), Project 창 Create > Animator Controller, 더블클릭 = Animator 창 앞으로.
- 검사: `NOVA_ANIMATOR_TEST=<로그>` → 로그 폴더에 `SwordFighter.controller`(Rapier Idle / GreatSword Idle / GreatSword Root, Bool GreatSword, Trigger Pose, Any State 전이, Exit Time 전이)를 만들고 Rapier 모델 캐릭터에 붙여 조건 전이·크로스페이드 포즈(두 상태 사이)·Any State·Trigger 소비·Exit Time·CrossFade/Play·저장 파일을 검사(마지막 줄 `RESULT: ALL PASS`). `NOVA_FOCUS_WINDOW=Animator` 는 시작 직후 Animator 탭을 앞으로. 참고: GreatSword 클립은 한 자세짜리(정지) 클립이라 그 상태에서는 화면이 움직이지 않는 것이 정상.

**Terrain (Unity 지형)**:
- 에셋 `TerrainData`(`Source/Terrain/TerrainData.*`, `.terraindata` 바이너리 "NVTD"): 높이맵(해상도 33~4097, 저장은 16비트, 메모리는 0~1 float, 인덱스 [z*res+x]), 크기(기본 1000x600x1000, 해상도 513), 컨트롤(스플랫) 맵 RGBA8 512 = 레이어 4개 가중치, 레이어 목록. `Load` 는 경로별 **강한 참조 캐시**(Play 종료로 씬을 다시 읽어도 저장 안 된 편집 유지), 씬 저장(Ctrl+S) 때 `SaveAllDirty`, 저장 안 된 지형 편집도 씬 `*` 표시(`AnyDirty`). 높이 조회 `GetHeight`(렌더링 메시와 같은 대각선 (0,0)-(1,1) 삼각형 보간), `GetNormal`, `Raycast`(칸 절반 간격 전진 + 이분 탐색). 편집 후 `OnHeightsChanged/OnControlChanged(범위)` → 바뀐 영역만 GPU 텍스처에 올리고(UpdateSubresource) 해당 쿼드트리 노드만 다시 계산, `Revision` 증가(물리 형상 재생성).
- `TerrainLayer`(`.terrainlayer` JSON: diffuse, tileSize, tileOffset, tint). 엔진 기본 레이어 `Resources/Packages/Terrain/Layers/{Grass,Dirt,Rock,Sand}`(절차적 타일 텍스처, 폴더는 .gitignore 라 `git add -f`).
- **LOD = 쿼드트리**(`Source/Terrain/TerrainRenderer.*`): 루트 노드 = 지형 전체, 깊이마다 4등분, 가장 깊은 노드 = 높이맵 간격 1. 모든 노드는 같은 32x32 격자(정점 버퍼 없이 `SV_VertexID` + 인덱스 버퍼)이고 큰 노드일수록 높이맵을 성기게 샘플링한다. 노드 오차 = 자식 격자점에서의 보간 오차 + 자식 오차 최대값(계층 누적). 매 프레임 화면(Scene/Game)마다: 오차를 화면에 투영한 픽셀 > Pixel Error 이면 분할 → 이웃 크기 차이 최대 2배로 균형(큰 이웃을 쪼갬) → 두 배 큰 이웃과 닿는 변은 홀수 격자점을 붙여 틈 제거(인덱스 버퍼 16가지) → 절두체 컬링(그림자 패스는 광원 ViewProj). 선택은 절두체와 무관하게 지형 전체를 덮어 균형/봉합이 안정적. 그림자/노멀깊이/본 패스가 같은 카메라로 고르므로(`RenderManager::RenderingEditorView` 로 어느 화면인지 구분) 모양이 일치한다.
- 셰이더: `Shaders/40. TerrainCommon.fx`(공통 include: 높이맵 정점, 픽셀 단위 법선, 레이어 혼합) + `32. InstancedBasic.fx` 의 `TerrainTech`(조명/그림자/SSAO/안개, 깊이 LESS_EQUAL 후 C++ 에서 원래 깊이 상태 복원), `26. BuildShadowMap.fx` 의 `TerrainShadowTech`, `28. SsaoNormalDepth.fx` 의 `TerrainNormalDepthTech`. **주의**: 노멀깊이 패스와 본 패스의 클립 좌표는 반드시 같은 CPU 행렬 한 번 곱셈(`mul(posW, ViewProj)`)으로 계산해야 한다 — `(p·V)·P` 로 하면 반올림 차이로 깊이 검사가 반쯤 실패해 하늘이 얼룩처럼 비친다.
- 컴포넌트 `Terrain`(`Source/Scene/Terrain.*`, Transform 위치만 사용): Draw, Pixel Error(기본 5), Basemap Distance(저장만), Shadow Casting Mode(Off/On/Two Sided/Shadows Only), `SampleHeight`, `GetInterpolatedNormal`, `Raycast`, `GetActiveTerrains`. Debug > Show LOD Nodes = Scene 뷰에 그린 노드 경계를 깊이별 색으로 표시. `TerrainCollider`(Physics): Jolt `HeightFieldShape`(블록 4, 16비트, 정적 전용), Terrain Data 가 비면 같은 오브젝트 Terrain 의 데이터, 시그니처에 데이터 Revision 포함.
- 편집(`Source/Editor/TerrainEditor.*`, Unity Inspector 모양): 도구 막대 5개(Create Neighbor/Trees/Details 는 "Not supported yet"), Paint Terrain 드롭다운(Raise or Lower[Shift=내리기], Paint Texture, Set Height[Shift+클릭=높이 샘플, Flatten All], Smooth Height; Paint Holes/Stamp 없음), 브러시 4종(Soft/Hard/Linear/Noise), Brush Size, Opacity, Terrain Layers(썸네일, Edit Terrain Layers...: Add/Create/Remove, Diffuse/Tile Size/Offset), Terrain Settings(Basic Terrain, Terrain Data 선택/Create, Mesh Resolution, Heightmap Resolution, LOD 통계). Scene 뷰: 지형이 선택되고 Paint Terrain 이면 지형 표면을 따르는 파란 브러시 원 + 칠하기, 이때 `SceneGizmoTools::SetSuppressed` 로 변환 핸들/클릭 선택을 끈다(카메라 조작은 유지). 지형 클릭 선택은 `PickRecursive` 가 지형 광선으로 처리. GameObject > 3D Object > Terrain = `GameObjectFactory::CreateTerrain`(Assets 에 New Terrain.terraindata). Undo 없음.
- 검사: `NOVA_TERRAIN_TEST=<로그>` → 로그 폴더에 `TestTerrain.terraindata`(300x60x300, 513)를 실제 브러시 코드로 만들고(언덕/분화구/잔 기복/고원/다듬기, 경사·높이로 Rock/Sand, 흙길 칠하기) 노드 깊이별 오차·광선 검사를 기록하고 공 5개를 올린다. `NOVA_AUTOPLAY=1` + `NOVA_PHYSICS_LOG` 로 공이 지면+반지름(오차 5mm 이내)에 멈추는지 확인. `NOVA_TERRAIN_LOD=1` 이면 LOD 노드 표시 + Settings 탭.

**에디터 디버그 로그**(`Source/Core/EditorLog.*`, Unity 의 Editor.log): 사용자 Console(`Debug::Log`)과 별개로 `<실행 폴더>/Logs/Editor.log`(직전 실행은 `Editor-prev.log`)에 `[경과초] [분류] 내용` 을 매 줄 즉시 기록. C++ 런타임 assert 메시지(`_CrtSetReportHookW2`)와 처리되지 않은 예외도 남는다. 분류: App(COM 초기화, 백버퍼 크기), Startup(로딩 단계), Shader(캐시/컴파일 시간), View(렌더 타깃 크기), Camera, Scene(저장/복원), Undo, Terrain(획), Prefab, DragDrop(프리팹 놓기), Texture(로드 실패), Project(현재 폴더, 이름 바꾸기, 삭제). Console 창 "Open Editor Log" 버튼. **원인을 모르는 실패는 같은 테스트를 반복하지 말고 여기에 로그를 추가해 읽고 판단한다.**

**Undo / Redo**(`Source/Editor/UndoSystem.*`, Ctrl+Z / Ctrl+Y·Ctrl+Shift+Z, Edit 메뉴 "Undo <이름>"): 조작이 끝난 순간(위젯 비활성화, 마우스/키 뗌, `RequestCheck`, 다음 프레임에 한 번 더) 씬 JSON 을 직전 확정본과 비교해 바뀌었으면 한 단계로 기록한다 → Inspector 값, 이동/회전/크기 핸들(이름은 `SetActionName`), 생성/삭제/복제/부모 변경/컴포넌트 추가·제거가 모두 같은 방식. 되돌리기 = `SceneManager::RestoreSceneState(json)`(씬 재생성, 경로 유지, 선택과 Hierarchy 펼침은 **GameObject fileID** 로 복원). `GameObject::m_FileID`(저장되는 64비트 고유 ID, "fileID", 복제/복사 붙여넣기는 `RegenerateFileIDs`, 잘라내기는 유지) — 프리팹도 이 ID 를 쓴다. 에셋: `Undo::WatchAsset(key, label, capture, restore)` 를 편집 중인 창이 매 프레임 부른다(Animator 창·상태/전이 Inspector = 컨트롤러 `ToJsonString/ApplyJson`, 재질 Inspector = `UMaterial` json + `ReloadTextures`). 지형: 브러시 한 획 = 바뀐 사각형 영역의 높이/컨트롤만 저장, Flatten All/레이어 추가·제거/높이맵 해상도 = 전체 스냅샷(`TerrainData::RestoreState`), 크기 = 값만. Play 중엔 기록 안 함, 다른 씬을 열면 기록 비움(Play/Stop 은 유지). 최대 300 단계 / 256MB. **주의: 매 프레임 값이 바뀌는 필드를 씬에 저장하면 열자마자 `*` 가 붙고 Undo 뒤 클릭마다 가짜 단계가 생겨 Redo 가 지워진다** — 그래서 Camera `aspect` 는 저장하지 않는다. 크기 0 인 뷰(숨김/배치 전)에서는 렌더 타깃을 만들지 않고 종횡비 0 은 무시(투영 행렬 assert 방지).

**프리팹**(`Source/Scene/PrefabUtility.*`, Unity 의 PrefabUtility): 에셋 = `.prefab` JSON `{nova_prefab:1, root:<GameObject json>}`, 에셋 안 오브젝트의 fileID 가 "원본 ID". 인스턴스의 각 GameObject 는 `PrefabLink{Asset, Source, Root, Revision}`(`GameObject::m_Prefab`)을 갖고, 씬에는 `"prefab":{asset, source, root, overrides}` 로 **오버라이드만** 저장한다. 씬을 읽을 때 `NeedsMerge` → `MergeWithAsset` 이 현재 에셋 값 위에 오버라이드를 덮어써 만든다 → 에셋을 고치면 모든 인스턴스에 반영된다. 오버라이드 키: `GameObject/필드`, `타입#순번/필드`, `+타입#i`(추가한 컴포넌트), `-타입#i`(제거한 컴포넌트). 인스턴스 루트의 로컬 위치/회전/크기와 이름 등은 항상 인스턴스 값(Unity 와 같음). 오버라이드는 **인스턴스를 만든 에셋 리비전** 기준으로 계산한다(에셋 캐시가 파일 시각이 바뀔 때마다 새 리비전을 최대 8개 보관) — 최신 에셋 기준으로 계산하면 방금 적용된 변경이 오버라이드로 잘못 잡혀 전파가 막힌다. 만들기: Hierarchy 의 GameObject 를 Project 창으로 끌어 놓기(`SaveAsPrefabAssetAndConnect`, 같은 이름이면 " 1" 붙임). 배치: `.prefab` 을 Hierarchy(행 = 그 자식, 빈 곳 = 루트)로 끌기(`InstantiatePrefab`, Undo "Instantiate Prefab"). Hierarchy 에서 인스턴스는 파란 아이콘/이름, 우클릭 Prefab > Select Asset / Unpack Completely. Inspector 인스턴스 루트에 "Prefab [Open(미구현)] [Select] [Overrides ▾]" 행 — Overrides 팝업에 목록과 Revert All / Apply All(`ApplyAll`: 새 오브젝트는 새 원본 ID, 에셋 루트의 위치/이름은 유지 후 모든 인스턴스 재생성). 오버라이드된 컴포넌트 헤더 왼쪽에 파란 막대. 미구현: 프리팹 편집 모드(Open), 중첩 프리팹/Variant, 개별 오버라이드 Apply/Revert. 검사: `NOVA_PREFAB_TEST=<로그>`(20개 항목 PASS).

**Project 창**(`Source/Editor/Windows/ProjectEditorWindow.*`, Unity Two Column Layout): 툴바(+ 생성 메뉴: Folder/Material/Animator Controller/Terrain Layer, 둥근 검색창과 지우기, 타입 필터, Label/즐겨찾기(비활성), 새로고침) | 왼쪽 폴더 트리(Assets, Packages = 엔진 `Resources/Packages`; 화살표 = 펼치기, 행 = 그 폴더 열기) | 오른쪽 breadcrumb(조각 클릭 이동) + 목록(폴더 먼저, 확장자 숨김, FBX 는 화살표로 하위 메시/애니메이션 클립 — 모델은 펼칠 때만 로드) 또는 격자(하단 슬라이더, 맨 왼쪽 = 목록, 텍스처는 썸네일) + 하단 선택 경로. 검색은 Assets + Packages 전체(결과 1초 캐시). 더블클릭 = 폴더 열기/씬 로드/컨트롤러는 Animator 창/그 외 기본 프로그램. 우클릭: Create, Show in Explorer, Open, Rename(F2, 인라인), Delete(Del, 확인 창 → 휴지통 `SHFileOperation`), Copy Path, Refresh. 끌기 payload: `PREFAB_FILE`(상대 경로), `FBX_FILE`(절대), `PNG_FILE`, `MAT_FILE`, `CONTROLLER_FILE`, `ASSET_FILE`. 폴더 목록은 1초 캐시. **ImGui ID 는 경로 문자열로 만든다** — 예전 코드의 `PushID(entry.path().wstring().c_str())` 는 임시 객체 주소라 매 프레임 ID 가 바뀌어 끌기가 시작되지 않았다. 아이콘: `Tools/` 로 만든 folder/folder_open/prefab/asset_*/filter_type/label/star/refresh.

**Volume / 후처리 (Unity URP Volume)**:
- 데이터: `Source/Graphics/Common/VolumeProfile.*` — `VolumeParameter`(Override 체크 + float[4] 값, 종류 Float/Clamped/Int/Bool/Enum/Color/Vector2), `VolumeComponent`(효과 = 파라미터 목록, `Create(type)` 에 Unity 기본값), `VolumeProfile`(.volumeprofile JSON 에셋, 경로별 캐시 + 파일 시각이 바뀌면 다시 읽음, `CreateAsset`), `VolumeStack`(섞인 결과). 효과: Bloom, Chromatic Aberration, Color Adjustments, Film Grain, Tonemapping(None/Neutral/ACES), Vignette, White Balance. 효과를 추가하려면 `VolumeComponent::Types/Create`, `VolumeStack::IsActive`, `PostProcessPass::Execute`, 셰이더를 함께 고친다.
- 컴포넌트: `Source/Scene/Volume.*` — Mode(Global/Local), Blend Distance, Weight, Priority, Profile. Local 은 같은 GameObject 의 Box/Sphere Collider 까지의 거리로 섞는다(Unity 와 같이 거리 제곱 기준). `VolumeManager::Update(stack, 카메라 위치)`: 스택 초기화 → **Project Settings 의 기본 프로파일(모든 값 적용)** → 씬 Volume 을 Priority 오름차순으로 Override 된 값만 lerp(Bool/Enum 은 t>0 이면 교체). GameObject > Volume > Global/Box/Sphere Volume(`GameObjectFactory::CreateVolume`), Add Component > Miscellaneous > Volume.
- 편집 UI: `Source/Editor/VolumeEditor.*` — Profile 필드 [이름 ⊙][New][Clone] (⊙ = 프로젝트의 .volumeprofile 목록, Project 창에서 끌어 놓기, 클릭 = 에셋 선택), 효과 헤더(활성 체크, ALL/NONE, ⋮ Reset/Remove), 파라미터별 Override 체크, Add Override > Post-processing. 입력이 끝나면 저장, `Undo::WatchAsset("volumeprofile:"...)`. Project 창 Create > Volume Profile, .volumeprofile 선택 시 Inspector 에 같은 편집기.
- 기본 프로파일: Edit > Project Settings... > Graphics (`Source/Editor/Windows/ProjectSettingsWindow.*`, 떠 있는 ImGui 창) → Volume > Default Profile. 설정은 `<프로젝트>/ProjectSettings/GraphicsSettings.json` 의 `defaultVolumeProfile`(`RenderPipelineSettings`). 페이지를 처음 열 때 없으면 `Assets/Settings/DefaultVolumeProfile.volumeprofile`(모든 효과, 기본값)을 만든다. 기본 프로파일은 Override 체크 없이 모든 값이 기준값이다(Unity 6 과 같음). **`PathManager::IsEngineRelative` 는 첫 폴더 이름 전체를 비교한다** — 예전에는 접두어 비교라 프로젝트의 `ProjectSettings\` 가 엔진의 `ProjectSetting\` 으로 잘못 풀렸다.
- 렌더링: `Source/Graphics/DX11/PostProcessPass.*` + `Shaders/41. PostProcess.fx`. 뷰(Scene/Game)마다 `PostProcessingManager` 가 패스와 스택을 하나씩 갖는다. `EditorApp::OnSceneRender/_Editor_OnSceneRender` 가 필요할 때(`IsNeeded`: 켜진 효과가 있거나 FXAA/디더링)만 씬을 R16G16B16A16F 타깃에 그린 뒤 Execute → 뷰 타깃. 파이프라인: Bloom(절반/1/4 해상도 prefilter + 13탭 down + 텐트 up, scatter 로 섞음) → Uber(색수차, Bloom 합성, 비네트, 노출·화이트 밸런스(LMS)·대비(로그)·컬러 필터·색조·채도, 톤매핑, 감마, 필름 그레인, 디더링) → FXAA. 씬 색이 감마 공간이라 Uber 는 pow 2.2 로 선형화해서 처리하고 되돌린다. Game 뷰는 카메라 Rendering 의 Post Processing / Anti-aliasing(FXAA, SMAA 선택도 FXAA) / Stop NaNs / Dithering 을 따른다(새 카메라는 Post Processing 켬, 기존 씬의 카메라는 저장된 값). Scene 뷰는 툴바 Effects 토글 + Effects 메뉴의 Post Processing 이 켜져 있으면 적용(와이어프레임 제외), 투명 배경이라 Bloom 이 번진 곳은 알파를 올린다. 미구현: Depth of Field, Motion Blur, Lift Gamma Gain, Channel Mixer, Color Curves, Lens Distortion, Bloom Dirt, SMAA, Volume Mask/Trigger.

**Scene 카메라 설정**: Scene 툴바 카메라 아이콘 옆 ▾ = Unity 의 Scene Camera 패널(`SceneToolbar::CameraSettings()`): Field of View(기본 60), Clipping Planes(Near 0.3 / **Far 10000**), Camera Easing, Camera Acceleration, Camera Speed(+Min/Max), Reset, Reset Scene Camera. 사용자별 저장 `%LOCALAPPDATA%/NOVA/Editor/SceneCamera.json`. 비행(우클릭 + WASD, Q/E 아래/위): 20 유닛/초 × Speed, Shift ×4, 가속(최대 ×6), Easing = 속도 보간, 비행 중 휠 = Speed 조절(`SceneViewManager::Update`). 방향광 그림자는 카메라 Far 가 아니라 `FitDirectionalShadow` 범위(40)를 쓰므로 Far 를 늘려도 그림자 해상도는 그대로다.

**Game 뷰**(`Source/Editor/Windows/GameViewEditorWindow.*`, Unity 6 Game 뷰 툴바): [Game ▾(Simulator 비활성)][Display 1~8 ▾][해상도 ▾][Scale 슬라이더 + 배율][Play Focused/Maximized/Unfocused ▾][🐞 Frame Debugger 비활성] … [Mute][단축키][Stats][Gizmos ▾]. 해상도: Free Aspect, 5:4/4:3/3:2/16:10/16:9, Full HD/QHD/4K + 사용자 항목(+ 로 추가: Label, Aspect Ratio/Fixed Resolution, 크기, 우클릭 Delete, 기본으로 1080x1920 포함). 고정 해상도는 그 크기의 렌더 타깃에 그린 뒤 뷰에 맞춰 축소(Scale 최소 = 맞춤 배율, 최대 5x, 휠 확대(Play 중 제외), 가운데 버튼 드래그 이동, 더블클릭 = 맞춤). Display: 카메라 Target Display 가 같은 카메라 중 Priority 최대(`DisplayManager::GetCameraForDisplay`), 없으면 "No cameras rendering". `GetActiveCamera` 도 Game 뷰가 고른 디스플레이의 카메라를 돌려준다. Play Focused = Play 시작 때 Game 탭을 앞으로, Play Maximized = Play 중 도킹 영역 전체를 덮는 창(`DrawMaximized`, EditorGUIManager 가 도킹 영역과 Play 전환을 알려 줌). Stats = Unity Statistics 창: FPS(Play 중), CPU/렌더 시간, Batches(=SetPass), Saved by batching(인스턴싱), Tris/Verts, Screen, Shadow casters, Visible skinned meshes, Animation/Animator 수 — 카운터는 `Source/Graphics/Common/RenderStats.h`(MeshGeometry::Draw/InstancingDraw, 지형, 그림자·스킨 렌더러에서 Game 뷰 그리기 동안만 셈). Mute/단축키/Gizmos 는 상태만 저장(오디오·게임 뷰 기즈모 없음). 설정은 `<프로젝트>/UserSettings/GameView.json`. **깊이 버퍼**: Scene/Game 뷰 그리기는 창 백버퍼 깊이 대신 `EditorApp::ViewDepth(w,h)`(뷰 크기 이상, 커지기만 함)를 쓴다 — 창보다 큰 해상도(1080x1920 등)에서 깊이 버퍼가 작아 아무것도 그려지지 않던 문제.

**오디오**(`Source/Audio/`, XAudio2 — Windows 10 기본 포함, `xaudio2.lib`): `AudioClip`(WAV: PCM 8/16/24/32비트·32비트 float, 경로별 캐시, `FindAll` = Assets + Resources/Packages), `AudioManager`(처음 쓸 때 초기화, 소스 보이스/믹스 행렬(볼륨·밸런스 팬)/피치(최대 3배), 3D: 리스너 = `AudioListener` 또는 Game 뷰 카메라, 로그(min/거리)·선형 감쇠 + 좌우 팬, PlayOneShot 자동 정리, 편집기 일시정지 = `StopEngine`, Play 종료 시 One Shot 정리, Game 뷰 Mute = 마스터 볼륨 0, Stats = 볼륨 미터 레벨(dB)·클리핑·DSP 부하·보이스 수), `AudioSource`(Unity Inspector 그대로: Audio Generator(클립) · Output · Mute · Bypass 3종 · Play On Awake · Loop · Priority(High/Low) · Volume · Pitch · Stereo Pan(Left/Right) · Spatial Blend(2D/3D) · Reverb Zone Mix · 3D Sound Settings; 동작하는 것 = 클립, Mute, Play On Awake, Loop, Volume, Pitch(음수는 절댓값), Stereo Pan, Spatial Blend, Rolloff/Min/Max; 나머지는 값만 저장. API: Play/Stop/Pause/UnPause/PlayOneShot/IsPlaying/GetTime), `AudioListener`(설정 없음, 새 카메라에 자동으로 붙음). GameObject > Audio > Audio Source, Add Component > Audio. Project 창 .wav: 아이콘, 더블클릭 = 미리 듣기, 선택 시 Inspector 에 정보 + Play/Stop. 테스트 효과음은 `python Tools/make_test_audio.py` → `Resources/Packages/Audio/SFX/`(Beep, Coin, Jump, Explosion, Click, BGM_Loop(스테레오 반복)) — 직접 합성. 검사: `NOVA_AUDIO_TEST=<로그>`(클립 읽기, 재생/끝남, 반복, 정지, 피치 2배 = 샘플 2배, One Shot 정리, 출력 레벨, 직렬화, 3D 감쇠·팬 — 25개 PASS).

**Object Picker**(`Source/Editor/ObjectPicker.*`, Unity 의 "Select <타입>" 창): 오브젝트 필드의 ⊙ 가 `ObjectPicker::Open(key, Options{TypeName, Icon, Items, Current, Describe, Preview})` 를 부르고 매 프레임 `Poll(key, path)` 로 선택을 받는다. 떠 있는 창(도킹 안 됨): 검색, Assets/Scene 탭, 아이콘 크기 슬라이더(0 = 목록, 그 외 격자), 👁 패키지 항목 표시/숨김 + 숨긴 개수, None + 항목, 아래 미리보기(이름·경로·Describe, Preview 가 있으면 ▶). 한 번 클릭 = 바로 할당, 더블클릭/Enter = 할당 후 닫기, Esc/X = 닫기, 위/아래 화살표 이동. 현재 사용처: Audio Source 의 Audio Generator, Volume Profile 필드(Volume, Project Settings). **주의**: ImGui 팝업을 마우스 위치(화면 오른쪽 끝)에서 열면 메인 창 밖으로 나가 별도 OS 창이 되어 보이지 않을 수 있다 — 팝업은 위치를 창 안으로 지정할 것.

**README**: 저장소 첫 화면 `README.md`(한국어, 기능 표·스크린샷·빌드·실행·단축키·폴더 구조·사용 라이브러리). 스크린샷은 `docs/images/*.webp`(1280px). 영상용 전체 컷은 `Showcase/`(git 제외, 목록.md).

**창 크기 / 입력 좌표**: `App::OnResize` 가 `IDXGISwapChain::ResizeBuffers` 로 백버퍼를 창 크기에 맞춘다(예전에는 처음 크기의 백버퍼를 늘려 그려서 창 크기가 바뀌면 클릭 위치가 어긋났다). 요청한 창 크기가 작업 영역(작업 표시줄 제외)보다 크면 작업 영역에 맞춰 최대화로 연다(`s_StartMaximized`). **COM**: `Main.cpp` 가 `CoInitializeEx(APARTMENTTHREADED)` 를 한다 — 없으면 WIC PNG 디코딩이 0x80004002 로 실패해 Hub 가 만들어 둔 DDS 캐시가 없는 텍스처/아이콘이 자홍색으로 나온다.

**시작 로딩 창**(`Source/Platform/LoadingScreen.*`): 에디터 시작 시 별도 스레드의 GDI 창(600x330, 아이콘/제목/프로젝트 이름/현재 작업/진행 막대/%)을 띄우고, 메인 창은 두 번째 프레임이 그려질 때까지 숨긴다(`App::_deferredShow`, 숨긴 동안은 `_appPaused` 무시). 단계: 그래픽 장치 → 설정 → 에디터 UI → 씬 로드 → 셰이더(22개, `ShaderCache::CompileEffect` 가 캐시/컴파일을 보고) → 텍스처 → 창 준비. FBX 가져오기는 `ResourceManager::LoadMeshFile` 에서 "Importing ..." 표시. 한 작업이 1초 넘게 걸리면 막대 위로 빛이 흐른다. 셰이더 캐시(`ShaderCache`)는 **파일별 의존성 지문**(자기 파일 + `#include` 로 끌어오는 파일들의 수정 시각/크기, 재귀)을 키로 쓴다 → .fx 하나를 고치면 그 파일과 그것을 include 하는 이펙트만 다시 컴파일된다(Editor.log 에 "cache miss X (depends on: ...)"). 시작 시 `ShaderCache::PrecompileParallel` 이 오래된 이펙트를 CPU 코어 수만큼 병렬 컴파일한다(목록은 `EditorApp::Init` 의 kShaderFiles — 이펙트를 추가하면 여기에도 넣을 것, 빠져도 동작은 한다). 측정: 캐시 없음 12.0초 → 7.3초, 변경 없음 약 2초.

**계층(부모/자식)**: `GameObject::SetParent(parent, worldPositionStays = true)` — Hierarchy 드래그는 월드 위치/회전/크기를 유지(Unity 와 동일, `Transform::SetWorldPose`), 새로 만들기/복제/자식으로 붙여넣기는 로컬 값 유지(false). 자기 자손 밑으로는 옮길 수 없다. 씬의 전체 목록(`GetAllGameObjects`, 렌더/업데이트/물리 대상)에는 자손까지 등록된다(`Scene::RegisterGameObjectTree`, 씬 로드·붙여넣기·SetParent 에서 호출). 부모를 지우면 자손도 함께 제거된다. 월드 위치/회전/크기(`GetPosition/GetRotation/GetScale`)는 행렬 분해 대신 계층을 따라 합성한다(비균일 스케일 부모 아래 기울어진 행렬에서 Decompose 가 실패하던 문제). 개발용 `NOVA_PARENT_TEST=<로그 파일>`.

**씬 저장 / 변경 표시**: 저장은 모두 `SceneManager::SaveCurrentScene(saveAs)` 로 모인다(Ctrl+S, Ctrl+Shift+S, File > Save/Save As, Hierarchy 씬 메뉴). Play 모드에서는 저장하지 않고 Console 에 안내한다(Unity 와 동일). 저장 성공/실패는 Console 에 기록. 변경 여부는 씬 JSON 해시를 저장 시점과 0.25초마다 비교(`IsCurrentSceneDirty`)해 Hierarchy 씬 행과 창 제목에 `SampleScene*` 로 표시한다. Hierarchy 씬 행은 Unity 처럼 [▼][씬 아이콘] 파일 이름(굵게) … [⋮] 이고, 우클릭/⋮ 메뉴에 Save Scene, Save Scene As, Save All, Discard changes(파일에서 다시 읽기), GameObject 생성이 있다. 다른 이름 저장은 프로젝트 밖 경로도 절대 경로로 저장된다.

**Hierarchy 컨텍스트 메뉴**: `Source/Editor/GameObjectMenu.*`(밝은 회색 Unity 컨텍스트 메뉴 스타일 + 생성 메뉴 트리)를 Hierarchy 우클릭, `[+]` 버튼, 상단 GameObject 메뉴가 공유한다. Cut/Copy/Paste/Duplicate/Delete/Rename(F2, 더블클릭) 동작, 단축키(Ctrl+C/X/V/D, Del, F2, Ctrl+Shift+N). 이 엔진이 지원하지 않는 항목(2D Object, Effects, Audio, UI 등)은 회색 비활성으로 표시한다. 다중 선택(Select All/Invert/Select Children)은 미구현.

**Scene 뷰 툴바**: `Source/Editor/SceneToolbar.*` — 상단 바(Pivot/Local, 그리드 크기, 스냅, 와이어/셰이디드, 라이팅, 디버그(Instancing), 2D, 오디오, 이펙트, 가시성, 그리드, 카메라, 기즈모)와 뷰 위에 떠 있는 도구 팔레트(Hand/Move/Rotate/Scale/Rect/Transform). 실제로 동작하는 것: Wireframe/Shaded(=RenderManager::WireFrameMode), Instancing, Grid 표시, Gizmos 표시, Reset Scene Camera, Hand 도구(좌클릭 드래그 이동), 마우스 휠 줌, Q/W/E/R/T/Y 도구 단축키. 나머지 토글(2D/Audio/Effects/Scene Lighting/Visibility 등)과 Pivot/Local/스냅은 상태만 저장한다. WASD 카메라 이동은 Unity 처럼 우클릭을 누른 동안만 동작한다. 아이콘은 `Tools/icons_scene_toolbar.py` → `make_icons.py`.

**Scene 조작 핸들**: `Source/Editor/SceneGizmoTools.*` — Move(축 화살표, 평면 사각형, 가운데 화면 평면 이동), Rotate(카메라 쪽 절반만 보이는 축 링 + 바깥 시선 축 링, 드래그 중 부채꼴), Scale(로컬 축 + 가운데 균일 크기), Rect(카메라를 향한 로컬 평면의 사각형: 모서리/변 드래그 시 반대쪽 고정, 안쪽 드래그 이동), Transform(이동+회전+균일 크기). Pivot/Center, Local/Global, Ctrl(또는 툴바 Grid Snapping) 스냅(이동 = Grid Size, 회전 15°, 크기 0.1) 반영. Scene 뷰 클릭 선택(메시 삼각형 레이캐스트, 메시 없는 오브젝트는 화면 위치 근처 클릭), 빈 곳 클릭 시 선택 해제, 가운데 버튼/Hand 패닝, Alt+좌드래그 궤도, 휠 줌, F 포커스. 기존 `Gizmo::DrawTransformHandler` 는 더 이상 호출하지 않는다. `Gizmo.cpp` 의 화면 좌표는 `SceneViewOverlay::GetViewRect` 로 이미지 영역(툴바 제외)을 쓴다. Undo 는 아직 없다. 개발용 `NOVA_TOOL=0..5` 로 시작 도구 지정.

**좌표계 / 회전 규약 (Unity 와 동일)**: 왼손 좌표계(+X 오른쪽, +Y 위, +Z 앞), 행 벡터 행렬(DirectX). `Transform` 회전의 기준 값은 **쿼터니언(`m_LocalRotation`)** 이고 오일러 각은 표시/입력용이다. 오일러 순서는 Unity 의 `Quaternion.Euler` 와 같은 Z → X → Y (`XMQuaternionRotationRollPitchYaw`), 역변환은 `Transform::ToEulerRadians`(atan2 기반, 짐벌 락 처리). 물리/기즈모가 쿼터니언을 넣으면 Inspector 오일러 값은 이전 값과 가장 가까운 표현으로 이어진다(`ToEulerAnglesNear`, Unity 의 오일러 힌트와 같은 동작). 월드 오일러(`GetEulerAngle`)는 0~360. 이전 규약으로 저장된 씬은 쿼터니언을 기준으로 읽고 표시 값만 다시 계산한다. 라이트 방향은 Transform 의 forward(+Z) 를 쓴다(이전에는 오일러 라디안 값을 방향 벡터로 넘기던 버그). Jolt 쿼터니언/벡터는 같은 숫자를 그대로 주고받는다(두 쪽 모두 q·v·q* 규약이라 손 방향 변환이 필요 없음).

**물리 (Jolt Physics v5.6.0)**: `ThirdParty/JoltPhysics`(MIT, `Jolt/` 소스 + `Build/CMakeLists.txt` 만 포함, `VERSION.txt`)를 CMake `add_subdirectory` 로 빌드해 링크한다(동적 런타임 /MD, 엔진과 같은 Debug /O2, RTTI·예외 ON, 디버그 렌더러·프로파일러·GPU 컴퓨트 OFF). 레거시 `NovaEngine/NovaEngine.vcxproj` 는 Jolt 를 링크하지 않으므로 **빌드는 `build.bat`(CMake)만 지원**. `Source/Physics/PhysicsManager.cpp` 만 Jolt 헤더를 쓰며 Windows.h 의 min/max 매크로보다 먼저 Jolt 를 포함해야 해서 **PCH 를 끈 파일**이다(그래서 `REGISTER_COMPONENT` 는 `inline` 변수로 바꿨다). 동작: Play 시작(Scene::Enter, Awake 뒤·Start 전)에 Collider/Rigidbody 가 있는 GameObject 마다 바디 생성 — Rigidbody 없음=Static, Is Kinematic=Kinematic(MoveKinematic 으로 Transform 추종), 그 외 Dynamic. 자식 콜라이더는 가장 가까운 부모 Rigidbody 의 복합 형상에 들어간다. 고정 간격 0.02초마다 FixedUpdate → Transform 변경 반영(사용자가 옮기면 텔레포트) → 시뮬레이션 → Dynamic 결과를 Transform 에 기록 → 이벤트. 매 스텝 형상/설정 해시가 바뀌면 바디를 다시 만든다(런타임 AddComponent/Destroy/값 변경 대응, 속도 유지). 트리거는 접촉을 `ContactSettings::mIsSensor` 로 바꿔 통과시키고 `OnTriggerEnter/Stay/Exit`, 일반 접촉은 Dynamic 이 끼어 있을 때만 `OnCollisionEnter/Stay/Exit`(콜라이더의 GameObject 와 Rigidbody 소유자 양쪽에 전달, 잠든 바디 쌍은 유지). Rigidbody: Mass, Linear/Angular Damping, Use Gravity, Is Kinematic, Interpolate(보간 구현), Collision Detection(Continuous 계열=LinearCast), Constraints(Freeze → Jolt AllowedDOFs), Info; API: `GetVelocity/SetVelocity`, `Get/SetAngularVelocity`, `AddForce(force, ForceMode)`, `AddTorque`, `AddForceAtPosition`, `MovePosition/MoveRotation`, `Sleep/WakeUp/IsSleeping`. `PhysicsManager::Raycast(origin, dir, hit, maxDistance, hitTriggers)`(Unity Physics.Raycast). 기본 마찰 0.6, 반발 0(Unity 기본 재질), 마찰/반발 결합 = 평균(Unity 기본), 중력 -9.81, 최대 각속도 50 rad/s(Unity 기본), 침투 허용치 5mm. 미구현: Physics Material 에셋, 레이어 충돌 행렬, Joint, CharacterController, 충돌 정보 객체(Collision: 접촉점/충격량 — 이벤트 인자는 상대 Collider*), Automatic Center Of Mass/Tensor 끄기, Extrapolate(보간과 동일 처리 안 함), 런타임 스케일 변경 외 부모-자식 Rigidbody 조합의 세부 동작.

**Add Component 팝업**: `Source/Editor/AddComponentMenu.*` — Unity 처럼 검색창 + "Component" 헤더 + 카테고리(Animation, Mesh, Miscellaneous, Physics, Rendering, Scripts) → 하위 목록(아이콘). 검색어는 전체 카테고리에서 찾고 ↑/↓·Enter·←/Backspace 로 조작한다. 등록 이름 → 표시 이름/카테고리/아이콘 표는 `kKnown`, 표에 없는 등록 타입(REGISTER_SCRIPT 스크립트)은 Scripts 로 들어간다. Rigidbody·MeshFilter·MeshRenderer·Camera·Light 등은 이미 있으면 회색(중복 불가). 아래 공간이 부족하면 버튼 위로 연다.

**Layer Overrides**: Rigidbody 와 모든 Collider 에 Include/Exclude Layers(마스크 드롭다운 `UnityGUI::MaskField`, 비트 = `UnityGUI::LayerNames()` 순서)와 Collider 의 Layer Override Priority. 물리에서 `OnContactValidate` 로 상대 레이어가 Exclude 에 있으면 접촉을 만들지 않는다(설정이 엇갈리면 Priority 가 높은 쪽). 레이어 충돌 행렬이 아직 없어 Include 는 실질 효과가 없다.

**MeshFilter / MeshRenderer / Collider Inspector**: `MeshFilter`(제목 "<메시> (Mesh Filter)", 내장 도형 선택 팝업), `MeshRenderer`(Materials 리스트, Lighting/Probes/Additional Settings/2D — Cast Shadows Off 만 실제 적용), `BoxCollider`/`SphereCollider`(Edit Collider, Is Trigger, Provides Contacts, Material, Center, Size/Radius) 를 Unity Inspector 와 같은 모양으로 구현했고 재질 패널(`UnityGUI::MaterialPanel`)이 컴포넌트 아래에 표시된다. Capsule Collider(Radius/Height/Direction), Mesh Collider(Convex, Cooking Options, 메시는 MeshFilter 에서)도 있다. 콜라이더 와이어는 Unity 처럼 **선택된 오브젝트만** 연두색으로 그린다. 기본 도형 콜라이더는 Unity 와 동일(Cube=Box, Sphere=Sphere, Capsule/Cylinder=Capsule, Plane/Quad=Mesh).

**폰트**: `EditorTheme::FontFile()` 가 `ProjectSetting/fonts/Pretendard-Regular.ttf` + `Pretendard-SemiBold.ttf`(또는 Bold)를 찾아 있으면 사용하고, 없으면 Segoe UI + 맑은 고딕으로 대체한다. 현재 저장소에는 `Pretendard-Regular.otf` / `Pretendard-SemiBold.otf`(SIL OFL, 라이선스 `Pretendard-LICENSE.txt`, 출처 github.com/orioncactus/pretendard)가 들어 있어 Pretendard 가 적용된 상태이며 .otf(CFF)도 ImGui 의 stb_truetype 로 렌더링된다. 본문 14px, 글자를 또렷하게 하려고 오버샘플링 1 + 픽셀 스냅 + RasterizerMultiply 1.2 이며, UnityGUI 의 텍스트 y 좌표와 프레임 패딩은 정수 픽셀로 맞춘다(반 픽셀에 걸리면 흐려짐). 폰트 파일을 추가/교체하면 `nova_layout_v2.ini` 삭제는 필요 없다.

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
│  ├─ Physics/     PhysicsManager (Jolt 백엔드), Octree
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
- 우선 후보: ImGuizmo로 Scene 기즈모 개선, 에셋 파이프라인(메타 파일/GUID), 프리팹 편집 모드·중첩 프리팹, 셀렉션 하이라이트, Joint/CharacterController/Physics Material.

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
