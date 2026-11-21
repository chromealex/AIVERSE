# Review fixes: PRISMA + View + MotionWeave + RTS view (2026-11-21)

Branch: `feature/aiv20261121_fix_prisma_view` (worktree
`.worktrees/feature/aiv20261121_fix_prisma_view`), base origin main `973b32970`.
Not pushed, not merged. PRISMA stage note: `docs/handoffs/prisma-stage-96-review-fixes.md`;
animation note appended to `docs/agent-handoffs/animation-universal.md`.

## Findings

| # | Sev | Status | Commit | Files |
|---|---|---|---|---|
| 1 | HIGH | FIXED | 52c7e66 | modules/rts/view/RtsLook.cpp |
| 2 | MED | FIXED | 02802c6 | engine/render/Prisma.{h,cpp} |
| 3 | MED/PERF | FIXED (immediate-publish path); capture-mode repeats DEFERRED | bcde4bf | modules/rts/view/RtsView.{h,cpp} |
| 4 | MED/PERF | FIXED | c0c1331 | engine/view/View.h, tests/view/packets.cpp |
| 5 | LOW | FIXED | 50538fb | tests/view/camera_lod.cpp |
| 6 | LOW/PERF | FIXED | 89f9e2e | modules/rts/view/RtsRoadView.{h,cpp} |
| 7 | LOW | FIXED | 91f3515 | engine/view/View.h, tests/view/view.cpp |
| 8 | LOW/opt | PARTIAL (one dump per frame instead of 2-5) | fc3099e | engine/render/Prisma.{h,cpp} |
| 9 | LOW | FIXED (with #2) | 02802c6 | engine/render/Prisma.cpp |
| 10 | LOW | FIXED | 486e4af | libraries/include/aiverselib/animation/MotionWeaveWorkers.h, tests/animation/motionweave_workers.cpp |

Details:
1. Confirmed by reading `MotionWeaveSourceBridge::sync`: a still-linked source whose Model/Graph
   changed calls `player.instance->snapshotPlayback()` on the old Graph -> old Model. `trimTypes`
   now calls `genericView_->forgetSources()` for genericViewIds of unloaded types before
   `trimUnused`; `loadType(replacing)` calls `forgetSourcesForReimport()` for the type while the old
   Model is still resident (same handshake as reloadModelSources / reloadAppearanceTypes). The
   editor path (`Editor.cpp` -> `trimTypes`) is covered without editor changes.
2. HDR failure is remembered per target size (bounded, 16) instead of a global latch; a new size,
   `setQuality()` change or `init()` retries. `fallbackReason` = "HDR target allocation failed: <lastError>".
3. `stageSurfaceFrame(..., fingerprint)`; `publishSurfaceFrame` stages with `false` (lease is published
   at once and marked `!fingerprinted`, so `matches()` can never accept it). DEFERRED: LENS capture-mode
   `matches()` (prewarm consume + draw-end guard) still runs full fingerprints; MapGrid / RtsScene have no
   revision counters (sim side, outside this area).
4. `View::scheduler_` is a `shared_ptr<SharedScheduler>` shared with `cloneForPreparation()` candidates
   (scheduler holds only threads + per-frame scratch). Atomic `busy` flag: a concurrent second update takes a
   private scheduler instead of re-entering. Additive diagnostic `View::sharesAnimationScheduler()`. RtsView
   unchanged.
5. `AIVERSE_TEST_CHECK` + try/catch main.
6. `PropModel::borrowed` (non-owning) + `get()`; candidate sees the owner's props via `propSource_`
   during `buildUnpublished`; `commitRebuild` moves ownership of borrowed Models; draw skips null.
   Same pattern in `RtsShoreView` (not in this area) -> follow-up.
7. `View::update` builds sources and runs `bridge_.sync` (which preflights the whole batch before World
   mutation) before resetting stats / frameStamp_ / lodState_.
8. `composite()` takes an optional precomputed key; `resolve()` dumps the graph once. No graph revision exists.
9. `hdrModeWithEnv()` shared by `hdrRequested()` and `runtimeCapabilities()` (handles `auto`).
10. thread_local "dispatching pool" marker on caller and worker lanes; nested `parallelFor` on the same
    pool runs inline. `tasks==0` returns before locking.

## Verification actually run
- `g++ -std=c++20 -fsyntax-only -Wall -Wextra -I. -Iengine -Ilibraries/include -Ithird_party [-Ithird_party/imgui]` on
  RtsLook.cpp, Prisma.cpp, RtsView.cpp, RtsAnimate.cpp, RtsRoadView.cpp, RtsRoadKit.cpp, RtsShoreView.cpp: no errors;
  only pre-existing warnings (RtsView.cpp: 3 before and after).
- Scratch targeted build (not a full build): `cmake -S . -B /tmp/pv_build -G Ninja -DCMAKE_BUILD_TYPE=Release
  -DAIVERSE_BUILD_TESTS=ON`, then `ninja` of 18 targets only and run each:
  aiverse_motionweave_{api,worker,hybrid,cost,backend,cache,worker_reuse,parallel_rig,phase,source_bridge,action_edges}_tests,
  aiverse_view_{core,camera_lod,layer,packet,surface,provider}_tests -> all rc 0.
  `aiverse_motionweave_phase_transfer_tests` -> rc 134 ("basic_string: construction from null"); it includes only
  AnimationPhaseTransfer.h, none of the files changed here (checked with `g++ -M`) -> pre-existing, not caused here.
- Regression proof: tests/view/view.cpp new check fails against the old View.h (view.cpp:52, rc 134).
- `g++ -std=c++20 -O2 -pthread tests/animation/motionweave_workers.cpp`: new header 5x rc 0, `-fsanitize=thread` rc 0
  with no reports; old header hangs (timeout 10 s, rc 124).

## UNVERIFIED
- Full build, full CTest, editor/client runtime, Metal/GPU behaviour. No runtime test of #1 (RtsView reload, needs
  tests/deferred_loading.cpp with the full runtime), #2 (Prisma needs the runtime library), #3, #6 (tests/tests.cpp),
  #8. No shader changes were made.

## CMake
No new test files; nothing to register (all regression checks were added to already registered tests).

## Next step
Integrator: merge, full build + CTest on KINGSTON (watch view_*, motionweave_*, deferred_loading, the core road
tests in tests/tests.cpp). Follow-ups: revision counters for MapGrid/RtsScene to drop capture-mode fingerprints (#3);
shader-graph revision for the composite key (#8); prop borrowing in RtsShoreView (#6); pre-existing
motionweave_backend_phase_transfer failure.
