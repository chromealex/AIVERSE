# Handoff: building styles (stage 1, sci-fi) — 2026-11-22

Branch `feature/aiv20261122_building_styles` (worktree `.worktrees/feature/aiv20261122_building_styles`).
Capability W3 «Генерация структур мира» (docs/capability-status.json): first step on procedural architecture
as authored rules.

## What exists now

- `libraries/include/aiverselib/systems/BuildingStyle.h` — package `building-style` (catalog, `AIVERSE::lib_building_style`,
  depends on `building-gen`): `readBuildingStyle`, `applyBuildingStyle`, `dressBuilding` → `StyledMesh`
  (vertices, one index list per material, materials after the seeded palette, bounds).
- `libraries/include/aiverselib/systems/BuildingStructure.h` — `dressStructure`: one armoured hull per building
  (owner: "StarCraft 2 style, one building, not office blocks"). Kinds in the style's `structures` block.
- `engine/assets/building-styles/scifi.json` — sci-fi: structures `barracks`, `command`, `factory`, `depot`
  (cut-corner leaning hull, armour plates with team colour, modules, tower, gates with hazard stripes and
  ramps, landing legs, rear thrusters, vents, stacks / tanks / dish); also a military facade set for
  enterable buildings (skirt, buttresses, chamfered corners, pipes, hooded slit windows).
- `engine/assets/building-styles/modern.json` — the first draft (owner: "not sci-fi, just buildings"), kept as a
  modern-city style: ribbon windows, panels, light strips, crown, fins, signs.
- `BuildingGen.h` (additive): `BuildingLayout::openings` — every window and the door (hole box through the
  outer wall, floor, side, door flag). Boxes and determinism of building-gen are unchanged.
- `tools/buildings/building_style_export.cpp` → `aiverse_building_export` (.glb of one building or a street).
- Tests: `tests/libraries/building_style_tests.cpp` (CTest `library_building_style`, both CMake trees).
- Docs: `docs/building-style.md`, skill `skills/world/aiverse-building-style/SKILL.md`, skills catalog keywords.

## Verified

- `aiverse_library_building_style_tests` and `aiverse_library_building_tests` pass (g++ 13, Linux, -Wall -Wextra -Werror -pedantic).
- Frames: `aiverse_modelview` (Linux, Mesa GL under Xvfb, shadows high) of the sci-fi barracks, command centre and
  factory (seed 3); devlog entry media.
- Owner feedback history: v1 facades on floors read as office blocks; v2 military facades still read as
  offices in a street; v3 structures accepted for review.

## Not done / next stage entry points

1. Runtime: a structure / style reference in map data and a `StyledMesh` → `render::Model` adapter; for buildings
   a `"style"` key in the actor map `buildings` block (`engine/actor/ActorBuildings.cpp`) and a
   presentation adapter `StyledMesh` → `render::Model` so games draw dressed buildings instead of collision
   boxes (`apps/fps/client.cpp` draws collider boxes for ids ≥ `kBuildingColliderBase`). Load styles by id from
   `building-styles/<id>.json` through `platform::readContentFile`.
2. Structure look: more greebles (rivets, panel lines via a trim texture), animated lights, lift-off pose;
   footprint grid sizes per game; collision boxes for the RTS footprint.
2b. Kit models in styles (GLB pieces per element), trim-sheet textures and UV use (UVs are already metres).
3. A Protoss-like style (smooth gold/white forms, crystals) needs curved / sloped massing elements first.
4. LOD: merged far mesh per block, drop props and panels at distance.
5. Footprints beyond rectangles / setbacks need building-gen changes first.
6. Metal / mobile GPU not checked; triangle budget: sci-fi 22 × 16 m two floors ≈ 5–8k triangles.

## Checks not run / caveats

- Built and screenshotted in a Linux cloud workspace (2 cores), not on KINGSTON: no external drive there.
  `aiverse_tests` (full suite incl. FPS `buildings`) not run; `BuildingLayout::openings` is additive.
- `tools/libraries/catalog.py check` fails on `origin/main` already (nereid-* packages have status `cpp-runtime`);
  not caused by this branch. `tools/skills/catalog.py check` passes (catalog regenerated).
- `tools/site/build.py check` not run: the VM's Python cannot import `tools/site/render.py` (f-string syntax of
  Python 3.12+). Run it on the Mac before merging.
- Metal / mobile not checked. Devlog entry is `in-progress` until merged into `main`.
