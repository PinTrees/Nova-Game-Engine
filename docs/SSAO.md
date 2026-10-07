# Screen Space Ambient Occlusion (SSAO)

Unity URP 의 **Screen Space Ambient Occlusion** 과 같은 값을 **Volume** 효과로 다룹니다. 깊이 프리패스의 노멀 · 깊이로 물체가 맞닿은 곳 · 모서리 · 틈의 환경광을 가립니다. Volume 이라 장소마다 (Local Volume) 다르게 섞을 수 있습니다.

![SSAO](images/ssao_volume.webp)

## 쓰는 법

1. 씬의 **Global Volume** (또는 Local Volume) 을 고르고 Profile 의 **Add Override > Lighting > Screen Space Ambient Occlusion**
2. 바꿀 칸의 덮어쓰기를 켜고 값을 줍니다. 기본 = **켜짐** (URP 기본 렌더러처럼), 끄려면 **Enable** 을 끕니다
3. 모든 Lit 재질 · 지형 · 나무 · 바위 · 디테일에 적용됩니다 (재질마다 켜는 칸은 없다 — Unity 와 같음). Game 뷰 · Scene 뷰 모두

## Inspector

| 항목 | 기본 | 뜻 |
|------|------|-----|
| Enable | 켬 | 끄면 AO 맵을 흰색으로 (계산하지 않음) |
| Intensity | 1 | 가려짐 세기 (0 ~ 4, 0 = 없음) |
| Radius | 0.5 m | 표본 반지름 (뷰 공간). 크면 넓게 어두워진다 |
| Direct Lighting Strength | 0.25 | 직접광 (해 · 점광) 에도 곱하는 몫. 0 = 환경광만, 1 = 모두 |
| Samples | Medium | 픽셀마다 표본 Low 4 · Medium 8 · High 14 |
| Falloff Distance | 100 m | 카메라에서 이 거리까지 (끝 20 % 에서 사라진다) |

## 동작 (엔진 안)

- 깊이 프리패스가 뷰 노멀 · 뷰 깊이를 그린다 (`28. SsaoNormalDepth.fx` — SSR · 데칼 · DoF 도 이것을 쓴다)
- **반 해상도** AO (`28. Ssao.fx`): 픽셀의 뷰 위치를 깊이와 화면 구석 방향으로 되짚고, 노멀 쪽 반구에 고르게 퍼진 표본 (정육면체 꼭짓점 · 면 14 방향을 픽셀마다 무작위 방향으로 반사) 을 놓아 그 자리의 깊이가 앞을 막는 만큼 가린다. AO = 1 − 가림 × Intensity × (거리 페이드)
  - 화면 구석 방향은 **그 프레임 카메라의 투영에서** — 시야각 · 화면비를 바꿔도 바로 맞다
- 가장자리를 지키는 흐림 (노멀 · 깊이가 다른 이웃은 섞지 않음) 가로 · 세로 4 번 — Game · Scene 뷰 모두
- `32. InstancedBasic.fx` 의 `ShadeLit`: 환경광 (하늘 · Adaptive Probe Volume) × AO, 직접광 × lerp(1, AO, Direct Lighting Strength)
- 반사 프로브 · APV 찍기 · 재질 미리보기에는 없다 (흰 맵)
- 컴퓨트 없이 픽셀 셰이더만 — DirectX 11 · OpenGL 같은 결과 (검사에서 AO 맵 차이 0)
- 파일: `Source/Graphics/DX11/Ssao.*` (타깃 · 계산 · 흐림 · Volume 값), `Source/Graphics/Common/VolumeProfile.cpp` (효과 정의), `Source/Editor/EditorApp.cpp` (두 뷰)

## 예전과 달라진 것 (2026-10-08)

- AO 맵은 계산했지만 **어떤 재질에도 쓰지 않았다** (재질의 숨은 `UseSsaoMap` 칸이 기본 0) → 모든 Lit 에 적용
- 표본이 **1 개** (`PS(1)`) 에 `pow(…, 16)` 로 대비를 올려 거의 0 / 1 인 노이즈를 흐림이 뭉갰다 → 4 · 8 · 14 표본, 세기는 Intensity
- 무작위 방향 텍스처에 float 를 RGBA8 로 그대로 올렸고, 방향을 정규화하지 않았다
- 화면 구석 방향을 뷰를 만들 때의 시야각 · 크기로만 정해, 시야각이 바뀌면 위치를 잘못 되짚었다
- Scene 뷰는 흐림 없이 노이즈 그대로였다
- 값이 셰이더에 고정 (반지름 0.5 m 등) → Volume 으로

## CLI

```bash
nova ssao info
nova ssao map ao.png --view game
nova set "Global Volume" --component Volume --values '{"profile":"Assets/Settings/MyProfile.volumeprofile"}'
```

- `ssao info`: 이번 프레임에 쓴 값 (enabled · active · intensity · radius · directLightingStrength · samples · falloffDistance) · AO 맵 크기
- `ssao map <png> [--view game|scene]`: AO 맵을 회색 PNG 로 (흰 = 가림 없음) + 평균 · 최소
- Volume Profile JSON 의 효과 이름 `AmbientOcclusion`, 키: `enabled` · `intensity` · `radius` · `directLightingStrength` · `samples` (0 · 1 · 2) · `falloffDistance`

## 검사

`Tools/tests/run_tests.ps1 -Only ssao` — 바닥 위 상자 · 벽:
끄면 AO 맵이 흰색, 켜면 맞닿은 곳만 어둡고 평평한 바닥 · 하늘은 1 (자기 가림 없음), 최종 그림도 맞닿은 곳만 어두워짐,
Intensity 2 > 1 · 0 = 없음, Radius 1.5 m 가 더 넓게, Samples 4 · 14, Falloff Distance 1 m = 없음, Direct Lighting Strength 1 이 직접광도 가림,
Game 뷰 = Scene 뷰 · 시야각을 좁혀도 바닥이 가려지지 않음, OpenGL = DirectX 11 (10 항목).

## 아직 · 한계

- 화면 공간의 본래 한계: 화면 밖 · 앞의 물체에 가려진 것은 가리지 못한다. 물체 윤곽 (깊이가 크게 끊기는 곳) 에 얇은 선이 남을 수 있다
- 시간 누적 (TAA 와 함께 표본 돌리기) · 파란 노이즈 · 전체 해상도 선택 · After Opaque (URP) 는 아직
