# NOVA Claude 작업 상태

- 갱신 시각: 2026년 10월 10일 — **열린 월드 바람 · TAA 기본 · 그림자 캐스케이드 8 개 (2 km) · 임포스터 그림자** (사용자 지시: "흔들리는 효과, 기본 카메라 TAA, 쉐도우 캐스케이드 8 단계, 임포스터에도 그림자, 그림자 거리 오픈월드 고려해서 2000")
  - 바람: `Shaders/67. BatchWind.fx` (모델 높이로 숙임 · 돌풍 물결 · 잎 떨림) — 바람 묶음만 따로 기법 (`BatchWindTech` · `BatchGBufferWindTech` · `NormalDepthWindBatchTech` · `BuildShadowMapInstancingWindTech` + Alpha Clipping 짝). 처음엔 VS_Batch 에 넣었다가 Debug (최적화 없는 셰이더) 에서 Mesh Renderer 묶음도 깊이 프리패스와 어긋나 (render 의 Culling DX = GL 실패) 되돌림, `MeshBatcher::ExternalBatch::Wind` · `WindShape`, PCG Volume Wind Strength · Direction (× 날씨 바람), `PCG::MeshAsset::Foliage` (Alpha Clipping 재질이 있는 모델만)
  - TAA: `Camera::m_antiAliasing` 기본 3 (새 카메라)
  - 그림자: 캐스케이드 1 ~ 8 (`ShadowMap::SetCascadeCount` — 조각 = 빛 × 캐스케이드 수, 셰이더 32 · 46 은 gShadowParams.x 간격), Volume Split 4 ~ 7, 먼 캐스케이드 캐시의 **뒤집힌 조건 고침** (차례가 아닌 프레임마다 다시 그리던 것), `CasterPass::Inner` (앞 캐스케이드가 맡는 구 — PCG 가 셀을 뺀다), PCG 묶음 버퍼 서명을 셀 버퍼 번호로 (가만히 있으면 복사 0 번), 캐스케이드마다 묶음 버퍼
  - 열린 월드: `OpenWorldShadows.volumeprofile` (2000 m · 8 개) Global Volume, 나무 Shadow Distance 2000 (빌보드 LOD 가 먼 그림자)
  - 군중 임포스터 그림자: `BuildShadowMapSkinnedImpostorTech` (빛을 바라보는 사각형, `gSkinView.w` = 방향), 임포스터 단계는 그림자에 보여도 자세 · 팔레트 없음 (`SkinnedInstancing::ShadowImpostors`, Animator)
  - 에디터 끊김: C# 스크립트 변경 확인 (매초 Assets 훑기) 을 작업 스레드로 (`ScriptEngine` — 큰 프로젝트에서 25 ~ 35 ms), `nova perf` 에 worstFrame
  - 결과 (Release, 숲): 처음 29 ms → 14.5 ~ 15 ms (그림자 GPU 15 → 3 ms, 캐스케이드 다시 그림 6.9 → 3.1 / 프레임). Debug 셰이더 (최적화 없음) 에서 PCG 잎 가장자리 배경색 점 — 바람을 끈 기법에서도 같아 예전부터 (원인 아직), Release 0
  - 검증: Debug 12 스위트 (pcg · crowd · antialiasing · render · deferred · behaviour · shadergraph · motionvectors · occlusion · gfx · vulkan · d3d12) — 바람 기법 분리 · occlusion 테스트의 카메라 TAA 끔 뒤 render · occlusion · shadergraph · pcg · deferred 59/59
- 이전: 2026년 10월 10일 — **초대형 열린 월드 (32 km) + PCG** (사용자 지시: "SeedMesh 나무 · 풀 · 식생으로 초거대 오픈월드, 반드시 PCG — 언리얼처럼 딸깍으로 규칙을 정하고 자유롭게 편집하면 자동으로 채워지게, 32 km x 32 km")
  - World Terrain (`Source/Terrain/WorldGen.*` 높이 · 바이옴 함수, `WorldTerrain.*` 1 km 타일 1024 개 스트리밍 513 · 129 · 33, 작업 스레드 · 캐시 · 스커트), `TerrainData::SetGenerated`, TerrainRenderer 스커트 · 지형 전체 절두체 컬링 · 레이어 높이 배열을 레이어 조합마다 한 장 (예전 타일마다 21 MB)
  - PCG (`Source/PCG/` — PCGGraph 노드 11 종 · PCGExecutor 셀 실행 · PCGMeshAsset _LODn 모델 · PCGVolume Runtime Generation), `MeshBatcher::SetExternalSource` · `CreateInstanceBuffer` (바깥 인스턴스 · 미리 만든 버퍼), 셀 버퍼를 GPU 복사로 모아 묶음 하나, 가까운 무거운 모델은 인스턴스마다 LOD, `PS_BatchFace` (양면 잎 뒷면 법선)
  - PCG Graph 창 (`Source/Editor/Windows/PCGGraphWindow.*`, imgui-node-editor), `.pcg` 에셋, GameObject > Open World (32 km · 8 km · PCG Volume), CLI `create open-world` · `world` · `pcg`
  - Unity 환경 가져오기 `Tools/unity_import/unity_env_import.py` (FBX .meta externalObjects 재질, TIFF 의 숨은 알파 — tifffile, `.lod.json`, 지형 레이어), 테스트 프로젝트 `E:\NovaTest\OpenWorld` (SeedMesh 378 FBX)
  - 모델 처음 읽기: 텍스처 디코드 · 모델 캐시를 작업 스레드에서 미리 (`Utils::PrefetchReady`, `ResourceManager::HasTexture`), FBX 재질 칸 `.materials.json`
  - 결과 (Release): 숲 · 하늘 · 초원 · 사막 15 ~ 17 ms (인스턴스 16 ~ 29 만), 모델 읽기 7.9 → 0.41 s
  - 문서 PCG (`docs/PCG.md`, 그림 `docs/images/pcg.webp`), `nova create` 가 `--size` · `--graph` 를 넘긴다
  - 검증: Debug 회귀 15 스위트 — 새 `pcg` 스위트 (8 km 월드 64 타일 · 같은 규칙 = 같은 결과 · 밀도 두 배 = 수 두 배 · 스포너 끄기 · 그래프 JSON) 6/6, 나머지 스위트 모두 통과
- 이전: 2026년 10월 10일 — **군중 (스킨드 메시 · 애니메이션 대량 배치) 자동 최적화 + 큰 씬 편집기 경로 + Unity 캐릭터 · 애니메이션 가져오기** (사용자 지시: "스킨드 메쉬 · 애니메이션 임포스터 + 원거리 단순화 + 1000 명 이상 + 인스턴스" → "토탈워 같은 5 만 · 10 만 — 논문 · 최신 기술 확인, 기본 옵션으로" → "임포스터도 자동" → "전투 셰이더 말고 범용 셰이더" → "애니메이션 FBX 를 천 개 · 만 개 드래그해도 자동" → "TheTalesFactory 캐릭터로 테스트 프로젝트" → "애니메이션도 Unity 에서 정리해 가져오기 · 종류별 애니메이션 에셋 패키지로 따로" → "먼 휴머노이드는 본 개수도 줄이기")
  - 메시 LOD · 본 줄이기 `Source/Animation/SkinnedLod.*` (meshoptimizer `ThirdParty/meshoptimizer`), 범용 스킨 인스턴싱 `Source/Scene/SkinnedInstancing.*` + `Shaders/66. SkinInstancing.fx` (32 · 26 · 28 · 63 의 기법), 자동 임포스터 · 클립 굽기 `Source/Scene/CrowdAnimation.*`, 모듈형 부위 합치기 · Auto LOD 기본 켬 (`SkinnedMeshRenderer`), Animator 자세 모으기 · 갱신 빈도 · 임포스터만이면 자세 건너뜀 (`Packages/com.nova.animation`)
  - 큰 씬 (오브젝트 17 만 개) 경로: Update 목록 · 컬링 조밀 목록 · Mesh Batcher 렌더러 캐시 · Scene 카메라 옛 컬링 제거 · 물리 훑기 · LateUpdate IsAlive · Scene 오브젝트 목록 제곱 경로 (`GameObject::SceneListed` · `PendingDelete`) · Play 스냅숏을 Undo 루트 캐시에서 · Undo 돌아가며 직렬화 한도 · 프리팹 인스턴스 병렬 미리 합치기 · 메시 바인드 캐시 · 제목 줄 dirty 검사
  - `SceneCulling::Stamp` 등 헤더 inline 변수 → NovaCore 하나로 내보냄 (패키지 DLL 이 사본을 봤다), SkinnedLod 정적 목록 경쟁 (LOD 만들기 잡 충돌) 고침
  - Unity 가져오기 `Tools/unity_import/` (NovaExport.cs · unity_character_import.py · unity_anim_package.py), 애니메이션 라이브러리 패키지 `E:\NovaAssets\AnimationLibrary` (저장소 밖 — 에셋 스토어 라이선스, 4760 클립 · 13 범주), 테스트 프로젝트 `E:\NovaTest\TalesTest` (MedievalWomen 27 변형 + 군중 컨트롤러)
  - `nova crowd spawn|clear|info|lod|instancing` (`--prefabs` 와 `--controller` 함께 — 프리팹 Animator 컨트롤러를 바꾼다)
  - 결과 (Release): 기본 캐릭터 1000 명 64.9 → 6.5 ms, 모듈형 1 만 명 284 → 37 ms (27 fps), 편집기 1 만 명 spawn 38.6 → 16.8 s · Play 2.3 s · Stop 178 → 29 s
  - Animator Auto: 이름이 맞아도 바인드 자세가 다르면 Humanoid 리타깃 (Unreal 마네킹 클립이 Tales 캐릭터를 눕혔다), 실행 중 만든 오브젝트의 대기 컴포넌트가 붙지 않던 것 (LastUpdate 문 — `GameObject::s_PendingComponents`)
  - 검사: 새 스위트 `crowd` 5/5, 관련 18 스위트 (cli · animation · scenes · motionvectors · occlusion · physics · physicssync · recovery · starter · ragdoll · cloth · clothskin · memory · transform · streaming · modelplace · lodgroup · crowd) Debug 모두 통과. Showcase 282
  - 문서 CROWD · UNITY_IMPORT
- 이전: 2026년 10월 10일 — **대규모 레벨 블록아웃** (사용자 지시: "기본 패키지에 프로토타입 텍스쳐 (미터 격자, 흰색 ~ 어두운 회색)" → "프로토타입 패키지 — 다양한 다각형, 오픈월드 판타지 (지붕 · 창문 …)" → "성벽 · 길 스플라인 배치 (휘게 / 반복)" → 영상 Volcanic Heist 같은 거대한 규모. 그다음 성능 추천 1 · 2)
  - `Resources/Packages/Prototype` (텍스처 · 재질 5 색, GLB 메시 + 프리팹 57 — Mesh Collider), `Tools/prototype/make_prototype.py` · `bake_prefabs.ps1`, `.gitignore` 가 엔진 Resources/Packages 를 빼던 것
  - World Space UV: `PbrMaterial.UVMode` (112 → 128 바이트), `32. InstancedBasic.fx` WorldBoxUV, `UMaterial` "WorldSpaceUV", MaterialInspector 토글
  - `Scene/PrototypeShape.*` (상자 · 계단 · 경사 · 원기둥 · 원뿔 · 아치 벽 · 원호 벽 — 크기를 숫자로, 메시는 저장 안 함), GameObject > Prototype
  - `Scene/Spline.*` (SplineContainer · SplineInstantiate — Repeat / Deform, 간격 · Fit · 앞 축 · Keep Upright · 콜라이더 · Bake), `Editor/SplineEditor.*`, GameObject > Spline, `GameObject::SetHideAndDontSave` (저장 · 계층 창에서 뺌, 누르면 조상 선택)
  - `Editor/SceneDimensions.*` (Scene 뷰 Selection Dimensions), CLI `prefab save|place` · `spline info|rebuild|bake` · `dimensions` · `create prototype|spline`
  - 검사: 새 스위트 `blockout` 7/7. 문서 PROTOTYPE · SPLINE, Showcase 279 · 280 · 281
  - 성능 추천 1 (씬 바꾼 뒤 끊김, 커밋 db698a8): 씬 JSON 해제 · 미리 디코드 이미지 해제를 작업 스레드로, 나무 GPU 미리 데우기 (SceneStreaming::QueueMainPrewarm) — frameAfterMs 50 ~ 56 → 28 ~ 30 ms
  - 성능 추천 2 (렌더 준비 바뀐 것만): 쪼개 쟀으나 남은 항목이 각 0.1 ~ 0.3 ms, 안전한 변경 (Collect 를 컬링 자리로) 은 번갈아 재도 차이가 흔들림보다 작아 되돌림. 결과는 RENDER_THREAD 의 아직 절에
