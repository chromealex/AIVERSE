# AIVERSE MotionWeave / Universal View — Stage 130 handoff

Date: 2026-11-19
Branch: `feature/universal-ecs-animation-pipeline`
PR: https://github.com/chromealex/AIVERSE/pull/22 (Open / **Draft**)
Previous: [Stage 129](animation-stage129-model-cache-read-leases-2026-11-19.md)

## Repository and branch ancestry

- Continued Stage 129 HEAD `fad0d4ab47b31b941a074a96b42f24a67a9de1d6`.
- At Stage 130 start, `main` HEAD `d71c6d47e532b25fb816a8bb1f96c41e5275a165` was already in the feature history; compare ahead **961** / behind **0**.
- Feature-to-PR-base comparison (PR base `feature/universal-entity-inspector-fps`) was **ahead 1331 / behind 615** at start: these branches have DIVERGED. **Do not assume PR #22 is ready to merge as-is.** Re-evaluate its base and branch dependencies before changing PR target or merging; never force push.
- After Stage 130 changes, ahead of main at least 968 / behind 0 (final exact count should be rechecked).
- No other feature branches or `main` were modified, and neither PHYSIS, NEREID, TerraWeave, Prisma, SpectraFX, batching nor texture/RHI code was intentionally changed.

## Source code actually changed — exact commits

1. `116ee75bb87c55cb8b914821ae80e44896fa8960` —
   `engine/render/ModelResourceCache.h`: reset the read-state `ownerThread` at the END of a successful `ModelResourceCache::shutdown`, after the no-readers check, GPU full-idle barrier, GPU resource release and cache cleanup. The owner may then rebind a fresh `gfx::Device` on another render thread. Previously the old read owner thread survived shutdown, incorrectly forbidding legal post-shutdown reinitialization. Does not reset a live lease.
2. `77ea39ed7b5467cc86fd872cf6d91a237aa1e682` —
   `engine/render/ModelReadLease.h`: both `ModelReadLease::reset()` and `ModelCacheReadScope::reset()` now reject foreign-thread release by printing a diagnostic and calling `std::terminate()` (destructors must remain noexcept). A foreign thread must NOT decrement a reader count while it may still be using a borrowed Model: otherwise cache eviction could race with that read. Move-only tokens remain owner-thread scoped. This is intentional FAIL-CLOSED behavior; incorrect cross-thread destruction is a fatal host contract error, NOT normal flow control.
3. `07f3fcadfdb4ce8250127ae3dc6dca010ab78c41` —
   `engine/render/ModelReadLease.h`: `ModelReadLeaseState` now exposes cumulative atomic `peakReaders` and `blockedMutations`. Both narrow and coarse lease acquisition record the maximum simultaneous reader count. This is CPU lifetime diagnostics only; no GPU fence, RHI resources or graph update changes.
4. `212f51f16aee589eb10b533ff6ffdd0e3f16a66c` —
   `ModelResourceCache.h`: `modelReadSafetyStats()` returns `ModelReadSafetyStats { activeReaders, peakReaders, blockedMutations }`. Every failed `requireNoModelReaders` increments `blockedMutations` BEFORE throwing `logic_error`. Counts all mutation attempts that are prohibited while leases are active: `invalidateSources`, `replace`, `trimUnused`, `shutdown`; do not interpret the count as successfully completed evictions or unique Model objects.
5. `125ff30330c6a4d020394bab3298bed5b9e57ac6` —
   `modules/rts/view/RtsView.h`: forwards Model cache reader diagnostics to RTS/editor via `modelReadSafetyStats()`, without affecting the engine-level View or simulation ECS.
6. `ab343fa48d639ddfafef6aaca84af3451c980ea3` and
   `ccb8f54ee3a9d1f312606a806855f9b273f7488c` —
   `apps/editor/windows/StatsWindow.cpp`: the Editor reports active/peak Model read guards and cumulative denied cache mutations, with terminology that covers replacement, invalidation, trim and shutdown, not just eviction.

## Architecture and native API

```cpp
auto scope = cache.acquireFrameReadScope();
// Every cache-owned Model used by graph evaluation or synchronous
// Renderer3D draws remains uniquely owned and cannot be evicted
// while the scope is live.
view.update(objects, dt);                  // one MotionWeave update
view.submit(renderer, objects);            // synchronous submission
scope.reset();                             // on cache render-owner thread
auto counters = cache.modelReadSafetyStats();
assert(counters.activeReaders == 0);
// Only now: cache.invalidateSources(...), cache.replace(...),
// cache.trimUnused(...), cache.shutdown(...).
```

The cache still owns `std::map<std::string,std::unique_ptr<Model>>`. The lease counter is **not** a cross-thread read/write lock. Acquire, read and release on its owner/render thread. Separate animation worker jobs may access borrowed Models ONLY while the owner holds the guard and joins them before release. Never release the token on a worker thread. Any host that changes Model bytes in place, bypasses the cache, or changes GPU resource lifetimes outside existing fence owners must coordinate independently.

The cache's `RenderDeviceLifetime::Lease` remains separate from this reader guard. The former checks device owner thread/generation; the latter prevents CPU Model eviction while reading; neither proves GPU work completion. Existing `AsyncResourceRetirementQueue` continues to own the GPU retirement protocol.

## Actual validation and unverified risks

- ACTUALLY PERFORMED: connected GitHub PR and main/PR-base comparisons; read Stage 129 handoff and current cache/lease/RTS/editor source; applied precise current-blob-SHA source updates and read them back.
- NOT RUN: native checkout/build/C++ compiler/linker/CMake, CTest/CI, real GPU Metal/Vulkan/GL, FPS/RTS/editor visual parity, GPU fence failure injection, ownership/rebind and thread-exit tests, ASan/TSan/UBSan or frame-time benchmarks. Source is UNVERIFIED FOR COMPILATION OR RUNTIME.
- Destruction of a live ModelReadLease on a different thread, and cache destruction while readers remain, deliberately terminate the process. Test these cases in isolated/subprocess fixture (never inside the primary Test runner).
- Counters are cumulative for the current ModelResourceCache instance and atomic. No automatic reset is performed at device shutdown; a new owner can inspect historical blocked attempts and peak readers.
- `RtsView::draw` still holds a coarse guard after its upfront synchronous type load. Callbacks performing eviction during the guard now get exceptions: validate the full streaming and editor pipeline during GPU parity tests.
- The PR base `feature/universal-entity-inspector-fps` is divergent from this feature branch: investigate and resolve with proper ownership before merging. Being 0 behind `main` is NOT sufficient proof of readiness.

## Exact next Stage 131

1. Recheck `main`, animation feature HEAD, PR #22 base, and peer agent branches. Avoid overwriting parallel systems or force pushes. Repair any read-guard late-streaming load boundary with an explicit narrow phase split while maintaining the single MotionWeave clock.
2. Add actual CPU contract tests (including cache read acquisition, blocked invalidation/replacement/trim, post-shutdown thread rebind, and move-only lease semantics) and test read/thread misuse in a process-isolated fixture.
3. Build with the current native Mac/Metal and other available CMake targets; run CTest, address all compilation/warnings and relevant game/editor visual parity, add GPU failure-injection and performance checks. DO NOT claim tests passed unless results are observed.
4. When all features and validation are ready, review the **divergent PR base** before merge. Keep PR #22 Draft until the reviewer agrees to a safe integration path.
5. Create standalone Stage 131 handoff and update AGENTS.md with actual tests, warnings, current code HEAD and next steps.
