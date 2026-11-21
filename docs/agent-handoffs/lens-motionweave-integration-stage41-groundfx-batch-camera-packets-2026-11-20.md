# AIVERSE LENS Stage 41 — transactional GroundFX mesh batches and immutable camera draw packets (2026-11-20)

## Repository / branch / exact commits
- Repository: chromealex/AIVERSE
- Branch: feature/lens-motionweave-integration; main NOT changed.
- Parent Stage 40 handoff: docs/agent-handoffs/lens-motionweave-integration-stage40-incremental-detail-2026-11-20.md
- aa9953daaeed8d7b767706d064b9e203358fec28 — create generic engine/render/NativeMeshBatch.h.
- d497543fb0ff2321af62fe1849e1f5d9535d1ce7 — count vertex and index staging bytes.
- aa70007aa01ee42770cf87f4e49e0bf98777a9f1 — add ephemeral batch pointer to RtsGroundFx.h.
- 0c40215afd24a8e09cf37ee3a3ace63d6a3e13e7 — route GroundFX setMesh uploads to batch when active.
- bda7ff616d1b2df5d024db4ba02da82280335e3e — render-owner batch transaction across lanes and visible dirty chunks.
- bbcc519f3fd51aae6c435d92fbde60b406ec93d5 — queued GroundFX custom draw uses immutable packets instead of reading live mutable chunks_ and visual look state.
- cf350b4be9daf5087c025ecc95727fe5c7ad4178 — three generic GPU batch fault-injection tests.
- 82b0d0de9d7b4813c64b041e34d57327dc5b2b76 — enforce batch budget when the lane mesh consumes the temporary budget.
- bd9de2566cf515fb2acc56da503666f0478828db — GroundFX integration regression test for a late second-road upload failure.

## API and architecture
1. Generic render::NativeMeshBatch(gfx::Device&, NativePresentationRetirement&) in engine/render/NativeMeshBatch.h is independent of RTS, FPS, or any game genre. stage(Mesh& published, const MeshBuilder&) uploads a new mesh into an unpublished replacement without mutating the old published mesh. Multiple stage() calls for one target coalesce, freeing the earlier private replacement. stagedMeshes() and stagedBytes() report pending work.
2. publish() retires all old published VB/IB handles into the existing ordered GPU-completion queue and swaps in all replacements. It does not require an idle barrier on every ordinary update. On any exception the batch destructor discards only privately uploaded GPU handles. A published batch does not destroy its new GPU resources.
3. RtsGroundFx::draw now creates one stack batch for lane updates and the visible dirty chunk layer updates (territory, dig, marks, road, concrete). setMesh becomes a private staged operation inside this transaction. Dirty-layer flags and laneSig_/laneCount_ are snapshotted and restored on failure, preserving a retry path with the previously published GPU meshes.
4. To control upload spikes, a primary frame stages at most 16 visible dirty chunks, or stops when already prepared mesh buffers reach 128 MiB. Remaining chunks stay dirty for later primary views. One exceptionally large chunk can exceed the batch budget; this is a scheduling threshold rather than a strict allocator limit.
5. A full batch commits only after the last selected chunk has uploaded successfully. It is atomic PER SELECTED BATCH, not across all dirty chunks of an arbitrarily large world: additional batches may publish on subsequent frames. Resetting the entire map (RtsGroundFx::resetGrid) still follows the previous retirement path and is outside this per-frame transaction.
6. GroundFX secondary camera drawCustom now captures an immutable vector of render::Mesh GPU handles, copied GU0 per-draw uniforms, resolved texture handles and the pipeline handle. The render callback does not access mutable RtsGroundFx::chunks_, visible_, time_, lane/road/territory looks or TextureCache. Built-in cobble/dirt noise texture resolution is moved to packet construction before command recording.
7. Other opaque dig rock/gem geometry uses Renderer3D::drawMesh by-value presentation packets. Existing NativePresentationRetirement protects old submitted VB/IBs while prior primary/RT camera draws finish. The generic MeshBatch mechanism has no genre dependencies; GroundFX remains an RTS presentation adapter, not a camera architecture dependency.