- 이전: 2026년 10월 9일 — **물리를 데이터 지향으로** (추천 2) + **검사 안정성** (추천 4). 사용자 지시: 1 → 2 → 4 차례로 전부. 1 은 커밋 ec7f678 (푸시 전)
  - 물리 번호 `Component::s_PhysicsSerial` (`MarkPhysicsDirty`): Scene 오브젝트 목록 · SetActive · SetStaged · 부모 · 레이어 · 컴포넌트 지우기 · 켜기 (헤더 체크박스 포함) ·
    콜라이더 · Rigidbody · Character Controller 설정 함수 · fromJson (2D 포함), 인스펙터는 그리기 앞뒤 toJson 이 다를 때만 (`AffectsPhysics` — 처음엔 그릴 때마다 올려 선택해 둔 Play 가 늘 전체 훑기였다)
  - `PhysicsManager.cpp`: 소유자 묶음 (콜라이더 · 지켜볼 Transform 월드 번호) 을 기억하고 구조가 그대로면 바뀐 소유자만 서명, 돌아가며 64 · 50 스텝마다 전체 (안전망, `verifyFixes`),
    `CreateBody` 를 모아 `AddBodiesPrepare/Finalize`, 빼기는 `RemoveBodies/DestroyBodies`, Transform → 바디는 Rigidbody 바디 + 움직인 정적 소유자만, 동적 바디 목록 · FixedUpdate 호출 목록 캐시,
    `PrebuildStaged` (SceneManagerRuntime 이 루트를 다 지으면 — 형상은 미리 데우기 잡, 물리 월드 밖에 둔다: 씬을 바꾸면 월드를 새로 만든다), `nova physics` 의 sync 통계 · `--full-sync`
  - 2D: `Physics2DManager.cpp` · `Physics2DJoints.cpp` — 2D 컴포넌트가 없는 씬은 구조가 그대로면 훑지 않는다
  - C#: `Collider.isTrigger`, `BoxCollider.center · size`, `SphereCollider.center · radius`, `CapsuleCollider.center · radius · height · direction` (ScriptBindings COL_Get/Set)
  - 결과 (Release 도시 Play): Physics.Update 프레임당 0.519 → 0.035 ms, 씬 바꾸기의 물리 6 → 4 ms (미리 만든 형상 2780 개 모두 사용)
  - 검사: 새 스위트 `physicssync` 5/5 (바뀐 것만 = 전체 훑기와 같은 결과), physics · physicsasync · ragdoll · streaming 34/34. 문서 PHYSICS_SYNC
  - 검사 안정성 (**완료, 커밋**): `common.ps1` Wait-Until · Invoke-Nova 재시도 · Stop-StrayTestEditors, run_tests 의 스위트 예외 → 남은 검사 에디터 끄고 한 번 다시 (줄 번호 기록, NOVA_TEST_THROW_ONCE 로 확인),
    VRM 다시 가져오기 로그 · APV 프로브를 조건 폴링으로. 전체 회귀 708/709 — 실패한 APV -1 (정오 · 자정 모두) 은 폴링으로도 안 풀려 엔진을 고침: `ProbeVolumes.cpp` 자원 실패 뒤 다시 시도 (5 번, NOVA_DEV_APV_FAIL 로 확인),
    HRESULT 로그, 검사는 실패 때 probevolume info 를 남긴다 (원인 확정은 못 함 — 다시 나면 info 로). 문서 TESTING
- 이전: 2026년 10월 9일 — **Graphics Jobs 조사 → 그리기 준비를 데이터 지향으로** (추천 1). **완료 (커밋 ec7f678)**
  - 측정 먼저 (Release 도시 DX12, 렌더 스레드 켬): 그리기 명령 기록은 프레임당 0.12 ms 뿐 → 기록을 잡으로 나누는 대신, 메인 시간을 먹던 그리기 준비를 고쳤다 (사용자에게 알림)
  - `Scene.cpp`: 불투명 패스의 씬 전체 순회 (dynamic_cast + 가상 Render) → Skinned Mesh Renderer · 지형만 등록 목록에서 (CollectViewRenderers 가 컬링 자리 · Terrain::GetActiveTerrains)
  - `MotionVectors.cpp`: 컬링 자리를 차례로, 자리마다 뷰가 본 월드 · 번호 (번호가 같으면 비교 없음) — 1.05 → 0.09 ms
  - `SceneCulling.*` (EntryCount · EntryAt, ShadowStamp · EndShadowPass — 그림자는 Component::ShadowCullStamp 에 표시해 카메라 컬링을 다시 하지 않는다, 병렬 질의 2048 개부터), `EditorApp.cpp` (그림자 뒤 Cull → EndShadowPass)
  - `nova perf --top N` (CPU 구간 수, 구간마다 calls)
  - 결과: CPU 7.2 → 4.51 ms, 134 → 200 fps (GPU 2.8 ms 그대로). 렌더링 회귀 15 스위트 **145/145**. 문서 RENDER_THREAD (Graphics Jobs 조사) · CONCURRENCY_ROADMAP 5 단계 · TRANSFORM_SOA
