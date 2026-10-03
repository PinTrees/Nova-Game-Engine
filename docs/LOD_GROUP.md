# LOD Group

Unity 의 **LOD Group** 과 같은 이름 · 필드 · 규칙입니다. 카메라에서 본 화면 크기로 LOD 0, 1, 2 … 의 렌더러 중 하나만 그려, 멀리 있는 물체를 단순한 메시로 바꾸거나 아예 그리지 않습니다 (Culled).

![LOD Group](../Showcase/187_LOD_Group_거리별_LOD_크로스페이드.webp)

## 쓰는 법

1. 물체의 부모 (또는 아무 GameObject) 에 **Add Component > Rendering > LOD Group** — 기본 LOD 0 · 1 · 2 = 60 · 30 · 10 %, 그 아래 Culled (Unity 와 같음)
2. LOD 막대의 칸을 누르고 그 LOD 에 그릴 물체를 **Renderers** 의 Add 칸에 끌어 놓거나, Hierarchy 에서 막대의 칸에 바로 끌어 놓습니다 (그 GameObject 와 자식 중 렌더러가 있는 것). 넣으면 범위 (Object Size) 를 다시 잽니다
3. 막대의 경계를 끌어 전환 높이를 바꿉니다. 오른쪽 클릭 = **Insert Before** · **Delete** (최대 8 개)
4. 막대 위 삼각형 = 지금 Scene 뷰 카메라에서의 화면 높이

## Inspector

| 항목 | 뜻 |
|------|-----|
| Fade Mode | **None** = 바로 바뀜, **Cross Fade** = 두 LOD 를 화면 디더로 섞어 바꿈 |
| Animate Cross-fading | (Cross Fade) 경계를 넘는 순간부터 0.5 초 동안 섞음 (`LODGroup.crossFadeAnimationDuration`) — 끄면 Fade Transition Width 구간에서 거리에 따라 섞음 |
| Object Size | 화면 높이를 잴 때 쓰는 크기 (로컬, 가장 큰 축 배율을 곱함). Recalculate Bounds 가 렌더러 상자의 가장 긴 변으로 |
| LOD 막대 | LOD i 의 Screen Relative Transition Height (%) — 화면 높이가 이 값 이상이면 그 LOD (앞에서부터 처음 맞는 것), 마지막 LOD 보다 작으면 Culled |
| Screen Relative Height (%) | 고른 LOD 의 전환 높이 (막대와 같은 값) |
| Fade Transition Width | (Cross Fade, 애니메이션 아님) 그 LOD 범위의 아래 끝에서 다음 LOD 와 섞는 비율 (0 ~ 1) |
| Renderers | 그 LOD 에 그릴 GameObject (그 GameObject 의 Mesh Renderer · Skinned Mesh Renderer). 한 렌더러가 여러 LOD 에 들어도 됨 |
| Recalculate Bounds | 모든 LOD 렌더러를 감싸는 상자로 기준점 (Local Reference Point) · Object Size |

LOD Group 을 끄면 모든 LOD 를 그립니다 (Unity 와 같음).

## 동작 (엔진 안)

