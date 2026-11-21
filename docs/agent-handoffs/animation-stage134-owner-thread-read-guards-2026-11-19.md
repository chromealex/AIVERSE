# AIVERSE MotionWeave / Universal View — Stage 134 handoff

Date: 2026-11-19
Branch: `feature/universal-ecs-animation-pipeline`
PR: https://github.com/chromealex/AIVERSE/pull/22 — **DRAFT**
Previous: [Stage 133](animation-stage133-model-cache-callback-reentrancy-2026-11-19.md)

## Exact branch topology

- Continued Stage 133 HEAD `104cff7dbcf202dffe5a5b69239575882cf2d86b`.
- After the two Stage 134 code/test commits, GitHub reported current feature HEAD `1874dad97cfadd1f9dd51f02ef56dbd8dcdda928`.
- `main` and this feature are **divergent**: feature ahead **998**, behind **305**, merge base `58e29ab052458fffcdebebffe27022b53adb9367`.
- PR #22 base `feature/universal-entity-inspector-fps` is separately **divergent**: feature ahead **1368**, behind **615**, merge base `c944882193c1404a7e47f409f8d7847c766b4e31`.
- Do NOT assume `git merge-base --is-ancestor origin/main HEAD` succeeds now. Do not auto-merge, rebase, retarget PR, force push or edit `main`; other agents actively modify renderer, textures, physics, VFX, terrain, water, GI and batching.

## Implementation commits

1. `540cfc1665b0ba941d9f18a62866540b8731dda4` — `engine/render/ModelResourceCache.h`.
   - Added a private owner-thread preflight `requireReadOwnerThread(operation)` that rejects a foreign established render/reader owner or expired reader state **before touching non-threadsafe `models_` storage**.
   - Applied to both mutable/const `find(key)`, `contains(key)`, `models()`, `affectedBySource(source)`, `stats()`, `modelFiles()`, `forEachOwnedModel(visitor)`, `geometryPoolModelReferences()` and `gpuOwnerEstimate()`.
   - Existing `find` and `models` no longer claim `noexcept`: wrong-thread access throws `std::logic_error`. On the established owner thread, return values and cache semantics remain unchanged.
   - `models()` still returns a borrowed reference; later iteration must also occur on the owner thread while the cache is alive. This is NOT a lock-free concurrent map.
   - Before the host establishes a device or read-owner, it remains responsible for serializing first-time cache population and reads. No locks, new worker dispatch or gameplay authority were added.

2. `1874dad97cfadd1f9dd51f02ef56dbd8dcdda928` — `tests/render/device_epoch.cpp`.
   - Added `verifyOwnerThreadMapReadContracts()` to the existing test target. One background thread attempts 10 unsafe entry points; all must throw `std::logic_error`. After the worker joins, the render owner checks that normal lookup, registry iteration, source inventory and shutdown remain valid.
   - Repaired POSIX fatal-child environment forwarding: `posix_spawnp` now inherits process environment via `_NSGetEnviron()` on macOS or `environ` on other POSIX hosts instead of passing an empty `envp`. This avoids obscuring loader/system paths in the Stage 132 death fixtures. The child still must die with `SIGABRT` under the tested fatal contract.
   - No new CMake target required: existing `aiverse_device_epoch_tests` / CTest `render_device_epoch` already registers this source.

## Host-side audit actually done

- Reviewed `modules/rts/view/RtsLook.cpp`, `RtsView.cpp`, `RtsViewDraw.cpp`, `apps/client/GameClient.cpp`, `apps/editor/ClientHost.cpp`, `engine/render/ModelResourceCache.h`, `ModelReadLease.h`, and the existing `tests/render/device_epoch.cpp` via the connected GitHub repository.
- RTS `trimUnused` release callback currently only clears `animationCache_.erase(model)`; no direct nested `ModelResourceCache` write was observed.
- RTS `reloadModelSources` detach callback assembles affected type/source sets, forgets generic View sources, clears animation/streaming choices, and invalidates TypeLooks. It does NOT call `resources_.insertIfAbsent`, `publishStagedModels`, `replace`, `trimUnused` or `invalidateSources` within the callback. Rebuild `loadType` occurs after `resources_.invalidateSources` returns; no deferred mutation queue is required for these audited callbacks.
- RTS `RtsView::draw` performs synchronous `loadType` before its `ModelCacheReadScope`, then holds the scope across animation graph evaluation and synchronous renderer submission; host-owned GPU retirement remains separate.
- In `reloadAppearanceTypes`, staged Model publication happens before the host swaps its authored TypeLooks; it is not nested inside a Model cache detach callback.
- This is a **targeted source audit**, not a proof that every downstream callback or external module obeys the thread contract.

