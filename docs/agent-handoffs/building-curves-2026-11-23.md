# Building grammar Stage 4 — curved forms and minimum gate size

Date: 2026-11-23
Branch: `feature/aiv20261123_building-grammar`; PR: https://github.com/chromealex/AIVERSE/pull/114
Parent handoffs: `docs/agent-handoffs/building-grammar-2026-11-23.md` and
`docs/agent-handoffs/building-forms-2026-11-23.md`.
Stage roadmap commit: `1871c9397849bece528a6a226fe9d13cb844457e`.
Curved forms implementation: `601b14f8c78d76ee76e6e8c07afb4081e852182a`.
Focused workflow: `01e9dbca7e1c9da5cb06e73c9760e6fdf21b1c07`.

## Architecture and implementation

- `StructureForm::Kind` extended with `Arch`, `Dome`, `SweptHull`
  in `libraries/include/aiverselib/systems/BuildingStyle.h`; all share
  existing bounded JSON form schema (`kind,material,at,size,level,taper,chance,
  repeat,step`). Unknown types still throw `JsonError` with field path.
- `libraries/include/aiverselib/systems/BuildingStructure.h` adds reusable
  mesher helpers `addArch`, `addDome`, `addSweptHull` to
  `buildingStyleDetail`. Arch has a 12-segment curved beam and separately
  generated collision for uprights and beam arc to preserve the opening;
  dome uses 16 azimuth sectors and 7 vertical bands; swept hull uses
  12-sided, 8-station rings with curved radius/elevation along depth.
  No separate host or genre-specific generator.
- Under explicit `StructureSize`, current final-envelope fit rejects visual
  front gate size below 1.6m width / 2.1m height. This is not exact
  conservation of all individual part dimensions.
- `engine/assets/building-styles/scifi.json` declares an arch, two domes
  and one swept hull across the four existing sci-fi functions.
- `tests/libraries/building_style_tests.cpp` now asserts use of all seven
  geometric kinds, deterministic geometry, invalid configuration rejection,
  low-envelope rejection, finite curved vertex positions/normals and a
  free-standing arch centre unobstructed by its own segmented collision.
- `.github/workflows/building-style-focused.yml` performs focused C++20
  compilation and execution without the existing, known-broken
  `tools/libraries/catalog.py check` gate; exports four exact-dimension GLBs.

## Tests / real evidence

- Code, data and tests committed; original function contracts preserved.
- No local CMake, GPU screenshots or modelviewer output was executed in this
  model runtime. Do not describe as passing until a GitHub workflow has
  reported success. The preceding overall PR CI had unrelated catalog
  assertion failures, plus game-host override errors in Web builds.
- Focused GitHub CI job URL is discoverable through Actions on this branch:
  `Building Style Focused`. Check output and fix any C++ failures.
- No new public site/devlog entry: studio screenshot and web demo have
  not been produced. Roadmap stage remains in-progress.

## Next steps

1. Evaluate the focused CI compile and regression results, fix failures.
2. Render all four kinds with multiple seed/dimension/yaw combinations in
   `aiverse_modelview` (real screenshot evidence).
3. Replace the legacy final nonuniform scale with physical clearances and
   fixed thicknesses/doorways by semantic geometry parts (important).
4. Add broader free-standing and connected structures with collision and
   navmesh audit; avoid claiming enterability where hull boxes remain closed.
5. Support setting/style/function registries, project asset persistence,
   and the HTML5 Studio generator window using the shared UI kit.
6. Main integration must reconcile the old feature branch behind main;
   do not silently move PR to main or modify independent agents' work.

Release notes candidate for 3.1.0 once tested and merged:
"Curved architectural modules and parametrically sized building models."
No save format migration; additions to optional style JSON schema only.
