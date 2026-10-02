# NOVA Claude 작업 상태

- 갱신 시각: 2026년 10월 3일 03시 52분 KST
- 단계: **Shader Graph 3단계 완료** (Vertex 단계 · Sub Graph · Custom Function) — 검증 끝, 커밋 `125cb4f` — 사용자 지시로 origin/main 에 push 함 (**Codex 의 `7751c5e` · `6f2f642` 도 같이 push 됨** — 사용자가 함께 올리라고 함)
- 기준: Codex 의 로컬 커밋 `7751c5e` · `6f2f642` 위 (그 커밋들은 건드리지 않았다)
- 사용자 지시: "Shader Graph 3단계 진행" (앞서: 2단계 미리보기 · 투명 · 컷아웃 — 끝, 공유 명세를 필요할 때마다 같이 갱신)

## 3단계 결과

- Vertex 단계 (Vertex Position · Normal · Tangent, Position / Normal 노드 Space Object), 깊이 프리패스 · 그림자도 같은 위치
- Sub Graph (`.shadersubgraph`, Blackboard = 입력, Outputs = 출력, 저장하면 쓰는 그래프를 다시 만듦)
- Custom Function (String 본문 / File `.hlsl` 의 `<name>_float`)
- Main Preview 를 실제 메시로 (정점 이동이 보임)
- 고친 것: 정점만 옮긴 그래프가 엔진 프리패스로 그려져 상자가 배경색 (`CustomDepth` 조건), 패스마다 시각이 달라 정점이 어긋나 검게 빔 (프레임마다 한 시각), 따옴표가 깨진 `--options` 를 조용히 무시 (이제 오류)

## 검증 (2026년 10월 3일 03시 52분 KST, Debug 빌드)

- `shadergraph` **24/24** (`TestResults/sg9`) — 2단계까지 19 개 + 정점 올리기 · 시간 물결 (검지 않음) · Custom Function String/File · Sub Graph (입력 → 출력, 고치면 다시 만듦) · 잘못된 options
- 회귀 `render · packages · animation · layers` **40/40** (`TestResults/sg_reg3`)
- 이 빌드에는 Codex 의 `6f2f642` 까지 들어 있다 (마지막 증분 빌드에서 다시 컴파일한 파일 없음)

## 공용 빌드 · 테스트 에디터 사용

- **지금: 사용 안 함** (2026년 10월 3일 03시 52분 KST 반환). Claude 가 띄운 테스트 에디터는 모두 닫았다.

## Codex 에게

- 3단계는 `Source/ShaderGraph/`, `Source/Graphics/DX11/CustomShaders.h` (`ClipsAlpha` → `CustomDepth` 이름만), `Source/Scene/MeshBatcher.cpp` · `SkinnedMeshRenderer.cpp` (그 이름), `Source/Build/BuildPipeline.cpp` (`.shadersubgraph` 한 단어), `Tools/tests/run_tests.ps1` (`Suite-ShaderGraph` 안만), 문서만 고쳤다. 씬 파일 · `CliCommands.cpp` · `GameObject.cpp` 는 건드리지 않았다
- 내 커밋에 이 상태 파일 (`docs/ai-status/CLAUDE.md`) 을 처음으로 넣는다 (Codex 가 협업 문서를 커밋했으므로 같이 맞춤)

## 3단계 담당 파일

| 파일 | 왜 |
|---|---|
| `Source/ShaderGraph/` (전부) | Vertex 단계 생성기, Sub Graph (`.shadersubgraph`), Custom Function 노드, 창 · CLI, Main Preview 를 실제 메시로 (정점 이동이 보이게) |
| `Source/Graphics/DX11/CustomShaders.*` | `ClipsAlpha` 를 "깊이 · 그림자 패스도 이 셰이더가" 로 넓힘 (정점을 옮기면 프리패스도 옮겨야 EQUAL 깊이가 맞다) |
| `Source/Scene/MeshBatcher.cpp`, `Source/Scene/SkinnedMeshRenderer.*` | 위 이름 변경 반영 (동작은 2단계와 같은 길) |
| `Source/Build/BuildPipeline.cpp` | 그래프가 쓰는 Sub Graph · Custom Function `.hlsl` 도 게임 빌드에 |
| `Tools/tests/run_tests.ps1` | `Suite-ShaderGraph` 함수 안만 |
| `docs/SHADER_GRAPH.md`, `README.md`, `AGENT_HANDOFF.md`, `docs/NOVA_CLI.md` | 문서 |

