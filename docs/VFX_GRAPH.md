# Visual Effect Graph (VFX Graph)

Unity 의 Visual Effect Graph 처럼 **GPU 에서 수십만 개의 파티클**을 시뮬레이션하고 그립니다. 엔진 코어 기능 (패키지 없이), Game · Scene 뷰 · 게임 빌드 (PC · 안드로이드) 에서 동작합니다.
대화로 이펙트를 만드는 **VFX Assistant** 는 이 PC 에 설치된 Claude Code 를 그대로 씁니다 (API 키 없이, 구독 로그인으로만).

![VFX Graph 견본](../Showcase/213_VFX_Graph_견본_7종.webp)

![Visual Effect Graph 창 · VFX Assistant](../Showcase/214_VFX_Graph_편집창.webp)

![불꽃놀이 — GPU Event (로켓 → 폭발 → 반짝임)](../Showcase/215_VFX_불꽃놀이_GPU_Event.webp)

## 쓰는 법

1. Project 창 **Create > Visual Effect Graph** → `New VFX.vfx` (더블클릭 = **Window > Visual Effect Graph**). 창의 **File > New from Template** 로 견본 (Fireworks · Magic Circle · Tornado · Sparks · Galaxy · Fire · Portal) 에서 시작해도 됩니다
2. 장면에 놓기: **GameObject > Effects > Visual Effect** 의 Asset Template 에 `.vfx` (끌어 놓기 · ⊙), 또는 그래프 창 메뉴 **Place in Scene**. Unity 처럼 **Play 모드가 아니어도** 장면에서 재생됩니다
3. 그래프의 **시스템** 하나 = 세로로 쌓인 문맥 네 개: **Spawn** (주황 — 초당 수 · Burst · Loop/Duration/Delay · Start/Stop 이벤트) → **Initialize Particle** (초록 — 태어날 때) → **Update Particle** (노랑 — 프레임마다) → **Output Particle Quad** (보라 — 모양 · 섞기 · 방향 · HDR 세기)
4. 문맥의 **+** 또는 캔버스에서 **Space / 오른쪽 클릭** = 검색 창 (문맥을 골랐으면 그 문맥의 블록, 아니면 새 시스템 · 견본의 시스템). 블록의 값은 노드 안 (짧게) 과 오른쪽 **Inspector** (곡선 · 그라디언트 · 콤보 · 색 고르기 · 속성 연결 🔗)
5. 왼쪽 **Blackboard** 의 `+` = Exposed Property (Float · Int · Bool · Vector3 · Color). 블록 값의 🔗 로 속성에 잇고, 장면의 Visual Effect Inspector 에서 **오브젝트마다 덮어쓰기** (Unity 와 같은 왼쪽 체크)
6. **GPU Event**: 시스템의 **On Die** 핀을 다른 시스템의 **GPU Event** 핀으로 끌면, 부모 파티클이 죽은 자리에서 자식이 태어납니다 (Unity 의 Trigger Event On Die → GPU Event). 불꽃놀이: 로켓 → 폭발 → 반짝임
7. 고칠 때마다 장면의 Visual Effect 가 **저장 전에도 바로** 따라옵니다. **Ctrl+S** 저장, Ctrl+Z / Ctrl+Y

### 블록

| 문맥 | 블록 | 하는 일 |
|---|---|---|
| Initialize | **Set Position (Shape)** | 점 · 구 · 원 · 상자 · 원뿔 · 선 · 토러스 (표면 / 부피, 호, **평면 XZ · XY · YZ** — 세운 고리 = 포털) |
| | **Set Position (Spiral Arms)** | 나선 팔 (팔 수 · 감김 · 흩어짐 · 두께) — 은하 · 소용돌이 |
| | **Set Velocity** | 방향 · 모든 방향 · 모양 바깥 (From Shape) · 원뿔. 여러 개면 더해진다 (바깥 + 위 = 깔때기) |
| | Set Lifetime · Set Size (Random) · **Set Color** (고정 · 둘 사이 · 무지개, HDR 세기) · Set Angle · Angular Velocity | |
| | **Inherit Source (GPU Event)** | 부모 파티클의 속도 (배율) · 색 |
| Update | Gravity · Linear Drag · Speed Limit | |
| | **Turbulence** | 3D 잡음 힘 (옥타브 · 흐름) — 연기 · 불 · 마법 가루 |
| | **Vortex** · **Conform to Sphere (Attractor)** | 축 둘레 회전 + 당김, 한 점 / 구 표면으로 |
| | **Orbit** | 축 둘레로 위치 · 속도를 돌린다 (Falloff = 멀수록 느리게 — 은하의 차등 회전) |
| | **Collide with Plane** | 튕김 · 마찰 · 부딪힐 때 수명 줄이기 |
| | **Color over Life · Size over Life** | 그라디언트 (HDR) · 곡선 |

