# AIVERSE MotionWeave / Universal View — Stage 136 handoff

Date: 2026-11-19
Branch: `feature/universal-ecs-animation-pipeline`
PR: https://github.com/chromealex/AIVERSE/pull/22 (OPEN / DRAFT)
Previous: [Stage 135](animation-stage135-model-ownership-visitors-2026-11-19.md)

## Source and branch topology

- Continued Stage 135 HEAD `314dbcd75d4e3f538ae7303981f55bdfb9bd54f1`.
- Stage 136 code/test commits, ordered:
  1. `b00ac85132f0d6ad1a120b66f8c6d68b0e80a0ed` — Model cache prepared upload rollback + retirement-observer guard.
  2. `b0d0eb96cf0066faad7de8127065fee4bb1b8d96` — preserve unique Model ownership on queue allocation failure.
  3. `13121d078ee1f81a63107524783a8b4efa6ecb1d` — RTS async preparation warning / progress after failed publish.
  4. `b32dd5e984dc69413ff2174706bf00ffed241ce5` — CPU-only regression fixtures.
- Last readback code HEAD `b32dd5e984dc69413ff2174706bf00ffed241ce5`.
- Comparing to latest GitHub main: feature ahead **1014** / behind **305**, `main` HEAD `58e29ab052458fffcdebebffe27022b53adb9367`. Both sides have unique commits.
- Comparing to PR #22 base `feature/universal-entity-inspector-fps`: feature ahead **1384** / behind **615**. Both histories diverged.
- No main/PR-base/other agent ref edits, no rebase, force push, merge, retarget, or cherry-pick. Keep PR **Draft**.

## Problem 1 — worker-prepared Model upload could publish invalid GPU geometry

Previously `ModelResourceCache::publishPrepared` called `render::uploadModel(dev,*prepared.model)` and ignored its boolean result. `uploadModel` can return false after a partially successful GPU allocation (for example, vertex buffer created but index buffer missing); an invalid resident Model would then be published, and its GPU handles had no explicit rollback. An exception from GPU upload or `models_.try_emplace` could also let a local `unique_ptr<Model>` destroy GPU-live storage without the host retirement queue.

**Fix** — `engine/render/ModelResourceCache.h`:
- If a CPU worker key already became resident, short-circuit with `false` before GPU upload (same duplicate behavior as before).
- Check `uploadModel` boolean. On `false`, count `preparedUploadFailures()`, transfer the still-owned scratch Model to `retireUnpublished(dev,prepared.model)`, return `false`, and never publish the cache entry.
- On an exception from upload or insertion, count the failure, safely retire the scratch Model if still present, then rethrow for host reporting. In normal queue operation `retireUnpublished` uses ordered GPU retirement; its existing exception/no-token fallback requires proven `waitForResourceIdle` and otherwise preserves safety by orphaning potentially GPU-live storage.
- A null prepared Model retains the previous failed-load/tombstone publication semantics; GPU-capable worker models remain owned by the cache only after successful upload and insertion.
- Replaced three `retired_.enqueue(std::move(it->second))` sites (replace, trim, source invalidation) with `retired_.enqueueFrom(it->second)`. The latter allocates the vector slot *before* moving the `unique_ptr`; an allocation exception no longer destroys the Model as a by-value `enqueue` parameter.

## Problem 2 — synchronous retirement diagnostic callbacks could recurse into the cache

`AsyncResourceRetirementQueue::emit` invokes a host-supplied observer synchronously during `enqueueFrom`, `collect`, or shutdown `drain`. Stage 135 protected a Model-ownership visitor, but normal non-visitor retirement could still call the observer while a `std::map` iterator or retirement queue iterator was live.

**Fix** — `engine/render/ModelResourceCache.h`:
- Reuse the existing nestable `ModelDetachCallbackScope` for `replace`, `trimUnused`, `invalidateSources` retirement enqueues, `retireUnpublished` append, `collectRetired` and shutdown drain.
- Observers may inspect immutable Model/retirement statistics on their established owner thread; recursive `insertIfAbsent`, `publishPrepared`, `replace`, `trimUnused`, `invalidateSources`, `retireUnpublished` queue append and `collectRetired` are prevented from mutating iterated containers. The `retireUnpublished` noexcept fallback remains explicit-GPU-idle or fail-closed, never silent immediate GPU release.
- Does not change `AsyncResourceRetirementQueue` itself or RHI Device fence semantics.

## Host integration

- `modules/rts/view/RtsLook.cpp` `RtsView::prepareType` now handles `publishPrepared=false` for a missing cache key by logging a warning (without warning on expected duplicate arrivals). Caught upload/publish `std::exception` is logged and the worker-preparation cursor still advances rather than repeatedly blocking on the same failed source.
- The host may subsequently use its existing synchronous fallback, but this stage does **not** claim a full retry policy, automatic backoff, or ready native rendering.

