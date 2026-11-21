# AIVERSE MotionWeave / Universal View — Stage 131 handoff

Date: 2026-11-19
Branch: `feature/universal-ecs-animation-pipeline`
PR: https://github.com/chromealex/AIVERSE/pull/22 (Open / Draft)
Previous: [Stage 130](animation-stage130-read-lease-lifecycle-diagnostics-2026-11-19.md)

## Git status / parallel branch safety

- Continued Stage 130 HEAD `e5c43f91490ed33bd0f16ce3835f764a4f3cf8bb`.
- Initial comparison: `main` `d71c6d47e532b25fb816a8bb1f96c41e5275a165`, animation branch ahead **974**, behind **0**.
- After Stage 131 *source/test commits* the branch was ahead **977**, behind **0**, HEAD `41c5c9fc3be5997fa11adb310fae6c9f3a7b5e02`.
- PR #22 base `feature/universal-entity-inspector-fps` has **diverged** from the animation branch: comparison after code commits: ahead **1347**, behind **615**. Don't force push/rebase or retarget PR without reviewing the base/other agents' contributions. Do not equate 0-behind-main with PR merge readiness.
- Main, the PR base and unrelated PHYSIS/NEREID/TerraWeave/Prisma/SpectraFX/textures/batching branches were not modified.

## Changed files and exact commits

1. `dc58ec6d2f0f926208bb5b65234f70c4259f59c7` —
   `engine/render/ModelResourceCache.h`:
   `acquireModelRead(const std::string& key)` used to call
   `models_.find(key)` BEFORE the ownership-thread validation in
   `acquireModelRead(const Model*)`. A foreign thread could thereby
   touch the non-threadsafe `std::map` at the wrong time.
   It now first rejects a foreign `resourceThread_`, a foreign
   `readState_->ownerThread` or inactive read-state via
   `std::logic_error`, and only then looks up the key. Correct
   single-thread presentation callers retain the same API/behavior.
   The check does NOT make `ModelResourceCache` a fully
   thread-safe concurrent read/write container; callers must
   continue to respect owner-thread use.
2. `e621e8f50a16695e0ebf6bb0f390ba8066d841f3` —
   `tests/render/device_epoch.cpp`:
   Adds `verifyReadLeaseContracts()` and
   `verifyReadOwnerRebind()` to the existing
   `aiverse_device_epoch_tests` target registered in
   `CMakeLists.txt` as `render_device_epoch`.
   CPU-only `StubDevice` coverage for:
   - unknown/foreign/null Model read leases rejected;
   - narrow pointer/key leases, move-only transfer and
     coarse frame guard + active/peak reader counts;
   - foreign-thread lookup rejected while Model owner bound
     to the main/render thread;
   - `replace`, `invalidateSources`, `trimUnused`
     and `shutdown` all throw BEFORE Model detach, eviction,
     or resource release while readers are active;
   - `blockedMutations` reaches the expected four denied
     operations; zero active readers after all tokens release;
   - a subsequent `shutdown` is permitted when no readers
     are active;
   - cache owner successfully binds on a different thread
     AFTER an empty, GPU-idle-proven `shutdown`.
3. `41c5c9fc3be5997fa11adb310fae6c9f3a7b5e02` —
   same `tests/render/device_epoch.cpp`:
   Added streaming regressions while three Model readers are
   live. `insertIfAbsent` and the transactional
   `publishStagedModels` are still legal for NEW keys,
   because `std::map` insertion doesn't invalidate
   references to already resident `Model` objects. The
   staged source map is emptied on transfer. By contrast,
   replacing/evicting existing Model entries is forbidden
   until the last lease is released.

## Streaming and owner-lifetime audit

- Inspected `modules/rts/view/RtsView.cpp`: the up-front
  `loadType` loop precedes `resources_.acquireFrameReadScope()`;
  the scope then covers View layer callbacks, MotionWeave worker
  evaluation (after join) and synchronous Renderer3D submission.
