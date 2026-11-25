# TerraWeave Caverns — stage 25 handoff (2026-11-23)

## Repository / provenance
Repository `chromealex/AIVERSE`, branch `feature/terraweave-caverns-core-20261123`, not merged to main.
- `d1131ccf82df8ac79f15b79c7473766fa90f406a`: created `engine/world/TerraWeaveCavernsOwnerBarrier.h`.
- `7dc4bc3f7b8a2a2afea8d750ea741c8457212ea1`: created `engine/render/TerraWeaveCavernsEditedSceneTransaction.h`.
- `27c4f2dcb8419f59e2aafe487ae74034463788b3`: created `tests/terraweave_caverns_owner_barrier_test.cpp`.
- `0e7a15e459ee1b2046fb7f430bf874568b1c4f4d`: reject composite/unknown owner-mask preparation.
- `ff6730ab42acf9ee5ba54ad07782989cb8b15c0a`: add composite mask test case.
- `395356a4f5e94fe97b8223be3bbd2c9d12a97b25`: updated `skills/world/aiverse-terraweave-caverns/SKILL.md`.
- This handoff commit follows; inspect current feature HEAD for final tip.

## Existing APIs inspected
- `engine/render/GeometryScene.h/.cpp`: staged `loadPrepared`, `draw`, retirement and STRATA sidecars. `GeometryScene::retireResident` can require `gfx::Device::waitForResourceIdle()` for STRATA metadata/index resources, so full asynchronous retirement is not guaranteed.
- `engine/world/TerraWeaveCavernsBox3D.h`: `CavernsBox3DPublisher::publish` immediately creates a new body and destroys the old body. It is NOT a safe prepare-only phase for a multi-subsystem atomic commit.
- `engine/world/TerraWeaveCavernsNav.h`: surface nav refresh updates the existing `nav::Mesh`, not a fully staged/reversible shadow navmesh.
- `engine/world/TerraWeaveCavernsPublication.h`: earlier Stage8 per-chunk status coordinator is bookkeeping only and cannot roll back a partially applied Box3D/NavMesh swap.
- `engine/render/TerraWeaveCavernsGpuSwap.h`, `engine/world/TerraWeaveCavernsEditRuntime.h`, and `engine/render/TerraWeaveCavernsEditPresentation.h` supply the Stage24 GPU scene owner, stage22 CPU edit bridge, and CPU GeometryScene manifest conversion.

## Implemented this stage
- `CaveOwnerBarrier`: a self-contained, genre-independent four-owner preparation protocol keyed by monotonically incrementing `CaveOwnerBatchTicket`. Required bits Geometry/Spatial/Box3D/Navigation must all be marked by the real owner before a ticket becomes ready. Composite and unknown flags are rejected. Stale tickets cannot affect newer stages; false external commit marks Aborted; Committed ticket cannot be aborted.
- `CaveEditedSceneTransaction` links Stage23 CPU candidate batches to Stage24 GPU-staged `GeometryScene`. It validates consistent candidate source revision, stages source CPU models through actual `CaveGeometrySwapOwner::stage(device,...)`, then opens an owner ticket with the Geometry bit already prepared. Explicit methods acknowledge pre-staged spatial collider, Box3D, and navmesh owners; activation requires an external host callback to confirm successful owner-wide changeover before making the new visual GeometryScene active. Previous visual owner stays retained until explicit GPU-idle collection. The transaction's `draw(renderer)` routes to ONE active GeometryScene; no duplicate submit route is created.

## Tests actually executed
- **PASS, isolated policy test**: A self-contained reproduction of `CaveOwnerBarrier`/unit assertions was compiled with `c++ -std=c++20 -Wall -Wextra -Werror` inside `/mnt/data/caverns-stage25-check` and executed; stdout: `caverns stage25 owner barrier PASS`. The original GitHub test later added an extra composite-mask case, and the associated source update was not rerun locally. The test proves only the revision/status bookkeeping, not GPU staging or actual physics.
- GitHub connector confirmed the source, test, skill writes and returned commit IDs.
- **NOT RUN**: full AIVERSE native C++ compilation, CMake/CTest, actual `CaveEditedSceneTransaction` linked runtime, GeometryScene/STRATA GPU upload or hardware capture, Box3D/SpatialWorld/navmesh synchronized publication, user-interface Scene integration, GPU idle/fence failure injection. Do not claim these passed.
- The local workspace is not a complete AIVERSE checkout, so isolated policy validation is NOT evidence of full engine compatibility.

## Limitations and critical safety
1. `CaveEditedSceneTransaction` is a real GeometryScene staging path but NOT itself a complete graphics+collision atomic implementation. External callbacks must prepare exact matching revisions on all owner sides, and the host must ensure all-or-none visibility. Returning true from a no-op callback would incorrectly mark success.
2. Existing Box3D publisher destroys its previous body on publish; cannot be used as a staging callback. Navigation mutation and SpatialWorld replacement likewise require staged shadow state and explicit commit barriers, failure recovery and rollback-safe handles.
3. STRATA GPU retirement sidecars still require a real idle barrier. Retained scene owners must not be destroyed during outstanding graphics submissions. This stage does not introduce nonblocking sidecar fence management.
4. Stage23 native Editor still performs CPU preparation only; it does not own, `draw`, or activate `CaveEditedSceneTransaction`. Existing UI truthfully says visual/physics commit is not connected.
5. Stage16 seam certification must pass on every loaded neighbor; multi-LOD fallback workers remain incomplete. A transition found by Stage14 does not justify drawing an unstaged modified mesh.
6. No changes to main; no merge without native compilation and QA.

## Concrete next steps
1. Introduce reusable prepare/commit/abort leases in actual Box3D/SpatialWorld and native NavMesh owners without mutating visible game state before the coordinated barrier.
2. Implement a graphics-owner integration in `Editor::renderScene` that draws only the committed `CaveEditedSceneTransaction`, retaining old scene on failed preparation, and validate no duplicate Scene draws.
3. Wire the real Stage23 `CaveEditRuntimeBridge::publishReady` callback to the full owner transaction and GPU lifetime fences. Do not acknowledge the dirty queue merely upon successful CPU staging.
4. Register, compile and run all Caverns stages 1–25 in full CMake/CTest; test the editor and two graphics backends including GPU-staging failures, delete/Undo, and cave navigation after edit.
5. Update `skills/world/aiverse-terraweave-caverns/SKILL.md` and write separate dated handoff after every next implementation stage.
