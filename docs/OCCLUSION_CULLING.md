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
2. **Hi-Z** — 깊이 버퍼를 compute 로 R32 밉 사슬에. 0 번 밉부터 반 해상도 (칸 = 깊이 2x2 — 전체 해상도 복사 단계 없음),
   밉마다 아래 2x2 의 가장 먼 깊이 (D3D 처럼 내림 크기 — 홀수면 마지막 칸이 남는 줄까지 3 칸).
3. **가려짐 검사** — 렌더러 월드 상자의 8 모서리를 화면에 투영해, 칸이 사각형의 1/4 ~ 1/2 인 밉에서 덮는 칸 (많아야 5x5) 의 가장 먼 깊이보다
   상자의 가장 가까운 깊이가 앞이면 보임 (칸 = 사각형 크기로 고르면 2x2 만 읽지만 영역이 4 배까지 넓어져 벽 위 하늘까지 들어간다). 카메라 앞면을 넘는 상자는 늘 보임. 결과는 뷰마다 렌더러 자리 (SceneCulling 번호) 에 프레임 번호로 남긴다.
4. **깊이 프리패스 2 단계** — 이번에 새로 보인 렌더러의 깊이.
5. **본 패스** — 지금 보이는 렌더러만.

- 고른 인스턴스는 compute 가 월드 행렬을 묶음마다 이어 써서 `DrawIndexedInstancedIndirect` 로 그린다 → **CPU 가 GPU 결과를 기다리지 않는다**.
- 1 단계에 그리지 않은 것도 같은 프레임의 2 단계에서 그리므로 **한 프레임 늦은 구멍 · 튐이 없다** (카메라를 확 돌려도, 물체를 옮겨도).
- 셰이더는 그대로 (엔진 Lit · Shader Graph · 패키지 셰이더) — 인스턴스 정점 버퍼 (월드 행렬) 만 GPU 가 채운 것으로 바뀐다.
- **Skinned Mesh Renderer** (인스턴싱이 아니라 낱개): 깊이 프리패스가 끝난 뒤 렌더러 상자를 `D3D11_QUERY_OCCLUSION_PREDICATE` 로 그려 두고
  (색 · 깊이 안 씀, 정점 셰이더만), 본 패스의 그리기를 `SetPredication` 으로 감싼다 — 가려졌으면 GPU 가 건너뛴다 (CPU 는 결과를 기다리지 않는다,
  같은 프레임 깊이라 늦은 구멍 없음). 카메라가 상자 안이거나 가까운 면이 상자를 자르면 쿼리하지 않고 그린다.
  상자는 렌더러의 Bounds (메시 노드 · 단위 변환 반영) — 예전에는 FBX 정점 (cm) 그대로라 100 배 큰 상자였다 (절두체 컬링도 안 되던 것을 함께 고침)
- **나무** (Tree · 지형에 칠한 나무 — LOD 마다 CPU 가 만든 인스턴싱 목록): 본 패스에서 목록을 그대로 올리고 compute 가 나무마다 경계 구로
  같은 Hi-Z 검사 → 보이는 것만 이어 쓰고 줄기 · 잎 (또는 임포스터) 의 간접 그리기 인자를 센다 (`OcclusionCulling::CullList`)
- **그림자 캐스터** (방향광, 매 프레임 다시 그리는 캐스케이드 — 몇 프레임 캐시하는 먼 캐스케이드는 카메라에 따라 빼면 안 되어 그대로):
  그림자를 깊이 프리패스 뒤에 그린다 (그다음 카메라 컬링을 다시). 빛의 절두체 안 캐스터의 상자를 빛 방향으로 캐스케이드 구 지름만큼 쓸어 늘리면
  그 그림자가 떨어질 수 있는 곳 전부 — 그 상자가 카메라 Hi-Z 에 모두 가려졌으면 보이는 표면에 그림자가 닿지 않으므로 뺀다 (`OcclusionCulling::BeginShadow`)
