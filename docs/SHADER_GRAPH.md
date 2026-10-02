# Shader Graph

Unity 의 Shader Graph 처럼 **노드를 이어 재질 셰이더를 만듭니다**. 엔진 코어 기능이라 패키지 없이 쓸 수 있고, 게임 빌드에서도 동작합니다.

![Shader Graph](../Showcase/180_ShaderGraph_용암_Voronoi.webp)

## 쓰는 법

1. Project 창 **Create > Shader Graph** → `New Shader Graph.shadergraph` (더블클릭 = **Window > Shader Graph** 로 열기)
2. 캔버스에서 **오른쪽 클릭 또는 Space** = Create Node (검색 · 분류 메뉴). 출력 핀을 끌어 입력 핀에 놓아 잇습니다. 핀을 끌어 **빈 곳에 놓으면** Create Node 가 열리고, 만든 노드를 그 핀에 자동으로 잇습니다
3. 오른쪽 **Fragment** 노드 (Master) 의 입력에 이어 표면을 정합니다: Lit = Base Color · Normal (Tangent Space) · Metallic · Smoothness · Emission · Ambient Occlusion · Alpha, Unlit = Base Color · Alpha
4. 왼쪽 **Blackboard** 의 `+` 로 속성 (Float · Color · Vector2/3/4 · Texture2D) 을 만들고 캔버스로 끌어 놓습니다. 속성 값은 **재질마다** (재질 Inspector 의 Properties)
5. **Save Asset (Ctrl+S)** = 저장 + 셰이더 만들기. 오류는 창 아래 상태 줄 (HLSL 컴파일러 메시지 포함)
6. **Apply to Selection** = 그래프 옆 `<이름>.mat` 를 만들어 (없으면) 고른 Mesh Renderer 에 넣습니다. 재질 Inspector 의 Shader 목록에도 `Shader Graphs/<이름>` 이 나옵니다

- 이어지지 않은 입력은 노드 안에서 값을 바로 바꿉니다 (UV · Position 같은 입력은 기본으로 메시 값)
- 노드를 고르면 오른쪽 **Graph Inspector** 에 설명 · 입력 값 · 노드 설정 (Color 노드의 색, Swizzle 의 mask, Sample Texture 2D 의 Type (Default / Normal) · 그림)
- Delete = 노드 · 선 지우기, Ctrl+Z / Ctrl+Y, 휠 = 확대, 오른쪽 끌기 = 화면 이동, Frame All
- 그래프 기본값을 바꿔도 **이미 만든 재질은 제 값**을 지킵니다 (Unity 와 같다). 새 재질은 새 기본값

## 노드 (65 종)

| 분류 | 노드 |
|---|---|
| Input / Basic | Float, Vector2/3/4, Color, Time (Time · Sine Time · Cosine Time · Delta Time) |
| Input / Geometry | UV, Position (World), Normal Vector, View Direction, Screen Position |
| Input / Texture | Sample Texture 2D (RGBA · R · G · B · A, Type = Normal 이면 탄젠트 노멀로 풀어 줌) |
| Math | Add, Subtract, Multiply, Divide, Power, Square Root, Absolute, Negate, One Minus, Reciprocal, Exponential, Log, Modulo, Posterize, Saturate, Fraction, Minimum, Maximum, Clamp, Remap, Floor, Ceiling, Round, Step, Lerp, Smoothstep, Sine, Cosine, Tangent, Dot Product, Cross Product, Normalize, Length, Distance, Fresnel Effect |
| Channel | Split, Combine, Swizzle |
| UV | Tiling And Offset, Rotate, Polar Coordinates |
| Procedural | Simple Noise, Gradient Noise, Voronoi, Checkerboard, Ellipse, Rectangle |
| Artistic | Normal Strength, Normal Blend, Contrast, Saturation |
| Utility | Branch, Property (Blackboard) |

