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
| 빌드 | CMake(VS 2022 생성기) → `build.bat` (Debug x64, `/O2` 적용) / `build.bat release` (Release: D3D 디버그 레이어 끔·셰이더 최적화·`/Zi`) |
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

**Undo / Redo**(`Source/Editor/UndoSystem.*`, Ctrl+Z / Ctrl+Y·Ctrl+Shift+Z, Edit 메뉴 "Undo <이름>"): 조작이 끝난 순간(위젯 비활성화, 마우스/키 뗌, `RequestCheck`, 다음 프레임에 한 번 더) 씬 JSON 을 직전 확정본과 비교해 바뀌었으면 한 단계로 기록한다 → Inspector 값, 이동/회전/크기 핸들(이름은 `SetActionName`), 생성/삭제/복제/부모 변경/컴포넌트 추가·제거가 모두 같은 방식. 되돌리기 = `SceneManager::RestoreSceneState(json)`(씬 재생성, 경로 유지, 선택과 Hierarchy 펼침은 **GameObject fileID** 로 복원). `GameObject::m_FileID`(저장되는 64비트 고유 ID, "fileID", 복제/복사 붙여넣기는 `RegenerateFileIDs`, 잘라내기는 유지) — 프리팹도 이 ID 를 쓴다. 에셋: `Undo::WatchAsset(key, label, capture, restore)` 를 편집 중인 창이 매 프레임 부른다(Animator 창·상태/전이 Inspector = 컨트롤러 `ToJsonString/ApplyJson`, 재질 Inspector = `UMaterial` json + `ReloadTextures`). 지형: 브러시 한 획 = 바뀐 사각형 영역의 높이/컨트롤만 저장, Flatten All/레이어 추가·제거/높이맵 해상도 = 전체 스냅샷(`TerrainData::RestoreState`), 크기 = 값만. Play 중엔 기록 안 함, 다른 씬을 열면 기록 비움(Play/Stop 은 유지). 최대 300 단계 / 256MB. **확정 캡처는 루트 GameObject 단위 캐시**: 씬 JSON = `{"rootGameObjects":[...]}` 를 루트마다 dump 한 문자열을 이어 붙여 만든다(`json(scene).dump()` 와 바이트까지 같음). 조작이 끝날 때는 (1) 지난 확정 뒤 선택된 적 있는 오브젝트의 루트 (2) 서명(오브젝트·컴포넌트 포인터, fileID, 이름, 활성, 컴포넌트 켜짐, 로컬 위치/회전/크기 — 직렬화 없이 계산)이 바뀐 루트 (3) 확정마다 돌아가며 8 개만 다시 직렬화한다. Undo/Redo 직전(`Commit(true)`)·씬 바뀜은 전체 직렬화(놓친 변경이 사라지지 않게). **스냅샷 = 루트 (fileID, 문자열 shared_ptr) 목록**이고 문자열은 캐시·기록이 같이 쓴다(기록 메모리 = 바뀐 루트만: 물체 1600 씬 한 단계 2.7 MB → 78 KB). **되돌리기는 부분 복원**: 지금·목표 스냅샷을 루트 fileID 로 맞춰 다른 루트만 `Scene::DestroyGameObject`(Hierarchy 삭제와 같은 경로) 후 JSON 에서 다시 만들고 `Scene::SetRootOrder` 로 순서를 맞춘다. 선택·Hierarchy 펼침은 fileID 로 이어 가고, 절반 넘게 다르면 `RestoreSceneState` 로 씬 전체. 오브젝트 사이 참조는 fileID 로 찾으므로(`FindByFileID`) 다시 만든 루트를 가리켜도 안전. Ctrl+Z(물체 1600, Release): 확정 33~41 ms + 복원 1~2 ms (예전 Debug 약 1.7 s). `CommittedSceneHash` 는 스냅샷이 바뀔 때만 이어 붙여 해시(저장 시점 `json(scene).dump()` 해시와 같은 값). 물체 1600 개: 확정 450 ms → 약 4 ms(클릭마다 멈추던 것). 위 둘에 안 걸리는 변경(선택 안 한 오브젝트의 Transform 외 값을 도구가 바꿈)은 돌아가는 검사나 다음 Undo 때 잡힌다. `NOVA_DEV_PROFILE=1` 이면 확정마다 `[Undo] capture N ms (다시 직렬화한 루트 / 전체)` 기록. **주의: 매 프레임 값이 바뀌는 필드를 씬에 저장하면 열자마자 `*` 가 붙고 Undo 뒤 클릭마다 가짜 단계가 생겨 Redo 가 지워진다** — 그래서 Camera `aspect` 는 저장하지 않는다. 크기 0 인 뷰(숨김/배치 전)에서는 렌더 타깃을 만들지 않고 종횡비 0 은 무시(투영 행렬 assert 방지).

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

