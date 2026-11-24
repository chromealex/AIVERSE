# TerraWeave Caverns — Stage 28 handoff (2026-11-23)

## Provenance
Repository `chromealex/AIVERSE`, feature branch `feature/terraweave-caverns-core-20261123`, **not merged into main**.

Commits:
- `8243d9e705fd220bf5d45969dfa25f5f37e17d85` — `engine/world/TerraWeaveCavernsVolumeCollision.h`: accept fully empty cave mesh as a zero-collider retirement payload.
- `b6f4fbed71cab145af1d53559bb7ecd89461ae14` — `engine/world/TerraWeaveCavernsSpatialShadow.h`: stage/commit an empty new collider range to retire the previous range.
- `700ab42b93c1ecf1f72e33f42bc2b693ed6a77c2` — `engine/world/TerraWeaveCavernsBox3DShadow.h`: stage zero-body retirement.
- `8ba83017e9356fad2f818d4d4a97f9dfe78e6058` — `engine/render/TerraWeaveCavernsGpuSwap.h`: stage a visual scene removal without allocating a dummy GeometryScene.
- `ae92808e122e500b4c91cc597b1e53a416482054` — `engine/render/TerraWeaveCavernsEditedSceneTransaction.h`: use visual removal for an all-empty batch.
- `819bc41d4dab44ddb3c8ce301b6cea951c41be40` — `engine/world/TerraWeaveCavernsVolumePhysicsStage.h`: paired, prepare-only SpatialWorld and Box3D CPU staging from one CaveMesh.
- `f9ccff5f6a4c5ef16ed6217821fd9de2cc723c38` — `tests/terraweave_caverns_empty_tile_test.cpp`.
- `9353a1c356897e160a00b001134e62e78add4817` — `skills/world/aiverse-terraweave-caverns/SKILL.md` update.
- This handoff is committed afterwards; check HEAD for exact final SHA.

## Implemented
- Authored Fill can remove the last visible cave surface. `prepareCaveVolumeCollision` now accepts zero vertices/indices and returns a revisioned zero-collider payload. It refuses a half-empty mesh. It continues strict validation on nonempty meshes, including floor/wall/ceiling collisions.
- `CaveSpatialShadow` can prepare a zero-triangle replacement without reserving a new collider range; when committed between physics operations, it retires the previous active ID range and updates active revision/count to zero.
- `CaveBox3DShadow` can prepare an empty body descriptor, then destroy its previous Box3D body on exclusive commit with no replacement body. A failed destroy leaves staged state, instead of reporting success.
- `CaveGeometrySwapOwner::stageEmpty` stages deletion of the current scene with a monotonically larger render revision. Active GeometryScene stays visible until `activate(hostCommit)` approves it. Then the old GPU owner moves to bounded retired storage; it must be released under `collectAfterGpuIdle` when idle proof exists. All-empty authored batches are handled by `CaveEditedSceneTransaction::prepareVisual`.
- `CaveVolumePhysicsStage` takes the exact original authored CaveMesh plus versioned volume collision and attempts to stage BOTH physics owners. If the second owner refuses, it aborts the first CPU stage. `abort` cancels both uncommitted stages, while `prepared` verifies matching revisions. No live physics mutation occurs during paired preparation.
- New regression source covers nonempty SpatialWorld collision, empty retirement, Box3D descriptor empty stage+abort, paired prepare/abort and invalid half-empty geometry.

## Tests actually executed
- GitHub connector confirmed source/test/skill commits.
- **NOT RUN:** Stage28 native C++ compilation, actual Box3D Scene commit, SpatialWorld linked regression, CMake/CTest, GPU rendering on Metal/Vulkan/GLES, STRATA source selection, native editor interaction or NavMesh gameplay. This execution container has no full AIVERSE checkout, and github.com DNS resolution failed. The Stage28 regression is written, not passed.
- Suggested in full checkout: register `tests/terraweave_caverns_empty_tile_test.cpp` in normal CMake/CTest with the engine's Box3D and SpatialWorld implementation linkage. Run it with sanitizers and include Stage1–27 regression targets.

## Important known limitations
1. **No true four-owner atomicity.** Box3D body creation and destruction mutate a live physics Scene immediately. SpatialWorld performs a separate mutation and NavMesh swaps its own pointer; failing a later owner can leave partially committed state. Stage25 barrier counts readiness, but cannot rollback already changed native systems. Do NOT wire a callback that simply commits them in sequence and returns true as proof of safety.
2. All-empty visual batch is supported, but a mixed batch with SOME empty and SOME nonempty chunks requires deleting the corresponding prior GeometryScene instances while retaining the unrelated resident ones. Stage11 batch is full-scene replacement, and the host must rebuild the complete affected shard snapshot; publishing only edited nonempty chunks can inadvertently remove unrelated instances.
3. GPU retirement remains idle-barrier conservative. The current `GeometryScene` STRATA sidecars may require real `gfx::Device::waitForResourceIdle()`; do not call blocking cleanup inside each paint dab.
4. The new physics payload uses the original unsimplified CaveMesh, not STRATA's presentation LOD. Any cave collider budget overrun must subdivide or decompose the affected chunk rather than silently dropping collision triangles. Empty chunks should have neither cave mesh nor collision; terrain surface and other systems may still exist.
5. Stage27 authored nav source supports Carve/Fill, but Stage28 does not automatically build and swap shadow NavMesh after a Fill. The host must stage a matching snapshot/revision and synchronize navigation readers.
6. No gameplay runtime hookup to the editor's CPU preparer yet; `Editor::cavernsBuildStats.visualCommitConnected` must remain false until a real render/physics commit host is implemented.

## Concrete next steps
1. Implement one frame-safe owner-side volume edit transaction with preflight, globally leased collider IDs, inverse/rollback of all native owners or true inactive Box3D preparation, and a single visibility point between simulation steps and render passes.
2. Implement complete per-shard visual resident manifest (edited + unchanged sources) and zero-instance deletion without affecting unrelated caves.
3. Wire editor CPU `CaveEditRuntimeBridge::publishReady` to this real staged transaction, and provide one NavMesh shadow built from the *same* `CaveVolumeSnapshot`.
4. Add real Box3D and SpatialWorld collision hit tests (all 3 dimensions, negative coordinates, filled removal), NavMesh walkability, camera rendering, plus full AIVERSE compiler/CTest and hardware STRATA acceptance. Do NOT merge into main before evidence.
5. Keep this handoff and the Caverns skill current after every stage.
