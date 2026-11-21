# AIVERSE LENS Stage 34 — staged Roads/Shores GPU kits and idle-retry ownership (2026-11-20)

## Repository, branch and exact commits
- Repo `chromealex/AIVERSE`; branch `feature/lens-motionweave-integration`; `main` unchanged.
- `ade3d4a9d8878bbe399e0c47aec2f02d5eb2c189`: `modules/rts/view/RtsRoadView.h`, runtime-state handoff API and `buildingKit_` partial GPU texture owner.
- `e3c9ef452d0b7b5ef6573c3b2c3bd79631daf1cd`: `modules/rts/view/RtsShoreView.h`, corresponding water-network handoff and `buildingKit_`.
- `bc62aa10704f997902130faf74bb8046a52187de`: `modules/rts/view/RtsRoadView.cpp`, retain partial GPU kit textures through failed init, fail on essential road kit or paint atlas allocation, carry map/weather/ghost runtime configuration, idle-gated cleanup of incomplete kit.
- `d33afd339ef772688674474c9d2c7f32705d7e0d`: `modules/rts/view/RtsShoreView.cpp`, retain partial GPU kit textures until safe shutdown and carry serialized water-network configuration into staged replacement.
- `118a2131dc013abe54c3df5b7f92010e787652d3`: `modules/rts/view/RtsView.h`, store `pendingRoadRecovery_` and `pendingShoreRecovery_` if staged cleanup is denied GPU idle.
- `adaffeb87db6d2af3f5ed0b24677e95b01ef8a68`: `modules/rts/view/RtsView.cpp`, stage both Road and Shore GPU owners before replacing either live owner, clean up failed new owners or retain them for retry, drain pending recovery at subsequent init/final shutdown.
- `66cf1ff05a4f4c75276f61c06c2e07317802e41a`: `tests/tests.cpp`, road/shore partial kit texture allocation and refused idle test scenarios plus CPU runtime configuration handoff.
- `e27eecbbf1a9d03bea7f1eec8824399d0f4e6648`: `modules/rts/view/RtsRoadView.cpp`, preserve manually supplied networks when no JSON map exists, clear stale map metadata when the map removes roads.
- `790f155b786f4b0a3a485c4bd27bf2dd292d742d`: `modules/rts/view/RtsView.cpp`, include failed staging owners in retirement totals and pre-render retirement polling.
- `c252774847d60142caf6bc0749f82c12b65a27c1`: `tests/tests.cpp`, extend CPU handoff regression to a directly supplied `RoadNetwork` topology.
- Previous: `docs/agent-handoffs/lens-motionweave-integration-stage33-overlays-gpu-transaction-2026-11-20.md`.

## Architecture and API
- Each Road/Shore initializer now owns its progressively uploaded GPU kit in `buildingKit_` before publishing `images_`. If an allocation throws or a required texture fails, that owner remains reachable. `shutdown(dev)`, after `retirement_.completeForShutdown(dev)`, releases the partial kit via `releaseModel(dev,*buildingKit_)`, exactly as for other owned kit images.
- `RtsRoadView::inheritRuntimeStateForReload` copies only CPU state (serialized road network or direct network, replacements, weather and Ghost). It never clones GPU handles, chunks, derived surfaces, decal caches or retirement queues. `RtsShoreView::inheritRuntimeStateForReload` copies the water network descriptor/CPU network, invalidating derived geometry.
- `RtsView::init` constructs separate `nextRoad` and `nextShore`, copies the old CPU runtime configuration, then uploads both kits. Only after both new kits initialize does it shutdown both old owners and swap the unique_ptrs. If either upload throws, the old Road/Shore owners remain on the host; the new owners are shutdown. If shutdown refuses GPU idle, ownership is transferred to persistent `pendingRoadRecovery_` / `pendingShoreRecovery_` for later retry.
- Before the next init or final host teardown, pending owners are retried against the same GPU device; on failure the exception preserves ownership. Their Model retirement queues are polled at the existing pre-render `RtsView::collectRetiredModels` boundary and added to `pendingModelRetirements()` diagnostics.
- `RtsRoadView::init` now leaves directly supplied `setNetwork` topology intact when no `netJson_` exists, instead of reparsing null into an empty network.
- This stage is specific to the RTS presentation adapter. Generic camera, ECS, MotionWeave and serialization APIs remain unchanged.