- Inspected `modules/rts/view/RtsLook.cpp`:
  `ModelPreparationService` can publish **new** models into
  the cache via `resources_.publishPrepared`; the
  `ModelResourceCache` read guard does not prohibit
  `publishPrepared`, `insertIfAbsent`, or
  `publishStagedModels` when those operations insert a NEW
  cache key. This preserves streaming additions.
  The separate `trimTypes` path calls guarded
  `resources_.trimUnused` and must occur outside the
  per-frame read scope; if called by an unexpected nested
  frame callback it fails closed rather than releasing a
  Model currently read by a draw.
- `ModelResourceCache::replace`, `invalidateSources`,
  `trimUnused`, and `shutdown` still reject active
  leases; `ModelCacheReadScope` remains a CPU lifetime
  barrier, not a GPU completion fence.
- No changes were made to GPU resource ownership or
  deferred `AsyncResourceRetirementQueue` behavior.

## How to run the newly added regression test

On a native build host with a checked-out version of this branch:

```bash
git fetch origin main feature/universal-ecs-animation-pipeline
git switch feature/universal-ecs-animation-pipeline
git pull --ff-only origin feature/universal-ecs-animation-pipeline

cmake -S . -B build/animation-validation -DAIVERSE_BUILD_TESTS=ON
cmake --build build/animation-validation --target aiverse_device_epoch_tests -j 8
ctest --test-dir build/animation-validation --output-on-failure -R '^render_device_epoch$'
```

Then run the whole native CMake/CTest matrix, renderer editor/FPS/RTS
GPU visual tests, true GPU-fence failure injection and sanitizers.
Mac/Metal builds should run on a properly configured macOS host.
The test target was **already present**; no CMake addition was needed.

## Exact test status and limitations

- **ACTUALLY PERFORMED:** connected GitHub PR/latest-main/PR-base
  comparison, Stage 130 handoff and exact source inspection,
  three source/test GitHub commits with blob-SHA guards,
  source readback, review of existing test registration in
  `CMakeLists.txt`, and query of GitHub Actions workflow runs
  for the Stage 131 test-commit HEAD. No workflow runs were
  reported for that commit at check time.
- **NOT RUN:** native checkout/CMake configure, C++ compile/link,
  CTest, CI, Metal/GL/Vulkan, FPS/RTS/editor visual parity,
  ASan/TSan/UBSan, Model GPU-fence fault injection,
  cross-thread move/destructor `std::terminate` subprocess
  fixtures, throughput or frame-time benchmarks.
- Therefore test cases are **AUTHORED, NOT VERIFIED PASSING**.
  No compiled correctness/runtime performance gain is claimed.
  PR #22 remains DRAFT.
- The read guard cannot prevent a host mutating
  `Model*` in place outside `ModelResourceCache`.
  Asset reimports require ModelPublicationEpoch and a
  rebuilt compatible AnimationGraph/LOD plan.
- The test uses StubDevice only and does not prove real
  GPU asynchronous resource completion, device retirement
  or graphics-frame correctness.
- Cross-thread lease RELEASE deliberately terminates
  the process by contract; future negative tests should
  run in child processes, not the main CTest process.

## Next Stage 132

1. Refresh `main`, PR #22 base and all relevant peer branch heads.
   The base is still divergent; coordinate a safe integration plan,
   no force push or silent base replacement.
2. Run the new `render_device_epoch` native target, collect real
   compiler/CTest output and fix every error/warning. Run
   `animation_hot_reload`, `animation_ecs_presentation`,
   `animation_generic_retarget` and all full CTest targets,
   including all editor/RTS/FPS relevant validation.
3. Add isolated subprocess fixtures for fatal cross-thread release
   and cache-destructor-with-active-reader errors; test
   `publishStagedModels` key collisions and real streaming
   transitions on the render-owner thread.
4. Validate real Mac/Metal and any available GL/Vulkan backend,
   Model pointer recycle/hot reload, mesh LOD retarget parity,
   GPU resource-fence failure behavior and crowd frame-time
   after full feature completion.
5. Create a separate Stage 132 handoff with **actual test logs**,
   current HEAD, modified files, warnings, known limits and
   next concrete steps, and update `AGENTS.md`. Keep PR DRAFT
   until the code actually builds and passes the required tests.
