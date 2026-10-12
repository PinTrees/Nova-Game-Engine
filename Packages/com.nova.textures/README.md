# Texture Pack — Materials (com.nova.textures)

CC0 PBR 재질 (ambientCG) 을 분류별로 모은 텍스처 팩입니다. 원본을 그대로 쓰고 (색 · 거칠기 · AO · 높이를 바꾸지 않는다), 파일 형식만 1024² WebP 로 바꿨습니다.

- 재질마다 `Textures/<분류>/<이름>/` 에 `color.webp` · `normal.webp` (DirectX) · `roughness.webp` · `ao.webp` · `height.webp` (· `metalness.webp` · 잎은 `opacity.webp`, 비율 그대로)
- `catalog.json` = 재질 목록: **id** (`분류/이름`, 바뀌지 않는다) · 이름 · 분류 · 태그 · 설명 · 실제 크기 (`tileMeters`) · 원본 (ambientCG 자산 이름 · 주소 · 라이선스)
- 엔진은 처음 쓸 때 GPU 형식 (BC1 · BC7 · BC3 · BC4) 으로 바꿔 프로젝트 `Library` 에 캐시합니다 — 패키지 파일은 WebP 그대로. 높이 맵이 없으면 노멀맵에서 만든다 (테셀레이션)
- 찾기 (AI · CLI): `nova textures categories` · `nova textures list --category ground` · `nova textures search --query "mossy stone wall"` · `nova textures info --id rock/cliff-grey`
- 쓰기: Building Generator — `nova building set --target <건물> --values {"materials":{"roof":"roof/clay-tiles-mossy"}}` · 지형 그래프 레이어 · 바위 · 나무
- 더하기: 폴더 하나 + `catalog.json` 한 줄 (같은 형식). 새 분류도 그대로 (id 의 앞부분)

재질 134 개 · 분류 10 개.

## 지붕 (Roof) — 35

