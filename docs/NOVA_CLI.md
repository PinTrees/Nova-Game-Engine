# NOVA CLI

터미널이나 AI 에이전트가 **실행 중인 NOVA 에디터를 명령으로 다루는** 도구입니다 (Unity CLI 와 같은 역할).
에디터 창을 앞으로 가져오거나 마우스로 조작하지 않아도 됩니다. 포커스를 잃어 멈춰 있던 에디터도 요청이 오면 깨어나 처리합니다.

## 설치

NOVA Hub > **설치** 탭 > **NOVA CLI** > **설치**.

- `nova.exe` 를 `%LOCALAPPDATA%\NOVA\CLI` 에 복사하고 그 폴더를 사용자 PATH 에 더합니다 → **새로 연** 터미널에서 `nova` 로 실행.
- 엔진을 다시 빌드해 새 버전이 생기면 카드에 "업데이트" 가 뜹니다. **제거** 는 파일과 PATH 항목을 지웁니다.
- 자동화: `NovaEngine.exe --install-cli` / `--uninstall-cli` (결과는 `Binaries\hub_log.txt`).
- 개발 중에는 `Binaries\nova.exe` 를 바로 써도 됩니다.

## 동작 방식

- 에디터는 시작할 때 이름 있는 파이프 `\\.\pipe\nova-editor-<pid>` 를 열고, 파이프 이름과 임의 토큰을
  `%LOCALAPPDATA%\NOVA\Instances\<pid>.json` 에 적습니다 (이 PC, 같은 사용자만). 네트워크 포트는 쓰지 않습니다.
- `nova` 는 그 파일로 에디터를 찾습니다: `--project <폴더|이름>`, `--pid <pid>`, 환경 변수 `NOVA_PROJECT`,
  지금 폴더가 들어 있는 프로젝트, 하나뿐인 에디터 순.
- 요청·응답은 한 줄 JSON. 명령은 에디터의 메인 스레드에서 실행되고, 바꾸는 명령은 **Undo 한 단계**("CLI ...")로 남습니다.
- 종료 코드: 0 성공, 1 명령 실패(이유는 stderr), 2 에디터를 찾지 못함, 3 사용법 오류. `--json` 이면 결과를 JSON 그대로 출력.

## 명령

