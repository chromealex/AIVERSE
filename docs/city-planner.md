# City planner (map generator preset `city`)

`modules/rts/gen/GenCity.cpp` lays out a city for top-down games: a street network with a hierarchy, a river
with bridges, roundabouts, zoned blocks with buildings, parks, car parks, street trees, lamps and furniture,
parked cars, people and the hero. It is one of the map generator presets (docs/game-format.md "Map generation"),
so it runs at match start (`"generate": {"preset": "city", "seed": "match"}`), in `aiverse_headless --generate city
--gen-out map.json` (bake to a static map), in the editor and over MCP (`map_generate`). Integer maths only: the same
request gives the same map on every platform. Tests: `aiverse_tests city_plan`, `city_streets`, `city_tiles`.

## What it builds

1. **Arterials**: four cells wide, a ring along the map edges and a jittered grid inside (`arterialSpacing`).
2. **River** (`river`: chance or true / false, `riverWidth`): a meandering band inside one column of superblocks.
   Every road that crosses it becomes a **bridge** (passable ground over the water).
3. **Local streets**: two cells wide; every superblock is split until blocks are at most `blockWidth` x `blockDepth`.
   Streets end at the roads around them, which gives T-junctions; no street runs along the river bed.
4. **Roundabouts** (`roundabouts`: chance per inner arterial crossing): a 2 x 2 island in the junction.
5. **Pavements**: one cell around every block and along the water.
6. **Blocks**: `parks` and `parking` chances (riverside blocks are parks three times as often); the rest are zoned
   by distance from the centre: commercial, residential, industrial (also by the river). Buildings are packed in rows
   on free lot cells, by footprint, preferring the block's zone.
7. **Street life**: lamps by the kerb every few cells (facing the road), trees, furniture, people on pavements
   (`trees`, `props`, `people`), parked cars in the kerb lanes of arterials facing the way traffic goes (right-hand)
   and in car parks (`cars`). The hero starts on the pavement cell nearest to the centre.

## What it takes from the rules

| Thing | How it is found |
|---|---|
| buildings | structures with the tag `building`; zone tags `residential`, `commercial`, `industrial` (none = anywhere) |
| trees, bushes | tags `tree`, `bush` |
| lamps, furniture | tags `lamp`, `prop` |
| people | tags `pedestrian` or `ambient_pedestrian` (owner: player 2 when there is one, else neutral) |
| vehicles | keys of `driving.vehicles`, plus the tag `vehicle` |
| hero | first of `topDown.heroes`, else params `hero` |

Missing kinds are skipped and listed in `generated.warnings`.

## Tiles

With params `"tileset": "tilesets/city.json"` the map gets a `tiles` block: one terrain character per kind
(`chars`: `road` a, `street` s, `pavement` p, `lot` l, `park` g, `water` w, `parking` k). `tileBases` gives the first
tile index of exact artwork and fills `tiles.fixed`: `ns`, `ew` (4 x 4 tiles of a four-cell road), `x1`..`x15`
(4 x 4 tiles of an arterial crossing; bits: 1 north arm, 2 east, 4 south, 8 west), `ns2`, `ew2` (2 x 2, local
streets), `walk`, `lot`, `grass`, `water`, `street`, `parking` (2 x 2 each).

## The plan in the map: `"city"`

```json
"city": {
  "cellMeters": 4, "traffic": "right",
  "roads": [{"id": 0, "class": "arterial", "axis": "ns", "pos": 0, "from": 0, "to": 72, "width": 4, "lanes": 4}],
  "junctions": [{"x": 24, "z": 22, "w": 4, "h": 4, "roads": [1, 5], "type": "cross"}],
  "bridges": [{"road": 5, "from": 49, "to": 55}],
  "blocks": [{"x": 4, "z": 4, "w": 8, "h": 9, "use": "buildings", "zone": "industrial"}],
  "river": {"from": 49, "to": 59}
}
```

A road is a rectangle of cells: `axis` `ns` covers x in [pos, pos + width), z in [from, to); `ew` the other way
round. Junction `type`: `cross`, `t`, `corner`, `roundabout`. Traffic, traffic lights and pedestrian routing can be
built on this block.

## Limits

One level only: bridges cross water, junctions are at grade. Overpasses, multi-level interchanges and tunnels need
a second driving level in the simulation, which does not exist yet. Roads are axis-aligned (no diagonals or curves);
buildings are not rotated; a building's footprint is its bounding rectangle.
