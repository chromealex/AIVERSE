# AIVERSE LENS Stage 25 — preflight shared non-GPU reload inputs (2026-11-20)

## Branch, exact commits and changed paths
- Repository: `chromealex/AIVERSE`; feature branch: `feature/lens-motionweave-integration`; `main` unchanged.
- Implementation `e14c1a19308a8be44575623dea721b5e17834a40`: `modules/rts/view/RtsView.cpp` reuses already parsed `incomingGame` to select optional geometry scene rather than re-reading `game.json` mid-reload.
- Implementation `3e5ca05308add098570b830650c3e9e0ac2ee298`: same file, parse `materials.json` before staging sprites and publish `incomingMaterials` / recorded parse warnings during the existing reload.
- Implementation `338a8a222da08ae4b06e08bd44004062f4a9fec4`: same file, parse geometry descriptor via `incomingRead` before staging sprites, retain parse error as warning, load already parsed geometry only at the normal GPU publication point.
- Prior handoff: `docs/agent-handoffs/lens-motionweave-integration-stage24-config-recovery-2026-11-20.md`.

## Design and compatibility
- `RtsView::init` now pre-parses `game.json`, composed rules, optional geometry descriptor, and shared materials before publishing the staged `Rts2DView`. Fog, overlays, roads, shores, sprite defaults, geometry descriptor, and material library share a consistent incoming configuration snapshot. No repeated `game.json` read in the later geometry phase.
- Optional missing/broken geometry descriptor and materials parser errors still produce warnings without making all of `RtsView::init` fail; no GPU creation occurs at descriptor pre-parse time.
- The existing Stage 24 pending sprite GPU recovery and old-view idle checks remain. ECS/world simulation is not rerun, no MotionWeave advancement is introduced, and no save, scene, or public camera API schema is modified.
- Only non-GPU *configuration* staging is improved. GeometryScene GPU creation, terrain/road/shore/overlays GPU resources and remaining mutable presentation owners are **not** atomically swapped. Runtime game assets read by `geometryScene_.load` may still change between parsing and loading.

## Validation actually performed
- Read latest LENS branch `RtsView.cpp` and Stage 24 handoff; after commits re-fetched `RtsView.cpp`.
- Source assertions: one `incomingRead("game.json")` occurrence, one `incomingRead("materials.json")`, geometry descriptor parsed before `twoD_=`, geometry and material publication uses the pre-parsed objects; pending GPU recovery logic still present.
- CMake configure **NOT RUN**, native C++ compiler **NOT RUN**, CTest/fault injection **NOT RUN**, Metal/Vulkan/GLES compilation and GPU validation **NOT RUN**. No benchmark claimed.

## Known issues / next exact actions
1. Finish transaction across **GPU** presentation owners: stage `GeometryScene`, fog, overlays, roads, shores, terrain and dependent model assets without modifying live instances until successful construction. Identify all owners' GPU lifetime/retirement contracts before changing them.
2. Add integration-level reload regressions proving old scene/sprite/selection remain functional following failed incoming materials, geometry GPU allocation, and staged sprite init with GPU idle failure.
3. Validate `RtsView::init` on native compiler and run `lens_sprite_gpu_faults`. Resolve test harness and remaining Stage 18/19 compile risks before any merge.
4. Merge the current MotionWeave branch via real merge base and conflict-by-conflict reconciliation, preserving changed `apps/modelview/main.cpp`. Create separate handoff, then run user-requested consolidated CMake build/CTest and Metal/Vulkan/GLES checks.
