# NOVA Claude 작업 상태

- 갱신 시각: 2026년 10월 4일 (LOD Group · Screen Space Reflection 완료, 커밋 — push 는 사용자 확인 뒤)
- 단계: **LOD Group (`83226d8`) · Screen Space Reflection 완료** — 사용자 지시 "LOD Group … Screen Space Reflection … 진행"
- 이전 단계: Depth of Field · Motion Blur (`11002fe`, push), Adaptive Probe Volume (`efbab1e`, push) — 아래 기록
- 기준 커밋: `c1b7646`
- Codex 분담: Joint 2D 완료 (작업 폴더에 아직 미커밋), 다음 후보 Tilemap — 공동 명세의 "Tilemap 명세 (Codex)" (사용자 확인 후 착수)

## Screen Space Reflection 담당 파일 (커밋에 넣은 것)

| 파일 | 왜 |
|---|---|
| 새 `Source/Graphics/DX11/ScreenSpaceReflection.*` | Volume 값 · 지난 프레임 장면 색 (밉) · 이펙트에 넣기 |
| `Source/Graphics/Common/VolumeProfile.cpp` | `ScreenSpaceReflection` 효과 (HDRP 이름) · 켜짐 규칙 |
| `Shaders/32. InstancedBasic.fx` | `cbScreenSpaceReflection` · `ScreenSpaceReflection()` · ShadeLit 반사 항 (꺼지면 예전과 같음) |
| `Source/Editor/EditorApp.cpp` | 두 뷰에서 Prepare · Bind (CustomShaders 포함) · StoreHistory |
| **공용** `Tools/tests/run_tests.ps1` | 새 `Suite-SSR` + 목록 · switch · 도움말에 한 단어 |
| 문서 | 새 `docs/SCREEN_SPACE_REFLECTION.md`, README · AGENT_HANDOFF 한 줄씩, 공동 명세의 Claude 줄 |

## Screen Space Reflection 검증 (Debug, 독립 빌드 `E:\NovaTest\ClaudeDecalEngine`)

- `ssr` **4/4**: 켜기 (반사 자리 빨강 0 → 100 %) · 매끈함 0.8 = 없음 · 카메라 옮긴 첫 프레임 = 안정 프레임 · Game 뷰
- 회귀 `render · gfx · shadergraph · decal · reflectionprobe · probevolume · lodgroup` **62/62**, OpenGL 같은 그림
- 쇼케이스 188

## Codex 에게 (Screen Space Reflection)

- 32 의 ShadeLit 반사 항이 `ScreenSpaceReflection` 을 거친다 (Volume 에서 켜지 않으면 `gSsrParams.x = 0` 으로 바로 돌아옴). 32 컴파일이 Debug 에서 5 초 남짓
- 새 렌더 경로 (다른 뷰) 를 만들면 `ScreenSpaceReflection::Prepare(..., capture=true, ...)` 로 끄거나 Bind 를 부르지 않으면 이전 뷰 값이 남을 수 있다

## LOD Group 담당 파일 (커밋에 넣은 것)

| 파일 | 왜 |
|---|---|
| 새 `Source/Scene/LODGroup.*` | 컴포넌트 (Unity 필드), 뷰마다 LOD 고르기, Inspector LOD 막대, CLI `nova lod` |
| `Source/Scene/Component.h` | 렌더러마다 `LodStamp` · `LodHidden` · `LodShadowHidden` · `LodFade` · `LodFadeBelow` (기본값 = 예전과 같음) |
| `Source/Scene/SceneCulling.*` | `LodStamp` · `ShadowPass`, `IsVisible` 이 LOD 숨김도 본다 |
| `Source/Scene/MeshBatcher.*` | `BeginView(capture)`, Collect 에서 `LODGroup::SelectForView`, 크로스페이드 렌더러는 묶음 밖에서 `gLodFade` 로 (묶음 그리기를 람다로) |
| `Shaders/32. InstancedBasic.fx` · `Shaders/28. SsaoNormalDepth.fx` | `gLodFade` + `LodFadeClip` (PS 첫 줄 — 기본 0 이면 그대로) |
| `Source/Editor/EditorApp.cpp` | `BeginView(probe)`, `LODGroup::RegisterEditor` |
| `Tools/NovaCli/main.cpp` | `lod` op 명령 + 도움말 한 줄 |
| **공용** `Source/Editor/AddComponentMenu.cpp` | Rendering > LOD Group 한 줄만 stage |
| **공용** `Tools/tests/run_tests.ps1` | 새 `Suite-LODGroup` + 목록 · switch · 도움말에 한 단어 |
| 문서 | 새 `docs/LOD_GROUP.md`, README · AGENT_HANDOFF · NOVA_CLI 한 줄씩, 공동 명세의 Claude 줄 |