## Parallel-main integration audit

Connected GitHub commit comparisons showed current `main` has 305 commits absent from this branch, with changed files including `engine/render/Model.cpp`, `Model.h`, `Renderer3D.cpp`, `Renderer3DModels.cpp`, `GlobalGeometryPool.*`, `engine/gfx/Gfx.h`, Metal/Vulkan backends, `modules/rts/view/RtsViewDraw.cpp`, editor Scene Window, and numerous batching policies/tests. No `ModelResourceCache.h` or `tests/render/device_epoch.cpp` change appeared in the current main-only compare.
The separately divergent PR base adds editor/VFX and gfx changes, also requiring coordinated integration. **No merge was attempted**: preserve other agents' concurrent work; audit both bases in an isolated worktree with actual compilation and visual parity before selecting a merge strategy.

## Tests performed vs not performed

**ACTUALLY PERFORMED:** connected GitHub source and previous handoff reads, exact blob-SHA code/test writes and readback of both files, branch/PR-base/main comparisons, GitHub Actions status query, and targeted host-call-site inspection.

**NOT RUN:** local AIVERSE source checkout, CMake configure, full C++ compiler/link, CTest `render_device_epoch`, native FPS/RTS/editor, Metal/GL/Vulkan GPU parity, ASan/TSan/UBSan, real GPU fence/fault injection, performance benchmarks. No running or completed passing GitHub Actions CI result for the Stage 134 HEAD was available at query time (recent runs were queued/cancelled on older commits). The new tests are authored and committed, NOT passing/verified. Keep PR #22 DRAFT.

## Mac host validation commands

```bash
git fetch origin main feature/universal-ecs-animation-pipeline feature/universal-entity-inspector-fps
git switch feature/universal-ecs-animation-pipeline
git pull --ff-only origin feature/universal-ecs-animation-pipeline

# WARNING: both bases diverged; inspect before any merging.
git rev-list --left-right --count origin/main...HEAD
git rev-list --left-right --count origin/feature/universal-entity-inspector-fps...HEAD
git diff --name-only origin/main...HEAD -- engine/render engine/gfx engine/view modules/rts/view

cmake -S . -B build/animation-validation -DAIVERSE_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/animation-validation --target aiverse_device_epoch_tests -j 8
ctest --test-dir build/animation-validation --output-on-failure -R '^render_device_epoch$'
cmake --build build/animation-validation -j 8
ctest --test-dir build/animation-validation --output-on-failure
```

Then inspect native fatal-subprocess behavior, C++ warnings and graphics snapshots, run memory/thread sanitizers on supported builds, test Model hot reload and read-token lifetimes with GPU fences, and compare crowd frame time.

## Next Stage 135

1. Run native compilation/CTest on a real Mac checkout; fix any warning/error in Stage 132–134 code. Upload actual logs in its own handoff.
2. Continue auditing less-common indirect Model cache access through callback visitors and async appearance reload paths. Any nested host mutation should execute only after the enclosing cache transaction has returned.
3. Review divergence in a throwaway integration worktree (main-only Model/Renderer/Metal and PR-base editor/VFX changes); coordinate with other branch owners before merging into the shared feature.
4. Complete remaining universal View/legacy RtsView migration, verify animation LOD/model publication continuity and GPU/CPU performance before changing PR state.
5. Update `AGENTS.md`, `docs/animation-graphs.md`, and the append-only universal animation journal with actual state, exact commits and a separate Stage 135 handoff.