**C# 스크립팅**(Unity 의 Mono 스크립팅 대응):
- 관리 코드: `ScriptCore/`(net8.0 `NovaScriptCore.dll`, 네임스페이스 `NovaEngine`) — Unity 와 같은 이름의 API: `MonoBehaviour`(Awake/OnEnable/Start/Update/LateUpdate/FixedUpdate/OnDisable/OnDestroy, OnCollision*/OnTrigger*, 코루틴(null, WaitForSeconds(Realtime), WaitUntil/While, 중첩), Invoke/InvokeRepeating, IEnumerator Start), `Object`(파괴 후 `== null`, Destroy(지연), Instantiate(pose/parent), FindObjectsOfType), `GameObject`(Find/FindWithTag/CreatePrimitive/SetActive/tag/Get·AddComponent/SendMessage), `Transform`(position/rotation/euler/local/scale/forward…, Translate/Rotate/RotateAround/LookAt, parent/child), 수학(Vector2/3/4, Quaternion — Euler 는 Unity 와 같은 ZXY, Color, Mathf, Random), `Time`, `Input`(KeyCode, 마우스, GetAxis Horizontal/Vertical/Mouse X…, GetButton Jump/Fire1…), `Debug`(Log/Warning/Error/Exception → Console, 스택+파일:줄), `Rigidbody`, `AudioSource`/`AudioClip`, `Animator` 파라미터, `Collider`/`Collision`, `Physics.Raycast`, `Camera.main`, `Screen`, 속성(SerializeField, HideInInspector, Range, Header, Tooltip, Space).
- 호스팅: `Source/Scripting/ScriptEngine.*` — `%ProgramFiles%\dotnet\host\fxr\<최신>\hostfxr.dll`(또는 DOTNET_ROOT)을 직접 찾아 `Binaries/Scripting/NovaScriptCore.runtimeconfig.json` 으로 런타임을 띄우고 `[UnmanagedCallersOnly]` 진입점(`NovaEngine.Interop.Bridge`)을 받는다. 네이티브 → C# 함수 표는 `ScriptBindings.cpp` 의 `NativeApiTable` = `ScriptCore/Interop/NativeApi.cs` 와 **순서·시그니처가 반드시 같아야** 한다(크기가 다르면 Initialize 가 거부하고 Editor.log 에 표시). 오브젝트는 GameObject fileID(64비트)로 가리킨다. 헤더는 `Libraries/Include/DotNet`(MIT).
- 컴파일: 프로젝트 루트에 `Assembly-CSharp.csproj`(Assets/**/*.cs, NovaScriptCore 참조)를 만들고 백그라운드 스레드에서 `dotnet build`(출력 `Library/ScriptAssemblies`, 첫 빌드 ~2.5초, 이후 ~1.3초). 1초마다 .cs 목록·시각 지문을 비교해 바뀌면 다시 빌드, 성공하면 수집 가능한 AssemblyLoadContext 를 내리고 새로 읽는다(바이트로 읽어 DLL 을 잠그지 않음, pdb 로 줄 번호). Play 중에 바뀌면 Play 가 끝난 뒤 읽는다. 지문이 같으면 시작 시 컴파일 없이 바로 읽는다(~60ms). 오류/경고는 `file(line,col): error CSxxxx` 를 파싱해 Console 에 Compile 항목으로(다시 빌드하면 지움). 오류가 있으면 `CanEnterPlayMode()` 가 Play 를 막는다(모든 Play 진입점: 툴바, Edit 메뉴, NOVA_AUTOPLAY). 컴파일 중이면 끝날 때까지 기다린다.
- 컴포넌트: `CSharpScript`(C++ `MonoBehaviour` 상속 → 물리 충돌 전달을 그대로 받음) — 편집 중에는 클래스 이름 + 필드 값 JSON 만, Play 중 Awake 때 C# 인스턴스 생성(GCHandle). 업데이트 중 추가되는 것은 `GameObject::QueueComponent` 로 프레임 끝에 목록에 넣고(반복 무효화 방지), C# 에서 만든/복제한 오브젝트도 캐시에 먼저 넣고 씬에는 LastUpdate 에 추가한다. Inspector: 제목 "Class Name (Script)", Script 행(클릭 = Project 선택, 더블클릭 = 편집기), 필드(float/int(+Range 슬라이더)/bool/string/Vector2·3·4/Color/enum/GameObject·Transform(Hierarchy 끌어 놓기)/AudioClip(Object Picker)/그 외 "not supported"), Play 중에는 실시간 값 표시·수정(끝나면 원래대로). 클래스가 없으면 Unity 와 같은 경고.
- 에디터: Project 창 Create > Scripting > MonoBehaviour Script(템플릿, 바로 이름 바꾸기 → 파일 이름을 바꾸면 클래스 이름도 바뀜), .cs 아이콘, 더블클릭 = `ScriptEngine::OpenInCodeEditor` → Preferences 에서 고른 편집기(기본 NOVA Code, 아래), .cs 선택 시 Inspector 에 코드 미리보기 + Open. Hierarchy 행에 .cs 를 놓으면 컴포넌트 추가. Add Component > Scripts 에 클래스 목록. 입력은 Game 뷰가 포커스일 때만(Unity 와 같음), 마우스 좌표는 게임 화면 픽셀(왼쪽 아래 원점).
- Console(`ConsoleEditorWindow`, Unity 식): Clear/Collapse/Clear on Play/Error Pause/Open Editor Log, 검색, ⓘ⚠⛔ 개수 토글, 두 줄 행(메시지 + 스택 첫 줄/파일), 아래 상세(스택의 "(at 파일:줄)" 클릭 = 열기), 더블클릭 = 열기. `Debug::Write(type, msg, stack, file, line, compile)` — Console 내용은 Editor.log 에도 `[Console]` 로 남는다.
- 빌드: CMake 가 `dotnet` 을 찾으면 `NovaScriptCore` 타깃(dotnet build → Binaries/Scripting)을 엔진 빌드에 포함. .NET 이 없으면 스크립팅만 꺼지고(Console 경고) 엔진은 동작.
- 검사: `NOVA_SCRIPT_TEST=<로그> NOVA_AUTOPLAY=1` + 프로젝트에 `ScriptSelfTest.cs`/`Helper.cs`(검사 코드가 C# 쪽에 있음, 세션 scratchpad 의 ScriptTest 프로젝트 참고) — 29개 PASS: 수명 순서, Inspector 값 주입, private SerializeField, GameObject 참조, Transform/Euler/Quaternion, Find/GetComponent/AddComponent, Instantiate(복제본의 스크립트 포함), CreatePrimitive, Destroy(지연), 코루틴, Invoke, 예외 뒤에도 Update 계속, Rigidbody 속도, OnCollisionEnter.

**NOVA Code (내장 C# IDE)** — IDE 코드는 커질 것이므로 전부 `Source/Editor/NovaCode/` 에 둔다(CMake include 경로에 추가됨):
- `CodeEditor.*`: 편집 위젯(ImGui 로 직접 그림). 줄 단위 `std::string`(UTF-8) + 커서/앵커(바이트 위치), 스냅샷 Undo(입력/삭제는 1.5초 안에서 묶음, 500개), 자동 들여쓰기·`}` 내어쓰기·괄호/따옴표 자동 닫기(덮어쓰기), 스마트 Home, Ctrl+/ 주석, Ctrl+D 복제, 선택 없는 Ctrl+C/X = 줄 전체, 붙여넣기의 탭 → 공백, 찾기/바꾸기/줄 이동 바, 자동 완성 목록(Foreground 드로리스트), 마커(물결 밑줄 + 여백 점 + 불투명 hover 툴팁 — 창 스타일의 PopupBg 가 투명이라 직접 Push 해야 함), 줄 끝 오류는 마지막 단어/기호에 밑줄. 글꼴은 `Fonts[2]`(Consolas + 맑은 고딕 한글, `EditorGUIManager::Init`). 포커스일 때 `PlatformImeData` 로 한글 IME 창을 커서에 두고 `WantTextInputNextFrame`/`SetNextFrameWantCaptureKeyboard` 로 다른 창의 단축키를 쉬게 한다. `CodeEditor::IsAnyEditorFocused()`.
- `CSharpLanguage.*`: 토큰 분리(키워드/제어/타입/메서드/문자열(@ $ 포함)/숫자/주석/전처리), 줄 끝 블록 주석 상태, 자동 완성 후보. 엔진 API 는 `Bridge.GetApiJson`(NovaEngine 네임스페이스 공개 타입의 멤버 이름+타입, 선택 진입점 — 없으면 키워드/문서 단어만) → `ScriptEngine::GetApiJson`. `.` 앞 단어의 타입 = 타입 이름 자체 → 문서의 `Type name` 선언 → 엔진 멤버의 타입(transform → Transform) → 모름(전체 멤버).
- `NovaCodeWindow.*`: EditorWindow("NOVA Code", 처음엔 닫힘, 처음 열 때 Scene 창의 DockId 에 탭으로). Explorer(Assets 트리 2초마다 스캔, 새 스크립트), 탭(●=저장 안 함, 가운데 클릭/Ctrl+W 닫기, 저장 확인 모달), 도구 모음(저장/모두 저장/Undo/Redo/찾기/바꾸기/줄 이동, 컴파일 상태, ⚙), 상태 표시줄(오류/경고 수 → 클릭 = 첫 오류, Ln/Col, Spaces, UTF-8(BOM 유지), CRLF/LF 유지). 저장 = 파일 쓰기 + `ScriptEngine::RequestRecompile()`. 1초마다 디스크 변경 확인(저장 안 한 변경 없으면 다시 읽기, 있으면 경고 줄). 마커는 `Debug::Entries()` 의 Compile 항목 중 경로가 같은 것(메시지 `(줄,열)` 에서 열). `NovaCodeWindow::IsFocused()` 면 `SceneManager::HandleSaveScene`(Ctrl+S 씬 저장)과 `UndoSystem` 단축키가 양보한다. 창 여백은 `EditorWindow::WindowPaddingOverride()`(새 가상 함수), `BeforeBegin()`/`ExtraWindowFlags()` 도 새로 추가.
- `ExternalScriptEditor.*`: Unity 의 External Script Editor. EditorPrefs 키 `ExternalScriptEditor.Kind/Path/Name/Args`. 설치 탐지: VS Code(사용자/시스템 설치), Visual Studio(`vswhere -all -prerelease -format json` 의 productPath/displayName/productDisplayVersion), Rider. 실행 인자는 README 표 참고. 실패하면 기본 프로그램.
- `Source/Editor/EditorPrefs.*`: 사용자별 설정(`%LOCALAPPDATA%\NOVA\Editor\EditorPrefs.json`, 바꿀 때마다 저장). `Source/Editor/Windows/PreferencesWindow.*`: Edit > Preferences...(External Tools / NOVA Code(글꼴 크기, 탭 크기, 자동 완성, 괄호 자동 닫기)).
- 테스트: scratchpad ScriptTest 프로젝트의 `Assets/PlayerMover.cs` 를 Project 창에서 더블클릭 → 입력 → Ctrl+S → 약 1초 뒤 밑줄/상태 표시줄 확인. **테스트로 사용자 EditorPrefs 를 바꾸지 말 것**(Preferences 에서 편집기를 고르면 사용자 설정이 바뀐다).
- `Logs/Editor.log` 는 이제 `_wfsopen(_SH_DENYWR)` 로 열어 에디터가 켜져 있어도 다른 프로그램이 읽을 수 있다.

**UI (UGUI, `Source/UI/`)** — Unity 의 Canvas UI:
- 컴포넌트: `RectTransform`(anchorMin/Max, anchoredPosition, sizeDelta, pivot; Inspector = 기준점 프리셋 팝업(Shift 피벗, Alt 위치), Pos X/Y/Z·Width/Height ↔ 늘어나는 축은 Left/Right/Top/Bottom, Anchors/Pivot 을 바꾸면 사각형 제자리 유지), `Canvas`(Screen Space - Overlay 만 그림, Sort Order, Target Display), `CanvasScaler`(Constant Pixel Size / Scale With Screen Size — Unity 와 같은 log2 식), `GraphicRaycaster`(있어야 클릭), `EventSystem`(씬에 켜진 것이 있어야 입력), `UIImage`(클래스 이름이 DirectX::Image 와 겹쳐 UIImage, 표시 "Image": Simple/Preserve Aspect, Sliced(9 조각), Tiled(=Simple), Filled Horizontal/Vertical/Radial 90·180·360), `Text`(글자 = `UIFont`), `Button`(Color Tint, Fade Duration, On Click () 목록 = 대상 fileID + "클래스.메서드"(인자 없음/int/float/string/bool) 또는 GameObject.SetActive). 공통 부모 `UIGraphic`(색, Raycast Target, Button 의 틴트).
- 좌표: RectTransform 이 매 프레임(`UISystem::Update`, App 루프의 ScriptEngine::Update 뒤) 레이아웃을 계산해 **Transform 로컬 위치(x, y)를 쓴다**. Transform 이 밖에서(이동 도구, C# transform.position) 바뀌면 마지막으로 쓴 값과의 차이를 anchoredPosition 에 더한다(역동기화). 회전/크기는 Transform 값. 루트 캔버스 Transform = 화면 가운데·배율(scale factor), 캔버스 월드 단위 = 게임 화면 픽셀(y 위). 캔버스 아래 모든 오브젝트는 RectTransform 을 갖게 된다(Unity 와 같음). UI 오브젝트의 Inspector 는 Transform 을 숨기고 Rect Transform 만. Transform/RectTransform 저장 시 화면 크기에 따라 바뀌는 값(루트 캔버스 크기, 레이아웃 위치)은 크기와 무관한 값으로 저장 → 창 크기만 바뀌어도 씬이 "*" 되지 않음.
- 그리기: `UIRenderer`(동적 VB/IB, 텍스처별 배치, `Shaders/42. UI.fx` — UITech(깊이 없음), UISceneTech(깊이 비교)). Game 뷰 = `GameViewEditorWindow::RenderScene` 끝에서 `UISystem::RenderGameView`(픽셀→NDC 직교). Scene 뷰 = `EditorApp::_Editor_OnSceneRender` 끝에서 `RenderSceneView`(에디터 카메라 VP, Far 무한 투영 — 캔버스가 매우 커서). 캔버스 테두리/선택 사각형은 `Canvas/RectTransform::OnDrawGizmos` 가 `SceneViewOverlay::DrawLine`(1px). F 포커스와 Rect 도구는 `SceneGizmoTools::LocalBounds` 가 RectTransform 사각형을 쓰고, Rect 도구는 UI 면 크기(sizeDelta)를 바꾼다.
- 글꼴(`UIFont`): (글꼴 파일, 픽셀 크기)별 `ImFontAtlas`(에디터 UI 와 별개) — 처음 보는 글자가 나오면 넣어 다시 굽는다(한글도 쓰는 글자만). 픽셀 크기 = fontSize × 캔버스 배율(선명). 기본 = `ProjectSetting/fonts/Pretendard-*.otf`(없으면 맑은 고딕), Assets 의 .ttf/.otf 선택 가능. 스프라이트(`UISprites`): 내장 "builtin:UISprite"(둥근 사각형+테두리, 9-slice 10px), "builtin:Background", "builtin:Knob"(실행 중 생성) + Assets 이미지.
- 입력: Play 중 + EventSystem 있음 + Game 뷰 포커스일 때, `GameViewEditorWindow::MouseToGame` 좌표로 위 캔버스(Sort Order 큰 것)·나중에 그린 그래픽부터 맞춤 → 부모 쪽 첫 Button 이 Hover/Pressed, 같은 버튼 위에서 떼면 Click(저장된 호출 → C# 리스너 순, Unity 와 같음).
- 메뉴: GameObject > UI (Canvas) / UI: Image, Text, Panel, Button, Toggle, Slider, Scroll View, Input Field, Canvas, Event System (Dropdown 등 나머지는 회색). 캔버스가 없으면 Canvas(+Scaler+Raycaster, 레이어 UI) + EventSystem 을 만든다. Add Component 에 UI / Layout / Event 분류.
- C#: `NovaEngine.RectTransform`(: Transform), `Canvas`, `Rect`, `Sprite`(경로), `TextAnchor`, `FontStyle`, `NovaEngine.Events.UnityEvent`, `NovaEngine.UI.Graphic/Image/Text/Selectable/Button`(onClick = GameObject 별 정적 레지스트리 `UIEvents` — 래퍼가 GetComponent 마다 새로 만들어지므로; Play 시작/끝·어셈블리 내리기 때 비움), `TMPro.TextMeshProUGUI/TMP_Text`(= Text). 네이티브 표 끝에 `UI_GetVec/UI_SetVec/UI_GetString/UI_SetString`(번호표는 NativeApi.cs 주석 = `UIScriptBindings.cpp`). 관리 진입점(선택): `InvokeMethod`(On Click 저장 호출), `InvokeUIEvent`(kind 0 onClick, -1 비우기). `GetClassesJson` 에 `methods`(On Click 에서 고를 수 있는 public void 메서드). 스크립트가 UI 컴포넌트를 AddComponent 하면 RectTransform 도 같은 프레임 대기열에 넣고, `FindComponent`/UI 바인딩은 대기열(`GameObject::GetPendingComponents`)도 찾는다 → AddComponent<Image>() 직후 GetComponent<RectTransform>() 가 된다.
- 검사: scratchpad ScriptTest 의 `Assets/UITest.cs`(코드로 체력 바/점수 Text 를 만들고 onClick 리스너) — Canvas 에 붙이고 Play → Game 뷰 버튼 클릭 → Console "Button clicked N", On Click 저장 호출 "Hello NOVA".
- 상호작용 컴포넌트(2차): 공통 부모 `UISelectable`(Interactable, Transition Color Tint 색 5개, Target Graphic, `DrawSelectableInspector`/`SelectableToJson`), 드래그/휠 받는 쪽은 `IUIDragHandler`/`IUIScrollHandler`. `Button`(UISelectable + `UIEventList` On Click), `Toggle`(isOn, Graphic=체크 표시, On Value Changed(bool)), `Slider`(Fill Rect/Handle Rect, 방향 4개, min/max, Whole Numbers, On Value Changed(float) — 누르면 바로 그 위치로), `InputField`(Text Component/Placeholder, Character Limit, Content Type(Standard/Integer/Decimal/Alphanumeric/Name/Email/Password/Pin), Line Type Single/Multi, 캐럿 깜빡임·선택·Ctrl+A/C/X/V·Home/End, 한글 IME 조합 중 글자 표시, `GameViewEditorWindow::GameToScreen` 으로 IME 창 위치, On Value Changed/On End Edit(string)), `ScrollRect`(Content/Viewport, Horizontal/Vertical, Movement Elastic/Clamped/Unrestricted, Inertia·Deceleration, Scroll Sensitivity(휠 ×30), normalizedPosition), `Mask`(부모 Graphic 사각형으로 자르기, Show Mask Graphic), `RectMask2D`(자기 사각형으로 자르기). 자르기 = `UIRenderer::SetClip` → 명령별 scissor(`42. UI.fx` ScissorEnable), 맞춤 검사도 같은 사각형 안에서만.
- 입력 라우팅(`UISystem::ProcessInput`): 맞은 그래픽에서 위로 `FindUp<UISelectable>`/`IUIDragHandler`/`IUIScrollHandler`. 드래그는 10px 넘게 움직이면 시작 — ScrollRect 드래그가 시작되면 눌린 Button 은 취소(Unity 와 같음), Slider 는 바로 드래그. 선택(`Select/ClearSelection`) 은 InputField 포커스용, 키보드는 `OnUpdateSelected`.
- `UIEventList`(`UIPersistentCall{Target, Method, ParamType, Argument, CallState, Dynamic}`): Inspector 목록, Invoke(저장 호출 → `ScriptEngine::InvokeMethod`, Dynamic 이면 이벤트 값 전달), JSON. C# 이벤트 = `InvokeUIEvent(goId, kind, float, byte*)` — kind 0 click, 1 slider, 2 toggle, 3 input changed, 4 input end edit, -1 비우기.
- 메뉴: GameObject > UI 에 Toggle, Slider, Scroll View(+Viewport(RectMask2D)/Content/Scrollbar 없음), Input Field 추가. 아이콘 `Tools/icons_ui.py`.

**빌드 시스템 (`Source/Build/`, Unity 의 Build Settings / Player)**:
- `BuildSettings`: `ProjectSettings/EditorBuildSettings.json`(scenes[{path, enabled}], developmentBuild, lastBuildFolder), `ProjectSettings/PlayerSettings.json`(companyName, productName(비면 프로젝트 폴더 이름), version, fullscreenMode 0 FullscreenWindow / 1 MaximizedWindow / 2 Windowed, defaultScreenWidth/Height, resizableWindow, runInBackground). `RuntimeScenes()` = 플레이어에서는 player.json 의 목록, 에디터에서는 켜진 빌드 씬(에디터 Play 중 SceneManager.LoadScene 도 Build Settings 목록 기준 — Unity 와 같음).
- `BuildSettingsWindow`(File > Build Settings..., Ctrl+Shift+B): Scenes In Build(체크, 끌어서 순서, Project 창 .scene 드롭, Delete/우클릭 제거, Add Open Scenes), Platform 목록(Windows 만 사용 가능), Development Build, Player Settings... (Project Settings > Player), Build / Build And Run(Ctrl+B, 폴더 선택 IFileOpenDialog FOS_PICKFOLDERS).
- `BuildPipeline`: 검사(Play 중 아님, 씬 있음(없으면 열린 씬), 스크립트 컴파일 중/오류 아님, 저장 안 한 씬 경고) → 스레드에서 의존 에셋 수집(씬 JSON 의 모든 문자열 중 실제 파일인 것, 재귀. 모델은 옆 텍스처/폴더도) + 복사 + "Building Player" 진행 창. 결과: `<Out>/<Product>.exe`(NovaEngine.exe 복사) + DLL, `<Out>/<Product>_Data/`(player.json, Assets·Resources 의존 파일 + 하늘 dds, Shaders, Binaries/ShaderCache, Binaries/Scripting(ScriptCore/런타임 설정), ProjectSetting/fonts·icon.ico, ProjectSettings, Library/ScriptAssemblies/Assembly-CSharp.dll). 끝나면 Console 에 결과, Build And Run 이면 exe 실행, 아니면 탐색기로 폴더 표시.
- `PlayerRuntime`: 시작 시 `<exe 이름>_Data/player.json` 이 있으면 플레이어 모드(`Application::isPlayer`) — 엔진·프로젝트 경로 모두 _Data, 작업 폴더 _Data/Binaries, Hub 건너뜀, 에디터 창/도킹 없음(`EditorGUIManager::Init(true)`), 로딩 창 없음, 창 모드(FullscreenWindow = 모니터 크기 WS_POPUP), `ScriptEngine` 은 컴파일 없이 DLL 만 읽음, 첫 빌드 씬을 열고 바로 Play(`PlayerRuntime::Start`), 매 프레임 `PlayerRuntime::Render` 가 백버퍼에 카메라 → UI 를 그린다(`GameViewEditorWindow::SetPlayerView` 로 입력 좌표 = 창). `Application.Quit` = PostQuitMessage.
- C#: `NovaEngine.SceneManagement.SceneManager`(LoadScene(이름/경로/번호) — `SceneManager::LoadSceneDuringPlay` 로 프레임 끝에 교체, GetActiveScene, sceneCountInBuildSettings), `SceneUtility`, `Application.isEditor/productName/Quit`. 에디터 Play 중 다른 씬을 열었어도 Stop 하면 Play 를 시작한 씬으로 돌아온다(`m_PlayOriginalPath`).
- 개발용: `NOVA_DEV_BUILD=<폴더>` — 에디터 시작 후 180 프레임이 지나고 스크립트 컴파일이 끝나면 대화상자 없이 한 번 빌드(탐색기 창은 열지 않음).
- 검증(2026-10-01): scratchpad ScriptTest → `BuildOut/UI Demo.exe`(72 파일, 86.5 MB, 0.3초). Windowed 1280x720 / Fullscreen Window 둘 다 실행, 버튼 클릭(저장 호출 + C# 리스너), 3번째 클릭에서 `SceneManager.LoadScene("SampleScene")` → 새 씬 Start, Esc → `Application.Quit` → 정상 종료. 빌드된 게임을 빨리 다시 시험할 때는 엔진만 빌드한 뒤 `Binaries/NovaEngine.exe` 를 `<Product>.exe` 로 복사해도 된다(_Data 는 그대로).
- 플레이어에서만 드러났던 문제(고침):
  - `SceneViewManager::m_LastActiveSceneEditorWindow` 가 초기화되지 않아 Scene 창이 없는 플레이어에서 쓰레기 포인터 → `Light::EditorViewUpdate/EditorProjUpdate` 충돌. 이제 nullptr 초기화 + 두 함수에서 null 이면 건너뜀.
  - 플레이어는 첫 프레임 전에 Start 가 돌아 `UISystem::Update` 의 `EnsureRectTransform` 이 스크립트가 대기열에 넣은 RectTransform 을 못 보고 새 것을 붙였다(스크립트가 넣은 anchoredPosition/sizeDelta 가 가려짐) → `GetComponentIncludingPending` 로 확인.
  - `SceneManager::LastUpdate` 가 동작 목록을 돌면서 동작 안에서 `AddLastUpdate` 가 불리면 벡터가 다시 할당되어 실행 중인 람다가 사라졌다 → 목록을 떼어 낸(swap) 뒤 실행, 새로 들어온 것은 다음 프레임.
  - Play 중 같은 씬을 다시 열면 fileID 가 같아 `ScriptBindings` 의 id → 포인터 캐시가 지운 오브젝트를 가리켜 C# `SetParent` 에서 AccessViolation(.NET 이 프로세스를 바로 끝내 Editor.log 에 [CRASH] 가 안 남음 — Windows 이벤트 로그 "Application" 의 .NET Runtime 항목에 관리 스택이 있다) → 씬 교체 때 `UISystem::OnSceneUnloading()`(선택/눌린 대상 해제) → 옛 씬 삭제 → `ScriptEngine::OnSceneSwapped()`(id 캐시, C# UI 리스너 비우기) → 새 씬 Enter.

**Particle System (`Source/Effects/`, Unity 의 Shuriken)**:
- `ParticleSystem`(컴포넌트, Behaviour 가 아니라 헤더 체크박스 없음): 설정은 공개 멤버(모듈별 `...Enabled` + 값), 실행 상태는 비공개(입자 배열, m_Playing/m_Paused/m_Emitting, 시스템 시간, 방출 누적, xorshift 난수). 입자 = 위치/속도(적분)/AnimatedVelocity(Velocity over Lifetime, 적분 안 함)/수명/나이/크기/회전/색/난수 4개/시트 프레임. `Advance(dt)`: Start Delay → 시스템 시간 [이전, 지금) 구간의 Rate over Time + Burst(주기·간격·확률, 반복 경계 처리) → Rate over Distance(오브젝트 월드 이동) → 입자 갱신(중력(월드 → 시뮬레이션 공간), 힘, 속도 제한+Drag, Noise(3D 값 노이즈, 옥타브, 스크롤), 위치, 회전, 크기·색·시트 프레임). `Simulate(dt)` 는 Simulation Speed 를 곱하고 1/20초 단위로 나눠 돈다. 도형 공간은 +Z 로 방출(기본 GameObject 는 X -90 도, `GameObjectFactory::CreateParticleSystem`), Shape Position/Rotation/Scale → (World 공간이면 오브젝트 월드 행렬까지) → 시뮬레이션 공간. 반복 안 함: 방출이 끝나고 입자가 다 사라지면 멈추고 Stop Action(None/Disable/Destroy, Play 모드만).
- 값 형식(`ParticleCurves.*`): `ParticleCurve`(키 + Catmull-Rom 접선 Hermite), `ParticleGradient`(색 키 + 알파 키, Blend/Fixed), `MinMaxCurve`(Constant/Curve/TwoCurves/TwoConstants — Unity 번호, 곡선은 Multiplier), `MinMaxGradient`(Color/Gradient/TwoColors/TwoGradients/RandomColor). JSON: 상수는 숫자 하나, 색 하나는 배열 하나로 저장(씬 파일을 읽기 쉽게).
- 진행: `ParticleSystem::UpdateAll()`(App 루프, UISystem::Update 다음) — Play 모드는 `ShouldUpdateGame` 일 때 게임 DT, 아니면 `ParticleSystemEditor::UpdatePreview`(선택한 오브젝트가 속한 Particle System 묶음의 맨 위부터 자식까지 재생, 선택이 바뀌면 이전 것은 Stop+Clear 하고 새것은 Restart). Start() 는 미리보기 입자를 지우고 Play On Awake 면 재생.
- 그리기(`ParticleRenderer`, `Shaders/43. Particle.fx`): 입자 하나 = 인스턴스(68 바이트: 월드 위치, 회전, 크기, 월드 속도, 색, UV 칸), 정점 버퍼 없이 SV_VertexID 로 모서리 4개 삼각형 띠, 시스템마다 `DrawInstanced` 한 번. Render Mode 4개(Stretched = 속도 방향 + Speed/Length Scale), AlphaTech/AdditiveTech, 깊이 비교만. 시스템은 카메라에서 먼 것부터, Alpha Blended + Sort Mode 면 입자도 정렬. `EditorApp::OnSceneRender`(Game 뷰, 플레이어) 와 `_Editor_OnSceneRender`(Scene 뷰, 툴바 Effects > Particle Systems 가 켜져 있을 때) 에서 불투명 물체 다음·후처리 전에 그린다(Bloom 이 Additive 를 빛나게). Local 공간이면 오브젝트 크기를 입자 크기에 곱한다.
- 텍스처(`ParticleTextures`): 내장 `builtin:Default-Particle / Glow / Smoke / Spark / Flame-Sheet(4x4 플립북)` 를 실행 중에 만든다(밉맵 포함), 그 외는 Assets 이미지(UISprites 로 읽음). Renderer 의 Texture 필드(⊙ Object Picker, 이미지 끌어 놓기) — Flame-Sheet 를 고르면 Texture Sheet Animation 4x4 를 켠다.
- 에디터(`ParticleSystemEditor`): Inspector 모듈 머리글(지원 안 하는 모듈은 회색: Inherit Velocity, Collision, Sub Emitters, Trails, Lights 등), `CurveField`(오른쪽 ▼ 모드, 곡선이면 작은 그림 → 곡선 편집 팝업: 키 끌기/더블클릭 추가/오른쪽 클릭 삭제, Multiplier, 프리셋 6개), `GradientField`(그라디언트 편집 팝업: 위 알파 키/아래 색 키, 끌기·추가·삭제, Blend/Fixed, 프리셋 5개). Scene 뷰 오른쪽 아래 "Particle Effect" 창(Play/Pause, Restart, Stop, Playback Speed, Playback Time, Particles) — `SceneEditorWindow` 가 이 영역을 클릭 선택에서 뺀다. 선택하면 도형 기즈모(연파랑), 모든 시스템에 화면 크기 아이콘.
- C#: `NovaEngine.ParticleSystem`(Play/Stop(withChildren, StopBehavior)/Pause/Clear/Emit/IsAlive, isPlaying/isPaused/isStopped/isEmitting/particleCount/time, `main`/`emission`/`shape`/`colorOverLifetime` 모듈 구조체, `ParticleSystem.MinMaxCurve`(float 암시 변환)/`MinMaxGradient`(Color 암시 변환)). 스크립트에서 곡선은 상수·두 상수만, 색은 한 색·두 색만. 네이티브 표 끝 `PS_*`(번호표는 NativeApi.cs 주석).
- 이전 DX11 책 예제의 스트림 출력 입자(`Graphics/DX11/ParticleSystem.*`, 쓰이지 않음)는 이름이 겹쳐 `StreamOutParticles` 로 바꿨다.
- 검사: scratchpad `make_particle_scene.py` → ScriptTest `Assets/Scenes/Particles.scene`(모닥불 = 불꽃 플립북 + 연기 + 불티, 불꽃 분수(Stretched), 마법 구슬), `PS_SCRIPT_TEST=1` 이면 `Assets/ParticleTest.cs`(모듈 읽기/쓰기, AddComponent + Emit, Stop) 도 붙인다.
- 2차(2026-10-01): **Collision**(Planes = 목록 GameObject 의 위치·+Y 평면, 들어올 때 반사: Bounce/Dampen/Lifetime Loss/Min·Max Kill Speed/Radius Scale, Scene 뷰 초록 격자. World = `PhysicsManager::Raycast` 로 콜라이더, 물리 바디가 있는 Play 모드에서만), **Sub Emitters**(Birth: 사는 동안 대상의 Rate over Time 만큼 따라 뿜음 / Collision / Death: 대상의 Bursts 합(없으면 Rate 1초 분량)을 그 자리에서, Inherit Color/Size/Rotation, 확률. 대상은 자식 GameObject 의 Particle System — `IsSubEmitterTarget()` 이면 Play/Play On Awake 로 스스로 방출하지 않는다. + 버튼 = 불꽃 프리셋 자식을 만들어 Death 로 연결), **Trails**(입자마다 점 목록 + 머리, Ratio/Lifetime(수명 비율)/Minimum Vertex Distance/World Space/Die with Particles(끄면 남은 꼬리가 사라질 때까지 `IsAlive`)/Size affects Width/Inherit Particle Color/Color over Lifetime/Width over Trail/Color over Trail, 렌더러의 Trail Texture(내장 `builtin:Trail`). 카메라를 향한 띠를 CPU 로 만들어 `TrailAlphaTech/TrailAdditiveTech` 로 입자 바로 앞에 그린다).
- 참조 fileID 다시 매기기: `GameObject::RegenerateFileIDs()`(복제·붙여넣기·프리팹 배치·Instantiate)가 옛 → 새 ID 표를 만들어 `Component::RemapFileIDs(map)`(새 가상 함수)를 부른다 — Particle System 은 하위 이미터 대상·충돌 평면을 고친다(UI On Click 등 다른 참조는 아직 이 함수를 쓰지 않음).
- 기본 Volume: `SceneManager::CreateScene`(새 씬·새 프로젝트의 SampleScene)이 "Global Volume" + `RenderPipelineSettings::EnsureSampleSceneProfile()`(`Assets/Settings/SampleSceneProfile.volumeprofile`: Bloom threshold 0.9 / intensity 1 / scatter 0.7, Tonemapping Neutral, Vignette 0.2)을 넣는다(Unity URP 기본 씬과 같음). 기존 씬은 그대로.
- 미구현: Lights, Inherit Velocity, 3D Start Size/Rotation, Scaling Mode, Custom Simulation Space, 소프트 파티클, 조명 받는 입자, Mesh 렌더 모드, 컬링(보이지 않아도 시뮬레이션).

**머티리얼 / PBR (URP Lit, 2026-10-01)**:
- 에셋(`UMaterial`, `.mat` JSON): Shader(Lit/Unlit), BaseColor(감마), Metallic, Smoothness(+Source: Metallic Alpha / Albedo Alpha), NormalScale, OcclusionStrength, Tiling/Offset, AlphaClipping/Cutoff, ReceiveShadows, SpecularHighlights, EnvironmentReflections, Emission(+색·세기), Priority, 맵 5개(Base/Metallic/Normal/Occlusion/Emission 경로). 예전 키(Diffuse/Specular…)도 함께 저장하고, 예전 파일은 Diffuse → BaseColor, specular power → smoothness((log2(p)−1)/10)로 바꿔 읽는다. `UMaterial::Create(dir)` 는 "New Material", "New Material 1" … 로 이름이 겹치지 않게 만든다. 내장 머티리얼(builtin 경로)은 저장·편집 안 함.
- 셰이더: `Shaders/32. InstancedBasic.fx` 의 메시 PS 가 URP Lit(`cbPerObject` 의 `PbrMaterial gPbr`, C++ 쪽 `Source/Graphics/Common/PbrMaterial.h` 112 바이트 — 필드 순서를 바꾸면 둘 다 고칠 것). kDielectricSpec 0.04, 직접광 = Unity 의 정규화 GGX 근사, 간접광 = **하늘 큐브맵**: 확산은 법선 방향 가장 흐린 밉(mips−3), 반사는 `reflect` 방향 밉 = pr·(1.7−0.7·pr)·6 + EnvironmentBRDF(surfaceReduction, grazing, fresnel pow4). 빛의 Ambient 값은 Lit 에서 쓰지 않는다(Unity 와 같음). 파이프라인이 감마 공간이라 PS 안에서 pow 2.2 로 선형 계산 후 되돌린다. 터레인은 아직 예전 Blinn-Phong(gMaterial).
- 바인딩: `UMaterial::Apply(fx, forPreview)` / `ApplyOrDefault` — MeshRenderer / SkinnedMeshRenderer 의 Render·RenderInstancing·_Editor_Render 가 부른다. 맵이 있으면 Use* 플래그를 SRV 로 정한다.
- 에디터(`Source/Editor/MaterialInspector.*`): 머리글(미리보기 아이콘, 이름, Shader), Surface Options / Surface Inputs / Advanced Options 접기, 텍스처 행(썸네일 칸, ⊙ Object Picker, Project 에서 끌어 놓기), 값이 바뀌면 SyncLegacy + 저장, `WatchUndo` 로 Undo. .mat 선택 시 아래 Preview(256px 구, 드래그 회전) — `RenderPreview` 가 오프스크린 RT 에 Sphere 를 그리고 RTV/DSV/뷰포트를 되돌린다. MeshRenderer 안에서는 접을 수 있는 임베드 형태. Renderer 의 Materials 목록 칸은 `MaterialInspector::MaterialSlot`(⊙ = ObjectPicker "Select Material": None / builtin:Default-Material / `FindAllMaterials()` 의 .mat, Project 의 "MAT_FILE" 끌어 놓기). None 슬롯은 빈 경로로 저장하고 그릴 때 기본 재질.
- 스카이박스: `Resources/Textures/Skybox/KloofendalPureSky.dds`(Poly Haven CC0, `Tools/hdri_to_cubemap.py` 로 512 큐브·밉 10·R9G9B9E5, 감마 값, 위 반구 밝기 중간값 0.38 로 노출, 태양 12 에서 자름 — 더 밝으면 기본 Bloom(0.9)이 구름 전체를 번지게 함). `EditorApp` 의 `_sky` 가 읽고 매 프레임 gCubeMap 으로 넘긴다. 그리기: Game 뷰/플레이어는 카메라 `GetBackgroundType()==0`(Skybox), Scene 뷰는 `SceneToolbar::SkyboxVisible()`(Effects 토글 + Effects > Skybox, 와이어프레임 제외) — 불투명 물체 다음·입자 전에 `Sky::Draw(dc, eye, viewProj)`(xyww, LessEqual). 빌드에 넣는 의존 파일도 이 DDS(`BuildPipeline`).
- 개발용 환경 변수: `NOVA_DEV_SELECT=<GameObject 이름>`, `NOVA_DEV_FILE=<프로젝트 기준 파일>`(90 프레임 뒤 한 번 선택 → Inspector), `NOVA_DEV_SCENECAM=px,py,pz,tx,ty,tz`(60 프레임 뒤 Scene 카메라 위치·바라보는 점).
- 검사: scratchpad `make_material_scene.py` → ScriptTest `Assets/Scenes/Materials.scene`(금·은·거친 구리·빨간 플라스틱·유광 파랑·발광 구 + 노멀맵 큐브 + 바닥).

**그림자 (Cascaded Shadow Maps, 2026-10-01)**:
- 구조: `Source/Graphics/DX11/ShadowRenderer.*`(그림자 패스 + 받는 쪽 변수), `ShadowMap.*`(깊이 맵 모음, Game/Scene 뷰가 하나씩). 예전에는 모든 빛의 DSV/SRV 가 **같은 텍스처 하나**를 가리켜 서로 덮어썼고, 받는 쪽은 9 탭 합을 16 으로 나눠 빛이 항상 56% 로 약했으며, 맵 밖(Border 0)은 그림자로 처리됐다 → 모두 고침.
- 맵: 빛마다 Texture2DArray — 방향광 = 캐스케이드 4 조각(해상도 = Volume Resolution), 스포트광 = 1 조각, 점광 = 6 조각(+X, -X, +Y, -Y, +Z, -Z 월드 축, 해상도/2). 처음 쓰일 때 만든다. 셰이더는 모두 `Texture2DArray`.
- 캐스케이드: 카메라 View/Proj 에서 절두체 조각 [n, f]("Split × Max Distance")을 감싸는 최소 구(중심은 시선 축 위, 반지름은 1/16 로 올림 → 카메라가 돌아도 크기 불변). 빛 공간에서 구 중심을 텍셀 단위로 맞춘 정사영(폭 2r, 빛 쪽으로 100 m 더) → 떨림 없음. 받는 쪽은 픽셀이 들어 있는 첫 구(`gCascadeSpheres`, w = r²)를 고르고, Max Distance 끝의 Last Border 구간에서 흐려진다(`gShadowParams`).
- 바이어스: 하드웨어 DepthBias 는 0, 캐스터 VS(`26. BuildShadowMap.fx` 의 `ApplyShadowBias`)가 URP 처럼 월드 공간에서 깊이(빛 반대쪽) + 노멀(안쪽, 1−N·L 비례) 이동. 값 = Bias × 텍셀 크기(방향광: 2r/해상도, 원근 광원: 거리 × 2tan(fov/2)/해상도), Soft 면 ×2.5. 메시/인스턴싱/스킨/지형 VS 모두 적용 → 모든 캐스터 패스가 `gViewProj`(드라이버가 매 조각 설정)와 `gWorld`/`gWorldInvTranspose` 를 쓴다.
- 필터: `ShadowPCF` 동적 루프 한 벌 — Hard 1 샘플(비교 선형 2x2), Soft Low 2x2 / Medium 3x3 / High 4x4. (unroll 로 쓰면 빛·면마다 복사돼 InstancedBasic 컴파일이 20 초 넘게 걸렸다.)
- 설정: Volume 효과 "Shadows"(분류 Shadowing — Add Override 메뉴가 분류별 하위 메뉴가 됨): Max Distance 50, Cascade Count 4, Split 1/2/3 = 0.067/0.2/0.467, Last Border 0.2, Resolution 2048, Depth/Normal Bias 1, Soft Shadows + Quality Medium. Inspector 에 캐스케이드 색 막대(`VolumeEditor` 의 `DrawCascadeBar`). 예전 기본 프로파일에 없는 효과는 `DefaultVolumeProfile()` 이 기본값으로 채워 저장. Volume 섞기는 그림자 패스 앞으로 옮겼다(Game/Scene 모두).
- 빛(Light > Shadows): Shadow Type(No/Hard/Soft), Strength, Bias(Use settings from Render Pipeline / Custom: Depth, Normal), Near Plane(스포트/점광). 저장 키 shadowStrength/shadowBiasMode/shadowDepthBias/shadowNormalBias/shadowNearPlane.
- 진단: 설정이 바뀌면 Editor.log 에 `[Shadow] cascades=... ends=[...] radii=[...]` 한 줄.
- 검사: scratchpad `make_shadow_scene.py` → ScriptTest `Assets/Scenes/Shadows.scene`(격자 울타리, 70 m 까지 기둥 두 줄, 낮은 해).
- 미구현: 캐스케이드 사이 섞기, 캐스케이드 디버그 색 보기, Contact Shadows, 그림자 받는 투명 물체.

**절두체 컬링 + Hierarchy 가상 스크롤 (2026-10-01)**:
- `Source/Scene/SceneCulling.*`: Mesh Renderer / Skinned Mesh Renderer 의 월드 AABB 를 느슨한 옥트리(칸의 2 배 영역, 중심이 든 칸 하나에만)에 둔다. `Update`(App 루프, 그리기 전) = 위치·메시가 바뀐 렌더러만 다시 넣고 사라진 것은 뺌(포인터를 건드리지 않음), 뿌리 밖이면 전체 다시 짓기. `Cull(ViewProj, shadow)` = 노드 상자가 밖이면 통째로 버림 / 완전히 안이면 통째로 받음, 보이는 렌더러는 `Component::CullStamp = Stamp`. Scene 의 모든 그리기 루프(본·그림자·노멀깊이, Game/Scene, 인스턴싱 묶기 포함)가 `SceneCulling::IsVisible` 로 거른다. 그림자는 조각마다 빛 절두체(가까운 면 제외), 카메라 컬링은 깊이 사전 패스 직전 한 번(본 패스와 공용). 지형·나무는 각자 컬링. Skinned 는 기본 자세 상자를 넉넉히(애니메이션). Game 뷰 Stats 에 "Frustum culling: 보임 / 전체".
- 측정(상자·구 1600 개, Scene 뷰): 모두 보이는 방향 136 ms, 등 돌린 방향 6 ms(끄면 165 ms). 옥트리 갱신 2.3 ms, 조회 노드 36~554 개 → 순회 비용은 작고, 남은 비용은 보이는 물체마다의 그리기 제출(패스 × 물체 수만큼 Effects11 Apply + 상수 버퍼·리소스 바인딩)이다. 다음 단계 = Scene 뷰 인스턴싱/배칭.
- Mesh Renderer 묶어 그리기(`Source/Scene/MeshBatcher.*`): 보이는 Mesh Renderer 를 (메시, 서브셋, 재질)로 묶어 월드 행렬 배열(동적 인스턴스 버퍼, 슬롯 1)로 `DrawIndexedInstanced` 한 번. 그림자·깊이 패스는 (메시, 서브셋)만. 본 패스는 재질 순으로 정렬해 `UMaterial::ApplyOrDefault` 를 재질이 바뀔 때만. 셰이더: InstancedBasic `BatchTech`(VS_Batch), SsaoNormalDepth `NormalDepthBatchTech` — 둘 다 월드 위치 × ViewProj 라 EQUAL 깊이 검사가 맞는다. 법선은 월드 3x3 여인수 행렬(축마다 크기가 달라도 맞음). 그림자는 BuildShadowMap 의 `BuildShadowMapInstancingTech`. Scene 의 본/그림자/깊이 패스(Game·Scene 뷰)가 모두 이 경로로 그리고, Mesh Renderer 의 Render/_Editor_Render/RenderShadow* 는 더 이상 Scene 이 부르지 않는다. Mesh Renderer 의 Enabled 체크, Cast Shadows(Off / Shadows Only)를 따른다. 예전 `RenderManager::InstancingMode`(툴바 Debug > Instancing, 최대 500 개, VS 가 인스턴스 행렬을 제대로 안 씀)는 이제 쓰이지 않는다.
- 측정(상자·구 1600 개, 1497 개 보임): Scene 뷰 그리기 145 ms → 9.4 ms, 7 FPS → 58 FPS. 물체 수 × 패스 수만큼이던 효과 Apply 가 묶음 수(메시 2 × 재질 3 ≈ 6) × 패스 수로.
- Hierarchy: 펼친 트리를 포인터 목록(`m_Rows`)으로 편 뒤 `ImGuiListClipper` 로 보이는 행만 그린다.
- "저장 안 된 변경(*)" 판단: 예전에는 `SceneManager::IsCurrentSceneDirty` 가 0.25 초마다 씬 전체를 JSON 으로 만들어 1600 개면 프레임당 약 820 ms(프레임이 0.25 초보다 길어 매 프레임). 지금은 Undo 가 조작이 끝날 때 확정한 씬 JSON 의 해시(`Undo::CommittedSceneHash`)를 쓴다 → Hierarchy 0.5 ms. 단, Undo 확정은 이제 바뀌었을 수 있는 루트만 다시 직렬화한다(아래 Undo 항목).
- 개발용: `NOVA_DEV_PROFILE=1` = `Source/Core/FrameProfiler.h` 구간(창별 Render/Update, Scene/Game 뷰 그리기, 컬링 갱신, Present)의 프레임당 평균 ms 와 컬링 결과를 3 초마다 Editor.log 에. `NOVA_DEV_NOCULL=1` = 컬링 끄기(비교용). 검사 씬: scratchpad `make_cull_scene.py` → `Assets/Scenes/Culling.scene`.

**Profiler 창 (2026-10-01)**:
- 데이터: `Source/Core/Profiler.*`. 프레임 = `BeginFrame`(App 루프, CalculateFrameStats 뒤) ~ `EndFrame`(프레임 끝). CPU 구간 = `PROFILE_SCOPE("이름")`(문자열 상수) 또는 기존 `FRAME_PROFILE(std::string)`(이름을 `Profiler::Intern`). 메인 스레드만, 시작 순서 + 깊이 + 시작/길이로 평평하게 저장. GPU 구간 = `PROFILE_GPU("이름")` → TIMESTAMP 쿼리 쌍, 프레임마다 TIMESTAMP_DISJOINT 하나, 슬롯 6 개 고리(결과는 몇 프레임 뒤 `DONOTFLUSH` 로 읽어 그 프레임 기록에 채움, 프레임 GPU 시간 = 깊이 0 구간 합). `Profiler::Phases` = 연달아 이어지는 단계(Next 가 앞 단계를 닫음, CPU+GPU). 통계 = `SetStat("그룹/이름", 값)`(`App::RecordProfilerStats`: 이번 프레임에 그 화면을 그렸을 때만 RenderStats·MeshBatcher·SceneCulling·TreeRenderer 값). 기록은 최근 300 프레임.
- 모으기는 Profiler 창이 열려 있고 Record 가 켜져 있을 때만(`SetCollecting` 은 다음 BeginFrame 부터 적용). 꺼져 있으면 구간마다 bool 검사 하나.
- 구간: App 루프(Scripts.BeginFrame, Scene.Update, Physics.Update, EditorUpdate, Audio/Scripts/UI/Particles.Update, Camera/Light Update, CullingUpdate, Editor Windows, Undo, ImGui Render, Present, Main Thread Tasks), EditorApp Game/Scene 뷰 단계(Shadows, Depth Prepass, SSAO, Opaque, Sky, Grid, Particles, Post Processing, UI — GPU 도), MeshBatcher(GPU "Mesh Renderers"), TreeRenderer::DrawAll(GPU "Trees"), SceneCulling::Cull.
- 창: `Source/Editor/Windows/ProfilerEditorWindow.*` (Window > Analysis > Profiler, Ctrl+7, 처음엔 닫힘, 1040x600 떠 있는 창). CPU 범주 = 구간 이름 규칙(없으면 부모 범주를 물려받음)으로 자기 시간(자식 제외)을 더함: Rendering / Scripts / Physics / Editor / Present / Others. GPU 범주 = Shadows / Depth·SSAO / Opaque / Sky·Transparent / Post / UI / Other. 그래프 눈금은 위 3% 를 버린 값으로(튀는 프레임에 납작해지지 않게). 따라가기 모드는 GPU 결과까지 도착한 가장 최근 프레임을 보여준다. 아래: Hierarchy(같은 경로 합치기, 프레임의 25% 이상 구간은 처음부터 펼침) / Timeline(휠 확대, 끌어 이동) / GPU, 오른쪽 Rendering Statistics. ← → = 앞/뒤 프레임.
- 측정 예(Debug 빌드): 1600 물체 씬 Scene 뷰 그림자 4.9 ms 중 MeshBatcher 2.2 ms(조각 4 개), 숲 1500 그루 GPU Opaque > Trees 1.8 ms. Profiler 창 자체 그리기 약 1~1.5 ms(Debug).
- GPU 구간마다 PIPELINE_STATISTICS 쿼리도 건다 → GPU 표의 Pixels Shaded(PSInvocations, 겹쳐 칠한 것 포함) / Triangles(CPrimitives). **타임스탬프 ms 는 GPU 가 CPU 를 기다린 빈 시간도 포함**한다(CPU 가 느린 Debug 빌드에서는 엉뚱한 구간에 몇 ms 가 붙음). GPU 일의 양은 Pixels Shaded 로 비교할 것. 나무 그리기 안에도 GPU 구간(LOD0/LOD1 Bark·Leaves, Impostors).
- 미구현: 메모리/GC 모듈, 다른 스레드 구간, 프레임 저장/불러오기, Hierarchy 검색·정렬 바꾸기, Deep Profile.

**Release 빌드 (2026-10-01)**: `build.bat release`. Debug 는 `/O2` 라도 `_DEBUG` 라서 D3D 디버그 레이어(`D3D11_CREATE_DEVICE_DEBUG`)와 최적화 끈 셰이더(`D3D10_SHADER_SKIP_OPTIMIZATION`)를 써 CPU·GPU 숫자가 모두 부풀려진다 → 성능 측정은 Release 로. 셰이더 캐시 파일 이름에 컴파일 플래그를 넣어(`_f<flags>.fxo`) 두 구성 캐시가 서로 덮어쓰지 않는다. Release 첫 실행은 최적화 컴파일이라 오래 걸린다(NormalMapSkinned 약 12 초). **`HR(p)`/`CHECK(p)` 는 이제 `p` 를 항상 실행**한다(예전 `assert(SUCCEEDED(p))` 는 Release 에서 `D3DX11CreateEffectFromMemory`·`Present` 호출 자체가 빠져 시작하자마자 죽었다) — assert 안에 할 일을 넣지 말 것. Assimp: 저장소에 Release DLL(`assimp-vc143-mt.dll`)이 없어 Release 도 `mtd` 를 링크한다(엔진은 `Assimp::Importer`(pimpl) + C 구조체만 주고받아 CRT 가 달라도 안전). mt DLL 을 두면 `NOVA_ASSIMP_RELEASE_DLL` 정의. 측정(Release): 물체 1600 씬 Scene 뷰 364 FPS(Debug 76), 숲 안 CPU 0.5 ms + GPU 대기 → GPU 가 병목.

**먼 캐스케이드 캐시 (2026-10-01)**: `ShadowRenderer::Render` 가 화면(Game/Scene)마다 `FrameData::Cache[4]` 를 둔다. 캐스케이드 0·1 은 매 프레임, 2 는 2 프레임·3 은 4 프레임마다(Volume > Shadows > Far Cascade Update: Every Frame / Staggered(기본) / Slow = 4·8) 겹치지 않게 돌아가며 다시 그린다. 다시 그리지 않는 캐스케이드는 **그 맵을 그린 때의 행렬(`Cache.Dir`)과 구(`Spheres[i]`)를 그대로 받는 쪽에 넘겨** 맵과 같은 좌표로 읽는다(정적 물체 그림자는 그대로, 움직이는 물체만 몇 프레임 늦음). 빛 방향(dot < 0.99999)·설정 키·맵 재생성(`ShadowMap::Generation`)·카메라가 구 반지름의 5% 넘게 이동·반지름 변화면 바로 다시 그린다. Profiler GPU 에 `Cascade N` 구간, 통계 `Shadow Cascades Redrawn`. 측정(Release, 숲 안): 그림자 GPU 2.07 → 1.49 ms(0·1·3 을 그린 프레임), 평균 2.75 캐스케이드/프레임.

**Profiler Memory 모듈 (2026-10-01)**: `Source/Core/MemoryStats.*`(D3D 자원 크기 = `DirectX::ComputePitch` 로 밉·배열 합, 프로세스 `GetProcessMemoryInfo`, GPU `IDXGIAdapter3::QueryVideoMemoryInfo`), `Source/Editor/Windows/ProfilerMemory.*`(0.5 초마다 / Refresh 로 범주별 수집: ResourceManager 텍스처·메시, 그림자 맵 2 벌, 나무 메시·임포스터·잎/수피 텍스처, 지형 GPU/CPU, Undo 기록·씬 JSON 캐시, Profiler 기록, 나무 선택용 CPU 사본, Other GPU = GPU 사용량 − 센 것). 매 프레임 통계 `Memory/…` 로 남겨 세 번째 모듈(묶음별 쌓은 그래프)을 그린다. 아래 Memory 탭 = 범주(큰 것부터) → 항목. 모듈 제목을 누르면 접힌다. 숲 씬 예: 프로세스 544 MB, GPU 319 MB 중 그림자 맵 128 MB(Game·Scene 뷰 각 4×2048²×4 B), 나무 32 MB, Other GPU 154 MB.

**잎 카드 외곽 다각형 (2026-10-01)**: `TreeTextures::LeafCardHull(shape, leaves, length, cell)` 가 잎 아틀라스 칸마다 덮인 곳(굽기와 같은 SDF, 96² 마스크)을 감싸는 8 방향 k-DOP(u, v, u+v, u−v 최소·최대 + 카드 3% 여유 — 먼 밉은 덮임을 맞추며 번지므로)을 구한다. `TreeGenerator` 는 잎 카드를 사각형 대신 이 다각형(부채꼴 삼각형)으로 만든다: 칸·좌우 뒤집기는 셰이더 `TreeLeafSample` 과 같은 식(시드)으로, 경계에 걸리면 사각형. 입력 `TreeParams::LeafLengthForHull`(저장 안 함, `TreeRenderer::GetMesh` 가 `TreeDesc::LeafLength` 를 넣고 메시 키에도 넣음). 다각형 = 카드의 47~57%. 측정(Release, 숲 안): 그림자 픽셀 70.9M → 42.6M(GPU 2.84 → 2.07 ms), 깊이 사전 패스 6.84M → 4.11M, 삼각형 1.8M → 2.4M, 전체 GPU 4.98 → 4.32 ms, 202 → 229 FPS.
- 메인 도킹 창(`##DockSpace`)은 `NoBringToFrontOnFocus | NoNavFocus` — 없으면 도킹된 창(Scene 등)을 누를 때 도킹 영역 전체가 앞으로 와서 떠 있는 창(Profiler, Particle Effect 등)이 뒤로 숨는다.

**그림자·나무 최적화 (2026-10-01)**:
- 화면 단위 목록: EditorApp 이 Game/Scene 뷰 그리기 시작에 `MeshBatcher::BeginView()`, `TreeRenderer::BeginView()`, `++RenderManager::ViewSerial`. 그 화면의 첫 패스가 한 번만 모으고(MeshBatcher = 켜진 Mesh Renderer 마다 월드 행렬 + 서브셋별 묶음 번호, 본/깊이·그림자 묶음 배열을 다시 씀; TreeRenderer = 나무마다 경계 구·카메라 거리 레코드; Scene = Skinned Mesh Renderer·지형 목록), 같은 화면의 나머지 패스(그림자 조각 4 개, 깊이, 본)는 보이는지 검사 + 행렬 추가만. 목록은 `SceneCulling::FrameIndex()` 가 바뀌어도 다시 모은다(프레임 사이 삭제 대비). 화면 안(EditorApp 의 한 함수)에서는 에디터 UI 가 끼지 않아 포인터가 살아 있다.
- 나무 본 패스는 같은 화면 깊이 사전 패스의 LOD 목록을 그대로 쓴다(같은 카메라 → 같은 절두체·LOD·디더).
- 나무 본 패스 = 깊이 EQUAL + 쓰기 없음(`TreeDepthEqual`), 픽셀 셰이더에 clip/LOD 디더 없음: 잎 알파·디더는 깊이 사전 패스(SsaoNormalDepth)가 이미 잘랐다. clip 이 없어 GPU 가 셰이더 전에 깊이로 거른다(early-Z) → 겹친 잎 뒤쪽은 조명 계산을 안 한다. 두 패스의 위치 식은 `precise` 로 묶어 비트까지 같은 깊이(EQUAL 이 맞도록). 임포스터 굽기만 `TreeLeafSurface(pin, true)` 로 자른다.
- 그림자 나무 LOD: `RenderManager::ShadowTexelWorld`(ShadowRenderer 가 방향광 캐스케이드마다, 원근 맵은 0). 나무 지름 / 텍셀 ≤ 900 이면 임포스터(빛을 바라보는 사각형 한 장). LOD1 은 카드가 절반이지만 커서 칠하는 면적이 거의 같아 그림자 픽셀을 줄이지 못한다 → 임포스터만 의미가 있다.
- `GameObject::GetComponent<T>` 는 `dynamic_pointer_cast`(검사마다 shared_ptr 생성·원자적 참조 수) 대신 `dynamic_cast` 포인터 검사.
- 측정(Debug, Scene 뷰): 숲 안(나무 843 그루 보임, 카메라 190,18,100) 본 패스 픽셀 6,043,696 → 639,916 (GPU 1.62 → 0.90 ms), 그림자 픽셀 91.5M → 71.0M, 145 → 173 FPS. 물체 1600 씬: Scene 뷰 그리기 CPU 8.36 → 4.04 ms(그림자 4.86 → 3.04, 깊이 1.42 → 0.23, 본 1.77 → 0.43), 53 → 76 FPS.
- 남은 것: 그림자 GPU 의 대부분은 가까운 캐스케이드(0·1)의 LOD0 잎 카드 겹쳐 칠하기(약 57M 픽셀). 다음 후보 = 잎 그림자용 더 굵은 알파 텍스처/카드 합치기, 캐스케이드 0·1 해상도 조정, 정적 캐스터 그림자 맵 캐시.

**지형 생성기 (2026-10-01)**: World Creator(레이어 스택) + Unity Atlas(오브젝트 스탬프, 비파괴) 참고.
- 설정 `TerrainGenSettings`(`Source/Terrain/TerrainGenSettings.h`) = `TerrainData::Generator` + `BaseSnapshot`(Current Terrain 기준). .terraindata **v3** = v2 + 설정 JSON 문자열 + 스냅샷 수·16비트 값.
- 생성 `TerrainGenerator::Generate(Input)`(`Source/Terrain/TerrainGenerator.*`): 미터 높이 버퍼에서 Base(개선 Perlin, 옥타브 8 개 가중치, Classic/Ridged/Billow/Eroded(IQ 식: 앞 옥타브 기울기가 크면 디테일 감소)/Flat/CurrentTerrain, ShapePower) → 스탬프(Order 순, 회전된 사각형 범위만, `ParallelRows`) → 필터(위→아래, Strength 로 이전 값과 섞음) → 재질(제어 맵 텍셀마다 높이·경사·퇴적 마스크·노이즈로 규칙을 순서대로 덮음). 수력 침식 = 빗방울 입자(칸 단위 높이, 가장 낮은 곳을 0 으로 옮겨 계산 — 0 아래를 판 협곡에서 깎는 양이 음수가 되어 발산하던 문제), 퇴적량은 재질 규칙의 Sediment 마스크. 열 침식 = 이웃 4 개와의 안식각 초과분을 모으는 방식(병렬, 안정). 끄는 중(`Input::Preview`)에는 무거운 필터(침식)를 건너뜀.
- 연결 `TerrainGenerator::Update()`(App 루프, 에디터만, Play 중 제외): 켜진 지형마다 입력 해시(설정 JSON + 스탬프 월드 행렬·값 + 지형 위치·크기·해상도·레이어 수)가 바뀌면 `std::async` 로 생성, 끝나면 메인 스레드가 Heights/Control 을 바꿔 끼우고 `OnHeightsChanged/OnControlChanged`(전체). 마우스를 누른 채면 미리보기, 놓으면 같은 해시라도 전체 한 번 더. 한 지형에 작업 하나(끝나면 최신 입력으로 다시). 상태(`GetStatus`) = 단계별 ms.
- 스탬프 `TerrainStamp` 컴포넌트(`Source/Scene/TerrainStamp.*`): Transform 위치 x·z = 가운데, 크기 x·z = 넓이(m), 크기 y = 높이 배율, 회전 y = 방향, 위치 y = Max/Min/Blend 기준 높이. 모양은 식(산 = 도메인 워프 + 방사형 능선 + 릿지 디테일, 분화구 = 그릇 + 테두리, 화산 = 원뿔 + 칼데라, 메사, 능선, 협곡(사행), 사구(비대칭 파형), 섬) 또는 높이맵 이미지(DirectXTex 로 R32 변환, 캐시). 경계 = lerp(max(|u|,|v|), 반지름, Roundness), BlendSize 만큼 smoothstep. 선택하면 Scene 뷰에 영역·섞기 경계를 지형 표면을 따라 그린다. GameObject > 3D Object > Terrain Stamp > 모양, 또는 Generate 도구의 Add Stamp(씬 루트, 첫 지형 가운데에 지형 너비 30%).
- UI `TerrainEditor` 의 6 번째 도구(Generate, 산 아이콘): Enable Generator(처음 켜면 기본 스택: 수력+열 침식, 규칙 3 개), Auto Update/Regenerate, 상태, Base(옥타브 막대를 위아래로 끌기), Stamps(씬 목록 → 선택, Add Stamp), Filters(켜기·순서·삭제·Add Filter, 종류별 값 표 `TerrainGenFilter::Params`), Materials(레이어 드롭다운, 높이/경사/퇴적/노이즈). 설정 Undo = `Undo::WatchAsset("terraingen:<경로>")` (되돌리면 해시가 바뀌어 다시 생성), 스탬프 이동 Undo = 씬 Undo.
- 측정(Release, 513² 1 km 지형, 스탬프 5): Base 13~18 ms, 스탬프 4~5 ms, 수력(12만 방울)+열 침식 약 450 ms, 재질 12~14 ms, 미리보기 약 36 ms.
- `NOVA_DEV_PROFILE=1` 이면 단계별 높이 범위(min/max/mean)와 미리보기 시간도 Editor.log 에.
- 지형 셰이더 triplanar(`40. TerrainCommon.fx` `TerrainTriplanarSetup/TerrainLayerSample`): 레이어마다 위(xz)·옆(zy, xy) 투영을 |법선|^4 로 섞고, 옆 가중치 < 2% 는 버려 평지는 샘플 1 번. 컨트롤 가중치 0 인 레이어도 분기로 건너뜀. 분기 안 밉을 위해 좌표 미분을 밖에서 구해 `SampleGrad`. 본 패스 `TerrainPS` 가 `TerrainAlbedo(uv, 지형 로컬 위치, 법선)` 로 부른다.
- 미구현/다음: 텍스처 반복 무늬(anti-tiling, 가까운 곳), 스플라인 스탬프(도로·강), 스탬프별 스플랫 규칙, GPU 생성, 손으로 칠한 높이를 스택의 조각 레이어로.

**바이옴 (2026-10-01)**: World Creator 의 색 재질 + Biome Layer 참고. 프리셋 패키지 + 씬의 영역 오브젝트.
- 색 재질: `TerrainGenMaterialRule` 에 `Mode`(Texture / Color / Gradient), `Color`, 그라디언트(입력 = Height / Slope / Flow / Sediment / Cavity / Noise, 정지점 목록), `ColorVariation`(큰 얼룩 약 250 m = 밝기·색온도, 작은 얼룩 약 16 m = 밝기), 마스크 `Flow`(수력 침식 물길, 제곱근 + 98% 정규화, smoothstep 0.2~0.7 = 물이 모인 줄기만), `Cavity`(-1 볼록 ~ +1 오목, 라플라시안 4 칸). 결과 = 스플랫 + **컬러 맵**(RGBA8, 제어 맵 해상도: rgb = 색, a = 색이 텍스처 색을 대신하는 정도). 마지막에 캐비티 음영(오목 × (1-0.22), 볼록 × 1.07). .terraindata **v4** = v3 + 컬러 맵 바이트 수·데이터. 셰이더(`TerrainAlbedo(..., viewDist)`) = lerp(텍스처, 컬러 맵 색 × 텍스처 명암(텍스처 밝기 / 가장 작은 밉 평균 밝기, 0.35~1.9), a), 멀수록(60 m~460 m) 명암 비중을 75% 줄여 타일 반복이 줄무늬로 보이지 않게.
- Base 에 `Dunes`(6) 추가: 바람 방향(`WindAngle`)으로 늘어선 비대칭 파형(완만한 바람받이 75% + 가파른 미끄럼면 25%), 노이즈로 휘고 높이가 다름. 옥타브 1 = 큰 사구, 2 = 작은 사구, 나머지 = 바닥 기복.
- 프리셋 `TerrainBiomes`(`Source/Terrain/TerrainBiomes.*`): `Resources/Packages/Terrain/Biomes/*.biome`(JSON = 생성기 설정 키 + name, description, layers(지형 레이어 4 개), paintMaterials) 10 종 — Alpine Mountains, Arctic Tundra, Badlands, Desert Dunes, Grassland Hills, Highland Moor, Red Rock Canyon, Savanna, Tropical Islands, Volcanic Highlands. 모두 일반 레이어 Grass/Rock/Dirt/Sand 를 쓰고 모습은 색 규칙이 만든다. 값은 1 km × 600 m 기준 미터(지형이 크면 같은 크기 지형이 반복될 뿐, 경사·재질은 같게). 기복은 크기의 약 1/4 이하(그 이상이면 칼날 능선). `Apply(data, preset)` = 설정 + 레이어 교체(Enabled/AutoUpdate 유지, `BiomePreset` 이름 기록). 썸네일 = 129² 로 실제 생성한 위에서 본 음영(백그라운드, 한 번, IMMUTABLE 텍스처).
- 영역 `TerrainBiome` 컴포넌트(`Source/Scene/TerrainBiome.*`): Preset 이름, Opacity, Blend Size, Roundness, Edge Noise, Affect Heights / Materials, Seed, Order. 생성 순서: 지형 Base → 바이옴마다 마스크(스탬프와 같은 회전 사각형/원 + 경계를 Fbm 으로 흔듦) 안을 그 프리셋 Base 로 lerp → 스탬프 → 지형 필터 / 바이옴마다 같은 스탬프 후 높이에서 그 프리셋 필터를 따로 돌려 높이·퇴적·물길을 마스크로 lerp → 재질: 지형 규칙과 바이옴 규칙을 따로 평가해 가중치·색을 마스크로 섞음(바이옴의 높이 그라디언트는 영역 안 범위). 프리셋 재질 규칙의 레이어 번호는 같은 에셋 경로의 지형 레이어 번호로 바꿈(없으면 같은 번호, 범위 밖이면 색만). 해시 = 행렬 + 값 + 프리셋 JSON. GameObject > 3D Object > Terrain Biome > 프리셋, Generate 탭 Biome Areas > Add Biome Area, 프리셋 썸네일 오른쪽 클릭 > Add as Biome Area. 선택하면 Scene 뷰에 영역(초록)·섞기 경계.
- UI(Generate 탭): Biome Presets 격자(썸네일, 현재 프리셋 강조, 설명 툴팁, 클릭 = 지형 전체 적용 + Undo "Apply Biome …"(설정 + 레이어 목록을 같이 되돌림)), Biome Areas 목록, 재질 규칙에 Name·Flow·Cavity·Color 모드·Tint·그라디언트 정지점 편집·Variation.
- 측정(Release, 513²): 프리셋 하나 0.05~1.1 초(사구는 침식이 없어 56 ms), 바이옴 영역 3 개 지형 3.3 초(필터 2.0 초 — 바이옴마다 필터를 지형 전체 크기로 다시 돌림).
- 검증 도구: `NOVA_DEV_BIOMEDUMP=<폴더>` 면 시작 뒤 한 번 프리셋마다 513² 로 생성해 `<폴더>/biome_<이름>.ppm`(위에서 본 음영) + Editor.log 에 시간·높이 범위·튀는 점 수(8 이웃보다 2 m 넘게 높거나 낮은 칸). 3D 뷰의 "떠 있는 조각"이 높이 버그인지 실제 지형인지 가리는 데 썼다(튀는 점 0 → 칼날 능선이 원인, 기복을 낮춤).
- (물이 판 지형) 지형 생성기는 필터 뒤에 호수·강 바디(Carve Terrain)를 판다 → 아래 "물" 참고.
- World Creator 와 비교해 남은 차이: 해상도(513² = 2 m 칸, WC 는 GPU 로 4K+), 침식의 잔 물길 디테일, 가까운 곳 텍스처 반복, 하늘·안개·노출(엔진 조명), 바이옴 필터를 영역 범위만 돌리기(지금은 전체), 레이어 4 장 제한.
- **ImGui 멀티 뷰포트 버그 수정**: 떠 있는 팝업·툴팁이 창 밖으로 나가면 ImGui 가 별도 OS 창을 만들고 `RenderPlatformWindowsDefault` 가 그 창의 RTV 를 묶은 채로 끝나, 다음 프레임부터 메인 창 UI 가 그쪽에 그려져 화면이 멈춘 듯 보였다(긴 툴팁으로 재현). App 루프가 매 프레임 ImGui 그리기 직전에 메인 백버퍼 RTV 를 다시 묶는다. Present 실패는 Editor.log 에 한 번 기록.

**물: 바다·호수·강 (2026-10-01)**: Crest(카메라 LOD 격자, 얕은 물 파도 감쇠, 산란/SSS), KWS(흐름 노멀, 코스틱), Unreal Water(Water Body 종류, Gerstner 생성기, 지형 파기, 부력 폰툰) 참고. 모듈 `Source/Water/`(CMake include 에 추가), 셰이더 `Shaders/46. Water.fx`, 패키지 `Resources/Packages/Water/`.
- 컴포넌트 `WaterBody`(`Source/Water/WaterBody.*`): Type Ocean / Lake / River, Profile 이름, Wave Scale, Flow Speed, Points(로컬: 위치, 강은 점마다 Width·Depth·Speed), Carve Terrain / Carve Depth / Bank Width. `Curve(step)` = Catmull-Rom(호수 닫힘, 강 열림)을 간격대로 찍은 월드 점(접선·폭·깊이·유속·누적 거리). `Sample/Query(world)` = 물 위 높이·법선·흐름(바다·호수 = 기준 수면 + Gerstner CPU 식, 강 = 가장 가까운 가운데 선 조각). `SnapToGround()` = 강 점을 땅 - 0.6 m(하류로 갈수록 0.05 m 이상 낮게), 호수 수위를 윤곽 위 가장 낮은 땅 - 0.3 m 로. 땅 = `TerrainData::UncarvedHeights`(생성기가 물로 파기 전 높이, 저장 안 함, `GetUncarvedHeight`) → 맞추기를 반복해도 가라앉지 않는다. GameObject > 3D Object > Water > Ocean/Lake/River(`GameObjectFactory::CreateWaterBody`, 바다 = 지형 높이 아래 20 %, 강은 만들 때 SnapToGround), Add Component > Water.
- 파도 `WaterWaves`(`Source/Water/WaterWaves.*`): 바람 속도 → Pierson-Moskowitz 마루 파장(주기 0.81·2πU/g), 최대 파장 = 마루 × 1.8, 최소 파장까지 로그 간격 + ±15% 흔들기, 진폭 = Hs(0.21U²/g) 기준 마루 파도에 몰리게(짧으면 r^1.5, 길면 e^-2.5(r-1)), 경사 kA ≤ 0.12, 방향 = 바람 ± Spread × (0.75 + 0.6t), Q = Choppiness / (kA·N) → ΣQkA = Choppiness(≤ 1 이면 고리 없음). 최대 32 개. `Height()` = 가로 이동을 4 번 되짚어 (x, z) 바로 위 높이 + 법선. `Time()` = 프로그램 시작부터 초(에디터에서도 흐른다).
- 그리기 `WaterRenderer`(`Source/Water/WaterRenderer.*`), `EditorApp::DrawWater` 가 Game·Scene 뷰 모두 하늘 다음·격자/입자 전에 부른다(Scene 뷰 와이어프레임이면 생략). 뷰 깊이 버퍼를 `R24G8_TYPELESS` + 읽기 전용 DSV(`D3D11_DSV_READ_ONLY_DEPTH|STENCIL`) + SRV(`R24_UNORM_X8`)로 바꿨다 → 물은 깊이 검사하면서 같은 깊이를 읽는다. 순서: 화면 색 복사(대상 텍스처와 같은 크기·형식, `CopyResource`) → 지형 높이 지도(활성 지형 합친 범위 256², 지형 Revision 이 바뀔 때만) → 카메라가 물 아래면 전체 화면 수중 패스 → 바디마다 그리기(불투명 쓰기, 깊이 쓰기 없음) → 같은 바디를 깊이만 다시 그림(입자·격자가 물에 가려지게). 상태(블렌드·DSS·RS)는 되돌린다.
  - 바다 격자: 레벨 10 개(칸 0.5 m × 2^L), 레벨마다 -64~64 칸, 레벨 1~ 은 가운데 -30~30 을 비운 고리(안쪽 레벨과 2 칸 겹침, 거친 레벨부터 그려 고운 레벨이 덮음). 정점 = (칸 좌표, 레벨), 원점은 카메라를 2 칸 단위로 맞춤, 바깥 띠(|좌표| 48~62)는 짝수 좌표로 모핑. 정점에서 파장 < 칸 × 4 인 파도는 뺌. 16.6 만 정점, 26.3 만 삼각형.
  - 호수 = 윤곽 곡선(3 m) 귀 자르기 삼각형(수면 높이 평면, 꼬이면 남은 것은 부채꼴), 강 = 가운데 선(2 m) × 가로 9 정점 띠(흐름 = 접선 × 유속 × (1 - 0.6s²), UV = 가로 0~1·거리 m, Edge = |s|). 바디별 메시는 점·행렬·프로파일 해시가 바뀔 때만 다시 만든다.
- 셰이더(`46. Water.fx`): 법선 = Gerstner(픽셀, 발자국 × 3 보다 짧은 파도 뺌, 지형 높이 지도로 얕으면 감쇠) + 잔물결 노멀 2 장(바람 방향으로 흘림, 멀면 약하게) / 강 = 2 위상 흐름 노멀(주기 2 초). 깊이 = 하드웨어 깊이 → 뷰 z = _43/(d - _33), 물속 거리 = (장면 z - 물 z) × 거리/물 z. 굴절 = 법선으로 흔든 픽셀(물 앞 물체면 원래 픽셀). 흡수 σ = -ln(Absorption)/Clarity, 길이 = 시선 두께 + 수직 깊이, 산란 색 × (해 + 하늘 주변광), 탁도 = 산란이 바닥을 가림. 코스틱 = 바닥 월드 xz(해 방향으로 깊이만큼 밀림)에 텍스처 두 번 min, Caustics Depth 까지. SSS = 해를 등지고 볼 때 파도 마루(정점 높이/최대 진폭). 반사 = 하늘 큐브맵 + Blinn 해 반사 × 프레넬(Schlick 0.02). 거품 = 야코비안 < 0.6(흰 물결) + 물가(두께 < Shore Foam, 밀려오는 줄) + 강(흐름 거품 × 유속 × 가장자리). 아주 얕은 곳(0.35 m)은 화면 색으로 부드럽게. 수중 = 장면 거리와 수면까지 중 짧은 것만큼 흡수(σ × 1.6 + 산란 소광)·산란, 깊을수록 어두움, 바닥 코스틱. 수면 아랫면 = 임계각 안은 위 화면(스넬의 창), 밖은 전반사(물 색).
- 프로파일 `WaterProfiles`(`Source/Water/WaterProfile.*`): `Resources/Packages/Water/Profiles/*.waterprofile` 10 종. 텍스처 `Resources/Packages/Water/Textures/`: WaterNormalA/B(주기 FFT 높이장의 정확한 미분 → 이음매 없음), WaterFoam(감싸는 Worley F2-F1 두 크기 + 저주파 마스크), WaterCaustics(높이장 기울기만큼 광선을 옮겨 감싸는 격자에 쌓음 → 실선 그물). 만든 스크립트는 스크래치(numpy) — 다시 만들 일이 있으면 같은 방법으로.
- 편집 `WaterEditor`(`Source/Editor/WaterEditor.*`): Inspector 의 Edit Points 를 켜면 Scene 뷰에서 점 핸들(강 Source/Mouth 표시), 끌기 = 지형 레이캐스트(없으면 수면 평면) 위로(강 = 땅 - 0.6 m, 호수 = 수면 높이), Ctrl+클릭 = 가장 가까운 변에 끼움(강 끝 바깥이면 늘림), Shift+클릭/Delete = 지우기(호수 3 개, 강 2 개 이하로는 안 됨). 켜져 있으면 `SceneGizmoTools::SetSuppressed`. Undo 는 씬 Undo(컴포넌트 JSON)가 잡는다.
- 지형 파기: `TerrainGenerator::Input::Water`(`WaterCarveInput` = 곡선 점(x, z, 수면 y, 폭) + 깊이 + Bank). 필터 다음·재질 전에 `CarveWater`: e = 물 안쪽 거리(밖이면 음수), 바닥 = 수면 + 0.6 - (깊이 + 0.6)·smoothstep(0, Bank, e), 밖은 수면 + 0.6 + |e| × 0.35(약 19°), 지형을 낮추기만. 파기 전 높이는 `Output::Uncarved` → `TerrainData::UncarvedHeights`. 입력 해시에 물 곡선 포함(점을 끌면 다시 판다).
- 부력 `Buoyancy`(`Source/Water/Buoyancy.*`): 같은 GameObject 의 RigidBody 에 FixedUpdate 마다 폰툰 5 개(Size 상자 바닥 네 모서리 + 가운데)의 잠긴 비율(수면 - 높이)/(월드 상자 높이)로 위로 m·g·Float Strength·2·sub/5(절반 잠기면 무게와 같음), 폰툰 속도(v + ω×r) - 흐름 × Flow Force 에 맞서는 저항, 잠긴 만큼 회전 저항. `NOVA_DEV_PROFILE=1` 이면 1 초마다 높이·수면·잠김·속력을 Editor.log 에.
- 측정(Release, 1080p Scene 뷰, 바다 + 호수 + 강 + 1 km 지형): 프레임 약 1.0 ms(950~1030 FPS). 생성기 지형 + 물 파기 0.6 초.
- 미구현/다음: 물에 그림자 받기, 화면 공간 반사(SSR, 지금은 하늘만 반사), 거품 시뮬레이션(쌓이고 사라지는 텍스처, 지금은 매 프레임 식), FFT 바다, 강 급류(경사에 따른 하얀 물살·물보라 입자), 물가 물결(해안으로 밀려오는 파도 방향), 수중 빛줄기, 물 상호작용(물체가 만드는 물결), 바다 바디가 지형 아래 웅덩이까지 채우는 문제(Unreal 의 Water Zone/마스크처럼 바다 영역 제한).

**숲: 인스턴싱 + LOD + Paint Trees (2026-10-01)**:
- 구조: `TreeDesc`(`Source/Scene/TreeDesc.*`) = 나무 한 종류의 설정(모양 TreeParams + 수피·잎 색 + 바람 + LOD 거리 + Cast Shadows, Inspector·JSON·프리셋). `Tree` 컴포넌트는 TreeDesc 하나를 갖고, 지형(`TerrainData::TreePrototypes`)도 TreeDesc 목록을 갖는다. 그리기는 모두 `TreeRenderer`(`Source/Scene/TreeRenderer.*`)가 맡고 Scene 의 각 패스 끝에서 `TreeRenderer::DrawAll(pass, editor)` 한 번(본 패스·그림자·SSAO 깊이 × Game/Scene).
- 모으기: 켜진 Tree 컴포넌트(`Tree::All()`) + 활성 지형의 나무 인스턴스. 같은 `TreeDesc::Hash()`(모양+보이는 값)끼리 묶고, 종류마다 메시·범위를 한 번 준비(`prepare`)한 뒤 나무마다 절두체(그림자는 가까운 면 제외 5 면) → LOD. 인스턴스 = 월드 행렬 + (색 변화, 바람 위상, LOD 섞기 문턱, 쪽) 80 바이트. 지형 나무 월드 행렬은 높이·나무·위치가 바뀔 때만 다시 계산(`TerrainCache`).
- LOD: 거리 ÷ 나무 높이 배율로 LodDistance(35) / BillboardDistance(90) / CullDistance(800) 비교. 경계 앞뒤 10% 는 두 단계 모두 그리고 `TreeLodClip`(화면 디더, 나가는 단계 = 디더 ≥ t, 들어오는 단계 = 디더 < t)로 나눈다. 그림자는 섞지 않고 한 단계. LOD1 = `TreeGenerator::Generate(params, out, 1)`: 난수 순서는 같게 두고 링 변을 줄이고(줄기 절반, 가지 4/3 변) 0.6 m 미만 잔가지를 빼고 잎 카드를 절반(1.45 배 크게) → 같은 seed 면 모양이 같다.
- 임포스터: 처음 필요할 때 LOD0 을 물체 공간(바람 0)에서 8 방향 정사영으로 알베도(감마)+법선(물체 공간, a = AO) 아틀라스(2048x256, 밉 6)에 MRT 로 굽는다(`TreeBarkBakeTech/TreeLeafBakeTech`). 그릴 때는 정점 버퍼 없이 인스턴스마다 사각형 6 정점(`TreeImpostorVertex`): 바라볼 방향(카메라, 그림자 패스는 빛 방향 `gShadowLight`)에 가까운 프레임을 고르고 Y 축 기준으로 돌린다. 법선을 인스턴스 회전으로 월드로 바꿔 `ShadeLit` 로 다시 조명(그림자·SSAO 받음). 알파 문턱 0.35.
- 셰이더: `44. TreeCommon.fx` 의 `TreeInstanceIn`(WORLD0~3 + INSTANCE, 슬롯 1), `TreeWorldPos(v, inst)`, 줄기 흔들림은 인스턴스 높이 배율에 비례. InstancedBasic 의 나무 PS 는 표면 함수(`TreeBarkSurface/TreeLeafSurface`)로 나눠 본 패스와 굽기가 같이 쓴다. 색 변화(인스턴스 x)는 잎 0.78~1.22, 수피 0.9~1.1 배.
- 지형 데이터: `.terraindata` 버전 2 = 뒤에 프로토타입 JSON 문자열 + `TerrainTreeInstance`(X, Z 0~1, 높이·폭 배율, 회전, 색 변화, 종류) 배열. 버전 1 파일도 읽는다. `ReadString` 한도를 16 MB 로(프로토타입 JSON).
- Paint Trees(`TerrainEditor`): 종류 목록(썸네일 = 임포스터 앞면, `TreeRenderer::Thumbnail`), Add Tree(프리셋)/Remove, Brush Size, Tree Density(최소 간격 8 m → 1.5 m), Height/Width Min·Max(Lock Width), Color Variation, Random Rotation, Mass Place Trees(개수), Remove All, 통계(보이는 수 / LOD 별 / 드로 콜), 고른 종류의 설정(바꾸면 그 종류 전부 바로). 클릭·끌기 = 브러시 반지름 1/4 마다 빈 곳에 간격을 지키며 추가, Shift = 지우기, Ctrl = 고른 종류만 지우기, 한 획 = Undo 한 단계.
- 개발용: `NOVA_DEV_TREES=<수>` — 첫 지형에 (없으면) Oak/Pine/Birch 프로토타입을 넣고 그만큼 흩뿌림(저장 안 함). 검사 씬: scratchpad `make_forest_scene.py` → ScriptTest `Assets/Scenes/Forest.scene`(400 x 60 x 400 언덕, 풀/흙 레이어).
- 측정: 나무 1500 그루(Oak/Pine/Birch) Scene 뷰 230 FPS. 손으로 두 번 칠한 85 그루 = 드로 콜 5.
- 미구현: 나무 충돌(Terrain Collider 에 나무), 나무 GPU 컬링/간접 그리기, 임포스터 프레임 사이 섞기(지금은 가까운 프레임 하나), 바람 영향 받는 임포스터, Paint Details(풀).

**Scene 뷰 격자 (2026-10-01)**: 예전에는 `SceneViewOverlay::DrawGrid` 가 ImGui 선을 이미지 위에 덧그려 물체 앞에도 보였다(기둥·줄기가 반투명해 보임). 지금은 `Source/Editor/SceneGrid.*` + `Shaders/45. SceneGrid.fx` 가 `_Editor_OnSceneRender` 에서 불투명 물체·하늘 다음, 입자 전에 y = 0 평면(카메라 주변 ±80)을 깊이 검사(LessEqual, 쓰기 없음, 바닥 메시와 겹침 방지용 음수 DepthBias)하며 그린다. 선 = fwidth 1 픽셀, 10 칸마다 진한 선, 70 유닛에서 사라짐, 칸이 3 픽셀보다 작으면 가는 선을 지움. 색·알파(92,98,106 / 0.55·0.85)는 예전과 같다. 툴바 Grid 토글을 따른다.

**나무 생성기 (Tree, 2026-10-01)**: 텍스처 없는 절차적 나무 (SpeedTree / Weber-Penn 방식 + Unreal 식 계층 바람)
- 생성(`Source/Scene/TreeGenerator.*`): `TreeParams`(Seed, 줄기 Height/Radius/TipRadius/Flare/Gnarl/Lean/RadialSegments, 수관 Crown 7 종, 가지 단계 L[0..2]: Count/Start/Angle(±)/Length(±)/Gravity/Up/Radius/Gnarl, 잎 Shape/LeafCards/LeavesPerCard/LeafCardSize/LeafStart). `Grow()` 재귀: 경로(무작위 휘어짐 + (Up−Gravity) 굽힘) → 링을 평행 이동 틀로 이은 관 + 끝 마개 → 자식은 황금각 137.5° 로 돌려 붙이고, 1 단계 길이는 수관 모양 함수(r = 수관 아래 1 ~ 꼭대기 0)로. 마지막 단계 가지에 잎 카드(가지에서 바깥으로 기울임, 무작위 비틀기). `FinishLeaves()` 가 잎 법선을 수관 구 쪽으로 70% 굽히고(양면 같은 법선 → 덩어리가 부드럽게 빛을 받음) 안쪽 깊이·아래쪽으로 AO. 삼각형은 기준 법선으로 감기 방향을 맞춘다. 상한: 가지 4000, 잎 카드 12000.
- 정점(80 바이트, `TreeVertex` ↔ 셰이더 `TreeVertexIn`): Pos, Normal, UV(수피: 둘레 0~1·길이 m / 잎: 카드 0~1), Wind(줄기 가중 = (y/H)², 1차·2차 가지 가중, 잎 떨림), Axis(가지 방향 또는 카드 위쪽, w = AO), Phase(1차·2차 위상, 잎 무늬 시드, 수피 밑동 반지름). 자식 가지는 붙은 지점의 부모 가중·위상을 물려받아 관절이 떨어지지 않는다.
- 컴포넌트(`Source/Scene/Tree.*`): Params + 수피(Color, Moss, Ridge Depth/Frequency, Fleck(음수 = 자작나무 어두운 줄무늬), Smoothness) + 잎(Color/Color 2/Variation/Transmission/Smoothness/Leaf Length) + 바람(Strength, Direction, Trunk/Branch Sway, Leaf Flutter) + Cast Shadows. 모양 JSON 이 같으면 GPU 메시를 같이 쓴다(weak_ptr 캐시). 씬에 "shape" 가 없으면 preset + seed 로 만든다. `Tree::UpdateAll()`(App 루프)이 바람 시간을 프레임마다 한 번 정한다 — 깊이 사전 패스와 본 패스가 같은 값을 써야 한다.
- 구운 텍스처(`Source/Scene/TreeTextures.*`): 처음에는 픽셀마다 잎 SDF 를 반복 계산했는데 바늘잎(카드당 16 장)이 그림자 캐스케이드 4 장·깊이 패스까지 곱해져 매우 무거웠다(같은 장면 104 FPS). 지금은 실행 중에 한 번 굽고 셰이더는 한 번 읽는다(258 FPS). 잎 아틀라스 = 2x2 변형(카드 시드로 고르고 좌우 뒤집기), 셀 256(바늘 512), r 잎맥 거리 / g 잎 안 위치 / b 잎 번호(0 = 잔가지) / a 덮임, 밉맵은 알파 > 0.5 비율을 원본과 같게 맞춰 멀리서 잎이 사라지지 않는다. 키 = (모양, 카드당 잎 수, 잎 길이) — 최근 12 개만 둔다. 수피 = 256 타일(가로·세로 8 칸, 양쪽 주기 노이즈) rg 기울기 / b 높이, 둘레에 정수 번 둘러 이음매 없음. 굽기 시간은 Editor.log `[Tree] leaf texture baked ...` (바늘 약 220 ms, 넓은잎 60 ms, 여러 코어).
- 셰이더: `Shaders/44. TreeCommon.fx`(cbTree, 바람 `TreeWindOffset`/`TreeWorldPos`, 값 노이즈(이끼·반점), 구운 텍스처 `TreeLeafSample`/`TreeLeafClip`/`TreeBarkSample`)를 세 효과가 include: InstancedBasic `TreeBarkTech`/`TreeLeafTech`(LessEqual 깊이, 잎 CullNone), BuildShadowMap `TreeShadowBarkTech`/`TreeShadowLeafTech`(ApplyShadowBias, 잎 clip), SsaoNormalDepth `TreeNormalDepth*`. 본 패스와 사전 패스 모두 `월드 위치 × CPU 에서 곱한 ViewProj`.
- 조명 공용화: InstancedBasic 의 Lit PS 조명을 `ShadeLit(LitSurface, posW, N, V, ssaoPosH)` + `FinishLit` 로 뺐다(메시·나무 공용). `LitSurface.Transmission` = 잎 투과(뒷면 감싸기 + 역광, 하늘 반대쪽 환경광).
- 에디터: GameObject > 3D Object > Tree(`GameObjectFactory::CreateTree`, Oak), Add Component > Miscellaneous > Tree, Scene 뷰 클릭 선택(`Tree::RaycastLocal`, 바람 전 삼각형)·F 포커스(`GetLocalBounds`). Scene 의 그림자/노멀깊이/에디터 패스에 Tree 호출 추가(게임 본 패스는 Component::Render).
- 로그: `[Tree] generated seed N: V vertices, T triangles, B branches, C leaf cards (ms)`. 참나무 ≈ 8k 정점 / 10k 삼각형 / 5 ms.
- 검사: scratchpad `make_tree_scene.py` → ScriptTest `Assets/Scenes/Trees.scene`(프리셋 4 + 숲 10 그루).
- 미구현: LOD/빌보드 임포스터, 인스턴싱(같은 메시 여러 그루를 한 번에), Wind Zone 컴포넌트(지금은 나무마다 바람), 지형 나무 칠하기, 가지 끝 곡률 보간, 뿌리.

**멈춤 감시(EditorLog)**: 메인 루프가 매 프레임 `EditorLog::Heartbeat()` 를 부르고, 감시 스레드가 4초 넘게 안 오면 메인 스레드를 잠깐 멈춰 호출 스택을 `[HANG]` 으로 남긴다(멈춘 동안은 주소만 모으고, 풀어 준 뒤 기호로 바꿔 기록). 디버거(cdb/procdump)가 없는 환경에서 무한 루프·교착을 찾는 용도. 모달 대화 상자/창 크기 조절 중에도 한 번 남을 수 있다. 관리 코드(C#) 예외로 .NET 이 프로세스를 끝낸 경우는 [CRASH]/[HANG] 이 없고 이벤트 로그를 봐야 한다.

**씬 반복 안전성**: `Scene::Enter/UpdateScene/LastFramUpdate` 는 오브젝트 목록의 **복사본**을 돈다 — 스크립트가 도중에 transform.SetParent(→ `RegisterGameObjectTree`)로 목록을 늘리면 반복자가 무효화되어 충돌하던 문제(0xC0000005). 스크립트가 만든 오브젝트(`AddToSceneLater`)에 그 사이 부모가 생겼으면 루트로 넣지 않는다.

**충돌 로그**: `EditorLog` 의 처리되지 않은 예외 필터가 DbgHelp 로 호출 스택(함수 + 파일:줄)을 `[CRASH]` 로 남긴다 — 원인 모를 충돌은 먼저 Editor.log 를 볼 것.

**Add Component / Component 메뉴**: `AddComponentMenu::kKnown` 표에 있는 네이티브 컴포넌트 + 프로젝트 C# 스크립트만 보인다. `REGISTER_SCRIPT` 로 등록된 C++ 예제 스크립트(Rotator, PlayerController, SineWaveMover)는 숨김(`AddComponentMenu::IsListed`), 상단 Component 메뉴도 같은 기준.

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
3-1. **WRL `ComPtr` 의 `&`**: `&comptr` 는 `ReleaseAndGetAddressOf()` — 담긴 포인터를 **해제**하고 `T**` 를 준다. `ComPtr<T>*` 가 필요하면 `std::addressof(comptr)`(머티리얼 텍스처가 사라지던 원인).
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
