# TerraWeave Caverns — Stage 23 handoff (2026-11-23)

## Repository, branch, and actual commits
Repository: `chromealex/AIVERSE`.
Working branch: `feature/terraweave-caverns-core-20261123`, **not merged into main**.

Stage23:
- `f037399567d0acf2053af231502d50594371c760` — `apps/editor/Editor.h`: CPU runtime/telemetry fields and lifecycle method.
- `225b9d01fadf727edd6193c1006cd211e49b91ea` — `apps/editor/Editor.cpp`: `CavernsEditHost` observes MapDoc/Undo changes, prepares versioned CPU chunks in the editor update loop.
- `98111efbe584600fc93053dd0c3ffaffc65d11f9` — `Editor.cpp`: include previous/new entrance-shaft bounds and fail closed when full generator settings change.
- `29ddeff64a597bc90c4cbc67c197a6753ef652f7` — `apps/editor/windows/TerrainWindow.cpp`: show CPU dirty/prepared/inFlight telemetry and explicitly state GPU/physics not published.
- `94e602a9397b1187984186610b857e019a3e3059` — `engine/render/TerraWeaveCavernsEditPresentation.h`: turn validated edit candidates into Stage11 GeometryScene CPU source/manifest.
- `e81c6ef5428164b77e683dcad265e3d93f3d6a48` — `tests/terraweave_caverns_edit_presentation_test.cpp`.
- `e5c076a42eabb3f8725889eee667e3aef993bf3d` — `skills/world/aiverse-terraweave-caverns/SKILL.md` update.
- This handoff is a subsequent commit. Check current branch HEAD for exact final tip.

## Implemented architecture and API

**Editor lifecycle:** `Editor::updateCavernsAuthoring()` now runs immediately after `windows_[].update` each native editor update. The editor-owned `CavernsEditHost` observes `MapDoc::extra["terraweaveCaverns"]`, including editor/MCP brush writes and Undo/Redo. On change it canonicalizes a new Stage18 `CaveVolumeSnapshot`, unions previous and new stamp-sphere and entrance-shaft influence bounds including gradient halo, and calls the Stage22 `CaveEditRuntimeBridge::invalidate`. It then drives `bridge.tick(2)` each frame, collecting and scheduling bounded immutable CPU tasks.

**Undo source revisions:** MapDoc Undo can restore an older persisted sourceRevision. The editor therefore uses a monotonically increasing runtime author epoch for worker request generation, without overwriting the actual saved revision; this prevents stale worker results from being mistaken for a current edit. Project change destroys/joins previous CPU worker state.

**Broad-change safety:** Generator settings changes affect whole worlds, not a local stamp footprint. The current host refuses an in-place local rebuild and emits a diagnostic requiring full loaded-region reload. It does not silently substitute a stamp-only rebuild. If old/new influence exceeds the queue budget, it fails closed; broad conservative union can exceed bounds.

**GeometryScene presentation staging:** `prepareCaveEditPresentation(candidates,generatorVersion,options)` checks source/render revisions and converts prepared CPU meshes into actual Stage11 `CaveBatchPrepared` containing source-keyed `render::Model` objects and versioned GeometryScene/STRATA manifest. A future GPU owner can use this as the staging input. It does NOT itself run `GeometryScene::loadPrepared`, swap graphics scenes or physics.

**Editor UX:** Native TerrainWindow Caverns pane displays pending, prepared and in-flight CPU mesh counts and diagnostics. Explicit warning says no GPU/STRATA/Box3D/NavMesh update occurs yet; CPU completion is NOT counted as a successful render/physics commit. No new shader, tool or genre dependency was introduced.

**Test source:** `tests/terraweave_caverns_edit_presentation_test.cpp` checks two chunk source models with negative signed coordinates, stable order, STRATA manifest flag, validation of malformed revision and duplicate key.

## Files modified
- `apps/editor/Editor.h`
- `apps/editor/Editor.cpp`
- `apps/editor/windows/TerrainWindow.cpp`
- `engine/render/TerraWeaveCavernsEditPresentation.h` (new)
- `tests/terraweave_caverns_edit_presentation_test.cpp` (new)
- `skills/world/aiverse-terraweave-caverns/SKILL.md`
- This handoff.

## Actual tests and limitations
- GitHub connector reported successful creation/update commits. Existing `Editor::update`, `Editor::~Editor`, editor Scene overlay and native Workspace/MapDoc APIs were inspected via GitHub before implementation.
- **No full-checkout C++ compilation, CMake/CTest, GPU screenshots, native UI acceptance, Box3D/NavMesh or STRATA runtime test has been executed for Stage23.** Source-only test is NOT RUN. There is no supported claim that the native editor already builds.
- Editor runtime currently batches broad all-stamp old/new influence regions, not minimal spatial diffs; this may exceed the maxQueued cap for large projects and block until spatial-indexed incremental invalidation is implemented.
- A world with a changed generator/settings requires full re-generation. The current editor stops local updates with a diagnostic; the complete load/transition path is NOT provided.
- No actual feedback link from prepared CPU mesh to native Scene's GeometryScene draw call exists yet. `CaveEditRuntimeBridge::publishReady` is purposefully NOT called with true in the editor. GPU staging, lease/fence retirement, Box3D, SpatialWorld and nav::Mesh replacement remain missing.
- The editor host has not been independently unit tested; only a source-level presentation-manifest test was added. Test on real GUI, Undo/Redo and scene open/project switch before main merge.
- Existing Stage16 seam gate can block geometries with differing topology; Stage22 runtime bridge does not itself schedule fallback seam jobs. Preserve the fail-closed behavior.

## Concrete next steps
1. **High priority: compile/link Stage23 native editor and all Caverns Stage1–23 tests** in a complete AIVERSE checkout; address source and CMake errors, run real UI and GPU diagnostics. Previous local baseline Stage2 mesh test is not evidence for Stage23.
2. Implement the real gfx-owner staged GeometryScene/STRATA publication path, retaining old visual scene until the candidate is certified and all dependent physics/nav owners are ready. Use an explicit GPU fence/lifetime proof before retiring old resources.
3. Add authoring diff and loaded-resident spatial map for precise dirty updates, complete field/version change and undo handling, seam fallback worker scheduling and broad-world reload.
4. Add a safe all-or-none Box3D/SpatialWorld/NavMesh swap barrier and actual gameplay acceptance, including navigation after cave carving and filling.
5. Keep the mandatory dated handoff and skill update for EVERY stage. Do not merge main until tests and integration have evidence.
