# NOVA Codex와 Claude 공동 작업 명세

같은 저장소에서 Shader Graph와 씬 안정성 작업을 병행하기 위한 파일 분담, 완료 조건, 상태 공유 절차를 정리한다. Claude는 Shader Graph를 맡고 Codex는 씬 저장과 Play/Stop 복원을 맡는다. Claude가 자신의 상태 문서에서 이 파일 분담에 동의했다.

작성 기준은 2026년 10월 3일 02시 28분 KST, Git 커밋 `8e4a8b3`과 당시 작업 폴더의 변경이다. 실제 저장소는 `E:\GitHub\Nova-Game-Engine`이다. 작업이 계속 진행되므로 각 담당자는 착수 전에 현재 코드와 상태 문서를 다시 확인한다.

## 현재 허용된 범위

사용자가 Claude의 명세 확인을 알리고 Codex에도 겹치지 않는 작업을 진행하라고 지시했다. Codex는 아래 씬 소스와 별도 회귀 검사 작업을 진행한다. 공용 빌드와 테스트 에디터는 Claude가 사용 중이므로 사용 순서를 정한 뒤 검증한다. 각자의 최신 진행 상태는 담당자 상태 문서를 우선한다.

Claude가 별도 대화에서 받은 작업 지시는 여기서 추정하지 않는다. Claude는 자신의 사용자 지시에 따라 현재 작업을 진행하고 실제 범위를 상태 문서에 기록한다.

## 담당 범위 제안

| 담당 | 작업 | 파일 범위 | 현재 상태 |
|---|---|---|---|
| Claude | Shader Graph와 그래프 재질 통합 | `Source/ShaderGraph/`, 그래프가 사용하는 렌더링과 재질 파일 (세부 목록은 `docs/ai-status/CLAUDE.md`) | 1단계 (`49b2632`) · 2단계 (`ffbf792`, 미리보기 · 투명 · 컷아웃) · 3단계 (Vertex · Sub Graph · Custom Function) 완료 — Claude 갱신 (10월 3일 03시 52분) |
| Claude | 그래프를 게임 빌드에 포함하는 연결 작업 | `Source/Build/BuildPipeline.cpp` | 완료 — 그래프 · Sub Graph · .hlsl · 그래프만 쓰는 그림을 포함, 빌드한 게임 실행 확인 (2단계) |
| Codex | 저장하지 않은 씬의 Play/Stop 복원 | `Source/Scene/SceneManager.cpp` | 수정 완료, 독립 빌드와 씬 검사 통과 |
| Codex | 외부 경로의 씬 저장과 불러오기 일치 | `Source/Scene/Scene.cpp` | 수정 완료, 외부 씬의 재시작 복원까지 통과 |
| Codex | 씬 동작 전용 회귀 검사 | 새 파일 `Tools/tests/scene_lifecycle.ps1` | 14/14 통과, 공용 검사 파일 보존 |
| Codex | 공동 명세와 자신의 상태 기록 | `AGENTS.md`, `CLAUDE.md`, 이 문서, `docs/ai-status/CODEX.md` | 작성 완료 |

Codex의 씬 작업은 Shader Graph API, 재질 직렬화, 렌더링, C# 바인딩을 바꾸지 않는 범위로 제한한다. 불러오기 실패 보호를 위해 변경이 없던 `Source/Scene/GameObject.cpp`의 JSON 복원 소유권 순서와 `Source/Editor/CliCommands.cpp`의 `scene-open` 처리만 추가로 맡는다. 이 두 파일은 Claude의 3단계 담당 목록에 없으며 실제 수정 범위를 Codex 상태에 기록했다. 헤더나 공용 경로 유틸리티까지 수정이 필요하면 기존 분담을 먼저 다시 조율한다. 담당이 기록되지 않은 파일도 착수 전에 변경 상태를 확인한다.

## Codex가 보존할 진행 변경

작성 시점에 다음 파일과 폴더에 커밋되지 않은 변경이 있었다. 변경의 작성자는 Git 상태만으로 확정할 수 없으므로, Codex는 다른 담당자의 진행 작업으로 취급하여 수정하거나 되돌리지 않는다.

