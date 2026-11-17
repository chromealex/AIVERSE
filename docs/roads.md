# Spline roads

Roads drawn as splines, as in city builders: a network of nodes and cubic Bezier edges on the map, road types with
lanes, curbs, sidewalks, barriers and props, junctions built from whatever meets at a node, bridges and tunnels from
the heights of the spline points, branches that leave chosen lanes of another road, bus stop bays.

| Part | Where |
|---|---|
| Data, built-in types, shared geometry (Fixed math) | `modules/rts/RtsRoadNet.{h,cpp}`, `modules/rts/RtsRoadTypes.cpp` |
| Look: meshes, paint decals, props, signals | `modules/rts/view/RtsRoadView.{h,cpp}`, `modules/rts/view/RtsRoadKit.{h,cpp}` |
| Editor window «Дороги» (Мир) | `apps/editor/windows/RoadsWindow.cpp` |
| Edit helpers + MCP | `apps/editor/core/RoadOps.{h,cpp}` |

Water (straits, rivers, the sea with embankments, cliffs and beaches) is drawn the same way: `docs/water.md`.

## Почему участки одной дороги не обязательно один Draw Call

В **Сцена → Batching → Road surfaces: materials / chunks** показаны
число исходных и загруженных road-chunk моделей, степень группировки,
количество примитивов/треугольников, а также имена материалов,
идентификаторы GPU-текстур и количество чанков для каждого материала.
Это *данные загруженной геометрии*, а не замер числа GPU API submissions.

Генератор обычно использует **разные** material slots: текстуру
проезжей части с линиями/следами (`paintedMaterial`), обычный
асфальт перекрёстков, бордюры, тротуары и разные полосы.
Разметка, зебры, мокрая плёнка и лужи отдельно отправляются через
custom decal-проход. Даже одинаковый материал не делает разные
меши одним прямым GPU Draw Call: обычный instancing требует того же
меша; MDI требует общего GPU состояния и совместимых геометрических
буферов и экономически выгодной группы команд.

Группировка road chunks `AIVERSE_RTS_ROAD_CHUNK_MERGE` уменьшает
число геометрических чанков, **не количество различных материалов**.
Маленькие карты автоматически используют ограниченное по геометрии
объединение x2, большие сохраняют culling. Для A/B можно использовать
`AIVERSE_RTS_ROAD_CHUNK_MERGE=off`. Не объединять разные материалы
или трассировки с мокрой дорогой только ради сокращения счётчика:
это меняет изображение и может увеличить GPU time.

## Road types (rules `roadTypes`)

Values are metres (km/h for `speed`); `cellMeters` (default 4) turns them into map cells. The built-in library is
always there unless `"builtin": false`; a game adds types or retunes a built-in one by its id.

```json
"roadTypes": {
  "cellMeters": 4,
  "my_avenue": {
    "extends": "avenue_4", "name": "Мой проспект", "category": "city",
    "lanes": {"forward": ["car", "bus"], "backward": 2, "width": 3.5},
    "median": {"width": 2, "surface": "grass", "barrier": "none", "curb": true},
    "sides": {"curb": "low", "sidewalk": 3, "sidewalkSurface": "paving"},
    "right": {"barrier": "fence"},
    "surface": "asphalt", "centre": "double", "laneLines": true, "crossings": true, "speed": 60,
    "props": [{"shape": "lamp", "side": 0, "spacing": 25, "offset": 0.4},
              {"shape": "tree", "side": 2, "spacing": 10},
              {"model": "models/bench.glb", "side": 1, "spacing": 40, "offset": 1.5},
              {"type": "street_lamp", "side": -1, "spacing": 30}]
  }
}
```

- `lanes.forward` / `backward`: a count or a list centre outwards (`car`, `bus`, `parking`, `bike`, `tram`, or
  `{"use", "width"}`); a parking lane is the outermost one.
- Per side (`sides` for both, `left` / `right` seen along a -> b): `curb` none | low | high | sloped | shoulder,
  `curbHeight`, `shoulder` + `shoulderSurface`, `sidewalk` + `sidewalkSurface`, `barrier` none | guardrail |
  concrete | fence | wall | hedge, `barrierHeight`.
- `median`: `width`, `surface`, `barrier`, `curb` (raised). A zebra crossing cuts through a raised / green median at
  road level, paved.
