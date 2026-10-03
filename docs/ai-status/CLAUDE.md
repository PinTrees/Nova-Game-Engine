# NOVA Claude 작업 상태

- 갱신 시각: 2026년 10월 4일 02시 KST
- 단계: **Reflection Probe 완료 · 커밋** (push 는 사용자 확인 뒤) — 사용자 지시 "푸시하고 리플렉션 프로브 진행해줘"
- 기준 커밋: `89086c1` (push 함 — Decal `ce15018` 포함)
- Codex 분담: Joint 2D 완료 (작업 폴더에 미커밋), 다음 후보 Tilemap — 명세는 `docs/AI_COLLABORATION.md` 의 "Tilemap 명세 (Codex)" (사용자 확인 후 착수)

## Reflection Probe 담당 파일 (커밋에 넣은 것)

| 파일 | 왜 |
|---|---|
| 새 `Source/Scene/ReflectionProbe.*` | Unity 의 Reflection Probe 컴포넌트 (Type Baked · Custom · Realtime, Importance, Intensity, Box Projection, Blend Distance, Box Size · Offset, Resolution, HDR, Shadow Distance, Clear Flags, Background, Culling Mask, Clipping Planes, Refresh Mode, Time Slicing, Bake 버튼, 기즈모) |
| 새 `Source/Graphics/DX11/ReflectionProbes.*` | 찍기 (6 면) · GGX 필터 · 큐브 배열 (최대 8) · 뷰마다 고르기 · 셰이더 값 · 굽기 (DDS) · CLI `probe` |
| 새 `Shaders/54. ReflectionProbe.fx` | 면 · 밉마다 GGX 필터 |
| `Shaders/32. InstancedBasic.fx` | 반사 = `ProbeReflection` (프로브 블렌드 + Box Projection + 남는 몫 하늘). 확산 환경광은 하늘 그대로 |
| `Source/Editor/EditorApp.*` | Game 뷰 본문을 `RenderGameView(target, GameViewDesc)` 로 (프로브 찍기가 같은 길), 두 뷰에 `Select` · `Bind`, `RenderApplication` 에서 `Update` |
| `Source/Graphics/OpenGL/GfxGL.cpp` | `CaptureTexture` 가 큐브 · 배열 · 밉도 (OpenGL 굽기) |
| `Tools/NovaCli/main.cpp` | `probe` 를 op 형 명령 줄에 한 단어 + 도움말 한 줄 |
| **공용** `Source/Editor/AddComponentMenu.cpp` | Rendering > Reflection Probe 한 줄만 stage (Codex 의 Joint 6 줄은 작업 폴더에 그대로) |
| **공용** `Source/Editor/GameObjectMenu.cpp` | Light > Reflection Probe 켬, Rendering 하위 메뉴 (Reflection Probe · URP Decal Projector) |
| **공용** `Tools/tests/run_tests.ps1` | 새 `Suite-ReflectionProbe` + 목록 · switch · 도움말에 한 단어 |
| 문서 | 새 `docs/REFLECTION_PROBE.md`, README · AGENT_HANDOFF · NOVA_CLI 한 줄씩, 공동 명세의 Claude 줄 · 절 |

C# 네이티브 표 · `Source/Physics2D/` · 씬 파일 · `CliCommands.cpp` · Codex 의 Joint 2D 파일은 고치지 않았다.

## 검증 (Debug, 독립 빌드 `E:\NovaTest\ClaudeDecalEngine` = `89086c1` + 작업 폴더의 Codex Joint 2D 변경 + Claude Reflection Probe)

- `reflectionprobe` **9/9** (`TestResults/probe2`): 프로브 없으면 하늘 · 굽기 (DDS + 빨간 벽 반사) · 다시 열기 · 상자 밖은 하늘 · Intensity 0 · Custom · Realtime On Awake 는 `probe render` 까지 유지 · Every Frame (Individual Faces) · Box Projection (347 → 801)
- 회귀 `render · shadergraph · decal · packages` **47/47** (`TestResults/probe_reg1`) — 32 셰이더 변경 · Game 뷰 함수 분리 뒤
- OpenGL 짧은 확인: DX 와 같은 그림, OpenGL 굽기 (큐브 읽기) 성공
- 커밋한 파일은 독립 빌드와 내용이 같다 (공용 파일은 내 줄만 stage)

## 공용 빌드 · 테스트 에디터 사용

- **공용 `build/` · `Binaries/` 는 사용 안 함.** Claude 의 테스트 에디터는 모두 닫았다. 독립 빌드 `E:\NovaTest\ClaudeDecalEngine` 에서 빌드 · 검사, 테스트 프로젝트 `E:\NovaTest\ScriptTest` (`$env:NOVA_ENGINE` 으로 그 엔진)

## Codex 에게

- `Shaders/32. InstancedBasic.fx` 의 ShadeLit 반사가 `ProbeReflection` 을 거친다 (프로브가 없으면 예전과 같은 하늘). 셰이더 캐시가 한 번 다시 컴파일된다
- `EditorApp::OnSceneRender` 의 본문이 `RenderGameView(target, GameViewDesc)` 로 옮겨졌다 (동작은 같음). 씬 쪽 코드는 건드리지 않았다
- `GameObjectMenu.cpp` 의 Rendering 하위 메뉴를 켰다 (Reflection Probe · URP Decal Projector). Tilemap 메뉴는 2D Object 쪽이라 겹치지 않는다
- 공동 명세에 "Tilemap 명세 (Codex)" 를 적었다 — 사용자 확인 후 착수

## 편집 중인 공용 파일

- 없음. `docs/AI_COLLABORATION.md` 는 Codex 의 미커밋 변경 (Joint 줄 · 인계 절) 과 같은 파일이라 커밋에는 내 줄 (Decal 줄 · Reflection Probe 줄 · Tilemap 줄 · 새 절 둘) 만 넣었다

## 지난 작업

- Decal Projector (`ce15018`, push): `docs/DECAL.md`. Joint 2D 와 합친 독립 빌드 검사 joints2d 42/42, physics2d · decal 13/13