- `CMakeLists.txt`
- `Source/Core/EditorLog.cpp`
- `Source/Editor/MaterialInspector.cpp`
- `Source/Graphics/Common/RenderLayers.cpp`, `RenderLayers.h`
- `Source/Graphics/DX11/CustomShaders.cpp`, `CustomShaders.h`
- `Source/Graphics/DX11/ShaderCache.cpp`, `ShaderCache.h`
- `Source/Platform/App.cpp`
- `Source/Scene/MeshBatcher.cpp`
- `Source/ShaderGraph/`
- `Tools/NovaCli/main.cpp`
- `Tools/tests/run_tests.ps1`
- `Intermediate/`, `Showcase/`, `docs/examples/shadergraph_lava.txt`

명세 확인 중 `README.md`, `AGENT_HANDOFF.md`, `docs/NOVA_CLI.md`에도 변경이 생겼고 `docs/SHADER_GRAPH.md`가 추가됐다. 이 문서들도 다른 담당자의 진행 작업으로 보존한다.

`Tools/tests/common.ps1`도 공용 검사 기반이므로 병행 작업 중에는 조율 없이 수정하지 않는다. 이 목록은 고정된 잠금이 아니며, 최신 상태 문서와 실제 작업 폴더를 함께 확인한다.

## 씬 복원 작업의 완료 조건

수정 전 `SceneManager::HandleStop`은 Play 이전 경로가 비어 있으면 실행 중인 씬 경로로 대체했다. 저장하지 않은 씬에서 다른 씬으로 이동한 뒤 Stop하면 원래 오브젝트가 다른 씬의 경로를 갖게 됐다. Codex는 Play 이전의 빈 경로도 유효한 원래 상태로 보존하도록 수정했고 아래 조건을 별도 엔진에서 검증했다.

완료 조건은 다음과 같다.

1. 저장하지 않은 씬에서 Play하고 다른 저장된 씬으로 이동한 뒤 Stop하면, 원래 오브젝트와 빈 씬 경로가 복원된다.
2. 그 상태에서 저장하면 새 저장 위치를 선택하며, 이동했던 씬 파일은 바뀌지 않는다.
3. 저장된 씬에서 시작한 경우에는 Stop 뒤 원래 씬 경로와 내용이 복원된다.
4. 씬 전환 없이 Play/Stop하는 기존 동작도 유지된다.

이미 JSON 스냅샷의 유무로 복원 여부를 판단하므로, 먼저 공개 API나 클래스 메모리 배치를 바꾸지 않는 최소 수정으로 해결할 수 있는지 검토한다. 실제 변경 결과를 보기 전에는 해결됐다고 표시하지 않는다.

## 씬 경로 작업의 완료 조건

수정 전 씬 저장은 절대 경로를 허용했지만 `Scene::Load`는 프로젝트 기준 경로 결합을 항상 사용했다. Codex는 `Scene.cpp` 안에서 읽기와 쓰기의 경로 해석을 통일했고 `SceneManager.cpp`의 시작 씬 확인에도 절대 경로를 반영했다. 한글과 공백이 있는 경로를 포함해 아래 파일 동작을 검증했다. 저장 대화상자의 취소 클릭은 자동화하지 않았다.

1. 프로젝트 안의 상대 경로 씬을 정상적으로 저장하고 다시 연다.
2. 프로젝트 밖의 절대 경로 씬을 저장하고 다시 연다.
3. 한글과 공백이 포함된 경로에서도 같은 동작을 확인한다.
4. 존재하지 않는 파일의 실패 동작과 저장 취소 동작을 확인한다.
5. `PathManager` 전체의 경로 정책을 변경할 필요가 생기면 별도 작업으로 제안한다.

## Claude에게 전달할 검토 항목

다음은 최초 검토에서 전달한 항목이다. Claude는 2단계에서 처리와 검증 결과를 자신의 상태 문서에 기록했다. 최신 구현과 남은 범위는 `docs/ai-status/CLAUDE.md`를 확인한다.

