# PHYSIS Stage 72 — merge main with completed NEREID Water

Date: 2026-11-19
Repository: `chromealex/AIVERSE`
Branch: `feature/physis-nature-simulation`
PR #25: https://github.com/chromealex/AIVERSE/pull/25 (DRAFT)
Last PHYSIS pre-merge head: `04f3fae2576edf92aeddfcd3ab3f6aab77d1d8e7`
Main NEREID merge head: `d71c6d47e532b25fb816a8bb1f96c41e5275a165`
**True merge commit:** `7c617d7fbc1a7ff7529e84a8e7e281f5f3298f0b`
Common ancestor: `9ee6ff6fbf390bd45363bc92c838f492dfe92eee`
Previous handoff: `docs/agent-handoffs/physis-stage71-macos-gui-build-green-2026-11-19.md`
This handoff's documentation commit: inspect
`git log -1 -- docs/agent-handoffs/physis-stage72-main-nereid-merge-2026-11-19.md`.

## User request

Import the current `main` into the PHYSIS feature branch because
NEREID water was already merged in main. Preserve all existing
PHYSIS work, do NOT alter main, and retain clear PHYSIS/NEREID/Terra
ownership. This is a source merge, NOT automatic gameplay integration.

## Exact merge performed

GitHub branch comparison showed **370 commits ahead on main** and
**496 on PHYSIS** relative to their common ancestor, so fast-forward
was impossible. Created a true two-parent commit with the PHYSIS
head as first parent and main head as second parent, and advanced
the PHYSIS branch with an expected-head lease (no force push).

A content-addressed three-way tree merge imported **97 main-only
file changes** and reconciled **12 overlapping paths**. Added/retained:
- `libraries/include/aiverselib/water/Nereid*.h` water ECS bodies,
  sampling, currents, buoyancy, interaction/wake, vortices, cascades,
  confluences, geometry, water surface meshes, and native pipeline.
- `engine/world/Nereid*.h`, `engine/render/Nereid*.h`, renderer
  and WorldScene changes; the optional read-only
  `engine/world/NereidPhysisWindBridge.h`.
- NEREID test suite, validation script, workflow, skills and docs,
  with `tests/AlwaysCheck.h` and release-test assertions.
- Combined `AGENTS.md`, `CMakeLists.txt`,
  `skills/catalog.json` and `tools/skills/catalog.py` without
  duplicating VFX test targets. Advanced PHYSIS SpectraFX source,
  graph/canvas/Metal prototype, docs and Stage7+ handoffs retained;
  main's corrected VFX ECS regression replaces the older unsigned
  `before-20` particle-count test. The water integration's VFX
  Stage45.4 addendum was preserved in the universal VFX handoff.

## Static verification actually performed

- GitHub compare `main...feature/physis-nature-simulation`:
  **ahead 497, behind 0**. Checked merge commit has EXACTLY two
  expected parents, current `main` is an ancestor.
- Combined CMake contains
  `AIVERSE::lib_nereid_water` link, nine `ecs_nereid_*`
  test declarations, `physis_core`, and exactly one
  `vfx_graph_compiler` and `vfx_graph_assets` test registration.
- Skills catalog parsed as JSON: **220 unique skill IDs**, including
  `aiverse-physis`, six NEREID skills and SpectraFX.
- Root AGENTS retains both PHYSIS and NEREID ownership and
  the Release-mode always-on test assertion rules.
- `tests/vfx_ecs_presentation_test.cpp` includes the main
  `impactCount()==3` count-based regression.
- This is **static GitHub source/tree verification**, NOT native
  compilation, CTest, GUI, GPU or target-device QA.

## Required authority boundary and available adapter

- PHYSIS: authoritative weather, climate, wind, storms, gravity,
  precipitation intensities, wetness/snow/ice, fire/quake.
- NEREID: authoritative water-body surfaces/depth, river/ocean
  geometry, currents, buoyancy/contact, water rendering and water
  gameplay sampling.
- TerraWeave: terrain/foliage/biome ownership; do not duplicate
  PHYSIS climate or NEREID water.
- PHYSIS `FloodMode::LocalShallow` is an experimental standalone
  cellular flood, NOT a NEREID solver. Explicitly configure
  `InstallOptions::floodMode=FloodMode::ExternalAuthority` for
  native worlds using NEREID water authority.
- `engine/world/NereidPhysisWindBridge.h` accepts read-only
  wind samples (plus zones) and updates NEREID **visual** wind.
  It does not install PHYSIS/NEREID, advance either ECS module,
  or convert rain to body volume. Host must control scheduling,
  save/rollback authority, units and opt-in semantics.

## Test status — precise

Previously user-reported before this merge:
- PHYSIS headless + Box3D macOS CTest: 24/24 PASS.
- PHYSIS Editor MapDoc authoring: 2/2 PASS.
- PHYSIS remote test after namespace fix: 1/1 PASS.
- `aiverse_editor` native macOS build: 100% completed.
- NEREID main handoff records user-reported full native macOS
  CTest 119/119 PASS on its earlier water integration history,
  not on this combined PHYSIS merge.
- User also submitted a cropped PHYSIS GUI heatmap image showing
  populated raster/zone rings, but this is not full editor QA.

**NOT RUN after merge:** any PHYSIS/NEREID combined CMake configure,
native compile, CTest, GPU/Metal or GL water visual/throughput tests,
cross-platform CI, full interactive GUI gizmo/Undo/Redo, authenticated
remote networking/security acceptance. PR #25 remains DRAFT.
A true merge is NOT proof the combined build is green.

## Next agent concrete step: compile PHYSIS + NEREID together

```sh
cd ~/Projects/AIVERSE && \
git fetch origin && \
git switch feature/physis-nature-simulation && \
git pull --ff-only origin feature/physis-nature-simulation && \
cmake -S . -B build/physis-nereid-validation \
  -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_EDITOR=ON \
  -DAIVERSE_ENABLE_PHYSICS=ON && \
cmake --build build/physis-nereid-validation --parallel 4 && \
ctest --test-dir build/physis-nereid-validation \
  --output-on-failure --parallel 4 \
  -R '^(physis_|ecs_nereid_|vfx_)'
```

On failure inspect the FIRST actual compiler or test error before
modifying code. On success capture full CTest logs/commit, test
the real editor UI/Scene PHYSIS overlay and NEREID water rendering
on target backends, then run broad repository-wide regression.
Do not merge PR #25 to main until remaining review/validation.
Follow the cross-agent handoff contract after each stage.
