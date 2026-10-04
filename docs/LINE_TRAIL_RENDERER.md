# Line Renderer · Trail Renderer

Unity 의 Line Renderer · Trail Renderer 와 같은 이름 · 값. Add Component > Effects, GameObject > Effects > Line · Trail.

| 값 | Line | Trail | 하는 일 |
|---|---|---|---|
| Positions · Loop · Use World Space | ✓ | | 점 목록 (Loop = 마지막 → 처음을 잇는다, 끄면 Transform 기준) |
| Time · Min Vertex Distance · AutoDestruct · Emitting | | ✓ | 점이 사라지기까지 (초), 이만큼 움직여야 새 점, Play 중 다 사라지면 GameObject 삭제, 끄면 점을 더하지 않는다 |
| Width (곡선) × Width Multiplier | ✓ | ✓ | 선을 따라 길이 비율 0 → 1 (Trail 은 물체 쪽이 0) |
| Color (그라디언트) | ✓ | ✓ | 같은 비율로 |
| Corner Vertices | ✓ | ✓ | 0 = 뾰족한 이음 (미터, 4 배까지), 1 이상 = 꺾인 바깥을 둥글게 |
| End Cap Vertices | ✓ | ✓ | 끝을 반원으로 (너비의 반만큼 늘어난다) |
| Alignment | ✓ | ✓ | View = 카메라를 향함, Transform Z = 물체의 Z 축을 향함 |
| Texture Mode · Texture Scale | ✓ | ✓ | Stretch (전체에 한 번) · Tile (월드 길이마다) · Distribute Per Segment · Repeat Per Segment |
| Materials: Texture · Blend Mode | ✓ | ✓ | Particle System 의 Renderer 와 같은 방식 — 텍스처가 비면 흰색 (Unity 의 Default-Line), Alpha Blended · Additive |

- 띠는 CPU 가 프레임마다 만들어 Particle System 과 같은 투명 패스에서 먼 것부터 함께 그린다 (`43. Particle.fx` 의 `LineAlphaTech` · `LineAdditiveTech` — 길이 방향 반복 샘플러, 빛 없음).
- Trail 은 Play 중이면 게임 시간 (일시 정지에 멈춤), 아니면 에디터 시간으로 점을 더한다 (Scene 뷰에서 물체를 옮겨도 꼬리). Play 시작 · 끝에 지운다.
- 저장: `positions`, `widthCurve`, `widthMultiplier`, `colorGradient`, `numCornerVertices`, `numCapVertices`, `alignment`, `textureMode`, `textureScale`, `texture`, `blend`, `time`, `minVertexDistance`, `autodestruct`, `emitting`.

## C# (Unity 와 같은 이름)

```csharp
var line = gameObject.AddComponent<LineRenderer>();
line.SetPositions(new[] { new Vector3(0, 0, 0), new Vector3(1, 1, 0) });
line.startWidth = 0.1f; line.endWidth = 0.5f;
line.startColor = Color.red; line.endColor = Color.yellow;
line.numCornerVertices = 4; line.numCapVertices = 4; line.loop = false;

var trail = GetComponent<TrailRenderer>();
trail.time = 0.5f; trail.Clear(); trail.AddPosition(transform.position);
```

`positionCount` · `GetPosition` · `SetPosition` · `GetPositions` · `widthMultiplier` · `alignment` (`LineAlignment`) · `textureMode` (`LineTextureMode`) · `useWorldSpace` · `emitting` · `minVertexDistance` · `autodestruct` · `enabled`.
엔진이 이름으로 내보낸 `NovaLine_*` · `NovaTrail_*` 함수 (DllImport) — 안드로이드도 같다.

## 검사 (`run_tests.ps1 -Only linetrail`)

굵기 (0.3 m → 화면 픽셀 기댓값), C# 의 startWidth · endWidth 로 가늘어짐 · 색, 같은 프레임에 C# 로 만든 선, End Cap 이 너비의 반만큼 늘어남,
Trail 이 움직인 길을 따름 · Time 뒤 사라짐 · AddPosition · Clear, 저장 → 다시 열기, Game 뷰.
