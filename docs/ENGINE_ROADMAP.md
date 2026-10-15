# 엔진 로드맵 — Godot 4.7 기능 목록과 대조 (기능만)

2026-10-16 사용자 요청: "Godot 오픈소스도 참고해서 기능적인 부분만, 앞으로 나아가야 할 부분을 잡아 달라".

- 기준: Godot 4.7 공식 기능 목록 <https://docs.godotengine.org/en/stable/about/list_of_features.html> — 화면 모양 · 편집기 배치는 보지 않고 **기능** 만 본다
- 규칙: Godot 는 "무엇이 있어야 하나" 의 체크리스트로 쓴다. 이름 · 동작 · 직렬화는 계속 **Unity 와 같게** (엔진 기본 규칙). Godot 는 MIT 라 설계 · 알고리즘을 참고해도 되고, 코드를 가져오면 `ThirdParty/` 에 넣고 라이선스 문구를 남긴다
- 목표 두 가지에 맞춰 순서를 정했다: (1) 지금 만드는 **다크 판타지 오픈월드** (50 km 월드 · 휴머노이드 전투 · 풀 · 나무), (2) 엔진 완성도
- 표의 상태: ● 있음 · ◐ 일부 · ○ 없음

## 1. 대조표

### 렌더링 · 3D

| Godot 4.7 기능 | NOVA | 메모 |
|---|---|---|
| Forward+ · Mobile · Compatibility 렌더러 | ● | Forward · Forward+ · Deferred, DX11 · DX12 · Vulkan · GL · GLES · WebGPU ([RENDERING_ROADMAP](RENDERING_ROADMAP.md)) |
| PBR (ORM · 디테일 · 시차 · SSS · 굴절 · 거리 페이드) | ◐ | URP Lit 기준. SSS · 화면 굴절 · 거리 디더 페이드는 재질 단위로 확인 필요 |
| 방향 · 점 · 스포트 빛, 클러스터 | ● | Forward+ 클러스터 |
| **면광 (사각형 Area Light)** | ○ | 실시간 면광 없음 (LTC 방식) |
| 그림자 (PSSM · 큐브 · PCSS 흐림 · 프로젝터 텍스처) | ◐ | 캐스케이드 · 소프트 섀도 있음, 쿠키 (프로젝터) · PCSS 확인 필요 |
| **라이트맵 굽기** | ○ | 없음 (APV 프로브만) |
| **실시간 GI (VoxelGI · SDFGI · SSIL)** | ○ | APV (Adaptive Probe Volume) · 하늘 Indirect 만. 큰 월드용 동적 GI 없음 |
| 반사 (프로브 · SSR) | ● | Reflection Probe · SSR |
| 데칼 | ● | [DECAL](DECAL.md) |
| 하늘 (HDRI · 절차 · 물리) | ● | Atmosphere · Day Night |
| 높이 안개 · 대기 원근 · 볼륨 안개 | ● | Fog (Volumetric) · Atmosphere |
| **국소 안개 볼륨 (Fog Volume: 상자 · 구 · 3D 텍스처)** | ○ | 전역 안개만 |
| GPU 입자 (서브 이미터 · 트레일 · 끌개 · 충돌) | ● | VFX Graph · Particle System ([VFX_GRAPH](VFX_GRAPH.md)) — 깊이 · SDF 충돌은 확인 필요 |
| 후처리 (톤매핑 · 노출 · DoF · SSAO · 블룸 · LUT) | ● | URP Volume 오버라이드 전부 |
| AA (TAA · FXAA · MSAA · SMAA) | ● | [ANTI_ALIASING](ANTI_ALIASING.md) |
| **해상도 배율 · 업스케일 (FSR 1/2)** | ○ | Render Scale · FSR 없음 |
| VRAM 압축 (BC · ASTC · ETC2 · Basis) | ◐ | BC · ASTC (안드로이드). Basis 없음 |
| 가상 텍스처링 | ● | Godot 에 없음 |
| 메시 LOD · 가시 범위 (HLOD) | ◐ | LOD Group · Auto LOD · 임포스터 있음. **HLOD (멀리서 여러 오브젝트를 한 메시로)** 없음 |
| 오클루전 컬링 | ● | Hi-Z |
| 기본 도형 · CSG · GridMap · Path3D | ◐ | 기본 도형 · Spline · Prototype (블록아웃) · 모델 편집기 있음. CSG · GridMap 없음 |
| 런타임 glTF 읽기 · 내보내기 | ◐ | 읽기 (GLB · VRM) 있음, 런타임 내보내기 없음 |