Output 모양은 그림 없이 셰이더가 만듭니다: Soft Dot · Glow · Star · Sparkle · Ring · **Spark** (속도로 늘린 불꽃 줄기) · **Smoke** (잡음 덩어리) · Square · Heart, 또는 **Texture** (+ 플립북 열 × 행, 수명 동안 한 번 / 초당 칸).
Additive 는 빛 · 불 · 마법 (HDR 1 보다 크면 Bloom 으로 빛난다), Alpha Blend 는 연기 · 먼지. Soft Particles (장면 깊이에 닿는 곳을 부드럽게).

### 공간 · 이벤트

- **World** (기본): Visual Effect 가 움직여도 이미 태어난 파티클은 월드에 남는다. 위치 값 (Attractor · Vortex · Orbit 의 가운데, 충돌 평면) 은 Visual Effect 를 따라간다
- **Local**: 파티클이 Visual Effect 와 함께 움직인다
- Spawn 의 **Start Event** (기본 `OnPlay`) · **Stop Event** (`OnStop`) — 다른 이름으로 바꾸면 `SendEvent("Burst")` 로만 시작. Visual Effect 의 **Initial Event Name** (기본 OnPlay, 비우면 스스로 시작하지 않음)
- Spawn **Rate Property**: 초당 수를 Float 속성에 연결 (예: 불꽃놀이 Launch Rate)

## 어떻게 동작하나

