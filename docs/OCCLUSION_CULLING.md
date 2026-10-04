# 오클루전 컬링 (Occlusion Culling)

벽 · 건물 · 지형 뒤에 가려진 Mesh Renderer 를 그리지 않는다. Unity 는 기본으로 Umbra 로 장면을 미리 굽지만 (Window > Rendering > Occlusion Culling > Bake),
NOVA 는 **굽기 없이 매 프레임 GPU 에서** 계산한다 — 사전 작업이 없고, 움직이는 물체도 가리고 가려진다.

## 켜고 끄기

| 곳 | 값 |
|---|---|
| Camera > **Occlusion Culling** (Unity 와 같은 이름 · 같은 저장 값 `occlusionCulling`, 새 카메라는 켬) | Game 뷰 · 빌드한 게임 |
| Scene 뷰 | 늘 켬 |
| `nova occlusion set --enabled false` · 환경 변수 `NOVA_DEV_NOOCCLUSION=1` | 비교 측정용 (전체 끔) |

Game 뷰 Stats 창에 `Occlusion culling: 가려진 수 / 검사한 수`, Profiler 의 `Game View/Occlusion Culled` · `Tested` (Scene 뷰도).

## 방식 — 두 단계 Hi-Z (GPU 가 고른 인스턴스로 간접 그리기)

MeshBatcher 가 보이는 Mesh Renderer 를 (메시, 서브셋, 재질) 묶음으로 인스턴싱해 그리던 길에 끼운다.

1. **깊이 프리패스 1 단계** — 지난 프레임에 보였던 렌더러만 그린다. 이어서 스킨 메시 · 지형 · 나무 · 바위 · 디테일도 깊이에 그려져 가리는 물체가 된다.
2. **Hi-Z** — 깊이 버퍼를 compute 로 R32 밉 사슬에 (밉마다 아래 2x2 의 가장 먼 깊이, 홀수 크기의 마지막 칸은 3 칸까지).
3. **가려짐 검사** — 렌더러 월드 상자의 8 모서리를 화면에 투영해, 사각형이 한 칸 이하가 되는 밉에서 덮는 2x2 칸의 가장 먼 깊이보다
   상자의 가장 가까운 깊이가 앞이면 보임. 카메라 앞면을 넘는 상자는 늘 보임. 결과는 뷰마다 렌더러 자리 (SceneCulling 번호) 에 프레임 번호로 남긴다.
4. **깊이 프리패스 2 단계** — 이번에 새로 보인 렌더러의 깊이.
5. **본 패스** — 지금 보이는 렌더러만.

- 고른 인스턴스는 compute 가 월드 행렬을 묶음마다 이어 써서 `DrawIndexedInstancedIndirect` 로 그린다 → **CPU 가 GPU 결과를 기다리지 않는다**.
- 1 단계에 그리지 않은 것도 같은 프레임의 2 단계에서 그리므로 **한 프레임 늦은 구멍 · 튐이 없다** (카메라를 확 돌려도, 물체를 옮겨도).
- 셰이더는 그대로 (엔진 Lit · Shader Graph · 패키지 셰이더) — 인스턴스 정점 버퍼 (월드 행렬) 만 GPU 가 채운 것으로 바뀐다.
- 그림자 맵은 빛에서 본 것이라 그대로 (빛의 절두체 컬링). LOD 크로스페이드 중인 렌더러 · 투명 재질 · 프로브 찍기는 CPU 목록 (예전과 같음).
- DirectX 11 (feature level 11) 만. OpenGL · Vulkan · 안드로이드는 절두체 컬링만 (예전과 같음).

| 파일 | 하는 일 |
|---|---|
| `Shaders/57. OcclusionCulling.hlsl` | compute 커널 5 개 (Prepare · Compact · CopyDepth · Reduce · Cull) — `fx` 효과가 아니라 `cs_5_0` 로 따로 컴파일 (GLES 변환 대상 아님) |
| `Source/Graphics/DX11/OcclusionCulling.*` | 버퍼 · Hi-Z · 뷰 (카메라) 마다 기록, 간접 그리기, 몇 프레임 뒤 통계 읽기 (기다리지 않음), CLI `nova occlusion` |
| `Source/Scene/MeshBatcher.*` | 후보 목록 → GPU, 1 단계 · `FinishDepthPrepass` · 본 패스, CPU 목록과 같은 묶음 · 재질 순서 |
| `Source/Editor/EditorApp.cpp` | Game · Scene 뷰의 깊이 프리패스 뒤 `FinishDepthPrepass` |

## 검사 (`run_tests.ps1 -Only occlusion`)

큰 벽 (24 x 5 m) 뒤 상자 100 개 + 벽 앞 상자 3 개:

1. DirectX 11 에서 켜짐 (Scene 뷰)
2. 벽 뒤 100 개 가려짐 (검사 105, 보임 5 — 벽 · 바닥 · 앞 상자)
3. 끈 화면과 픽셀이 같다 (구멍 · 빠진 물체 없음)
4. 벽 위에서 내려다본 **첫 프레임** 에 상자가 보이고 끈 화면과 같다 (벽 바로 뒤 줄만 계속 가려짐)
5. 벽 뒤 상자를 앞으로 옮기면 다음 프레임에 그려진다
6. Game 뷰: Camera 의 Occlusion Culling 켬 = 99 개 가려짐, 끔 = 검사 안 함, 같은 화면
7. 성능: 벽 뒤 구 2000 개 — Scene 뷰 GPU 시간 (단계별 깊이 프리패스 · 불투명 · 그림자)

```
nova occlusion info                 # supported, enabled, game / scene: active, tested, visible, culled
nova occlusion set --enabled false
```