### 2D

| Godot | NOVA | 메모 |
|---|---|---|
| 스프라이트 · 타일맵 · 2D 빛 · 2D 물리 · 2D 내비 | ● | [TILEMAP](TILEMAP.md) · [LIGHT_2D](LIGHT_2D.md) · [JOINTS_2D](JOINTS_2D.md) · [NAVIGATION_2D](NAVIGATION_2D.md) |
| 2D GI · Polygon2D · 시차 레이어 | ○ | 오픈월드 목표와 멀다 — 나중 |

### 물리

| Godot | NOVA | 메모 |
|---|---|---|
| 정적 · 리지드 · 캐릭터 · 영역 · 관절 · 래그돌 · 차량 | ● | Jolt, Wheel Collider, Ragdoll, Character Controller |
| 물리 보간 | ● | Rigidbody Interpolation |
| 소프트 바디 | ◐ | Cloth 만 (부피 소프트 바디 없음) |

### 애니메이션

| Godot | NOVA | 메모 |
|---|---|---|
| 상태 머신 · 블렌드 트리 · 루트 모션 · 리타깃 | ● | Animator (Unity Mecanim 과 같은 JSON), Humanoid 리타깃 |
| 블렌드 셰이프 | ● | |
| **레이어 마스크 (Avatar Mask) · 덧셈 레이어** | ◐ | 레이어는 있지만 본 마스크가 없다 — **걸으며 상체만 공격** 이 안 된다 |
| **애니메이션 이벤트 · 메서드 · 소리 트랙** | ○ | 타격 프레임 · 발소리 · 이펙트를 시간에 맞출 길이 없다 |
| 역운동학 (IK) | ◐ | Foot IK · Look · Legs Animator. 손 IK (Two Bone) · 일반 IK 체인 없음 |
| 아무 속성이나 애니메이션 (곡선) | ◐ | Transform · 블렌드 셰이프 위주. 재질 · 컴포넌트 값 곡선은 확인 필요 |
| **시네마틱 (Unity Timeline)** | ○ | 컷신 · 연출 도구 없음 |

### 오디오

| Godot | NOVA | 메모 |
|---|---|---|
| 2D · 3D 소리 · 도플러 · 버스 · 효과 | ● | Audio Mixer · Audio Source |
| **무작위 · 연속 재생 컨테이너 (Random Container) · 폴리포니 · 반복 막기** | ○ | 발소리 · 칼 소리 변주에 필요 |
| 리버브 존 · 마이크 · TTS · MIDI | ◐ | 리버브 섞기만 |

### 입력 · UI · 텍스트

| Godot | NOVA | 메모 |
|---|---|---|
| 키 · 마우스 · 게임패드 · 터치 | ● | Unity 옛 Input Manager 식 |
| **행동 매핑 (Input Actions · 재지정 · 데드존 · 키보드/패드 공유)** | ○ | Unity Input System 이 없다 — 키 바꾸기 · 패드 지원이 스크립트마다 따로 |
| UI 컨트롤 · 레이아웃 · 스크롤 | ● | uGUI 식 (Canvas · RectTransform …) |
| **현지화 (CSV · PO · tr · 복수형 · RTL)** | ○ | 없음 |
| 글꼴 (SDF · 아웃라인 · 이모지 · OpenType) | ◐ | UIFont. 리치 텍스트 · 폴백 글꼴 확인 필요 |
| 접근성 (화면 낭독기) | ○ | 나중 |

