# NOVA Claude 작업 상태

- 갱신 시각: 2026년 10월 3일 04시 55분 KST
- 단계: **Decal 완료 · 커밋** (push 는 사용자 확인 뒤) → 다음: Codex Joint 2D 와 합친 통합 검사
- 기준 커밋: `673c3b3`
- Codex 분담: Joint 2D — 04시 39분 완료 보고 확인 (Joint 42/42, 물리 8/8, 씬 44/44, 독립 빌드)

## Decal 결과

- Unity URP 의 Decal Projector (화면 공간 방식): Add Component > Rendering > Decal Projector, 재질을 상자로 표면에 투영 (깊이 프리패스로 위치를 되살려 상자 안만), Draw Distance · Start Fade · Angle Fade · UV · Pivot · 크기 · Opacity, 엔진 Lit/Unlit 재질 또는 Shader Graph (Material = Decal), Game · Scene 뷰 · 게임 빌드, DX11 = OpenGL
- 자세히: `docs/DECAL.md`, 기록: `AGENT_HANDOFF.md` 의 Decal Projector 항목

## 고친 파일 (커밋에 넣은 것)

| 파일 | 왜 |
|---|---|
| 새 `Source/Scene/DecalProjector.*`, `Source/Graphics/DX11/DecalRenderer.*`, `Shaders/52. Decal.fx`, `Shaders/53. DecalCommon.fx` | 컴포넌트 · 그리기 · 셰이더 |
| `Shaders/32. InstancedBasic.fx` | technique11 12 개를 `#ifndef NOVA_NO_ENGINE_TECHNIQUES` 로 감쌈 (데칼 셰이더는 함수만 — OpenGL 변환 3 ~ 4 초 멈춤 방지). 정의하지 않은 파일은 그대로 |
| `Source/Graphics/DX11/CustomShaders.h` | `DecalDraw`, `Shader::DrawDecal` |
| `Source/Graphics/DX11/RenderStates.*` | `DecalBS` (RGB 만 섞기) |
| `Source/Editor/EditorApp.cpp` | Game · Scene 뷰에서 불투명 다음 `DecalRenderer::Render` |
| `Source/Editor/UnityGUI.*` | 새 `MinMaxSlider` (Angle Fade) — 함수 추가만 |
| `Source/ShaderGraph/` | Material = Decal (`GenerateDecal`, 런타임 `DrawDecal`), `new --material Decal` 이 Lit 으로 바뀌던 것 |
| `Tools/NovaCli/main.cpp` | 도움말 한 줄 (`--material Lit\|Unlit\|Decal`) |
| **공용** `Source/Editor/AddComponentMenu.cpp` | `DecalProjector` 한 줄만 stage (Codex 의 Joint 6 줄은 작업 폴더에 그대로) |
| **공용** `Tools/tests/run_tests.ps1` | `Suite-Decal` 함수 + 목록 · switch · 도움말에 `decal` 한 단어씩만 stage |
| 문서 | 새 `docs/DECAL.md`, `README.md` · `AGENT_HANDOFF.md` 한 줄씩, `docs/NOVA_CLI.md` · `docs/SHADER_GRAPH.md` 는 `Decal` 단어 |

C# 네이티브 표 (`ScriptBindings.cpp` · `NativeApi.cs`) · `Source/Physics2D/` · `GameObject.*` · 씬 파일 · `CliCommands.cpp` 는 고치지 않았다. Decal 은 C# 바인딩이 아직 없다 (필요하면 Codex 의 `J2_*` 8 개 뒤에).

## 검증 (Debug, 독립 빌드 `E:\NovaTest\ClaudeDecalEngine` = `673c3b3` + Claude 변경만)

- `decal` **5/5** (`TestResults/decal3`): 바닥 빨강 (상자 안 1800/1800, 밖 0) · Opacity 0 · Base Map 알파 줄무늬 (870/1800) · Shader Graph 체커 데칼 · 저장 → 다시 열기
- `decal · shadergraph · render` **37/37** (`TestResults/regress2`, 32 를 고친 뒤 다시)
- OpenGL 짧은 확인: DX 와 같은 그림, HANG 없음 (DecalTech 만 변환)
- 커밋한 파일은 독립 빌드와 바이트가 같음을 확인 (`cmp`), 공용 두 파일은 독립 빌드의 내용을 그대로 stage

## 공용 빌드 · 테스트 에디터 사용

- **공용 `build/` · `Binaries/` 는 사용 안 함.** Claude 의 테스트 에디터는 모두 닫았다
- 따로 빌드한 엔진으로 검사할 때는 `$env:NOVA_ENGINE` 을 그 엔진으로 (안 하면 `nova open` 이 Hub 의 `engine.json` 엔진을 띄운다)

## 편집 중인 공용 파일

- 없음

## Codex 에게

- Decal 이 `32. InstancedBasic.fx` 를 바꿨다 (기법을 매크로로 감쌈 — 기존 셰이더는 결과가 같다, `render` 8/8). 셰이더 캐시가 한 번 다시 컴파일된다
- 다음: Claude 가 내 독립 빌드에 Codex 의 Joint 2D 파일을 넣어 **통합 검사** (`joints2d` · `physics2d` · `decal`) 를 한다. 결과는 여기에 적는다
