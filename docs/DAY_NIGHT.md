# 낮 · 밤 순환 (com.nova.daynight)

장면에 **Day Night Cycle** 컴포넌트 하나를 두면 시각이 흐르며 장면 전체가 바뀐다 (전역 — 하나만 쓴다).
**새벽 → 아침 → 낮 → 저녁 → 노을 → 밤 → 은하수 → 새벽 …** 단계마다 모습이 있고 그 사이를 부드럽게 섞는다.
날씨 패키지 (com.nova.weather) 와 함께 쓰면 둘이 곱해진다 (먹구름 밤 = 별이 가려진 어두운 밤).

<p align="center"><img src="images/daynight.webp" width="900"/><br/><sub>새벽 (동쪽) · 낮 · 노을 (서쪽) · 밤 · 은하수 (달과 함께)</sub></p>

**Package Manager > Day Night Cycle** 를 넣고 **Add Component > Environment > Day Night Cycle**.

## 단계

| 단계 | 시작 | 모습 |
|---|---|---|
| 새벽 Dawn | 4:30 | 해가 뜨기 전 — 동쪽 지평이 붉고 별이 사라져 간다 |
| 아침 Morning | 6:30 | 따뜻하고 낮은 해 (6 시에 동쪽에서 뜬다) |
| 낮 Day | 10:00 | 스카이박스 · 빛 그대로 |
| 저녁 Evening | 16:00 | 해가 기울며 노랗게 |
| 노을 Sunset | 18:00 | 해가 진 (18 시) 서쪽 하늘이 주황 · 붉게, 위는 보라 |
| 밤 Night | 19:30 | 푸른 달빛, 별 |
| 은하수 Milky Way | 23:00 | 가장 어두운 밤 — 별과 은하수가 가장 밝다 |

단계마다 (Inspector 의 **Looks**): Sun Color · Intensity, Ambient, Skybox Brightness, Gradient (스카이박스 → 천정 · 지평 그라데이션), Zenith · Horizon,
Glow (해 쪽 지평 빛) · Strength, Stars, Milky Way, Fog (안개 색 배율). **Reset Looks** = 기본.

## 항목

| 항목 | 내용 |
|---|---|
| Time Of Day | 0 ~ 24 시. 단계 단추로 바로 가기. 편집 중에는 멈춰 그 시각을 보여 준다 |
| Day Length (min) | Play 중 실제 몇 분에 하루 (0 = 멈춤), Paused |
| Sun Light | 해로 돌릴 Directional Light (비우면 장면의 첫 Directional Light) — 해가 지면 반대편 **달빛** (그림자도 따라온다). 지평 가까이에서 세기가 0 이라 바뀔 때 튀지 않는다 |
| Sun Azimuth | 해가 뜨는 방위 (0 = +X 에서 떠서 -X 로) |
| Max Elevation | 정오의 해 높이 (정오에는 -Z 쪽) |
| Stars · Milky Way | 밤하늘 밝기 배율 |

## 그리기

엔진의 `DayNightState` (Source/Graphics/Common) 를 프레임마다 쓴다 — 장면에 저장되지 않고, 패키지가 없거나 꺼지면 아무것도 바뀌지 않는다.
- 빛: 방향광 · 환경광 · 반사 (32. InstancedBasic 의 WeatherSkyGrade) · 물 · 안개 색 (하늘색 모드는 하늘처럼 어둡게) — 날씨 값에 곱한다 (`WeatherState::SkyScale · AmbientScale · ApplySun`)
- 하늘 (`Shaders/21. Sky.fx`): 스카이박스 위에 천정 · 지평 그라데이션, 해 쪽 지평 빛, 해 · 달 원반, 별 두 겹 (방향의 3D 칸마다 하나 — 약 1 화소, 반짝임), 은하수 (기울어진 큰 원을 따라 fbm 구름 + 먼지 띠 + 밝은 중심, 별이 더 많다). 먹구름이면 별 · 달이 가려진다
- DirectX 11 · OpenGL · Vulkan 같은 값

## C# · CLI

```csharp
DayNight.timeOfDay = 18.5f;                  // 노을
DayNight.dayLengthMinutes = 12f;
DayNight.SetPhase(DayPhase.MilkyWay);
if (DayNight.isNight) streetLights.SetActive(true);
float elevation = DayNight.sunElevation;     // 도 (밤 = 음수)
```

`DayNight.exists · timeOfDay · dayLengthMinutes · paused · phase (DayPhase) · sunElevation · sunDirection · isNight · sunAzimuth · starBrightness · milkyWayBrightness · SetPhase`

CLI: `nova daynight status` · `nova daynight set --time 18.5 [--minutes 24] [--paused true] [--azimuth 0]` · `nova daynight phase --name Sunset`
(시각 · 단계 · 해 높이 · 해 방향 · 빛 방향 · 세기 · 별 · 은하수 · 달).

## 검사

`Tools/tests/run_tests.ps1 -Only daynight` — 시각 → 단계 이름, 정오 = 해 / 자정 = 달 (빛이 위에서), 낮 하늘 밝음 · 밤 어두움, 노을 (서) · 새벽 (동) 지평이 붉다,
은하수 시각에 별 (밝은 점 수백), Play 중 7 단계가 차례로, C# API (8 항목).

## 아직 없는 것

달의 위상, 계절 · 위도 (해 길이), 별자리, 구름의 해 · 달 빛 받기 (스카이박스의 구름은 그라데이션에 섞인다), 시각마다 바꾸는 반사 프로브.
