# Handoff — AIVERSE building composition stage 3 (2026-11-23)

Branch: `feature/aiv20261123_building-grammar`. Parent: `feature/aiv20261122_building_styles` (base of draft PR #114).
Stage 2 handoff: `docs/agent-handoffs/building-grammar-2026-11-23.md`.
Current stage started with roadmap commit `642c9a84ff78f2be262c689e28592aa601788339`.

## What changed

- `BuildingStyle.h`: new `StructureForm` data/enum and strict `structures.<kind>.forms`
  parser; 24 entries per kind, up to 8 repeats each, validated extent/chance/
  repetition bounds. Existing styles with no `forms` still work.
- `BuildingStructure.h`: render pods, cylindrical silos, tapered pylons,
  open-frame gantries with conservative collision boxes, in footprint-relative
  proportions before the legacy seeded roof props. Roof volumes reserved
  before roof props to reduce intersections. Each form uses a palette material.
- `scifi.json`: functional visual differences for `barracks`, `command`,
  `factory`, `depot`; combinations of pods/pylons, gantries and silos.
- `building_style_tests.cpp`: checks each shipped role has geometry beyond the
  shared base silhouette, validates material/shape schema and bounded counts,
  determinism across seeds/yaw, triangle bounds.
- `docs/building-style.md`, `skills/world/aiverse-building-style/SKILL.md`
  updated with the new format; roadmap entry added in `docs/devlog/systems.json`.

## Architecture and API

No gameplay genre split. The public function stays
`dressStructure(style, kindId, seed, origin, yaw, StructureSize)`.
`structures.<kind>.massing` selects base silhouette, while `forms` composes
large extra masses; material ids resolve against `style.materials`.
Coordinates in `forms` are normalized to the model footprint/body height;
all forms use the existing `Mesher` and update `StyledStructure::boxes`.
The `--structure ... --size W H D` GLB exporter from stage 2 remains.

## Verification actually performed

- Source-level anchor checks and GitHub write confirmations: yes.
- JSON structure generated with `JSON.stringify` before GitHub update: yes.
- Added C++ tests **not yet compiled or executed here** (no local repository
  worktree in this environment). Do not mark them passed.
- Previous PR CI (#114, run 45203138238) had a failed Web job reporting
  `-Winconsistent-missing-override` in `engine/game/GameCore.h` inherited
  from base; the other CI jobs were then cancelled / pending, so **this does
  not validate or invalidate the new building code**. Recheck latest CI
  job statuses and inspect logs before integration.
- No real modelviewer screenshot, Metal GPU capture or web demo for stage 3;
  do not update public devlog without real visual evidence.

## Local verification commands

```sh
git fetch origin
git switch feature/aiv20261123_building-grammar
cmake -S . -B build/building-forms -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_TOOLS=ON
cmake --build build/building-forms --target aiverse_library_building_style_tests aiverse_library_building_tests aiverse_building_export -j 4
ctest --test-dir build/building-forms -R '^library_building_(style|gen)$' --output-on-failure
find build/building-forms -name aiverse_building_export
```

Export every kind with `--size 24 12 18 --seed 5` and separately with
seed 19. Inspect actual silhouettes in `aiverse_modelview`, check no
inverted/degenerate faces, roofs, clearances and unrotated/rotated bounds.
Run `python3 tools/site/build.py check` and broader CI after syncing main.

## Remaining work

1. Get real GPU screenshots and CPU test results. Fix surfaced compilation/
   geometry issues before marking roadmap stage done.
2. Replace final nonuniform envelope fit with physical-size-constrained
   constructive grammar: doors, railings, steps and panel thickness must remain
   realistic across extreme aspect ratios.
3. Extend primitives (arched ribs, domes, swept hulls), GLB kit slots and trim
   textures, add UV/material validation and GPU/material draw-budget tests.
4. Split Setting/Style/Function registries, then implement the data-driven
   generator panel in HTML5 Studio with seed, dimensions, 3-option preview and
   Assets GUID/meta + Undo/Redo.
5. `StyledMesh` → `render::Model` native adapter and ECS asset instances;
   collision/navmesh follow the world owner. No genre-specific adapter fork.
6. PR #114 targets the previous building-style branch, which is behind main;
   the integrator must resolve that divergence with concurrent work.

3.1.0 release note suggestion once verified and merged:
"Procedural sci-fi buildings combine configurable architectural masses,
including industrial silos, wings, pylons and gantries."
No authored save migrations or existing call sites are required.