## Authored regressions (NOT executed)
- `road_view_partial_gpu_kit_retains_ownership_until_idle_recovery`: mock texture #2 fails after #1 is uploaded; shutdown with idle refusal retains owner and GPU handles; retry after idle success releases textures.
- `shore_view_partial_gpu_kit_retains_ownership_until_idle_recovery`: same scenario for water shore kit.
- `road_and_shore_staged_reload_preserves_cpu_runtime_settings`: road weather and directly supplied road network topology survive CPU handoff. Shore network state is copied.
- Added in existing `tests/tests.cpp` using `fogfaulttest::Device` (CPU-only recording RHI). No CMake target changes.

## Actual checks and evidence
- Inspected latest `RtsRoadView.h/.cpp`, `RtsShoreView.h/.cpp`, `RtsView.h/.cpp`, `Model.cpp::addImage/releaseModel` and existing tests.
- Committed the edits via GitHub and re-fetched all seven changed files: confirmed staged unique_ptr swap, kit recovery owners, idle-gated shutdown, pending-retirement collection and test functions are present.
- **NOT RUN**: native CMake configure, C++ compiler, CTest, Metal/Vulkan/GLES backend compilation or device-based GPU tests, pixel parity, benchmarks. No claim of passing native tests.

## Limits / risk register
1. The **Road/Shore pair** is staged against its own prior GPU owners, but `RtsView::init` already publishes sprites, fog-look and overlays before reaching Road/Shore staging. Therefore the **entire** view reload is NOT atomic on a Road/Shore error.
2. After staging succeeds, old Road and Shore `shutdown` calls may still fail independently due to transient device loss. A successful first old-owner shutdown followed by a failed second one can still leave a partially committed full view. The error path retains the newly built staging owners, but does not resurrect a destroyed old GPU kit. Stage changes should not be considered device-loss atomic until a shared multi-owner publish/retire transaction exists.
3. Unpublished GPU textures created before an exception from deep backend upload routines must be tracked by the Model `addImage` contract. `addImage` reserves its vector before upload, but exceptions between raw GPU creation and handle publication need native RHI audit.
4. Newly initialized Road kits require road textures and generated paint/verge atlases, while Shore kits require textures for represented materials; allocation failure now surfaces as a reload exception instead of silently substituting an invalid texture. Check GLES lower-memory fallbacks during final device testing.
5. Staging the complete Road and Shore kits alongside resident old assets temporarily increases GPU memory. No new shared memory budget has been introduced. On hot-reload-heavy projects, profile peak use and add bounded staging budgets/retirement if needed.
6. `RtsRoadView::inheritRuntimeStateForReload` preserves weather and ghost data; derived road surface geometry rebuilds lazily when drawn. Validate map editing and direct-network helpers on actual runtime.

## Next concrete steps
1. First inspect `TerrainRenderer` and `RtsGroundFx` GPU lifetime, staging dependencies, and immutable runtime configurations. Move their kit/resource preparation into an explicit safe transaction without assuming `NativePresentationRetirement` can be moved or copied.
2. Add an integration-level Road/Shore reload fault test exercising `RtsView::init` with an old visible map and injecting a failure only in the incoming Road or Shore kit. Assert the old Road/Shore presentation owners remain valid. Keep GPU idle refusals distinct.
3. Audit `RtsView::init` cross-owner ordering and stage a shared commit phase to avoid CPU/GPU view state mismatches if the second old-owner shutdown fails.
4. Reconcile newest `feature/universal-ecs-animation-pipeline` through merge base/per-file conflict resolution and document integration state.
5. After remaining features and reconciliation, do the user-requested single consolidated native build/CTest (including all Stage 20–34 additions) and Metal/Vulkan/GLES compile/runtime evidence before main merge.
