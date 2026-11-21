# Review fixes: ECS core + SceneRuntime + batching (2026-11-21)

- Branch: `feature/aiv20261121_fix_ecs_scene` (worktree
  `.worktrees/feature/aiv20261121_fix_ecs_scene`), base main `973b32970`.
- Not pushed, not merged.

## Commits

| SHA | Subject |
|---|---|
| `abb8983` | test(ecs-scene): lock COW detach in prepareExistingWrite |
| `21d763a` | fix(ecs-scene): scene residency restore, rollback diff and job lifetime |
| `98d3829` | fix(ecs-scene): non-throwing WorldScene retire hook with parked teardown |
| `3e0cf45` | fix(ecs-scene): refcount SceneCollisionBridge acquire per key |
| `7fbb192` | fix(ecs-scene): validate SharedRef slots against SharedStore on load |
| `238d57d` | test(ecs-scene): align static-scene rollback tests and docs with residency diff |
| `8e792dd` | fix(ecs-scene): decline static packet replay for GPU selection draws |
| `11f8a5d` | fix(ecs-scene): keep CPU MDI in non-learning AIVERSE_BATCH_AUTO passes |
| `a519cf4` | fix(ecs-scene): reclaim fence-completed tail spans in GlobalGeometryPool |
| `d653270` | docs(ecs-scene): ECS handoff checkpoint (docs/ecs-agent-handoff.md) |

## Findings

| # | Finding | Status | Files |
|---|---|---|---|
| 1 | `World::prepareExistingWrite<T>` mutates COW snapshot | **NOT A BUG** | test only: `tests/ecs_cow_exception_test.cpp` |
| 2 | `SceneResourceBackend::pin` rejects Cancelled forever | FIXED | `engine/scene/SceneResourceBackend.h` |
| 3 | Restored Loading records have no job; pendingReplace blocks Replace | FIXED | `engine/scene/SceneLoadCoordinator.h` |
| 4 | Retire hook can throw (WorldScene::shutdown); release order | FIXED | `engine/scene/WorldSceneResourceBridge.h`, `SceneLoadCoordinator.h` |
| 5 | `jobs_` never erased; committed jobs keep manifest | FIXED | `SceneLoadCoordinator.h` |
| 6 | Rollback releases all leases and restreams | FIXED | `SceneLoadCoordinator.h` |
| 7 | Static packet replay ignores GPU selection packets | FIXED | `engine/render/StaticModelCachePolicy.h` |
| 8 | `autoCpuPermit` disables CPU MDI in non-learning passes | FIXED | `engine/render/AutoCpuBatchPolicy.h`, `Renderer3DModels.cpp` (1 call site) |
| 9 | GlobalGeometryPool never reuses freed space by default | FIXED (safe part) | `engine/render/GlobalGeometryPool.cpp` |
| 10a | `SceneCollisionBridge::acquire` does not refcount | FIXED | `engine/scene/SceneCollisionBridge.h` |
| 10b | SharedStore loads don't validate SharedRef slots | FIXED | `engine/sim/{PersistentEcs.h,Simulation.h,SharedStaticComponents.h,SharedStaticPersistence.h}` |

Details:
1. Non-const `World::poolIfExists<T>()` (used by `prepareExistingWrite`)
   already clones a pool with `use_count()!=1` when COW is on, and
   `Pool::prepareParallelWrite` detaches pages. Proven with a 600-entity /
   3-page repro; regression block added. NEREID buoyancy already runs with COW
   + rollback in `tests/ecs_nereid_buoyancy_test.cpp`. `engine/core/ECS.h`
   was not changed. The comment in `engine/world/TerraWeaveShorelineRaster.h:271`
   claiming the opposite is stale (not in this area; not edited).
2. `pin()` on a Cancelled entry: Preparing again if its future is valid
   (staging stays reserved, worker reused), else Queued with staging released.
3. `queueRestoredResidency()` also schedules full (non-residency) jobs for
   restored Loading records; the host commits or cancels them, which clears a
   restored `pendingReplace`.
4. Bridge retire hook is `noexcept`: detaches instance from its key, calls
   `shutdown`; on exception parks the instance, logs to stderr, stores
   `lastRetireError()`. `update()` / `retryParkedShutdowns()` retry;
   `parkedCount()`. Coordinator `release()` erases the refcount entry before
   calling the hook and swallows a throwing hook.
5. Committed jobs drop their manifest. `poll()` prunes terminal jobs
   (Committed, Unloaded, Failed whose Loading record is gone) beyond
   `kMaxTerminalJobs = 64`, oldest serial first; pruned tickets report
   Unknown. Failed loads with a live Loading record are kept so `cancel()`
   still works. `invalidateAfterRestore` erases old-epoch jobs that have no
   running future. New `jobCount()`.
6. `invalidateAfterRestore` keeps leases of scenes still Active in the
   restored `SceneState` with the same asset; releases the rest. Resident
   records now store the asset. Simulation state is untouched (residency only).
   **Behaviour change**: after a rollback, `queueRestoredResidency()` returns
   tickets only for scenes that are not resident (tests updated accordingly).
7. `staticRenderPacketDrawCandidate` declines when `gpuIndexed.valid()` or the
   primitive/shadow packet vectors are non-empty (`drawHasGpuSelectionPackets`).
