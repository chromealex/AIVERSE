# AIVERSE MotionWeave / Universal View — Stage 135 handoff

Date: 2026-11-19
Branch: `feature/universal-ecs-animation-pipeline`
PR: https://github.com/chromealex/AIVERSE/pull/22 — OPEN / DRAFT
Previous: [Stage 134](animation-stage134-owner-thread-read-guards-2026-11-19.md)

## Branch and integration safety

- Started from Stage 134 head `8cf88ea8e6c66d33ac7d8803a4f721c1a359a05d`.
- Source/test commits after that head:
  1. `cc3490e773d89aec585117c87de5aceb70523ec9`
  2. `e9a50db7a597e668450138147b66e4fc2308032f`
  3. `fe43730d74c0aafcce1082049e93be69b491dd58`
  4. `121a70d93dda48288afbf6e11ce1f7f484e0a44c`.
- Latest checked branch code HEAD: `121a70d93dda48288afbf6e11ce1f7f484e0a44c`. Relative to `main`, feature ahead **1006 / behind 305** (diverged, merge base `58e29ab052458fffcdebebffe27022b53adb9367`).
- Relative to PR #22 base `feature/universal-entity-inspector-fps`, feature ahead **1376 / behind 615** (diverged, merge base `c944882193c1404a7e47f409f8d7847c766b4e31`).
- **No main, PR base, other feature branches, merge, rebase, force-push or PR state changes.** Coordinate with batching/renderer/VFX/terrain/water/lighting owners before integrating.

## Problem found: iterator invalidation through read-only ownership visitors

The generic owner accounting path (`RenderResourceOwnerGroup::snapshotOwners`) uses `ModelResourceCache::forEachOwnedModel` to enumerate resident and GPU-retired Model pointers with a user-supplied visitor. Stage 134 validated thread ownership before lookup, but a visitor executed on the *correct* thread could still recursively call cache mutations such as `insertIfAbsent`, `replace`, `trimUnused`, `collectRetired` or `retireUnpublished` during the map/retirement queue iteration, leaving borrowed pointers or container iterators invalid. Independently, retirement statistics/backpressure enumerated non-concurrent internal queues without an owner-thread check.

## Implemented changes

1. `cc3490e773d89aec585117c87de5aceb70523ec9` — `engine/render/ModelResourceCache.h`:
   - `forEachOwnedModel` now holds a scoped render-owner callback guard while visiting both resident Models and pending GPU-retirement Models.
   - Generalized the private `ModelDetachCallbackScope` to a *nestable* callback-depth guard: read-only nested `forEachOwnedModel` and `find` remain legal; mutation / new `ModelReadLease` / new `ModelCacheReadScope` remains rejected at every active depth.
   - `collectRetired` now rejects callback reentrancy before touching retirement batches.
   - `pendingRetirements`, `retirementStats`, `estimatedRetiredGpuBytes` and `publicationBackpressured` now validate established owner thread BEFORE reading the retirement queue. The formerly `noexcept` queue statistics accessors now intentionally throw `std::logic_error` on wrong-thread misuse. `activeModelReaders()` and `modelReadSafetyStats()` remain atomic read-only cross-thread diagnostics.
2. `e9a50db7a597e668450138147b66e4fc2308032f` — `tests/render/device_epoch.cpp`:
   - Added `verifyModelOwnershipVisitorContracts()`: a visitor can recursively enumerate and inspect immutable residency, but attempts to insert/replace/create a new Model reader/collect GPU retirements are rejected. An exception thrown by an inspector must unwind and restore mutation rights at the owner thread.
   - Extended Stage 134 off-thread retirement-stat probes from 10 to 14 preflight rejections.
3. `fe43730d74c0aafcce1082049e93be69b491dd58` — `engine/render/ModelResourceCache.h`:
   - `retireUnpublished` is `noexcept` and cannot throw to the host. It now rejects queue append inside an ownership visitor by entering its existing catch/fallback branch; if GPU idle can be proven, release the unpublished scratch Model synchronously, otherwise abandon the unsafe GPU-live storage instead of freeing it early.
   - `setRetirementDiagnosticObserver` now requires owner thread and rejects callback reentrancy, avoiding mutation of a diagnostic observer while an ownership visitor is active.