- 그래프 재질은 `Shader Graphs/Tint`처럼 확장자 없는 셰이더 이름을 저장한다. 현재 빌드의 에셋 수집은 파일 경로를 추적하므로 그래프 파일과 그 텍스처가 빠질 수 있다. 그래프가 포함된 씬을 별도 게임으로 빌드하는 검증이 필요하다.
- 그래프 스킨 메시의 그리기 경로가 `SetObjectLayer(..., ~0u)`로 실제 레이어를 덮어쓴다. Lit 그래프와 조명의 culling mask 조합을 확인한다.
- 그래프 셰이더 이름과 생성 파일이 파일의 stem으로 만들어져, 서로 다른 폴더의 같은 파일명이 충돌할 수 있다. 경로나 안정적인 식별자를 사용해 구분하는 방안을 검토한다.
- `TestResults/sg2/results.json`은 14개 중 10개 통과, 4개 실패다. Checkerboard, Time, 전체 노드 컴파일, 편집 창 캡처의 실패를 엔진과 검사 도구 문제로 나누어 조사할 필요가 있다. 현재 코드로 재실행한 결과는 아니다.

## 다음 분담 — 렌더링 (Claude) · 게임 기능 (Codex)

작성: Claude, 2026년 10월 3일 03시 56분 KST. 사용자가 Claude 에게 "GPT 한테도 기능적인 부분 맡겨줘" 라고 지시했다. 렌더링은 Claude, 게임 기능은 Codex 가 맡아 **동시에 같은 파일을 고치지 않게** 나눈다. 이 분담은 사용자의 지시를 Claude 가 정리한 것이다 — Codex 는 착수 전에 사용자에게 확인받고 자기 상태 문서에 담당 파일을 적는다. 진행 중인 "씬 저장 실패 보호" 는 Codex 가 판단해 먼저 끝내도 된다.

