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
| 재질 · `MaterialPropertyBlock` | 있음 (같은 값 = 한 인스턴싱 묶음) | 있음 (원래 낱개로 그린다) | 없음 — `material` · `sharedMaterial` 은 `null`, `materials` 는 빈 배열, `SetPropertyBlock` 은 무시 (색은 `SpriteRenderer.color`) |
| `bounds` | 컬링이 마지막 프레임에 잰 상자 | 같음 — 애니메이션 여유 (기본 자세 상자의 60 % + 0.25 m) 를 더해 Unity 보다 크다 | 그림 사각형 × 월드 행렬 (바로) |
| `shadowCastingMode` | 바꿀 수 있다 | 바꿀 수 있다 | 늘 `Off` (바꿔도 무시) |

같은 프레임에 만든 Mesh · Skinned 렌더러의 `bounds` 는 다음 프레임부터 맞다 (그 전엔 위치에 크기 0).

## MaterialPropertyBlock 과 인스턴싱

NOVA 의 인스턴싱 버퍼는 월드 행렬만 담는다 (셰이더 · GPU 오클루전 컬링이 그 배치를 쓴다). 그래서 블록은 렌더러마다 값을 버퍼에 넣지 않고,
**(공유 재질, 재질 값, 블록 값) 마다 파생 재질 하나** 를 만들어 같은 값의 렌더러가 같이 쓴다 — 같은 값이면 한 묶음으로 그려진다
(값이 렌더러마다 다르면 그만큼 묶음이 나뉜다). 공유 재질 값이 바뀌면 파생 재질도 다시 만든다 (`UMaterial::StateHash`).

검사 `run_tests.ps1 -Only material`: 사본 · 공유 재질 색, 블록 (상자 24 개에 두 값 — 묶음 2 개만 늘어남), 블록 지우기, `GetComponent<Renderer>` · `bounds` · `shadowCastingMode` · `enabled`, 캐릭터 (Skinned) 블록 색 · `enabled`, SpriteRenderer (재질 없음 · 그림 상자), 저장 뒤 다시 열면 사본이 없어짐.

| 파일 | 하는 일 |
|---|---|
| `ScriptCore/Engine/Material.cs` | `Material` · `MaterialPropertyBlock` · `Shader.PropertyToID` (DllImport `NovaMat_*` · `NovaRenderer_*`) |
| `ScriptCore/Engine/Renderer.cs` | `Renderer` 기본 클래스 · `Bounds` · `ShadowCastingMode` |
| `Source/Scene/MaterialScripting.cpp` | 네이티브 (재질 핸들 = 주소, 스크립트에 건넨 재질은 잡아 둔다). 렌더러 함수의 `kind` = 0 Mesh · 1 Skinned · 2 Sprite |
| `Source/Scene/MaterialBlock.*` | 블록 · 파생 재질 (Mesh · Skinned 공용) |
| `Source/Graphics/DX11/UMaterial.*` | 이름으로 값 읽기 · 쓰기, `CloneInstance`, `StateHash` |
| `Source/Scene/MeshRenderer.*` · `SkinnedMeshRenderer.*` · `MeshBatcher.cpp` | `SetMaterialAt`, 그릴 재질 (`GetRenderMaterials` · `RenderMaterials`), Skinned 는 `enabled` 를 그리기 함수에서 확인 |