- **화면 높이** = Object Size × 가장 큰 축 배율 / (2 · 거리 · tan(FOV / 2)), 거리 = 카메라 → 기준점 (월드). 직교 카메라는 크기 / (2 · Orthographic Size). Unity 의 식과 같고 LOD Bias 는 1
- **뷰마다 따로**: Game 뷰는 그 카메라, Scene 뷰는 Scene 카메라, Reflection Probe · APV 찍기는 찍는 카메라로 (찍기는 크로스페이드 애니메이션 상태를 건드리지 않고 바로). 그 뷰의 첫 그리기 전에 한 번 고르고 그림자 · 깊이 · 본 · 투명 패스가 모두 같은 값을 따른다 (`SceneCulling::IsVisible` 이 LOD 숨김도 본다)
- **그림자**: 더 많이 보이는 LOD 하나만 그림자를 드리운다 (Culled 면 그림자도 없음)
- **크로스페이드**: 섞는 중인 렌더러만 묶음에서 빼서 하나씩 그리며 `gLodFade` (문턱 · 방향) 로 화면 픽셀 고정 무늬 디더 — 한 쪽은 무늬 < t 인 픽셀, 다른 쪽은 ≥ t 인 픽셀이라 합치면 빈틈이 없다. 깊이 프리패스 (`28. SsaoNormalDepth.fx`) 와 본 패스 (`32. InstancedBasic.fx`) 가 같은 무늬라 EQUAL 깊이 검사가 맞는다. 섞지 않는 렌더러는 예전처럼 인스턴싱 묶음 그대로 (비용 변화 없음)
- Skinned Mesh Renderer 는 디더 없이 더 많이 보이는 쪽으로 바뀐다. Shader Graph · 패키지 셰이더는 본 패스 디더가 없지만 깊이 프리패스가 디더하므로 EQUAL 로 같은 무늬가 된다 (Alpha Clipping 이 있는 사용자 셰이더는 섞는 동안 둘 다 보임)
- DirectX 11 · OpenGL 같은 결과
- 파일: `Source/Scene/LODGroup.*` (컴포넌트 · 고르기 · Inspector 막대 · CLI), `Source/Scene/Component.h` (`LodStamp` · `LodHidden` · `LodShadowHidden` · `LodFade`), `Source/Scene/SceneCulling.h` (보임 검사), `Source/Scene/MeshBatcher.*` (뷰마다 고르기 · 섞는 렌더러 따로), `Shaders/32. InstancedBasic.fx` · `Shaders/28. SsaoNormalDepth.fx` (`LodFadeClip`)

## CLI

| 명령 | 뜻 |
|------|-----|
| `nova lod info` | LOD Group 목록: LOD (높이 · Fade Transition Width · 렌더러 이름), Object Size, Game · Scene 뷰가 고른 LOD (-1 = Culled) 와 화면 높이 |
| `nova lod assign --name G --lod 1 --object O` | O (와 자식 렌더러) 를 LOD 1 에 넣고 범위 다시 |
| `nova lod set --name G [--fadeMode 0\|1] [--animate true] [--lod N --height 0.3 --fadeWidth 0.5]` | 값만 바꿈 (렌더러 그대로) |
| `nova lod recalc --name G` | Recalculate Bounds |

## 검사

`Tools/tests/run_tests.ps1 -Only lodgroup` — 같은 자리의 빨강 (LOD 0) · 초록 (LOD 1) · 파랑 (LOD 2) 구:

1. Add Component = Unity 기본 (60 / 30 / 10 %), 렌더러 넣기 → Object Size 1
2. 거리 1.2 · 2 · 5 · 12 m → LOD 0 · 1 · 2 · Culled, 그 LOD 만 보임
3. Cross Fade (Fade Transition Width 1, 화면 높이 80 %): 빨강 50 % · 초록 50 %, 빈 픽셀 0
4. Animate Cross-fading: 넘는 순간 둘 다, 0.9 초 뒤 LOD 1 만
5. Game 뷰는 Main Camera (5 m → LOD 2), Scene 뷰는 제 카메라 (LOD 0)
6. 끈 LOD Group = 모든 LOD (Culled 거리에서도 보임)
7. 저장 → 다시 열기: LOD · 렌더러 참조 그대로

## 모델의 LOD 노드 (자동)

Project 창에서 모델 (FBX · GLB · glTF) 을 Hierarchy · Scene 뷰에 끌어 놓을 때 노드 이름이 `이름_LOD0` · `이름_LOD1` … 인 형제가 **둘 이상** 있으면 Unity 처럼 그 부모에 LOD Group 을 만들어 LOD 마다 넣습니다 (LOD 0 = 60 %, 다음 LOD 는 반씩, 마지막 LOD 는 1 % 까지). LOD 노드가 없는 모델은 그냥 Mesh Renderer 입니다 — LOD Group 은 사용자가 추가합니다. 자세히: [MODEL_PLACEMENT.md](MODEL_PLACEMENT.md)

## 아직

- LOD Bias (Quality 설정), C# `LODGroup` API (`SetLODs` · `ForceLOD`)
