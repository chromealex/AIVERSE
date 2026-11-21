# AIVERSE MotionWeave / Universal View — Stage 132 handoff

Date: 2026-11-19
Branch: `feature/universal-ecs-animation-pipeline`
PR: https://github.com/chromealex/AIVERSE/pull/22 (Open / Draft)
Previous: [Stage 131](animation-stage131-streaming-cache-read-regressions-2026-11-19.md)

## Scope and current topology

- Continued from Stage 131 HEAD `fd6fd9ca91fd9206f78b56458c9770e8f0926b5d`.
- Before changes: `main` at `d71c6d47e532b25fb816a8bb1f96c41e5275a165`, feature 983 ahead / 0 behind. Following the two functional/test commits, feature 985 ahead / 0 behind.
- PR #22 targets `feature/universal-entity-inspector-fps`, **NOT main**. At the post-change comparison the PR base was divergent: feature ahead 1355 / behind 615 relative to that base. Do not retarget, merge or force push without a separate integration audit.
- Do not touch other agents' PHYSIS/NEREID/TerraWeave, Prisma/SpectraFX, textures, batching, Vivid or GI branches.
- Native C++/GPU validation is still incomplete and this PR MUST remain Draft.

## Implemented and exact commits

1. `e36a69f06d3f844ede4f04595ee91615d6a4935b` — `engine/render/ModelResourceCache.h`.
   `insertIfAbsent()` now checks that a cache which is already bound to a GPU/render owner or has an established Model-reader owner is being mutated on that owner thread. It also rejects an expired read-state. This check happens **before** `models_.try_emplace`, so a foreign thread does not race with the render owner's `std::map` insertions. New distinct keys remain insertable during a live frame read scope **by the owner thread**. This is NOT a concurrent read-write map or a cross-thread streaming publication API.
2. `58b7b62101698e95201737d07a06f3d96ad4d8bd` — `tests/render/device_epoch.cpp`.
   Replaced all per-test `assert()` with `CHECK()`, an always-on aborting check with file/line diagnostics. Previously a Release/RelWithDebInfo build could silently skip regression assertions. Added:
   - atomic collision rejection for `publishStagedModels()`, preserving both staged/resident entries and cache generation;
   - foreign-thread `insertIfAbsent()` rejection while three readers remain active;
   - POSIX `posix_spawnp` child-process fixtures expecting `SIGABRT` for cross-thread `ModelReadLease::reset()`, cross-thread `ModelCacheReadScope::reset()`, and cache destruction with active reader. Child mode returns zero if a mandatory fatal contract is accidentally relaxed, so the parent fixture fails.
3. `9b0c28ea77c8c3c1b6630d5df95391b310f372c9` — fix the `CHECK` comment in the same test source.

No public graph/playback API changed. No changes to the simulation ECS, MotionWeave worker/graph clock, GPU retirement fences or host save/network formats.

## Concrete verification performed

- **ACTUAL:** read connected GitHub Stage 131 handoff, `ModelResourceCache.h`, `ModelReadLease.h`, `device_epoch.cpp` and CMake registration. Performed exact-path GitHub writes with current blob SHA, read back the committed source, compared branch to `main` and the divergent PR base. Existing `aiverse_device_epoch_tests` / CTest `render_device_epoch` target remains registered.
- **NOT RUN:** C++ compilation/link, local CMake/CTest, CI for these new commits, Metal/GL/Vulkan renderer, FPS/RTS/editor visuals, ASan/TSan/UBSan, actual GPU retirement fault injection, pointer-recycle stress or benchmarks. The connected execution sandbox has no mounted AIVERSE checkout and cannot resolve github.com to clone. A query of recent GitHub Actions runs did not establish a completed run for this Stage 132 HEAD.
- The test cases are **authored, not demonstrated passing**. There is no measured speedup, native compile proof or validated crash behavior.

## Platform and lifetime caveats

- POSIX death fixtures run on macOS/Linux; they are intentionally skipped on native Windows while the normal CPU regressions still execute.
- Death fixtures expect `std::terminate` to deliver `SIGABRT`; unusual application-level terminate handlers could change this expectation.
- The Model-resource cache is still owner-thread-only: its `find()`, `contains()`, `ModelMap` and bound `gfx::Device` are NOT general lock-free cross-thread access.
- Existing `ModelReadLease` and `ModelCacheReadScope` protect borrowed CPU Model memory only until worker join and synchronous draw complete. They are NOT GPU fences; `waitForResourceIdle`/retirement tokens remain the GPU-owner responsibility.
- `insertIfAbsent()` before any GPU/reader binding retains the historical host setup contract; the host must serialize any such prebinding construction.
- Source publication epochs still invalidate reused Model pointers after reimport. Strong shared ownership and unique Model-cache read leases are different modes and must not be confused.

## Commands for the next macOS agent

```bash
git fetch origin main feature/universal-ecs-animation-pipeline
git switch feature/universal-ecs-animation-pipeline
git pull --ff-only origin feature/universal-ecs-animation-pipeline
git merge-base --is-ancestor origin/main HEAD || echo "Main has advanced; inspect before integration"

cmake -S . -B build/animation-validation -DAIVERSE_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/animation-validation --target aiverse_device_epoch_tests -j 8
ctest --test-dir build/animation-validation --output-on-failure -R '^render_device_epoch$'
ctest --test-dir build/animation-validation --output-on-failure -R '^(animation_ecs_presentation|animation_hot_reload|animation_generic_retarget)$'
cmake --build build/animation-validation -j 8
ctest --test-dir build/animation-validation --output-on-failure
```

For the death fixtures, verify parent reports normal success when its children die with `SIGABRT`; confirm there are no leftover core dump files. Then validate the native Editor/FPS/RTS GPU paths on available Metal and GL/Vulkan hosts, exercise real GPU fences and ASan/TSan/UBSan under a dedicated build, and collect before/after crowd CPU+GPU timings.

## Next Stage 133

1. **Run and fix** the native test target first, especially POSIX child spawning, RelWithDebInfo checks and the owner-thread insertion contract. Attach actual logs and compiler warnings.
2. Audit all remaining model-cache mutation and access entry points for render-owner-thread consistency, including host streaming publication and source detach paths; do not introduce a false global thread-safety guarantee.
3. Resolve PR-base divergence with a deliberate reviewed merge plan, keeping feature branch isolated from other agents until validated.
4. Finish any remaining universal View/RtsView migration and run full FPS/RTS/editor visual parity and benchmarks before marking PR #22 ready.
5. Append a separate Stage 133 handoff with actual test results, exact code SHAs, limitations and next step. Update `AGENTS.md` and `docs/agent-handoffs/animation-universal.md`.
