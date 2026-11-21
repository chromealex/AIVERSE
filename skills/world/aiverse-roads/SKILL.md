---
name: aiverse-roads
description: Lay out spline roads in an AIVERSE game: road types (rules roadTypes), the map roadNetwork (Bezier edges, heights for bridges and tunnels, lane branches, junctions, bus stops, parking bays), the editor tool «Дороги» and road_* MCP ops, keeping tiles in step, and checking the result with screenshots and tests.
---

# roads

Format and behaviour: `docs/roads.md`; traffic on the network: `docs/traffic.md` § Spline roads. Code: `engine/roads/RoadNet.*` (types, network, Fixed geometry; old path `modules/rts/RtsRoadNet.h` is a compatibility header), `engine/roads/RoadTypes.cpp` (built-in library), `engine/roads/RoadView.*` / `RoadKit.*` (meshes, painted carriageways, decals; drawn by every client), `apps/editor/core/RoadOps.*`, `apps/editor/windows/RoadsWindow.cpp`, `modules/rts/RtsTrafficRoads.*`.

## 1. Road types (rules `roadTypes`)
- Built-in library: street_2, street_2_parking, avenue_4/6, avenue_bus, boulevard, one_way_1..3, alley, cobble_street, pedestrian, highway_4/6 (+ ramps), rural_2/4, country_lane, gravel_road, dirt_road, farm_track, footpath, park_path, trail, rural_bridge.
- A game's own types sit directly in the object: `"roadTypes": {"cellMeters": 4, "<id>": {...}}` (not under `"types"`).
- Do **not** write `"extends": "_city"` (or any built-in) in game rules: the rules loader resolves `extends` before the road library is known (`extends unknown '_city'`). Spell the type out: `category` (city / rural / highway / path), `surface` (asphalt / concrete / gravel / dirt / paving), `speed`, `centre` (none / dashed / solid / double; US style: yellow), `laneLines`, `crossings`, `lanes {forward, backward, width}` (metres), `props` (`[]` when the game already places lamps / trees), `sides {curb: none|low|shoulder, sidewalk, sidewalkSurface, shoulder, shoulderSurface, barrier}`.
- No `laneLines` and `centre: none` = a quiet street: no arrows or stop lines at its junctions. A side with `curb: none` + `shoulder` (grass) + `sidewalk` (concrete) = residential (verge strip and walk at road level). No kerb and no sidewalk = soft side: a ragged verge decal.

## 2. The map's `roadNetwork`
```json
"roadNetwork": {
  "nodes": [{"id": 1, "x": 10, "z": 20}],
  "edges": [{"id": 1, "a": 1, "b": 2, "type": "street_2",
             "c1": [x, z, h], "c2": [x, z, h],
             "attachA": {"edge": 7, "dir": 1, "lane0": 0, "lane1": 1},
             "stops":   [{"at": 0.5, "side": 1}],
             "parking": [{"at": 0.5, "side": -1, "length": 24, "taper": 4, "width": 2.5, "stall": 6}]}],
  "replaces": ["road_surface", "old_road_prefix_"],
  "signals": false
}
```
- Map cells; `h` in metres over the ground (bridge above 1.5, tunnel below -2.5; the deck follows a smooth curve, pillars come by themselves). A node where every road meets; two roads bending more than 35° get a junction box.
- `driveways`: `[{"at", "side", "width": 3.5, "depth": 6}]` — an entrance into the yard: the kerb comes down, an apron of the roads' asphalt (grime along its edges, kerb stones on both sides) crosses the sidewalk; edge and solid centre lines break there, no props stand in it. Editor: mode «Заезд», op `road_driveway_set`. A generator places them where no tree, lamp or other object stands on the apron.
- `parking`: a bay cut into the sidewalk; `"inside": true` marks the spaces in the kerb lane; a type with a `Parking` lane gets them in that lane.
- `replaces`: tags or type id prefixes of a game's old baked road surfaces; the view does not draw them while the network is shown. `signals: false`: the game puts up its own traffic lights. Unknown keys survive editing in the editor.
- Rules the geometry enforces: a dirt / gravel road cannot continue a road of more than 2 lanes (only join at a junction, or follow a 1–2 lane road: asphalt → broken asphalt → dirt is drawn by itself); widths change by narrowing over the last stretch (kerbs come down when the next road has none). The editor turns the stretch of an unpaved road over water into an asphalt `rural_bridge`.

## 3. Laying roads
- Editor window «Дороги» (group World): Прямая / Кривая / Свободная / Правка / Остановка / Удалить / Парковка / Заезд; point height PageUp/PageDown; draw from single lanes; 15° snap; type builder.
- MCP: `road_types`, `road_type_set`, `road_network`, `road_add` (straight / smooth / bezier, fromLanes / toLanes), `road_node_set`, `road_edge_set`, `road_stop_set` (`parking: true` for a parking zone), `road_driveway_set`, `road_split`, `road_delete`.
- A whole city by script: generate `nodes` / `edges` (one node per junction and road end; merge ends within ~1 cell; split lines where they cross; fit Bezier through polylines), keep the game's own bridge / tunnel models (`decks`) and end roads at their mouths. A game's pipeline lives in the game's repository, not here.

## 4. Tiles in step with the network
Games that read roads from tiles (grid traffic, pedestrians, plot generators) need the tiles written by the same install: carriageway cells, sidewalk cells, old road cells the network no longer covers back to ground.

## 5. Checks
- `aiverse_headless --project <dir> --map maps/<map>.json` — road type errors show at once.
- Screenshots: `AIVERSE_CONTENT=<dir> AIVERSE_SCREENSHOT=/tmp/x.ppm AIVERSE_EXIT_AFTER_FRAMES=100 AIVERSE_CAM="x,z,dist,pitch,yaw" AiverseGame --map N --spectate` (Linux: under Xvfb). For a big map without its models: a copy with `size`, `players`, `terrain`, empty `entities`, the `roadNetwork`, and rules with the `roadTypes`.
- Look for: no gaps at joins, kerbs never on the carriageway, square plot corners (only the kerb is rounded), one tone of wheel tracks, verges right at the asphalt edge, lanes of branches matching their parent's.
- `aiverse_tests` (`traffic_drives_spline_roads`, `road_network_geometry_and_decks`): grid traffic and checksums of other maps must not change.
