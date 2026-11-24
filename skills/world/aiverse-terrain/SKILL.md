---
name: aiverse-terrain
description: Change AIVERSE terrain of a map - cliff levels, heightfield simulation, sub-cell detail surface, materials and splat, heightmap PNG import/export, planet relief - through MCP and the «Рельеф» window, keeping old games' checksums.
packages: terrain-pbr
---

# Terrain: levels, heightfield, detail, materials

Use when a task sculpts or paints relief, switches a game to `model: "heightfield"`, adds a detail surface or
imports a heightmap. Map generation from presets is `skills/world/aiverse-map-generation`; whole-map building is
`skills/world/aiverse-map-build`; spline water is `skills/world/aiverse-water`.

Authority: `docs/terrain.md` (contract and branches), `docs/game-format.md` (keys), `docs/terrain-textures.md`.

1. Decide the layer. Simulation layers (`rows`, `levels`, `corners` with `rules.terrain.model: "heightfield"`)
   change pathing, vision and checksums; visual keys (`terrain` block, `detail`, splat, materials) do not.
2. Simulation relief: `terrain_level`, `terrain_height`, `terrain_heights`, `map_heightfield`; heightfield maps
   get slopes from corners (`MapGrid::surfaceHeight`), so check `move.maxSlope` of ground types afterwards.
3. Visual relief: `terrain_detail {action: create, scale}`, then `terrain_sculpt` (raise / lower / smooth /
   flatten / noise / set / erode / stamp) and `terrain_paint_material`; per-cell materials with
   `terrain_material` / `terrain_paint`. Code: `apps/editor/core/TerrainDetailOps.cpp`,
   `engine/view/game/GameTerrainSurface.h`.
4. Heightmaps: `heightmap_export` / `heightmap_import` (16-bit PNG; `detail: true` for the detail grid).
5. Planets: the same tools with `body` and `face` (`apps/editor/core/PlanetDetailOps.cpp`,
   `engine/view/game/GamePlanetDetail.cpp`); planet bodies themselves - `skills/world/aiverse-planets`.
6. Check: `map_validate` (slopes, visual surface vs simulated ground on walkable cells), `view_screenshot`,
   a bot match (`match_run`); 2D tile games must look unchanged (`~/.aiverse/packages/templates/tiles2d`, `~/.aiverse/packages/templates/km2d`).

Invariants: the view never writes simulation layers; the terrain mesh is not rebuilt during play (editor edits
rebuild dirty chunks only); old maps without a `terrain` block keep pixels and checksums.

Limits: one surface height per cell column (no overhangs or caves; block worlds are the voxel packages,
`skills/world/aiverse-voxel-world`); runtime terrain deformation beyond digging/craters is not supported.

Workflow recipe over existing engine code and MCP tools; no runtime library.