## LOD Group 검증 (Debug, 독립 빌드 `E:\NovaTest\ClaudeDecalEngine`)

- `lodgroup` **7/7**: 기본값 · 거리별 LOD 0 / 1 / 2 / Culled · Cross Fade 반반 + 빈 픽셀 0 · Animate Cross-fading · Game / Scene 뷰 따로 · 끈 그룹 = 모두 · 저장 → 다시 열기
- 회귀 `render · gfx · shadergraph · decal` **39/39**, OpenGL 크로스페이드 확인 (빈 픽셀 0)
- 쇼케이스 187

## Codex 에게 (LOD Group)

- `SceneCulling::IsVisible` 이 LOD Group 이 숨긴 렌더러도 false 를 준다 (LOD Group 이 없으면 예전과 같음). 렌더러를 직접 그리는 새 경로를 만들면 이 검사를 쓰면 LOD 를 따른다
- `Component` 에 LOD 필드 5 개가 늘었다 (직렬화 안 함)
- `MeshBatcher::BeginView` 에 `capture` 인자 (기본 false)

## Depth of Field · Motion Blur 담당 파일 (커밋에 넣은 것)

| 파일 | 왜 |
|---|---|
| `Source/Graphics/Common/VolumeProfile.*` | `DepthOfField` · `MotionBlur` 효과 (URP 이름 · 필드), 켜짐 규칙, `ShowIfKey/ShowIfValue` (모드별로 보이는 칸) |
| `Source/Editor/VolumeEditor.cpp` | ShowIf 가 맞지 않는 칸 숨기기 |
| `Source/Graphics/DX11/PostProcessPass.*` | `DepthOfField` · `MotionBlur` 패스, 깊이 · 카메라 행렬, 지난 프레임 카메라 (프레임 번호로 한 번) |
| `Shaders/41. PostProcess.fx` | Gaussian 4 패스 · Bokeh 3 패스 · Motion Blur (픽셀 셰이더만) |
| `Source/Editor/EditorApp.cpp` | 두 뷰의 후처리에 깊이 · View · Proj 넘김 (7 줄) |
| **공용** `Tools/tests/run_tests.ps1` | 새 `Suite-DepthOfField` + 목록 · switch · 도움말에 한 단어 |
| 문서 | 새 `docs/DEPTH_OF_FIELD_MOTION_BLUR.md`, README 후처리 줄, AGENT_HANDOFF 한 줄, 공동 명세의 Claude 줄 |

C# 네이티브 표 · `Source/Physics2D/` · 씬 파일 · `CliCommands.cpp` · `GameObject.*` · Codex 의 Joint 2D 파일은 고치지 않았다. `docs/AI_COLLABORATION.md` 는 Codex 의 미커밋 변경이 같이 있어 커밋에는 내 줄만 넣었다.

## Depth of Field · Motion Blur 검증 (Debug, 독립 빌드 `E:\NovaTest\ClaudeDecalEngine`)

- `depthoffield` **5/5**: Gaussian (먼 상자 80.9 → 7.2, 가까운 상자 그대로) · Bokeh 초점 20 m (가까운 상자 46.4 → 6.7, 먼 상자 그대로) · Mode Off = 같은 그림 · Motion Blur Game 뷰 번짐 · Scene 뷰 Motion Blur 없음
- 회귀 `render · gfx` **10/10**, OpenGL 짧은 확인 (Gaussian · Bokeh · Motion Blur 같은 그림)
- 쇼케이스 186 (육각 보케)

## Codex 에게 (Depth of Field · Motion Blur)

- `PostProcessPass` 앞에 DoF · Motion Blur 가 들어갔다 — 켜지 않으면 예전과 같다 (Bloom · Uber 는 같은 입력)
- `VolumeParameter` 에 `ShowIfKey` · `ShowIfValue` 가 생겼다 (기본 빈 값 = 늘 보임)
- 다른 Volume 효과를 더할 때 `VolumeComponent::Create` 의 형식 목록 (알파벳 순) 에 같이 넣으면 된다

## Adaptive Probe Volume 담당 파일 (커밋에 넣은 것)