| 담당 | 작업 | 주 파일 | 상태 |
|---|---|---|---|
| **Codex** | **Joint 2D** (Unity 의 2D Joint 6 종) | `Source/Physics2D/` (새 `Physics2DJoints.*` 권장), `ScriptCore/Engine/Physics2D.cs`, `Source/Scripting/ScriptBindings.cpp` · `ScriptCore/Interop/NativeApi.cs` (C# 네이티브 표 — **이번 회차는 Codex 만** 고친다), 새 검사 `Tools/tests/joints2d.ps1` | 사용자 확인 후 착수 |
| Codex (다음 후보) | Tilemap (Tile Palette 창, Tilemap Renderer, Tilemap Collider 2D) — 아래 "Tilemap 명세" | `Source/` 아래 새 폴더 (예: `Source/Tilemap/`), 위 C# 표 | Joint 2D 다음 — 사용자 확인 후 착수 |
| **Claude** | **Decal** (Unity URP 의 Decal Projector — 깊이 버퍼에서 표면 위치를 되살려 그림 투영, 일반 재질 + Shader Graph 데칼) | 새 `Source/Scene/DecalProjector.*`, 새 셰이더 (`Shaders/52. Decal.fx`), `Source/Editor/EditorApp.cpp` (그리는 순서), `Source/ShaderGraph/` | 완료 · push (`ce15018`). Joint 2D 와 합친 독립 빌드 검사: joints2d 42/42, physics2d · decal 13/13 (10월 3일 05시 05분) |
| **Claude** | **Reflection Probe** (Unity 의 Reflection Probe — Baked · Realtime · Custom, Box Projection, 블렌드) — 아래 "Reflection Probe (Claude)" | 새 `Source/Scene/ReflectionProbe.*`, 새 `Source/Graphics/DX11/ReflectionProbes.*`, 새 셰이더, `Shaders/32. InstancedBasic.fx` (반사 함수), `Source/Editor/EditorApp.cpp` (Game 뷰 그리기를 함수로 나눔) | 완료 · push (`10bbdac`) |
| **Claude** | **Adaptive Probe Volume — 실시간** (Unity 6 APV 자리 · 이름, 굽기 없음. 레거시 Light Probe Group · Proxy Volume 제거) | 새 `Source/Scene/AdaptiveProbeVolume.*`, 새 `Source/Graphics/DX11/ProbeVolumes.*`, 새 `Shaders/55. ProbeVolume.fx`, `Shaders/32. InstancedBasic.fx` (확산 환경광), `Source/Editor/EditorApp.*`, `Source/Scene/SceneCulling.*` (바뀐 렌더러 상자), `Source/Scene/MeshRenderer.cpp` · `SkinnedMeshRenderer.cpp` (레거시 드롭다운) | 완료 · push (`efbab1e`) |
| **Claude** | **후처리 Depth of Field (Gaussian · Bokeh) + Motion Blur** (Volume, Unity URP 이름) | `Source/Graphics/Common/VolumeProfile.*`, `Source/Editor/VolumeEditor.cpp`, `Source/Graphics/DX11/PostProcessPass.*`, `Shaders/41. PostProcess.fx`, `Source/Editor/EditorApp.cpp` | 구현 · 독립 빌드 검사 완료 (10월 4일): depthoffield 5/5, render · gfx 10/10 |

### Reflection Probe (Claude)

작성: Claude, 2026년 10월 4일. 사용자 지시 "푸시하고 리플렉션 프로브 진행해줘".

- 고치는 파일: 새 `Source/Scene/ReflectionProbe.*` (컴포넌트), 새 `Source/Graphics/DX11/ReflectionProbes.*` (캡처 · 필터 · 큐브 배열 · 바인딩 · CLI `probe`), 새 `Shaders/54. ReflectionProbe.fx`, `Shaders/32. InstancedBasic.fx` (반사를 프로브 블렌드로 — 확산 환경광은 하늘 그대로), `Source/Editor/EditorApp.cpp` (Game 뷰 그리기를 `RenderGameView` 로 나눠 프로브 캡처가 같은 길을 씀), `Source/Graphics/OpenGL/GfxGL.cpp` (`CaptureTexture` 가 큐브 · 밉도), 공용 `AddComponentMenu.cpp` · `GameObjectMenu.cpp` 한 줄씩, `Tools/tests/run_tests.ps1` 의 새 `Suite-ReflectionProbe`
- 안 고치는 파일: `Source/Physics2D/`, 씬 파일, `CliCommands.cpp` (CLI 는 Shader Graph 처럼 내 파일에서 등록), C# 표 (C# `ReflectionProbe.RenderProbe()` 는 다음 회차 — Codex 의 `J2_*` 뒤에)
- 굽기 결과는 Unity 처럼 씬 옆 폴더 (`Assets/.../<씬 이름>/ReflectionProbe-<n>.dds`), 컴포넌트 JSON 의 `bakedTexture` 에 경로

### Tilemap 명세 (Codex)

작성: Claude, 2026년 10월 4일 (Joint 2D 다음 후보 — 사용자 확인 후 착수). Unity 의 2D Tilemap 과 같은 이름 · 동작.

- **컴포넌트**: `Grid` (Cell Size, Cell Gap, Cell Layout = Rectangle 부터, Cell Swizzle XYZ), `Tilemap` (Grid 의 자식, Animation Frame Rate, Color, Tile Anchor (0.5, 0.5, 0), Orientation XY), `TilemapRenderer` (Sort Order, Mode = Chunk, Sorting Layer · Order in Layer, Material), `TilemapCollider2D` (Used By Composite 는 나중, Is Trigger, Offset — 칸마다 사각형 또는 Tile 의 Collider Type: None · Sprite · Grid)
- **Tile 에셋** (`.asset` JSON — Unity 의 Tile ScriptableObject): Sprite (`스프라이트시트.png#이름` 형식 — 엔진의 sprite-slice 결과), Color, Collider Type. Project 창 Create > 2D > Tiles > Tile, 스프라이트를 Tile Palette 로 끌면 Tile 자동 생성
- **Tile Palette 창** (Window > 2D > Tile Palette): 팔레트 (`.palette` — 칸 → Tile), 도구 Select · Move · **Paint (B)** · **Box Fill (U)** · Pick (I) · **Erase (D)** · **Flood Fill (G)**, Active Tilemap 고르기, Scene 뷰에서 칸 미리보기 · 칠하기 (Undo 한 번에 한 획)
- **저장**: 씬 JSON 의 Tilemap 에 칸 목록 (`[x, y, tile 번호]` + tile 경로 표 — 큰 맵도 작게), Undo, Play/Stop 복원, 프리팹, 게임 빌드 (Tile · 그림이 따라감 — `BuildPipeline` 은 JSON 의 문자열 경로를 따라간다)
- **그리기**: 기존 2D 스프라이트 경로 (`SpriteBatch`, Sorting Layer) 에 칸을 덩어리 (Chunk) 로 — 보이는 칸만. `SpriteBatch` 를 고쳐야 하면 공용 파일 규칙
- **C#** (`ScriptCore/Engine/`): `Tilemap.SetTile(Vector3Int, TileBase)` · `GetTile` · `HasTile` · `ClearAllTiles` · `GetCellCenterWorld` · `WorldToCell` · `CellToWorld` · `origin` · `size` · `cellBounds`, `Grid.WorldToCell` · `CellToWorld`, `Tile` (sprite, color, colliderType). 네이티브 함수는 표 맨 끝 (`J2_*` 다음)
- **CLI** (예: `nova tilemap paint --target Ground --cells 0,0:9,0 --tile Assets/Tiles/grass.asset`, `tilemap info`) — 검사와 AI 가 맵을 만들 수 있게
- **완료 조건** (새 검사, 별도 테스트 프로젝트): 칠한 칸이 Scene · Game 뷰에 보임 (그림 비교), Box Fill · Erase · Flood Fill, Tilemap Collider 2D 위에 Rigidbody2D 상자가 섬 · 지운 칸으로 떨어짐, 저장 → 다시 열기 · Undo · Play/Stop, C# SetTile 로 바꾼 칸이 보이고 충돌함, 빌드한 게임에서 보임
- Claude 는 이 동안 `Source/Tilemap/` · 2D 물리 · C# 표를 고치지 않는다

### Joint 2D 명세 (Codex)

엔진의 2D 물리는 Box2D v3.1.1 (`ThirdParty/box2d`) 이고 Unity 의 2D 물리도 Box2D 라 동작을 Unity 와 같게 맞춘다. 배경: `AGENT_HANDOFF.md` 의 "2D 물리" 항목, `Source/Physics2D/Physics2DManager.cpp` (매 프레임 `Sync` 가 Rigidbody2D · Collider2D 를 모아 몸체를 만들고 지운다).

- **컴포넌트 (Unity 이름 · 필드 그대로)**
  - 공통 `Joint2D`: Connected Rigid Body (없으면 월드에 고정 — Unity 와 같다), Enable Collision, Break Force · Break Torque (기본 Infinity), Anchor · Connected Anchor (로컬), Auto Configure Connected Anchor
  - `HingeJoint2D` (Box2D `b2RevoluteJoint`): Use Motor + Motor (Motor Speed 도/초, Maximum Motor Force), Use Limits + Angle Limits (Lower · Upper 도)
  - `SpringJoint2D` (`b2DistanceJoint` + spring): Auto Configure Distance, Distance, Damping Ratio, Frequency
  - `DistanceJoint2D` (`b2DistanceJoint`): Auto Configure Distance, Distance, Max Distance Only (가까워지는 것은 허용)
  - `WheelJoint2D` (`b2WheelJoint`): Suspension (Damping Ratio, Frequency, Angle), Use Motor + Motor
  - `FixedJoint2D` (`b2WeldJoint`): Damping Ratio, Frequency
  - `SliderJoint2D` (`b2PrismaticJoint`): Auto Configure Angle, Angle, Use Motor + Motor, Use Limits + Translation Limits
- 같은 GameObject 에 `Rigidbody2D` 가 필요하다 (Unity 의 RequireComponent — 붙일 때 없으면 함께 붙인다). 몸체를 다시 만들면 (Box2D 는 몸체를 지울 때 그 Joint 도 지운다) Joint 도 다시 만든다. 설정을 바꾸면 (Inspector · C#) 바로 반영
- 끊어짐: 반작용 힘 / 토크가 Break Force / Torque 를 넘으면 Joint 를 지우고 `OnJointBreak2D(Joint2D)` 를 한 번 (C# 메시지 — `Bridge.cs` 의 InvokeCollision 방식 참고), 컴포넌트는 Unity 처럼 지워진다
- **C#** (`ScriptCore/Engine/Physics2D.cs`): `Joint2D` (connectedBody, anchor, connectedAnchor, autoConfigureConnectedAnchor, enableCollision, breakForce, breakTorque, reactionForce, reactionTorque), `AnchoredJoint2D`, `HingeJoint2D` (useMotor, motor: `JointMotor2D`, useLimits, limits: `JointAngleLimits2D`, jointAngle, jointSpeed), `SpringJoint2D`, `DistanceJoint2D`, `WheelJoint2D` (suspension: `JointSuspension2D`, useMotor, motor, jointSpeed), `FixedJoint2D`, `SliderJoint2D` (jointTranslation, limits: `JointTranslationLimits2D`). 네이티브 함수는 **표 맨 끝에 붙인다** (`ScriptBindings.cpp` 의 표와 `NativeApi.cs` 의 순서가 같아야 한다 — 가운데에 넣으면 이후 함수가 모두 어긋난다)
- **에디터**: Add Component > Physics 2D (`Source/Editor/AddComponentMenu.cpp` 의 `kKnown` 표 — 아래 공용 파일 규칙), Inspector 필드 (Unity 순서), Scene 뷰에서 고른 Joint 의 Anchor 점 · 연결선 (Collider 2D 윤곽 그리는 곳 참고)
- 저장 (`toJson` · `fromJson`), Undo, Play/Stop 복원, 프리팹, 게임 빌드
- **완료 조건** (새 검사 `Tools/tests/joints2d.ps1`, 별도 테스트 프로젝트 · 결과 폴더):
  1. Hinge: 월드에 고정한 막대가 중력으로 흔들려 각도가 바뀌고 Anchor 거리는 그대로
  2. Hinge Limits ±30° 를 넘지 않음, Motor 로 정한 속도로 돎
  3. Spring: 늘어났다 돌아오며 진동 (Frequency), Damping Ratio 1 이면 진동 없이 멈춤
  4. Distance: 두 몸체 거리 유지, Max Distance Only 면 가까워질 수 있음
  5. Wheel: 바퀴 두 개 차가 Motor 로 바닥 위를 달림
  6. Fixed: 두 몸체가 붙어 함께 떨어짐. Slider: 정한 축으로만 움직이고 Limits 에서 멈춤
  7. Break Force 를 넘으면 끊기고 `OnJointBreak2D` 가 한 번
  8. 저장 → 다시 열기 · Undo · Play/Stop 뒤 설정 그대로
  9. C# 으로 motor · limits 를 바꾸면 반영
  10. 기존 2D 물리 검사 (`run_tests.ps1 -Only physics2d`) 가 그대로 통과

### 공용 파일 편집 규칙 (둘 다 고칠 수 있는 파일)

같은 작업 폴더를 동시에 쓰므로 둘 다 고칠 수 있는 파일은 짧게, 줄 단위로 고친다.

- 대상: `Source/Editor/AddComponentMenu.cpp`, `Source/Editor/CliCommands.cpp`, `Source/Platform/App.cpp`, `Tools/NovaCli/main.cpp`, `README.md`, `AGENT_HANDOFF.md`, `docs/NOVA_CLI.md`, `Showcase/목록.md`, `Tools/tests/run_tests.ps1` (각자 자기 묶음 함수만 — Codex 는 지금처럼 별도 검사 스크립트를 권장), `CMakeLists.txt`
- **고치기 직전에 파일을 다시 읽는다.** 예전에 읽은 내용을 통째로 다시 쓰지 않는다 (상대의 변경이 사라진다). 바꿀 줄만 바꾸고 바로 저장한다
- 오래 걸리는 편집이면 자기 상태 문서의 "편집 중인 공용 파일" 에 적고 끝나면 지운다. 상대 상태에 그 파일이 "편집 중" 이면 기다린다
- 커밋은 자기 변경만: 파일을 골라 `git add`, 한 파일에 상대 변경이 섞여 있으면 `git add -p` 로 자기 줄만
- 이번 회차에 Claude 는 C# 네이티브 표 (`ScriptBindings.cpp` · `NativeApi.cs`) 를 고치지 않는다 (Decal 의 C# 는 Codex 의 Joint 2D 가 끝난 뒤). Codex 는 렌더링 파일 (`EditorApp.cpp` 의 그리기 순서, `Source/Graphics/`, `Shaders/`, `Source/ShaderGraph/`, `Source/Scene/MeshBatcher.cpp`) 을 고치지 않는다

### 빌드 · 검사

- Codex 는 지금처럼 자기 독립 엔진 복사본과 전용 테스트 프로젝트를 쓴다. Claude 는 공용 `build/` · `Binaries/` · `E:\NovaTest\ScriptTest`
- 서로의 변경이 다 들어간 통합 검사는 기능이 끝날 때 상태 문서로 순서를 정해 한 번 돌린다

## 상태 공유 방법

공동 명세는 담당 경계와 완료 조건을 담는다. 수시 진행 상황은 각자의 상태 파일에 기록하여 같은 문서를 동시에 고치는 일을 줄인다.

- Codex는 `docs/ai-status/CODEX.md`만 갱신한다.
- Claude는 `docs/ai-status/CLAUDE.md`를 직접 만들고 갱신한다. 파일이 없으면 확인과 합의를 했다고 간주하지 않는다.
- 상태에는 갱신 시각, 단계, 담당 파일, 현재 결과, 검증 여부, 다음 작업, 다른 담당자에게 필요한 사항을 적는다.
- 소스 수정 전에 상대 상태를 읽고 자신의 담당 파일을 기록한다. 같은 파일이 필요하면 담당자가 인계할 때까지 읽기와 독립 작업을 계속한다.
- 공용 명세의 담당 변경은 한 번에 한 담당자가 반영한다. 상대의 상태 파일을 대신 고치거나 상대 작업을 완료로 표시하지 않는다.

이 방식은 문서를 통한 상태 공유이며 자동 메시지 전송이나 강제 잠금 기능은 아니다. 이미 실행 중인 Claude 세션에는 사용자가 새 명세 경로를 한 번 알려주어야 한다.

## 공용 빌드와 검증

소스 파일이 달라도 빌드 출력과 실행 중인 에디터는 충돌할 수 있다. `build/`, `Binaries/`, `Intermediate/`를 사용하는 빌드와 에디터 검증은 한 번에 한 담당자만 수행한다. 현재 사용자를 추정하지 말고 담당자 상태와 사용자 지시로 사용 순서를 정한다. Codex는 현재 이를 실행하지 않는다.

검증은 별도 테스트 프로젝트와 담당자별 결과 폴더를 사용한다. 사용자 프로젝트와 열려 있는 사용자 에디터의 씬, 레이아웃, 실행 상태를 변경하지 않는다. 자신이 시작한 테스트 프로세스만 종료한다. 기존 테스트 결과는 과거 기록으로 표시하고 새 코드의 통과 결과로 재사용하지 않는다.

다른 담당자의 작업이 섞인 전체 폴더를 일괄 커밋하거나 되돌리지 않는다. 커밋이 허용된 경우에도 자신의 파일과 변경만 포함한다. push, 병합, 다른 세션에 메시지 전송은 사용자의 해당 지시를 따른다.

## 기존 명세와의 관계

`AGENT_HANDOFF.md`와 `PROJECT_HANDOVER.md`는 구현 배경과 과거 작업을 확인할 때 참고한다. 전자는 현재 경로와 OpenGL 구현 단계에 오래된 설명이 남아 있다. 현재 소스와 최신 담당자 상태를 우선하며, 이번에는 기존 문서를 재작성하지 않았다.

## 후속 작업 제안

사용자가 후속 개발 진행을 지시하여 Codex가 불러오기 실패 시 현재 씬 보존을 구현했다. 새 씬의 JSON과 오브젝트 복원이 끝난 뒤 기존 씬을 교체하며, 같은 씬 다시 열기와 자동 복구에도 적용했다. 실패하면 내용·경로·수정 상태·선택·Undo를 보존하고 부분 생성된 오브젝트를 정리한다. 성공한 교체에서는 이전 씬의 미저장 지형 편집을 기존 동작처럼 버린다. 최신 검증과 수정 파일은 `docs/ai-status/CODEX.md`를 우선한다.

다음 후보는 저장 실패 시 기존 씬 파일 보존이다. 임시 파일에 쓰고 성공한 뒤 교체하는 처리는 아직 착수하지 않았다.

Claude의 Shader Graph 3단계가 끝나면 최신 엔진으로 씬과 그래프의 통합 검증을 진행한다. Codex의 실행 검사는 확정 커밋 `49b2632`에 자신의 담당 소스만 반영한 독립 엔진의 결과이며 최신 Shader Graph 통합 결과로 대신 표시하지 않는다. 이전 씬 복원·경로 수정은 `7751c5e`에 커밋했다.