8. `autoCpuMdiPermit(policy, auto, learning, phase, items, min)`: no AUTO →
   permit; learning → one `useMdi()`; otherwise permit iff `items >= min`
   (`AIVERSE_BATCH_AUTO_CPU_MIN_ITEMS`, default 64). The small-workload gate
   is preserved as the comment above the call site intends.
9. Default stays non-recycling. Releases are recorded (default: bounded
   `tailPending`; recycle mode: existing `retired`). Before each pooled upload,
   pairs with `retireFrame <= completedFrameSerial()` that sit exactly at both
   bump pointers are popped LIFO. Nothing happens without fence info
   (serial 0) or on an unhealthy device. Auxiliary index spans below a pair
   block it. No change to `GlobalGeometryPool.h`.
10a. Resident entries carry `refs`; the stream is destroyed at zero.
   New `leaseCount(key)`.
10b. New setup-time `PersistentEcsRegistry::addLoadValidator(name, fn)` /
   `Simulation::addPersistentLoadValidator`, run on the staged World before a
   load commits; no change to save bytes or checksums. `registerSharedPersistence`
   registers `validateSharedRefs<T>`; error format, e.g.:
   `persistent ECS: sample.actor.shared_ref: shared ref of entity 0:0 has slot 999 but the shared store holds 2 values`.

## Verification actually run (Linux, GCC, this container; no CMake)

Engine objects were compiled locally from `engine/core/Fixed.cpp`,
`FixedMath.cpp`, `engine/data/Json.cpp`, `JsonCompose.cpp`,
`engine/sim/Simulation.cpp`, `Replay.cpp`, `ProjectileData.cpp` (`-O1`), then
each test: `g++ -std=c++20 -O2 -DNDEBUG -Wall -Wextra -I. tests/<t>.cpp <objs> -pthread`.
No new warnings in any test compile.

| Test | Result |
|---|---|
| `ecs_cow_exception_test` (header-only) | PASS |
| `ecs_scene_load_coordinator_test` | PASS; also PASS with `-fsanitize=address,undefined` |
| `ecs_scene_resource_pipeline_test` | PASS; also PASS with ASan/UBSan; new checks FAIL against old headers |
| `ecs_scene_runtime_test` | PASS |
| `ecs_shared_static_components_test` | PASS (new corrupt-save check) |
| `ecs_prefab_static_catalog_test` | PASS |
| `ecs_static_scene_integration_test` | PASS (after expectation update; failed with old 2-ticket expectation) |
| `ecs_static_scene_multidomain_test` | PASS (same) |
| `ecs_static_scene_editor_contract_test` | PASS |
| `ecs_simulation_bridge_test` | PASS |
| `ecs_scene_hierarchy_simulation_test` | PASS |
| `ecs_scene_collision_bridge_test` | PASS (linked with `engine/world` collision sources + `engine/pack/Sha256.cpp`) |
| `tests/render/model_batching_policy.cpp` | PASS, linked against all `aiverse_models` sources compiled locally (+ufbx, zstddeclib); new pool check FAILS against old `GlobalGeometryPool.cpp` |
| default-mode tail-reclaim repro (`/tmp`, not committed; MockDevice copied from the test) | PASS |
| `autoCpuMdiPermit` standalone repro | PASS |
| `g++ -fsyntax-only -Wall -Wextra` of `engine/render/Renderer3DModels.cpp` | clean except pre-existing `-Wmisleading-indentation` in `GpuResourceRetirement.h:228` (also present on base) |
| `-fsyntax-only` TU including `WorldSceneResourceBridge.h` | clean |
| `-fsyntax-only` `tests/render/material_batches.cpp` | clean except pre-existing unused `machStartOk` |

## UNVERIFIED

- Full CMake build, CTest suite, `aiverse_tests`, headless RTS/FPS matches
  (owner rule: builds only on KINGSTON).
- `WorldSceneResourceBridge` runtime behaviour (needs gfx::Device + WorldScene).
- `tests/render/material_batches.cpp` (real GPU backend) — CPU checks added, NOT RUN.
- Renderer3D behaviour for findings 7/8 on Metal/Vulkan/GL; GPU-fence
  correctness of tail reclamation on real backends.
- No Metal / `.mm` file was changed.

## CMake registrations needed

None. All changed tests are already registered (`ecs_*` foreach list,
`ecs_scene_collision_bridge_test`, `model_batching_policy`,
`aiverse_material_batches`). The default-mode tail-reclaim repro needs its own
process (the batching test sets `AIVERSE_GEOMETRY_POOL_RECYCLE=1` once at
start); optional follow-up: a separate small test target for it.

## Risks / API notes

- Finding 6 changes observable behaviour of `invalidateAfterRestore` +
  `queueRestoredResidency` (fewer tickets, kept leases). Hosts that assumed
  "all Active scenes return a ticket after rollback" must use
  `residentSceneCount()` or simply commit what is returned. Docs updated:
  `docs/scene-worlds.md`, `engine/scene/AGENTS.md`.
- Job pruning: status queries for tickets older than the 64 newest terminal
  jobs return Unknown.
- Tail reclamation (finding 9) relies on the same retire/completed-frame
  contract as the opt-in recycle path; caches referencing a released Model's
  pool offsets must be invalidated before `releaseModel` (existing contract).

## Next step

Integrator: merge the branch, run on KINGSTON the full build, CTest (labels
for ECS/scene/render policy), and headless matches; optionally add a separate
test target for default-mode tail reclamation.