### 스크립트 · 편집기 · 도구

| Godot | NOVA | 메모 |
|---|---|---|
| 스크립트 (GDScript · C#) · 네이티브 확장 (GDExtension) | ● | C# + 패키지 네이티브 DLL |
| **스크립트 디버거 (중단점 · 단계 실행)** | ○ | C# 을 IDE 에 붙여 디버깅하는 길이 없다 |
| **Play 중 스크립트 다시 읽기** | ○ | |
| 프로파일러 (CPU · GPU · 사용자 모니터) | ● | Profiler 창 · `nova perf`. Tracy · Perfetto 내보내기 없음 |
| **원격 인스펙터 (빌드한 게임의 장면 보기)** | ○ | 빌드된 플레이어 · 안드로이드 디버깅이 로그뿐 |
| 여러 인스턴스 실행 (서버/클라 검사) | ○ | 네트워크가 생기면 |
| 장면 상속 · 중첩 | ◐ | 프리팹 오버라이드는 있다. **프리팹 변형 (Variant) · 중첩 프리팹** 없음 |
| 데이터 리소스 (Resource) | ○ | Unity ScriptableObject 가 없다 — 무기 · 아이템 · 스탯 데이터를 둘 곳 |
| 편집기용 스크립트 · 플러그인 | ◐ | EditorExtensions · 패키지. 스크립트 Gizmos API 없음 |
| **무비 메이커 (프레임 고정 녹화 + 소리)** | ○ | 쇼케이스 · 유튜브 영상에 바로 쓸 수 있다 |
| CLI 자동화 · CI | ● | `nova` CLI (Godot 보다 넓다 — AI 가 편집기를 다룬다) |

### 파일 · 배포 · 네트워크 · XR

| Godot | NOVA | 메모 |
|---|---|---|
| 텍스트 / 이진 장면 · 리소스 | ◐ | JSON 텍스트. 이진 · 압축 · 암호화 없음 |
| **저장 · 불러오기 API (ConfigFile · 직렬화)** | ◐ | PlayerPrefs 만 — 오픈월드 세이브 (월드 상태 · 인벤토리) 기반 없음 |
| **팩 파일 (PCK) · 모드 · DLC** | ○ | 빌드는 폴더째 |
| 헤드리스 (전용 서버) | ○ | |
| **네트워크 (TCP · UDP · HTTP · WebSocket · WebRTC)** | ○ | 없음 |
| **고수준 멀티플레이 (RPC · 동기화 · 스폰)** | ○ | 없음 |
| 동영상 재생 | ○ | 컷신 · UI 배경 |
| XR (OpenXR) | ○ | 나중 |
| 플랫폼 (Windows · 안드로이드 · 웹) | ● | iOS · macOS · Linux 없음 |

### NOVA 가 Godot 보다 앞선 것 (다시 만들지 않는다)

VFX Graph · Shader Graph · Terrain Graph (Gaea 식) · PCG · 절차적 식생 160 종 · 건축물 생성기 · 모델 편집기 (리깅 · VRM) · 50 km 스트리밍 월드 · 풀밭 · 가상 텍스처링 · DX12 / Vulkan + Render Graph + Async Compute · 군중 (GPU 인스턴싱 · 임포스터) · Cloth · Cinemachine · Day Night · Weather · Unity 에셋 가져오기 · AI 용 CLI

## 2. 단계 (우선순위)

규모: S = 하루 안, M = 며칠, L = 한 주 넘게. 단계마다 검사 스위트 · 문서 · 쇼케이스 · 커밋.

### P0 — 오픈월드 게임을 지금 막는 것

| # | 무엇 | Godot 에서 참고 | NOVA 이름 (Unity) | 규모 |
|---|---|---|---|---|
| 0-1 | **Avatar Mask + 레이어 블렌딩 (Override · Additive)** — 하체는 이동, 상체는 공격 · 막기 · 활 조준 | AnimationNodeBlend2 의 filter (본 필터), AnimationTree 의 OneShot (filter) | Avatar Mask 에셋 · Animator Layer 의 Mask · Blending · Weight · Sync | M |
| 0-2 | **애니메이션 이벤트** — 클립 시간에 함수 · 소리 · 이펙트 (타격 판정 · 발소리) | Animation 의 Call Method · Audio Playback 트랙 | AnimationEvent (클립 `.meta` 에 저장), `OnAnimationEvent` · 수신 MonoBehaviour 메서드 | M |
| 0-3 | **Input Actions** — 행동 이름 · 키 / 패드 묶기 · 재지정 · 데드존 · 저장 | InputMap · InputEventAction · 데드존 | Input Action Asset · PlayerInput · `action.WasPressedThisFrame()` | M |
| 0-4 | **전투 기반 (게임플레이 프레임워크)** — 히트박스 · 허트박스, 피해 · 경직 · 넉백, 무기 장착 (손 본에 붙이기) | Area3D · 그룹 · 신호 | 트리거 콜라이더 + 0-2 이벤트, `Animator.GetBoneTransform` 소켓, 샘플 패키지 `com.nova.combat` | M |
| 0-5 | **세이브 · 로드** — 월드 상태 · 인벤토리 · 위치 · 퀘스트 값을 JSON (+ 이진) 으로, 버전 · 비동기 | ConfigFile · ResourceSaver · `var_to_bytes` | `SaveSystem` (JsonUtility 식 직렬화) + 슬롯 | M |
| 0-6 | **데이터 에셋 (ScriptableObject)** — 무기 · 아이템 · 적 스탯 | Resource · 사용자 Resource 클래스 | ScriptableObject · `CreateAssetMenu` | M |
| 0-7 | **오픈월드 성능 마무리** — 먼 풀 (지형 표면 풀결로 이어 보이기), 나무 GPU (임포스터 거리 · 그림자 컬링), 모듈 캐릭터 부위 합치기 · 캐릭터 단위 임포스터, HLOD | 가시 범위 (visibility range) · HLOD · MultiMesh | Auto LOD · LOD Group · HLOD 묶음 | L |

### P1 — 오픈월드 품질 (붉은사막 수준의 화면 · 소리 · 연출)

| # | 무엇 | Godot 참고 | NOVA 이름 | 규모 |
|---|---|---|---|---|
| 1-1 | **큰 월드 동적 GI** — 먼저 화면 공간 (SSGI / SSIL), 다음 카메라 둘레 계단식 SDF · 복셀 GI | SSIL · SDFGI (캐스케이드, 굽기 없음) | Screen Space Global Illumination · (HDRP 식) | L |
| 1-2 | 국소 **Fog Volume** (상자 · 구 · 밀도 텍스처 — 늪 · 동굴 · 성 안) | FogVolume · FogMaterial | Local Volumetric Fog (HDRP) | M |
| 1-3 | **면광** (사각형 · 원판, LTC) — 실내 · 횃불 · 창 | 사각형 면광 | Area Light (Rectangle · Disc) | M |
| 1-4 | **Render Scale + FSR 1 / 2 업스케일** — 고해상도에서 GPU 아끼기 | 해상도 배율 · FSR 2.2 | URP Render Scale · Upscaling Filter | M |
| 1-5 | **시네마틱 Timeline** — 애니메이션 · 카메라 (Cinemachine) · 소리 · 이벤트 트랙, 컷신 재생 | Animation 트랙 (메서드 · 소리 · Bézier) | Timeline · PlayableDirector · Signal | L |
| 1-6 | **오디오 컨테이너** — 무작위 · 연속 · 반복 막기 · 음높이 · 볼륨 흔들기, 동시 재생 수 | AudioStreamRandomizer · polyphony | Audio Random Container (Unity 6) | S |
| 1-7 | 손 **IK (Two Bone) · 일반 IK 체인** — 무기 쥐기 · 문 열기 · 사다리 | Skeleton IK · SkeletonModifier3D | Animation Rigging (Two Bone IK · Multi-Aim) | M |
| 1-8 | **현지화** — 문자열 표 (CSV · PO), `tr`, 복수형, 글꼴 폴백 | TranslationServer · CSV · gettext | Localization (String Table · Locale) | M |
| 1-9 | 동영상 재생 (컷신 · 메뉴 배경) | VideoStreamPlayer (Theora) | Video Player | M |

### P2 — 엔진 완성도 · 개발 도구

| # | 무엇 | Godot 참고 | NOVA 이름 | 규모 |
|---|---|---|---|---|
| 2-1 | **C# 디버거** — VS Code · Rider 붙이기, 중단점 · 단계 실행 | 스크립트 디버거 (스레드 포함) | Attach to Unity 식 디버그 어댑터 | M |
| 2-2 | **무비 메이커** — 프레임 고정 녹화 + 소리 (쇼케이스 · 유튜브) | Movie Maker 모드 | Recorder | S |
| 2-3 | **프리팹 변형 · 중첩 프리팹** | 상속 장면 · 인스턴스 장면 | Prefab Variant · Nested Prefab | L |
| 2-4 | 원격 인스펙터 — 빌드한 플레이어 · 안드로이드의 장면 · 값 보기 | Remote scene tree · 원격 인스펙터 | (Unity: Player Connection) | M |
| 2-5 | Play 중 스크립트 다시 읽기 | 실시간 스크립트 다시 읽기 | Domain Reload 옵션 | M |
| 2-6 | 트레이싱 내보내기 (Tracy · Perfetto) | 트레이싱 프로파일러 | Profiler 저장 | S |
| 2-7 | 팩 파일 · 모드 · DLC (압축 · 암호화) | PCK · 추가 PCK | Asset Bundle · Addressables | L |
| 2-8 | 이진 장면 · 압축 저장 (큰 월드 읽기 속도) | 이진 리소스 형식 | (Unity: 직렬화 모드) | M |
| 2-9 | 스크립트 Gizmos · 편집기 도구 API | `@tool` · EditorPlugin | Gizmos · Handles · OnDrawGizmos | S |

### P3 — 넓히기 (멀티플레이 · 플랫폼)

| # | 무엇 | Godot 참고 | NOVA 이름 | 규모 |
|---|---|---|---|---|
| 3-1 | 저수준 네트워크 — UDP · TCP · HTTP(S) · WebSocket | StreamPeer · PacketPeer · HTTPRequest · WebSocketPeer | UnityWebRequest · Transport | M |
| 3-2 | 고수준 멀티플레이 — RPC · 동기화 · 스폰 · 소유권, 헤드리스 서버, 여러 인스턴스 실행 | MultiplayerAPI · MultiplayerSpawner · Synchronizer · `--headless` | Netcode for GameObjects (NetworkObject · NetworkVariable · RPC) | L |
| 3-3 | XR (OpenXR) | OpenXR | XR Plugin Management | L |
| 3-4 | 접근성 (화면 낭독기) · 여러 창 | AccessKit · 여러 창 | | M |
| 3-5 | 다른 플랫폼 (Linux · macOS) | | | L |

## 3. 바로 다음에 할 일

P0 를 위에서부터: **0-1 Avatar Mask → 0-2 애니메이션 이벤트 → 0-4 전투 기반** 을 묶어 "걸으며 휘두르고 맞히는" 전투 한 바퀴를 먼저 완성하고 (OpenWorld 의 `HumanoidShared.controller` 에 상체 레이어 · 타격 이벤트를 더한다), 그다음 0-3 입력 · 0-5 · 0-6 세이브 · 데이터, 그 사이사이 0-7 성능을 잰다 (Release, 1080p).