Codex 의 씬 파일 (`Source/Scene/Scene.cpp`, `Source/Scene/SceneManager.cpp`, `Tools/tests/scene_lifecycle.ps1`) 은 건드리지 않는다.

### (3단계 동안) 공용 빌드

- 3단계 동안 사용 중이었다: Claude — `build/`, `Binaries/`, 테스트 프로젝트 `E:\NovaTest\ScriptTest` 의 테스트 에디터 (3단계 끝까지). Codex 는 별도 엔진 복사본 (`E:\NovaTest\CodexSceneEngine-*`) 을 쓴다고 적었으므로 겹치지 않는다. 끝나면 여기에 "사용 안 함" 으로 적는다.
- 작업 폴더의 Codex 씬 변경 (`Scene.cpp`, `SceneManager.cpp`) 은 내 빌드에 같이 컴파일된다 (커밋에는 넣지 않는다).

## 지난 단계 기록 (2단계, `ffbf792`)

## 공동 명세 확인

`docs/AI_COLLABORATION.md` 를 읽었고 담당 분담에 동의한다. Codex 의 씬 작업 (`Source/Scene/SceneManager.cpp`, `Source/Scene/Scene.cpp`, `Tools/tests/scene_lifecycle.ps1`) 은 건드리지 않는다. Codex 가 만든 `AGENTS.md`, `CLAUDE.md`, `docs/AI_COLLABORATION.md`, `docs/ai-status/CODEX.md` 는 커밋하거나 고치지 않는다 (공동 명세의 Claude 담당 표 줄만 이 상태에 맞춰 고친다).

명세가 "보존할 진행 변경" 으로 적은 파일들은 **Shader Graph 1단계** 변경이었고 커밋 `49b2632` 로 들어가 push 됐다 (작업 폴더에 남은 미커밋 변경 없음).

## 담당 파일 (2단계에서 고칠 것)

| 파일 | 왜 |
|---|---|
| `Source/ShaderGraph/` (전부) | 그래프 · 생성기 · 런타임 · 창 · 미리보기 (새 파일 `ShaderGraphPreview.*`) |
| `Source/Graphics/DX11/CustomShaders.*` | Provider 의 "만드는 중" 상태, 깊이 · 그림자 패스 위임, 투명 그리기 |
| `Source/Graphics/DX11/ShaderCache.*` | 백그라운드 컴파일 (캐시만 채움) |
| `Source/Graphics/DX11/RenderStates.*` | 깊이 읽기만 하는 상태 (투명) |
| `Source/Scene/MeshBatcher.*` | 컷아웃 재질의 프리패스 · 그림자 (재질별 묶음), 투명 묶음 따로 |
| `Source/Scene/SkinnedMeshRenderer.cpp` | 그래프 재질의 깊이 · 그림자 패스 위임 (필요하면) |
| `Source/Editor/EditorApp.cpp` | Game · Scene 뷰에 투명 패스 끼우기 (하늘 · 대기 다음, 물 앞) |
| `Shaders/26. BuildShadowMap.fx`, `Shaders/28. SsaoNormalDepth.fx` | VS_Batch 용 Alpha Clip 기법 (엔진 Lit 재질도) |
| `Source/Build/BuildPipeline.cpp` | 재질이 쓰는 `.shadergraph` 와 그 그림을 게임 빌드에 |
| `Source/Editor/MaterialInspector.cpp` | Shader Graph 재질 Surface 설정 표시 |
| `Tools/tests/run_tests.ps1` | `Suite-ShaderGraph` 함수 안만 (다른 묶음 · `common.ps1` 은 고치지 않음) |
| `docs/SHADER_GRAPH.md`, `README.md`, `AGENT_HANDOFF.md`, `docs/NOVA_CLI.md` | 문서 |

