# NOVA Claude 작업 상태

- 갱신 시각: 2026년 10월 6일 — **지형 본 패스 비용 줄이기** (사용자 지시: "지형 본 패스 비용 줄이기 … 다음 작업은 유니티 컴포넌트 중 중요도 순으로 하나씩"). **완료 (커밋 · 푸시함)**
  - Release (ClaudePerfEngine, 1660 SUPER, 가까운 돌 지형): 테셀레이션 켬 2.16 → **1.72 ms** (본 패스 1.24 → 0.83), 끔 (POM) 1.85 → 1.77
  - 61 `TerrainLayerHN` noTile (위 투영 타일 없애기 3 표본은 30 m 안만), 3 % 미만 레이어는 높이 · 노멀을 읽지 않음, POM 걸음 = 높이 범위의 화면 픽셀 / 2 (2 ~ 10), 32 지형 250 m 너머 높이 · 노멀 건너뜀, `TerrainRenderer.cpp` kTessTriangleSize 14
  - 남은 것: 먼 시점에서 테셀레이션 켬이 끔보다 0.9 ms 더 (원인 못 찾음 — 화면 공간 나눔 실험은 그림자가 나빠져 되돌림)
  - 검사: tessellation · tessellationgl · tessellationvk **37/37**. 문서 TESSELLATION.md 성능 표
  - 다음 (진행 중, 커밋 전): Unity 컴포넌트 1 순위 **2D Tilemap** — 새 패키지 `Packages/com.nova.tilemap` (Grid · Tilemap · Tilemap Renderer · Tilemap Collider 2D · .tile · Tile Palette · CLI `nova tilemap` · C#),
    엔진 쪽 `EditorExtensions` (RegisterSceneTool · RegisterCreateMenu), `SpriteRenderer::ResolveSprite`, `UISprites` NOVA_API, C# Vector2Int · Vector3Int · BoundsInt, 스위트 `tilemap`
- 이전: 2026년 10월 6일 — **테셀레이션 Release 성능 · 절벽 POM · Terrain Layer Normal Map** (사용자 지시: "절벽 POM 과 레이어 Normal Map · 테셀레이션 Release 성능 측정과 최적화"). **완료 (커밋 · 푸시함)**
  - 성능 (Release, ClaudePerfEngine, MuMu 끔, 번갈아 3 번 중앙값, 1660 SUPER Scene 뷰 1904x1001, 돌 지형): 테셀레이션 켬 2.40 → **1.72 ms** — 화면 밖 패치를 Hull 에서 버림 (60 `TessPatchOutside` · `gTessCull`, 본 · 깊이만),
    방향광 캐스케이드 1 ~ 3 은 나누지 않음 (`ShadowRenderer::CasterPass::Cascade` · `TessellateShadow()`). Normal Map 을 더한 뒤 2.16 ms (그림자 0.38 · 깊이 0.16 · 본 1.24), 끔 (POM) 1.85, 높이 없음 0.92
  - Normal Map: `TerrainLayer` NormalPath · NormalScale (normalMap · normalScale), 편집기 칸 + Fix Now. 배열을 R10G10B10A2 로 (R 높이, GB 노멀 xy) — 샘플러를 늘리지 않는다. 62 blit 이 두 원본. 61 `TerrainLayerHN` (삼평면 화이트아웃)
  - 절벽 POM: 61 `TerrainParallax` 를 3D 로 (가장 큰 투영, 섞이는 비탈은 줄임). CLI `nova terrain-height` (원 안 높이 — 절벽 검사)
  - 검사: tessellation 스위트에 Normal Map · 절벽 POM 추가. 문서 TESSELLATION.md 성능 표 · 절벽, NOVA_CLI. Showcase 242
- 이전: 2026년 10월 6일 — **지형 높이 3 차: 높이 배열 · 높이 기반 섞기 · 지형 POM · 픽셀 범프** (사용자 지시: "높이 기반 레이어 섞기 · 지형 POM 과 먼 거리 높이 음영"). **완료 (커밋 · 푸시함)**
  - 새 `Shaders/62. TerrainHeightBlit.fx` + `TerrainRenderer.cpp` HeightArrayFor (레이어 높이 맵 넷 → Texture2DArray R16F 1024² 밉, 키가 바뀔 때만 GPU 로 모음 · 상태 되돌림)
  - `61. TerrainTessellation.fx` 다시: gTerrainHeights (배열 — 샘플러 하나), TerrainHeightBlend (HDRP 식, 색 · 변위 · 범프 같은 가중치), TerrainNoTileHeight (레벨 / 미분), TerrainParallax · Weight (위 투영 POM, 40 ~ 100 m · 테셀레이션 없으면 0 ~ 100 m),
    TerrainBumpNormal (모든 거리, 250 m 까지). Domain 법선 (TerrainDisplacedNormal) 은 뺌. 40 `TerrainControlWeights` · `TerrainAlbedoW`, 32 `TerrainShade(pin, tess)`
  - `Terrain` 컴포넌트 Height-Based Blend · Height Transition (기본 끔 — HDRP 와 같게, heightBasedBlend · heightTransition), TerrainEditor UI, CLI `terrain-layer --fill --soft`
  - **OpenGL 샘플러 32 개**: 지형 PS 가 정확히 32 개였다 (배열 하나로 33 → C7612, 지형이 안 그려짐). `ShaderCross.cpp` MergeNoSamplerCombos — 같은 텍스처의 빈 샘플러 결합 (`<tex>_nosampler`, 크기 · 밉 수 조회 · texelFetch) 을
    같은 종류의 다른 결합으로 바꿔 하나 아낀다 (하늘 큐브 맵의 밉 수 조회 — 모든 Lit PS). kCacheVersion 8
  - 검사: tessellation 스위트에 높이 기반 섞기 · 테셀레이션 없는 지형 POM 추가 — DX 11/11 · GL 10/10 · Vulkan 10/10, 회귀 render · gfx · materialgl · vfxgl · occlusiongl · shadergraph · weather · decal · lodgroup · vulkan · material · occlusion · cli · tessellation(vk) **158/158**
- 이전: 2026년 10월 6일 — **테셀레이션 2 차: 지형 레이어 높이 · POM · Shader Graph Displacement · 높이 맵 Fix Now** (사용자 지시: 추천 목록 "지형 레이어의 높이 변위 · POM 섞어 쓰기 · Shader Graph 에 테셀레이션 연결 · 높이 맵 가져오기 설정 진행해줘"). **완료 (커밋 7119936, 푸시함)**
  - 높이 맵: `ImportSettingsInspector::MarkAsHeightMap` (sRGB 끔 + High Quality BC7) · `IsLinearHeightMap`, 재질 · Terrain Layer Inspector 의 Fix Now. `make_tess_textures.py` 가 높이 맵 .meta (선형) 도
  - 지형: 새 `Shaders/61. TerrainTessellation.fx` (cbTerrainHeight · gTerrainHeight0~3, 색과 같은 타일 없애기 무늬의 밉 지정판 TerrainNoTileLevel, 삼평면, precise, TerrainDisplacedNormal — 법선은 Domain 에서),
    32 `TerrainTessTech` (레이어 + 눈, TerrainShade 로 나눔) · 28 `TerrainTessNormalDepthTech` · 26 `TerrainTessShadowTech`. `TerrainLayer` HeightPath · HeightAmplitude · HeightBase (+ 저장), `TerrainEditor` UI,
    `TerrainRenderer.cpp` (레이어 타일 · 컨트롤 맵 · 높이를 모든 패스에, 테셀레이션 기법 패스마다, **가까운 40 m 는 쿼드트리 가장 잘게** — 평평한 지형은 오차 0 이라 31 m 칸이 남아 높이가 뭉개졌다, 컬링 상자 넓힘)
    **주의**: 지형 PS 에 텍스처를 더하면 OpenGL 픽셀 단계 샘플러 32 개를 넘는다 (C7612 → 지형 전체가 안 그려짐) — 그래서 픽셀 범프 대신 Domain 법선
  - POM: 60 `TessParallaxUV` · `TessParallaxWeight`, 32 `PS_TessBatch` (나눔 거리 끝에서 이어 받기) · `PomBatchTech` (테셀레이션 없는 기기). `MeshBatcher::TessellationEnabled()` + CLI `nova tessellation info | set --enabled false`
  - Shader Graph: Graph Settings Tessellation (Factor · Triangle Size · Fade Distance), Master Vertex 블록 Displacement, 생성기 `TessellationCode` (SG_TessDisplaced precise, 옆 두 점으로 법선), Graph{Tess,TessDepth,TessShadow}BatchTech,
    `ShaderGraphRuntime` (패치 · cbTessellation), `CustomShaders::InstancedDraw` Topology · Camera*, `MeshBatcher` FillCamera · EndCustomDraw. 예제 `docs/examples/shadergraph_tessellation.txt`
  - CLI `nova terrain-layer <지형> [--add] [--set --layer] [--fill --center --radius]` (Tools/NovaCli + CliCommands)
  - 검사: tessellation · tessellationgl · tessellationvk **25/25** (POM · Shader Graph 물결 · Terrain Layer 돌 추가), 회귀 shadergraph · render · material · weather · occlusion · lodgroup · decal · vulkan · cli **101/101**. 문서 TESSELLATION.md (지형 · POM · Shader Graph), SHADER_GRAPH.md, NOVA_CLI.md, README. Showcase 238 ~ 240 (233 · 234 새로)
  - 안드로이드 기기: 여전히 MuMu VM 이 설치 뒤 죽는 문제로 확인 못 함 (아래 항목)
- 이전: 2026년 10월 6일 — **날씨 품질 보강 (캐릭터 · 물 · 지형 눈 두께) + 재질 테셀레이션** (사용자 지시: "캐릭터도 비에 젖고 어깨 · 머리에 눈 … 물 수면에 빗방울 물결과 먹구름 하늘 반사 … 지형 높이를 실제로 올려 눈 두께 … 이거 하고 그 테셀레이션도 추가해줘. 벽, 바닥 등 … 높이 가변"). **완료 (커밋 5349765, 푸시함)**
  - 새 `Shaders/60. Tessellation.fx` (cbTessellation · gHeightMap, 변마다 가운데 거리로 나눔 — 틈 없음, 높이 밉 = 정점 간격, 정점 법선 다시), 32 `TessBatchTech` (+ `PS_TessBatch` 픽셀마다 높이 기울기 법선) · 28 `TessNormalDepthBatchTech` · 26 `TessBuildShadowMapInstancingTech`
    **EQUAL 깊이**: 자리 계산은 `precise` + 덧셈 · 곱셈 매크로만 (TESS_DOT3 · TESS_CROSS — GLSL 의 distance · normalize · mix 에는 precise 가 붙지 않는다), `ShaderCross.cpp` 가 TES 에 `invariant gl_Position` (kCacheVersion 7 — GL 캐시 다시 만듦). 없으면 OpenGL 에서 검은 얼룩
  - `UMaterial` Tessellation (DisplacementMode · HeightMapPath · HeightAmplitude · HeightBase · TessellationFactor · TessellationTriangleSize · TessellationFadeDistance), `MaterialInspector` (Surface Options > Displacement Mode, Height Map + Amplitude · Base, Tessellation Options)
  - `MeshBatcher`: 테셀레이션 재질은 깊이 · 그림자 묶음도 재질마다 (예전엔 메시만 → 프리패스가 평면이었다), 패치 토폴로지 + `GfxContext::ClearTessellationShaders` (DX11 — Effects11 이 HS · DS 를 남긴다), `FxPass::IsUsable` / `Rhi::Effect::PassUsable` (GL · Vulkan 은 프로그램이 만들어졌는가 — 테셀레이션 없는 GLES 는 보통 그리기)
  - 지형 눈: 32 `TerrainSnowTech` (TerrainHS · TerrainSnowDS — 눈 덮임 × Snow Depth 만큼 올리고 발자국 자리는 땅까지, `s_SnowDisplaced` 로 시차 건너뜀), `TerrainRenderer.cpp` (눈이 있으면, 40 m · 10 픽셀). 프리패스 · 그림자는 맨 지형 (눈은 위로만 → LESS_EQUAL 이 맞는다)
  - 캐릭터: 이미 ShadeLit 으로 젖음 · 눈을 받았다 — 발자국 음영이 몸에 찍히던 것만 `WeatherAboveCover` (onGround) 로. 물 `46. Water.fx` + `WaterRenderer.cpp`: 먹구름 반사 · 물속 잿빛 · 번쩍임 · 바람 잔물결 · 빗방울 고리 (1.8 배)
  - 검사: 새 스위트 `tessellation` · `tessellationgl` · `tessellationvk` (6 · 5 · 5) — 회귀 tessellation ×3 · weather · material · occlusion · lodgroup · render · shadergraph · decal · vulkan **107/107**, 새 `Tools/tests/android_tessellation.ps1` (+ `make_tess_textures.py`, `-SkipBuild`). `common.ps1` Start-MuMuHidden: VM (headless) 이 죽었으면 다시 켠다
  - **안드로이드 기기 확인 못 함**: GLES 셰이더 내보내기 (TCS · TES · invariant) · 게임 데이터 · APK · 설치는 통과, 그 뒤 MuMu VM (MuMuVMMHeadless) 이 몇 초 안에 죽는다 (네 번, 부팅 다 끝난 뒤 · 다른 일 없이도). VBox.log 는 종료 기록 없이 끊김. 다음에 MuMu 를 살펴볼 것
  - 문서 `docs/TESSELLATION.md` (새), WEATHER.md (캐릭터 · 지형 눈 · 물, 한계), README. Showcase 233 ~ 237
- 이전: 2026년 10월 6일 — **VFX Turbulence 가 안드로이드 GLES 에서 NaN** (사용자 지시: 작업 칩 "Fix VFX Turbulence producing NaN on Android GLES" 를 여기서) + 편집기 abort 대화상자 + MuMu 창 숨기기. **완료 (커밋 27ba508, 푸시함)**
  - 원인: `58. VFX.fx` NoiseVec 의 횟수가 값에 따라 바뀌는 고리 (`for (int o = 0; o < n; ++o)`) — MuMu GLES 3.2 에서 그 이펙트의 파티클이 통째로 NaN (그려지지 않음). 진단: 변형 이펙트 여러 개를 한 장면에 두고 기기 경계 · 그림 (모드 3 · 4 = 고리 없는 Noise3 는 정상)
    고침: 4 번 정해진 고리 + `[branch] if (o < n)`. 날씨의 안드로이드 우회 (Turbulence 빼기) 지움. 잘못 짚은 것: 정수 변환 · gTime · 잡음 함수 · sSlot 배열 (배열을 0 으로 채우면 오히려 모든 이펙트가 깨졌다 — 되돌림)
    주의: `vfx new` 기본 견본에 Turbulence 가 들어 있다 (진단의 "대조군" 이 아니었다). GLES 에서 경계 상자가 null 이면 그 이펙트 전체가 망가진 것
  - 편집기 시작 abort() (사용자가 본 Debug Error): 셰이더 미리 컴파일 12 스레드 × 디버그 정보 (D3D10_SHADER_DEBUG) → 컴파일러 PDB 내부 오류 · out of memory 로 abort. 그때 커밋 여유 2.5 GB (Unity 10 GB)
    `ShaderCache.cpp`: Debug 도 디버그 정보 없이 (NOVA_SHADER_DEBUG=1 이면 넣고 한 번에 하나), 내부 오류 · 메모리 부족이면 혼자서 한 번 더. 빌드도 메모리가 모자라면 `/m:4`
  - MuMu: `common.ps1` 의 `Start-MuMuHidden` (창을 화면 밖 -32000 + 숨기기, MuMu 가 자리를 기억) — android · android_vfx · android_weather · android_occlusion · android_city_perf
  - 검사: android_vfx **14/14** (Turbulence 경계 유한 추가), android_weather 눈보라 **11/11** (Turbulence 포함, 눈송이 8.6 만), PC vfx · vfxgl · vfxvk · weather · shadergraph **94/94**
- 이전: 2026년 10월 6일 — **날씨 4 단계: 시네마틱 데모 · 안드로이드** (같은 지시). **완료 (커밋 078280f, 푸시함)**
  - 데모 프로젝트 `E:\NovaTest\WeatherDemo` (Forest.terraindata 지형, 오두막 · 돌 마당 · 처마 · 등불 · 숲 900 그루 · 걷는 사람, Volume 에 SSR · 그림자) — 스크립트는 `docs/examples/weather/` (WeatherDirector.cs · Walker.cs · build_cabin_scene.ps1)
  - 패키지: 해 = 먹구름 × 안개 (눈보라 · 폭풍에 그림자가 거의 없게), 안드로이드는 눈송이 Turbulence 빼고 처음 흔들림 크게 (아래)
  - `58. VFX.fx`: Collide with Depth · Weather Cover 가 NaN · 무한을 쓰지 않게 (맞는 쪽으로 묻기 + 쓰기 전 검사)
  - 새 검사 `Tools/tests/android_weather.ps1` (MuMu): 폭풍 **11/11** · 눈보라 **11/11** — GLES 셰이더 (59 포함), 게임 데이터의 소리, 기기 오류 없음, 입자 자리 유한, DX11 과 같은 밝기 (차이 평균 1.1 ~ 1.7), 폭풍 2.4 ms · 눈보라 4.5 ms
  - **남은 엔진 문제**: MuMu GLES 에서 VFX Turbulence 블록이 파티클 자리를 NaN 으로 깨뜨린다 (잡음 · gTime · 정수 변환이 원인 아님, 블록을 빼면 정상) — 작업 칩 "Fix VFX Turbulence producing NaN on Android GLES" 로 따로
  - 검사: weather 14/14 + vfx · vfxgl · vfxvk 56/56 (70/70), 안드로이드 위. Showcase 229 ~ 232, 문서 WEATHER.md (데모 · 안드로이드 · 한계), README 그림
- 이전: 2026년 10월 6일 — **날씨 2 · 3 단계: 젖은 세상 · 쌓이는 눈 + 발자국** (사용자 지시: "푸시하고 2 단계 … 3 단계 … 4 단계 … 진행해줘"). **완료 (커밋 c8a2f15, 푸시함)**
  - 엔진: 새 `Source/Graphics/DX11/WeatherCover.*` — 덮개 맵 (카메라 둘레 128 m 를 위에서 본 깊이, 그림자 캐스터 패스, 8 m 칸 · 30 프레임마다), 발자국 맵 (48 m, 1024², RGBA16F 두 장 번갈아 + 도장 R32F,
    아래에서 본 깊이 = `Scene::RenderSceneDeformers` (스킨 메시 + RigidBody · CharacterController 의 메시), `Scene::RenderSceneCover` (풀 · 캐릭터 뺌)), 새 셰이더 `59. WeatherSnow.fx` (StampCS · DeformCS — 고리 배치, 12 cm 비탈, 다시 덮임)
  - `32. InstancedBasic.fx`: cbWeather + `ApplyWeather` (ShadeLit 앞 — 젖음 · 웅덩이 · 빗방울 물결 (30 m 안) · 쌓인 눈 · 발자국 시차 8 걸음 · 기울기 법선), `WeatherSkyGrade` (하늘에서 온 환경광 · 반사를 먹구름처럼),
    `s_WeatherPuddles` (지형 0.45, 나무 · 바위 · 풀 0). **fxc 주의**: SnowPressAt 에 조기 return 이 있으면 fxc 가 스택 넘침 (0xC00000FD) → 분기 없이 WRAP 샘플러로
  - `58. VFX.fx` + `VfxAsset.cpp` + `VfxRuntime.cpp`: 블록 32 **Collide with Weather Cover** (빗방울이 지붕 · 나무 위에서 죽는다 — 화면 밖도). `WeatherState` 에 PuddleLevel · RainIntensity · Time · SnowDepth · SnowFall
  - 패키지: 표면 상태가 천천히 (젖음 · 웅덩이 · 눈 쌓임 · 녹음), Snow Depth, status 에 surfaceWetness · puddles · snowAmount. 안드로이드 소리: `BuildPipeline::CollectGameFiles` 가 넣은 패키지의 Resources 를 게임 데이터에
  - 검사: weather **14/14** (젖은 바닥 · 지붕 아래 마름 · 천천히 젖기 · 쌓인 눈 · 발자국 추가), 회귀 vfx · vfxgl · vfxvk · shadergraph · decal · render · material · reflectionprobe · probevolume · ssr **129/129** (2 단계 뒤), render · shadergraph · decal · material (DX · GL · Vulkan) · vfx (DX · GL · Vulkan) **135/135** (3 단계 뒤), 안드로이드 빌드
  - 데모 프로젝트 `E:\NovaTest\WeatherDemo` (WeatherDirector.cs 시간표 + 도는 카메라, Walker.cs) — 장면은 아직
- 이전: 2026년 10월 5일 — **날씨 패키지 com.nova.weather 1 단계: 비 · 눈 · 먹구름 · 안개 · 바람 · 번개 · 소리** (사용자 지시: "오픈월드 게임 기반 웨더 컨트롤러 … 비오는거 + 눈오는거 … 눈은 쌓이는거 + 밟으면 … 패키지매니저로" → 4 단계 계획에 "어 진행해줘"). **1 단계 완료 (커밋 af2346c, 푸시함)**
  - 엔진: 새 `Source/Graphics/Common/WeatherState.*` (NOVA_API 전역 — 그릴 때만 적용, 장면에 저장 안 됨, 카메라 자리 · 방향 · 프레임 기록), `EditorApp.cpp` (해 · 환경광에 적용, Game · Scene 뷰 카메라 기록),
    `Sky.cpp` + `21. Sky.fx` (gSkyWeather 채도 · 밝기, gSkyFlash), `AtmospherePass.cpp` (날씨 안개를 Volume Fog 와 섞기 — 장면에 안개가 없어도), `TreeRenderer.cpp` · `DetailRenderer.cpp` (바람 배율), `App.cpp` · `AppAndroid.cpp` (Frame),
    내보내기 `VisualEffect` · `AudioSource` · `AudioClip`, `Vfx::SetLiveJson` (패키지가 파일 없이 .vfx JSON 등록)
  - VFX 공용 고침 `58. VFX.fx` + `VfxRuntime.cpp`: 화면에서 1.4 픽셀보다 가는 파티클은 넓히고 그만큼 옅게 (gDepthParams.z = 픽셀 배율, Spark 는 보이는 심이 1/4) — 가는 빗줄기가 점선으로 끊겨 보이던 것. vfx · vfxgl · vfxvk **56/56** 그대로
  - 패키지 `Packages/com.nova.weather/`: `WeatherController.*` (프로필 전환 smoothstep, 돌풍, 장면 밖 돕는 오브젝트 — Visual Effect · 번개 LineRenderer · AudioSource, 카메라 속도만큼 앞에서 태어남, 번개 = 가운데 점 밀기 + 가지 · 깜빡임 · 천둥 지연),
    `WeatherFx.*` (Precipitation 메모리 에셋: Rain · Splash · Ripple · Far Rain · Snow, 바람 위쪽 Offset · 눈 수명 속성), `WeatherProfile.*` (기본 6 + .weather), `Package.cpp` (.weather 에셋 · CLI weather · C exports), `Runtime/Weather.cs` (C# Weather.Set …),
    소리 `Resources/Audio/*.wav` 는 `Tools/gen_weather_audio.py` 가 만든다 (합성, 9 MB). `Tools/NovaCli/main.cpp` (weather 명령)
  - 검사: `run_tests.ps1 -Only weather` **10/10** (새 스위트). 문서 `docs/WEATHER.md`, README 패키지 줄, NOVA_CLI. Showcase 227 ~ 228
  - 남은 것: 2 단계 젖음 (Wetness — 값만 넘김), 3 단계 쌓이는 눈 · 발자국 (SnowCover), 4 단계 시네마틱 데모 + 안드로이드 (안드로이드는 PackageManager::Find 가 없어 소리 경로를 못 찾는다 — 그때 고칠 것)
- 이전: 2026년 10월 5일 — **VFX Graph 남은 기능: Output Mesh · 깊이 버퍼 충돌 · Sub Graph · 사용자 속성** + 문서 그림 고침 (사용자 지시: "기능 문서의 깨진 그림 고치기 … v0.2.0 릴리스 … VFX Graph 남은 기능 … 이거진행하고 알려줘"). **완료 (푸시 · v0.2.0 게시)**
  - 문서 그림: docs/*.md 10 개가 git 밖 `Showcase/` 를 가리켜 GitHub 에서 깨짐 → 18 장을 `docs/images/` (영문 이름) 로 옮김
  - 셰이더 `58. VFX.fx`: 파티클 96 → 112 바이트 (사용자 속성 float4), Eval op 14 (Get Attribute), 블록 9 (Set Attribute) · 30 (Collide with Depth Buffer — gCollDepth 를 compute 가 Load, 뷰포트로 화소 · 이웃 깊이 법선), Set Color 를 Update 에서도,
    MeshVS/PS (메시 정점 0 번 + 파티클 인스턴스 1 번, 무작위 축 · 속도 방향 · Y 회전, 해 · 환경광), Opaque (사각형 잘라내기 · 메시) 기법
  - `VfxAsset.*` (Blend Opaque, Shape Mesh · Mesh · Lit, Attribute · AttributeLanes, BlockDesc.AnyContext · AllowedIn, OperatorInputs · NodeInput, DependencyRevision, FindAssets(ext), DefaultSubgraph),
    `VfxOperators.cpp` (Get Attribute · Sub Graph · Output (Sub Graph), 컴파일러를 겹 (Frame) 으로 — Sub Graph 안의 Property = 부른 노드의 입력), `VfxRuntime.cpp` (메시 캐시 GeometryGenerator, 첫 뷰 시뮬레이션 전에 출력 깊이를 풀고 깊이 SRV 를 compute 에, 뷰포트, 불투명은 쓰기 깊이로),
    `VfxTemplates.cpp` (Debris · Fireflies), `VfxGraphWindow.*` (Custom Attributes Blackboard · Inspector, 속성 · 파일 고르기, Sub Graph 입력 핀 · Open, Mesh · Lit, Sub Graph 파일 화면, Create > Visual Effect Subgraph Operator), `VfxCli.cpp` (attribute.* · subgraph.new · list subgraphs · stats bounds), `BuildPipeline.cpp` (.vfxoperator)
  - 검사: vfx 18/18 · vfxgl + vfxvk 26/26 (깊이 충돌 · 사용자 속성 · Sub Graph · Output Mesh 추가), `android_vfx.ps1` MuMu **12/12** (파편 메시 · 깊이 충돌 · 반딧불 추가). Showcase 222 ~ 224
  - **v0.2.0 게시함** (사용자 확인: 푸시 + 공개 릴리스, Hub 설치 파일은 v0.1.0 것을 다시) — https://github.com/PinTrees/Nova-Game-Engine/releases/tag/v0.2.0 (태그 778acb5, latest, 사이트 다운로드 링크 200): 버전 0.2.0 (EngineInfo.h · NovaCli · CMake), `dist/NOVA-Engine-0.2.0-win64.zip` 56.2 MB (SHA256 02C4D219…F38AFD, Debug CRT 의존 없음, 안드로이드 x86_64 플레이어 · Mono 포함), `dist/release_notes_0.2.0.md`
    - 전체 회귀 (Debug, -Suite full): 355/360 — model 3 개 (VRM 다시 가져오기가 긴 실행 중 늦어 그 뒤 연쇄) · shadergraph 1 개 (미리보기 로그 대기) 는 따로 다시 돌려 56/56 · 24/24, perf Trees Vulkan 은 Debug 수치 (Release 에서 통과)
    - Release 묶음 시험 (풀어서 NOVA_ENGINE): cli · vfx · vulkan · perf — 남은 것: perf OpenGL 대 DX11 비율 2 개 (Characters ×1.6, Materials ×2.1~2.4) 는 예전 Release 복사본에서도 같아 이번 작업 전부터, Culling DX = Vulkan max 39 는 텍스처 탓이 아니었다 (아래 고침)
    - 고친 것: `Tools/package_release.ps1` 의 Mono 복사 경로에 `\r` `\n` 이 실제 글자로 들어가 있던 것 (cac1b68 때 깨짐), zip 의 읽어 보기에 Vulkan, 메시 그리기 검사의 카메라를 가까이 (Release 에서 0.92 %)
    - Hub 설치 파일: v0.1.0 의 NovaHubSetup.exe (SHA256 43a0692b…) 를 그대로 다시 올림 — Hub 의 안드로이드 모듈 · Mono 변경은 다음 서명 빌드 (package_hub.ps1 + 인증서) 때
  - 처음 깊이 충돌이 듣지 않던 까닭: 깊이 텍스처가 뷰보다 커서 NDC → 화소를 텍스처 크기로 바꾸면 엉뚱한 화소 → 지금 뷰포트 (RSGetViewports) 로
- 같은 날 이어서 — **VFX Graph 끝: SDF 충돌 · 모델 파일 Output Mesh · Compare / Branch · Block Sub Graph** (사용자 지시: "이거로 일단 VFX 그래프 끝내고"). **완료 (커밋 86e91e2, 푸시함)**
  - 새 `Source/Effects/VfxMeshes.*`: 메시 이름 → CPU 사본 (엔진 기본 GeometryGenerator · 모델 파일 ResourceManager::LoadMeshFile — FBX · glTF · GLB · VRM, OBJ 는 엔진이 불러오지 않는다;
    번호 없으면 스킨 메시 모두 합침, `#n`), `BakeSdf` (가장 긴 변 N 칸 — 삼각형 둘레 정확한 거리 → 가장 가까운 점 물려받기 두 번 → 가장자리에서 칠한 바깥 + 정점 법선 부호)
  - 셰이더 `58. VFX.fx`: 블록 31 Collide with SDF (gSdf raw 버퍼, 세 방향 보간 · 기울기 법선, 다음 자리 기준), Eval op 32 Compare · 33 And · 34 Or · 44 Branch · 63 Not
  - `VfxAsset.*`: ParamDesc.NoLink, CollideSDF (행렬 = 크기 · 회전 · 자리 × World 의 역 · 정방향 열, 거리 배율), SubgraphBlock (Id 0, AnyContext — Encode 가 .vfxblock 첫 시스템의 같은 문맥 블록으로 펼친다,
    SubgraphProps 로 입력 = 파일 기본 → 바깥 값 → 바깥 속성 연결, 4 겹), EffectiveBlockDesc (입력을 값으로 — 이름은 오래 사는 표), Validate (파일 · 메시 · 시스템마다 SDF 하나)
  - `VfxOperators.cpp` (Compare · Branch · And · Or · Not, DependencyRevision 에 .vfxblock, DefaultBlockSubgraph), `VfxRuntime.cpp` (메시 = VfxMeshes, 시스템마다 SDF 굽기 · 올리기 · gSdf 묶기),
    `VfxGraphWindow.*` (EffectiveBlockDesc 로 그리기, Mesh 고르기 = 기본 + 프로젝트 모델, Path = .vfxblock / .vfxoperator, Open Sub Graph Block, Create > Visual Effect Subgraph Block), `VfxCli.cpp` (blockgraph.new · list)
  - 검사: vfx · vfxgl · vfxvk **56/56** (Compare + Branch, glTF 로 구운 SDF 에 멈춤, 모델 Output Mesh, Block Sub Graph 추가 — 검사가 2 x 1 x 2 glTF 상자를 base64 로 쓴다), `android_vfx.ps1` MuMu **13/13** (SDF + Compare/Branch 추가). Showcase 225 ~ 226
- 같은 날 이어서 — **DX = Vulkan 비교 검사 고침** (사용자 지시: 작업 칩 "Fix missing-texture fallback differing DX vs Vulkan" 을 여기서). **완료 (커밋 7aee658, 푸시함)**
  - 칩의 가정 (묶음에 없는 텍스처의 대체 색) 은 틀렸다: Culling 장면 재질은 텍스처를 쓰지 않는다. 묶음 실행은 창 배치 파일이 달라 캡처가 1243 x 630 (저장소는 1143 x 567)
  - 다른 화소는 두 실행 모두 1 ~ 3 개뿐 (0.0002 ~ 0.0004 %), 셋 다 두 면이 만나는 가로 모서리에 걸친 화소가 다른 면으로 갈린 것 — 채우기 규칙 (Y 뒤집기) 문제라면 같은 모서리의 화소가 줄줄이 달라야 한다 →
    정점 계산의 마지막 자리 차이 (DX = fxc, Vulkan = DXC → SPIR-V) 로 모서리에 거의 걸친 화소 중심이 갈리는 것. 엔진은 정상, 화소 하나의 최대 차이 (20) 기준이 약했다 (Shadows 도 한 화소 max 14)
  - `Tools/tests/run_tests.ps1` vulkan: max 20 이하 또는 (8 넘게 다른 화소 0.005 % 이하 + 평균 0.05 이하), 비율을 소수 넷째 자리까지. 검사: Release 묶음 · Debug 둘 다 vulkan 통과 (Debug 10/10)
- 이전: 2026년 10월 5일 — **VFX Graph 마무리: 꼬리 · 정렬 · 컬링, 연산 노드, 성능 · 데모** (사용자 지시: 추천 1 · 2 · 3 "이거해줘. 대화는 일단 킵 하고"). **완료 (커밋 bff097c, 푸시함)**
  - 셰이더 `Shaders/58. VFX.fx`: 이벤트 버퍼 하나 (죽음 · Rate), 꼬리 기록 (링 버퍼) + `TrailCS` (마디 인스턴스) + TrailVS/PS, 정렬 (SortKeys · 512 그룹 비토닉 · 전역 단계 · Gather), Update 가 경계 상자를 모음 (gState 48 바이트),
    연산 노드 해석 `Eval` (고정 레지스터 r0..r9 스택 — fxc 가 동적 색인 배열을 잘못 옮겨 DX11 에서 값이 사라졌음), 블록마다 한 번 계산 (`PrepareSlots` — fxc 컴파일 61 → 약 5 초)
  - `Source/Effects/VfxAsset.*` (Trigger On Die · Rate, Output Sort · Trail, Culling, operators · links, EncodeSlots · EncodeLinks — 칸 찾기는 표식 값으로), 새 `VfxOperators.cpp` (노드 약 50 종 · 스택 명령으로 옮기기, 깊이 · 고리 검사),
    `VfxRuntime.*` (꼬리 · 정렬 · 경계 버퍼, 뷰마다 정렬, 절두체 컬링, 화면 밖이면 시뮬레이션 쉼, 프로그램 키 = 에셋 개정 번호), `VisualEffect.*` (IsCulled · 경계 기즈모), `VfxTemplates.cpp` (Energy Swirl · Rainbow Spiral, 불꽃놀이 로켓 꼬리 + Rocket Sparks, 토네이도 용량 줄임)
  - 편집기: `VfxGraphWindow.*` (연산 노드 · 블록 값 핀 · 연결 만들기 · 끊기, Trigger · Sort · Trail · Culling 칸), `VfxCli.*` (operators · op.add/set/remove/connect/disconnect · block.link/unlink · encode, stats 에 culled), `EditorApp.cpp` (58. VFX.fx 를 미리 컴파일 목록에 — 첫 컴파일 5 초 HANG)
  - 검사: vfx · vfxgl · vfxvk **32/32** (연산 노드 + 정렬, 연산 노드 수명, 꼬리, 화면 밖 컬링 추가), `android_vfx.ps1` MuMu **11/11** (Scene 카메라로 비춘 뒤 DX11 기준 그림 — 컬링 때문에 멈춰 있었다), Debug 복원 뒤 vfx 14/14
  - 성능 (PC Release, `E:\NovaTest\ClaudePerfEngine`, MuMu 끔): 견본마다 파티클 패스 0.09 ~ 1.68 ms (DX11) — docs/VFX_GRAPH.md 표. 데모 `E:\NovaTest\VfxDemo` (밤 캠프장, F · E, C# 이 Exposed Property 를 바꿈). Showcase 217 ~ 221
  - 이어서 README 최신화 · 압축 (사용자 지시 "리드미 … 글자는 압축 … 메인 썸네일 더 이쁜 사진으로") — 메인 그림 `docs/images/hero_night_camp.webp` (VfxDemo 의 Hero.scene), 커밋 f494ce8 푸시
  - 대화 (VFX Assistant) 는 사용자 지시로 보류 — claude CLI 로그인 만료 그대로, Assistant 시스템 안내에 연산 노드는 아직 안 적음
- 이전: 2026년 10월 5일 — **Visual Effect Graph + VFX Assistant** (사용자 지시: "vfx 그래프? 유니티의 해당 기능 만들어줘 … 대화는 클로드 코드랑 연결 … API 같은거는 고려하지마"). **완료 (커밋 087b296, 푸시함)**
  - GPU 런타임: 새 `Shaders/58. VFX.fx` (Reset · Spawn · Update compute, 블록 목록을 셰이더가 해석, GPU Event, 그림 없는 모양 · 플립북 · Soft), `Source/Effects/VfxAsset.*` (.vfx JSON · 블록 정의표 19 종 · Encode),
    `VfxTemplates.cpp` (견본 8), `VisualEffect.*` (컴포넌트 · 이벤트 · Spawn 수 · Inspector), `VfxRuntime.*` (버퍼 · 시뮬레이션 · 그리기 · 살아 있는 수 읽기), `VfxScripting.cpp` (C# NovaVfx_*)
  - 연결: `Platform/App.cpp` · `Android/Source/Engine/AppAndroid.cpp` (UpdateAll), `Editor/EditorApp.cpp` (Particles 단계 뒤 VfxRuntime::Render — 두 뷰), `AddComponentMenu` · `GameObjectMenu` · `GameObjectFactory` (CreateVisualEffect),
    `CliCommands.cpp` (create visual-effect), `Build/BuildPipeline.cpp` (.vfx), `Tools/NovaCli/main.cpp` (vfx 명령 · create --asset), `Android/Source/AndroidMain.cpp` (scene 검사 결과에 vfx)
  - 편집기: `Editor/Windows/VfxGraphWindow.*` (시스템 노드 · 블록 · Blackboard · Inspector · 검색 창 · GPU Event 핀 · Undo · 저장 전 장면 반영 Vfx::SetLive), `VfxAssistantWindow.*` (로컬 Claude Code headless —
    claude.exe · npm cli.js · 데스크톱 앱 claude.exe 중 높은 판, 자식 환경에서 ANTHROPIC_* · CLAUDE_CODE_* 제거, --allowedTools nova vfx/screenshot/camera/create visual-effect + Read), `Editor/VfxCli.*` (nova vfx)
  - C#: `ScriptCore/Engine/VisualEffect.cs` (NovaEngine.VFX.VisualEffect), `Core.cs` 매핑
  - 검사: vfx · vfxgl · vfxvk **20/20** (run_tests), `Tools/tests/android_vfx.ps1` MuMu **11/11**, 회귀 cli · particles · linetrail **21/21**, 안드로이드 빌드. 문서 `docs/VFX_GRAPH.md` · README. Showcase 213 ~ 216
  - Assistant 실제 대화는 아직 못 해 봄: 이 PC 의 claude CLI 로그인이 만료 ("OAuth session expired") — 사용자가 터미널에서 `claude` → `/login` 하면 동작. 파이프 · stream-json · 오류 안내까지는 확인
- 이전: 2026년 10월 5일 — **활성 카메라 찾기 · 물리 동기화 남은 비용** (사용자 지시: 추천 1 · 2 진행, "코덱스는 이제 작업 안해. 너가 다해"). **완료 (커밋, 푸시 전)**
  - 카메라 · 빛 1.1 ms (안드로이드) 의 원인 = `DisplayManager::GetCameraForDisplay` 가 부를 때마다 씬 전체 (복사 + 오브젝트마다 GetComponent_SP). `Camera::All` (생성 · 소멸 때 등록,
    복사 생성자 삭제) 에서 고른다 — 규칙 그대로 (현재 씬 · 활성 계층 · 켜짐 · 디스플레이 · 첫 Camera · Priority, 같으면 씬 순서), 지워진 오브젝트는 `GameObject::IsAlive` (Play 멈춤 때 충돌을 검사가 잡음)
  - 새 `Source/Scene/ComponentIndex.*`: 오브젝트마다 컴포넌트 분류 (콜라이더 · Rigidbody · Character Controller · Joint · 2D 콜라이더 · Rigidbody2D · Joint2D) 를 InstanceID 지문으로 기억.
    `Physics/PhysicsManager.cpp` (동기화 · FindRigidOwner), `Physics2D/Physics2DManager.cpp` (Sync · FindRigidbody), `Physics2D/Physics2DJoints.cpp` (Sync), `Scene/Scene.h` (GameObjectsView)
  - PC Release 도시 Play A/B: 프레임 7.95 → 6.6 ms, 카메라 · 빛 1.01 → 0.08 아래, 동기화 0.64 → 0.46, 2D 0.36 → 0.08 ms. FixedUpdate 는 예전 방식이 더 싸서 그대로
  - 검사: cli (새 활성 카메라 검사) · physics · physics2d · animation · packages · ui · render · material · occlusion · audio · layers · sprites · anim2d, joints2d **42/42**, scene_lifecycle **43/43**, 안드로이드 빌드. Showcase 212
  - Codex 작업 종료 — AI_COLLABORATION.md 에 적음
- 이전: 2026년 10월 5일 — **Renderer 마무리** (같은 지시 — 추천 3). **완료 (커밋, 푸시 전)**
  - 재질 칸 블록: `Scene/MaterialBlock.*` (SetAt · GetAt — 렌더러 블록 위에 덮음, 있으면 인스턴스 값 대신 파생 재질), `MeshRenderer.h` · `SkinnedMeshRenderer.h` (PropertyBlock()),
    `MaterialScripting.cpp` (NovaMat_SetBlock · BlockCount · BlockEntry 에 materialIndex, NovaMat_HasBlock)
  - 정렬: `MeshRenderer.*` · `SkinnedMeshRenderer.*` · `Effects/LineRenderer.*` 에 Sorting Layer · Order (저장), `MeshBatcher.cpp` 투명 패스 · `Effects/ParticleRenderer.cpp` 입자와 선 정렬 (레이어 → 순서 → 거리),
    `SpriteRenderer.*` (SortingFields — Line · Trail Inspector 도), `MaterialScripting.cpp` (kind 3 Line · 4 Trail, 선 bounds, NovaRenderer_GetSorting · SetSorting · SortingLayerName · SortingLayerId)
  - C#: `Renderer.cs` (SetPropertyBlock(block, i) · GetPropertyBlock(block, i) · sortingOrder · sortingLayerID · sortingLayerName), `Material.cs`, `Components.cs` (SpriteRenderer 의 정렬은 Renderer 로),
    `LineRenderer.cs` (LineRendererCommon : Renderer, LineKind), `Core.cs` (GetComponent<Renderer> 에 Line · Trail)
  - 검사: material · materialgl · materialvk **14/14**, linetrail · sprites, particles · anim2d · layers **31/31**, 안드로이드 빌드. Showcase 211
- 이전: 2026년 10월 5일 — **인스턴스 속성 넓히기** (사용자 지시 "원래 순서로 진행해줘" — 추천 2). **완료 (커밋 5b4e4de, 푸시 전)**
  - 인스턴스 값 80 → 112 바이트 (+ Surface: _Metallic · _Smoothness, + Emission: 선형 _EmissionColor): `Shaders/32. InstancedBasic.fx` (VertexIn_Batch · LitPS 인자 · PS_Batch · BatchToInstancing),
    `57. OcclusionCulling.fx`, `DX11/Vertex.*` (INSTSURFACE · INSTEMISSION), `DX11/OcclusionCulling.h`, `Scene/MeshBatcher.cpp` (InstanceProps · PropsOf), `Scene/MaterialBlock.*` (Instanced · InstanceValues),
    `Scene/MeshRenderer.h`, `DX11/UMaterial.*` (InstancePropOf · EmissionToLinear · CanInstanceEmission · ScriptProp — 패키지 · 그래프 재질은 같은 이름의 속성을 SetColor/GetColor)
  - Shader Graph: `ShaderGraph.*` (InstanceSlotOf, 인스턴스 속성은 상수 버퍼 gSGm_ + static gSG_, SG_InstanceProps, 배치 VS 가 VertexIn_Batch), `ShaderGraphRuntime.*` (InstanceSlotFor, gSGm_ 바인딩)
  - 검사: material · materialgl · materialvk **12/12** (파생 재질과 픽셀 같음, Shader Graph 묶음 그대로), shadergraph **24/24**, packages · animation **21/21**, 렌더링 회귀 12 스위트 **99/99** (112 바이트), 안드로이드 빌드. Showcase 210
- 이전: 2026년 10월 5일 — **물리 동기화 CPU** (사용자 지시 "푸시하고. 다음 작업 진행" — GLES 측정에서 찾은 물리 갱신 3.3 ms). **완료 (커밋, 푸시 전)**
  - `Source/Physics/PhysicsManager.cpp`: StepSimulation 의 동기화를 한 번 훑기 (콜라이더 · Rigidbody · Character Controller · Joint), 버퍼 재사용, 콜라이더 표 제자리 갱신 (seen 번호),
    종류 기억 (KindOf · ComputeSignature 의 kinds), SyncJoints 가 모은 목록으로, 단계마다 Profiler 구간 (Physics.FixedUpdate · Sync · Characters · Joints · TransformToBody · Simulate · Events)
  - PC Release 도시 Play: 동기화/2D 물리 비율 3.7 → 1.7 (약 2 배), 캐릭터 · Joint 0.29 ms → 0. 회귀 physics · physics2d · animation · packages · cli **51/51**
  - 안드로이드 재측정은 못 했다: 그때 Unity · ChatGPT 앱이 CPU 를 많이 써서 Debug 에디터가 도시 장면을 만들다 4.3 초 멈춤 → 게임 데이터 내보내기 실패 (에디터 모드라 물리 변경과는 무관).
    `android_city_perf.ps1` 은 막 켠 VM 의 adb offline 때 ABI 를 기다리게 고침
- 이전: 2026년 10월 5일 — **GLES 그리기 CPU** (사용자 지시 "푸시하고 진행해줘" — 추천 1 → 2 → 3 의 1). **완료 (커밋, 푸시 전)** — android **66/66**, android_occlusion **13/13**, 도시 10/10, PC material · cli **19/19**, Showcase 209
  - `Android/Source/GLESState.*` (상태 · 바인딩 기억: 프로그램 · 상수 블록 · 텍스처 유닛 · 샘플러 · SSBO · image · VAO, Invalidate · Forget · Deleted),
    `GLESRhi.cpp` (Apply 가 기억으로, 상수 블록은 바뀐 범위만), `GfxGLES.*` (VAO 안 버퍼 기억, 지우기 · Present · RestoreState 에서 기억 버림, GlesCounters),
    `AndroidMain.cpp` (`-e profile on` → gl · scopes), `Engine/AppAndroid.cpp` (프레임 구간), `Source/Core/Profiler.cpp` (BeginFrame 스레드 = 구간 스레드 — 안드로이드에서 구간이 모두 버려졌다),
    `Tools/tests/android_city_perf.ps1` (`-Profile` · `-SkipBuild`, 결과를 파일로), `docs/ANDROID.md`
  - 프레임마다 텍스처 바인딩 3528 → 79, 샘플러 3528 → 48, 상수 블록 바인딩 1240 → 33, 프로그램 205 → 37. MuMu 켬 11.6 → 11.0 ms, 끔 13.9 → 11.6 ms (편차 큼)
  - 찾은 것: 물리 갱신 3.3 ms (`PhysicsManager::StepSimulation` 이 고정 스텝마다 모든 GameObject 를 훑어 바디 동기화를 처음부터) — 다음 작업 후보
- 이전: 2026년 10월 5일 — **추천 2 · 3** (사용자 지시: 추천 2 "C# Renderer 공통 클래스 확장" · 3 "인스턴스별 속성 (GPU 인스턴싱 속성)" 붙여 넣기). **2 완료 (2ec9572 푸시)**, **3 완료 (커밋, 푸시 전)**
  - 2 (**완료**) C# `Renderer` 기본 클래스: 새 `ScriptCore/Engine/Renderer.cs` (Renderer · Bounds · `NovaEngine.Rendering.ShadowCastingMode`), `Material.cs` (렌더러 부분을 Renderer.cs 로, DllImport 에 kind),
    `Components.cs` (Mesh · Skinned · Sprite 가 Renderer 를 물려받음), `Core.cs` (GetComponent<Renderer> → Mesh → Skinned → Sprite), 새 `Source/Scene/MaterialBlock.*` (블록 · 파생 재질 공용 — MeshRenderer.cpp 에서 옮김),
    `MeshRenderer.*` (MaterialBlock 사용 · SetCastShadows), `SkinnedMeshRenderer.*` (재질 · 블록 · 그림자 API, 그릴 재질 = RenderMaterials, **enabled 를 그리기 함수에서 확인** — 예전엔 꺼도 그렸다),
    `MaterialScripting.cpp` (kind 0 Mesh · 1 Skinned · 2 Sprite, NovaRenderer_* enabled · bounds · shadows), `Scripting/ScriptBindings.cpp` (`Get<T>` 가 같은 프레임에 AddComponent 한 것도 — 예전엔 `AddComponent<SpriteRenderer>().sprite = …` 가 무시됨),
    `run_tests.ps1` (Suite-Material 3 개 더, sprites 의 Sprite Animator 검사를 여러 번 읽게 — 간격이 클립 한 바퀴 0.33 초와 겹쳐 떨어지던 것), `docs/MATERIAL_SCRIPTING.md` · `README.md`. material **8/8**, sprites · layers · physics2d · anim2d · occlusion **54/54**, 안드로이드 빌드. Showcase 207
  - Codex 에게: `Scene.cpp` 는 고치지 않았다 (Skinned 의 enabled 확인은 SkinnedMeshRenderer.cpp 안에서)
  - 3 (**완료**) 인스턴스별 속성: 인스턴스 값 64 → 80 바이트 (월드 + 기본색). `Shaders/32. InstancedBasic.fx` (VertexIn_Batch · INSTCOLOR · VS_BatchColor · PS_Batch · LitPS — BatchTech 만),
    `57. OcclusionCulling.fx` (World 에 Color, Compact 80 바이트), `DX11/Vertex.*` (INSTCOLOR, 배치를 BatchTech 서명으로), `DX11/OcclusionCulling.*` (InstanceBytes), `DX11/UMaterial.*` (IsBaseColorProperty),
    `Scene/MaterialBlock.*` (InstanceColor), `Scene/MeshRenderer.h` (GetBatchMaterials), `Scene/MeshBatcher.cpp` (Instance), `run_tests.ps1` (material 10 개, materialgl · materialvk).
    material · materialgl · materialvk **10/10** (Vulkan 검증 레이어 오류 없음), 렌더링 회귀 12 스위트 **99/99** (occlusion 세 API · render · gfx · vulkan · shadergraph · lodgroup · decal · probevolume · reflectionprobe · packages), 안드로이드 빌드. Showcase 208 (상자 576 · 색 47 — 묶음 2 대 47)
- 이전: 2026년 10월 5일 — **다음 작업 1 · 2 · 3 모두** (사용자 지시 "어 모두 적용해줘"). **모두 완료**
  - 1 (**완료**) OpenGL 오클루전 GPU 손해: `OpenGL/GfxGL.cpp` · `GLLoader.h` — 이벤트 · STAGING 쓰기 펜스를 프레임 끝 (Present) 에, STAGING 은 늘 매핑, 조건부 그리기 NO_WAIT.
    `Android/Source/GfxGLES.cpp` · `AndroidMain.cpp` (GLES 도 펜스를 프레임 끝에). 도시 GL GPU 3.72 → 2.65 ms (켜면 늘던 것). 35/35 · android_occlusion 13/13
  - 2 (**완료**) C# 재질: 새 `ScriptCore/Engine/Material.cs` (Material · Shader.PropertyToID · MaterialPropertyBlock · MeshRenderer.material/sharedMaterial/SetPropertyBlock),
    `ScriptCore/Engine/Components.cs` (MeshRenderer 를 partial 로 — 한 줄), 새 `Source/Scene/MaterialScripting.cpp` (NovaMat_* DllImport — 네이티브 표는 그대로),
    `Graphics/DX11/UMaterial.*` (이름으로 값 · CloneInstance · StateHash), `Scene/MeshRenderer.*` (SetMaterialAt · 블록 · 파생 재질), `MeshBatcher.cpp` (GetRenderMaterials),
    `run_tests.ps1` 의 `Suite-Material`, `docs/MATERIAL_SCRIPTING.md`. material 5/5, 회귀 69/69, 안드로이드 빌드
  - 3 (**완료**) 안드로이드 기기 성능 (MuMu, 도시): 새 `Tools/tests/android_city_perf.ps1` · `city_scene.py` (도시 생성기를 저장소로), `AndroidMain.cpp` (`-e warmup`, cpuMs).
    결과 10/10 — 1942 / 2128 가려짐 · 같은 그림, 프레임 끔 8.81 → 켬 8.97 ms (에뮬레이터는 CPU 가 프레임을 정함 — 삼각형 이득이 드러나지 않음, 실제 휴대폰은 따로). Showcase 205 · 206
- 이전: 2026년 10월 5일 — **오클루전 컬링 도시 쇼케이스 (세 API)** (사용자 지시: 다음 작업 3 번 "도시 쇼케이스 화면"). **완료** — Showcase 203 (도시 · 세 API 표) · 204 (안드로이드 MuMu)
  - 세 API 가 같은 렌더러 1977 / 2222 를 가리고 삼각형이 같다 (깊이 프리패스 2,351k → 970k, 불투명 2,351k → 536k) — `docs/OCCLUSION_CULLING.md` 의 도시 표
  - GL · Vulkan 파이프라인 통계 (프로파일러 · `nova perf` 의 primitives · pixels — 예전엔 0): `OpenGL/GfxGL.cpp` · `GLLoader.h` (ARB_pipeline_statistics_query, 겹친 구간 = 조각),
    `Vulkan/GfxVkContext.cpp` · `GfxVkDevice.cpp` · `GfxVkInternal.h` (pipelineStatisticsQuery, 렌더링 밖에서 조각, 제출 때 끊고 다시)
  - 고친 버그: GL `ClearUnorderedAccessViewUint` 가 호출한 쪽 스택의 0 을 넘겨 Release 에서 카운터가 약 21 억으로 지워졌다 → 오클루전이 "거의 가리지 않음" 으로 쉬었다 (긴 수명 값으로)
  - 도시 장면 · 측정 스크립트는 세션 scratchpad (`city/make_city.py` · `city_showcase.ps1` · `compose.py`), 장면은 ScriptTest 의 `Assets/Scenes/CityShowcase.scene`
- 이전: 2026년 10월 4일 — **Vulkan 그리기마다의 CPU 비용** (사용자 지시: 다음 작업 2 번). **완료** — 캐릭터 64 장면 (Release) 씬 뷰 그리기 CPU **2.02 → 1.11 ms** (DX11 1.15 ms), vulkan · gfx · render · occlusion 3 종 회귀 **35/35**
  - `Vulkan/GfxVkInternal.h` (ImageStateSerial · LastSet · LastKey · 묶인 정점 · 인덱스 버퍼), `GfxVkContext.cpp` (같은 값이면 SetProgram 그대로, 앞 디스크립터 집합 · 동적 UBO 오프셋만, 앞 파이프라인, 같은 버퍼 다시 안 묶기),
    `GfxVkDevice.cpp` (BindingLayout::HasStorage), `run_tests.ps1` 의 `Suite-Perf` 에 Characters (씬 뷰 그리기 CPU 비교), `docs/VULKAN_BACKEND.md`
  - 측정은 Release 복사본 `E:/NovaTest/ClaudePerfEngine` (Debug · Release 가 같은 Binaries 로 나와서 — 저장소 Binaries 는 Debug 로 되돌림)
  - 다음: 3 번 도시 쇼케이스 (세 API 삼각형 · 프레임 시간)
- 이전: 2026년 10월 4일 — **오클루전 컬링 안드로이드 (OpenGL ES)** (사용자 지시: 다음 작업 1 · 2 · 3 모두 — 1 부터). **완료** — android_occlusion **13/13** (MuMu), android 회귀 **66/66**, PC occlusion · occlusiongl · occlusionvk **15/15**
  - `OcclusionCulling.cpp` 의 안드로이드 빈 함수를 지우고 같은 코드로 (`kGles`: 간접 인자 첫 인스턴스 → 정점 버퍼 오프셋, 밉 SRV → `gSrcLevel`, 조건부 렌더링 → `BoxCullTech` + `SetPredicationBuffer`),
    첫 프레임 결과로 "30 번 쉬기" 하지 않음 (`StagingFrame` — 기기 검사 30 프레임 동안 꺼져 있던 원인), `57. OcclusionCulling.fx` (gSrcLevel · gBoxes · BoxCullTech), `Gfx.h` (SetPredicationBuffer)
  - `Android/Source/GfxGLES.cpp` (SSBO · image · 간접 그리기 · 예측 버퍼 · fence), `GLESRhi.cpp` (SSBO · image 바인딩, 샘플러 없는 칸 = NEAREST), `AndroidMain.cpp` (`-e occlusion off`, NOVA_TEST 의 occlusion),
    `Android/Include/WinCompat.h` (상수), `Tools/tests/android_occlusion.ps1` (새 검사)
  - 고친 버그: `glGetSynciv` 의 bufSize 를 바이트 수 (4) 로 넘겨 기기에서 스택이 깨졌다 (GL · GLES 모두 1 로)
  - 다음: 2 번 Vulkan 그리기 CPU 비용, 3 번 도시 쇼케이스
- 이전: 2026년 10월 4일 — **오클루전 컬링 OpenGL · Vulkan 지원** (사용자 지시 "그래 지원해줘" — 순서 OpenGL → Vulkan → 안드로이드). **OpenGL · Vulkan 완료** — occlusiongl **3/3**, occlusionvk **3/3** (검증 레이어 오류 0), occlusion (DX11) **9/9**
  - `Shaders/57. OcclusionCulling.hlsl` → `57. OcclusionCulling.fx` (fx 효과 — GL 은 ShaderCross 로 GLSL compute), `OcclusionCulling.cpp` 를 D3D11 직접 호출 없이 Gfx 층 · Rhi::Effect 로
  - `RHI/Gfx.h` (GfxContext 에 SupportsGpuDriven · 간접 그리기 · SetPredication · UAV 지우기), `DX11/GfxDx11.cpp` (그대로 넘김, 예측 쿼리 = CreatePredicate),
    `OpenGL/GfxGL.cpp` · `GLRhi.cpp` · `GLLoader.h` · `GLShared.h` (버퍼 SRV · UAV = SSBO, 텍스처 UAV = image, ANY_SAMPLES_PASSED + 조건부 렌더링, fence), `run_tests.ps1` 의 `Suite-OcclusionGL`
  - Vulkan: `Vulkan/GfxVkDevice.cpp` · `GfxVkContext.cpp` · `GfxVkInternal.h` · `GfxVkShared.h` · `VkRhi.cpp` · `VkLoader.h` — 스토리지 버퍼 · 이미지, compute 파이프라인 · 디스패치,
    간접 그리기, 오클루전 쿼리 (칸 고리 — 예전엔 결과 1), `VK_EXT_conditional_rendering`. `run_tests.ps1` 의 `Suite-OcclusionApi` (GL · VK 같은 검사), `docs/VULKAN_BACKEND.md`
  - 회귀: OpenGL 단계에서 13 스위트 110/110 (render · gfx · vulkan 포함), 안드로이드 빌드 통과
  - 다음: 안드로이드 GLES (`GLESRhi` — GLES 3.1 compute · SSBO, 간접 그리기는 baseInstance 가 없어 정점 버퍼 오프셋으로)
- 이전: 2026년 10월 4일 — **Line Renderer · Trail Renderer** (사용자 지시: 추천 1 · 2 · 3 모두 — 3 부터). **완료** — linetrail **9/9**
  - 새 `Source/Effects/LineRenderer.*` (두 컴포넌트 · 띠 만들기 · C# 내보내기 `NovaLine_*`), 새 `ScriptCore/Engine/LineRenderer.cs` (DllImport — 네이티브 표는 그대로), `Shaders/43. Particle.fx` (LineAlphaTech · LineAdditiveTech),
    `ParticleRenderer.cpp` (입자와 함께 정렬해 그림), `ParticleSystemEditor.h` · `ParticleSystemInspector.cpp` (TexturePicker), `GameObjectFactory.*` · `GameObjectMenu.cpp` · `AddComponentMenu.cpp` (메뉴 한 줄씩), `App.cpp` · `AppAndroid.cpp` (Trail 갱신)
  - 깊이 프리패스 "약 5 ms" 원인 (**완료**): GPU 일이 아니었다 — CPU 가 늦은 프레임에서 GPU 가 쉬는 시간이 구간에 붙음 + 오브젝트마다 CPU 비용.
    CPU: 프레임 20.6 → 12.8 ms (오브젝트 2000 개, Debug) — `Camera.cpp` 의 쓰지 않는 오브젝트 절두체 목록 제거 (3.3 → 2.0 ms), `Component.h` 에 CullSlot
    (SceneCulling · 오클루전이 해시 찾기 없이), MeshBatcher 묶음 · 재질 판정 기억 (Collect 3.0 → 1.5 ms), SceneCulling 메시 상자 기억 (2.8 → 1.5 ms).
    GPU: Hi-Z 를 반 해상도부터 (2 ~ 3.5 ms 로 보이던 구간 → 0.06 ~ 0.24 ms), 통계 읽기 DONOTFLUSH, 렌더러 64 개 미만 · 거의 안 가리면 쉼. `nova perf --gpu-depth`
  - 오클루전 컬링 넓히기 (진행 중): **Skinned Mesh Renderer** = 상자 오클루전 예측 쿼리 + SetPredication (`SkinnedMeshRenderer.*` · `SceneCulling.*` — 상자를 렌더러 Bounds 로,
    예전 FBX cm 정점 그대로의 100 배 상자 고침), **나무** = GPU 인스턴스 목록 컬링 (`TreeRenderer.cpp`, `OcclusionCulling::CullList`), Hi-Z 검사 정밀도 (5x5 칸) — occlusion 8/8, animation · render 통과
  - **그림자 캐스터** (완료): `EditorApp.cpp` 에서 그림자를 깊이 프리패스 뒤로 (그다음 카메라 컬링 다시), `ShadowRenderer.*` 의 `Current` (방향광 · 매 프레임 캐스케이드 · 빛 방향 · 구 지름),
    `MeshBatcher` 가 빛 방향으로 쓸어 늘린 상자를 `OcclusionCulling::BeginShadow` 로 카메라 Hi-Z 검사 → 간접 그리기. `nova perf` 의 구간마다 삼각형 · 픽셀 (PIPELINE_STATISTICS — 시간보다 믿을 수 있다)
  - 검사: 렌더링 회귀 12 스위트 **107/107** (occlusion 9 · linetrail · lodgroup · reflectionprobe · probevolume · antialiasing · decal · ssr · depthoffield · shadergraph · animation · render), 안드로이드 빌드
- 이전: 2026년 10월 4일 — **오클루전 컬링 (굽기 없는 GPU Hi-Z)** (사용자 지시: "너가 말한 방식으로 … 최고 효율방식 … 별도 사전 작업 없이 실시간"). **완료** — occlusion **7/7**, 회귀 lodgroup · shadergraph · reflectionprobe · probevolume · antialiasing · render 포함 **71/71**
  - 새 `Shaders/57. OcclusionCulling.hlsl` (cs_5_0 커널 5 개 — fx 가 아니라 GLES 변환 대상 아님), 새 `Source/Graphics/DX11/OcclusionCulling.*` (두 단계 Hi-Z, DrawIndexedInstancedIndirect, CLI `nova occlusion`)
  - `MeshBatcher.*` (GPU 목록 · `FinishDepthPrepass`), `SceneCulling.*` (TrackedSlot · SlotCount), `MeshGeometry.*` (BindForInstancing · GetSubset), `EditorApp.cpp` (Game · Scene 뷰 프리패스 뒤), `Camera.h` (UsesOcclusionCulling),
    `App.cpp` · `GameViewEditorWindow.cpp` (통계), `Tools/NovaCli/main.cpp`, `run_tests.ps1` 의 `Suite-Occlusion`
  - Codex 에게: 씬 파일 (`Scene.cpp`) 은 고치지 않았다 — 2 단계는 EditorApp 이 `RenderSceneShadowNormal` 뒤에 부른다. 안드로이드 · GL · Vulkan 은 예전과 같은 CPU 절두체 컬링
- 이전: 2026년 10월 4일 — **Codex 의 Joint 2D 통합 · 커밋** (사용자 지시 "그래 진행해줘" — 다음 작업 추천 1번). **완료** — 최신 main 빌드로 joints2d **42/42**, 씬 scene_lifecycle **44/44**, physics2d **8/8**, 엔진 빌드 (주석 변경 뒤) 통과
  - Codex 가 10월 3일에 끝내고 커밋하지 않은 Joint 2D (`Physics2DJoints.*` · C# `Physics2D.cs` · 네이티브 표 `J2_*` · `GameObject.*` RequireComponent · `PrefabUtility.cpp` · `SceneManager.cpp` …) 를
    최신 main 빌드 (안드로이드 · Decal 포함) 에서 다시 검사해 Codex 의 변경만 따로 커밋한다. 코드는 그대로, 영어 주석 11 줄만 한국어로 (프로젝트 규칙)
  - `docs/ai-status/CODEX.md` 는 Codex 가 쓴 그대로 커밋 (내용은 고치지 않음)
- 이전: 2026년 10월 4일 — **안드로이드 스토어 배포: 서명 키 · 아이콘 · Bundle Version Code · App Bundle (.aab)** (사용자 지시 "진행해줘" — 추천 1 → 2 → 3). **완료** — android **66/66** (MuMu — 키 만들기 · APK 배포 키 서명 · AAB jarsigner · bundletool validate · build-apks · 아이콘 · versionCode), PC ui 15 · cli 9
  - `BuildSettings.*` (Player 에 AndroidVersionCode · AndroidIcon · AndroidCustomKeystore · Keystore · Alias, 비밀번호는 메모리에만 · 편집기 설정에 androidBuildAppBundle), `UnityGUI.*` (PasswordField),
    `ProjectSettingsWindow.cpp` (Publishing Settings · Create New Keystore), `BuildSettingsWindow.cpp` (Build App Bundle), `AndroidBuild.*` (아이콘 mipmap · 배포 키 서명 · AAB 조립 · jarsigner), `AndroidTools.cpp` (CLI keystore-create · build 옵션)
  - bundletool 은 검사에만: `Tools/fetch_bundletool.ps1` → `ThirdParty/bundletool` (gitignore)
  - Codex 에게: `UnityGUI.h` 에 `PasswordField` 추가 (다른 함수는 그대로). 공용 파일 중 고친 것은 `BuildSettings.*` (Player 필드 추가만)
- 이전: 2026년 10월 4일 — **안드로이드 마무리: APK 압축 · C# 터치 · 앱 일시 정지 · 화면 방향 · 안전 영역 · Hub 의 Mono** (사용자 지시: "코덱스 무시하고 너가 다해줘"). **완료** — android **61/61**, PC cli 9 · physics 13 · ui 15 · keys 6, Hub 37/37
  - C# API (ScriptCore — Codex 의 미커밋 파일은 건드리지 않음): `ScriptCore/Engine/Services.cs` 에 Input.touchCount · GetTouch · Touch · TouchPhase, Screen.safeArea · orientation,
    Application.platform (RuntimePlatform 값을 Unity 와 같게: WindowsPlayer 2 · WindowsEditor 7 · Android 11) · isMobilePlatform, 새 `ScriptCore/Interop/AppEvents.cs` (OnApplicationPause · Focus)
  - 네이티브: 새 `Source/Scripting/PlatformBindings.*` (이름으로 내보낸 함수 — C# DllImport("NovaCore")), `ScriptEngine.*` 에 OnApplicationPause · Focus, `App.cpp` WM_ACTIVATE (빌드된 게임)
  - Player Settings 의 Android Default Orientation (`BuildSettings.*` · `ProjectSettingsWindow.cpp` → manifest screenOrientation), APK 의 라이브러리를 엔진 DEFLATE 로 (AndroidBuild.cpp)
  - Hub: `AndroidToolsInstaller.cs` 에 Mono (nuget.org, SHA-512) — Hub 검사 37/37 (+ live 39/39)
- 이전: 2026년 10월 4일 — **안드로이드: C# 스크립트 (Mono) · 시작 시간 · 리버브** (사용자 지시: 남은 점 진행). **완료** — android **54/54** (MuMu)
  - C#: 새 `Android/Source/Engine/ScriptEngineAndroid.cpp` (Mono 임베딩 — dlopen, TPA, PINVOKE_OVERRIDE, [UnmanagedCallersOnly] 진입점), 안드로이드가 `Source/Scripting/ScriptBindings.cpp` 도 빌드
    (작업 트리의 Codex 미커밋 변경을 포함해 컴파일됨 — 커밋된 상태끼리도 표 크기는 맞다). export 가 Managed/ (참조하는 BCL 만 — AssemblyRef 따라가기), 런타임은 `Tools/fetch_android_mono.ps1` → `ThirdParty/MonoAndroid` (gitignore)
  - 시작 시간: `GLESRhi.cpp` 효과의 pass 를 처음 쓸 때 컴파일 (+ 프로그램 바이너리 캐시), 그림자 샘플러 정규식 제거 → loadMs 약 9.7 s → 0.3 s
  - 리버브: 새 `Android/Source/Engine/AudioReverb.h` + `xaudio2.h` 의 I3DL2 프리셋 실제 값 · 변환, PC 검사 `Tools/tests/android_reverb_test.cpp`
  - Codex 에게: ScriptCore 는 고치지 않았다. C# 쪽 Input.touchCount 는 ScriptCore 담당 범위라 손대지 않음 (네이티브 `Input::GetTouch` 는 있음)
- 이전: 2026년 10월 4일 — **안드로이드: 모델 메시 캐시 · Build Settings APK · 터치 → UI · Input · 소리 (AAudio)** (사용자 지시: 추천 1 ~ 4 진행). **완료** — android **47/47** (MuMu), PC 회귀 ui 15 · model 56 · audio 4 · physics 13
  - 모델: `nova android export` 가 fbx · gltf · glb · vrm 을 메시 캐시만 넣음 (`SkinnedMesh.cpp` 의 ImportHash = FNV-1a 64 — MSVC std::hash 와 같은 값이라 PC 캐시 그대로)
  - 패키지: `Packages/*/Source` 를 안드로이드 엔진에 정적으로 (Animator · Toon …, `Android/CMakeLists.txt` + 만든 `nova_packages.cpp`), 패키지 셰이더도 GLES 로. lilToon.fx 에 `NOVA_GLES` 밉 개수 대체 (엔진 32 번과 같게)
  - **컴포넌트 등록 고침**: 안드로이드가 `NOVA_ENGINE_BUILD` 없이 빌드돼 `REGISTER_COMPONENT` 가 비어 기본 29 개만 있었다 (UI · 오디오 · 물리 컴포넌트가 씬에서 빠짐) → 켜고, `define.h` 에 `NOVA_KEEP_REGISTRATION` (clang `used`) — 83 개
  - Build Settings → Android: 새 `Source/Build/AndroidBuild.*` (셰이더 · 게임 데이터 → aapt2 → libnova.so zip 에 직접 → zipalign → apksigner → adb install · am start, MuMu 자동 connect),
    `BuildSettingsWindow.cpp` (플랫폼 Android, Run Device …), `BuildSettings.*` (activePlatform · androidRunDevice · lastAndroidApk · Player 의 androidPackageName), Player Settings 의 Package Name, CLI `nova android build · build-status`
  - 터치: `InputManager.h` · `Input.h` 에 Unity 의 Touch (touchCount · GetTouch — PC 는 0), 안드로이드 손가락 이벤트 큐 (빠른 탭), UI 좌표 = 왼쪽 아래 (0,0) (EditorStubs), `UIFont.cpp` 기본 글꼴 경로를 fs::path 로
  - 소리: 새 `Android/Source/Engine/XAudio2Android.cpp` (XAudio2 의 안드로이드 판 — 소프트웨어 믹서 + AAudio), 앱이 뒤로 가면 멈춤
  - Codex 에게: `define.h` 의 `REGISTER_PACKAGE_COMPONENT` 에 `NOVA_KEEP_REGISTRATION` (MSVC 는 비어 있음 — Windows 동작 같음). `InputManager.h` 에 `Touch` 구조체 (전역 이름 `Touch` — 충돌하면 알려 주면 바꾼다)
- 이전: 2026년 10월 4일 — **안드로이드 텍스처 압축 ASTC · ETC2** (사용자 지시 "유니티처럼 ETC2 나 ASTC 같은 압축 형식을 지원"). **완료 · push `075da13`** — android **32/32**, import 9/9
  - Unity 와 같은 설정: Player Settings → Android → Texture Compression (ASTC 기본 · ETC2 · DXT · None), 가져오기 설정의 Override for Android (Max Size · Format)
  - `nova android export` 가 그림을 `<이름>.png.dds` 로 구움 (ASTC = ARM astc-encoder 5.7.0 `ThirdParty/astcenc` Apache-2.0, ETC2 = 자체 `Source/Build/Etc2Codec.*`), 기기는 `경로 + .dds` 를 압축된 그대로 GPU 에
  - 새 파일: `Source/Build/{TextureCompressor,Etc2Codec}.*`, `Source/Graphics/Common/MobileTextureFormats.h`, `ThirdParty/astcenc/`. 공용 변경: 루트 `CMakeLists.txt` (astcenc 정적 라이브러리),
    `Source/Core/AssetImportSettings.*` (TextureSettings 에 Android 값 — `.meta` 의 `"android"`, 기본값이면 안 씀), `Source/Build/BuildSettings.*` (`androidTextureCompression`),
    `Source/Editor/ImportSettingsInspector.cpp` · `Windows/ProjectSettingsWindow.cpp` (UI 칸), 안드로이드 `DirectXTexLite` · `GLESState` · `GfxGLES` (블록 크기), `Tools/tests/android.ps1` (6 단계)
  - Codex 에게: `AssetImportSettings` 의 TextureSettings 에 필드 3 개가 늘었다 (IsDefault · JSON 포함). 캐시 키 (`CacheKey`) 는 PC 결과에 영향이 없어 그대로
- 이전: 2026년 10월 4일 — **안드로이드 2 단계 진행** (사용자 지시 "푸쉬하고 작업 진행해", 실제 휴대폰은 고려하지 않음 — MuMu 만). push `5738c18` 까지
  - `1a0e997` 플레이어 셸 (창 표면 · 프레임 루프 · 생명 주기 · 터치), `4cf6a88` Gfx 층의 GLES 구현 (GfxGLES — Gfx 검사 장면 DX11 과 차이 최대 1),
    `826d141` 엔진 런타임 전체를 NDK 로 빌드 · 링크 (엔진 소스 그대로, Windows 전용 6 파일만 안드로이드 판 + 에디터 함수 빈 구현). android **17/17**
  - 공용 파일 변경: `Source/Core/Utils.cpp` 한 줄 (`extension().wstring()`), `Tools/tests/common.ps1` (NOVA_ENGINE 옆 nova.exe), `Tools/tests/android.ps1`
  - **엔진 플레이어 완료**: EditorApp (PC 플레이어와 같은 렌더 경로) 을 안드로이드에서 — `Shadows.scene` 이 화면 없는 검사 · 앱 창 (60 fps) 모두 DX11 과 차이 최대 1, android **24/24**.
    새 `Android/Source/Engine/{AppAndroid,PlayerRuntimeAndroid}.cpp`, CLI `nova android export` (게임 데이터) · `nova android reference` (DX11 기준),
    공용 변경: `BuildPipeline::CollectGameFiles` (플레이어 빌드와 같은 에셋 모음을 밖에서 쓰게), `ShaderCross` 캐시 버전 6 (ES 의 gl_InvocationID 고치기 전 캐시 버리기)
  - 다음: 텍스처 (PNG) · 모델 (FBX) 을 PC 에서 구워 넣기 (기기에는 DirectXTex · Assimp 가 없다), 터치 → UI 입력 확인
  - Codex 에게: 안드로이드 빌드가 엔진 소스를 그대로 컴파일한다. 새 코드에서 `fs::path` 를 `wstring` 으로 바로 받거나 (`.wstring()` 쓰기), MSVC 만 되는 것 (Win32 API 직접 호출) 을 쓰면 `python Android/build.py` 가 깨질 수 있다 — 깨지면 알려 주면 Claude 가 대체를 단다
- 이전: 2026년 10월 4일 — **테스트 폴더 정리** (사용자 지시): `E:\NovaTest` 의 필요 없는 44.7 GB 를 `E:\NovaTest\_삭제대기` 로 옮김 (영구 삭제는 사용자가). `run_tests.ps1` 이 `TestResults\<시각>` 을 최근 10 개만 남김 (`-KeepResults`), 공동 명세에 "테스트 폴더 정리" 규칙
- 이전: 2026년 10월 4일 — **Hub "Android 빌드 지원" 모듈 완료 · push `0a5e731`** (사용자 지시 "유니티처럼 엔진 설치할 때 안드로이드 빌드 등 체크하면 해당 도구들도 같이 설치", "허브쪽 작업 완료되었어. 너가 이어서 작업해")
  - **Hub 인계**: Codex 가 만든 Hub 배포 · 엔진 설치 (미커밋이던 `Source/Hub/*` · `Tools/NovaHub*` · `Tools/package_hub.ps1` · `Tools/HubSigning.ps1` · `Tools/tests/hub_signing.ps1` · `docs/NOVA_HUB.md`) 를 사용자 지시로 이어받아 함께 커밋. Codex 의 Joint 2D 미커밋 파일과 `docs/ai-status/CODEX.md` 는 건드리지 않음
  - 새 `Tools/NovaHub/Core/AndroidToolsInstaller.cs` (OpenJDK 17 Temurin + Google repository2-3 의 platform-tools · build-tools 36 · android-34 · NDK 28 · CMake 3.22.1, 크기 + SHA-1/256, `<NOVA>\AndroidTools`), 서비스 `android-status · android-catalog · android-install`
  - Hub: 엔진 카드에 "Android 빌드 지원 함께 설치" 체크 (엔진 설치 뒤 이어서), Android 빌드 지원 카드, Android SDK 라이선스 동의 창 (사용자가 직접 체크 + 동의). 예전 카드의 API 배지에서 OpenGL · Vulkan "(미구현)" 제거
  - `Android/build.py`: `ANDROID_HOME`/`JAVA_HOME` → `NOVA_ANDROID_TOOLS` → 엔진 옆 / `%LOCALAPPDATA%\NOVA\AndroidTools` → Android Studio 순서
  - 검증: Hub 검사 **36/36** (Android 7 개 추가), 실제 공개 목록 (1034 MB, 라이선스 원문), 복사 엔진 Hub 화면 — 별도 상태 폴더로 카드 · 체크 → 동의 창 · 취소하면 아무것도 설치 안 됨 확인 (쇼케이스 191). **실제 1 GB 설치는 라이선스 동의가 사용자 몫이라 하지 않음**
  - Codex 에게: Hub 파일은 지금 Claude 가 이어서 맡는다. `HubEngineInstaller::Start` 에 `acceptLicense` 인자, `Android()` 상태, `Installing()` 이 `android-install` 도 포함
- 이전: 2026년 10월 4일 — **안드로이드 1 단계 첫 목표 완료 · push `de1bd52`** (사용자 지시 "다음 작업 진행 … MuMu 플레이어와 터미널 백그라운드 통신으로, 실제 창 없이"). 문서 `docs/ANDROID.md`
  - MuMu 게스트에 Vulkan 이 없어 (장치 0 개) 안드로이드 그래픽 = OpenGL ES 3.2. 엔진 RHI 검사 장면을 MuMu 에서 GLES 로 그려 DX11 과 차이 최대 1 — `Tools/tests/android.ps1` **7/7** (MuMu 검사 전용 VM "NOVA Test", 창 숨김, adb · logcat)
  - 새 파일: `Android/` (NDK CMake · NativeActivity · GLESRhi · WinCompat · build.py), `ThirdParty/DirectXMath/`, `Source/Build/AndroidTools.*` (CLI `nova android shaders`), `Source/Graphics/Common/FxStates.*` (GLState 에서 API 공용으로 옮김), `Source/Graphics/ShaderCross/ShaderCrossJson.*`
  - 고친 공용 파일: `ShaderCross.*` (GLSL ES 변환 · NOVA_GLES 전처리 · 캐시 버전 5), `GLState.cpp` (Fx 함수는 FxStates 로 넘김), `VkRhi.cpp` (FxStates), `Rhi.cpp` (안드로이드 분기), `MathHelper.cpp` (SimpleMath 대신 XMFLOAT4X4 한 줄), `Shaders/32 · 41 · 49 · 55` (ES 일 때만 밉 개수를 크기로), `EditorApp.cpp` · `NovaCli/main.cpp` (등록 한 줄씩)
  - 회귀: gfx 2/2 · vulkan 10/10 · render 8/8
- 이전: 2026년 10월 4일 — **Vulkan 마무리 (1 순위) 완료 · push `34e7b09`**: Release 성능 (Materials DX 0.93 / VK 1.02 ms, Trees DX 2.27 / VK 1.40 ms),
  장벽을 서브리소스 단위로 (`Image::Written`), 동기화 검사 (`NOVA_VK_SYNC_VALIDATION=1`) 경쟁 0, 창 밖 ImGui 창 (창마다 스왑체인 — `GfxVk::PresentWindow`).
  검사 vulkan 10/10 · perf 4/4 (Vulkan 줄 추가). 다음 후보: 안드로이드 빌드 (범위를 사용자와 정한 뒤)
- 이전: Vulkan 그래픽 백엔드 1 ~ 3 단계 + 4 단계 장면 · 빌드한 게임 완료 · push `0b6db63`
  - 검사 `vulkan` **10/10**: 화면 없는 장치 gfx · rhi (차이 최대 1), 에디터를 Vulkan 으로 띄운 렌더 7 장면이 DX11 과 같음, 검증 레이어 오류 0. 회귀 gfx 2/2, animation 11/11
  - 에디터: 스왑체인 · ImGuiGfx (`Shaders/56. ImGui.fx`) · 창 크기 따라감 · `-force-vulkan` · `nova open --graphics vulkan` · 실패하면 DX11. Graphics API 메뉴에서 고를 수 있음 (시험 단계). 창 밖 ImGui 창(뷰포트)은 Vulkan 에서 꺼 둠
  - 빌드한 게임: Player Settings 에 Vulkan → dxcompiler · SPIR-V 캐시를 넣고 Vulkan 으로 실행 확인
  - 다음: Release 성능 비교, 장벽 다듬기, 그다음 안드로이드 (사용자 확인 후)
  - 사용자 보고로 고친 것: Animator 창이 360 px 보다 좁으면 `std::clamp` Debug assert (`AnimatorEditorWindow.cpp`), 같은 꼴의 `AnimatorIK.cpp` (길이 0 뼈)
  - 담당 파일: 새 `Source/Graphics/Vulkan/*` · `ThirdParty/Vulkan/` · `Source/Editor/ImGuiGfx.*` · `Shaders/56. ImGui.fx` · `docs/VULKAN_BACKEND.md`, 고친 공용 파일 `Source/Graphics/RHI/Gfx.h` (`GfxObject::Api()`) · `Rhi.cpp` · `DX11/GfxDx11.cpp` · `OpenGL/GfxGL.cpp` · `Common/GraphicsAPI.h` · `GraphicsBackendFactory.cpp` · `GraphicsSettings.cpp` (`-force-vulkan`) · `ShaderCross/ShaderCross.*` · `Source/Platform/App.*` (InitVulkan · Present · 백버퍼) · `Editor/EditorGUIManager.cpp` · `EditorGUIResourceManager.cpp` · `CliCommands.cpp` (`screenshot-editor` 한 줄) · `Build/BuildPipeline.cpp` · `CMakeLists.txt` · `Editor/EditorApp.cpp` · `Tools/NovaCli/main.cpp` · `Tools/tests/run_tests.ps1` (`Suite-Vulkan`, `Capture-Scenes -Vulkan`) · `Tools/tests/common.ps1` (`Start-TestEditor -Vulkan`, 감시에 `device lost`)
  - Codex 에게: `GraphicsAPI::Count` 가 3 이 되어 API 를 도는 화면 (Hub 의 API 배지 · 에디터 Graphics API 메뉴 · Player Settings) 에 Vulkan 이 보인다. `GfxObject::Native() == nullptr` 를 "OpenGL" 로 보는 새 코드는 `Api() == GfxApi::OpenGL` 로, `App::IsOpenGL()` 분기에는 `IsVulkan()` 도 (백버퍼 텍스처를 쓰는 쪽)
- 이전: 모델 끌어 놓기 · TAA + SMAA · APV 2 단계 완료 · push `19591a4`
- 단계: **LOD Group (`83226d8`) · Screen Space Reflection 완료** — 사용자 지시 "LOD Group … Screen Space Reflection … 진행"
- 이전 단계: Depth of Field · Motion Blur (`11002fe`, push), Adaptive Probe Volume (`efbab1e`, push) — 아래 기록
- 기준 커밋: `564e00a`
- 진행 중 (10월 4일, 사용자 지시): ① FBX · 모델을 끌어 놓으면 Mesh Filter + Mesh Renderer 계층 (모델에 `_LOD0` · `_LOD1` 노드가 있으면 LOD Group 자동) ② 카메라 Anti-aliasing TAA + SMAA ③ APV 2 단계 (발광 재질 빛, 틈 · 모서리 빛 줄)
  - ① 모델 끌어 놓기 **완료 · 커밋** — 새 `Source/Editor/ModelPlacement.*`, `SceneHierachyEditorWindow.cpp` · `SceneEditorWindow.cpp` (끌어 놓기), `GameObjectFactory.*` (`AttachChild` 공개 도우미), `MeshRenderer.*` (`SetMaterialPath`), `EditorApp.cpp` · `Tools/NovaCli/main.cpp` (CLI `modelfile`), 검사 `modelplace` **6/6** (`Tools/tests/make_lod_gltf.py`), 문서 `docs/MODEL_PLACEMENT.md`
  - ② TAA + SMAA **완료 · 커밋** — `Source/Scene/Camera.*` (설정), `Source/Editor/EditorApp.*` (지터), `Source/Graphics/DX11/PostProcessPass.*` · `Shaders/41. PostProcess.fx`, 검사 `antialiasing` **7/7**, 회귀 render · gfx · depthoffield 15/15, OpenGL 확인, 문서 `docs/ANTI_ALIASING.md`
  - ③ APV 2 단계 **완료 · 커밋** — 발광 재질 (판과 겹칠 때만 발광 찍기 → 발광 복셀 → 다시 비추기), 방 모서리 빛 줄 (면 평면 6 칸 · 같은 축 AND / 다른 축 OR). `Source/Graphics/DX11/ProbeVolumes.cpp` · `UMaterial.*` (`EmissionLinear`) · `SceneCulling.*` (`TrackedBounds`) · `Shaders/32` · `55`, 검사 `probevolume` **9/9**, 회귀 61/61, OpenGL 확인
  - 고칠 파일 (예정): `Source/Editor/Windows/SceneHierachyEditorWindow.cpp` (끌어 놓기), `Source/Scene/GameObjectFactory.*`, `Source/Graphics/DX11/PostProcessPass.*` · `Shaders/41. PostProcess.fx` (TAA · SMAA), `Source/Graphics/DX11/ProbeVolumes.*` · `Shaders/55. ProbeVolume.fx` · `32` (APV), `Source/Editor/EditorApp.cpp`, 검사 · 문서
- Codex 분담: Joint 2D 완료 (작업 폴더에 아직 미커밋), 다음 후보 Tilemap — 공동 명세의 "Tilemap 명세 (Codex)" (사용자 확인 후 착수)

## Screen Space Reflection 담당 파일 (커밋에 넣은 것)

| 파일 | 왜 |
|---|---|
| 새 `Source/Graphics/DX11/ScreenSpaceReflection.*` | Volume 값 · 지난 프레임 장면 색 (밉) · 이펙트에 넣기 |
| `Source/Graphics/Common/VolumeProfile.cpp` | `ScreenSpaceReflection` 효과 (HDRP 이름) · 켜짐 규칙 |
| `Shaders/32. InstancedBasic.fx` | `cbScreenSpaceReflection` · `ScreenSpaceReflection()` · ShadeLit 반사 항 (꺼지면 예전과 같음) |
| `Source/Editor/EditorApp.cpp` | 두 뷰에서 Prepare · Bind (CustomShaders 포함) · StoreHistory |
| **공용** `Tools/tests/run_tests.ps1` | 새 `Suite-SSR` + 목록 · switch · 도움말에 한 단어 |
| 문서 | 새 `docs/SCREEN_SPACE_REFLECTION.md`, README · AGENT_HANDOFF 한 줄씩, 공동 명세의 Claude 줄 |

## Screen Space Reflection 검증 (Debug, 독립 빌드 `E:\NovaTest\ClaudeDecalEngine`)

- `ssr` **4/4**: 켜기 (반사 자리 빨강 0 → 100 %) · 매끈함 0.8 = 없음 · 카메라 옮긴 첫 프레임 = 안정 프레임 · Game 뷰
- 회귀 `render · gfx · shadergraph · decal · reflectionprobe · probevolume · lodgroup` **62/62**, OpenGL 같은 그림
- 쇼케이스 188

## Codex 에게 (Screen Space Reflection)

- 32 의 ShadeLit 반사 항이 `ScreenSpaceReflection` 을 거친다 (Volume 에서 켜지 않으면 `gSsrParams.x = 0` 으로 바로 돌아옴). 32 컴파일이 Debug 에서 5 초 남짓
- 새 렌더 경로 (다른 뷰) 를 만들면 `ScreenSpaceReflection::Prepare(..., capture=true, ...)` 로 끄거나 Bind 를 부르지 않으면 이전 뷰 값이 남을 수 있다

## LOD Group 담당 파일 (커밋에 넣은 것)

| 파일 | 왜 |
|---|---|
| 새 `Source/Scene/LODGroup.*` | 컴포넌트 (Unity 필드), 뷰마다 LOD 고르기, Inspector LOD 막대, CLI `nova lod` |
| `Source/Scene/Component.h` | 렌더러마다 `LodStamp` · `LodHidden` · `LodShadowHidden` · `LodFade` · `LodFadeBelow` (기본값 = 예전과 같음) |
| `Source/Scene/SceneCulling.*` | `LodStamp` · `ShadowPass`, `IsVisible` 이 LOD 숨김도 본다 |
| `Source/Scene/MeshBatcher.*` | `BeginView(capture)`, Collect 에서 `LODGroup::SelectForView`, 크로스페이드 렌더러는 묶음 밖에서 `gLodFade` 로 (묶음 그리기를 람다로) |
| `Shaders/32. InstancedBasic.fx` · `Shaders/28. SsaoNormalDepth.fx` | `gLodFade` + `LodFadeClip` (PS 첫 줄 — 기본 0 이면 그대로) |
| `Source/Editor/EditorApp.cpp` | `BeginView(probe)`, `LODGroup::RegisterEditor` |
| `Tools/NovaCli/main.cpp` | `lod` op 명령 + 도움말 한 줄 |
| **공용** `Source/Editor/AddComponentMenu.cpp` | Rendering > LOD Group 한 줄만 stage |
| **공용** `Tools/tests/run_tests.ps1` | 새 `Suite-LODGroup` + 목록 · switch · 도움말에 한 단어 |
| 문서 | 새 `docs/LOD_GROUP.md`, README · AGENT_HANDOFF · NOVA_CLI 한 줄씩, 공동 명세의 Claude 줄 |

## LOD Group 검증 (Debug, 독립 빌드 `E:\NovaTest\ClaudeDecalEngine`)

- `lodgroup` **7/7**: 기본값 · 거리별 LOD 0 / 1 / 2 / Culled · Cross Fade 반반 + 빈 픽셀 0 · Animate Cross-fading · Game / Scene 뷰 따로 · 끈 그룹 = 모두 · 저장 → 다시 열기
- 회귀 `render · gfx · shadergraph · decal` **39/39**, OpenGL 크로스페이드 확인 (빈 픽셀 0)
- 쇼케이스 187

## Codex 에게 (LOD Group)

- `SceneCulling::IsVisible` 이 LOD Group 이 숨긴 렌더러도 false 를 준다 (LOD Group 이 없으면 예전과 같음). 렌더러를 직접 그리는 새 경로를 만들면 이 검사를 쓰면 LOD 를 따른다
- `Component` 에 LOD 필드 5 개가 늘었다 (직렬화 안 함)
- `MeshBatcher::BeginView` 에 `capture` 인자 (기본 false)

## Depth of Field · Motion Blur 담당 파일 (커밋에 넣은 것)

| 파일 | 왜 |
|---|---|
| `Source/Graphics/Common/VolumeProfile.*` | `DepthOfField` · `MotionBlur` 효과 (URP 이름 · 필드), 켜짐 규칙, `ShowIfKey/ShowIfValue` (모드별로 보이는 칸) |
| `Source/Editor/VolumeEditor.cpp` | ShowIf 가 맞지 않는 칸 숨기기 |
| `Source/Graphics/DX11/PostProcessPass.*` | `DepthOfField` · `MotionBlur` 패스, 깊이 · 카메라 행렬, 지난 프레임 카메라 (프레임 번호로 한 번) |
| `Shaders/41. PostProcess.fx` | Gaussian 4 패스 · Bokeh 3 패스 · Motion Blur (픽셀 셰이더만) |
| `Source/Editor/EditorApp.cpp` | 두 뷰의 후처리에 깊이 · View · Proj 넘김 (7 줄) |
| **공용** `Tools/tests/run_tests.ps1` | 새 `Suite-DepthOfField` + 목록 · switch · 도움말에 한 단어 |
| 문서 | 새 `docs/DEPTH_OF_FIELD_MOTION_BLUR.md`, README 후처리 줄, AGENT_HANDOFF 한 줄, 공동 명세의 Claude 줄 |

C# 네이티브 표 · `Source/Physics2D/` · 씬 파일 · `CliCommands.cpp` · `GameObject.*` · Codex 의 Joint 2D 파일은 고치지 않았다. `docs/AI_COLLABORATION.md` 는 Codex 의 미커밋 변경이 같이 있어 커밋에는 내 줄만 넣었다.

## Depth of Field · Motion Blur 검증 (Debug, 독립 빌드 `E:\NovaTest\ClaudeDecalEngine`)

- `depthoffield` **5/5**: Gaussian (먼 상자 80.9 → 7.2, 가까운 상자 그대로) · Bokeh 초점 20 m (가까운 상자 46.4 → 6.7, 먼 상자 그대로) · Mode Off = 같은 그림 · Motion Blur Game 뷰 번짐 · Scene 뷰 Motion Blur 없음
- 회귀 `render · gfx` **10/10**, OpenGL 짧은 확인 (Gaussian · Bokeh · Motion Blur 같은 그림)
- 쇼케이스 186 (육각 보케)

## Codex 에게 (Depth of Field · Motion Blur)

- `PostProcessPass` 앞에 DoF · Motion Blur 가 들어갔다 — 켜지 않으면 예전과 같다 (Bloom · Uber 는 같은 입력)
- `VolumeParameter` 에 `ShowIfKey` · `ShowIfValue` 가 생겼다 (기본 빈 값 = 늘 보임)
- 다른 Volume 효과를 더할 때 `VolumeComponent::Create` 의 형식 목록 (알파벳 순) 에 같이 넣으면 된다

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