- `Shaders/58. VFX.fx` 하나: compute 커널 Reset · **Spawn** · **Update** + 그리기 (Additive · Alpha). 블록은 **목록 (float4 칸) 을 셰이더가 차례로 읽어 실행** — 그래프를 바꿔도 셰이더를 다시 컴파일하지 않는다 (Shader Graph 와 다른 점. 고치는 즉시 장면에 보임)
- 파티클 하나 = 96 바이트 (GPU 만 쓰는 raw 버퍼). 칸은 원자적 카운터로 고른다 (용량을 넘으면 가장 오래된 칸부터). 죽은 칸은 그리기에서 화면 밖으로
- **그리기 = 파티클 버퍼를 그대로 인스턴스 정점 버퍼로** (CPU 로 읽어 오지 않는다 — 안드로이드 GLES 도 같은 길). 시스템마다 그리기 한 번
- GPU Event: 부모의 Update 가 죽는 파티클의 위치 · 속도 · 색을 이벤트 버퍼에 (프레임마다 최대 4096), 자식의 Spawn 이 이벤트마다 N 개. 부모가 먼저 돌도록 시스템 순서를 정한다 (고리는 거부)
- 살아 있는 수는 몇 프레임 늦게 읽는다 (기다리지 않음) — Inspector · `nova vfx stats` · C# `aliveParticleCount`
- 시뮬레이션은 프레임의 첫 그리기 (Game 또는 Scene 뷰) 에서 한 번 — DirectX 11 · OpenGL 4.5 · Vulkan · **OpenGL ES 3.2 (안드로이드)**. compute 가 없는 장치 (`SupportsGpuDriven` = false) 에서는 그리지 않는다
- 코드: `Source/Effects/VfxAsset.*` (에셋 · 블록 정의표 · 블록 목록 만들기), `VfxTemplates.cpp` (견본), `VisualEffect.*` (컴포넌트 · 이벤트 · Spawn 수), `VfxRuntime.*` (GPU),
  `VfxScripting.cpp` (C#), `Source/Editor/Windows/VfxGraphWindow.*` · `VfxAssistantWindow.*`, `Source/Editor/VfxCli.*`

## VFX Assistant (Claude Code)

**Window > VFX Assistant** (그래프 창 메뉴의 VFX Assistant). "밤하늘을 가득 채우는 화려한 불꽃놀이" 처럼 말하면 Claude 가 `.vfx` 를 만들고 장면에 놓습니다.

- **이 PC 의 Claude Code** 를 headless 로 실행합니다 (`claude -p --output-format stream-json`). PATH 의 `claude.exe`, npm 설치 (`node` + `cli.js`), Claude 데스크톱 앱에 딸린 `claude.exe` 중 **판이 가장 높은 것**
- **API 키를 쓰지 않습니다**: 자식 프로세스 환경에서 `ANTHROPIC_*` · `CLAUDE_CODE_*` 를 지운다 → 그 PC 의 로그인 (`~/.claude`, Claude 구독) 으로만. Claude Code 가 없거나 로그인하지 않았으면 동작하지 않고 안내를 띄운다
  - 처음 한 번: 터미널에서 `claude` → `/login` (Claude 계정으로)
- Claude 가 쓸 수 있는 도구는 **`nova vfx …` · `nova create visual-effect` · `nova camera` · `nova screenshot` 과 파일 읽기뿐** (`--allowedTools`, Edit · Write · 웹은 막음). 그래서 사람이 쓰는 CLI 와 같은 길로 고치고, 그래프 창 · 장면이 바로 따라온다. 결과를 스크린샷으로 찍어 보고 다듬기도 한다
- 대화는 이어진다 (`--resume`), **New Chat** 으로 새로. Stop = 프로세스 나무를 끈다 (Job 객체)
- 터미널 · 자동 검사: `nova vfx assistant.send --message "…" [path]`, `nova vfx assistant.status`, `nova vfx assistant.stop`

## C# (Unity 와 같은 API)

```csharp
using NovaEngine.VFX;

var vfx = GetComponent<VisualEffect>();
vfx.SetFloat("Spin", 90f);
vfx.SetVector4("Main Color", new Vector4(1f, 0.3f, 0.1f, 1f));   // Color 속성
vfx.SendEvent("OnPlay");
vfx.Stop();  vfx.Reinit();  vfx.playRate = 2f;  vfx.pause = true;
if (vfx.HasFloat("Spin")) Debug.Log(vfx.aliveParticleCount);
vfx.visualEffectAsset = "Assets/VFX/Portal.vfx";   // 다른 에셋으로 (다시 시작)
```

Play · Stop · Reinit · SendEvent, Set/Get/Has Float · Int · Bool · Vector2 · Vector3 · Vector4, ResetOverride, aliveParticleCount, playRate, pause, startSeed, resetSeedOnPlay, initialEventName, visualEffectAsset (경로), enabled, HasAnySystemAwake

## nova vfx (CLI)

```
nova vfx new Assets/VFX/Boom.vfx --template Fireworks
nova vfx blocks                                   블록 종류 · 값 (기본값 · 범위 · 설명)
nova vfx info Assets/VFX/Boom.vfx                 에셋 JSON + 문제
nova vfx block.add Assets/VFX/Boom.vfx --system Explosion --context update --type Turbulence --params '{"Intensity":4}'
nova vfx block.set Assets/VFX/Boom.vfx --system Explosion --context update --index 0 --bind '{"Force":"Wind"}'
nova vfx system.set Assets/VFX/Boom.vfx --system Rocket --data '{"spawn":{"rate":3},"output":{"shape":"Star"}}'   (JSON 합치기)
nova vfx property.add Assets/VFX/Boom.vfx --name Wind --type Vector3 --value 1,0,0
nova vfx set Assets/VFX/Boom.vfx --file boom.json  에셋 통째로 (--data '{...}' 도)
nova create visual-effect --asset Assets/VFX/Boom.vfx --name Boom --position 0,0,0
nova vfx event --object Boom --name OnStop        장면의 Visual Effect 에 이벤트
nova vfx override --object Boom --name Wind --value 0,3,0
nova vfx stats                                    시스템마다 살아 있는 수
nova vfx window Assets/VFX/Boom.vfx [--system Rocket --context update --block 0]
```

`.vfx` 는 JSON (`properties` · `systems[{name, capacity, space, spawn, initialize[], update[], output, editor}]`, 블록 = `{type, params, bind}`) — 게임 빌드는 씬이 가리키는 `.vfx` 를 따라 넣습니다.

## 검사

- `run_tests.ps1 -Only vfx,vfxgl,vfxvk` — CLI 편집 (속성 이름 바꾸면 연결도 따라감, 잘못된 문맥 거부, 없는 부모 알림), 세 API 의 GPU Spawn · Update (마법진 다섯 시스템), **GPU Event 사슬** (로켓 → 폭발 → 반짝임), OnStop · OnPlay, 속성 덮어쓰기 (Launch Rate 0 → 로켓 없음), 그리기 (켬 · 끔 화면 차이), C# API, 컴포넌트 JSON, Assistant 상태
- `Tools/tests/android_vfx.ps1` — MuMu (OpenGL ES 3.2): GLES 셰이더에 VFX 커널, `.vfx` 가 게임 데이터에, 기기의 compute 로 마법진 · 불꽃놀이 GPU Event, 그리기 시간

## 아직 없는 것

- Unity 의 연산 노드 (Operator — 블록 값에 수식 잇기), Sub Graph, Output Mesh · Strip (꼬리 띠), GPU 정렬 (Alpha 끼리 순서), Bounds 컬링, 깊이 버퍼 충돌 · SDF
- Trigger Event Always / Rate (살아 있는 동안 GPU Event — 로켓 꼬리), 파티클 속성 (Custom Attribute)
