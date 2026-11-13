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

## Organic layout (`"layout": "organic"`, preset `metropolis`)

For big cities (128..1024 cells per side; the preset is 384 x 384 = 1.5 km at 4 m per cell):

- **Centre**: the street grid above, in a square in the middle (`downtown`: share of the smaller side, default 0.38).
- **Avenues**: from the ends of the centre's arterials to the map edges, wandering around their direction; an
  avenue that runs into another arterial ends there (`radials`: chance per arterial end).
- **Ring roads**: wobbly circles around the centre at 1.72 and 2.75 of its half size, when they fit.
- **River**: from edge to edge past the centre; avenues and rings cross it on bridges, suburban streets stop at it.
- **Suburbs** (`suburbs`: reach as a share of the half size): curved two-cell streets grow from the avenues and
  rings, seed side streets, join the first road they come near or end in a turning circle. Houses (the smaller
  residential buildings) stand along them with a yard and sometimes a car by the kerb; shops along the avenues,
  works in the eastern sector. Roads outside the centre have grass verges instead of pavements.
- **Countryside**: trees in clumps, `treeBudget` in total.
- **Relief** (`relief`: height of the hills in units, 0 = flat): `terrain.corners`, flat in the centre, rising
  outside, the river bed below its banks, roads graded and the ground under every building flat. The simulation
  uses it only with rules `terrain.model: "heightfield"`; otherwise it is visual.
- **Ground materials**: with terrain layers named `asphalt` / `road`, `pavement` / `concrete`, `grass`, `lot` /
  `gravel` the map gets `terrainMaterial`.

Curved roads are in `city.paths`: `{"id", "class", "kind": "avenue" | "ring" | "street", "width", "points": [[x, z], ...]}`
(centre line in cells), `city.downtown` is the grid square, bridges on paths are `{"road", "from": [x, z], "to": [x, z]}`.
Junctions of curved roads are in `city.junctions` next to the grid's rectangles, as a point with a radius:
`{"x", "z", "radius", "roads": [a, b], "type"}` — `t` (a street leaves a road or joins one; an avenue runs into
another), `cross` (a ring road crosses an avenue), `join` (an avenue continues an arterial of the centre), `end`
(a turning circle; one road id). Every path has at least one.
Exact road tiles (`tileBases`) apply to the grid; curved roads get the plain `street` tile.

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

T-junctions of the grid (a street always ends at the road that bounds its block) use extra groups when they are given:
`nsOpenE`, `nsOpenW`, `ewOpenN`, `ewOpenS` (4 x 4: an arterial whose edge line opens on that side over the two-cell
mouth, with one cell before and after it for the corner curves), `ns2OpenE`, `ns2OpenW` (2 wide x 4 long), `ew2OpenN`,
`ew2OpenS` (4 wide x 2) for a street that bounds another street, and `ns2EndN`, `ns2EndS`, `ew2EndW`, `ew2EndE` (2 x 2:
the last two cells of the street at that end, with the pedestrian crossing and the stop line). Rows run along z,
columns along x. Arterial crossings (`x1`..`x15`) are whole 4 x 4 drawings, so crossings and stop lines on every arm
are the tileset's business.

A base may be a list of tile indices instead of one: looks of the same piece (worn paint, cracked, patched). The
planner picks one per segment of a road (per four cells of an arterial, per two cells of a street or of plain
ground), by a hash of the place and the seed.

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
a second driving level in the simulation, which does not exist yet. Buildings are not rotated; a building's footprint is
its bounding rectangle. Curved roads have no lane markings and their junctions are not listed in `city.junctions` yet.
Public transport, trams, metro and railways are not planned yet.

Car parks reserve a two-cell central aisle and two-cell curb cuts on road-facing
north/south edges before placing street furniture. Parking rows face the aisle and
leave turning space at the ends. `city.parkingAccess` lists the curb-cut rectangles
(`x`, `z`, `w`, `h`). Arterial curb bays are spaced two cells apart, with two cells
of uninterrupted pavement around them to exclude junctions and driveways.
`city.curbParking` lists each bay's cell (`x`, `z`), `axis` and `road` index, including
empty bays; consumers can use this metadata for parking markings.
