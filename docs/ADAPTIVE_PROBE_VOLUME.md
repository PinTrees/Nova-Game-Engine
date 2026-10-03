# Adaptive Probe Volume (실시간 간접광)

Unity 6 의 **Adaptive Probe Volume (APV)** 와 같은 자리 · 이름의 확산 간접광입니다. 다만 **굽지 않습니다** — 장면 · 빛 · 물체가 바뀌면 몇 프레임 만에 저절로 따라갑니다. 큰 씬도 카메라 둘레 단계만 계산하므로 비용이 같습니다. 레거시 Light Probe Group · Light Probe Proxy Volume · Lighting 창의 굽기는 없습니다 (이것만 씁니다).

![Adaptive Probe Volume](../Showcase/185_Adaptive_Probe_Volume_실시간_간접광.webp)

## 쓰는 법

1. **GameObject > Light > Adaptive Probe Volume** (또는 Add Component > Rendering > Adaptive Probe Volume). 씬에 하나면 됩니다
2. 끝 — 굽기 버튼이 없습니다. 방 안은 하늘이 가려져 어두워지고, 해를 받는 벽의 색이 옆 물체 · 바닥에 번지고, 모서리 · 물체 밑이 은은하게 어두워집니다
3. 빛 (해 방향 · 색 · 점광) 을 바꾸면 그 프레임부터, 물체를 옮기거나 지우면 그 근처 단계를 다시 찍어 (8 판 / 프레임) 곧 따라갑니다

없으면 지금처럼 하늘 환경광 (Unity 와 같음 — APV 가 없는 씬).

## Inspector

| 항목 | 뜻 |
|------|-----|
| Mode | **Global** = 카메라를 따라 어디든, **Local** = Size 상자 안만 |
| Probe Spacing | 가장 촘촘한 단계의 프로브 간격 (기본 1 m — Unity 의 Min Probe Spacing). 단계마다 4 배 |
| Cascades | 단계 수 1 ~ 3. 3 이면 32 · 128 · 512 m 를 덮는다 (Coverage 에 표시) |
| Rays Per Probe | 프로브마다 프레임당 광선 (16 · 32 · 64) |
| Update Speed | 새 결과의 비중 (0.02 ~ 1). 크면 빨리 따라가고 조금 흔들림 |
| Validity Threshold | 광선이 이 비율 넘게 면 뒤에 맞으면 물체 속 프로브로 보고 이웃 값으로 채움 (기본 0.5) |
| Intensity Multiplier · Normal Bias · View Bias | Unity 의 Probe Volumes Options 와 같은 뜻 (Normal Bias 기본 0.33 m) |

고르면 Scene 뷰에 단계 상자 (가까울수록 진하게), Local 이면 Size 상자.

## 동작 (엔진 안)

- **단계**: 카메라 둘레 32 x 16 x 32 프로브 (L1 SH), 간격 Probe Spacing x 4^단계. 카메라가 4 칸 움직이면 원점을 옮기고 예전 값을 새 자리로 옮겨 담는다
- **장면 → 복셀** (단계마다 64 x 32 x 64, 프로브 한 칸 = 2 복셀): 장면을 얇은 판 (2 m) 으로 6 방향 직교로 찍어 (빛 없이 표면 색 · 노멀 · 깊이) 복셀에 넣는다. 복셀마다 **면의 정확한 평면** (복셀 안 위치 + 노멀) 을 **6 칸** (노멀의 가장 큰 축 x 부호: +X −X +Y −Y +Z −Z) 에 둔다 — 방 모서리의 두 벽 · 바닥과 벽 · 얇은 벽의 두 면이 한 복셀에 있어도 다투지 않는다. 판 찍기는 Game 뷰 그리기와 같은 길 (그림자 · 하늘 · 투명 · 물 · 입자 없이)
  - **발광 재질**: 판이 발광 렌더러 (재질 Emission 이 0 이 아닌 Mesh Renderer) 와 겹치면 그 판을 발광만 한 번 더 찍어 **발광 복셀** (선형 HDR) 에 넣는다 — 발광 렌더러가 없으면 비용 0. 재질의 발광을 켜거나 바꾸면 단계를 다시 짓는다
  - 장면이 바뀐 단계 (렌더러가 옮겨짐 · 생김 · 지워짐 — `SceneCulling` 이 알려 줌) 와 카메라를 따라 옮겨진 단계는 프레임당 8 판으로 빨리 다시, 평소엔 1 판씩 천천히 (앞 · 뒤 버퍼)