| 분류 | 명령 |
|---|---|
| 에디터 | `status`, `open <프로젝트> [--background] [--graphics opengl\|d3d11]` (graphics = 이번 실행만 그래픽 API), `quit [--force]`, `info` (`liveObjects` = 메모리에 있는 GameObject 전체 — 지운 오브젝트가 새는지 볼 때), `log [-n 40] [--grep 글자] [--errors] [--follow]` |
| 오브젝트 | `hierarchy [--components] [--depth N] [--root <대상>]`, `find [이름] [--component 종류]`, `get <대상> [--component 종류]` |
| 수정 | `set <대상> [--name] [--active] [--tag] [--layer] [--static] [--position x,y,z] [--rotation x,y,z] [--scale x,y,z] [--world-position x,y,z] [Component.field=value ...]` |
| 만들기 | `create <종류> [--name] [--parent] [--position] [--rotation] [--scale] [--preset N]`, `delete <대상>`, `add-component <대상> <종류> [--values '{...}']` (종류에 C# MonoBehaviour 클래스 이름도 됨 — 값은 필드만 `{"speed":42,"target":"Ball"}`, GameObject·Transform 필드는 이름/경로로), `remove-component <대상> <종류>`, `parent <대상> <새 부모> \| --root`, `select <대상> \| --asset <Assets/...> \| --none` (`--asset` = Project 창에서 파일 고르기 → Inspector), `terrain-trees <지형> [--count N] [--clear]` (Paint Trees 의 Mass Place Trees — 종류가 없으면 Oak/Pine/Birch) |
| 씬·Play | `scene open <Assets/...scene> [--force]`, `scene new [--force]` (새 씬: 카메라·빛·볼륨), `scene save [--as Assets/Scenes/이름.scene]` (`--as` = 대화상자 없이 다른 이름으로), `raycast <x,y,z> <dx,dy,dz> [--max 거리] [--triggers]` (Play 중 Physics.Raycast — 맞은 오브젝트·점·법선·거리), `layers [--set 8 --name Enemy] [--add-tag Boss] [--remove-tag Boss]` (Tags and Layers), `physics [--gravity 0,-9.81,0] [--ignore Player,Enemy] [--collide A,B] [--all true\|false]` (Gravity · Layer Collision Matrix, `--2d` = Physics 2D 설정), `set <대상> --layer Water` (이름 또는 번호), `layers --add-sorting-layer Background [--move-sorting-layer 이름 --by -1]` (Sorting Layers), `sprite-slice <png> --mode grid\|count\|auto\|sheet [--cell 32,32] [--count 4,1] [--pivot 0.5,0] [--ppu 16] [--filter point] [--animation 12]` (Sprite Mode = Multiple + `.spriteanim`), `play`, `stop`, `pause [on\|off]`, `step`, `undo`, `redo` |
| 보기 | `camera [--position x,y,z --target x,y,z \| --frame <대상> [--distance d]]`, `screenshot <파일.png> [--view scene\|game\|editor]` (editor = 메뉴·창까지 에디터 전체) |
| 창·설정 | `window audio-mixer [--category <Assets\X.mixer>] [--close]` (Audio Mixer 창), `window <preferences\|project-settings\|build-settings> [--category 분류] [--close]`, `window <scene\|game\|project\|console\|hierarchy\|inspector\|animator> [--float]` (도킹 탭을 앞으로 / `--float` = 도킹에서 떼어 에디터 밖 OS 창으로 — 배치 파일에 남으니 시험 뒤 되돌릴 것), `wait [프레임]` (백그라운드 에디터가 N 프레임 그릴 때까지 — 씬 불러오기 등을 기다릴 때), `perf [--frames N] [--depth D]` (N 프레임(기본 240) 성능: 프레임 ms·fps(벽시계, 수직 동기 없음), CPU ms, GPU ms(타임스탬프), GPU 단계별 상위 12 — Profiler 창 없이도 모은다. 성능 비교는 `build.bat release` 로), `graphics [--editor DirectX11\|OpenGL] [--player OpenGL,DirectX11] [--auto true\|false]` |
| 렌더링 | `gfx-test [both\|DirectX11\|OpenGL] [--out 폴더] [--width W --height H]` — 엔진 렌더러와 같은 방식(Gfx 층 + 효과)으로 시험 장면을 엔진 장치와 OpenGL(숨은 창)에 그려 비교, `rhi-test [both\|DirectX11\|OpenGL] [--out 폴더] [--width W --height H]` — 같은 시험 장면(PBR + 그림자)을 RHI 로 API 마다 그려 PNG 와 픽셀 차이(평균·최대·8 넘는 픽셀 %) |
| 셰이더 | `shader-cross [파일 이름 일부] [--out 폴더] [--max-errors N]` — 엔진 `Shaders/*.fx` 를 OpenGL 용 GLSL 4.50 으로 변환해 보고 (pass 마다 성공·실패 이유, `--out` 이면 `.glsl` 파일) |
| 자동 저장 | `autosave [status\|now\|recover\|discard]` — 상태(켜짐·간격·복구할 것), 지금 저장(`Library/AutoSave/autosave_<pid>.scene`, 원래 씬 파일은 그대로), 지난 세션(충돌) 복구 / 버리기 |
| 가져오기 설정 | `import-settings <에셋> [--values '{...}'] [--reset]` — Unity 의 Import Settings(`<파일>.meta`). 값 없이 = 지금 설정 + 가져온 결과(텍스처: 원본·가져온 크기·형식·밉, 오디오: 채널·스트리밍, 모델: unitScale·Humanoid 본 매핑), `--values` = 바꾸고 바로 다시 가져오기(씬도 다시 만듦, Play 중 안 됨), `--reset` = .meta 지우고 기본값. 텍스처 `{"maxSize":512,"compression":"None\|NormalQuality\|HighQuality","mipmaps":true}`, 모델 `{"scaleFactor":2,"importAnimation":true,"animationType":"Generic\|Humanoid","humanBones":{"LeftFoot":"노드 이름 또는 None"}}`, 오디오 `{"loadType":"DecompressOnLoad\|CompressedInMemory\|Streaming","forceToMono":true}` |
| 패키지 | `package list` (레지스트리 패키지 + 프로젝트에 넣었는지·불러왔는지·오류), `package add <이름 | 폴더 | package.json>` / `package remove <이름>` (예: `com.nova.cameras`, 폴더·파일 경로면 Add from disk = manifest 에 `file:` 상대 경로 — `Packages/manifest.json` 에 적고 DLL 을 바로 불러오거나 내림. 씬이 쓰는 중이면 다음 시작 때 빠짐), `window package-manager [--close]` |
| 모델 편집기 | `model <op> [경로] [--이름 값 …]` — 패키지 `com.nova.modeling`. 만들기(`add --type cube…`) · 고르기(`select.box`, `select.normal` …) · 변환(`translate` · `rotate` · `scale`) · 편집(`extrude` · `inset` · `loopcut` · `bevel` · `subsurf` · `mirror` …) · `import` / `export <.fbx\|.obj\|.glb>` · `render --dir 폴더 --views front,right,persp` (PNG) · 버텍스 그룹(`group.assign/select`) · `checkpoint` · 기준 그림(`ref.add`) + 실루엣 비교(`compare` — IoU · 너비 힌트 · 차이 그림) · `batch <파일>`(Undo 한 번) · 모디파이어(`modifier.mirror` · `modifier.subsurf`) · 비례 편집(`--proportional`) · 머리카락(`hair.strand` · `hair.card`) · UV(`uv.smart`) · 재질 색 · 툰 렌더 · 셰이프 키 / 표정(`shape.add` · `shape.edit` · `shape.value` · `shape.mirror`) · 애니메이션(`anim.new` · `anim.key` · `anim.time`, `rig.pose --move`) · 리깅(`rig.humanoid` · `rig.weights` · `rig.chain` · `rig.paint` · `rig.pose` · `rig.check`, `export <.vrm>` = VRM 1.0 · `<.fbx>` = 본 + 스킨 → `create character --model`) · `undo`. 결과 = 점 · 면 수 · 경계 상자 · 선택 JSON. 목록 `nova model help`, 규격 [MODEL_EDITOR.md](MODEL_EDITOR.md) |
| 2D 애니메이터 | `anim2d <op> [경로] [--이름 값 …]` — 패키지 `com.nova.animation2d` (Spine 같은 2D 뼈대). 본(`bone.add --world` · `bone.set` · `bone.list`) · 그림(`image.make` 로 조각 만들기, `image.add` · `attachment.add` · `slot.set --order`) · 애니메이션(`anim.new` · `anim.time` · `pose` · `anim.key --curve smooth`) · `render` (PNG, `--anim --time`) · `export` (스프라이트 시트 + JSON 또는 `--sequence`) · `batch <파일>`(Undo 한 번) · `undo`. 목록 `nova anim2d help`, 규격 [ANIMATION2D.md](ANIMATION2D.md) |
| Shader Graph | `shadergraph <op> [경로] [--이름 값 …]` — 노드로 재질 셰이더 (Window > Shader Graph). `new <Assets/X.shadergraph> [--material Lit\|Unlit\|Decal]` · `open` · `info` · `nodes` (노드 종류 + 포트) · `node.add --type Multiply [--values '{"B":[1,0,0,1]}']` · `node.set` · `node.delete` · `connect --from N [--out Port] --to M\|Master --in "Base Color"` · `disconnect` · `property.add --name Tint --type Color [--value] [--node]` · `property.set/delete` · `settings [--material Lit\|Unlit\|Decal] [--surface Opaque\|Transparent] [--alpha-clip true\|false] [--path P]` · Sub Graph (`new X.shadersubgraph`, `output.add/set/delete`, `node.add --type "Sub Graph" --options '{"asset":…}'`) · Custom Function (`--options '{"name","mode":"String\|File","body","file","inputs","outputs"}'`) · Vertex 단계 (`--in "Vertex Position"`, [예제](examples/shadergraph_wave.txt)) · `compile [--hlsl]` · `save` (셰이더 만들기 — 오류가 돌아온다) · `material` (`.mat`) · `undo` · `redo` · `batch <파일 \| ->` (Undo 한 번, [예제](examples/shadergraph_lava.txt)). 자세히: [SHADER_GRAPH.md](SHADER_GRAPH.md) |
| Reflection Probe | `probe info` (프로브 목록 · 마지막 뷰가 쓴 수) · `probe bake [--name X]` (지금 찍어 `<씬 폴더>/<씬 이름>/ReflectionProbe-<n>.dds` — 씬이 저장돼 있어야, 이름이 없으면 모든 Baked) · `probe render [--name X]` (Realtime 다시 찍기). 값은 `set X --component ReflectionProbe --values '{"mode":"Realtime","size":[10,4,10],"boxProjection":true}'`. 자세히: [REFLECTION_PROBE.md](REFLECTION_PROBE.md) |
| Adaptive Probe Volume | `probevolume info` (단계 · 복셀 준비 · 짓는 판) · `probevolume probe --position x,y,z` (그 자리 프로브 · 복셀 값, DX11) · `probevolume voxels` · `probevolume debug --view 1\|2\|0 [--nodirect true]` (진단 보기). 값은 `set X --component AdaptiveProbeVolume --values '{"probeSpacing":0.5,"cascades":3}'`. 자세히: [ADAPTIVE_PROBE_VOLUME.md](ADAPTIVE_PROBE_VOLUME.md) |
| LOD Group | `lod info` (LOD · 렌더러 · 뷰마다 고른 LOD 와 화면 높이) · `lod assign --name G --lod 1 --object O` · `lod set --name G --fadeMode 1 --lod 0 --fadeWidth 0.3` · `lod recalc --name G`. 컴포넌트는 `add-component G LODGroup`. 자세히: [LOD_GROUP.md](LOD_GROUP.md) |
| 기타 | `assets [폴더] [--pattern 글자]`, `build <출력 폴더> [--run]`, `build-status [--wait]`, `call <명령> [json]`, `ai-guide`, `help`, `nova help --editor`(에디터가 아는 명령) |

**대상** = 이름, `부모/자식` 경로, 또는 `#id` (`hierarchy` 가 보여 주는 fileID). 같은 이름이 여럿이면 오류와 함께 후보 id 를 알려 줍니다.

**만들 수 있는 종류**: empty, cube, sphere, capsule, cylinder, plane, quad, directional-light, point-light, spot-light, camera,
terrain, tree, rock, rock-scatter, ocean, lake, river, particle-system, audio-source, volume, character (기본 캐릭터: 스킨 메시 + Animator, `--model <FBX | VRM | GLB> --controller <.controller>` 로 다른 모델·컨트롤러 — 컨트롤러를 안 주면 기본 컨트롤러를 Humanoid 리타게팅. **VRM** 이면 묻힌 그림 · 재질을 `<파일>.Textures` · `<파일>.Materials` 로 꺼내 붙이고, 파일의 사람 본 매핑으로 Humanoid, Spring Bone → **Dynamic Bone**),
third-person-character (= player: 캐릭터 + Character Controller + ThirdPersonController + Main Camera 의 Follow Camera — 패키지 Starter Assets·Cameras 가 없으면 넣는다).

### C# 실행 · 한 번에 여러 명령

| 명령 | 설명 |
|---|---|
| `exec "<C# 코드>"` / `exec --file 코드.cs` | 에디터 안에서 C# 를 실행 (Unity 의 `UnityEngine` 처럼 `NovaEngine`·`NovaEngine.UI`·System.Linq 와 게임 스크립트를 쓸 수 있다). 식이면 그 값(`GameObject.Find("Gold").transform.position` → `(-3.75, 0.60, 0.00)`), 문장(`;` 로 끝남)이면 `return` 한 값(없으면 null), 목록은 `[a, b, …]`. 컴파일 오류는 `Exec.cs(줄,열): error CS…`, 실행 중 예외는 그 메시지로 실패. `Library/NovaExec/` 에 작은 프로젝트를 만들어 `dotnet build`(약 1.3 초) → 수집 가능한 컨텍스트로 읽어 실행·내림 (메인 스레드에서 기다린다) |
| `batch <파일 \| -> [--keep-going]` | 줄마다 nova 명령 하나를 한 프로세스에서 차례로 (앞의 `nova` 는 있어도 됨, `#` 주석, `"…"` / `'…'` / `\"`). batch 에 준 `--project`·`--pid`·`--json`·`--timeout` 은 모든 줄에 붙는다. 실패하면 멈춘다(`--keep-going` 이면 계속, 끝에 실패 수). 명령 20 개: 따로 0.21 초 → batch 0.07 초 |

## 예

```bash
nova open D:\NovaProjects\MyGame --background
nova hierarchy --components
nova create cube --name Box --position 0,1,0 --scale 2,1,2
nova set Box MeshRenderer.castShadows=1 --rotation 0,30,0
nova add-component Box RigidBody --values "{\"mass\": 5}"
nova camera --frame Box --distance 6
nova screenshot box.png
nova play
nova get Box --json
nova stop
nova log --errors
nova scene save
```

## AI 에이전트에게

`nova ai-guide` 가 에이전트용 사용 안내(작업 순서·규칙)를 출력합니다. Hub 카드의 **AI 안내 복사** 는 에이전트에게 붙여 넣을 한 줄을 복사합니다.