## 계획 (차례)

1. **2-A 비동기 컴파일**: 저장 → `.fx` 쓰기 → 작업 스레드에서 `ShaderCache::CompileEffect` (캐시만) → 다음 프레임 메인 스레드 `LoadEffect` (캐시 적중). 그동안 예전 셰이더 / Fallback 으로 그린다. Provider 에 "만드는 중" 상태 (실패 목록에 넣지 않음)
2. **2-B 미리보기**: 그래프 전체를 담는 작은 독립 이펙트 (엔진 `32. InstancedBasic.fx` 를 포함하지 않아 빨리 컴파일) — 노드마다 첫 출력을 색으로 (아틀라스 한 장), Main Preview = 픽셀 셰이더에서 광선으로 구 / 상자 + 고정 빛 (씬 그림자와 무관), 끌어 돌리기
3. **2-C Alpha Clip**: 그래프 설정 Alpha Clipping + Master 의 Alpha Clip Threshold. 프리패스 (깊이 · 노멀) · 그림자 · 본 패스 모두 잘라낸다. MeshBatcher 의 깊이 묶음을 컷아웃 재질만 재질별로. **엔진 버그도 같이**: 일반 Lit 재질의 Alpha Clipping 이 Mesh Renderer 프리패스 · 그림자에서 무시됨 (구멍이 배경색, 그림자는 꽉 참)
4. **2-D Transparent**: 그래프 설정 Surface Type. 새 투명 패스 (뒤에서부터, 알파 섞기, 깊이 읽기만, 프리패스 · 그림자 제외)
5. **2-E Codex 검토 항목** (아래)
6. **2-F** 검사 묶음 `shadergraph` 늘리기, 문서, 쇼케이스, 커밋

## Codex 검토 항목에 대한 답

| 항목 | 확인 | 처리 |
|---|---|---|
| 게임 빌드에 그래프 파일이 빠짐 | 맞다. 재질의 `"Shader": "Shader Graphs/X"` 는 경로가 아니라 수집이 따라가지 않는다 | 2-E: BuildPipeline 이 그 이름을 그래프 파일로 풀어 넣는다 (그래프 JSON 안의 그림 경로도) |
| 스킨 메시 그래프가 레이어를 `~0` 으로 덮어씀 | 맞다. SkinnedMeshRenderer 가 이미 모든 사용자 이펙트에 레이어 비트를 넣는데 `DrawSkinned` 가 덮어썼다 | 2-E: 덮어쓰기 제거 |
| 다른 폴더의 같은 파일 이름이 충돌 | 맞다 (Unity 도 기본 이름은 `Shader Graphs/<파일 이름>`). 생성 `.fx` 도 같은 이름 | 2-E: 생성 파일 이름에 그래프 경로 해시, 같은 셰이더 이름이 둘이면 오류로 알림 + Blackboard 의 경로 (`Shader Graphs`) 를 바꿀 수 있게 (Unity 와 같이) |
| `TestResults/sg2` 14 중 4 실패 | 원인 찾음: 엔진 멈춤 감시가 메인 스레드를 멈춘 채 DbgHelp 로 힙을 써 셰이더 컴파일 중 교착 (WER AppHang). 그 뒤 검사가 모두 실패 | `49b2632` 에서 고침 (멈춘 동안 `RtlVirtualUnwind` 만). 다시 돌린 `sg3` · `sg4` 14/14 통과, `packages` · `render` 18/18 |

### (2단계 때) 공용 빌드

- 2단계가 끝난 03시 15분에 반환했다. Claude 가 띄운 테스트 에디터 · 빌드한 게임은 모두 닫았다.
- 다음에 Claude 가 쓰기 시작하면 여기에 다시 적는다.

## 진행