형 규칙은 Unity 와 같습니다: **동적 포트** (Add · Multiply · Lerp …) 는 이은 것 중 가장 큰 폭, 작은 벡터 → 큰 벡터는 float 이면 모든 칸에 · 나머지는 0, 큰 → 작은 은 앞 칸만. Texture2D 는 Texture 입력에만, 고리는 거절합니다. Procedural 노드의 식은 Unity Shader Graph 문서의 생성 코드와 같습니다.

## CLI (AI 에이전트)

창과 같은 연산을 `nova shadergraph <op>` 로 합니다 (`nova shadergraph help`, 노드 목록 = `nova shadergraph nodes`).

```bash
nova shadergraph new Assets/Shaders/Lava.shadergraph            # (--material Unlit)
nova shadergraph batch docs/examples/shadergraph_lava.txt       # 한 줄에 연산 하나, Undo 한 번
nova shadergraph save                                           # 저장 + 셰이더 (오류가 돌아온다)
nova shadergraph material                                       # Assets/Shaders/Lava.mat
nova set Sphere --component MeshRenderer --values '{"m_MaterialPaths":["Assets/Shaders/Lava.mat"]}'
```

| 연산 | 내용 |
|---|---|
| `new <path> [--material Lit\|Unlit] [--force]` · `open <path>` · `save [path]` · `info` | 문서 (info = 속성 · 노드 (입력: 이은 것 / 값) · Master) |
| `node.add --type T [--x --y] [--values '{"B":[1,0,0,1]}'] [--options '{"mask":"xy"}']` | 노드 id 는 새 그래프에서 1 부터 차례로 |
| `node.set --id N [--values] [--options] [--x --y]` · `node.delete --id N` | values · options 는 합친다 (null = 지움) |
| `connect --from N [--out Port] --to M\|Master --in Port` · `disconnect --to M --in Port` | 입력 하나에 선 하나 (새로 이으면 바꾼다) |
| `property.add --name --type [--value] [--range a,b] [--texture] [--ref] [--node]` · `property.set` · `property.delete` | Blackboard |
| `settings --material Lit\|Unlit` · `compile [--hlsl]` · `material [--mat path]` · `undo` · `redo` · `window` | |

## 구조 (엔진 코드)

- `Source/ShaderGraph/ShaderGraph.*` — 그래프 (JSON `.shadergraph`), 노드 정의 (포트 · 코드 생성 람다), 형 변환, `Generate()` → `.fx`
- `ShaderGraphRuntime.*` — `CustomShaders` Provider: 재질의 Shader 가 `Shader Graphs/<이름>` 이면 처음 찾을 때 그래프 → `<프로젝트>/Library/ShaderGraph/<이름>.fx` → `LoadEffect` → 등록. 그리기 = `DrawInstanced` (Mesh Renderer, MeshBatcher 의 묶음) · `DrawSkinned` (Skinned Mesh Renderer). 재질 값 = `.mat` 의 `Properties` (`{"_Tint": [1,0,0,1]}`)
- `ShaderGraphOps.*` — 문서 + 연산 (창 · CLI 공용, Undo 스냅숏)
- `ShaderGraphWindow.*` — imgui-node-editor 캔버스, Blackboard, Graph Inspector, `.shadergraph` 에셋 종류, CLI 등록
- 만든 `.fx` 는 엔진 `32. InstancedBasic.fx` 를 포함해 `ShadeLit` (URP Lit — 빛 · 그림자 · 하늘 · SSAO · 안개 · Light Culling Mask) 을 그대로 씁니다. 기법 `GraphBatchTech` (VS_Batch) · `GraphSkinnedTech` (VS_Skinned)

## 아직

노드 미리보기 · Main Preview, Alpha Clip (깊이 프리패스에 그래프가 필요), Transparent, Vertex 단계 (Position · Normal), Sub Graph, Custom Function, Keyword, Sampler State 노드, 그래프 셰이더를 백그라운드에서 컴파일 (지금은 저장할 때 2 ~ 7 초 멈춤)
