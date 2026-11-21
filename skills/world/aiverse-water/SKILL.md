---
name: aiverse-water
description: Add or change AIVERSE spline water - straits, rivers, the open sea - with shore types (embankment, cliff, beach, natural), bridges from spline roads and the «Вода» window; the game supplies only models and textures.
---

## Mandatory TerraWeave + NEREID ownership

User instruction: construct game environments through TerraWeave + NEREID first.
Terrain/biomes/trees/grass/rocks belong to TerraWeave; all new rivers/lakes/oceans
belong to NEREID. Never substitute a custom height/noise generator or a static PBR
water plane and describe it as the engine feature. Read
`skills/workflow/aiverse-terra-first/SKILL.md` and
`skills/gameplay/aiverse-nereid-water/SKILL.md`. Existing legacy maps retain their
compatibility until an explicit tested migration. For missing host integration,
add the adapter to the shared subsystem; record a real capability gap before any
custom fallback. Do not fake waves, flow, foam or buoyancy in project scripts.
Sky stays with PRISMA, climate with PHYSIS, VFX with SpectraFX; compose owners.


# Spline water

Use for any water body drawn as a spline: a strait or river (centre line with width per point) or an open sea
(shore line edge to edge). Cell water painted with `~` rows stays in `skills/world/aiverse-terrain`.

Authority: `docs/water.md`; roads that become bridges: `docs/roads.md`, `skills/world/aiverse-roads`.

1. Shore types live in rules `shoreTypes` (embankment, cliff, beach, natural: models, textures, props).
   A game provides only models and textures; do not write per-game code.
2. Bodies live in the map `waterNetwork`. List with `water_types` / `water_network`; add or replace a body with
   `water_set {body}`, delete with `water_delete {id}`.
3. Shores are cut automatically; override one stretch with `water_shores {id, side, from, to, type}` or reset with
   `water_shores {id, reset: true}`.
4. `water_bake` (every change bakes too) returns water / cliff cells, shore metres per bank, `landParts` and a
   warning when land falls apart without a bridge - fix it with a spline-road bridge, not by painting cells.
5. Editor: window «Вода» (`apps/editor/windows/WaterWindow.cpp`). Tests: `tests/tests_water.inc`.
6. Check with `view_screenshot` from several angles and a bot / traffic run that crosses the bridges.

Pitfalls already hit: faces of a bank whose land lies on the other hand of the point order are flipped (check new
geometry on both banks); water cells are taken with 0.75 cell of slack, so a promenade is never narrower than 1.5 cells;
land surfaces follow the ground with a lift or the terrain grass pokes through the sand; when testing connectivity
stamp diagonal road samples 3 x 3 (they break 4-connectivity). A game gives only `shoreTypes` textures and models
(its own `rules/water.json` through `include`, written with `tools/rules/rules_io.py`).

Not yet (from the doc): wall / parapet modules repeated along the bank, waves, water bodies at different heights.

Workflow recipe over existing engine code and MCP tools; no runtime library.