## Test source added (authored, NOT yet run)

Extended existing `tests/render/device_epoch.cpp` target `aiverse_device_epoch_tests`, CTest `render_device_epoch`:
- `verifyPreparedModelUploadRollback`: empty Model publication, duplicate-key acceptance without upload, failed geometry buffer allocation not entering resident map, exception from injected buffer allocation, failure counter, no lost scratch ownership, GPU-idle-proven shutdown drain.
- `verifyRetirementDiagnosticObserverIsolation`: Enqueued and RecoveredAfterIdle observer events, read-only cache inspection during callback, rejection of recursive publication and retirement collection, preservation of original model cache and successful later GPU retirement on CPU StubDevice.
- Existing Stage 132–135 lease/owner/visitor/death tests remain registered. The fake GPU stub's `waitForResourceIdle()` always returns true; this does **not** test a real partial GPU buffer/texture handle, asynchronously pending native GPU work or a failed real fence.

## Actual work vs deferred validation

**ACTUAL:** connected GitHub source review of `ModelResourceCache.h`, `Model.cpp`, `Model.h`, `AsyncResourceRetirementQueue.h`, `ModelPreparationService.h`, `RtsLook.cpp`, `device_epoch.cpp`, Stage 135 handoff and branch metadata; exact current-blob-SHA edits/commits; readback of updated code and test source; main/base comparisons and recent GitHub Actions status lookup; attempted local git access, which failed DNS.

**NOT RUN:** local AIVERSE CMake configure/compile/link, CTest, full CI green for Stage 136 HEAD, Metal/Vulkan/OpenGL, FPS/RTS/editor visuals, ASan/TSan/UBSan, real failed-GPU-fence injection, real partial GPU allocation or benchmark. Last queried Actions runs for this branch were queued/cancelled on *older* SHAs. Do not claim this code passes or is release-ready.

## Compatibility, limitations and safety

- `preparedUploadFailures()` counts reported upload false and thrown upload/admission attempts (not expected duplicate-key short-circuits). Do not use as an exact `gfx::Device` allocation-failure telemetry counter.
- Current `Model::uploadModel` returns bool for missing indexed geometry; success/failure of shared texture uploads and morph targets are not comprehensively validated by that boolean. More exhaustive device failure checks belong in a coordinated Model/Gfx change with renderer/texture agents — do not modify their live branches blindly.
- The failed Model may hold a shared texture lease and/or dedicated image; only the existing explicit GPU retirement discipline authorizes resource destruction.
- Cache diagnosis callbacks are read-only with respect to the cache during queue mutation. `retireUnpublished` from such a callback may enter an expensive explicit GPU-idle fallback. Native hosts should defer such operations until callback completion.
- Existing `retireUnpublished` fallback may intentionally orphan GPU-live storage if GPU completion cannot be proven; avoiding use-after-free takes priority over memory reclamation.
- No MotionWeave clock, animation ECS gameplay boundary, source generation/LOD schema, networking/saves, or other genre contract changed.

## Commands for native macOS host

```bash
git fetch origin main feature/universal-ecs-animation-pipeline feature/universal-entity-inspector-fps
git switch feature/universal-ecs-animation-pipeline
git pull --ff-only origin feature/universal-ecs-animation-pipeline

git rev-list --left-right --count origin/main...HEAD
git rev-list --left-right --count origin/feature/universal-entity-inspector-fps...HEAD

cmake -S . -B build/animation-validation -DAIVERSE_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/animation-validation --target aiverse_device_epoch_tests -j 8
ctest --test-dir build/animation-validation --output-on-failure -R '^render_device_epoch$'

cmake --build build/animation-validation -j 8
ctest --test-dir build/animation-validation --output-on-failure
```

Record real compiler diagnostics and CTest logs. Run TSAN/ASAN on a supported native compiler; validate genuine Metal/FPS/RTS/editor animation, Model hot reload, LOD, GPU token failure and crowd benchmarks before merge.

## Next Stage 137

1. Run native `aiverse_device_epoch_tests`; fix compile/runtime/test failures introduced in Stages 132–136, especially `prepareModelUpload` geometry-failure fixture and POSIX death children.
2. Coordinate with renderer/texture owners to strengthen `uploadModel` result for failed image/morph GPU textures and test *partially allocated valid GPU handles* on a real Device. Avoid silently publishing incomplete graphics.
3. Validate worker-prepared result and host retry/logging under real FPS/RTS/editor session and explicitly schedule any retry/backoff policy on owner thread.
4. Integrate main-only 305 commits and PR-base 615 commits on separate disposable integration branch/worktree after review and native validation. Do not rebase/force push shared animation branch.
5. Record exact commit, changed files, actual test outputs, limitations and next action in a separate Stage 137 handoff; update agent docs. PR #22 must stay Draft until verified.