- LOD 크로스페이드 중인 렌더러 · 투명 재질 · 프로브 찍기는 CPU 목록 (예전과 같음).
- **DirectX 11** (feature level 11) · **OpenGL 4.5** · **Vulkan 1.3** (PC). 같은 코드가 Gfx 층 (`GfxContext` 의 `SupportsGpuDriven` · `DrawIndexedInstancedIndirect` ·
  `SetPredication` · `ClearUnorderedAccessViewUint`) 과 `fx` 효과로 돌아간다.
  - GL: 커널을 ShaderCross 가 GLSL compute 로, 버퍼 SRV · UAV = SSBO, 텍스처 UAV = image, 예측 쿼리 = `GL_ANY_SAMPLES_PASSED` + `glBeginConditionalRender`, event query = `glFenceSync`
  - Vulkan: SPIR-V compute, 스토리지 버퍼 · 이미지, `vkCmdDrawIndexedIndirect` (`drawIndirectFirstInstance`), 예측 = 쿼리 결과를 복사한 값 + `VK_EXT_conditional_rendering`
    (확장이 없으면 캐릭터만 예측 없이) — 자세히 [VULKAN_BACKEND.md](VULKAN_BACKEND.md)
  - 안드로이드 (GLES) 는 아직 절두체 컬링만 (예전과 같음).
- 비용을 아끼는 규칙: 후보 렌더러가 64 개 미만인 뷰는 쓰지 않는다 (Hi-Z 비용 > 아낄 것). 검사에서 거의 가리지 않았으면 (max(4, 2 %) 미만)
  30 번 쉬고 다시 본다 (탁 트인 장면). 통계 읽기는 event query 를 `DONOTFLUSH` 로 먼저 물어 — `Map` 이 명령을 밀어 넣어 GPU 가 쉬게 만들지 않는다.

## 측정할 때 (GPU 타임스탬프)

CPU 가 늦은 프레임 (예: Debug 빌드 + 오브젝트 2000 개 — CPU 13 ms, GPU 4 ms) 에서는 GPU 가 명령을 기다리며 쉬는 시간이
드라이버가 명령을 보낸 자리의 구간 (Depth Prepass · Hi-Z · Opaque …) 에 붙어 보인다. 예전의 "깊이 프리패스 약 5 ms" 가 그것이었다 —
실제 깊이 그리기는 구 2000 개에 0.9 ms. 구간별 GPU 시간은 GPU 가 바쁠 때만 믿고, 비교는 `nova perf --gpu-depth 4` 의 CPU 구간과 함께 본다.

| 파일 | 하는 일 |
|---|---|
| `Shaders/57. OcclusionCulling.fx` | compute 기법 6 개 (Prepare · Compact · Reduce · Cull · CullList · ShadowCull) + 상자 쿼리 기법 (Box — 정점 셰이더만). DX11 = Effects11, GL = ShaderCross |
| `Source/Graphics/DX11/OcclusionCulling.*` | 버퍼 · Hi-Z · 뷰 (카메라) 마다 기록, 간접 그리기, 몇 프레임 뒤 통계 읽기 (기다리지 않음), CLI `nova occlusion` — D3D11 직접 호출 없이 Gfx 층만 |
| `Source/Graphics/OpenGL/GfxGL.cpp` · `GLRhi.cpp` | 버퍼 SRV · UAV → SSBO, 텍스처 UAV → image, 간접 그리기, 조건부 렌더링, fence |
| `Source/Graphics/Vulkan/GfxVk*.cpp` · `VkRhi.cpp` | 스토리지 버퍼 · 이미지 디스크립터, compute 파이프라인 · 디스패치, 간접 그리기, 오클루전 쿼리 칸 고리, 조건부 렌더링 |
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
7. 벽 뒤 캐릭터 2 (Skinned — 쿼리 3 중 2 가려짐) · 나무 2 (인스턴스 2 중 2 가려짐), 같은 화면
8. 성능: 벽 뒤 구 2000 개 — 삼각형 (PIPELINE_STATISTICS, `nova perf` 의 `gpuPasses[].primitives` · `pixels`) 이 깊이 프리패스 · 불투명에서 2,209k → 1k 아래,
   그림자 캐스터 198/256 빠짐 (그림자 삼각형 2,911k → 2,770k — 구 대부분은 캐시하는 먼 캐스케이드) — Scene 뷰 GPU 시간 (단계별 깊이 프리패스 · 불투명 · 그림자)

`run_tests.ps1 -Only occlusiongl,occlusionvk` — 같은 장면을 OpenGL · Vulkan 으로: 상자 100/105 · 캐릭터 1/2 · 나무 인스턴스 · 그림자 캐스터 22/35 가려짐,
끈 화면과 같다, 벽 위에서 본 첫 프레임 · 옮긴 상자 (Vulkan 은 검증 레이어 켠 채 오류 0).

```
nova occlusion info                 # supported, enabled, game / scene: active, tested, visible, culled
nova occlusion set --enabled false
```