| 파일 | 왜 |
|---|---|
| 새 `Source/Scene/AdaptiveProbeVolume.*` | 컴포넌트 (Mode Global · Local, Probe Spacing, Cascades, Rays, Update Speed, Validity, Probe Volumes Options) |
| 새 `Source/Graphics/DX11/ProbeVolumes.*` | 단계 · 복셀 · 판 찍기 계획 · 다시 비추기 · 프로브 갱신 · 채우기 · 옮기기 · CLI `probevolume` |
| 새 `Shaders/55. ProbeVolume.fx` | Inject · Relight · Update · Resample · Dilate (픽셀 셰이더만 — GL 에 UAV 가 없다) |
| `Shaders/32. InstancedBasic.fx` | `ProbeVolumeAmbient` (확산 환경광), `GIBlocked` (면 평면 벽 검사), 하늘 반사 가림, 알베도 찍기 (`gGIParams.z`) |
| `Source/Editor/EditorApp.*` | `CaptureGIView` (Game 뷰 Mode 2), 두 뷰에 `SetFocus` · `Bind`, `RenderApplication` 에서 `ProbeVolumes::Update` (Reflection Probe 보다 먼저) |
| `Source/Scene/SceneCulling.*` | `ChangedBounds()` — 옮겨지거나 생기거나 지워진 렌더러의 상자 (APV 가 그 근처만 다시 찍음) |
| `Source/Scene/MeshRenderer.cpp` · `SkinnedMeshRenderer.cpp` | 레거시 Light Probes 드롭다운 (Proxy Volume · Custom Provided · Anchor Override) 제거 → "Adaptive Probe Volume" 표시 |
| `Tools/NovaCli/main.cpp` | `probevolume` op 명령 + 도움말 한 줄 |
| **공용** `Source/Editor/AddComponentMenu.cpp` | Rendering > Adaptive Probe Volume 한 줄만 stage |
| **공용** `Source/Editor/GameObjectMenu.cpp` | Light > "Light Probe Group" (비활성) → "Adaptive Probe Volume" |
| **공용** `Tools/tests/run_tests.ps1` | 새 `Suite-ProbeVolume` + 목록 · switch · 도움말에 한 단어 |
| 문서 | 새 `docs/ADAPTIVE_PROBE_VOLUME.md`, README · AGENT_HANDOFF · NOVA_CLI 한 줄씩, REFLECTION_PROBE 한 줄, 공동 명세의 Claude 줄 |

C# 네이티브 표 · `Source/Physics2D/` · 씬 파일 (`Scene.cpp` · `SceneManager.cpp`) · `CliCommands.cpp` · `GameObject.*` · Codex 의 Joint 2D 파일은 고치지 않았다.

## 검증 (Debug, 독립 빌드 `E:\NovaTest\ClaudeDecalEngine` = `10bbdac` + 작업 폴더의 Codex Joint 2D 변경 + Claude APV)

- `probevolume` **7/7** (`TestResults/apv1`): 켜짐 (3 단계 복셀 준비) · 닫힌 방 안 0.008 vs 밖 · 화면 밝기 121 → 27 · 지붕 치우면 다시 밝아짐 (굽기 없이) · 색 번짐 · 벽을 초록으로 바꾸면 따라감 · 저장
- 회귀 `render · shadergraph · decal · reflectionprobe · packages` **56/56** (`TestResults/apv_reg1`)
- OpenGL 짧은 확인: 같은 그림 (닫힌 방 어두움), 55 변환 · 단계 준비 정상
- 성능 (Debug): APV CPU ~2 ms (평소), 194 fps / GPU 2.1 ms (작은 장면)
- 커밋한 파일은 독립 빌드와 내용이 같다

## 공용 빌드 · 테스트 에디터 사용

- **공용 `build/` · `Binaries/` 는 사용 안 함.** Claude 의 테스트 에디터는 모두 닫았다 (10월 4일 02시 59분 셰이더 컴파일 오류로 테스트 에디터 하나가 Vertex.cpp 어설션 대화상자를 띄웠다 — 사용자가 봤고 그 프로세스는 끝났다. 이후 셰이더는 fxc 로 먼저 컴파일)

## Codex 에게

- `Shaders/32. InstancedBasic.fx` 의 ShadeLit 확산 환경광이 `ProbeVolumeAmbient` 를 거친다 (APV 가 없으면 예전과 같은 하늘). 32 컴파일이 Debug 에서 10 초 남짓으로 늘었다 (fxc 기준 6 → 9 초)
- `SceneCulling.*` 에 `ChangedBounds()` 를 더했다 (Track · 지우기에서 상자만 모음 — 동작 변화 없음)
- `MeshRenderer.cpp` · `SkinnedMeshRenderer.cpp` Inspector 의 Light Probes 칸만 바꿨다 (JSON `lightProbes` 는 그대로)
- Tilemap 명세는 공동 명세에 그대로

## 편집 중인 공용 파일

- 없음. `docs/AI_COLLABORATION.md` · `AddComponentMenu.cpp` 는 Codex 의 미커밋 변경과 같은 파일이라 커밋에는 내 줄만

## 지난 작업

- Reflection Probe (`10bbdac`, push), Decal Projector (`ce15018`, push)
