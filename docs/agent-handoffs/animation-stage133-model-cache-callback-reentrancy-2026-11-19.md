# AIVERSE MotionWeave / Universal View — Stage 133 handoff

Date: 2026-11-19
Branch: `feature/universal-ecs-animation-pipeline`
PR: https://github.com/chromealex/AIVERSE/pull/22 — keep DRAFT.
Previous: [Stage 132](animation-stage132-thread-owned-streaming-release-tests-2026-11-19.md)

## Branch and concurrent work

- Started from Stage 132 HEAD `39357bacca8818cb1208ca284471f3c7317941ba`.
- Initial GitHub comparison to `main`: branch **ahead 990 / behind 0**.
- After Stage 133 source/test commits, while parallel owners updated `main`, comparison to CURRENT main changed to **ahead 992 / behind 305**, merge base `58e29ab052458fffcdebebffe27022b53adb9367`. This is a NEW divergence, not a successful main sync.
- The PR base `feature/universal-entity-inspector-fps` is STILL divergent: **ahead 1362 / behind 615** from the animation branch's perspective, merge base `c944882193c1404a7e47f409f8d7847c766b4e31`.
- Do not force-push, rebase, auto-merge either base, alter main or touch other agents' PHYSIS/NEREID/TerraWeave/Prisma/SpectraFX/texture/batching/lighting/Vivid work. Review overlap first with an isolated integration worktree and run full validation.

## Problem

`ModelResourceCache::replace`, `trimUnused` and `invalidateSources` expose cache-owned `Model*` in host detach/release callbacks while the caller holds a resident `std::map` iterator or a precomputed set of affected keys. A host callback that recursively inserted, replaced, invalidated or evicted models could invalidate the iterator, silently change the target of the outer eviction, or create a fresh reader lease immediately before the outer operation disposed of a Model.

This is a synchronous owner-thread callback/reentrancy risk, separate from Stage 132's foreign-thread map protection.

## Code changes and commits

1. `da3e4864b8d15490e8ff9909ac2a8b634742df36` — `engine/render/ModelResourceCache.h`:
   - Added small private `ModelDetachCallbackScope` with exception-safe restoration of `detachCallbackActive_`. It is a same-thread reentrancy guard, **not a mutex**.
   - `replace`, `trimUnused`, `invalidateSources` wrap their host release/detach callbacks in the scope.
   - While a detach/release callback runs, `insertIfAbsent`, `publishStagedModels`, `publishPrepared`, `replace`, `trimUnused`, `invalidateSources` and `shutdown` fail closed with `std::logic_error`, before map mutation.
   - Acquiring a new `ModelReadLease` or `ModelCacheReadScope` during these callbacks is also forbidden; an outstanding lease could outlive the callback and otherwise protect a Model about to be retired.
   - After a successful callback, the outer operation rechecks active readers before enqueue/erase, so a future callback implementation cannot bypass the CPU reader barrier.
   - Read-only inspection through `find` and the supplied affected-model packet remains possible on the render-owner thread.
2. `a74e339d547341d8d73107badf0f8f9db4e6e713` — `tests/render/device_epoch.cpp`:
   - Added `verifyDetachCallbackReentrancyContracts` and called it from the existing CTest executable.
   - A `replace` callback may read but cannot insert a new key; the original Model and generation remain intact after rejection.
   - An `invalidateSources` callback cannot publish a staged map; the staged entries, resident Model and generation remain intact after rejection.
   - A `trimUnused` callback cannot create a fresh frame reader guard; the original Model remains resident.
   - Subsequent normal invalidation and staged publication work after callback exceptions, demonstrating RAII flag recovery.
   - Test uses already present `StubDevice`; it does NOT prove any real asynchronous GPU retirement.

No public MotionWeave/AnimationGraph API, simulation ECS, playback clock, physics, graphics backend implementation or serialized model asset format changed.

## Actual verification versus unverified work

**ACTUAL:** fetched connected GitHub source and Stage 132 handoff; inspected Model-cache owner-thread/read-lease contract and CTest source; made two SHA-guarded GitHub commits; fetched both updated file blobs to confirm presence; checked head and latest main/PR base comparisons. One isolated local C++20 experiment confirmed that an inline member can construct a private nested RAII type declared later in the same enclosing C++ class; this does NOT compile or validate AIVERSE.

**NOT RUN:** native AIVERSE CMake or C++ compiler/linker, CTest, successful CI for Stage 133 head, macOS Metal, GL/Vulkan, RTS/FPS/editor visual parity, sanitizers, real GPU fence failure injection, fault-injection on host detach callbacks, throughput benchmarks. A public GitHub clone attempt in this execution environment failed DNS resolution; there is no mounted source worktree.

Recent GitHub Actions run queries showed queued/cancelled older SHAs; **no passing run for the new Stage 133 code** was established. Keep PR Draft. Do not claim successful builds, regressions or measured performance.

## Compatibility and caveats

- The callback contract is intentionally stricter. Any legacy callback that currently attempts to mutate `ModelResourceCache` must instead collect requested changes and apply them **after the enclosing eviction/invalidation returns**. Calls that only inspect Model metadata may still proceed.
- Pre-existing `find`, `contains`, `models`, `stats` etc. are owner-thread APIs, not concurrent readers. The new guard only protects the synchronous mutation callback window and does not authorize worker-thread access to the cache.
- A read lease protects CPU Model lifetime until workers join and synchronous Renderer3D draw returns. GPU resource retirement remains separately fenced on the gfx Device and host.
- The outer `trimUnused` may still have removed earlier entries if a later user callback throws: per-key callback atomicity was improved; **whole-trim transaction atomicity is not promised**.
- The external callback itself may have mutated *other host state* before throwing; the cache cannot roll that back. Affected view/graph references must be detached by the host before successful model retirement.

## Commands on the native Mac

```bash
git fetch origin main feature/universal-ecs-animation-pipeline feature/universal-entity-inspector-fps
git switch feature/universal-ecs-animation-pipeline
git pull --ff-only origin feature/universal-ecs-animation-pipeline

git rev-list --left-right --count origin/main...HEAD
git rev-list --left-right --count origin/feature/universal-entity-inspector-fps...HEAD
git diff --name-only origin/main...HEAD -- engine/render engine/view modules/rts modules/fps

cmake -S . -B build/animation-validation -DAIVERSE_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/animation-validation --target aiverse_device_epoch_tests -j 8
ctest --test-dir build/animation-validation --output-on-failure -R '^render_device_epoch$'
cmake --build build/animation-validation -j 8
ctest --test-dir build/animation-validation --output-on-failure
```

Review merge conflicts and all compiler/warning output. For the already-authored Stage 132 POSIX death fixtures, confirm the child processes abort as expected while the parent CTest reports pass. Then run Metal, ASan/TSan/UBSan, native FPS/RTS/editor parity, and actual fence/streaming/performance checks on supported hosts.

## Next Stage 134

1. Run the Stage 132/133 CTest targets on macOS and fix all compiler/runtime issues, collecting actual logs.
2. Audit host-side detach/release callbacks for previously relied-upon nested mutations. Move any deferred publication/eviction into an owner-thread, post-callback work queue with a clear lifetime contract, if needed.
3. Review `main`'s new 305 commits and the PR base's unrelated divergent 615 commits; resolve ancestry on a disposable integration branch, NOT directly in `main` or via force push.
4. Complete universal View and animation host migrations and verify real-device graphics parity.
5. Append a standalone Stage 134 handoff with commits, actual test output, risk/compatibility and next steps. Keep `AGENTS.md` and the append-only `animation-universal.md` updated after each stage.
