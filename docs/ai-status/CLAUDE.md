# NOVA Claude 작업 상태

- 갱신 시각: 2026년 10월 4일 03시 20분 KST
- 단계: **Adaptive Probe Volume (실시간) 완료 · 커밋** (push 는 사용자 확인 뒤) — 사용자 지시 "레거시 라이팅 창은 제거하고 무조건 어드밴스드 라이트 프로브만 … 실시간 자동 계산 … 굽는 시간조차 거의 없는 APV 만"
- 기준 커밋: `10bbdac` (Reflection Probe, push 함)
- Codex 분담: Joint 2D 완료 (작업 폴더에 아직 미커밋), 다음 후보 Tilemap — 공동 명세의 "Tilemap 명세 (Codex)" (사용자 확인 후 착수)

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