- Surfaces: asphalt, concrete, cobble, paving, gravel, dirt, grass. Centre line: none, dashed, solid, double, yellow.
- Props: `shape` (lamp, tree, bush, flowerbed, bollard, pole, bench, sign: drawn by the view), `model` (a model of the
  game, drawn by the view) or `type` (a unit type the simulation places); `side` -1 / 1 / 0 both / 2 median,
  `spacing`, `phase`, `offset` (from the inner side of the sidewalk / shoulder outwards), `scale`, `onBridges`.
  A tree on a paved sidewalk stands in a tree pit (soil in a stone edge).

Built-in types: city — `street_2`, `street_2_parking`, `street_2_trees`, `avenue_4`, `avenue_6`, `avenue_bus`,
`boulevard`, `one_way_1..3`, `alley`, `cobble_street`, `pedestrian`; highway — `highway_4`, `highway_6`,
`highway_ramp`, `highway_ramp_2`; rural — `rural_2`, `rural_2_guard`, `rural_4`, `country_lane`, `gravel_road`,
`dirt_road`, `farm_track`; paths — `footpath`, `park_path`, `trail`. MCP `road_types` lists them with widths.

## The network (map `roadNetwork`)

```json
"roadNetwork": {
  "nodes": [{"id": 1, "x": 20, "z": 24}, {"id": 2, "x": 44, "z": 24, "y": 0}],
  "edges": [{"id": 1, "a": 1, "b": 2, "type": "avenue_4", "c1": [28, 24, 2.6], "c2": [36, 24, 2.6],
             "attachA": {"edge": 7, "dir": 1, "lanes": [1, 1]}, "reversed": false,
             "stops": [{"at": 0.5, "side": 1, "length": 18, "taper": 12}]}]
}
```

- Coordinates in cells; `y` and the third value of `c1` / `c2` are metres over the ground. An edge without `c1` /
  `c2` is straight.
- Heights: the road height runs through the heights of the four control points as a clamped cubic spline (smooth
  slope and curvature, level where it meets the ground), over the straight line between the ground at the two ends
  (a river bed under the bridge does not pull it down). More than 1.5 m over the ground is a bridge (deck, pillars,
  railings), more than 2.5 m under it a tunnel (portals; the driving module gets the decks). An edge whose points
  are all at 0 drapes over the terrain. Slopes over 12% are reported.
- Junctions: three or more roads at a node make a junction: the roads are cut back, the surface gets rounded corners
  and sidewalk corners; with three or more driven arms it is signalised (signals over the incoming lanes, zebras,
  stop lines, lane arrows by the real exits); without signals the narrower roads get STOP and a stop sign.
- Branches (`attachA` / `attachB`): the end starts / ends on lanes `[first, last]` (centre outwards) of direction
  `dir` of the parent edge, which is split at that node. The branch's lanes that drive the parent's way are laid on
  those lanes; curbs, sidewalks, barriers and props of every road the branch crosses near there are cut along its
  carriageway. A one-way branch drawn against the traffic is turned round by the editor.
- Two roads meeting end to end (a node with two edges): the wider one narrows to the other over its last 20 m. An
  unpaved road (gravel, dirt) may only run on from a road of 1–2 lanes — the asphalt wears out over its last 15 m
  (broken asphalt) — or join any road at a junction; from a wider road it is refused.
- `stops`: bus stop bays — the kerb moves out by the kerb lane's width (or `width`) over `length` with `taper` at
  both ends; the sidewalk keeps at least 1.5 m; a shelter stands behind the bay, BUS is painted in it.
- `parking`: parking zones `{"at", "side", "length": 24, "taper": 5, "width": 2.5, "stall": 6}` (metres) — a bay
  cut into the sidewalk with the spaces marked across it; in the parking lane where the road type has one on that
  side; `"inside": true` marks the spaces inside the kerb lane (no bay).
- `driveways`: entrances into the yards `{"at", "side", "width": 3.5, "depth": 6}` — the kerb and the sidewalk come
  down over 1.5 m flares, an asphalt apron crosses the sidewalk and runs `depth` metres on into the plot; the edge
  line on that side and the solid centre line break there; no props stand in it.
- Network keys: `replaces` — tags or type id prefixes of the game's own baked road surfaces, not drawn while the
  network is shown; `signals: false` — the game puts up its own traffic lights. Other keys are kept as they are.

## Look

