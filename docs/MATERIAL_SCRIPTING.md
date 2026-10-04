# C# 재질 · Renderer — Material · Renderer.material · MaterialPropertyBlock · enabled · bounds

Unity 와 같은 이름 · 같은 뜻 (`using NovaEngine;`).

```csharp
var r = GetComponent<Renderer>();             // MeshRenderer → SkinnedMeshRenderer → SpriteRenderer 순서로 찾는다
r.material.color = Color.red;                 // 이 렌더러만의 사본 ("Shared (Instance)") — 다른 렌더러는 그대로
r.sharedMaterial.SetFloat("_Smoothness", 0.9f); // 공유 재질 — 이 재질을 쓰는 모든 렌더러
var m = Material.Load("Assets/Materials/Gold.mat");
r.sharedMaterial = m;

var block = new MaterialPropertyBlock();
block.SetColor("_BaseColor", Color.yellow);   // Shader.PropertyToID("_BaseColor") 도
r.SetPropertyBlock(block);                    // 재질은 공유한 채 값만 — null 이면 없앰

r.enabled = false;                            // 그리지 않는다 (그림자 포함)
Bounds b = r.bounds;                          // 월드 상자
r.shadowCastingMode = NovaEngine.Rendering.ShadowCastingMode.Off;
```

`MeshRenderer` · `SkinnedMeshRenderer` · `SpriteRenderer` 모두 `Renderer` 를 물려받는다 (Unity 와 같음).

| API | 하는 일 |
|---|---|
| `Renderer.material` · `materials` | 처음 읽을 때 이 렌더러만의 사본으로 바꾼다. 사본은 씬에 저장되지 않는다 (다시 열면 원래 재질) |
| `Renderer.sharedMaterial` · `sharedMaterials` | 공유 재질 (바꾸면 같은 재질의 모든 렌더러). 대입하면 그 재질 파일 경로가 씬에 저장된다 |
| `Material.color` · `SetColor` · `GetColor` · `SetFloat` · `GetFloat` · `SetVector` · `SetInt` · `HasProperty` · `name` · `shaderName` | URP 이름 `_BaseColor` (`_Color`) · `_EmissionColor` (HDR — 1 넘는 성분은 Intensity) · `_Metallic` · `_Smoothness` (`_Glossiness`) · `_Cutoff` · `_BumpScale` · `_OcclusionStrength`, 그 밖의 이름 = 패키지 · Shader Graph 속성. 색은 감마 |
| `new Material(source)` · `Material.Load(path)` | 런타임 사본 · 프로젝트의 `.mat` |
| `MaterialPropertyBlock` · `Renderer.SetPropertyBlock` · `GetPropertyBlock` · `HasPropertyBlock` | 렌더러마다 값 덮어쓰기 |
| `Renderer.enabled` · `isVisible` | Inspector 의 체크 상자 (씬에 저장된다). 꺼지면 본 패스 · 그림자 · 깊이 모두 그리지 않는다 |
| `Renderer.bounds` (`Bounds`: `center` · `extents` · `size` · `min` · `max` · `Contains` · `Intersects` · `Encapsulate`) | 월드 상자 — 아래 표 |
| `Renderer.shadowCastingMode` (`NovaEngine.Rendering.ShadowCastingMode`: `Off` · `On` · `TwoSided` · `ShadowsOnly`) | Inspector 의 Cast Shadows |

## 렌더러마다 다른 점

| | MeshRenderer | SkinnedMeshRenderer | SpriteRenderer |
|---|---|---|---|
| 재질 · `MaterialPropertyBlock` | 있음 (`_BaseColor` · `_EmissionColor` · `_Metallic` · `_Smoothness` 는 인스턴스 값 — 아래) | 있음 (원래 낱개로 그린다) | 없음 — `material` · `sharedMaterial` 은 `null`, `materials` 는 빈 배열, `SetPropertyBlock` 은 무시 (색은 `SpriteRenderer.color`) |
| `bounds` | 컬링이 마지막 프레임에 잰 상자 | 같음 — 애니메이션 여유 (기본 자세 상자의 60 % + 0.25 m) 를 더해 Unity 보다 크다 | 그림 사각형 × 월드 행렬 (바로) |
| `shadowCastingMode` | 바꿀 수 있다 | 바꿀 수 있다 | 늘 `Off` (바꿔도 무시) |

같은 프레임에 만든 Mesh · Skinned 렌더러의 `bounds` 는 다음 프레임부터 맞다 (그 전엔 위치에 크기 0).

## MaterialPropertyBlock 과 인스턴싱 (GPU 인스턴싱 속성)

Mesh Renderer 는 (메시, 서브셋, 재질) 이 같으면 인스턴싱 한 번으로 그린다. 인스턴스 값은 **112 바이트** = 월드 행렬 + MaterialPropertyBlock 값 3 칸
(Unity 의 `UNITY_INSTANCING_BUFFER` 자리 — 값이 렌더러마다 모두 달라도 한 묶음):

| 칸 | 엔진 Lit · Unlit | Shader Graph |
|---|---|---|
| 기본색 | `_BaseColor` · `_Color` | Color 속성 `_BaseColor` · `_Color` |
| Surface | `_Metallic` · `_Smoothness` (`_Glossiness`) — 수 | Float 속성 `_Metallic` · `_Smoothness` · `_Glossiness` |
| Emission | `_EmissionColor` (HDR — 엔진이 쓰는 선형 값으로) | Color 속성 `_EmissionColor` (그대로) |

