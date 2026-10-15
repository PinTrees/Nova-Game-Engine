# Animator — 레이어 · Avatar Mask · 애니메이션 이벤트

패키지 `com.nova.animation`. Unity Mecanim 과 같은 이름 · 동작 · 저장 꼴 (컨트롤러 `.controller` JSON).
로드맵: [ENGINE_ROADMAP](ENGINE_ROADMAP.md) P0-1 · P0-2.

## 레이어 (Weight · Mask · Blending)

| 항목 | 값 | 동작 |
|---|---|---|
| `weight` | 0 ~ 1 | 첫 레이어는 늘 1. C# `animator.SetLayerWeight(i, w)` 는 **이 Animator 만** (컨트롤러는 그대로) — `GetLayerWeight` · `GetLayerIndex(이름)` |
| `avatarMask` | `.mask` 경로 | 이 레이어가 움직이는 몸 부위. 비우면 모든 본 |
| `blending` | `Override` · `Additive` | Override = 아래 레이어를 (마스크 · 무게만큼) 덮어쓴다. Additive = 클립 **첫 프레임** 과의 차이 (회전 · 이동) 를 아래 자세에 더한다 (Unity 와 같은 기준) |

- **빈 상태** (클립 · Blend Tree 없음): 그 레이어는 아래 레이어를 그대로 둔다. 상체 레이어의 기본 상태를 `Empty` 로 두고 공격 상태로 전이하면 전이 시간만큼 서서히 들어오고 나간다 (Unity 와 같음)
- `Play(state)` · `CrossFade(state)` 의 `layer = -1` = 그 이름의 상태가 있는 첫 레이어 (Unity 와 같음)
- Animator 창: 레이어 줄의 톱니 → Weight · Mask · Blending. 줄 오른쪽 `M` = 마스크, `A` = Additive

## Avatar Mask (`.mask`)

Project 창 Create > Avatar Mask. Inspector: Humanoid 몸 부위 13 개 (Unity `AvatarMaskBodyPart` 순서 — Root · Body · Head · 다리 · 팔 · 손가락 · IK) + `Upper Body` · `All` · `None` 버튼, Transform 목록.

```json
{ "humanoid": { "Root": false, "Body": true, "Head": true, "LeftLeg": false, "RightLeg": false, "LeftArm": true, "RightArm": true,
                "LeftFingers": true, "RightFingers": true, "LeftFootIK": false, "RightFootIK": false, "LeftHandIK": true, "RightHandIK": true },
  "transforms": [ { "path": "Root/pelvis/spine_01", "active": true } ] }
```

- 노드 → 부위: 가장 가까운 사람 본 조상 (HumanoidAvatar). 손 아래 사람 본이 아닌 노드 (손가락 · 무기 소켓) = Fingers
- **Hips 는 Root 쪽** — 상체 마스크 (Body 켬) 가 엉덩이를 돌려 다리까지 흔들지 않는다. 몸통 비틀기는 Spine 부터
- Transform 목록: 노드 이름 (경로의 마지막) 마다 켜고 끄기 — 목록에 있으면 그 값이 Humanoid 결과보다 우선 (Generic 리그는 이것만)

## 애니메이션 이벤트 (Unity AnimationEvent)

클립의 정한 시간에 Animator 가 **자기 GameObject 의 모든 스크립트** 에서 이름이 같은 메서드를 부른다.

```csharp
void OnFootstep(int foot) { … }                        // int · float · string · bool · enum 하나, 또는 인자 없음
void OnAttackHit(AnimationEvent e) { … }               // e.functionName · stringParameter · floatParameter · intParameter · time
                                                       // NOVA: e.clipName · stateName · layer · weight (섞인 무게)
```

- 저장: 모델 파일 옆 `<모델>.animevents` (Unity 는 모델 `.meta` 의 clipAnimations events — NOVA 는 `.meta` 가 바뀌면 모델을 다시 가져오므로 따로). 키 = 파일 안 클립 번호, 시간 = 초
- 편집: Animator 창에서 상태를 고르면 Inspector 의 **Events** (시간 · 함수 · float · int · string, Add Event · −), CLI `nova animevent list|add|clear --model <fbx> --clip N [--time s --function F --string --float --int]`
- 언제: Play 중, 상태 시간이 이벤트 시간을 **지날 때마다** (반복 클립은 매 바퀴, 한 프레임에 여러 바퀴면 여러 번 — 최대 3). 상태를 막 시작하면 0 초 이벤트도. 전이 중에는 두 상태 모두 (weight = 섞인 무게). Blend Tree 는 가장 무거운 자식 클립만 (발소리가 두 번 나지 않게). 무게 0 인 레이어는 부르지 않는다
- 받는 곳이 없으면 Console 경고 한 번 (`AnimationEvent 'X' on animation 'Y' has no receiver!`)
- 순서: 모든 레이어의 시간을 진행한 뒤 모아서 보낸다 — 받는 스크립트가 `Play` · `CrossFade` 를 불러도 된다

## 검사

`Tools/tests/run_tests.ps1 -Only animlayers` — 빈 상태 = 아래 레이어, 상체 마스크 (손은 흔들고 발은 걷기 그대로, 발 이동 0 mm), SetLayerWeight 0, Additive 기준 프레임 = 변화 없음 · 뒤 프레임 = 차이, GetLayerIndex, animevent CLI (정렬), 반복마다 한 번 (int), AnimationEvent 인자, 받는 곳 없음 경고.
