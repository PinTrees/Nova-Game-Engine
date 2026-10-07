# Starter Assets — 3인칭 캐릭터 · 차 · 래그돌 표적

바로 Play 해 볼 수 있는 예제 (패키지 `com.nova.starter-assets`, C# 스크립트 — Unity Starter Assets 처럼).
메뉴 **GameObject > 3D Object** 의 아래 항목을 고르면 패키지가 없을 때 프로젝트에 넣고 (Cameras 도), 필요한 컴포넌트 · Main Camera 설정까지 붙인다.

<p align="center"><img src="images/starter_assets.webp" width="900"/><br/><sub>차가 상자 더미를 들이받는다 (Follow Camera 가 뒤에서) · 맞은 표적이 래그돌로 쓰러진다</sub></p>

| 메뉴 / CLI | 만드는 것 | 조작 (Play) |
|---|---|---|
| Third Person Character · `nova create third-person-character` | 기본 캐릭터 + Character Controller + `ThirdPersonController` + Main Camera 의 Follow Camera | WASD · 화살표 이동, Left Shift 달리기, Space 점프, 오른쪽 버튼 끌기 = 카메라 |
| **Car** · `nova create car` | 차 (프리팹 `Assets/StarterAssets/Car.prefab`) + Main Camera 의 Follow Camera | W · ↑ 가속, S · ↓ 브레이크 → 멈추면 후진, A D · ← → 조향, Space 손 브레이크, R 뒤집힌 차 세우기 |
| **Ragdoll Target** · `nova create ragdoll-target` | 기본 캐릭터 + Ragdoll (꺼짐 — 서 있음) + `RagdollTarget`, Main Camera 에 `RagdollShooter` | 왼쪽 클릭 = 쏘기 → 맞은 표적이 맞은 자리에서 밀려 쓰러진다 |

## 차 (CarController)

처음 만들 때 차를 조립해 **프리팹** 과 재질 (`Assets/StarterAssets/Materials` — 차체 · 유리 · 타이어 · 휠 · 전조등 · 후미등) 로 저장하고,
다음부터는 그 프리팹의 인스턴스를 놓는다 (프리팹을 고치면 모든 차에). 새로 만든 차를 Main Camera 가 따라간다.

- 구조: `Car` (Rigidbody 1200 kg) — `Body` · `Cabin` (상자 콜라이더, 합쳐서 하나의 바디), 범퍼 · 등 (그림만),
  `Wheel FL · FR · RL · RR` (Wheel Collider — 반지름 0.36, 서스펜션 0.22 m, 40000 · 4500), `Wheel XX Mesh` (그림 바퀴 — 콜라이더 없음)
- `CarController`: 뒷바퀴 굴림 (`allWheelDrive` 로 네 바퀴), 빠를수록 조향을 줄인다 (`highSpeedSteer`), 가속을 놓으면 엔진 브레이크,
  달리는 중의 S = 네 바퀴 브레이크 → 멈추면 후진 (`maxReverseSpeed`), 속도² 에 비례하는 다운포스. 바퀴 그림은 `GetWorldPose` 로
- 키보드 대신 (UI 버튼 · AI · 시험): `readKeyboard = false` 로 두고 `throttle` (-1 ~ 1) · `steer` · `handbrake` 를 넣는다. 읽기: `speed` (m/s, 뒤 = -) · `speedKmh` · `groundedWheels`

```csharp
var car = GameObject.Find("Car").GetComponent<StarterAssets.CarController>();
car.readKeyboard = false;
car.throttle = 1f;     // 가속
car.steer = 0.5f;      // 오른쪽
Debug.Log(car.speedKmh);
```

## 래그돌 표적 (RagdollTarget · RagdollShooter)

[Ragdoll Wizard](RAGDOLL.md) 로 바디 11 개를 만들어 **꺼 둔다** (바디가 애니메이션을 따라감 — 서 있다).
`RagdollShooter` (Main Camera) 가 클릭한 화면 자리로 광선 (`Camera.ScreenPointToRay`) 을 쏘아 `RagdollTarget` 을 맞히면:
Ragdoll 을 켜고 (Animator 는 자동으로 꺼진다) 맞은 자리에 가장 가까운 바디를 광선 방향으로 민다 (`force`, N·s — 바디가 다이내믹이 된 다음 스텝에).
Character Controller · ThirdPersonController 가 있으면 함께 끈다. 다른 Rigidbody 를 맞히면 그 자리를 민다.

| RagdollTarget | 기본 | 내용 |
|---|---|---|
| `hitsToFall` | 1 | 몇 번 맞으면 쓰러지나 |
| `recoverAfter` | 0 | 쓰러진 뒤 다시 서기까지 (초, 0 = 그대로) — `Recover()` 로 직접 |

```csharp
var shooter = Camera.main.GetComponent<StarterAssets.RagdollShooter>();
shooter.readMouse = false;
shooter.ShootAt(target.GetComponent<Animator>().GetBonePosition(HumanBodyBones.Chest));
```

함께 생긴 C# API (Unity 와 같은 이름): `Camera.ScreenPointToRay · ViewportPointToRay`, `Rigidbody.AddForceAtPosition`.

## 검사

`Tools/tests/run_tests.ps1 -Only starter` — 차: 프리팹 · 재질 저장, 다음 차 = 인스턴스, 바퀴 4 · CarController · Follow Camera · 1200 kg,
네 바퀴로 쉬기, 가속 (12 m/s, 카메라가 뒤 9 m), 오른쪽 조향 (90°, 똑바로), S = 멈춤 → 후진,
래그돌 표적: 바디 11 (꺼짐) · RagdollTarget · RagdollShooter, ScreenPointToRay (가운데 = 앞, 가장자리 = 가로 시야각의 절반), 쏘면 쓰러져 밀려남 (9 항목).