| id | 이름 | 설명 | 크기 (m) | 원본 |
|---|---|---|---|---|
| `roof/stone-fish-scale` | Fish-Scale Stone Roof | Grey round-cut stone shingles — turrets, towers, wizard roofs | 2.0 | [RoofingTiles001](https://ambientcg.com/a/RoofingTiles001) |
| `roof/stone-fish-scale-weathered` | Weathered Fish-Scale Stone Roof | Old round-cut stone shingles with lichen | 2.0 | [RoofingTiles002](https://ambientcg.com/a/RoofingTiles002) |
| `roof/slate` | Slate Roof | Grey slate in staggered courses — castles, chapels, manors | 2.0 | [RoofingTiles003](https://ambientcg.com/a/RoofingTiles003) |
| `roof/stone-scale-weathered` | Weathered Stone Scales | Small worn stone scales, pale grey | 2.0 | [RoofingTiles004](https://ambientcg.com/a/RoofingTiles004) |
| `roof/slate-small` | Small Slates | Fine small slates in many thin courses | 2.0 | [RoofingTiles005](https://ambientcg.com/a/RoofingTiles005) |
| `roof/clay-tiles-weathered` | Weathered Clay Tiles | Old terracotta barrel tiles with dirt streaks — town houses, inns | 2.5 | [RoofingTiles006](https://ambientcg.com/a/RoofingTiles006) |
| `roof/clay-tiles-mossy` | Mossy Clay Tiles | Terracotta barrel tiles with green moss streaks | 2.5 | [RoofingTiles007](https://ambientcg.com/a/RoofingTiles007) |
| `roof/clay-tiles-dirty` | Dirty Clay Tiles | Terracotta barrel tiles stained dark by soot and rain | 2.5 | [RoofingTiles008](https://ambientcg.com/a/RoofingTiles008) |
| `roof/dark-tiles-mossy` | Dark Mossy Tiles | Dark grey tiles with moss — cold, wet towns | 2.5 | [RoofingTiles009](https://ambientcg.com/a/RoofingTiles009) |
| `roof/glazed-tiles-mossy` | Glazed Mossy Tiles | Blue-grey glazed tiles with moss | 2.5 | [RoofingTiles010](https://ambientcg.com/a/RoofingTiles010) |
| `roof/flat-tiles-red` | Red Plain Tiles | Flat red plain tiles, even courses | 2.0 | [RoofingTiles011A](https://ambientcg.com/a/RoofingTiles011A) |
| `roof/flat-tiles-red-weathered` | Weathered Red Plain Tiles | Flat red plain tiles, faded and stained | 2.0 | [RoofingTiles011B](https://ambientcg.com/a/RoofingTiles011B) |
| `roof/flat-tiles-red-mossy` | Mossy Red Plain Tiles | Flat red tiles with moss and broken pieces | 2.0 | [RoofingTiles011C](https://ambientcg.com/a/RoofingTiles011C) |
| `roof/flat-tiles-orange` | Orange Plain Tiles | Flat orange clay tiles | 2.0 | [RoofingTiles012A](https://ambientcg.com/a/RoofingTiles012A) |
| `roof/flat-tiles-orange-weathered` | Weathered Orange Plain Tiles | Flat orange tiles, worn | 2.0 | [RoofingTiles012B](https://ambientcg.com/a/RoofingTiles012B) |
| `roof/flat-tiles-orange-broken` | Broken Orange Plain Tiles | Cracked and missing orange tiles — ruins, poor quarters | 2.0 | [RoofingTiles012C](https://ambientcg.com/a/RoofingTiles012C) |
| `roof/flat-tiles-black` | Black Plain Tiles | Flat black glazed tiles | 2.0 | [RoofingTiles013A](https://ambientcg.com/a/RoofingTiles013A) |
| `roof/flat-tiles-black-weathered` | Weathered Black Tiles | Flat black tiles, dull and stained | 2.0 | [RoofingTiles013B](https://ambientcg.com/a/RoofingTiles013B) |
| `roof/flat-tiles-black-mossy` | Mossy Black Tiles | Black tiles with moss patches | 2.0 | [RoofingTiles013C](https://ambientcg.com/a/RoofingTiles013C) |
| `roof/flat-tiles-black-overgrown` | Overgrown Black Tiles | Black tiles covered in moss and lichen | 2.0 | [RoofingTiles013D](https://ambientcg.com/a/RoofingTiles013D) |
| `roof/clay-barrel-tiles` | Clay Barrel Tiles | Rounded clay tiles in long rows — Mediterranean, Roman | 2.5 | [RoofingTiles014A](https://ambientcg.com/a/RoofingTiles014A) |
| `roof/clay-barrel-tiles-weathered` | Weathered Clay Barrel Tiles | Rounded clay tiles, sun-faded and stained | 2.5 | [RoofingTiles014B](https://ambientcg.com/a/RoofingTiles014B) |
| `roof/clay-barrel-tiles-mossy` | Mossy Clay Barrel Tiles | Rounded clay tiles with moss between the rows | 2.5 | [RoofingTiles014C](https://ambientcg.com/a/RoofingTiles014C) |
| `roof/clay-barrel-tiles-broken` | Broken Clay Barrel Tiles | Rounded clay tiles with gaps and moss — ruins | 2.5 | [RoofingTiles014D](https://ambientcg.com/a/RoofingTiles014D) |
| `roof/flat-tiles-grey` | Grey Plain Tiles | Flat violet-grey tiles | 2.0 | [RoofingTiles015A](https://ambientcg.com/a/RoofingTiles015A) |
| `roof/flat-tiles-grey-weathered` | Weathered Grey Plain Tiles | Flat violet-grey tiles, worn | 2.0 | [RoofingTiles015B](https://ambientcg.com/a/RoofingTiles015B) |
| `roof/flat-tiles-grey-mossy` | Mossy Grey Plain Tiles | Flat grey tiles with moss | 2.0 | [RoofingTiles015C](https://ambientcg.com/a/RoofingTiles015C) |
| `roof/thatch` | Thatch | Combed straw thatch — cottages, barns | 2.0 | [ThatchedRoof001A](https://ambientcg.com/a/ThatchedRoof001A) |
| `roof/thatch-dark` | Dark Old Thatch | Old darkened straw thatch | 2.0 | [ThatchedRoof001B](https://ambientcg.com/a/ThatchedRoof001B) |
| `roof/thatch-pale` | Pale Reed Thatch | Fresh pale reed thatch | 2.0 | [ThatchedRoof002A](https://ambientcg.com/a/ThatchedRoof002A) |
| `roof/thatch-mossy` | Mossy Thatch | Old thatch with green moss | 2.0 | [ThatchedRoof002B](https://ambientcg.com/a/ThatchedRoof002B) |
| `roof/wood-shingles` | Wood Shingles | Split wooden shakes, grey with age — Nordic halls, stave churches | 2.0 | [WoodSiding011](https://ambientcg.com/a/WoodSiding011) |
| `roof/wood-shingles-mossy` | Mossy Wood Shingles | Wooden shakes with moss | 2.0 | [WoodSiding010](https://ambientcg.com/a/WoodSiding010) |
| `roof/wood-shingles-dark` | Dark Wood Shingles | Dark tarred wooden shakes | 2.0 | [WoodSiding012](https://ambientcg.com/a/WoodSiding012) |
| `roof/wood-shakes-long` | Long Wood Shakes | Long rough split shakes in deep courses | 2.0 | [WoodSiding013](https://ambientcg.com/a/WoodSiding013) |

## 돌 (Stone) — 19

| id | 이름 | 설명 | 크기 (m) | 원본 |
|---|---|---|---|---|
| `stone/rubble-wall` | Rubble Stone Wall | Rounded field stones set in lime mortar — cottage and town ground floors | 2.0 | [Bricks089](https://ambientcg.com/a/Bricks089) |
| `stone/rubble-wall-dark` | Dark Rubble Wall | Large dark rough stones with deep joints — keeps, towers, foundations | 2.5 | [Bricks076A](https://ambientcg.com/a/Bricks076A) |
| `stone/rubble-wall-dark-blocks` | Dark Rubble Blocks | Dark squared rubble in rough courses | 2.5 | [Bricks076B](https://ambientcg.com/a/Bricks076B) |
| `stone/rubble-wall-black` | Black Rubble Wall | Very dark wet stones with pale mortar — dungeons, sea walls | 2.5 | [Bricks076C](https://ambientcg.com/a/Bricks076C) |
| `stone/fieldstone-stacked` | Stacked Fieldstone | Flat stones stacked with thin joints — barns, walls, plinths | 2.0 | [Bricks102](https://ambientcg.com/a/Bricks102) |
| `stone/polygonal-wall` | Polygonal Stone Wall | Light irregular polygonal stones — southern and coastal houses | 2.0 | [Bricks075A](https://ambientcg.com/a/Bricks075A) |
| `stone/polygonal-wall-dark` | Dark Polygonal Wall | Dark irregular polygonal stones | 2.0 | [Bricks075B](https://ambientcg.com/a/Bricks075B) |
| `stone/sandstone-rubble` | Sandstone Rubble | Warm sandstone rubble in thick mortar | 2.0 | [Bricks078](https://ambientcg.com/a/Bricks078) |
| `stone/sandstone-layered` | Layered Sandstone | Thin layered sandstone slabs | 2.0 | [Bricks083](https://ambientcg.com/a/Bricks083) |
| `stone/sandstone-courses` | Sandstone Courses | Weathered sandstone blocks in long courses | 2.5 | [Bricks091](https://ambientcg.com/a/Bricks091) |
| `stone/rough-sandstone` | Rough Sandstone | Eroded sandstone wall, soft edges | 2.5 | [Bricks095](https://ambientcg.com/a/Bricks095) |
| `stone/rubble-wall-grey` | Grey Rubble Wall | Grey rubble with dirty mortar | 2.5 | [Bricks096](https://ambientcg.com/a/Bricks096) |
| `stone/cyclopean-blocks` | Cyclopean Blocks | Huge irregular sandstone blocks — ancient walls, fortresses | 3.0 | [Bricks100](https://ambientcg.com/a/Bricks100) |
| `stone/ashlar-weathered` | Weathered Ashlar | Large cut stone blocks, worn and stained — castles, chapels, gates | 2.4 | [Bricks043](https://ambientcg.com/a/Bricks043) |
| `stone/ashlar-grey` | Grey Cut Stone | Clean grey cut blocks with fine joints — palaces, trim, columns | 2.4 | [Bricks064](https://ambientcg.com/a/Bricks064) |
| `stone/ashlar-dark` | Dark Cut Stone | Dark grey cut blocks | 2.4 | [Bricks066](https://ambientcg.com/a/Bricks066) |
| `stone/ashlar-cream` | Cream Cut Stone | Pale limestone blocks with dark stains | 2.4 | [Bricks041](https://ambientcg.com/a/Bricks041) |
| `stone/marble-white` | White Marble | Polished white marble with fine veins — temples, columns, floors | 2.0 | [Marble019](https://ambientcg.com/a/Marble019) |
| `stone/marble-grey` | Grey Veined Marble | Grey-blue marble with strong veins | 2.0 | [Marble012](https://ambientcg.com/a/Marble012) |

## 벽돌 (Brick) — 5

| id | 이름 | 설명 | 크기 (m) | 원본 |
|---|---|---|---|---|
| `brick/old-red-brick` | Old Red Brick | Weathered red-brown brick, uneven mortar | 1.6 | [Bricks097](https://ambientcg.com/a/Bricks097) |
| `brick/red-brick` | Red Brick | Even red brick in running bond | 1.6 | [Bricks082A](https://ambientcg.com/a/Bricks082A) |
| `brick/dark-brick` | Dark Brick | Soot-dark brick — industrial quarters | 1.6 | [Bricks073C](https://ambientcg.com/a/Bricks073C) |
| `brick/brown-brick` | Brown Brick | Brown brick with pale mortar | 1.6 | [Bricks054](https://ambientcg.com/a/Bricks054) |
| `brick/pale-old-brick` | Pale Old Brick | Faded pale brick, worn faces | 1.6 | [Bricks030](https://ambientcg.com/a/Bricks030) |

## 회벽 (Plaster) — 7

| id | 이름 | 설명 | 크기 (m) | 원본 |
|---|---|---|---|---|
| `plaster/lime-plaster` | Lime Plaster | Hand-applied lime plaster with soft stains — half-timber infill, cottages | 2.5 | [Plaster005](https://ambientcg.com/a/Plaster005) |
| `plaster/rough-plaster` | Rough Plaster | Coarse rough-cast plaster | 2.5 | [Plaster003](https://ambientcg.com/a/Plaster003) |
| `plaster/plaster-exposed-brick` | Peeling Plaster over Brick | Old white plaster falling off to show brick — poor quarters, ruins | 2.5 | [PaintedPlaster014](https://ambientcg.com/a/PaintedPlaster014) |
| `plaster/peeling-plaster-stone` | Peeling Plaster over Stone | Dirty plaster coming away from stone | 2.5 | [PaintedPlaster016](https://ambientcg.com/a/PaintedPlaster016) |
| `plaster/patched-plaster` | Patched Plaster | White plaster with patched repairs | 2.5 | [PaintedPlaster006](https://ambientcg.com/a/PaintedPlaster006) |
| `plaster/cracked-plaster` | Cracked Plaster | White plaster with hairline cracks | 2.5 | [PaintedPlaster017](https://ambientcg.com/a/PaintedPlaster017) |
| `plaster/smooth-stucco` | Smooth Stucco | Fine smooth render — Mediterranean and Baroque walls | 2.5 | [Plaster001](https://ambientcg.com/a/Plaster001) |

## 나무 (Wood) — 11

| id | 이름 | 설명 | 크기 (m) | 원본 |
|---|---|---|---|---|
| `wood/vertical-boards-weathered` | Weathered Vertical Boards | Sun-greyed vertical boards — Nordic houses, barns, upper floors | 1.6 | [Planks001](https://ambientcg.com/a/Planks001) |
| `wood/vertical-boards-light` | Light Vertical Boards | Pale vertical boards with nail heads | 1.6 | [Planks002](https://ambientcg.com/a/Planks002) |
| `wood/vertical-boards-warm` | Warm Vertical Boards | Warm brown vertical boards — doors, shutters, gables | 1.4 | [Planks004](https://ambientcg.com/a/Planks004) |
| `wood/dark-boards` | Dark Weathered Boards | Dark weathered horizontal boards — decks, barns, docks | 1.6 | [Planks023A](https://ambientcg.com/a/Planks023A) |
| `wood/grey-boards` | Grey Weathered Boards | Grey bleached horizontal boards | 1.6 | [Planks027B](https://ambientcg.com/a/Planks027B) |
| `wood/weathered-beam` | Weathered Oak Beam | Dark weathered oak with open grain — half-timber frames, posts, brackets | 1.2 | [Wood062](https://ambientcg.com/a/Wood062) |
| `wood/rough-beam-grey` | Rough Grey Beam | Rough grey timber with knots | 1.2 | [Wood073](https://ambientcg.com/a/Wood073) |
| `wood/grey-old-wood` | Grey Old Wood | Silver-grey long-weathered wood | 1.2 | [Wood011](https://ambientcg.com/a/Wood011) |
| `wood/painted-blue-worn` | Worn Blue Paint | Blue paint worn through to wood — shutters, doors | 1.2 | [PaintedWood008B](https://ambientcg.com/a/PaintedWood008B) |
| `wood/painted-green-worn` | Worn Green Paint | Green paint worn through to wood | 1.2 | [PaintedWood009B](https://ambientcg.com/a/PaintedWood009B) |
| `wood/painted-cream-worn` | Worn Cream Paint | Cream paint, chipped — window frames | 1.2 | [PaintedWood006C](https://ambientcg.com/a/PaintedWood006C) |

## 금속 (Metal) — 5

| id | 이름 | 설명 | 크기 (m) | 원본 |
|---|---|---|---|---|
| `metal/black-iron` | Black Forged Iron | Dark hand-forged iron — hinges, straps, lanterns, sign brackets | 1.0 | [Metal046A](https://ambientcg.com/a/Metal046A) |
| `metal/rusty-iron` | Rusty Iron | Iron with heavy orange rust | 1.0 | [Metal053C](https://ambientcg.com/a/Metal053C) |
| `metal/copper-verdigris` | Copper Verdigris | Weathered copper turned green — domes, spires, roof sheets | 1.5 | [Metal058C](https://ambientcg.com/a/Metal058C) |
| `metal/copper-dark` | Dark Copper | Darkened aged copper | 1.5 | [Metal047B](https://ambientcg.com/a/Metal047B) |
| `metal/lead-sheet` | Lead Sheet | Matte grey lead — flat roofs, flashing, spires | 1.5 | [Metal038](https://ambientcg.com/a/Metal038) |

## 땅 · 길 (Ground — 지형 · 거리 · 포석) — 29

| id | 이름 | 설명 | 크기 (m) | 원본 |
|---|---|---|---|---|
| `ground/wild-grass` | Wild Grass | Uneven grass with dry blades — turf roofs, lawns | 2.0 | [Grass004](https://ambientcg.com/a/Grass004) |
| `ground/lush-grass` | Lush Grass | Dense green grass | 2.0 | [Grass001](https://ambientcg.com/a/Grass001) |
| `ground/forest-floor` | Damp Forest Floor | Damp forest earth with moss and sparse grass — woodland terrain | 2.1 | [Ground037](https://ambientcg.com/a/Ground037) |
| `ground/forest-leaves` | Leaf Litter | Brown dirt covered with fallen leaves — under trees | 2.0 | [Ground041](https://ambientcg.com/a/Ground041) |
| `ground/autumn-forest` | Autumn Forest Floor | Autumn leaves and roots on forest soil | 2.6 | [Ground070](https://ambientcg.com/a/Ground070) |
| `ground/soil` | Brown Soil | Plain brown soil — fields, gardens, dirt | 1.4 | [Ground048](https://ambientcg.com/a/Ground048) |
| `ground/mud` | Dark Mud | Dark wet mud — riverbanks, swamps, puddles | 2.0 | [Ground051](https://ambientcg.com/a/Ground051) |
| `ground/dirt-path` | Dirt Path | Trodden dirt path with pebbles and grass edges — roads, trails | 2.0 | [Ground030](https://ambientcg.com/a/Ground030) |
| `ground/rocky-dirt` | Rocky Dirt | Brown dirt full of sharp stones — mountain slopes, scree | 2.0 | [Ground063](https://ambientcg.com/a/Ground063) |
| `ground/mossy-forest` | Mossy Forest Ground | Moss, mud and twigs — dense forest floor | 2.0 | [Ground024](https://ambientcg.com/a/Ground024) |
| `ground/grass-dirt` | Patchy Grass | Short grass with bare dirt patches — meadows, village greens | 2.0 | [Ground003](https://ambientcg.com/a/Ground003) |
| `ground/meadow-grass` | Meadow Grass | Green meadow grass seen from above | 1.4 | [Grass002](https://ambientcg.com/a/Grass002) |
| `ground/short-lawn` | Short Lawn | Short clean lawn grass — gardens, courtyards | 2.0 | [Grass005](https://ambientcg.com/a/Grass005) |
| `ground/dry-cracked` | Dry Cracked Earth | Sun-baked cracked earth — deserts, dry plains | 2.0 | [Ground031](https://ambientcg.com/a/Ground031) |
| `ground/sand` | Beach Sand | Smooth fine sand — beaches, dunes, shores | 2.0 | [Ground057](https://ambientcg.com/a/Ground057) |
| `ground/wet-sand` | Wet Sand | Wet packed sand at the water line | 1.8 | [Ground059](https://ambientcg.com/a/Ground059) |
| `ground/river-pebbles` | River Pebbles | Rounded river pebbles — riverbeds, shores, canal edges | 1.4 | [Gravel019](https://ambientcg.com/a/Gravel019) |
| `ground/gravel` | Grey Gravel | Grey gravel with dirt — paths, yards, roads | 1.5 | [Gravel022](https://ambientcg.com/a/Gravel022) |
| `ground/forest-gravel` | Forest Gravel | Brown gravel with sand and twigs — forest trails | 2.4 | [Gravel039](https://ambientcg.com/a/Gravel039) |
| `ground/snow` | Fresh Snow | Fresh fluffy snow — peaks, winter | 2.0 | [Snow005](https://ambientcg.com/a/Snow005) |
| `ground/snow-trampled` | Trampled Snow | Snow trodden by many feet — winter roads, villages | 2.0 | [Snow009C](https://ambientcg.com/a/Snow009C) |
| `ground/dirty-snow` | Dirty Snow | Melting snow mixed with dirt | 2.0 | [Snow011](https://ambientcg.com/a/Snow011) |
| `ground/cobblestone` | Cobblestone | Grey medieval cobblestones — town streets, bridges, squares | 1.6 | [PavingStones046](https://ambientcg.com/a/PavingStones046) |
| `ground/cobblestone-old` | Old Paving Stones | Worn old grey paving stones — old town squares | 2.0 | [PavingStones042](https://ambientcg.com/a/PavingStones042) |
| `ground/roman-road` | Roman Road Stones | Large worn road stones — Roman and medieval highways | 1.9 | [PavingStones054](https://ambientcg.com/a/PavingStones054) |
| `ground/cobblestone-arches` | Fan Cobblestones | Small cobbles laid in fan arches — European old towns | 1.6 | [PavingStones048](https://ambientcg.com/a/PavingStones048) |
| `ground/pebble-road` | Pebble Road | Rough road of set pebbles and rocks | 3.0 | [PavingStones055](https://ambientcg.com/a/PavingStones055) |
| `ground/stone-floor` | Rough Stone Floor | Uneven medieval stone floor — courtyards, quays, castle floors | 1.9 | [Rocks025](https://ambientcg.com/a/Rocks025) |
| `ground/rubble-floor` | Rubble Floor | Set rubble stones — old yards and lanes | 1.1 | [Ground065](https://ambientcg.com/a/Ground065) |

## 바위 · 절벽 (Rock) — 11

| id | 이름 | 설명 | 크기 (m) | 원본 |
|---|---|---|---|---|
| `rock/cliff-grey` | Grey Cliff | Grey cliff rock with cracks — mountains, steep slopes, boulders | 3.0 | [Rock030](https://ambientcg.com/a/Rock030) |
| `rock/cliff-mossy` | Mossy Cliff | Cliff rock with moss in the cracks — forests, wet climates | 3.0 | [Rock036](https://ambientcg.com/a/Rock036) |
| `rock/cliff-dark` | Dark Mossy Cliff | Dark grey rock with moss — northern coasts, gorges | 3.0 | [Rock012](https://ambientcg.com/a/Rock012) |
| `rock/granite-rough` | Rough Granite | Rough grey granite — boulders, highlands | 2.5 | [Rock028](https://ambientcg.com/a/Rock028) |
| `rock/cliff-light` | Light Limestone Cliff | Pale limestone cliff face | 3.0 | [Rock017](https://ambientcg.com/a/Rock017) |
| `rock/sandstone-cliff` | Sandstone Cliff | Beige layered sandstone — canyons, dry mountains | 3.0 | [Rock052](https://ambientcg.com/a/Rock052) |
| `rock/red-desert` | Red Desert Rock | Red-orange desert cliff — canyons, mesas | 3.0 | [Rock029](https://ambientcg.com/a/Rock029) |
| `rock/mountain-layered` | Layered Mountain Rock | Layered mountain rock with strata — alpine cliffs | 3.0 | [Rock006](https://ambientcg.com/a/Rock006) |
| `rock/boulder-wall` | Boulder Face | Packed boulders — rock falls, rough walls | 2.0 | [Rocks001](https://ambientcg.com/a/Rocks001) |
| `rock/mossy-stone` | Mossy Stone | Grey stone with moss patches — small rocks, boulders | 0.9 | [Rock046S](https://ambientcg.com/a/Rock046S) |
| `rock/dark-rough` | Dark Rough Rock | Dark rough rock — volcanic, basalt, caves | 2.0 | [Rock047](https://ambientcg.com/a/Rock047) |

## 나무껍질 (Bark) — 8

| id | 이름 | 설명 | 크기 (m) | 원본 |
|---|---|---|---|---|
| `bark/oak` | Oak Bark | Deep-furrowed brown oak bark — oaks, elms, broadleaf trees | 1.0 | [Bark012](https://ambientcg.com/a/Bark012) |
| `bark/fir` | Fir Bark | Scaly brown conifer bark — firs, spruces, pines | 1.2 | [Bark014](https://ambientcg.com/a/Bark014) |
| `bark/smooth-light` | Smooth Light Bark | Smooth pale bark — beech, young trees, birch-like | 1.0 | [Bark005](https://ambientcg.com/a/Bark005) |
| `bark/mossy` | Mossy Bark | Bark with green moss — old wet-forest trees | 1.0 | [Bark013](https://ambientcg.com/a/Bark013) |
| `bark/dark` | Dark Bark | Dark brown rough bark — old trees, walnut, alder | 1.0 | [Bark006](https://ambientcg.com/a/Bark006) |
| `bark/rough-brown` | Rough Brown Bark | Rough light-brown bark — poplars, willows | 1.0 | [Bark007](https://ambientcg.com/a/Bark007) |
| `bark/smooth-dark` | Smooth Dark Bark | Smooth dark bark — cherry, young hardwoods | 1.0 | [Bark002](https://ambientcg.com/a/Bark002) |
| `bark/lichen-grey` | Lichen Grey Bark | Grey-green bark with lichen — old, northern trees | 1.0 | [Bark011](https://ambientcg.com/a/Bark011) |

## 자연 (Nature — 이끼 · 잎) — 4

| id | 이름 | 설명 | 크기 (m) | 원본 |
|---|---|---|---|---|
| `nature/moss` | Moss | Dense green moss | 1.0 | [Moss002](https://ambientcg.com/a/Moss002) |
| `nature/leaf-broad` | Broad Leaf | Single green broad leaf with opacity — leaf cards | 0.2 | [Leaf001](https://ambientcg.com/a/Leaf001) |
| `nature/leaf-oval` | Oval Leaf | Single green oval leaf with opacity — leaf cards | 0.2 | [Leaf002](https://ambientcg.com/a/Leaf002) |
| `nature/leaf-small` | Small Leaf | Single small green leaf with opacity — leaf cards | 0.2 | [Leaf003](https://ambientcg.com/a/Leaf003) |