- **매 프레임** (한 단계씩 돌아가며): 복셀을 엔진 Lit 그대로 다시 비춘다 (직접광 · 그림자 · **프로브 간접광** → 여러 번 튐) + 발광 복셀 → 프로브마다 광선을 복셀 속으로 걸어 맞은 복셀의 빛 / 하늘을 SH 로 모아 섞는다 (면 뒤에 맞은 광선은 빛 0 — 벽 속 프로브 판정) → 물체 속 프로브는 벽을 넘지 않는 이웃 값으로 채움 (Unity 의 Dilation)
- **그리기** (`ShadeLit` 의 확산 환경광): 둘레 8 프로브를 직접 섞는다 — 물체 속 프로브는 가중치를 낮추고, 면 뒤 프로브 · **벽 너머 프로브** (면 → 프로브 사이 네 점이 물체 속) 는 뺀다 (Unity APV 의 Leak Reduction). 물체 속 판정 (`GIBlocked`): 같은 축의 두 면은 둘 다 뒤 (얇은 벽), 다른 축끼리는 어느 하나라도 뒤 (방 안쪽 모서리 = 두 벽 중 하나의 속). 반사 방향의 프로브 빛으로 하늘 반사도 가린다 (닫힌 방 바닥이 하늘을 비추지 않게)
- 컴퓨트 · UAV 없이 픽셀 셰이더 · 렌더 타깃만 — DirectX 11 · OpenGL 같은 결과
- 비용 (Debug, 단계 3): CPU 약 2 ms (판 찍기 · 다시 비추기), GPU 몇 ms. 장면이 바뀔 때만 판 찍기가 늘어난다
- 파일: `Source/Scene/AdaptiveProbeVolume.*` (컴포넌트), `Source/Graphics/DX11/ProbeVolumes.*` (단계 · 복셀 · 발광 · 갱신 · CLI), `Shaders/55. ProbeVolume.fx` (넣기 · 발광 넣기 · 다시 비추기 · 광선 · 옮기기 · 채우기), `Shaders/32. InstancedBasic.fx` (`ProbeVolumeAmbient` · `GIBlocked` · GI 찍기 모드 2 = 발광만)

## CLI

```bash
nova create empty --name APV
nova add-component APV AdaptiveProbeVolume
nova set APV --component AdaptiveProbeVolume --values '{"probeSpacing":0.5,"cascades":3,"raysPerProbe":32}'
nova probevolume info
nova probevolume probe --position 0,1.5,0
```

- `probevolume info`: 켜졌는지, 단계마다 원점 · 간격 · 복셀이 준비됐는지 · 짓는 중인 판, 발광 렌더러 수 (`emissiveRenderers`) · 발광 찍기 수 (`emissionCaptures`)
- `probevolume probe --position x,y,z`: 그 자리 프로브 (L0 · 위쪽 조도 · 유효도) 와 복셀 (알베도 · 노멀 · 빛) — DirectX 11 만
- `probevolume voxels`: 단계마다 찬 복셀 수
- `probevolume debug --view 1|2|0 [--nodirect true]`: 진단 보기 (1 = 프로브 빛만, 2 = 섞은 방법 색), 다시 비출 때 직접광 끄기
- JSON 키: `mode` (Global · Local) · `size` · `probeSpacing` · `cascades` · `raysPerProbe` · `updateSpeed` · `validityThreshold` · `intensityMultiplier` · `normalBias` · `viewBias`

## 아직 · 한계

- 발광은 Mesh Renderer 의 엔진 재질만 (Shader Graph · 패키지 셰이더 · Skinned Mesh Renderer 의 발광은 아직)
- 복셀은 단계 0 에서 0.5 m: 아주 얇은 틈 · 작은 물체는 빛을 다 막지 못한다 (Probe Spacing 을 줄이면 촘촘해짐). 볼록한 기둥 모서리 바로 바깥은 조금 더 가려질 수 있다 (다른 축 면은 오목 모서리로 본다)
- 판 안에서 다른 물체 뒤에 가려진 면은 그 방향 판에서 빠진다 (다른 방향 판이 대개 채운다)
- 투명 물체 · 입자 · 물은 간접광을 막거나 내지 않는다
- C# API (`ProbeReferenceVolume` 등) 없음