## Tests authored, NOT run
- native_mesh_batch_second_upload_failure_preserves_all_published_handles:
  Stage first mesh; fail index allocation of second after its vertex buffer; expect no published mesh changes, no retired GPU resources, and exactly three never-submitted staged buffers destroyed after rollback.
- native_mesh_batch_success_retires_old_buffers_only_on_publish:
  Stage two; old buffers unchanged until publish; after publish four old buffers are queued, and released only on retirement completion; new buffers remain owned by the caller.
- native_mesh_batch_same_target_replacement_is_coalesced:
  Two successive stages of one target do not retire the original early; the first unpublished staging pair is freed and the final replacement publishes once.
- groundfx_visible_road_chunks_commit_together_or_rollback_on_late_upload_failure:
  Construct a real RtsGroundFx+RtsView+Renderer3D with fake RHI, place roads in two separate visible chunks, render initial view, edit both chunks and fail the fourth GPU-buffer creation (late index upload). Require no published mesh retirement and three unpublished buffer destructions, retry and expect at least four deferred old road handles. This assertion depends on camera/frustum visibility and must be verified on the actual native test run.
- Tests share the existing fogfaulttest::Device fake RHI; no real Metal GPU parity is claimed.

## Actual validation
- Read RtsGroundFx.cpp/.h, RtsView.h, Renderer3D.h, MeshBuilder.cpp and current fake device/tests in GitHub.
- Created/refetched generic mesh transaction, GroundFX draw and test changes; reviewed the packet capture and publication order.
- NOT RUN: CMake configure, compiler, any aiverse_tests, CTest, real Metal/Vulkan/GLES builds, camera GPU rendering, pixel parity, memory profiling.
- Compilation and all test expectations must be validated in the user's planned ONE consolidated native test pass, after remaining functions are complete.

## Known limitations and next actions
1. GroundFX full reset/reload, dynamic fog/territory memory scanning and territoryBlend_ CPU animation are outside the current mesh batch. For full-view epoch atomicity, stage all owners (Terrain, GroundFX, Road/Shore/Fog/Sprites) and publish at one pre-render boundary.
2. Batch memory limit is advisory: limit checked after selected chunks, and one large chunk may exceed it. For hard global limits use resumable multi-frame transactions, per-layer streaming, suballocation or fixed-size RHI pools with backpressure. A world with more than 16 dirty chunks updates in multiple visible batches.
3. The integrated GroundFX two-chunk test has not been compiled or executed; use the renderer fake to verify frustum coverage and exact expected created/destroyed buffers and adjust the fixture if necessary. In particular failure injection must be after two distinct *visible* road chunks start rebuilding.
4. Earlier Stage 37–40 functionality has not yet been compiled/tested either. Fix C++ compile errors, GPU fake expectations and backend parity problems during consolidated validation; do not claim fully functional or merge to main prematurely.
5. Camera snapshots copy GPU handles and uniforms but do not themselves extend device lifetime. RHI owner and retirement queue must remain alive until queued commands and GPU work are complete; device-generation lease must be respected.
6. Generated cobble/dirt textures are cached by TextureCache and are now resolved before command submission; native texture hot-reload and resource residency need separate audit.
7. MotionWeave changes from feature/universal-ecs-animation-pipeline still require a conflict-aware merge into the LENS branch without overwriting animation/editor RtsView refactors.

## Suggested Stage 42
Create a RtsView-wide immutable frame/publication epoch spanning Terrain, GroundFX, visibility, roads/shores and sprite/animation streams. Add a common pre-render commit/rollback boundary, pass immutable frame data to secondary cameras, and test failure of the final staged owner without replacing earlier live owners. After feature completion, execute the requested one consolidated native CMake/CTest and supported Metal/Vulkan/GLES compilation, fix failures, and only then consider merging to main. Preserve a handoff after every stage.
