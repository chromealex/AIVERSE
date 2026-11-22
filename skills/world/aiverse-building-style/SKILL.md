---
name: aiverse-building-style
description: Generate AIVERSE buildings in a setting from a style file - whole armoured RTS-style structures (sci-fi barracks, command centre, factory, depot) or dressed enterable building-gen houses (modern city); add or tune a style, export to .glb and check in the model viewer.
---

# building-style

Use when generated buildings must look like a setting rather than grey boxes, or when adding a new setting.
The setting is data: `engine/assets/building-styles/<id>.json`. The code (`BuildingStyle.h`) knows only
generic elements, so a new style never needs new C++ unless it needs a new kind of element.

Two generators share one style file: `dressStructure` (`BuildingStructure.h`) - one armoured hull per building,
the sci-fi look (kinds in the style's `structures` block) - and `dressBuilding` (`BuildingStyle.h`) - facades on
enterable building-gen houses. For an RTS / StarCraft-like base use structures; floors with windows always read
as city buildings.

0. Structure: `dressStructure(style, "barracks", seed, origin, yaw)` → `mesh`, `boxes` (collision), `gates`
   (ground points in front of the gates). Export: `aiverse_building_export --style <style> --out b.glb --structure barracks --seed 3 --ground`.
   New kind = new entry in `structures` (size, height, taper, chamfer, bands, modules, tower, door, legs, thrusters, props).
1. Read `libraries/include/aiverselib/systems/BuildingStyle.h` (namespace `aiverse::lib::systems`) and the
   reference style `engine/assets/building-styles/scifi.json`.
2. Load: `readBuildingStyle(json, "path")` - errors name the path (`style.panels.material: unknown material 'x'`).
3. Definition: build the `BuildingDef` as for building-gen (agent radius / height / step from the owner's
   motor), then `applyBuildingStyle(def, style)` writes the style's proportions (window / door size,
   floor height) when they still fit; otherwise the definition stays unchanged.
4. Mesh: `dressBuilding(def, style, seed, origin, yaw)` with the same seed / origin / yaw as
   `generateBuilding` - the mesh covers exactly that collision. It returns vertices (position, normal, UV in
   metres), one index list per used material and the materials after the seeded palette. Empty mesh = the
   building does not fit.
5. Look at it: `aiverse_building_export --style <style.json> --out street.glb --street 10 --seed 3 --ground`
   (or `--building def.json --seed 7` for one house), then `aiverse_modelview street.glb` (screenshots with
   `AIVERSE_SCREENSHOT`, see AGENTS.md section 4).
6. Check: `aiverse_library_building_style_tests <repo>` (CTest `library_building_style`): the style file loads,
   door corridor free, window holes hold only glass, determinism, seeds and palettes vary, yaw, budget,
   JSON errors. Add every new style file to the test's list of shipped styles.

Style file elements (all optional except `materials`, `shell`, `interior`, `roof`):
`materials` (linear colour, metallic, roughness, emissive + strength), `palettes` (seeded colour sets),
`proportions`, `panels` (cladding grid with accent chance and depth range), `base`, `band` (+ `light`),
`corners`, `window` (frame, glass, lit glass chance, sill `strip`), `door` (frame, `light`, canopy),
`parapet`, `crown` (slanted visor with mitred corners), `ribs` (fins between window columns), `sign`
(vertical light sign), `skirt` (sloped armour), `buttresses`, `pipes`, `hazard` stripes, chamfered `corners`,
window `hood`, `roofBlocks` (clad machine rooms), `props` (`box`, `mast`, `tank`, `stack`, `dish`, with `tip` lights).
Shipped: `scifi.json` (structures: barracks, command, factory, depot; plus a military facade set), `modern.json` (glass-and-panel city).

Adding a style: copy the closest shipped style, change materials, palettes, proportions and which elements exist
(a medieval style drops skirt / pipes / props and uses small windows and timber bands). Keep emissive
materials for real light sources. Check triangle count (the export prints it) against the target devices.

Not implied: kit models (GLB pieces) in styles, textures, non-rectangular footprints, a map block that builds
the Model at load (debt: hosts call the library; the actor map `buildings` block still draws its collision boxes),
LOD / merged far meshes, interiors furniture.

Build selection: `AIVERSE::lib_building_style` (depends on `building-gen`); native OBJECT modules call
`aiverse_native_use_libraries(target building-style)`.