Static models per 32-cell chunk through `Renderer3D::drawModel` (PBR, shadows, lightmaps as static scenery), with
procedural tiling textures of the road kit. Lane lines are part of the carriageway texture: one texture per road type
(and per open side: a branch or a bay drops the edge line there) with the centre line, lane lines, edge lines, bus and
parking lines painted in; u runs across the carriageway, v along it (12 m per dash period). Local marks — zebras, stop
lines, lane arrows, STOP, BUS — are decals from the paint atlas `engine/assets/roads/markings.png`
(`tools/roads/make_markings.py`; a game overrides it with its own `roads/markings.png`), drawn with the sprite decal
pipelines (depth bias, lit, shadows). No shaders of its own.

### Wet roads and puddles

Rain is the game's: its weather calls `RtsView::roads().setWeather(wet, puddles)` every frame (both 0..1). `wet`
darkens every road surface — carriageways, junction boxes; wet dirt turns a darker brown, wet asphalt a cooler grey —
except in tunnels. `puddles` fills the puddle spots of the network: in the gutters along kerbs (sparser on roads
without kerbs), now and then in a lane's wheel track, in the ruts of dirt and gravel roads (sometimes one broad pool
over both ruts) and in the corners of junction boxes (a broad pool or two in a dirt box). None on bridge decks, in
tunnels or on driveways. Every spot has its own level it appears at and grows from there, so a shower fills them
one by one and drying shrinks them in the reverse order. The spots are hashed from the edge / node ids: the same
puddles in every run and on every client. Both layers are decals (the verge atlas' film and puddle slots); the meshes
are remade only when a level changes by 1/32. Puddles are drawn as water (`SpriteBatch::drawWater`, shader
`kSpriteWaterFS`): the atlas gives the pool's shape and its dark tint, on top the sky by the view angle, the sun's
glint, the lights' highlights and glitter, and on the High light tier the planar reflection of cars, buildings and
lamp glows (the water reflection of the map; a map without water bodies has none, the rest stays).

A map without weather can fix the look: `"roadNetwork": {"weather": {"wet": 1, "puddles": 0.6}, ...}` (a game's
`setWeather` overrides it). A game that draws its own reflective water reads the spots,
`RtsRoadView::puddleSpots()` (centre on the road surface, long axis, full length / width, the level it starts at,
dirt or not), and sizes them with `RtsRoadView::puddleScale(spot, puddles)`.

### Surface for wheels

The carriageway is drawn a little over the ground, kerbs and raised sidewalks higher still. `RtsRoadView::surfaceAt`
(and `RtsView::groundAt`, which falls back to the ground) returns the drawn top of the road surfaces: carriageways,
kerbs, sidewalks, junctions, medians, driveways, verges and low embankments, sampled 8 times per cell and interpolated,
so a kerb is a short ramp a wheel rolls up. Barriers, poles, signal heads, pillars and props are not part of it, nor
are decks higher than a bridge clearance (the simulation's driving decks carry those). Moving ground units are drawn
on it; a game that draws its own vehicles samples it under each wheel to tilt the body and drop single wheels.
Presentation only: the simulation does not read it.

### Worn asphalt

Asphalt and concrete roads wear: patches of newer asphalt across a lane (darker, the cut sealed with tar), now and
then a trench patch along a kerb or a strip right across the carriageway, cracks along the kerbs and the open edges
(not along a bay's edge), transverse cracks across the carriageway and webs of cracks in the lanes; junction boxes get
a web or two and sometimes a patch. Hashed from the edge / node ids: the same in every run. All decals under the paint
(lines are drawn over a patch) and under the wet film. The road type's `look.wear` sets how much: 0 none, 1 the
default, up to 3 (the spacing divides by it). Dirt and gravel roads have their ruts instead.

## Editor

Window «Дороги» (menu Мир): «Рисовать в «Сцене»», modes Прямая / Кривая (a control point) / Свободная (continues the
last road's direction) / Правка (drag nodes, Shift + drag: height; drag the curve handles; type, heights, reverse,
straighten, delete) / Остановка (a bay where the road is clicked; Shift: remove) / Удалить. PageUp / PageDown: the
height of new points. «От полос / к полосам»: start or end on the lanes under the cursor (Shift adds the next lane).
Shift or «Шаг 15°»: angle steps. Ends snap to nodes and to edges (the edge is split into a junction). The type
builder saves the game's own types into rules `roadTypes`. Problems of the network are listed at the bottom.

MCP: `road_types`, `road_type_set`, `road_network`, `road_add`, `road_node_set`, `road_edge_set`, `road_split`,
`road_stop_set`, `road_delete`.