4. `121a70d93dda48288afbf6e11ce1f7f484e0a44c` — `tests/render/device_epoch.cpp`:
   - Added rejection of nested retirement-observer mutation and a `StubDevice` fixture that checks the `retireUnpublished` synchronous-idle fallback leaves no new queue entry and no orphaned scratch Model on the successful stub idle path.
   - It currently increments `stagingQueueFailures` for a denied nested enqueue as well as allocator/queue failures; monitor semantics accordingly.

No public animation graph API, genre module type, simulation ECS, network/save format, Model asset format, RHI API, worker scheduling, or GPU completion contract changed.

## Host source audit and limitations

- Reviewed `RenderResourceOwnerGroup.h` and the source-level `forEachOwnedModel` flow. `snapshotOwners` computes read-only resource estimates and does not intentionally mutate the cache.
- Previous Stage 134 audit already found RTS `trimUnused` callbacks only erase `animationCache_`, and `reloadModelSources` rebuilds after invalidation, not recursively inside the Model-cache callback. No host callback migration was required in this stage.
- The guard only covers synchronous `forEachOwnedModel` and detach/release callbacks; it is **not** a mutex or general async reader lock. A borrowed `models()` map reference or Model pointer held past the callback remains host-managed. `textures()` has independent shared texture owner semantics, and direct mutations of a host-owned `Model` outside the cache cannot be blocked here.
- The safe fallback for an illegal `retireUnpublished` call in a visitor can **block on a GPU idle wait** or intentionally orphan GPU-live storage if the device cannot prove completion. Native hosts must defer scratch retirement until after an ownership visitor returns; do not rely on the fallback as normal scheduling.
- Diagnostic observer callbacks from `AsyncResourceRetirementQueue::collect` warrant a separate reentrancy review before production if they call back into cache mutations; this stage specifically protects *Model ownership visitors*.
- Whole-trim transaction atomicity after a later throwing host callback is not promised; Stage 133 protects each callback and stale iterator.

## Actual verification and test status

**ACTUALLY PERFORMED:** connected GitHub source reads for Model cache, retirement queue, owner group and Stage 134 docs; exact SHA-guarded code/test commits; readback confirming callback-depth, retirement and test sources; GitHub main/PR-base ancestry comparisons; recent GitHub CI-run status query.

**NOT RUN:** local AIVERSE CMake configure, native compiler/link, CTest (including `render_device_epoch`), macOS Metal, GL/Vulkan, fps/rts/editor visual checks, real GPU-fence failure injection, sanitizers, performance or CPU/GPU benchmarking. No completed green CI for Stage 135 HEAD was observed; recent results were queued/cancelled on earlier SHAs. The test fixture is *committed, not verified passing*. Keep PR #22 **Draft**.

## Commands for the next Mac agent

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

Then validate Stage 132 POSIX death fixtures; test with ASan/TSan/UBSan on supported toolchains; perform Metal/GL/Vulkan, native RTS/FPS/editor View, graph hot reload/Model LOD, and GPU retirement/fence-failure parity. Record *actual* log results; do not infer pass from source inspection.

## Next Stage 136

1. Run/fix native build and `render_device_epoch` (including newly added callback visitor and retirement fallback scenarios); preserve logs and warnings.
2. Inspect and protect the asynchronous `publishPrepared` Model upload/admission transaction and any diagnostic observer reentrancy around `collectRetired`. Avoid an early release of partially uploaded GPU Models on failures.
3. Review missing `main` 305 commits and PR-base 615 commits on a throwaway integration worktree, especially batching/Model and graphics API changes, and coordinate with other owners.
4. Finish remaining generic View/legacy RtsView integration and validate native FPS/RTS/editor visual parity and performance.
5. Add a separate Stage 136 handoff and append to `docs/agent-handoffs/animation-universal.md`, `AGENTS.md`, `docs/animation-graphs.md`; PR stays Draft until verified.
