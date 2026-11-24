# NEREID open sea over large worlds: patch coverage and LOD rings (2026-11-22)

Applies to `shape: "infinite"` NEREID surfaces (`kind: "ocean"`). Presentation only:
no ECS, save, checksum or sampling change.

## What was wrong (found by analysis, now covered by a test)

The infinite ocean is one square patch, re-uploaded when the camera crosses a
`oceanPatchRadius` (r) cell. The patch was centred on the cell *anchor*
`floor(camera/r)*r`, so it covered `[anchor-r, anchor+r]` while the camera can be
anywhere in `[anchor, anchor+r)`. Just before the next cell switch the far edge
of the sea was **0 m** in front of the camera (default r = 256: the sea ended at
the camera, then jumped 256 m on the switch). The patch is now centred on its
cell (`anchor + r/2`): at least r/2 of water in every direction, same cell key,
same upload frequency. `ecs_nereid_ocean_lod_test` asserts the r/2 margin at
eight camera positions including 0.01 m before a switch.

The default r = 256 m also means a several-km map sees open sea only ~128-384 m
around the camera; from altitude it disappears entirely (measured below).

## LOD rings (`"oceanLod": "rings"`)

```json
{"id": 1, "kind": "ocean", "shape": "infinite", "surfaceY": 0, "bottomY": -200,
 "oceanLod": "rings", "oceanPatchRadius": 6000, "oceanInnerCell": 4, "oceanInnerCells": 16,
 "lod": "medium", "rippleTileGrid": 4}
```

- An inner square of `2*oceanInnerCells` cells of `oceanInnerCell` m (0 = by `lod`:
  near 2, medium 4, far 8) is surrounded by square rings whose cell size doubles
  until the half extent reaches `oceanPatchRadius` (at most 16 levels).
- Ring seams are stitched: a coarse cell touching the finer level gets the finer
  midpoint and is fanned into three triangles; all vertices sit on one integer
  lattice and are shared, so there are no cracks or T-junctions (the test checks
  that every interior edge is shared by exactly two up-facing triangles and that
  the area equals the covered square).
- The centre snaps to `oceanInnerCell*oceanInnerCells/2` (32 m by default), so the
  camera is always within 16 m of the centre of the 128 m fine square, and a
  rebuild is one async CPU job + one upload per 32 m of travel. Vertices are
  relative to the snapped centre in double precision (exact 1e8 m from origin).
- `rippleTileGrid > 1` splits the inner square into N x N draw ranges (ripple and
  wind packets stay local to the camera) plus one range per ring; `1` = one draw.
- The flow field of the sea covers the ring extent (bounded 256 x 256 texels).
- Default stays `"patch"`; nothing changes for existing scenes except the
  coverage fix above. Invalid ring settings or more than `maxVertices` fail closed.

Cost: `2*(2k)^2 + (L-1)*(6k^2+4k)` triangles for k = `oceanInnerCells`, L levels:
k = 16, 6 km -> 13 248 triangles / 6 689 vertices, independent of world size.
k = 8 halves the fine square and gives 3 840 triangles for the same extent.

## Measured (Metal, renderbench, `tests/render/verify_nereid_ocean_lod.py`)

Fixture: open sea at y = 0, cameras ~3.9 km from the origin, far plane 12 km,
960x540, profile `low`, frozen time, 30 samples x 3 runs.
Output kept on KINGSTON `AIVERSE-builds/nereid-next/ocean-lod-metal{,2}`.

| camera | variant | water tris | water draws | ocean in 5.5 km region | to horizon |
|---|---|---|---|---|---|
| low (6 m), 0.5 m before a 256 m cell switch | patch 256 (default) | 8 | 1 | 0.92 | 0.93 |
| | patch 6000, 8x8 tiles, near | 2 048 | 64 | 1.00 | 1.00 |
| | rings 6000 | 13 248 | 23 | 1.00 | 1.00 |
| high (400 m) | patch 256 | 8 | 1 | **0.00** | 0.00 |
| | patch 6000 tiled | 2 048 | 64 | 1.00 | 0.94 |
| | rings 6000 | 13 248 | 23 | 1.00 | 0.96 |
| grazing (30 m, 5 km) | patch 256 | 8 | 1 | 0.74 | 0.74 |
| | patch 6000 tiled | 2 048 | 64 | 1.00 | 1.00 |
| | rings 6000 | 13 248 | 23 | 1.00 | 1.00 |

"Coverage" = share of sampled pixels below the projected 5.5 km point (or the
true horizon) that differ from a no-water render. Any finite sea leaves a thin
sliver below the true horizon from altitude (0.94-0.96 at 400 m).

Ring rebuild popping: moving 0.4 m across a 32 m snap boundary changed the image
by mean 0.388/255, moving 0.4 m inside one cell by 0.379/255 - no visible pop.

Frame time: whole-frame CPU median 0.17-0.23 ms for every water variant. In the
first (quiet) run the GPU median was 0.05-0.19 ms with water vs 0.03-0.09 ms
without, for all three variants alike; the second run shared the GPU with other
work (2-6 ms spikes even without water). No variant is measurably faster on the
GPU here; the wins are coverage, fewer draws than an equally large tiled patch
(23 vs 64) and camera-local ripple tiles. Not measured: GL/Vulkan/mobile pixels,
editor scenes with terrain, underwater views of a ring sea.

## Files

`engine/world/nereid/NereidWaterSurfaceMesh.h` (ring mesh and policy),
`engine/render/NereidSurfaceResident.h` (keys, cell-centred patch),
`engine/world/NereidWaterScene.h` (basis, flow bounds, `drawStats()`),
`engine/world/NereidWaterManifest.h` (`oceanLod`, `oceanInnerCell(s)`),
`apps/renderbench/Bench.h` (`nereidWater*` per-frame counters), editor `geometry_stats`
(`nereidWaterDraws/Triangles`), tests `ecs_nereid_ocean_lod_test`,
`tests/fixtures/nereid/ocean-large-world.json`, `tests/render/verify_nereid_ocean_lod.py`.
