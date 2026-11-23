# TerraWeave Caverns — Stage 20 handoff (2026-11-23)

## Provenance
Repo `chromealex/AIVERSE`, working branch `feature/terraweave-caverns-core-20261123`, NOT merged into main.
- `159574fcdae6cbed68637c9147be292f5956d886` — `engine/world/TerraWeaveCavernsRaycast.h`.
- `8bd657f8a620369c53a4393df7ed20d1b894575d` — initial TerrainWindow ray brush.
- `f33724ec591ad3d9557d401ab2d2cc97ca5b2bfc` — removed erroneous `Editor::groundAt` requirement before cave-wall picking.
- `2b31b61f010205065670df89b237f7d22b86bfac` — `tests/terraweave_caverns_raycast_test.cpp`.
- `a358aa82d5507883d61b31a57b1b548e49eec4f0` — `apps/editor/core/TerraWeaveCavernsOps.h` dirty bounds.
- `a9e3026c2fdb42616151cfe0735b13d2ceaa6166` — updated `skills/world/aiverse-terraweave-caverns/SKILL.md`.
- This handoff is a subsequent commit; inspect branch HEAD for final SHA.

## Source and API
- `CaveRayCm` signed centimetre origin and normalized signed Q20 direction.
- `raycastCaveSurface(CaveVolumeSnapshot, CaveRayCm, maxDistanceCm, stepCm)` performs bounded world-space ray sampling of the authoritative authored density (analytic base cave, entrances, author Carve/Fill stamps), detects an air/rock sign transition and binary searches it to a one-centimetre interval. Returns `CaveRayHit` (world cm, distance and whether entering void), or nullopt if no sampled crossing.
- `dirtyBoundsForStamp` and `dirtyCaveChunks` derive half-open signed world XYZ extents with configurable sample halo, partition-independent negative floor division and bounded output.
- `TerrainWindow::update` uses `sceneCam.screenRay` for the Caverns brush and feeds the hit's actual XYZ to the existing `terraweave_caverns_paint` Workspace op. A cave hit no longer depends on `Editor::groundAt`. Shift inverses Carve/Fill, MapDoc stroke persistence and Undo remain unchanged.
- `TerrainWindow::draw` displays hovered hit XYZ as a textual preview; no rendered 3D gizmo yet. Mouse input still follows existing Tool::Count active-window ownership.
- Editor op now emits `dirtyBounds` on Carve/Fill/Erase, allowing later worker scheduling to invalidate the stamp AABB plus 100cm gradient halo. No runtime subscription or live resident publication is implemented by this step.

## What was actually tested
- GitHub connector acknowledged stage20 source/test/skill writes.
- Examined `apps/editor/Editor.cpp` existing `Camera::screenRay` usage and removed the ground-only restriction.
- **Full native AIVERSE checkout compilation, linked CTest, end-to-end editor mouse click, cave-wall selection, STRATA/Box3D/NavMesh and GPU screenshots NOT RUN.** Tests are authored, not passing evidence.
- New regression source `tests/terraweave_caverns_raycast_test.cpp` checks carve-sphere entry/exit, miss, invalid direction, and negative/positive dirty chunk coverage. Suggested compilation in a full checkout:
  `c++ -std=c++20 -O2 -I. tests/terraweave_caverns_raycast_test.cpp -o /tmp/caverns-raycast && /tmp/caverns-raycast`
- No screenshot or visual UI proof is claimed.

## Remaining issues and next steps
1. **Performance:** the current hover path reparses/canonicalizes `MapDoc::extra["terraweaveCaverns"]` each frame and samples SDF repeatedly across a long camera ray. Cache the snapshot on sourceRevision/document identity and add a bounded acceleration index for author stamp and entrance influences.
2. Ray marching uses fixed step with sign changes only; very thin features may be missed. A variable conservative step or exact root-interval bounds could improve picking. Its hit is SDF geometry, not necessarily final STRATA mesh.
3. Add a world-projected 3D sphere/wire gizmo and editor input hover lock, including cursor hit on negative heights and complex overhangs. Existing text preview is not a gizmo.
4. Wire dirty bounds (including full influence halo) to `CaveSnapshotRegenerator` and seam gate, then stage/update GeometryScene/STRATA, SpatialWorld, Box3D and NavMesh with proper owner-thread all-or-none visibility. The stage20 tool does not yet update live GPU/physics.
5. Register and run all Caverns Stage1–20 CMake/CTest and full editor integration; fix actual compiler/runtime defects before merging main.
6. After each stage update the skill and a dated handoff; keep edits isolated from RTS/FPS-specific modules.