- 이전: 2026년 10월 9일 — **씬 · 에셋 스트리밍 (LoadSceneAsync)**. **완료 (커밋 f333395, 푸시함)**
  - 사용자 요청: "비동기 씬 · 에셋 스트리밍 — 다음 씬을 백그라운드에서 미리, 한 프레임에 바꿔 끼우기, 텍스처 · 메시 비동기, Unity LoadSceneAsync · allowSceneActivation · progress"
  - 측정 먼저 (nova scenestream): 도시 바꿔 끼우는 프레임 831 ms (짓기 601 · Start 220 — Animator), 다음 프레임 2364 ms (Play 중 Undo 스냅숏 980 · 나무 생성 144)
  - `SceneManagerRuntime.cpp`: 맨 앞 비동기 작업을 프레임마다 예산 (Application.backgroundLoadingPriority — C# ThreadPriority, 2 · 4 · 10 · 50 ms) 만큼 루트 단위로 미리 짓기 (`Scene::LoadRoot`), 새 씬의 힙에 (런타임 씬이 앞 씬 힙에 지어지던 것도 고침), 루트는 `GameObject::SetStaged` (꺼진 것으로 — 나무 · 카메라 등 전역 목록이 지금 씬에 끼어들지 않게), Light 는 모았다가 바꿔 끼울 때
  - 미리 데우기 `Component::PrewarmStaged` + `SceneStreaming::QueuePrewarmJob` (작업 스레드, 끝나야 0.9): Animator Humanoid 아바타 (캐시 잠금), Tree 메시 · 잎 아틀라스 (생성 · RGBA8 변환은 잡, GPU 올리기만 메인), Terrain 나무
  - 에셋: 파싱 잡이 씬 · .mat 의 텍스처를 `Utils::PrefetchTexture` (디코드를 잡에서 — 메인은 끝났으면 올리기만, 디코드 중이면 기다리고, 시작 전이면 직접), 모델 캐시 파일 `PrefetchFile`
  - Undo: Play 중에는 씬 스냅숏을 뜨지 않는다 (UndoSystem TrackScene)
  - 결과: Release 바꿔 끼우는 프레임 86.5 · 71.9 → 10.8 · 11.0 ms, 가장 긴 프레임 105 · 91 → 29 · 30 ms. Debug 831 → 105 ms, 다음 프레임 2364 → 169 ms
  - 검사: 새 스위트 `streaming` 8/8, scenes 11/11. 문서 SCENE_STREAMING, Showcase 277
- 이전: 2026년 10월 9일 — **렌더 스레드 DirectX 12 · Vulkan + 데이터 지향 Transform · 컬링 (SoA · Job System · SIMD) + dxcompiler 종료 충돌**. **완료 (커밋 6b394d6, 푸시함)**
  - 사용자 요청: "렌더 스레드를 D3D12 · Vulkan 으로 넓히기" + "데이터 지향 Transform · 컬링 (SoA) 진행", 이어서 "Fix dxcompiler.dll crash at editor exit"
  - 렌더 스레드: `Source/Graphics/Common/SubmitThread.h` (무잠금 SPSC 링 + 작업 번호), D3D12 (`ExecuteCommandLists` + `Signal` · `Wait` · Present — 제출한 명령 목록을 할당기 칸에 묶어 Reset 을 늦춤), Vulkan (`vkQueueSubmit2` · `vkQueuePresentKHR` — 이미지 받기는 메인, 앞 Present 를 기다린 뒤). 켜고 끌 때 창마다 Present 번호를 지운다 (끄고 다시 켜면 멈췄다). Release: Vulkan 8.88 → 5.39 ms, D3D12 5.55 → 5.19 ms
  - Transform: `Source/Scene/TransformStore.*` (SoA — 로컬 TRS · 64 B 정렬 월드 행렬 · 월드 회전 · 크기 · 부모 + 세대 · 더러움 · 번호). Set = 배열 복사 + 아래 계층 더러움, Get = 깨끗하면 배열 · 더러우면 그 사슬만 (메인 아님 · ParallelFor 중이면 그 자리 계산), 프레임마다 Flush (겹치지 않는 하위 계층을 잡으로). 오일러 각은 요청할 때만. Transform.h/.cpp 는 UTF-8 로 정리 (공개 API 그대로)
  - 컬링: 렌더러 목록 (생성자 · 소멸자), 씬 표시, Component::s_BindingSerial (컴포넌트 붙이기 · 메시 바꾸기) 이 같고 월드 번호가 같으면 건너뜀, 움직인 것은 잡이 로컬 상자로 새 상자, 지금 칸에 맞으면 다시 넣지 않음, SoA 상자 + SIMD 4 개씩, 큰 장면은 질의를 잡으로. 돌아가며 512 개는 메시를 실제로 확인 (놓친 경로 자가 복구, Debug 는 로그)
  - Jobs: `EnterParallel/LeaveParallel/InParallel` (ParallelFor 구간 — 이때 Transform 늦은 계산은 배열을 고치지 않는다)
  - dxcompiler 종료 충돌 (10월 1일부터 DX12 · Vulkan 편집기가 끝날 때마다 0xc0000409 + 26 MB 덤프): 정적 ComPtr 이 DLL 이 떼어진 뒤 Release → App 이 `ShaderCross::Shutdown` 으로 먼저 놓고, 정적 소멸 때는 Detach. 확인: 충돌 기록 · 덤프 0, 종료 4 → 1.1 초
  - 지난 커밋의 Release 빌드 오류 고침: AllocTracker::Start 의 Release 쪽 정의 서명
  - 검사: 새 스위트 `transform` 4/4 (계층 9300 · 잡 읽기 106200 · 컬링 60000 결정 차이 0), renderthread 14/14 (DX12 · Vulkan 각 4), Transform 에 닿는 14 스위트 139/139. 문서 TRANSFORM_SOA · RENDER_THREAD, Showcase 276
- 이전: 2026년 10월 9일 — **메모리 알로케이터 · 씬 힙 (씬 전환 단편화 0) · 프레임 아레나 · 거짓 공유**. **완료 (커밋 776eb1a, 푸시함)**
  - 사용자 요청: "커스텀 메모리 알로케이터 (Linear/Stack/Pool/Buddy), 캐시 라인 (64 B) 정렬 · False Sharing 방지, 씬 전환 시 단편화 제로화 설계"
  - `Source/Core/Allocators.*` (Region · Linear (무잠금 CAS) · Stack · Pool · ConcurrentPool · Buddy · CacheAligned · pmr 어댑터), `MemoryHeaps.*` (씬마다 256 MB Buddy + 크기별 Pool 16, 활성 힙 = 불러오는 씬 또는 현재 씬, 주소로 놓기, 남은 힙 + 꼬리표별 누수 보고), `FrameArena.*` (16 MB x 2 번갈아), `AllocTracker.*` (Debug CRT 훅 — 프레임당 할당 · 구간 · 호출 스택), `AllocatorTests.cpp` (CLI memory info|test|allocs)
  - 연결: Scene 이 힙을 갖고 ~Scene 이 돌려준다, Scene::Load = ActiveScope, GameObject operator new/delete, AddComponent · 팩토리 = Heaps::MakeShared (allocate_shared)
  - 찾아 고친 누수: Transform 부모 ↔ 자식 shared_ptr 순환 (→ _parent weak_ptr), LightManager::DeleteLight (정렬 목록 · 건너뛰기)
  - 프레임당 할당 (도시, Debug) 5534 → 1164: Render Graph Publish = 목록 바꿔치기 (JSON 은 요청 때), VolumeStack::Reset = 기본값 캐시, 나무 · 디테일 메시 키 버퍼, Render Graph 이름 const char*, 프레임 아레나 (클러스터 · 투명 패스)
  - 거짓 공유: JobSystem 전역 · Job (alignas 64) · RenderThread 번호 · Profiler 스레드 버퍼. 측정 4 배
  - 검사: 새 스위트 `memory` 11/11 (씬 전환 8 번 매번 한 덩어리 반환 · 남은 힙 0 · 프로세스 메모리 안 자람 · Play/Stop). 문서 MEMORY_ALLOCATORS, Showcase 275
  - 전체 회귀: 엔진이 빨라져 motionvectors 의 모션 블러 테두리 검사 (한 프레임에 움직인 거리) 가 프레임 속도에 따라 떨어졌다 → C# `Application.targetFrameRate` 를 네이티브로 (Unity 처럼: 재생 중에만 프레임을 맞춘다, Stop 하면 -1, 고해상도 타이머로 기다림) 만들고 검사가 60 fps 로 잰다. motionvectors 12/12 (테두리 29). 나머지 실패 (ssao 메모리 · DEVICE_HUNG 등) 는 다시 돌리면 통과하는 일시 오류
- 이전: 2026년 10월 9일 — **동시성 로드맵 4 단계: 렌더 스레드 (Multithreaded Rendering, DX11)** + D3D11 디버그 층 오류 #343 · #388. **완료 (커밋 b66e1e7, 푸시함)**
  - `Source/Graphics/DX11/RenderThread.*`: 메인 = DxContext 가 deferred context 에 기록, 프레임 끝 FinishCommandList(TRUE) → 무잠금 SPSC 링 → 렌더 스레드가 ExecuteCommandList · Present (한 프레임 핑퐁). Sync (Map READ · 캡처) · Flush, GetData 는 ID3D11Multithread 아래 immediate, Map(READ, DO_NOT_WAIT) 는 복사한 목록 번호로 (Sync 없이), DONOTFLUSH GetData 는 넘기지 않음, deferred UpdateSubresource 우회, ImGui DX11 훅 (컨텍스트 · 뷰포트 Present · 크기 바꾸기 전 Sync), Dx11Rhi 가 그때그때 컨텍스트 (예전: 잡아 둔 immediate → 장치 제거), Profiler GPU 프레임 쿼리를 Present 전에 닫음, 진단 (D3D11 디버그 메시지 · 2 초 대기 · Sync 자원) → Editor.log
  - 설정: RenderPipelineSettings::MultithreadedRendering (GraphicsSettings.json, 기본 꺼짐), Project Settings > Graphics 토글, CLI `renderthread info|set|reset`, NOVA_RENDER_THREAD=1, NOVA_D3D11_DEBUGLOG=1
  - 고친 것: Push 의 해제 뒤 읽기 (멈춤), rhi-test · gfx-test 는 시험 동안 렌더 스레드를 끈다
  - D3D11 #343 (인스턴싱 VS 입력에 INSTCOLOR · INSTSURFACE · INSTEMISSION — SV_InstanceID 레지스터 맞춤, 26 · 28 · 32), #388 (뷰 깊이 버퍼를 렌더 타깃 (밉) 크기와 꼭 같게, 크기마다 캐시 4 — 뷰포트 크기로 하면 APV 아틀라스 찍기가 깨졌다) — 도시 디버그 메시지 40 → 0
  - 검사: 새 스위트 `renderthread` 6/6, 렌더 스레드 강제 그래픽 27 스위트 274/275 (rhi-test 는 고침 → 통과), 넓은 회귀 33 스위트 337/342 → probevolume 고침 뒤 probevolume · reflectionprobe 18/18
  - Release 측정: 렌더 스레드는 이 PC (NVIDIA, 드라이버 명령 목록) 에서 빨라지지 않는다 (도시 0.94 배 · 숲 1.07 · Materials 0.89) — 기본 꺼짐 유지, 문서에 그대로
- 이전: 2026년 10월 9일 — **동시성 로드맵 2 · 3 단계: 엔진에 Job System 적용 + 물리 ↔ 렌더링 겹치기**. **완료 (커밋 803fb82, 푸시 전)**
  - 2 단계: Jolt 를 `JoltNovaJobSystem` 으로 (엔진 일꾼 위), MeshBatcher.Collect · SceneCulling 훑기 병렬 (읽기) + 차례로 (재질 · 캐시 · 옥트리), Forward+ 클러스터 짓기 병렬 (같은 결과), `Jobs::Async` (Background Future) 로 씬 읽기 · 지형 · 바이옴 · 셰이더 그래프, VT 페이지 읽기 = SPSC 링 둘 + 잡, `TaskSystem::Post` (MPSC), Background 동시 실행 상한. Release: 도시 프레임 1.10 배, Collect 1.42, Culling 1.46, 물리 2.18 배
  - 3 단계: `PhysicsSettings::AsyncSimulation` (Simulate During Rendering, 기본 꺼짐, NOVA_PHYSICS_ASYNC=1 로 강제), StepBegin / SimulateAndCapture (핑퐁 버퍼) / StepEnd, CompleteAsync (프레임 시작 · 물리 API 35 곳 · FlushDestroyed), 접촉 = 무잠금 MPMC 링. 스위트 `physicsasync` 4/4, physics · cloth · ragdoll · wheel 31/31 (일반 · 겹치기 둘 다)
  - 검사: jobs · virtualtexture · scenes · shadergraph 66/66, forwardplus · occlusion · lodgroup · render · material 44/44. 문서 JOB_SYSTEM (엔진 적용) · ASYNC_PHYSICS, Showcase 274
- 이전: 2026년 10월 8일 — **동시성 로드맵 1 단계: Job System (작업 훔치기 · 파이버) + 무잠금 자료 구조**. **완료 (커밋 15f2fe1, 푸시 전)**
  - 사용자 요청: "Task/Fiber 기반 Work-Stealing Job System, 무잠금 큐, 메인-렌더-물리 스레드 간 락 없는 데이터 핑퐁/이중 버퍼링" — docs/CONCURRENCY_ROADMAP.md (4 단계)
  - `Source/Core/LockFree.h` (SpscRing · MpmcRing (Vyukov) · WorkStealingDeque (Chase-Lev) · TripleBuffer), `Source/Core/JobSystem.*` (일꾼 = 코어 − 1, 덱 + 우선순위 공용 링, Counter (Busy 로 수명), Windows 파이버 128 — 기다리면 내려놓고 다른 일꾼이 이어 돌림, 다른 곳은 돕기, 웹 · 인라인), App 에서 Init · Shutdown · OnFrame
  - Profiler: 메인 밖 스레드 구간 (스레드마다 SPSC 링 → EndFrame), Timeline 스레드 줄 + 세로 넘기기, perf 결과 threads, 통계 Jobs/Executed · Stolen. `window profiler --category timeline|threads`
  - `JobSystemTests.cpp`: CLI `jobs info|test|bench|set|reset`, 새 스위트 `jobs` 23/23 (파이버 켬 · 끔). JobSystem.cpp · Profiler.cpp 는 /GT. 문서 JOB_SYSTEM · CONCURRENCY_ROADMAP, Showcase 273
- 이전: 2026년 10월 8일 — **렌더링 현대화 6 단계: Rendering Path (Forward · Forward+ · Deferred)**. **완료 (커밋 daea8b1, 푸시함)**. 로드맵 1 ~ 6 단계 모두 끝
  - `RenderPipelineSettings` 의 Rendering Path (GraphicsSettings.json `renderingPath`, 기본 Forward+, Forward = 클러스터 끔), Project Settings > Graphics > Rendering 드롭다운, CLI `renderpath get|set --path`
  - `Source/Graphics/DX11/DeferredRenderer.*`: 뷰마다 (Game · Scene) G-버퍼 4 장 (알베도 sRGB + AO · 메탈릭 · 스무스니스 · 표시 · 레이어 · 노멀 16F · 발광 16F), 전체 화면 조명 (역 ViewProj 로 월드 자리)
  - 셰이더 `32`: LitPS 의 표면 계산을 `LitSurfaceOf` 로 나눠 포워드 · G-버퍼가 같이 쓴다. `PS_BatchGBuffer` · `PS_DeferredLight` (ShadeLit + FinishLit 그대로, 레이어 · 안개 · Receive Shadows 는 G-버퍼 표시로)
  - `MeshBatcher::SetDeferredSplit` (GBuffer = 엔진 Lit 묶음만, ForwardOnly = 그 밖 + LOD 크로스페이드). EditorApp Game · Scene 그래프에 GBuffer · Deferred Lighting 패스 (빛 묶기를 bindFrame 람다로 — 포워드 Opaque 와 같이 씀), Opaque 는 디퍼드 장면을 읽고 그 위에
  - 새 스위트 `deferred` 12/12 (Deferred = Forward+ 최대 차 2, Scene 뷰도, Forward Only 상자, GL · Vulkan · DX12 = DX11). 넓은 회귀 27 스위트 269/269 (virtualtexture 의 flush 검사는 피드백을 멈추고 보도록 고침 — CLI 호출 사이에 다시 올라오던 타이밍 문제). 문서 DEFERRED_RENDERING · 로드맵 · README · NOVA_CLI · FORWARD_PLUS, Showcase 272
- 이전: 2026년 10월 8일 — **렌더링 현대화 5 단계: Streaming Virtual Texturing**. **완료 (커밋 c5ad92b, 푸시함)**
  - `Source/Graphics/DX11/VirtualTexturing.*`: 가져오기 설정 Virtual Texture Only → 타일 파일 (136² RGBA8, 테두리 4, sRGB 유지), 공용 물리 캐시 (형식 없음 + 선형 · sRGB 뷰, LRU, 가장 거친 밉 고정), 가상 텍스처마다 페이지 표 (조상 대체), 작업 스레드 읽기 · 프레임당 24 올리기
  - 피드백: Render Graph 의 VT Feedback 패스 (1/8 크기, 프리패스 깊이 비교, 지터, 2 프레임 뒤 읽기) — `Shaders/65. VirtualTexture.fx`. 셰이더 `32` 의 SampleVirtual (UseBaseMap == 2), 재질 · 효과에 VT 묶기, 대체 텍스처는 그림자 · 깊이 · 미리 보기에
  - CLI `vt info|page|flush|set`, 새 스위트 `virtualtexture` 8/8 (DX11 · GL · Vulkan · DX12 같은 그림), 넓은 회귀 23 스위트 216/216. 문서 VIRTUAL_TEXTURING, Showcase 271
  - 사용자 의견 (2026-10-08): 작업이 오래 걸린다 → 앞으로 바꾼 기능의 스위트만, 넓은 회귀는 6 단계 끝에 한 번
- 이전: 2026년 10월 8일 — **렌더링 현대화 3 · 4 단계: DirectX 12 백엔드 + 비동기 컴퓨트**. **완료 (커밋 6d47b4e, 푸시 전)**
  - `Source/Graphics/DX12/` (Gfx 층의 D3D12 구현 — Vulkan 백엔드와 같은 구조): 직접 큐 + 펜스, 서브리소스 상태 장벽 (버퍼는 목록마다 승격 · 감쇠), CPU 전용 힙 → 셰이더에서 보이는 링 (50 만 칸) · 샘플러 표 캐시, 단계마다 디스크립터 표 루트 시그니처, 업로드 링, PSO 캐시, 플립 스왑체인, GenerateMips (2D · 배열 · 큐브 · 3D), 쿼리 · 예측 · ExecuteIndirect, 디버그 층 → Editor.log. `ShaderCross::CompileEffectDxil` (DXC → DXIL + 리플렉션 이름 → 효과 바인딩, ShaderCache/DXIL)
  - 연결: `GraphicsAPI::DirectX12`, `-force-d3d12`, `nova open --graphics d3d12`, App `InitD3D12` (안 되면 DX11), ImGuiGfx 뷰포트, 허브 · 빌드 (DXIL 캐시 포함), `nova d3d12 shaders|gfx-test|rhi-test`, 검사 `Start-TestEditor -D3D12` · `NOVA_TEST_GRAPHICS=d3d12` (스위트 전체를 DX12 편집기로)
  - 비동기 컴퓨트: `GfxContext::Begin/End/WaitAsyncCompute`, DX12 컴퓨트 큐 · Vulkan 그래픽 패밀리의 두 번째 큐 (타임라인), Render Graph 가 AsyncCompute 패스를 컴퓨트 큐로 + 결과를 읽는 패스 앞에서 기다림, VFX 시뮬레이션을 그리기와 나눠 `VFX Simulation` 패스 (장면 텍스처 충돌이면 그래픽 큐), `rendergraph set --async`, Render Graph Viewer 에 큐 표시
  - 함께 고침: Vulkan 숨은 창 스왑체인을 매 프레임 다시 만들던 것 (표면 크기 ≠ 요청 크기)
  - 검사: 새 스위트 `d3d12` (DXIL 54/54 · 528 pass, gfx/rhi-test, 렌더 7 장면 DX11 = DX12, 디버그 층 0) · `vfx12`. DX12 편집기로 그래픽 21 스위트 통과, vulkan · vfxvk · occlusionvk · materialvk 46/46, render · gfx · rendergraph · vfx · vfxgl · particles 통과. 문서 DIRECTX12_BACKEND · ASYNC_COMPUTE · 로드맵 · VULKAN_BACKEND · NOVA_CLI · README, Showcase 269 · 270
- 이전: 2026년 10월 8일 — **렌더링 현대화 2 단계: Render Graph**. **완료 (커밋 5e95cff, 푸시 전)**
  - `Source/Graphics/Common/RenderGraph.*`: 패스 노드 (Builder Read · Write — 쓸 때마다 새 판 · Create · SideEffect · AsyncCompute), 결과 · Side Effect 에서 거꾸로 따라가 빼기 (Frostbite refcount), 임시 텍스처 풀 (수명으로 다시 쓰기, TrimPool), 패스마다 Profiler 구간, CLI `rendergraph info [--view]`
  - `EditorApp::RenderGameView` · `_Editor_OnSceneRender` 를 그래프로 (손으로 쓴 같은 순서 두 벌 → 패스 15 · 17 개). Motion Vectors 는 읽는 쪽 (SSAO 시간 누적 · TAA · MB Camera And Objects · 디버그 보기) 이 없으면 빠진다
  - Window > Analysis > **Render Graph Viewer** (`RenderGraphViewerWindow` — 패스 x 자원 R/W 표, CPU ms, 풀), CLI `window render-graph-viewer [--close]`
  - 검사 `-Only rendergraph` 5/5, 그래픽 회귀 21 스위트 196/196 (render · gfx · layers · reflectionprobe · probevolume · ssao · motionvectors · decal · vulkan · weather · ssr · depthoffield · antialiasing · renderingdebug · forwardplus · rendergraph · occlusion · material · vfx · linetrail · sprites). 문서 RENDER_GRAPH, Showcase 268
- 이전: 2026년 10월 8일 — **렌더링 현대화 1 단계: Forward+ (클러스터 조명)**. **완료 (커밋 393303f, 푸시 전)**
  - `ClusteredLighting` (16 x 9 x 24, CPU 로 짓기 — 깊이 조각마다 구 단면 사각형, RGBA32F 텍스처 하나), `LightManager` 의 AdditionalLight (앞의 4 개 밖, 최대 1024), 입자 빛도, 셰이더 `ShadeLit` 의 클러스터 루프, Rendering Debugger Additional Light Count, CLI `forwardplus info | set --enabled`
  - 함께 고침: 카메라 절두체 (Camera · EditorCamera 의 FrustumUpdate 가 투영 x 뷰 의 행에서 평면을 뽑아 빛을 대부분 걸러 냄), 그림자 맵을 빛 종류마다 배열 하나로 (OpenGL 샘플러 32 개 한도 — 지형 셰이더가 깨졌다, 12 → 3)
  - 검사 `-Only forwardplus` 6/6 (DX11 · OpenGL · Vulkan), render · reflectionprobe 23/23 (OpenGL log clean · Forest DX = GL 포함). 문서 FORWARD_PLUS · RENDERING_ROADMAP, Showcase 267
- 이전: 2026년 10월 8일 — **Motion Blur 품질: 타일 최대 속도** (사용자 요청 — Rendering Debugger 다음). **완료 (푸시함)**. 사용자가 이어서 요청한 세 가지 (Cinemachine · Rendering Debugger · Motion Blur) 끝
  - `Shaders/41. PostProcess.fx`: TileMax (32 x 32) → NeighborMax (3 x 3) → McGuire 2012 재구성 (깊이 · 표본 속도 무게, 가운데와 같은 픽셀 표본은 건너뜀 — 넣으면 이웃 픽셀끼리 줄무늬). `PostProcessPass::MotionBlur` 에 R16G16F 타일 타깃 둘, GPU 구간 "Motion Blur"
  - 검사 `-Only motionvectors` 12/12 (새 항목: 빨간 상자 바깥의 옅은 번짐 행마다 0 → 7 픽셀, 예전 방식은 구조상 0). 예전 그림으로 잰 기준: 흐린 상자가 오히려 좁았다 (89 → 83 px)
  - Release 비용은 아직 재지 않음 (Debug 는 셰이더 최적화 꺼짐)
- 이전: 2026년 10월 8일 — **에디터 Rendering Debugger** (사용자 요청 — Cinemachine 다음). **완료 (커밋 a06d79a, 푸시 전)**
  - `RenderingDebug` (`Source/Graphics/DX11/RenderingDebug.*`, `Shaders/64. RenderingDebug.fx`): None · Depth · Normals (World) · Ambient Occlusion · Motion Vectors · Probe Volume Lighting · Sampling — Scene · Game 뷰 함께, 후처리 뒤 전체 화면
  - Window > Analysis > Rendering Debugger (`RenderingDebuggerWindow`), Scene 뷰 툴바 bug ▾ > Debug View, CLI `debugview <mode> [--range] [--scale] | info`
  - `MotionVectors` 를 뷰마다 (Game · Scene) 따로 — Scene 뷰는 이 보기일 때만 그린다. APV 모드는 `ProbeVolumes::SetDebugView` (새 API)
  - 검사 `-Only renderingdebug` 10/10. 문서 docs/RENDERING_DEBUGGER.md, Showcase 265
- 이전: 2026년 10월 8일 — **Cinemachine 급 카메라** (사용자 요청: 가상 카메라 여럿 · 섞기 · 흔들림) + **검사 편집기를 창 없이** (사용자 요청). **완료 (커밋 71a2dcf, 푸시 전)**
  - `Packages/com.nova.cameras` 1.1.0: Unity Cinemachine 3 이름의 C++ 컴포넌트 — Brain (Priority · 같으면 늦게 켜진 것, Default · Custom Blends 7 모양, 섞는 중 바뀌면 Mid-Blend), CinemachineCamera (Lens · Target · Procedural Components 드롭다운), Follow (Binding Mode 6 · Damping) · Orbital Follow (구 · 세 고리 · 축) · Third Person Follow (벽 피하기 = RaycastAll), Rotation Composer (Screen Position · Dead Zone · Hard Limits) · Hard Look At · Rotate With Follow Target, Basic Multi Channel Perlin (프로필 9), Impulse Source · Listener
  - GameObject > Cinemachine (Camera · Follow · FreeLook · Third Person Aim — Main Camera 에 Brain), CLI `cinemachine info|create|priority|prioritize|enable|axis|blend|impulse|snap`, C# `NovaEngine.Cinemachine` (Runtime/Cinemachine.cs, 묶음 설정은 바로 쓰이는 class) + C# Cinemachine Input Axis Controller
  - 엔진: 편집 중 컴포넌트 갱신 `Component::_Editor_Update` 를 실제로 부른다 (`Scene::EditorUpdateScene`, App 루프에서 Play 가 아닐 때 — Unity [ExecuteAlways]), GameObject 메뉴가 패키지 항목의 하위 메뉴를 일반적으로 (`EditorExtensions::CreateMenuFolders`)
  - **창 없는 검사 편집기**: `nova open --hidden` → 엔진 `--hidden` (`Application::hidden`, 본 창 · 로딩 창을 띄우지 않음). `Tools/tests/common.ps1` 의 Start-TestEditor 가 늘 `--background --hidden`. 확인: 최상위 창 2 개 모두 보이지 않음 (편집 · Play), Game 뷰 스크린샷 됨
  - 검사 `-Only cinemachine` 19/19 (세 고리 (−4, 2.75, 0) · 구 (−7.07, 7.57, 0) · 3 인칭 (0.5, 0.5, −2) 등 손 계산과 같음, 섞기 중간 x 5.25 · 시야각 44, 충격 −1.9, 벽 −2 → −0.9, 따라가기 0.56 m). 문서 docs/CINEMACHINE.md, README · NOVA_CLI · PACKAGES
  - 안드로이드 · 웹 플레이어는 이 패키지 변경으로 다시 빌드하지 않음 (나중에)
  - 회귀 cli · packages · starter · scenes · tween: 첫 실행 53/55 — starter 의 차 "steer right" (2.5 초 회전 중 뒤집힘) · 이어진 "S while driving" 실패, 다시 돌리니 starter · wheel 18/18 → 차 회전 검사가 가끔 흔들림 (이번 변경과 무관해 보임, 기록만)
- 이전: 2026년 10월 8일 — **모션 벡터 (Volume 의 Motion Vectors)** (사용자 요청 — 구글 시트 패키지 질문보다 먼저). **완료 (푸시함)**
  - `MotionVectors` (`Source/Graphics/DX11/MotionVectors.*`, `Shaders/63. MotionVectors.fx`): 지터 뺀 uv 의 이번 − 지난, 카메라 패스 (깊이로) + 움직인 Mesh Renderer · Skinned Mesh Renderer 만 지난 월드 · 지난 팔레트로 다시 그리기 (깊이는 픽셀 셰이더에서 비교). 인스턴스 버퍼는 그대로
  - Volume `MotionVectors` (Enable · Object Motion · Skinned Motion, 분류 Rendering), Motion Blur Mode **Camera And Objects** (URP), 렌더러의 Motion Vectors · Skinned Motion Vectors 칸 (있었지만 쓰이지 않던 것) 을 실제로, Skinned Mesh Renderer 에 Motion Vectors 드롭다운 추가
  - 쓰는 곳: TAA (3 x 3 가장 가까운 깊이의 속도), Motion Blur, SSAO 시간 누적 (모션 벡터 자리 + 지터 차, 움직이는 물체는 깊이 판정 느슨히)
  - 함께 고침: `PostProcessPass::IsNeeded` 가 SSAO · 모션 벡터 (기본 켜짐) 때문에 모든 장면을 HDR 후처리 경로로 보내던 것
  - 검사 `-Only motionvectors` 11/11 (TAA 체커 대비 51.3 → 53.1, Motion Blur 8.9 → 29.0 등, OpenGL), CLI `motionvectors info | map --rect`. 문서 MOTION_VECTORS · DEPTH_OF_FIELD_MOTION_BLUR · SSAO, Showcase 263
  - 진단 중 실수: Play 중 CLI set 이 막혀 Volume 이 안 바뀐 줄 모르고 TAA 비교를 한 번 잘못 읽음 (1 대 290 — 실제론 같은 설정). 설정마다 Play 를 다시 하도록 고침
- 이전: 2026년 10월 8일 — **SSAO 품질 2 차: 시간 누적 · 윤곽 선 없애기 · 전체 해상도** (사용자 요청). **완료 (푸시함)**
  - 윤곽 선 원인: AO · 흐림이 노멀 · 깊이를 선형으로 읽어 윤곽을 사이에 둔 값이 섞임 + 반 해상도 AO 를 32 가 선형으로 늘려 앞 물체가 뒤 AO 를 받음 → 점 읽기 (대표 = 2 x 2 왼쪽 위), 전체 해상도 업샘플 (깊이 · 노멀 비슷한 텍셀만)
  - 함께 찾음: 깊이를 읽은 픽셀과 방향을 구한 자리가 반 픽셀 어긋나 비스듬한 바닥 전체가 옅게 가려짐 (평균 0.993 → 0.9998)
  - 시간 누적: 표본 무늬를 프레임마다 (R2), 지난 프레임 뷰 · 투영 (TAA 지터 포함) 으로 히스토리 (AO · 뷰 깊이), 깊이 판정 + 이웃 3 x 3 범위로 자름, 값 · 크기 바뀌면 버림. Volume `temporalAccumulation` (기본 켬) · `fullResolution` (기본 끔)
  - 검사 `-Only ssao` 17/17 (떠 있는 상자 윤곽 · 바닥 얼룩 · 잔상 · TAA 깜빡임 0.38 → 0.15 · Full Resolution · OpenGL = DX11). 비용 반 해상도 + 누적 0.23 ms, 전체 0.55 ms (1143 x 572). 문서 SSAO, Showcase 262
  - 처음 넣은 "누적이 표본 14 개에 더 가깝다" 검사는 성립하지 않아 (1.85 대 1.05 /255 — 흐림 횟수 차이) 빼고, 실제 이득인 TAA 깜빡임 감소로 검사
- 이전: 2026년 10월 8일 — **SSAO 를 Volume 효과로 · 제대로 검사** (사용자 요청). **완료 (푸시함)**
  - 찾은 문제: AO 맵을 계산만 하고 어떤 재질에도 쓰지 않았다 (재질의 숨은 `UseSsaoMap` 기본 0 — 엔진 재질 · 지형 · 나무 · 바위 · 디테일 모두), 표본 1 개 (`PS(1)`) + `pow(…, 16)`, 무작위 방향 텍스처에 float 를 RGBA8 로, 방향 정규화 없음, 화면 구석 방향이 처음 시야각 · 크기로 고정, Scene 뷰는 흐림 없음, 값이 셰이더에 고정
  - Volume 효과 `AmbientOcclusion` (Screen Space Ambient Occlusion — URP 의 Enable · Intensity · Radius · Direct Lighting Strength · Samples · Falloff Distance, 기본 켜짐). `Ssao` 다시 씀 (`Render(proj, settings)` — 그 프레임 투영으로 구석 방향, 꺼지면 흰 맵), `28. Ssao.fx` 4 · 8 · 14 표본, `32` 에 Direct Lighting Strength (`gSsaoParams`)
  - CLI `ssao info | map <png> [--view]` (AO 맵을 회색 PNG 로). 검사 `-Only ssao` 10/10 (OpenGL = DX11), 그래픽 스위트 함께. 문서 `docs/SSAO.md` · README · NOVA_CLI, Showcase 261
  - SSAO 가 이제 실제로 그림에 들어가므로 안드로이드 · 웹 플레이어는 다시 빌드해야 같은 그림 (이번 Ragdoll · APV 변경과 함께)
- 이전: 2026년 10월 8일 — **낮 · 밤 + Adaptive Probe Volume 확산광** (사용자 지시 5 가지 중 5 번 — 마지막). **완료 (푸시함)**
  - APV 는 실시간이라 시각마다 다시 굽기는 필요 없음 — 대신 고친 것: 프로브 광선이 하늘에 닿으면 날씨 · 낮밤 배율 없이 스카이박스 그대로 모아, 밤에도 낮 하늘빛을 모았다 (화면에서 프로브 빛 전체에 밤 환경광 배율을 곱해 가려졌지만 가로등이 번진 빛까지 어두워짐)
  - `55. ProbeVolume.fx`: 하늘 = WeatherSkyGrade × `gIndirect` (ProbeVolumes 가 다시 비추기 · 광선 때 `WeatherState::AmbientScale` 로). `32. InstancedBasic.fx`: 프로브 빛 × `gIndirectGI` (Volume Indirect Lighting 만, EditorApp), 하늘 몫 × `gIndirect`, 하늘 반사 가림도 같은 배율. fxc (DX11 · GLES · WebGPU) 확인
  - `ProbeVolumes`: 발광이 서서히 바뀌는 동안 (NightLight) 매 프레임 다시 짓던 것 → 30 프레임에 한 번 + 멈춘 뒤 한 번
  - 검사 `-Only daynight` 13/13 (자정 프로브 위쪽 빛 = 정오의 0.05 배, 환경광 0.15 배), probevolume 함께 통과. APV 없는 장면은 같은 식. 문서 ADAPTIVE_PROBE_VOLUME · DAY_NIGHT, Showcase 260
  - 사용자 지시 5 가지 모두 끝. 푸시는 아직 (요청 시). 안드로이드 · 웹 플레이어는 이번 Ragdoll · 셰이더 변경 전 빌드
- 이전: 2026년 10월 7일 — **FBX 의 묻힌 그림 꺼내기** (사용자 지시 5 가지 중 4 번 — 다음: 낮밤 + APV 확산광). **완료 (푸시함)**
  - `FBXLoader::ExtractMaterials`: 재질 그림이 묻힌 그림이면 (`*0` 또는 원래 파일 이름 — `aiScene::GetEmbeddedTexture`) `<파일>_FBX.Textures/<원래 이름>.<형식>` 으로 꺼내 (압축 그림 그대로, 풀린 화소 = 32 비트 TGA, 있으면 그대로) Base Map · Normal · Emission 에
  - 검사 자료 `Tools/tests/data/EmbeddedTextures.fbx` (Blender 5.2 로 `Tools/tests/make_embedded_fbx.py`), `-Only modelplace` 7/7 (PNG 둘 · .mat 의 Base Map · 화면에 빨강 · 파랑). 문서 MODEL_PLACEMENT · NOVA_CLI, Showcase 259
- 이전: 2026년 10월 7일 — **Starter Assets 2 단계: 차에 타고 내리기 · 래그돌에서 일어나기 · 데모** (사용자 지시 5 가지 중 3 번 — 다음: FBX 내장 텍스처). **완료 (푸시함)**
  - `Ragdoll`: 꺼질 때 (일어나기) 루트를 골반 자리 · 일어서는 방향 (등 = 발 쪽, 배 = 머리 쪽) 으로, 쓰러진 자세 (월드) → 애니메이션 자세로 Blend Time (0.5 초, smoothstep) 섞기. `IsFaceUp · IsBlending`, 저장 `blendTime · alignRoot`, C# `Ragdoll.blendTime · alignRoot · isFaceUp · isBlending` (RD_Get/Set 번호만 — 표는 그대로)
  - 일어나기 클립 GetUpBack (2.2 초) · GetUpFront (2.4 초) 를 모델 편집기로 (`docs/examples/anim_basic.txt`) → `Nova_Basic.glb` 다시 내보냄 (Idle · Walk · Wave 바이트 같음), `DefaultCharacter.controller` 에 두 상태 + Idle 로 가는 Exit Time 전이
  - C#: `RagdollTarget.Recover` (누운 방향의 클립 · getUpTime 뒤 조작 켜기), 새 `VehicleEnterExit` (E 로 차 타기 · 내리기, 캐릭터 숨김, Follow Camera 전환, 장면의 차는 탈 때까지 주차) — Third Person Character 에 붙음. 패키지 1.2.0
  - CLI: `set/get --component <C# 클래스 이름>` (값 = public 칸). 데모 `docs/examples/starter/build_starter_demo.ps1`, Showcase 258
  - 검사 `-Only starter` 13/13 (일어나기 · 타기 · 운전 · 내리기 추가), starter · ragdoll · animation · model 90/90 (모델 스위트의 클립 수 3 → 5). 안드로이드 · 웹 플레이어는 Ragdoll 변경 전 빌드 (다음 플레이어 빌드 때 함께)
  - 함께 고침 (따로 커밋): **에디터 창이 비활성이면 GameTimer 가 멈춰** CLI 로 프레임을 돌려도 물리 시간이 0 → 뒤에 띄운 검사 에디터의 물리가 통째로 멈추던 것 (`App.cpp` — 아래 래그돌 항목의 '지켜볼 것' 의 원인. heartbeat 가 아예 없던 것으로 찾음). 에디터는 Unity 처럼 계속, 플레이어만 멈춤
- 이전: 2026년 10월 7일 — **activeInHierarchy 마무리 · 안드로이드 · 웹 플레이어에서 오늘 기능 확인** (사용자 지시 5 가지 중 1 · 2 번 — 다음: Starter Assets 2 단계). **완료 (푸시함)**
  - 1 번: 플레이어 다시 빌드 (안드로이드 · 웹 — Assimp 없는 플레이어에 `aiGetMaterialFloatArray · aiGetMaterialTexture` 대체 추가), 검사 장면 `common.ps1` 의 `New-PlayerFeatureScene` (천 치비 · 차 · 래그돌 표적 · 낮밤 가로등 · 구운 반사 프로브) + `Tools/tests/features_player_probe.cs`.
    안드로이드 `Tools/tests/android_player.ps1 -Check nav|features` (android_nav.ps1 을 바꿈) 11/11, 웹 스위트 15/15. Showcase 257
  - 함께 고침: Follow Camera 가 enabled 를 무시하고 따라가던 것 (C++ LateUpdate · C# `FollowCamera : Behaviour`)
  - 2 번: Particle System (끄면 입자 지움 · 다시 켜면 Play On Awake), Animator (다시 켜면 기본 상태부터 — Rebind), Hierarchy 가 꺼진 오브젝트 · 자식을 흐리게, Scene 뷰 클릭이 꺼진 오브젝트를 고르지 않음. `-Only behaviour` 6/6. 문서 PACKAGES · ANDROID · WEB · NAVIGATION_2D
- 이전: 2026년 10월 7일 — **부모를 끄면 자식도 꺼진다 (Unity activeInHierarchy)** (사용자 요청 — FBX 작업에서 남긴 작업 칩). **완료 (푸시함)**
  - `GameObject::IsActiveInHierarchy()`, `Scene::UpdateScene` 이 꺼진 오브젝트의 Update · LateUpdate 를 건너뛰고 (FixedUpdate 는 이미), 바뀐 프레임에 `Component::OnHierarchyActiveChanged`
  - 그리기: 일반 Render 루프 · 스킨 메시 · 지형 모으기 · MeshBatcher · 기즈모 · 디테일 · 나무 · 바위 · 지형 도장 · 물 · LOD Group · 발광 (APV) — 자기만 보던 곳을 hierarchy 로. 내비 에이전트 · 날씨 고르기 · UI 월드 카메라도
  - 훅: AudioSource (멈춤, 다시 켜면 Play On Awake), Cloth (물리에서 내림), C# 스크립트 (OnDisable / OnEnable)
  - 검사 `-Only behaviour` 5/5 (부모를 끄면 빈 장면과 같은 그림 0.00, 자식 스크립트 Update 멈춤 · OnDisable · 소리 멈춤 → 다시 OnEnable · 재생), audio · physics · animation · ui · cloth · sprites · lodgroup · packages · render · probevolume 함께 통과. 문서 PACKAGES
- 이전: 2026년 10월 7일 — **모델 편집기 FBX → 엔진: 치비 머리 · 머리카락 · 색** (사용자 요청 — 천 작업에서 남긴 작업 칩). **완료 (푸시함)**
  - 원인 1: 메시 `Head` 와 본 `Head` 가 같은 이름 → 엔진이 본 자리에 메시 노드 (가장 위) 를 잡아 머리에 묶인 정점이 가슴으로 (머리카락이 뻗침). 가져오기 `FBXLoader::RenameClashingMeshNodes` (본과 겹치는 메시 노드 = `<이름>_Mesh`, Blender FBX 도), 캐시 NVCC, 모델 편집기 FBX 내보내기도 `_Mesh` (VRM 과 같은 규칙)
  - 원인 2: FBX 재질 색을 쓰지 않았다 (모두 기본 재질) → `FBXLoader::ExtractMaterials` = `<파일>_FBX.Materials/*.mat` (Diffuse 색 · 그림 · 발광 · Phong → Smoothness, 있으면 그대로, 프로젝트 Assets 만 — 같은 이름 VRM 의 `<파일>.Materials` 와 겹치지 않게), 캐릭터 · 정적 모델 배치 둘 다
  - 함께 고침: 모델 파일 감시의 다시 가져오기가 장면을 프레임 가운데 다시 만들어 Scene 뷰가 지운 빛을 읽어 멈추던 것 → 프레임 끝에 (`ImportSettingsInspector::Reimport`)
  - 검사 `-Only model` 에 FBX = VRM 윤곽 (줄마다 끝 차이 2.6 px, 고치기 전 9.9 — 확인함) · Head_Mesh · Dress 색 · .mat. model · modelplace · clothskin · animation 80/80
  - 따로 남긴 일 (작업 칩): 부모 GameObject 를 꺼도 자식 렌더러가 그려진다 (activeInHierarchy 없음)
- 이전: 2026년 10월 7일 — **낮 · 밤 마무리** (사용자 지시 4 가지 중 4 번 — 마지막). **완료 (푸시함)**
  - 반사 프로브를 시각마다 다시 찍기: `DayNightState::TimeOfDay · ProbeRefreshMinutes · ProbeRefreshSerial`, `ReflectionProbes::Update` 가 시각이 그만큼 흐르면 모든 프로브를 다시 (Baked · Custom 도 실행 중의 큐브 `Relit` — DDS 는 그대로, 처음 뒤로는 한 면씩), 끄면 구운 큐브로. `probe info` 에 relit · capturedTime
  - `32. InstancedBasic.fx` `ProbeReflection`: 날씨 · 낮밤 하늘 보정을 하늘 · 구운 프로브에만 (실행 중에 찍은 프로브는 Intensity 음수로 표시 — 보정 안 함, 두 번 어두워지지 않게). fxc (DX11 · WebGPU) 확인
  - 패키지: Day Night Cycle 의 Probe Refresh (게임 분, 기본 30), C# `DayNight.probeRefreshMinutes · RefreshReflectionProbes()`, CLI `daynight set --probes` · `daynight probes`, C# `NightLight` (해 높이로 가로등 · 창문 켜기 — 히스테리시스 · 페이드 · 하나씩, 자식 Light + 발광 렌더러)
  - 데모 `docs/examples/daynight/build_street_scene.ps1` (비 오는 거리 — 건물 창문 · 가로등 · 거울 구 · 구운 프로브 · Day Night Cycle · Weather)
  - 엔진 버그 수정 (데모 중 발견): **GameObject > Light > Point / Spot Light 가 Directional 로 남던 것** (`Light::SetPointLight · SetSpotLight` 가 종류를 안 바꿈), **JSON 소수 칸에 정수 (CLI `\"range\":16`) 가 0 이 되던 것** (`DE_SERIALIZE_FLOAT` — `is_number`)
  - 검사 `-Only daynight` 12/12 (reflectionprobe · render · behaviour · weather 함께 통과). 문서 DAY_NIGHT · REFLECTION_PROBE, Showcase 256
- 이전: 2026년 10월 7일 — **차량 · 래그돌 샘플 (Starter Assets)** (사용자 지시 4 가지 중 3 번 — 다음: 낮밤 마무리). **완료 (푸시함)**
  - 패키지 `com.nova.starter-assets` 1.1.0: C# `CarController` (W/S/A/D · 브레이크 → 후진 · 속도에 따른 조향 · 손 브레이크 · R 세우기 · 다운포스, readKeyboard 끄고 throttle/steer 로 시험), `RagdollTarget` (맞으면 Ragdoll 켜고 가까운 바디를 밈, CC · TPC 끔, Recover), `RagdollShooter` (클릭 → ScreenPointToRay)
  - 엔진: `GameObjectFactory::CreateCar` (처음 = 재질 7 + `Assets/StarterAssets/Car.prefab` 저장, 다음 = 인스턴스, Follow Camera) · `CreateRagdollTarget` (기본 캐릭터 + Wizard 꺼짐 + RagdollShooter), 메뉴 3D Object > Car · Ragdoll Target, CLI `create car | ragdoll-target`. `CreatePrimitive` 공개
  - C# (Unity 이름): `Camera.ScreenPointToRay · ViewportPointToRay`, `Rigidbody.AddForceAtPosition`
  - 검사 `-Only starter` 9/9 (wheel · ragdoll 함께 23/23). 문서 `docs/STARTER_ASSETS.md` (+ WHEEL_COLLIDER · RAGDOLL · NOVA_CLI · README), Showcase 255
- 이전: 2026년 10월 7일 — **캐릭터 옷 (Skinned Mesh 위의 천)** (사용자 지시 4 가지 중 2 번 — 다음: 차량/래그돌 샘플 · 낮밤 마무리). **완료 (푸시함)**
  - `Cloth`: Skinned Mesh Renderer 에 붙이면 Jolt skinned constraint — rest = 바인드 자세 (월드 크기), 역바인드 하나 + 본 행렬 = 팔레트 × 월드 (GPU 스키닝과 같은 값), Unity `coefficients` (maxDistance · collisionSphereDistance), Pin = maxDistance 0, 뒤 막이를 위해 삼각형 감김을 메시 법선에 맞춤, 순간 이동 = hardSkinAll. 서브메시 기준 정점 (VertexStart) 반영
  - `SkinnedMeshRenderer::SetSimulatedVertices`: 천 정점 (오브젝트 공간) 을 동적 버퍼로, 팔레트 끝 단위 본 하나에 묶음 (BlendShape 와 같은 버퍼 · `UploadDynamicVertices`). `PhysicsManager::ClothSkin · SkinCloth`
  - C# `Cloth.coefficients` · `ClothSkinningCoefficient` (바인딩 표 끝 `CL_GetCoefficients · CL_SetCoefficients` — **안드로이드 `Android/Player` · 웹 플레이어는 다시 빌드해야 표가 맞는다**, 릴리즈 때)
  - 예제 `docs/examples/model_chibi_cloth.txt` (치마 · 망토 + 리깅), 검사 `-Only clothskin` 6/6 (cloth 4/4, animation · ragdoll 20/20). 문서 CLOTH · MODEL_EDITOR, Showcase 254
  - 따로 남긴 일: 모델 편집기의 FBX 로 내보낸 치비가 엔진에서 머리카락이 뻗치고 색이 빠진다 (VRM 은 정상) — 별도 작업 칩으로 제안함
- 이전: 2026년 10월 7일 — **내비게이션을 안드로이드 · 웹에서** (사용자 지시 4 가지 중 1 번 — 다음: 캐릭터 옷 · 차량/래그돌 샘플 · 낮밤 마무리). **완료 (푸시함)**
  - 패키지는 이미 플레이어 빌드에 정적으로 들어가 있었다 — 실제 기기 · 브라우저 검사를 더하고 웹의 실패를 고침
  - 웹: NavMesh 를 읽을 때 타일 만들기가 `std::thread` 를 띄워 시작이 예외로 멈췄다 → `NavData::ParallelFor` 를 웹에서는 부른 스레드에서 차례로. `Web/Source/WebMain.cpp` 가 시작 · 프레임의 C++ 예외를 받아 로그 (`[Web] exception in init: …`)
  - 검사: `Tools/tests/nav_player_probe.cs` (3D 벽 · 2D Box Collider 2D 벽을 돌아가는 길 · 도착), `common.ps1` 의 `New-NavPlayerScene · Test-NavPlayerLog`, 안드로이드 `Tools/tests/android_nav.ps1` (MuMu, Build And Run → logcat) 8/8, 웹 스위트에 두 항목 — web 13/13. 문서 NAVIGATION_2D · ANDROID · WEB
- 이전: 2026년 10월 7일 — **C# Behaviour.enabled → 네이티브 컴포넌트** (사용자 요청 — 래그돌 작업에서 남긴 일). **완료 (푸시함)**
  - 바인딩 `Comp_GetEnabled · Comp_SetEnabled` (타입 이름 — 표 끝), C# `Behaviour.enabled` (MonoBehaviour · UI Graphic · Selectable 은 그대로 자기 것) · `Collider.enabled` (전에는 늘 true)
  - 네이티브가 끄면 멈추게: 빛 (`LightManager::IsLit` — 꺼짐 · 꺼진 계층, 게임 · Scene 뷰 모두), SpriteAnimator, Expressions. (오디오 · 카메라 · 콜라이더 · 2D 렌더러 · 포즈 수정자 · UI · Animator 는 이미 봄)
  - 물리: 바디를 빼거나 다시 만들 때 둘레의 잠든 바디를 깨운다 (`WakeAround` — 콜라이더를 끄면 위의 상자가 떨어진다)
  - 검사 `-Only behaviour` 3/3 (Animator 멈춤 · 다시 · Inspector 값, 빛 끄기, 콜라이더 끄기), 함께 physics · ui · audio · animation · sprites · light2d · ragdoll · cloth — 75/75. 문서 `docs/PACKAGES.md`
- 이전: 2026년 10월 7일 — **천 (Cloth)** (같은 지시 순서의 8 번 — 마지막). **완료 (푸시함)**
  - `Source/Scene/Cloth.*`: Unity Cloth (Mesh Filter 메시 → Jolt Soft Body: 정점 묶기 · 늘어남 · 비틀림 · dihedral 접힘 · LRA, 고정 Top Edge · Top Corners · 고른 정점, 바람 · 출렁임, 순간 이동 = 천 전체 이동), 메시 사본을 동적 정점 버퍼로 (`MeshGeometry::UpdateVertices`)
  - `PhysicsManager`: CreateCloth · DriveCloth · ShiftCloth · GetClothVertices · WorldSerial, Soft Body 접촉 거르기 (자기 콜라이더 · 트리거)
  - C# `Cloth · ClothPinMode`, Add Component > Physics > Cloth. 검사 `-Only cloth` 4/4, 문서 `docs/CLOTH.md`, Showcase 252
  - 사용자 지시 순서 (씬 · PlayerPrefs · 트윈 · 2D 빛 · 2D 내비 · 래그돌 · 차량 · 천) + 낮 · 밤 순환 모두 끝. 푸시는 아직 (요청 시)
- 이전: 2026년 10월 7일 — **낮 · 밤 순환 패키지 `com.nova.daynight`** (사용자 추가 요청 — 새벽 > 아침 > 낮 > 저녁 > 노을 > 밤 > 은하수). **완료 (푸시함)**
  - 패키지: `DayNightCycle` (전역 하나 — 시각 · 하루 길이 · 단계별 Look 7 개를 섞음, Directional Light 를 해 · 달로 돌림), CLI `nova daynight status|set|phase`, C# `DayNight · DayPhase · DayNightCycle`
  - 엔진: `Source/Graphics/Common/DayNightState.*` (기본값 = 그대로), `WeatherState::SkyScale · AmbientScale` (날씨 × 낮 · 밤) 을 Sky · WeatherCover (반사) · 물 · ApplySun/Ambient 가 쓰도록, AtmospherePass 안개 색 (하늘색 모드는 하늘 밝기만큼), `21. Sky.fx` 에 그라데이션 · 노을 빛 · 해 · 달 · 별 · 은하수 (fxc 확인)
  - 검사 `-Only daynight` 8/8, weather 14/14, OpenGL · Vulkan = DX11 (노을 차이 0, 은하수 0.2 — 반짝임). 문서 `docs/DAY_NIGHT.md`, Showcase 251
- 이전: 2026년 10월 7일 — **차량 (Wheel Collider)** (같은 지시 순서의 7 번). **완료 (푸시함)**
  - `Source/Scene/WheelCollider.*`: Unity WheelCollider (레이 서스펜션 — 매달린 질량 × g + 스프링 · 댐퍼로 Target Position 에서 쉼, 슬립 곡선 타이어, 모터 · 브레이크 · 조향, 바퀴 각속도, GetWorldPose · GetGroundHit, 기즈모). Add Component > Physics > Wheel Collider, C# `WheelCollider · WheelHit · WheelFrictionCurve`
  - 안정: 마찰 반작용이 구르는 속도를 넘지 않게, 잠긴 바퀴는 브레이크가 버티면 미끄럼 마찰 그대로, 낮은 속도의 마찰은 접점의 실제 질량 (`PhysicsManager::GetEffectiveMass` — 회전 몫 포함) ÷ 바퀴 수의 절반까지 (서 있는 차가 좌우로 흔들리던 것)
  - 검사 `-Only wheel` 5/5, 문서 `docs/WHEEL_COLLIDER.md`, Showcase 250
  - 지켜볼 것: `-Only ragdoll` 이 느린 세션 (90 초 컴파일 대기) 에서 두 번 물리가 통째로 멈췄다 (바디는 다이내믹, 스텝 없음 — 같은 빌드로 다시 돌리면 9/9). 원인 미확인 → Editor.log 에 `[Physics] start · exit · heartbeat` (Play 처음 3 초 스텝 수) 를 남기게 했다
- 이전: 2026년 10월 7일 — **래그돌 (Character · Configurable Joint · Ragdoll Wizard)** (같은 지시 순서의 6 번). **완료 (푸시함)**
  - `Source/Scene/Joint.*`: CharacterJoint (Jolt SwingTwist — 흔들기 1 = Swing Axis 둘레 → Jolt Normal Half Cone, 흔들기 2 → Plane Half Cone), ConfigurableJoint (SixDOF — 축마다 Locked/Limited/Free, 선 한계 스프링, X·Y·Z · Angular X/YZ · Slerp 드라이브, 목표는 Unity 처럼 반대). 처음 만든 때의 쉬는 틀을 `Joint::Rest` 로 기억 (다시 만들어도 같은 기준)
  - `Source/Scene/Ragdoll.*`: Ragdoll Wizard (휴머노이드 Skinned Mesh → 바디 11 · Unity 값) + Ragdoll 컴포넌트 (Active = 바디가 스키닝 자세를 정함 / 꺼짐 = 키네마틱으로 애니메이션 따라감). `SkinnedMeshRenderer::GetNodeGlobals`. 메뉴 3D Object > Ragdoll..., CLI `ragdoll create|info|active`, C# `Ragdoll · CharacterJoint · ConfigurableJoint · SoftJointLimit · JointDrive`
  - 엔진 수정: 다이내믹 바디를 부모 먼저 Transform 에 쓰기, `Physics.IgnoreCollision` (쉬는 자세에서 겹친 래그돌 쌍), Animator · AnimationPlayer 가 enabled 꺼지면 멈춤 (Unity), **Transform 월드 크기 기본 1** (한 번도 갱신 안 된 루트 아래 콜라이더가 1 mm 이던 버그 — 예전 씬의 0 도 고쳐 읽음)
  - 검사 `-Only ragdoll` 9/9, physics · animation 함께 33/33, 문서 `docs/RAGDOLL.md`, Showcase 249
- 이전: 2026년 10월 7일 — **2D 내비게이션 (NavMesh Surface Plane = 2D)** (같은 지시 순서의 5 번). **완료 (푸시함)**
  - 패키지 `com.nova.ai.navigation`: `NavMeshSurface` 의 Plane (3D XZ / 2D XY) — 2D 굽기 = 바닥 사각형 (콜라이더 · 스프라이트 범위 또는 Volume) + 정적 2D 콜라이더 윤곽 (귀 자르기 삼각형, Edge 는 얇은 사각형, Tilemap Collider 2D 포함, 움직이는 Rigidbody 2D · 트리거 제외)
  - `NavData`: 내비 공간 (x, 0, y), 벽 삼각형을 반 칸 키운 볼록 다각형으로 `RC_NULL_AREA` 표시 (깎기 전), `.navmesh` 버전 3 (머리 뒤 2D 플래그, 버전 2 도 읽음), 2D 기즈모
  - `NavMeshAgent`: 2D 표면이면 XY 로 (z · 회전 그대로, 바닥 Raycast · Obstacle 밀기 없음), C# 내보내기 (destination · velocity · corners · CalculatePath · SamplePosition · 링크 점) 는 월드 좌표. 에이전트를 골라도 내비 메시 기즈모 (Unity 처럼)
  - 검사 `-Only nav2d` 8/8, 3D 는 `-Only packages` 10/10. 문서 `docs/NAVIGATION_2D.md`, Showcase 248
- 이전: 2026년 10월 7일 — **2D 빛 (Light 2D · Shadow Caster 2D · 노멀 맵)** (같은 지시 순서의 4 번). **완료 (푸시함)**
  - `Source/Scene/Light2D.*`: Light2D (Global · Spot — 반지름 · 원뿔 · Falloff · 그림자 · 노멀 맵 거리), ShadowCaster2D (2D 콜라이더 윤곽 → 없으면 스프라이트 사각형, Self Shadows). 메뉴 Light > Global/Spot Light 2D, Add Component, CLI `create global-light-2d · spot-light-2d`, 기즈모
  - `SpriteBatch` · `51. Sprite.fx`: 켜진 Light 2D 가 있으면 Lit 기법 (빛 32 개), 그림자 = 빛을 등진 모서리를 민 사각형을 화면 크기 RGBA 두 장의 채널에 MAX (빛 8 개), 노멀 맵 = 화면 미분 접선 틀. SpriteRenderer 에 Normal Map 칸
  - C# `NovaEngine.Rendering.Universal.Light2D · ShadowCaster2D`, 트윈 `DOIntensity · DOColor · DOShadowIntensity · DORadius`
  - 검사 `-Only light2d` 8/8 (값이 식과 같다), OpenGL · Vulkan 같은 값, 웹 (WebGPU) = DX11 (최대 1 — 웹 스위트에 추가, web 11/11), 안드로이드 빌드 OK. 문서 `docs/LIGHT_2D.md`, Showcase 247
  - Web · Android CMake 의 엔진 소스 glob 에 CONFIGURE_DEPENDS (새 파일을 다시 구성 없이)
- 이전: 2026년 10월 7일 — **트윈 패키지 `com.nova.tween`** (같은 지시 순서의 3 번: DOTween 같은 기능, 별도 패키지). **완료 (푸시함)**
  - 순수 C# (`Packages/com.nova.tween/Runtime`): Tween · TweenerCore<T> · Sequence (Append · Join · Insert · Prepend · 간격 · 콜백), 곡선 30 (Penner), 반복 Restart · Yoyo · Incremental, 지연 · Relative · From · SpeedBased · 거꾸로, 콜백 · WaitForCompletion · AsyncWaitForCompletion, 대상 지우면 조용히 끝, 숨긴 `[NovaTween]` 오브젝트가 Update · LateUpdate · FixedUpdate 로 진행
  - 단축 메서드 (DOTween 과 같은 이름): Transform (이동 · 회전 4 방식 · LookAt · 크기 · Punch · Shake · Jump · Path), Material · SpriteRenderer · UI Graphic 색 · 투명도, FillAmount · DOText · DOCounter, RectTransform, Light · Camera · AudioSource, DOVirtual. 정적 클래스 = `NovaTween` (DOTween 이름은 쓰지 않음)
  - `TweenAnimation` 컴포넌트 (코드 없이 Inspector 에서). 엔진 C# API 에 `Camera.fieldOfView · near · far · orthographicSize · aspect`, `Light.intensity · color · shadowStrength · range · spotAngle` 추가
  - 검사 `-Only tween` 11/11. 문서 `docs/TWEEN.md`, Showcase 246
- 이전: 2026년 10월 7일 — **씬 Additive · LoadSceneAsync · DontDestroyOnLoad · PlayerPrefs** (사용자 지시 순서: 씬 비동기 · 추가 → PlayerPrefs → 트윈 패키지 → 2D 조명 → 2D 내비게이션 → 래그돌 · 차량 · 천). **1 · 2 완료 (커밋 c6cc694, 푸시 전)**
  - `Source/Scene/SceneManagerRuntime.cpp`: 씬 객체는 하나, 루트마다 씬 핸들 (fileID → 핸들). Additive = 오브젝트를 지금 씬으로 옮김 (fileID 새로, 참조 함께), 비동기 = 작업 스레드에서 읽기 · 해석 (웹은 그 프레임), 프레임 끝에 요청 차례대로 바꾸기, allowSceneActivation 0.9 대기, Unload, DontDestroyOnLoad (Single 로 바꿔도 남고 Awake · Start 다시 없음)
  - C#: `SceneManager` (LoadSceneAsync · UnloadSceneAsync · GetSceneAt/ByName/ByPath/ByBuildIndex · SetActiveScene · MoveGameObjectToScene · sceneLoaded/Unloaded/activeSceneChanged), `Scene` (핸들), `AsyncOperation` (yield · completed), `CustomYieldInstruction`, `GameObject.scene`. 알림은 `AppEvents.SceneEvent` (세 호스트 모두) — Play 시작 때 지난 구독을 비운다
  - `PlayerPrefs` (`Source/Scripting/PlayerPrefsStore.*`): 에디터 Library/PlayerPrefs.json, Windows persistentDataPath, 안드로이드 파일 폴더, 웹 localStorage. `Application.persistentDataPath` 등
  - CLI `nova build-scenes list|set|add|remove`. 웹: 엔진 API 표의 호출 모양을 DllImport 로 만들어 줌 (`Tools/web/gen_native_signatures.py` — 없으면 새 모양에서 .NET 이 멈춘다). 씬 이름 찾기가 안드로이드 · 웹에서 '\\' 경로로 실패하던 것 고침
  - 검사: `-Only scenes` 11/11 (PC), 웹에서 같은 검사 스크립트 결과 같음 + localStorage 로 다음 실행에 남음, web 10/10, physics · ui 28/28. 문서 `docs/SCENE_MANAGEMENT.md`
- 이전: 2026년 10월 6일 — **`nova android reference` 의 UI 레이아웃 크기 고침** (사용자 지시: 웹 검사에서 찾은 작업을 이 세션에서). **완료 (커밋 5286035, 푸시함)**
  - 기준 그림이 Screen Space UI 를 Game 뷰 크기로 놓던 것 → 요청한 W x H 로 (`UISystem::LayoutForScreen` — Update 의 캔버스 레이아웃을 함수로 꺼냄), 끝나면 Game 뷰로 되돌림
  - 같은 자리의 옛 문제: 기준 그림 뒤 Game 뷰가 뷰포트 · SSAO 크기를 되찾지 못해 찌그러져 그려졌다 → 그리기 전 값으로 되돌림 (Game 뷰 전후 화소 차이 0)
  - 검사: 웹 스위트에 UI 위치 = DX11 추가 (10/10), ui 15/15, 웹 빌드 OK
- 이전: 2026년 10월 6일 — **웹 빌드 (WebGPU) 5 단계: 에디터 연결 · 검사 · 문서** (같은 5 단계 계획의 마지막). **완료 (커밋 1f211ff, 푸시함)**
  - `Source/Build/WebBuild.*`: Build Settings 의 Web (Build · Build And Run — 진행 창, 에디터 안 미리 보기 서버 127.0.0.1:8600+, wasm MIME · 교차 출처 격리 머리 · 폴더 밖 404), CLI `nova web build [--run] [--port] [--open]` · `web serve` · `web stop-server`
  - BuildSettings: 플랫폼 2 = Web (`activePlatform: "Web"`), `lastWebFolder`. 페이지 제목 = 제품 이름 (엔진이 제목을 바꾸지 않음)
  - 호스트 다시 링크: 엔진 .a 를 .NET 링크 입력에 넣음 (전에는 엔진만 바뀌면 옛 wasm 이 남았다)
  - 검사 `run_tests.ps1 -Only web` 9/9: C# · 물리 · 소리 · 재질 그림 = DX11 (최대 1) · build --run 서버. 웹 UI 위치는 맞고, `nova android reference` 의 UI 가 에디터 Game 뷰 크기로 놓이는 문제는 따로 (작업 제안)
  - 문서 `docs/WEB.md`, README (배지 · 빌드 · 그림), NOVA_CLI, `package_release.ps1` (Binaries/Web · Web/Shell · tint.exe), Showcase 245
- 이전: 2026년 10월 6일 — **웹 빌드 (WebGPU) 4 단계: C# 스크립트 (.NET 웹어셈블리)** (같은 5 단계 계획). **완료 (커밋 10e022e, 푸시 전)**
  - `Web/Host/` (NovaWebHost.csproj — Microsoft.NET.Sdk.WebAssembly net10.0, 다듬기 끔): .NET 런타임 (Mono) + 엔진 정적 라이브러리를 wasm 하나로 링크. `Program.cs` = Bridge 진입점 주소 → `nova_web_set_managed` → `nova_web_start`
  - `Web/CMakeLists.txt`: `nova_web_host` (WebMain 을 main 없이), `NovaWebHost.props` (링크할 .a 목록), `host-modules/<모듈>.c` (엔진 · 패키지 C# 의 DllImport 이름 — .NET 의 P/Invoke 표). 패키지 Runtime C# 도 호스트에 함께 컴파일 (빌드 때 본 DllImport 만 표에 들어간다)
  - `Web/build.sh Release host` → `Web/build/Release/host/wwwroot/_framework`. `ScriptEngineAndroid.cpp` 웹 분기 (주소를 받아 쓴다), 셸: game.json `runtime: dotnet` 이면 `_framework/dotnet.js`, `window.nova` (frames · stats · audio)
  - 내보내기: C# 이 있으면 `_framework` 복사 + 쓰는 BCL 만 (AssemblyRef — webcil .wasm 도 읽게), 게임 데이터 Managed 에는 Assembly-CSharp 만. `Application.platform` = WebGLPlayer (17)
  - 단일 스레드: Jolt `JobSystemSingleThreaded` (`__EMSCRIPTEN__`), 오디오 스트림은 Update 에서 채움. Web Audio 출력 진폭 (Module.novaAudioPeak) · 검사 Chrome 은 `--mute-audio`
  - 결과: AndroidScript 장면 (Start · LINQ · Update · Transform) 브라우저에서 돈다 — platform=WebGLPlayer, 60 fps, 오류 0. 물리 상자 낙하 정상. 엔진만 판도 그대로
  - 다른 담당자: 씬 파일 손대지 않음. `Source/Physics/PhysicsManager.cpp` · `Source/Audio/AudioManager.cpp` · `Source/Scripting/PlatformBindings.cpp` · `ScriptCore/Engine/Services.cs` (enum 추가) 수정
- 이전: 2026년 10월 6일 — **웹 빌드 (WebGPU) 2 · 3 단계: 엔진 wasm · WebGPU 백엔드 · 첫 장면** (같은 지시, 5 단계 계획 "이 대로 진행"). **완료 (커밋 5fe2da1, 푸시 전)**
  - `Web/`: CMakeLists (엔진 + 패키지 정적 · Emscripten 3.1.56 · -fwasm-exceptions -msimd128 — .NET 과 같게), `build.sh` (Ninja), `Source/WebMain.cpp` (캔버스 · 입력 · 프레임 루프 · nova_web_frames/stats),
    WebGPU Gfx 백엔드 `GfxWgpuDevice.cpp` · `GfxWgpuContext.cpp` (D3D11 즉시 컨텍스트 흉내 — 늦은 렌더 패스 · loadOp 지우기 · 파이프라인/바인드 그룹 캐시 · 링 버퍼 · 깊이 형식 → unfilterable 변형 · SV_InstanceID 시작 인스턴스 = 정점 버퍼 오프셋 · 행렬 입력 여러 위치),
    `WgpuRhi.cpp` (효과 = game/Shaders/<이름>.wgsl.json, 그림자 맵 배열 별칭), `Shell/index.html` (장치 · 기능 · 한계, game.json + game.data → MEMFS /game, 로딩 화면)
  - 안드로이드 판 공유 (플랫폼 상관없는 것): AndroidWin32 · Application · EditorStubs · PathManager · PlayerRuntime · App (그래픽만 #ifdef) · Engine (로그 = 콘솔) · XAudio2 믹서 (출력 = Web Audio ScriptProcessor) · ScriptEngine (지금은 스크립트 없이)
  - 엔진: GfxApi::WebGPU, Rhi.cpp 웹 분기, 메시 캐시 길이 size_t → uint64_t (wasm32 — 64 비트 형식 그대로), ShaderGraphWindow kLinkBit 32 비트
  - CLI `nova web export --out` (안드로이드 게임 데이터 BC + WGSL → game.data 한 덩어리 + 플레이어), `Tools/web/run_scene.sh` (서버 + 창 없는 Chrome + 진단)
  - 결과: WebCube · Materials 장면 PC DX11 과 픽셀 차이 0 (최대 1), AndroidModels (FBX 스킨 · VRM lilToon) 1 % (애니메이션 시점), 60 fps, WebGPU 오류 0. 회귀 render · tessellation 21/21, render · animation 19/19, 안드로이드 빌드 OK. Showcase 244
  - 다음: 4 단계 C# (.NET browser-wasm 으로 다시 링크) → 5 단계 Build Settings Web · nova web build · 웹 스위트 · 문서
- 이전: 2026년 10월 6일 — **웹 빌드 (WebGPU) 1 단계: 도구 · 셰이더** (사용자 지시: "WebGPU 최신기능으로 가자" · Tint 받기 허락 · 5 단계 계획 "이 대로 진행"). **완료 (1 단계 커밋 · 푸시함)**
  - 도구: Emscripten = .NET 10 wasm-tools 워크로드의 3.1.56 (C# 런타임과 같은 버전이라 한 wasm 으로 — `Tools/web/emenv.sh`, 캐시 ~/.nova/emcache), Tint = Dawn 얕은 복제 + 의존성 셋만 (`Tools/web/build_tint.ps1` → ~/.nova/dawn/out/tint/Release/tint.exe)
  - 확인: 3.1.56 의 USE_WEBGPU 로 Chrome (headless, NVIDIA Turing) 삼각형 + 컴퓨트, .NET browser-wasm 에서 C# → C 호출 (4.9 MB). 앱 안 브라우저는 WebGPU 어댑터가 없다 → `Tools/web/headless.mjs` (창 없는 Chrome · 별도 프로필 · CDP: 콘솔 · eval · 캡처)
  - 셰이더: `ShaderCross::CompileEffectWgsl` (NOVA_WEBGPU, Y 뒤집기 · 시작 인스턴스 보정 없음, 테셀레이션 · GS pass 는 건너뜀, Tint --allow-non-uniform-derivatives + 바인딩 정보, ShaderCache/WGSL), `WgslToJson/FromJson`, 결합 쌍 SamplerPairs,
    CLI `nova web shaders --out` (`Source/Build/WebTools.*`). 32 · lilToon 그림자 맵 배열 → NOVA_WEBGPU 에서 원소마다 (gDirShadowMaps_0..3), 41 KillNaN 비트 검사
  - 결과: 529 pass 중 458 성공 · 68 건너뜀 · 3 실패 (Stream Out 2 · 예제 VecAdd). `Tools/web/check_wgsl.html` 로 Chrome 에서 단계 888 · 파이프라인 458 **오류 0**. 회귀 render 8/8
  - 다음: 2 단계 웹 플랫폼 층 (Web/CMakeLists — 안드로이드처럼 대체 헤더, 프레임 루프 · 파일 · 입력) → 3 단계 WebGPU Gfx 백엔드 → 4 C# → 5 Build Settings Web · nova web build · 스위트
- 이전: 2026년 10월 6일 — **Unity 컴포넌트 1 순위: 2D Tilemap** (같은 지시). **완료 (커밋 · 푸시함)**
  - 새 패키지 `Packages/com.nova.tilemap` (NovaTilemap.dll): Grid · Tilemap (칸 = 타일 번호 + 회전 · 뒤집기, JSON 은 평평한 정수 배열) · Tilemap Renderer (SpriteSource, Chunk) · Tilemap Collider 2D (Collider2D 상속, 맞닿은 칸을 큰 사각형으로 합침),
    `.tile` 에셋 (sprite · color · colliderType, Inspector), Window > Tile Palette (팔레트 = 폴더, 그림 끌어 놓기 → 조각마다 .tile, B/U/I/D/G · [ ] · Shift 지우기 · Ctrl 고르기), Scene 뷰 붓, GameObject > 2D Object > Tilemap > Rectangular,
    CLI `nova tilemap` (20 연산), C# `NovaEngine.Tilemaps` (Tilemap · TileBase · Tile · TilemapRenderer · TilemapCollider2D) + `NovaEngine.Grid`
  - 엔진: `EditorExtensions` RegisterSceneTool (SceneViewContext — 광선 · 행렬 · 뷰 영역) · RegisterCreateMenu, `SpriteRenderer::ResolveSprite`, `UISprites` NOVA_API, C# Vector2Int · Vector3Int · BoundsInt, 아이콘 5 개 (Tools/icons_tilemap.py)
  - 검사: 새 스위트 `tilemap` **14/14** (CLI 만 — 마우스 입력 흉내 금지). 문서 TILEMAP.md (새), NOVA_CLI.md, README. Showcase 243
  - 다음: **웹 빌드 (WebGPU)** — 사용자 선택. Emscripten · WGSL 변환기 · .NET browser-wasm 받기 허락 대기
- 이전: 2026년 10월 6일 — **지형 본 패스 비용 줄이기** (사용자 지시: "지형 본 패스 비용 줄이기 … 다음 작업은 유니티 컴포넌트 중 중요도 순으로 하나씩"). **완료 (커밋 · 푸시함)**
  - Release (ClaudePerfEngine, 1660 SUPER, 가까운 돌 지형): 테셀레이션 켬 2.16 → **1.72 ms** (본 패스 1.24 → 0.83), 끔 (POM) 1.85 → 1.77
  - 61 `TerrainLayerHN` noTile (위 투영 타일 없애기 3 표본은 30 m 안만), 3 % 미만 레이어는 높이 · 노멀을 읽지 않음, POM 걸음 = 높이 범위의 화면 픽셀 / 2 (2 ~ 10), 32 지형 250 m 너머 높이 · 노멀 건너뜀, `TerrainRenderer.cpp` kTessTriangleSize 14
  - 남은 것: 먼 시점에서 테셀레이션 켬이 끔보다 0.9 ms 더 (원인 못 찾음 — 화면 공간 나눔 실험은 그림자가 나빠져 되돌림)
  - 검사: tessellation · tessellationgl · tessellationvk **37/37**. 문서 TESSELLATION.md 성능 표
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
- 이전: 2026년 10월 5일 — **활성 카메라 찾기 · 물리 동기화 남은 비용** (사용자 지시: 추천 1 · 2 진행, "코덱스는 이제 작업 안해. 너가 다해"). **완료 (푸시함)**
  - 카메라 · 빛 1.1 ms (안드로이드) 의 원인 = `DisplayManager::GetCameraForDisplay` 가 부를 때마다 씬 전체 (복사 + 오브젝트마다 GetComponent_SP). `Camera::All` (생성 · 소멸 때 등록,
    복사 생성자 삭제) 에서 고른다 — 규칙 그대로 (현재 씬 · 활성 계층 · 켜짐 · 디스플레이 · 첫 Camera · Priority, 같으면 씬 순서), 지워진 오브젝트는 `GameObject::IsAlive` (Play 멈춤 때 충돌을 검사가 잡음)
  - 새 `Source/Scene/ComponentIndex.*`: 오브젝트마다 컴포넌트 분류 (콜라이더 · Rigidbody · Character Controller · Joint · 2D 콜라이더 · Rigidbody2D · Joint2D) 를 InstanceID 지문으로 기억.
    `Physics/PhysicsManager.cpp` (동기화 · FindRigidOwner), `Physics2D/Physics2DManager.cpp` (Sync · FindRigidbody), `Physics2D/Physics2DJoints.cpp` (Sync), `Scene/Scene.h` (GameObjectsView)
  - PC Release 도시 Play A/B: 프레임 7.95 → 6.6 ms, 카메라 · 빛 1.01 → 0.08 아래, 동기화 0.64 → 0.46, 2D 0.36 → 0.08 ms. FixedUpdate 는 예전 방식이 더 싸서 그대로
  - 검사: cli (새 활성 카메라 검사) · physics · physics2d · animation · packages · ui · render · material · occlusion · audio · layers · sprites · anim2d, joints2d **42/42**, scene_lifecycle **43/43**, 안드로이드 빌드. Showcase 212
  - Codex 작업 종료 — AI_COLLABORATION.md 에 적음
- 이전: 2026년 10월 5일 — **Renderer 마무리** (같은 지시 — 추천 3). **완료 (푸시함)**
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
- 이전: 2026년 10월 5일 — **물리 동기화 CPU** (사용자 지시 "푸시하고. 다음 작업 진행" — GLES 측정에서 찾은 물리 갱신 3.3 ms). **완료 (푸시함)**
  - `Source/Physics/PhysicsManager.cpp`: StepSimulation 의 동기화를 한 번 훑기 (콜라이더 · Rigidbody · Character Controller · Joint), 버퍼 재사용, 콜라이더 표 제자리 갱신 (seen 번호),
    종류 기억 (KindOf · ComputeSignature 의 kinds), SyncJoints 가 모은 목록으로, 단계마다 Profiler 구간 (Physics.FixedUpdate · Sync · Characters · Joints · TransformToBody · Simulate · Events)
  - PC Release 도시 Play: 동기화/2D 물리 비율 3.7 → 1.7 (약 2 배), 캐릭터 · Joint 0.29 ms → 0. 회귀 physics · physics2d · animation · packages · cli **51/51**
  - 안드로이드 재측정은 못 했다: 그때 Unity · ChatGPT 앱이 CPU 를 많이 써서 Debug 에디터가 도시 장면을 만들다 4.3 초 멈춤 → 게임 데이터 내보내기 실패 (에디터 모드라 물리 변경과는 무관).
    `android_city_perf.ps1` 은 막 켠 VM 의 adb offline 때 ABI 를 기다리게 고침
- 이전: 2026년 10월 5일 — **GLES 그리기 CPU** (사용자 지시 "푸시하고 진행해줘" — 추천 1 → 2 → 3 의 1). **완료 (푸시함)** — android **66/66**, android_occlusion **13/13**, 도시 10/10, PC material · cli **19/19**, Showcase 209
  - `Android/Source/GLESState.*` (상태 · 바인딩 기억: 프로그램 · 상수 블록 · 텍스처 유닛 · 샘플러 · SSBO · image · VAO, Invalidate · Forget · Deleted),
    `GLESRhi.cpp` (Apply 가 기억으로, 상수 블록은 바뀐 범위만), `GfxGLES.*` (VAO 안 버퍼 기억, 지우기 · Present · RestoreState 에서 기억 버림, GlesCounters),
    `AndroidMain.cpp` (`-e profile on` → gl · scopes), `Engine/AppAndroid.cpp` (프레임 구간), `Source/Core/Profiler.cpp` (BeginFrame 스레드 = 구간 스레드 — 안드로이드에서 구간이 모두 버려졌다),
    `Tools/tests/android_city_perf.ps1` (`-Profile` · `-SkipBuild`, 결과를 파일로), `docs/ANDROID.md`
  - 프레임마다 텍스처 바인딩 3528 → 79, 샘플러 3528 → 48, 상수 블록 바인딩 1240 → 33, 프로그램 205 → 37. MuMu 켬 11.6 → 11.0 ms, 끔 13.9 → 11.6 ms (편차 큼)
  - 찾은 것: 물리 갱신 3.3 ms (`PhysicsManager::StepSimulation` 이 고정 스텝마다 모든 GameObject 를 훑어 바디 동기화를 처음부터) — 다음 작업 후보
- 이전: 2026년 10월 5일 — **추천 2 · 3** (사용자 지시: 추천 2 "C# Renderer 공통 클래스 확장" · 3 "인스턴스별 속성 (GPU 인스턴싱 속성)" 붙여 넣기). **2 완료 (2ec9572 푸시)**, **3 완료 (푸시함)**
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