- **블록의 이름이 모두 위 칸이고 재질이 모두 엔진 Lit · Unlit (Alpha Clipping 없음) 이거나 모두 Shader Graph** 이면 재질은 그대로 두고 값을 인스턴스로 넣는다.
  상자 100 개에 색 100 가지 = 묶음 1 개, 구 45 개에 금속도 · 매끄러움 · 색 · 발광 = 묶음 1 개 (Showcase 210).
  - 엔진: Emission 이 꺼진 재질에 Emission 맵이 있으면 (켜야 맵을 곱한다) 파생 재질.
  - Shader Graph: 같은 칸의 속성이 둘 (`_BaseColor` 와 `_Color`) 이면 어느 값인지 몰라 파생 재질. 그래프가 아직 만들어지는 중이어도 파생 재질.
- 그 밖의 블록 (다른 속성 · 패키지 셰이더 · 잘라내기 재질 · 엔진과 그래프가 섞인 렌더러) 은 **(공유 재질, 재질 값, 블록 값) 마다 파생 재질 하나** — 같은 값끼리는 한 묶음.
  공유 재질 값이 바뀌면 파생 재질도 다시 만든다 (`UMaterial::StateHash`). Skinned Mesh Renderer 는 늘 이 길 (원래 낱개로 그린다).
- GPU 오클루전 컬링 (`OcclusionCulling` 의 Compact) 도 고른 인스턴스의 값을 함께 옮긴다 — 켠 화면과 끈 화면이 같다.
- 부호 (`w >= 0` = 재질 값 — OpenGL 의 꺼진 입력 0,0,0,1 도 재질 값): 기본색 `w = -1 - 알파`, Surface `w = -(1 Metallic + 2 Smoothness)`, Emission `w = -1`.
  엔진은 `32. InstancedBasic.fx` 의 `BatchTech` (`VertexIn_Batch` 의 `INSTCOLOR` · `INSTSURFACE` · `INSTEMISSION` — WORLD 뒤 location 8 · 9 · 10),
  Shader Graph 는 배치 정점 셰이더가 같은 입력을 받아 `SG_InstanceProps` 가 그래프 속성 (`static gSG_…`, 재질 값은 상수 버퍼 `gSGm_…`) 을 채운다. 그림자 · 깊이 (엔진) 는 앞 64 바이트만.
- **패키지 · Shader Graph 재질의 `SetColor` · `GetColor` · `SetFloat` · `GetFloat`**: 셰이더에 같은 이름의 속성이 있으면 그 속성 (Unity 와 같음 — 예전엔 그래프의 `_BaseColor` 를
  바꾸려 해도 엔진 BaseColor 가 바뀌어 그래프에 보이지 않았다). 없으면 엔진 값 (lilToon 은 기본색 · 발광을 엔진 값으로 읽는다).

검사 `run_tests.ps1 -Only material` (`materialgl` · `materialvk` = OpenGL · Vulkan): 사본 · 공유 재질 색, 블록 (상자 24 개에 두 값 — 묶음이 늘지 않음,
`_BumpScale` 가 든 블록은 파생 재질로 1 개 늘어남), `_EmissionColor` · `_Metallic` · `_Smoothness` (묶음 그대로, 파생 재질로 그린 그림과 픽셀이 같음),
상자 100 개 · 색 100 가지 (묶음 그대로, GPU 오클루전 경로와 CPU 경로가 같은 그림), Shader Graph `_BaseColor` (묶음 그대로, SetColor · GetColor 가 그래프 속성),
블록 지우기, `GetComponent<Renderer>` · `bounds` · `shadowCastingMode` · `enabled`, 캐릭터 (Skinned) 블록 색 · `enabled`, SpriteRenderer (재질 없음 · 그림 상자), 저장 뒤 다시 열면 사본이 없어짐.

| 파일 | 하는 일 |
|---|---|
| `ScriptCore/Engine/Material.cs` | `Material` · `MaterialPropertyBlock` · `Shader.PropertyToID` (DllImport `NovaMat_*` · `NovaRenderer_*`) |
| `ScriptCore/Engine/Renderer.cs` | `Renderer` 기본 클래스 · `Bounds` · `ShadowCastingMode` |
| `Source/Scene/MaterialScripting.cpp` | 네이티브 (재질 핸들 = 주소, 스크립트에 건넨 재질은 잡아 둔다). 렌더러 함수의 `kind` = 0 Mesh · 1 Skinned · 2 Sprite |
| `Source/Scene/MaterialBlock.*` | 블록 · 파생 재질 (Mesh · Skinned 공용) |
| `Source/Graphics/DX11/UMaterial.*` | 이름으로 값 읽기 · 쓰기, `CloneInstance`, `StateHash` |
| `Source/Scene/MeshRenderer.*` · `SkinnedMeshRenderer.*` · `MeshBatcher.cpp` | `SetMaterialAt`, 그릴 재질 (`GetBatchMaterials` · `GetRenderMaterials` · `RenderMaterials`), 인스턴스 값 (`Instance` 80 바이트), Skinned 는 `enabled` 를 그리기 함수에서 확인 |
| `Shaders/32. InstancedBasic.fx` · `57. OcclusionCulling.fx` · `Source/Graphics/DX11/Vertex.cpp` · `OcclusionCulling.*` | `BatchTech` 의 인스턴스 값, Compact 가 112 바이트씩, 입력 배치 `INSTCOLOR` · `INSTSURFACE` · `INSTEMISSION` (`OcclusionCulling::InstanceBytes`) |
| `Source/ShaderGraph/ShaderGraph.*` · `ShaderGraphRuntime.*` | 그래프의 인스턴스 속성 (`InstanceSlotOf` · `SG_InstanceProps` · `InstanceSlotFor`) |