- 2-A 비동기 컴파일: `ShaderGraph::CompileInBackground` (작업 스레드 = `ShaderCache::CompileEffect`), `UpdateRuntime()` (App 루프), Provider `Pending`
- 2-B 미리보기: 새 파일 `Source/ShaderGraph/ShaderGraphPreview.*`, `GeneratePreview()` (노드 아틀라스 + Main Preview 광선 구 / 상자)
- 2-C Alpha Clip: 그래프 설정 + `GraphDepth*Tech` · `GraphShadow*Tech`, MeshBatcher 깊이 묶음을 잘라내는 재질마다, **엔진 Lit Alpha Clipping 도 Mesh Renderer 프리패스 · 그림자에서 자르게 고침** (`NormalDepthAlphaClipBatchTech` 추가)
- 2-D Transparent: `MeshBatcher::Pass::Transparent`, `RenderStates::DepthReadDSS`, EditorApp 의 Game · Scene 뷰 (대기 다음, 물 전, 읽기 전용 DSV)
- 2-E: BuildPipeline (셰이더 이름 → 그래프 파일, 메인 스레드에서 목록을 만들어 작업에 넘김), 스킨 레이어 덮어쓰기 제거, 경로 해시 · 같은 이름 오류 · Blackboard 경로
- 추가로 고친 파일: `Source/Graphics/DX11/ShadowRenderer.cpp` (그림자 조각의 빛 · 바이어스를 `CustomShaders::CurrentShadow()` 에도 기록 — 3 줄)

## 검증 (10월 3일 03시 10분, Debug 빌드)

- 검사 묶음 `shadergraph` **19/19** (`TestResults/sg6`): 1단계 14 개 + Alpha Clip (구멍 너머 빨간 벽이 보임 — 프리패스도 자름, 깊이 · 그림자 기법 생성), 엔진 Lit Alpha Clip (줄무늬 그림 — 고친 프리패스), Transparent (파란 유리 너머 빨간 벽이 섞임), 같은 셰이더 이름 (저장이 이유를 알림 → 경로를 바꾸면 됨), 미리보기 셰이더
- 회귀 `render · packages · animation · layers` **40/40** (`TestResults/sg_reg2`)
- 게임 빌드: 그래프를 쓰는 씬을 빌드 → 출력에 `.shadergraph` · `.mat` · **그래프만 쓰는 그림** 이 들어감, 빌드한 게임을 15 초 실행 → 로그에 `built Shader Graphs/BuildGraph` (게임이 백그라운드에서 셰이더를 만듦). 테스트 프로젝트의 Build Settings 는 원래대로 되돌림
- 눈으로 확인: `TestResults/sg_p2` (잘린 상자 · 체커 그림자 · 투명 구, 창의 노드 미리보기 · Main Preview)

## Codex 에게

- **내 빌드 · 검사에는 Codex 의 진행 중 변경 (`Source/Scene/Scene.cpp`, `Source/Scene/SceneManager.cpp`) 이 같이 컴파일돼 있었다.** 위 검사 (render · packages · animation · layers · shadergraph — 씬 열기 · 새 씬 · 저장 · Play/Stop 을 많이 쓴다) 는 모두 통과했다. 단, 그 변경 자체를 검증한 것은 아니다
- 내 커밋에는 그 두 파일과 `Tools/tests/scene_lifecycle.ps1`, 협업 문서 (`AGENTS.md`, `CLAUDE.md`, `docs/AI_COLLABORATION.md`, `docs/ai-status/`) 를 넣지 않는다
- 공용 검사 파일 `Tools/tests/run_tests.ps1` 은 `Suite-ShaderGraph` 함수와 묶음 목록에 `shadergraph` 한 단어만 고쳤다
- Codex 쪽에서 알아 두면 좋은 엔진 변경: `MeshBatcher::Pass::Transparent` 가 새로 생겼고 EditorApp 이 Game · Scene 뷰에서 부른다 (`Scene.cpp` 를 거치지 않고 `MeshBatcher::Draw` 를 직접). 씬 쪽 코드와는 겹치지 않는다
