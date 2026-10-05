# Tessellation (재질 높이 변위)

Lit 재질의 **Height Map** 만큼 표면을 실제로 밀어냅니다 (HDRP Lit 의 Displacement Mode = Tessellation Displacement). 벽돌 벽 · 자갈 바닥 · 바위 표면이 가까이서 진짜 입체가 되고, 옆에서 보면 윤곽이 울퉁불퉁하며 그림자도 그 모양을 따릅니다.

![돌 벽 · 자갈 바닥 — 왼쪽 끔 (평면 + 색), 오른쪽 켬 (높이 맵만큼 밀어냄 — 줄눈이 들어가고 벽돌 · 자갈이 나온다)](images/tessellation_compare.webp)

![옆에서 본 벽 (윤곽이 벽돌 모양), 벽면 안에서 본 평평한 벽 (두께가 없어 안 보인다) · 민 벽 (벽돌이 면 밖으로 나온다)](images/tessellation_silhouette.webp)

## 쓰는 법

1. 재질 (Lit) 의 **Surface Options > Displacement Mode** 를 **Tessellation Displacement** 로
2. **Surface Inputs > Height Map** 에 높이 맵 (회색, 흰색 = 높다) — 오른쪽 칸 = **Amplitude** (m, 흰색과 검정의 높이 차)
3. **Base** (0..1): 원래 면의 자리. 0.5 면 회색은 그대로, 밝은 곳은 나오고 어두운 곳은 들어간다
4. **Tessellation Options**: Tessellation Factor (변 하나를 최대 몇 조각으로, 1 ~ 64) · Triangle Size (원하는 삼각형 변, 1080p 화면의 픽셀) · Fade Distance (이 거리 너머는 나누지 않는다, m)

| 재질 값 (`.mat`) | 기본 | |
|---|---|---|
| `DisplacementMode` | `None` | `Tessellation` 이면 민다 |
| `HeightMapPath` | | 높이 맵 |
| `HeightAmplitude` | 0.05 | 높이 (m) |
| `HeightBase` | 0.5 | 원래 면의 자리 |
| `TessellationFactor` | 16 | 최대 나눔 |
| `TessellationTriangleSize` | 12 | 픽셀 |
| `TessellationFadeDistance` | 50 | m |

메시는 너무 거칠면 안 된다: 삼각형 하나가 최대 나눔 (16 이면 변 하나 16 조각) 보다 훨씬 크면 높이 맵을 다 담지 못한다. 벽 · 바닥은 Unity 의 Plane (10 × 10 칸) 처럼 몇 m 마다 정점이 있으면 된다. 메시의 모서리가 갈라진 곳 (Cube 의 면 경계처럼 정점이 따로인 곳) 은 면마다 다른 법선으로 밀려 틈이 생긴다 — HDRP 와 같다.

## 하는 일

| | |
|---|---|
| 나눔 | 카메라에 가까울수록 잘게 (원하는 변 길이 = Triangle Size 픽셀, 거리만큼 길게). 변마다 가운데 점의 거리로 정해 이웃 삼각형과 같은 값 → 틈이 없다. Fade Distance 의 끝 1/4 에서 1 로 줄어든다 |
| 변위 | (높이 − Base) × Amplitude 만큼 법선 쪽으로. 높이 맵의 밉은 정점 간격에 맞춘다 — 정점보다 잘게 바뀌는 높이는 흐린 밉으로 (날카로운 줄눈이 톱니가 되지 않는다) |
| 음영 | 픽셀마다 높이 맵의 기울기로 법선 (정점 사이의 잔무늬까지 빛이 따른다). Normal Map 을 함께 쓰면 그 위에 더한다 |
| 깊이 · 그림자 | 깊이 프리패스 (SSAO · EQUAL 깊이 검사) · 그림자 패스도 같은 함수로 같은 자리를 민다. 자리 계산은 `precise` + 덧셈 · 곱셈만 (OpenGL 은 `invariant gl_Position`) — 셰이더마다 비트까지 같아야 본 패스가 깊이 검사를 통과한다 |
| 그리기 | GPU 인스턴싱 (MeshBatcher) 그대로 — 같은 메시 · 재질은 한 번에. 패치 (3 정점) 로 그린다 |

### 쌓인 눈 (Weather)

[날씨](WEATHER.md) 의 눈이 쌓이면 **지형** 도 테셀레이션으로 실제로 올라간다: 눈 덮임 × Snow Depth 만큼, 발자국 자리는 땅까지. 가까운 40 m 만 잘게 (10 픽셀 삼각형). 깊이 프리패스 · 그림자는 맨 지형 그대로 — 눈은 위로만 쌓이니 본 패스가 늘 그 앞이고 (LESS_EQUAL), 그 위의 캐릭터 발은 눈에 묻힌다.

![지형의 눈 — 위에서 · 낮게: RigidBody 공이 지나간 자국이 실제로 파여 있고 테두리가 매끈하다, 공은 눈에 묻힌다](images/tessellation_snow.webp)

## 백엔드

| | |
|---|---|
| DirectX 11 | hs_5_0 · ds_5_0 |
| OpenGL 4.5 · Vulkan | 같은 .fx → DXC (hs_6_0 · ds_6_0) → SPIR-V → GLSL (TCS · TES) / Vulkan 파이프라인 |
| OpenGL ES 3.2 (안드로이드) | 같은 GLSL ES (TCS · TES, `invariant gl_Position`). 테셀레이션이 없는 기기는 그 기법을 만들지 못한다 → 보통 그리기로 (재질은 평면 + 색, 지형 눈은 시차 발자국 — `FxPass::IsUsable`) |

## 한계

- MeshBatcher 로 그리는 메시만 (보통의 정적 · 움직이는 Mesh Renderer). 스킨 메시 · 투명 · Alpha Clipping · Shader Graph 재질은 테셀레이션 없이 그린다
- 높이 맵은 R 채널. 가져오기 설정이 sRGB 면 값이 감마로 바뀐다 — 높이 맵은 Import Settings 에서 sRGB 를 끄는 것이 좋다
- 지형 재질 (Terrain Layer) 의 높이 변위는 아직 없다 (지형은 눈만)

## 검사

```bash
powershell -ExecutionPolicy Bypass -File Tools\tests\run_tests.ps1 -Only tessellation
```

```bash
powershell -ExecutionPolicy Bypass -File Tools\tests\android_tessellation.ps1
```

PC 6 개 (OpenGL · Vulkan 은 눈 지형 빼고 5 개): 켬 · 끔의 표면이 다르다, 검은 얼룩 없음 (깊이 프리패스 = 본 패스), 벽면 안에서 본 벽돌 윤곽, 가까우면 잘게 · Fade Distance 너머는 그대로, 쌓인 눈의 지형 (삼각형이 늘고 공 자국이 파인다), 테셀레이션 셰이더 오류 없음.
안드로이드: GLES 셰이더 (32 · 28 · 26 의 TCS · TES · invariant), 게임 데이터의 높이 맵, APK, 기기 그림을 DX11 기준과 비교. `-SkipBuild` = 지난 APK 로 기기만.
