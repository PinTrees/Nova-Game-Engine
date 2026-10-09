# Prototype 패키지 (블록아웃)

엔진 기본 패키지 `Resources/Packages/Prototype` (Project 창의 **Packages > Prototype**).
판타지 오픈월드 · 마을 · 성을 빠르게 짜 보는 회색 블록아웃 조각이다. 어느 프로젝트에서나 그대로 쓴다 (경로가 `Resources\...` 라 엔진 폴더에서 읽는다).

## 텍스처 · 재질

`Textures/Prototype_<색>.png` · `Materials/Prototype_<색>.mat` — 흰색에서 어두운 회색까지 다섯 가지:
`Light` · `LightGray` · `Gray` · `DarkGray` · `Charcoal`.

- 1024 px = 1 m. 0.25 m 가는 선, 0.5 m 중간 선, 1 m 테두리, 0.1 m 눈금, 모서리에 "1m".
- 조각 메시의 UV 는 미터 단위다 (면마다 그 면의 평면에 투영). 그래서 조각 크기와 상관없이 격자 한 칸이 1 m 로 보인다.
  기본 Cube 처럼 UV 가 0 ~ 1 인 메시에 입히면 면 하나가 한 칸이다.

## 조각 (Prefabs/ — 메시는 Meshes/ 의 GLB)

![프로토타입 마을](images/prototype_village.webp)

단위 m, 원점은 바닥 가운데. 벽은 X 를 따라 서고 두께 (0.2 m) 는 Z 가운데. 벽 높이 3 m, 지붕은 처마 바닥이 y = 0 이라 3 m 벽 위에 y = 3 으로 놓는다.

| 갈래 | 조각 |
|------|------|
| Shapes (1 m) | Cube · Wedge · Cylinder · Cone · HalfCylinder · Pyramid · Sphere · Stairs |
| Floor | Floor 1x1 · 2x2 · 4x4 (윗면 y = 0), Platform 4x4 (높이 1 m) |
| Walls | Wall 1 · 2 · 4 m, Half 2 m (1 m 높이), Window 2 m, Window Arch 2 m, Window Small 1 m, Door 2 m, Door Arch 2 m, Archway 4 m, Gable 4 · 6 m (박공 세모 벽) |
| Structure | Pillar Square · Round 3 m, Beam 4 m, Chimney, Fence 2 m, Door Slab, Bridge 4 m |
| Stairs | Stairs 2 m (1 m · 3 m 오르기), Ramp 2x4 |
| Roof | Gable 4x4 · 4x8 · 6x8 (박공), Hip 4x4 · 6x6 (모임), Shed 4x2 (외쪽), Cone Tower · Small (탑 고깔) |
| Castle | Castle Wall 4 m (총안), Castle Gate 4 m (아치), Tower Round 4 m (지름 4 m, 쌓아 올린다), Tower Round Top (총안), Tower Square 4x4 |
| Props | Barrel · Crate 1 m · Table |
| Nature | Tree Pine · Tree Round · Rock Large · Rock Small · Bush |

갈래마다 색이 다르다 (벽 Light, 바닥 Gray, 지붕 Charcoal, 기둥 · 소품 DarkGray …). 재질은 Inspector 에서 바꾸면 된다.

## 다시 만들기

```bash
python Tools/prototype/make_prototype.py
```

```bash
powershell -File Tools/prototype/bake_prefabs.ps1
```

- `make_prototype.py` — 텍스처 · 재질 · GLB 메시 (조각 목록은 `pieces()`).
- `bake_prefabs.ps1` — 창 없는 검사 에디터로 메시를 놓고 재질 · 그림자를 정해 프리팹으로 저장한다 (`nova prefab save`).

## CLI

- `nova prefab place --path Resources/Packages/Prototype/Prefabs/Walls/Wall_Door_2m.prefab --position 0,0,-2 --rotation 0,90,0`
- `nova prefab save --target <오브젝트> --path Assets/X.prefab`
