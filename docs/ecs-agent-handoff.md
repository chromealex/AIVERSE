# ECS 2.0 — Agent handoff (living document)

Last updated: 2026-11-17  
Repository: https://github.com/chromealex/AIVERSE  
Feature branch: `feature/ecs-architecture-benchmarks`  
Pull request: https://github.com/chromealex/AIVERSE/pull/8  
Base: `main`

## Non-negotiable architectural decisions

- Retain production `engine/core/ECS.h` Sparse Set and its deterministic `World::each<First,...>` order until a separate compatibility/performance review.
- Do not change `Simulation::step()` while experimental scheduling is not fully tested against lockstep, snapshots, rollback, RTS and FPS.
- Components and world resources must declare all reads/writes before allowing concurrent system execution. Structural writes and order-sensitive effects must form barriers.
- Never change gameplay logic based on observed wall-clock timing. `Auto` chooses a lane policy from stable workload metadata and fixed configuration.
- Preserve entity generations, canonical command ordering, RNG state, snapshot reproducibility and checksums.

## Research and baseline

Compared AIVERSE with https://github.com/chromealex/ME.BECS and https://github.com/Felid-Force-Studios/StaticEcs. Tested Sparse Set, auto-smallest, bitmap indices, 4096/256/64 hierarchical bitmap, static slots and persistent workers. The data supports keeping Sparse Set for typical sparse/moderate queries, with parallel jobs for sufficiently large independent workloads.

Selected Mac Studio benchmarks (all times are 10 passes median, milliseconds):

| Entities | Density | Sparse dense | Sparse+hierbitmap | Static+hierbitmap |
| ---: | ---: | ---: | ---: | ---: |
| 1,000,000 | 1% | 0.147834 | 0.448333 | 0.502541 |
| 1,000,000 | 25% | 3.177 | 4.56212 | 8.27862 |
| 1,000,000 | 100% | 7.92646 | 9.11454 | 8.22242 |

Adaptive jobs benchmark on equal sparse storage/callback:

| Entities | Density | Sequential | Parallel 8 lanes | Auto 8 lanes |
| ---: | ---: | ---: | ---: | ---: |
| 1,000,000 | 1% | 0.489416 | 0.621333 | 0.485625 |
| 1,000,000 | 25% | 8.14237 | 2.67354 | 2.64929 |
| 1,000,000 | 100% | 20.5996 | 4.18721 | 4.25746 |

These are microbenchmarks, not guaranteed game-frame speedups. Checksums matched for all tested independent per-entity update workloads. Do not infer that arbitrary parallel systems are deterministic.

## Implemented experiments (PR #8)

| Path | Purpose |
| --- | --- |
| `tests/ecs_architecture_bench.cpp` | Sparse Set query, snapshot and destruction baselines |
| `tests/ecs_bitmap_query_bench.cpp` | Bitmap index, prepared pair, static-slot prototypes |
| `tests/ecs_hierarchical_bench.cpp` | Hierarchical mask and storage-density comparisons |
| `tests/ecs_persistent_jobs_bench.cpp` | Persistent 0/1/2/3/7 workers at several densities |
| `tests/ecs_adaptive_jobs_bench.cpp` | Identical callback across Sequential, Parallel, Auto |
| `engine/experiments/EcsExperiments.h` | PreparedAll, PersistentWorkers, eachParallel, eachAdaptive, DeterministicCommands, DirtyPageSnapshot |
| `engine/experiments/EcsDependencyPlan.h` | Read/write/resource/structural conflict graph and stable waves |
| `engine/experiments/EcsWaveExecutor.h` | **NEW:** opt-in execution of compatible system waves through PersistentWorkers, with barrier callback and exception propagation |
| `tests/ecs_experiments_test.cpp` | Component queries, adaptive jobs, exceptions, command buffer, dirty pages, graph tests |
| `tests/ecs_wave_executor_test.cpp` | **NEW:** sequential/parallel wave parity, dependency ordering, worker exception recovery |

### Latest step: opt-in wave execution

- Commit `fac3d130`: added `EcsWaveExecutor.h`.
- Commit `f1d6534`: added `ecs_wave_executor_test.cpp`.
- `WaveExecutor` accepts a `DependencyPlan` and ordered array of callable systems. It executes independent systems within each wave concurrently if at least two systems are present, and waits before the next wave.
- A caller-provided `afterWave` callback supports barriers such as flushing *already joined* deterministic structural commands.
- Worker exceptions propagate after remaining tasks join; the simulation state may be partially updated after failure, so the caller must discard and rebuild from a valid snapshot.
- This is deliberately **not** wired to `Simulation::step()`: access declarations are trusted and not yet automatically verified/enforced.

### Validation status

- User previously reported `PASS prepared, persistent parallel, deterministic commands, dirty pages` from the older test version.
- User ran and reported full adaptive benchmark CSV with matching checksums, as shown above.
- **NEW files have been committed through the GitHub connector but have not yet been compiled on Mac Studio.** This session's container cannot resolve GitHub hosts, so no claim of native compilation is made.
- Previous test changes to adaptive/exception/dependency behavior also require current-branch revalidation.

## Build/run from repository root

```sh
git switch feature/ecs-architecture-benchmarks
git pull --ff-only

c++ -std=c++20 -O2 -pthread -I. tests/ecs_wave_executor_test.cpp -o /tmp/aiverse-wave-test
/tmp/aiverse-wave-test

c++ -std=c++20 -O3 -pthread -I. tests/ecs_experiments_test.cpp -o /tmp/aiverse-ecs-experiments-test
/tmp/aiverse-ecs-experiments-test

c++ -std=c++20 -O3 -DNDEBUG -pthread -I. tests/ecs_adaptive_jobs_bench.cpp -o /tmp/aiverse-ecs-adaptive
/tmp/aiverse-ecs-adaptive
```

## 2026-11-17 — Step: checked access scopes (latest)

User Mac Studio validation of prior step (reported in conversation):
- `PASS dependency wave execution, sequential/parallel checksum, worker recovery`
- `PASS prepared, adaptive jobs, exceptions, dependency waves, deterministic commands, dirty pages`

Current code changes in PR #8:
- Commit `932ebe0`: `engine/experiments/EcsGuardedWorld.h`. Added opt-in `GuardedWorld` facade with declared per-component `read<T>/write<T>/has<T>`, resource access, guarded structural calls, `eachRead<T>/eachWrite<T>`, and thread-local `AccessScope` checking the executing system's `SystemAccess`. `runChecked` wraps each `WaveExecutor` callback in an `AccessScope`, restoring scope on exceptions.
- Commit `1091e10`: `tests/ecs_guarded_world_test.cpp`. Covers sequential/parallel parity of independent system wave, resource barrier, rejecting undeclared writes/resources/structural operations, and worker pool reuse after access violation.

**Validation status:** these two new commits were pushed via GitHub connector, but the new test has **not** yet been compiled or run on Mac Studio. No claim of production memory safety. `GuardedWorld` is only an opt-in facade; raw `World`, previously retained references/pointers, global state, external resources, and misdeclared indirect effects can bypass the checks. Access IDs are per-process component type IDs and not stable serialized identifiers. This is a prototype, not full scheduler enforcement.

Run:
```sh
git switch feature/ecs-architecture-benchmarks
git pull --ff-only
c++ -std=c++20 -O2 -pthread -I. tests/ecs_guarded_world_test.cpp -o /tmp/aiverse-ecs-guarded
/tmp/aiverse-ecs-guarded
```

Next:
1. Validate this test; correct build/runtime errors.
2. Introduce a stronger restricted access API to prevent direct raw `World` escape in scheduled call sites; assess thread-safety of concurrent pool creation and access.
3. Add a deterministic rollback/replay test with the existing `SimSnapshot` API and wave jobs; execute with different worker counts, compare canonical checksums.
4. Then design conservative opt-in `Simulation` integration, preserving the sequential path.
5. Continue updating this handoff after each code step and do not merge PR #8 until complete regression tests pass.

## 2026-11-17 — Step: deterministic World replay (latest)

New test `tests/ecs_wave_replay_test.cpp`:
- Commit `5a93c66`: replay World snapshot through opt-in checked dependency waves and lane-ordered structural commands.
- Commit `327e0bc`: route scheduled component reads through `GuardedWorld` rather than raw `World::pool`.
- Executes ten deterministic ticks starting from the same deep-copied World with worker counts 0, 3 and 7; compares a fingerprint that includes dense Entity handles, component values and Entity allocator generations/free list.
- Movement reads Velocity and writes Position, acceleration writes Velocity, and structural command application occurs after the appropriate wave barrier.
- Confirms original seed state remains unchanged, compares repeated replay with the same and different worker counts.
- This test is not `Simulation::saveState/loadState` and does not verify network commands, RNG or full module snapshots.

**Validation status:** commits present on feature branch; Mac Studio has **not yet compiled or run this new replay test**. Prior wave/experiments tests passed as reported by user.

Run:
```sh
git pull --ff-only
c++ -std=c++20 -O2 -pthread -I. tests/ecs_wave_replay_test.cpp -o /tmp/aiverse-ecs-replay
/tmp/aiverse-ecs-replay
```

Next: validate new guarded/replay tests, address any failures, add simulation-level snapshot/rollback/lockstep regression coverage and enforce a no-raw-World contract before enabling production scheduled systems.

## 2026-11-17 — Bulk step: opt-in Simulation integration and consolidated test suite

User requested all outstanding work in one bundle, then will execute tests. Implemented several commits on the existing PR #8:

- `ad8979f` — `engine/core/ECS.h`: added `World::eachExisting<T>(fn)`, which does **not register a missing pool**. This avoids a known concurrent mutation hazard from calling the previous `World::each<T>()` on previously unused types. Structural operations must still be excluded while jobs are active.
- `54b4897` — `engine/experiments/EcsGuardedWorld.h`: `eachRead/eachWrite` now use `eachExisting`.
- `b0ca3cf` and `91e2ce3` — `engine/experiments/EcsSimulationBridge.h`: opt-in `addGuardedGroup(Simulation&, name, vector<GuardedSystem>, workerCount)`. A group registers as **one ordinary system** through the existing `Simulation::addSystem` API. Internally executes compatible declared systems with `WaveExecutor` and `runChecked`. The bridge disallows structural and ordered-side-effect systems. All legacy systems retain their original sequential path and a single `World::flush()` after the group.
- `689cd0f` — `tests/ecs_simulation_bridge_test.cpp`: exercises actual `Simulation::step()`, canonical player-command ordering, `SimSnapshot` save/load/replay, and matching checksums across 0/1/3/7 worker counts. Uses a small isolated `ToyModule` rather than RTS/FPS gameplay.
- `3863b7d` — `engine/experiments/EcsExperiments.h`: command buffer `flush` swaps queued lane batches before replay to avoid callback invalidation; `discard()` for cancelled batches. Commands enqueued during flush remain for the next flush; a partial mutation on exception remains nontransactional.
- `7eb318b` — `tests/ecs_experiments_test.cpp`: new command-buffer reentrant enqueue/discard tests.
- `d083de2` — `tests/run_ecs_experiment_suite.sh`: builds and executes all five correctness/integration tests with one command, optionally runs all performance benchmarks via `--bench`, optionally enables ASan+UBSan via `--sanitize`.

### Commands — run everything after pulling

```bash
git switch feature/ecs-architecture-benchmarks
git pull --ff-only
bash tests/run_ecs_experiment_suite.sh
bash tests/run_ecs_experiment_suite.sh --sanitize
bash tests/run_ecs_experiment_suite.sh --bench
```

**Verification status:** all new code is pushed through GitHub but not locally compiled by the assistant. Mac Studio previously passed wave and experiment tests *before these modifications*. User will run the full suite now. Do not claim this batch is verified yet.

### Safety limits / remaining production work

- This is **opt-in**, not default Simulation parallelism. It schedules groups of *independent systems*, not jobs within a single expensive system. For parallel entity iteration use separately profiled `eachAdaptive`, but it is not integrated with the guarded bridge.
- `GuardedWorld` is a **runtime-checked facade only**. A job can escape via external captures, raw `World`, retained pointers, static globals, RNG or events; access declarations are not statically enforced. It is still unsafe to enable arbitrary gameplay modules.
- No structural changes are allowed inside guarded groups. Use existing regular sequential systems before/after groups for spawning/deletion, or extend the staged command API with proper lifecycle semantics.
- Snapshot rollback in the test uses `Simulation::saveState/loadState` full snapshots. `DirtyPageSnapshot` is still only a standalone prototype and cannot replace `SimSnapshot` without full dirty tracking of components, allocator, resources and nontrivial data.
- There is no performance claim from the real grouped systems, and no production RTS/FPS integration yet. Benchmarking and regression suite still need a successful Mac Studio run.
- Do not merge PR #8 to `main` until assertions, sanitizers and real-game replay/lockstep regressions pass.

### Next agent actions

1. Run suite on Mac Studio; fix compile errors and any checksum mismatches, updating this handoff in each fix.
2. Expand opt-in safety with a restricted registration API and strict no-raw-World policy; instrument runtime access validation.
3. Add RTS and FPS integration checks under an explicit opt-in flag, compare checksums with baseline.
4. Design a safe per-entity parallel guarded job API that propagates `AccessScope` to workers without nested worker-pool deadlocks.
5. Evaluate real-world performance and snapshot optimizations, ensuring a sequential fallback.

## 2026-11-17 — Agent discoverability

- Commit `c873f52`: `AGENTS.md` now links this living ECS handoff in its introduction and instructs every agent to update it per implementation step.
- Current production world remains Sparse Set. The new bridge is **opt-in only** through `addGuardedGroup`; existing RTS/FPS system registration and execution remain unchanged.
- The bulk suite still awaits user execution on Mac Studio. Treat all fresh commits after `PASS dependency wave execution` as unverified until the new combined suite passes.

## 2026-11-17 — macOS test runner CRLF fix

Mac Studio reported before any C++ compilation:

```
: invalid option nameent_suite.sh: line 3: set: pipefail
```

This is consistent with the local shell script containing CRLF (`\r\n`) line endings, making Bash parse `pipefail\r`.

- Commit `e7e66fb`: added root `.gitattributes` with `*.sh text eol=lf` and `*.bash text eol=lf`.
- Commit `a1bff78`: rewrote `tests/run_ecs_experiment_suite.sh` with LF line endings and a header note.
- This addresses the script transport/checkout problem only; all C++ suite results remain unverified until Mac Studio can run it.

Recovery if an existing checkout retains CRLF despite pull:

```sh
git pull --ff-only
perl -pi -e 's/\r\n/\n/g' tests/run_ecs_experiment_suite.sh
bash tests/run_ecs_experiment_suite.sh
```

Check line endings with `file tests/run_ecs_experiment_suite.sh` or `git check-attr text eol -- tests/run_ecs_experiment_suite.sh`. The local recovery edits the working tree; after successful verification, restore the file to the committed version if needed.

## 2026-11-17 — Full Mac Studio validation (confirmed)

User ran the updated `tests/run_ecs_experiment_suite.sh` with normal, `--sanitize`, and `--bench` modes. Full pasted output is retained in conversation. **All five correctness suites passed in both normal and sanitizer modes**:

- `PASS prepared, adaptive jobs, exceptions, dependency waves, deterministic commands, dirty pages`
- `PASS dependency wave execution, sequential/parallel checksum, worker recovery`
- `PASS guarded access, dependency barriers, parallel parity, exception recovery`
- `PASS deterministic world snapshot replay across 0/3/7 workers`
- `PASS Simulation step, canonical commands, snapshot rollback, 0/1/3/7-worker checksums`
- `PASS ECS experiment suite` printed after normal, sanitizer, and benchmark runs.

During `--sanitize`, macOS emitted `malloc: nano zone abandoned due to inability to reserve vm space.` This is a sanitizer/allocator VM warning, **not a failed test**. No ASan/UBSan violations were shown in the supplied output. This does not amount to a proof of absence of all races; ThreadSanitizer has not been run.

Selected **latest** benchmark timings at 1,000,000 entities, 10 passes median (ms):

| Density | Adaptive Sequential | Adaptive Parallel (8 lanes) | Adaptive Auto (8 lanes) | Auto/Sequential speedup |
| ---: | ---: | ---: | ---: | ---: |
| 1% | 0.766041 | 0.706875 | 0.766166 | ~1.00x |
| 25% | 10.9338 | 2.99763 | 2.96833 | 3.68x |
| 100% | 27.1653 | 5.10267 | 4.82267 | 5.63x |

Latest hierarchical benchmark at 1m (ms):

| Density | Sparse dense | Sparse hierarchical | Static hierarchical | Static 2 threads |
| ---: | ---: | ---: | ---: | ---: |
| 1% | 0.140041 | 0.476584 | 0.458209 | 0.7465 |
| 25% | 3.36696 | 4.83896 | 8.24113 | 5.88079 |
| 100% | 8.07446 | 9.03925 | 8.45104 | 5.72075 |

Selected latest storage-oriented query benchmark (1m): sparse broad 16.8395ms; narrow 8.317ms; auto-smallest 8.38842ms; sparse-bitmap-prepared 8.24079ms; static-slot-bitmap 4.88879ms. Note: these are different microbenchmark workloads and MUST NOT be directly compared with hierarchical or adaptive numbers.

All compared checksum columns matched across variants in the user-provided CSV for each dataset. The entire suite executes, including `Simulation::step()` + `SimSnapshot` test on a toy module, but **no existing RTS/FPS gameplay module has been migrated or subjected to full network lockstep integration testing**.

**Updated handoff decision:** prior blocker "latest tests not compiled" is resolved for these five suites. Remaining blockers: runtime guarded access only checks calls using the facade, arbitrary external captures/raw World bypass it; complete production dependency contracts, deterministic external effect handling, realistic RTS/FPS replay tests, worker race detection/TSan, and complete ECS-wide incremental snapshots are not implemented/verified. Retain opt-in bridge and sequential default; do NOT merge by default.

## 2026-11-17 — Gameplay integration hardening and real RTS/FPS compare (CURRENT)

After the full prior test suite passed, new code was committed in PR #8:

- `9d84ca7` — `PersistentWorkers::parallelFor` now rejects nested calls on the **same** pool from its worker or caller lanes rather than deadlocking. This is defensive only; calling two distinct pools recursively is not yet subjected to a general concurrency audit.
- `12ad169` — `tests/ecs_experiments_test.cpp` verifies nested parallel submissions are rejected and worker pool remains usable.
- `942ff3b` — `modules/rts/RtsUnits.cpp` gains an **off-by-default** `AIVERSE_ECS_EXPERIMENTAL_RTS_TRANSFORM_JOBS` path for the independent `Transform.prevPos = Transform.pos` work in `sysMovement`. The spatial-hash insertion stays **sequential in original Unit dense order**. It processes at most one Transform per Unit and does not change the rest of RTS movement.
- `ed7ed90` — `CMakeLists.txt` defines the default-OFF option `AIVERSE_ECS_EXPERIMENTAL_RTS_TRANSFORM_JOBS` and applies it privately to `aiverse_game`. It **must not** be enabled for every game without validation.
- `ac8dfa3` — `tests/run_ecs_gameplay_compare.sh` builds a baseline and opt-in version of the **real RTS** headless runner and FPS tests. Runs RTS at fixed seed with `--verify-restore-at` and compares `--checksum-log` checkpoint files. FPS tests run in each build; FPS gameplay is still sequential and its authoritative State/actors/RNG must not be parallelized without a separate dependency redesign.
- `c06c48a`, `df98896` — registered `ecs_experiments_test`, `ecs_wave_executor_test`, `ecs_guarded_world_test`, `ecs_wave_replay_test`, `ecs_simulation_bridge_test` in CMake/CTest, linked to `aiverse_engine` and `Threads::Threads`.

### Validation status of this step

**Not yet compiled or run on Mac Studio**. The **previous** complete ECS suite (including sanitizers and benchmarks) passed; this new code requires fresh validation.

### Execute after git pull

```sh
# Existing standalone suites; now includes nested worker-pool test
bash tests/run_ecs_experiment_suite.sh
bash tests/run_ecs_experiment_suite.sh --sanitize

# Real modules: build two engine variants, run RTS snapshot/checksum comparison,
# FPS regression in both, optionally aiverse_tests in both:
bash tests/run_ecs_gameplay_compare.sh
bash tests/run_ecs_gameplay_compare.sh --full

# Alternative via existing CMake build tree:
cmake -S . -B build -DAIVERSE_BUILD_TESTS=ON
cmake --build build --target aiverse_ecs_experiments_test aiverse_ecs_simulation_bridge_test
ctest --test-dir build -R '^ecs_' --output-on-failure
```

### Known limitations

- No general parallel execution of RTS/FPS systems; only opt-in RTS previous-transform preprocessing is experimental.
- The number of copied Unit transforms must be large enough to justify 3 worker threads; performance and cross-platform checks are outstanding. Source option is OFF by default; no change to existing users.
- The guarded facade detects only accesses routed through it; raw `World` and external/global captures remain unguarded, so any such scheduled system must be audited.
- `DirtyPageSnapshot` does not integrate with `SimSnapshot` (no complete dirty tracking for all pools/resources/Entity state); do not replace the existing rollback mechanism.
- The compare script assumes CMake toolchain and example template availability; any configure/build failures must be fixed and recorded.
- Avoid calling this feature complete until `run_ecs_gameplay_compare.sh` and relevant RTS/FPS lockstep suites pass.

## 2026-11-17 — Additional guarded parallel jobs, failure recovery and race tests (CURRENT)

Additional work implemented on the existing PR #8 **since the earlier RTS integration section**:

- `9b16c2b` — `engine/core/ECS.h`: added `eachExistingRange<T>(begin,end,fn)` and `countExisting<T>()` for non-mutating, non-registering access to stable dense pool ranges.
- `ef1d480` — `engine/experiments/EcsGuardedWorld.h`: `eachAdaptiveWrite<T>(PersistentWorkers&, ExecutionMode, ParallelPolicy, callback)`. Propagates the system's `AccessScope` into each worker, partitions the target component pool into disjoint ranges, and uses `EntityScope` to reject **cross-entity reads/writes of component types declared writable** through the GuardedWorld facade. Read-only components can be read from other entities. It does not prevent retaining or escaping raw references, global state writes, or indirect unsafe writes outside the facade. The target component is passed as `T&` for the current Entity only; no structural changes during jobs.
- `8bf9357` — `tests/ecs_guarded_parallel_test.cpp`: compares checksum across 0/1/3/7 worker counts at 1%,25%,100% density, rejects foreign writable component access, verifies pool usability after thrown `AccessViolation`.
- `8365e0b` and `93958a2` — added guarded parallel test to standalone runner and CTest.
- `6d0d450` — `engine/experiments/EcsSimulationBridge.h`: optional `GuardedGroupOptions{restoreWorldOnFailure=true}` takes a deep World snapshot before each checked group and restores it on failure after worker joins. Disabled by default because of snapshot cost. Only the World is restored, not arbitrary external/module state, RNG or tick-global side effects.
- `b092b2e` — `tests/ecs_guarded_failure_test.cpp`: injects exception after partial writes in checked systems and verifies World checksum/data restored and Simulation tick not advanced, across 0/3/7 workers.
- `e899d50` and `5a1cfff` — registered transactional failure test in standalone suite and CTest.
- `037f864` — `tests/run_ecs_experiment_suite.sh` supports optional `--tsan` (ThreadSanitizer) as a **separate** run, not combined with `--sanitize`. TSan may be unavailable on some Apple/Clang configurations; log toolchain limitations without treating them as passing tests.

### Latest validation status

The previously uploaded regular, ASan/UBSan and benchmark suite **passed**, but these newly committed tests and RTS opt-in changes have **NOT YET been compiled/run**. No claims of RTS/FPS production speedups or TSan race-free status should be made from this step.

### Test commands for user

```sh
git switch feature/ecs-architecture-benchmarks
git pull --ff-only

# Unit and Simulation test suite (includes nested workers, checked parallel, exception rollback):
bash tests/run_ecs_experiment_suite.sh
bash tests/run_ecs_experiment_suite.sh --sanitize

# Optional race detector, compiler/runtime permitting:
bash tests/run_ecs_experiment_suite.sh --tsan

# Real engine game-module comparison (two independent CMake builds;
# RTS checksum + snapshot replay; FPS tests twice):
bash tests/run_ecs_gameplay_compare.sh

# Extended real-game tests (builds aiverse_tests in both configurations):
bash tests/run_ecs_gameplay_compare.sh --full
```

### Remaining engineering risks, not yet complete

1. `GuardedWorld` cannot prevent a captured `World&`, pointer/ref escape, global mutable state, or arbitrary mutation outside its facade; cannot safely parallelize existing RTS/FPS systems by simply declaring access masks. Static/compile-time restrictions or validated encapsulation are needed before default-on.
2. Only RTS transform history preprocessing has a real-game opt-in; FPS gameplay is a monolithic shared-`State` tick with RNG, hitscan, actors and history, and remains sequential pending its own dataflow redesign. The compare script runs FPS regressions but does **not** parallelize FPS gameplay.
3. `GuardedGroupOptions` is an opt-in World-only transaction. Non-World mutations must be prohibited/audited or restored via full Simulation snapshot.
4. `DirtyPageSnapshot` remains a vector-page prototype, not a complete tracked ECS rollback solution. Production rollback continues to use complete snapshots.
5. Real gameplay timing, frame stability, memory, TSan and cross-platform performance require user verification. The CMake configure and gameplay comparison scripts have not been executed by this agent.
6. Merge PR #8 only after normal/sanitized/regression tests, baseline-vs-opt-in RTS checksum equality, FPS tests and real gameplay checks.

### Follow-up fixes after guarded parallel step

- `5af921f`: simplified the caller-lane recursion guard in `PersistentWorkers` to avoid relying on a local class for private static member access; still prohibits nested calls into the same pool.
- `65531e3`: `enable_testing()` now executes before adding ECS CTest targets. Other existing `enable_testing()` calls remain harmless.
- These are **unverified code changes** until the latest full suite and gameplay build comparison run. The previous user-supplied successful suite predates these commits.

## 2026-11-17 — Harden per-entity job API (after gameplay compare setup)

- `e861e74`: `GuardedWorld` now denies structural mutation, shared-resource writes, nested pool traversals and nested adaptive traversal *inside* `eachAdaptiveWrite<T>` callbacks. This is enforced using the executing lane's thread-local `AccessScope::EntityScope` and throws `AccessViolation`, even if a standalone system declaration otherwise allows the operation.
- `be74da3`: `ecs_guarded_parallel_test.cpp` tests these denials with 64 entities and 4 intentionally prohibited operations per Entity, including a recursive call that previously could deadlock.
- These changes are not yet compiled/run on Mac Studio. Re-run both normal and sanitizer suites, then the gameplay compare script. A runtime-checked facade is still not a security sandbox; raw World references, leaked pointers and captured global state remain outside enforcement.

## 2026-11-17 — Gameplay comparison script repair

- `04df6e0` and `d54ae54` extended `run_ecs_gameplay_compare.sh` with network tests and tick timing, but introduced a **malformed CTest regular-expression line** while patching the shell source. This was found by source inspection before any user run.
- `f8ec548` replaced the script with a clean, explicit LF version. It compares baseline/opt-in RTS checksum checkpoints, verifies RTS snapshot restoration in both builds, runs FPS tests in both builds, logs measured tick durations, and optionally builds/runs `aiverse_tests` and `aiverse_net_tests` when passed `--full`.
- Verify shell parsing before running: `bash -n tests/run_ecs_gameplay_compare.sh`.
- **This replacement script has not yet been run**, so any configure, build, CTest or log-format failures must be reported and corrected.

## 2026-11-17 — Continuous integration and non-registering parallel queries

- Commit `e93f2e0`: added `.github/workflows/ecs-experiments.yml` to run normal and ASan/UBSan ECS suites on Linux and macOS. The workflow uses `actions/checkout@v4` and checks shell syntax. It is intended as an automated regression gate, not proof that every real-game configuration has passed.
- Commit `7149c7b`: `PreparedAll`, `eachAdaptive` and `eachParallel` now use `countExisting` and `eachExistingRange` instead of implicitly registering a missing pool through `World::pool<T>()`. This removes unintended structural registry changes from experimental query setup. Existing query order is retained.
- GitHub Actions had no associated pull-request workflow results exposed for commit `7149c7b` when checked. **No CI green status should be claimed.**
- Current status: latest manual full ECS suite from user passed **before** these additions; rerun new branch after obtaining checkout and compare real RTS/FPS tests.

## 2026-11-17 — Deterministic command lane producer safety

- `44ef4ff`: `DeterministicCommands` queues now have per-lane mutexes and single-producer thread ownership until flush/discard. Mixed-thread calls to the same lane throw `std::logic_error` rather than corrupting vector storage or letting interleavings decide gameplay command order. A flush moves a stable batch out, resets producer ownership, then replays lane-major commands; commands enqueued from callbacks remain in the next batch. **Caller must still join all worker producers before flush/discard; this is not a concurrent flush protocol.**
- `5ce3dbb`: `tests/ecs_experiments_test.cpp` validates same-lane mixed-thread rejection, separate lane acceptance, lane-major ordering, and ownership reset on subsequent flush.
- These commits have not yet been built/tested on the user's Mac; previous green suite predates them.

## 2026-11-17 — CI race checks and source validation limitation

- Commit `96327a7`: added a Linux ThreadSanitizer job in `.github/workflows/ecs-experiments.yml` in addition to the Linux/macOS normal and ASan/UBSan matrix. The sanitizer will report data races from tests instrumented by `--tsan`; this does not prove all gameplay systems are safe.
- Attempted native GitHub repository fetch in assistant environment: `git ls-remote https://github.com/chromealex/AIVERSE.git HEAD` failed because `github.com` could not be resolved. Therefore source changes committed through the GitHub connector are **not** locally compiled by this assistant.
- Last checked commit had no associated *PR-triggered* workflow runs reported. Do not claim CI success until actual job results are visible; CI may be disabled or run only on push events.

## 2026-11-17 — Explicit thread linkage for opt-in RTS jobs

- Commit `0049872`: `CMakeLists.txt` now calls `find_package(Threads REQUIRED)` and links `aiverse_game` with `Threads::Threads` only when `AIVERSE_ECS_EXPERIMENTAL_RTS_TRANSFORM_JOBS=ON`, including headless/export builds with `AIVERSE_BUILD_TESTS=OFF`. This avoids relying on incidental transitive thread flags.
- Build/link validation pending macOS and Linux runs. No changes to the default OFF build.

## 2026-11-17 — Serialized-state delta prototype

- `6f6559e`: `engine/experiments/EcsSnapshotDelta.h` defines `SerializedDelta` for **complete serialized binary states**, with 256-byte changed page payloads, base/result FNV-1a checksums, growth/shrink handling, and full-snapshot fallback when page storage costs more. It is encoding of finished bytes, NOT automatic `World` dirty tracking.
- `952f160`: `tests/ecs_snapshot_delta_test.cpp` tests unchanged data, sparse page modifications, binary bytes, length changes, full fallback, and rejection of an incorrect base snapshot.
- `c39e196`, `d4f1b3c`: registered the new delta test in the standalone ECS suite and CTest.
- **Important:** production `SimSnapshot` and `RollbackSession` still store full World snapshots. The new delta is experimental and not yet wired into the rollback ring; replacing the ring without measuring reconstitution latency would risk performance regressions.
- New test has not yet been compiled/run on the user's Mac Studio. Do not claim serialized delta correctness before the updated suite succeeds.

## 2026-11-18 — FPS independent motor jobs + real incremental COW rollback (LATEST)

User requested full FPS parallelization and production incremental rollback. The code below is an **incremental, opt-in implementation**, not a proof that arbitrary authoritative FPS gameplay can run concurrently. All changes below are committed in PR #8 and **have not been compiled or validated on Mac Studio yet**.

### FPS motor stage

- `b559900`: `modules/fps/FpsModule.cpp` adds `AIVERSE_FPS_EXPERIMENTAL_PARALLEL_MOTOR` opt-in two-phase motor path. Only when 32+ actors, deathmatch, no bots, weapons firing, vehicles, melee, systems, royale, streaming or active interactions: precompute per-actor movement concurrently on copies and commit position/velocity/orientation/stance back in original actor order. Other FPS combat/AI/projectiles/authority logic still runs sequentially. This does **NOT** implement full FPS parallel gameplay.
- `39066c5`: `modules/fps/CMakeLists.txt` exposes the feature as OFF by default; links Threads when enabled.
- `c463308` / `37bcffe`: `tests/fps/tests.cpp` generates a 100-player movement-only fixture for 120 ticks with command-driven movement, jump, crouch and sprint, checks snapshot restore and prints `FPS_MOTOR_PARITY_HASH`. This exercises the optional path when enabled and the legacy sequential path otherwise.
- `7273dd2`: `tests/run_ecs_gameplay_compare.sh` now also builds FPS motor OFF/ON and compares the full-tick hash produced by this fixture; FPS combat/authority regressions run in both builds. `--full` adds game and network suites.

### Pool-level COW snapshots in real RollbackSession

- `17094a6`: `engine/core/ECS.h` changes pool ownership from `unique_ptr<IPool>` to `shared_ptr<IPool>`, adding opt-in `World::enableCowSnapshots`. `World::copyFrom` shares pools and singleton resources when COW is enabled; mutable `pool<T>`, `tryGet<T>`, `get<T>`, `resource<T>`, `each` and `destroy` detach shared content before writing. When COW is off, deep-copy behavior is preserved. Component generations/allocator state still copy fully. **Granularity is a whole pool/resource, not per-page COW**.
- `1aec3e1` / `17f6159`: `net::SessionConfig::incrementalWorldSnapshots` (default false) activates COW snapshot copying in the existing `RollbackSession` ring; on session destruction it releases its ring and returns the World to its original mode.
- `348b81a`: introduces `tryGetConst<T>`, `resourceConst<T>`, `eachExistingConst<T>`, `prepareExistingWrite<T>`; read-only queries never trigger detach.
- `d043264`, `2b061ae`, `4a99df7`: GuardedWorld and experimental range jobs detach their writable pools on the caller **before** spawning worker lanes. RTS opt-in jobs prepare the shared Transform pool similarly. This avoids a COW detach race when multiple workers access a newly shared pool.
- `7bdf3c0`: `sharedPoolCount()` and `sharedResourceCount()` diagnostic counters.
- `21d8a14`, `b521203`: isolated World COW snapshot/rollback test with resource/Entity-generation isolation and actual shared-pool diagnostics.
- `0b60d39`: real `RollbackSession` deep-vs-COW checksum/replay test with a delayed remote confirmation.
- `a8bc56e`: guarded parallel update after COW snapshot, verifying untouched snapshot and consistent results for 0/1/3/7 workers.
- `84650dee`, `2b5717f`, `4a4e4b1`, `1bccd07`: added above correctness tests to standalone suite/CTest.
- `d385c22` / `ba350b3` / `c827251`: 12-snapshot deep-vs-COW benchmark at 10k/100k/1m Entities with read-only vs one changed Position per tick. Results pending.
- `2d8add2`: GitHub CI now includes RTS/FPS gameplay comparator and network tests on Ubuntu, plus original normal/sanitizer/TSan lanes.

### Required verification (all new code unverified)

```bash
git switch feature/ecs-architecture-benchmarks
git pull --ff-only
bash -n tests/run_ecs_experiment_suite.sh tests/run_ecs_gameplay_compare.sh
bash tests/run_ecs_experiment_suite.sh
bash tests/run_ecs_experiment_suite.sh --sanitize
bash tests/run_ecs_experiment_suite.sh --bench
bash tests/run_ecs_gameplay_compare.sh --full
# Linux TSan if available: bash tests/run_ecs_experiment_suite.sh --tsan
```

### IMPORTANT — unresolved full-production limitations

1. **FPS is NOT fully parallel.** Only a collision-world-read-only actor motor phase has an opt-in guarded workload gate. Combat, shared actor state, shooting, RNG, bots, vehicles, projectiles, melee and royale remain sequential. A complete safe parallel FPS architecture requires restructuring those into deterministic read-only planning stages and ordered commit phases, with new gameplay-equivalence tests.
2. **COW is pool-level, not page-level**. Changing one element clones the entire pool; FPS State is one large mutable singleton resource, so a full clone may still occur every tick. The new option is OFF by default until memory, tick cost, replay and sanitizer tests are green.
3. **COW bypass risks:** if callers retain mutable references/pointers from before a snapshot, those can mutate shared state without invoking the World write API. Modules must not retain such references across snapshot boundaries. GuardedWorld cannot prevent raw external captures.
4. **Network rollback safety:** COW snapshots carry World components/resources but do not change command ordering, RNG, state checksum or serializer. Ring lifecycle and concurrent jobs need TSan and real network stress validation.
5. **Performance**: no measured FPS speedup or COW snapshot memory savings from these latest changes. Do not enable either experimental mode by default or merge PR #8 before real testing.

### Further COW safety/performance refinement

- `da46368`: `World::destroy` now detaches only shared component pools that actually contain the destroyed Entity. Unrelated pools remain shared, reducing unnecessary whole-pool copies during rollback snapshots.
- This follow-up commit has not been compiled or benchmarked yet; retain the verification gates above.

## 2026-11-18 — Real FPS and RTS COW integration parity (latest step)

- `e45843d`: `tests/fps/tests.cpp` now also runs a **real `fps::Module`** deterministic match in deep mode vs `World::enableCowSnapshots(true)`. It steps 120 ticks with identical movement inputs, compares every tick checksum, restores snapshots at ticks 41 and 81, and verifies a serialized state can be deserialized into a fresh simulation with identical checksum. Unlike the standalone toy network test this exercises actual FPS `State` (large singleton, history, actors, projectiles, etc.). It remains a snapshot parity test rather than complete FPS network rollback / client prediction.
- `cde2c3c`: `apps/headless/main.cpp` accepts `--cow-snapshots` for **testing** only. It enables World pool-COW after game-module setup, preserving default behavior without flag.
- `a03ad3f`: `tests/run_ecs_gameplay_compare.sh` exercises real RTS headless restore twice in each of the sequential/parallel transform builds — once with deep snapshots and once with `--cow-snapshots` — and compares checksum logs.
- These latest changes have NOT been compiled or run in assistant environment; GitHub connector successfully committed all files. The previous user-reported green tests predate them.
- Current COW is still whole-pool/resource detach-on-write (not page dirty tracking) and OFF by default. The default FPS gameplay loop remains sequential except its restricted movement-only opt-in precompute stage. Do not describe full FPS parallelization or page-granular incremental rollback as finished.
- Verification: `bash -n tests/run_ecs_gameplay_compare.sh`, `bash tests/run_ecs_experiment_suite.sh`, `bash tests/run_ecs_gameplay_compare.sh --full` with exact checksum comparison and sanitizer checks before any merge.

## 2026-11-18 — Page-level opt-in COW + wider speculative FPS motor stage (latest)

Following the user's request to finish production incremental rollback and FPS parallel work, the following changes were made to PR #8:

- `99c87d4`: `engine/core/ECS.h` implements optional **256-component element pages** in `Pool<T>`, while leaving the default non-COW Sparse Set's contiguous `sparse_/dense_/data_` storage intact. When COW is enabled, dense/sparse metadata is shared and payload pages use copy-on-write. `World::copyFrom` still shares outer pools for snapshots, but the first mutation now clones only the relevant page, not the entire payload pool. Structural changes clone metadata and touched pages. `Pool<T>` implements `setPaged`, `prepareParallelWrite`, and `sharedPageCount`.
- `2c41d47`: replacing an existing component no longer unnecessarily clones the entire sparse metadata.
- `7118822`: conversion between contiguous and paged storage builds a complete replacement before committing, preventing partial conversion on throwing allocations or component copies.
- `f726be5`: COW snapshot tests now cover 800 entities across four pages, one-page detach, swap removal, generation recycling, nontrivial string components and rollback.
- `2a90016`, `8f2514e`: `World::prepareExistingWriteIds` detaches every component type declared writable by a parallel checked job before scheduling the workers, including all shared pages. This closes a race when a job writes two component types rather than only the iteration driver.
- `bb73a4f`: COW-parallel test now writes both Position and Audit per Entity and compares checksums, snapshot immutability and rollback across worker counts.
- `a9c876b`: FPS opt-in motor stage now speculatively precomputes movement of eligible human-controlled actors even during combat. At the original sequential actor slot it compares that actor with a pre-tick snapshot; if earlier actor interactions changed the actor, the motor is recomputed sequentially. All authoritative damage, RNG, AI, effects and combat application retain their original sequence.
- `89acfd9`, `4ae7443`: FPS tests compare both movement-only and combat (80 ticks with firing) checksums of baseline vs experimental builds.

**Important:** the above newest changes are pushed but *not yet compiled/run*. Strong source-level invariants were reviewed, but no Mac Studio or CI pass has been observed. This is an opt-in 256-component page-COW implementation for generic ECS pools, not for all mutable data: singleton resources (notably FPS State) are still whole-resource COW; their actors/history remain one mutable State resource. Full end-to-end FPS Job Scheduling across AI/shooting/projectiles is not yet implemented.

To validate:
```sh
git switch feature/ecs-architecture-benchmarks && git pull --ff-only
bash -n tests/run_ecs_experiment_suite.sh tests/run_ecs_gameplay_compare.sh
bash tests/run_ecs_experiment_suite.sh
bash tests/run_ecs_experiment_suite.sh --sanitize
bash tests/run_ecs_experiment_suite.sh --bench
bash tests/run_ecs_gameplay_compare.sh --full
```

**Remaining safety gate:** compile and run the above; then verify TSan on Linux and actual FPS/RTS production matches with both feature flags OFF and ON. Any failure must be fixed before this PR merges or either opt-in becomes a default.

## 2026-11-18 — ECS page COW hardening + FPS combat parity coverage (latest)

After the earlier implementation of pool-level COW, the current PR #8 now contains:
- `e45843d`: actual `fps::Module` deep-vs-COW snapshot/serialize/checksum parity for 120 ticks and rollback at two checkpoints in `tests/fps/tests.cpp`.
- `cde2c3c`, `a03ad3f`: `apps/headless/main.cpp` flag `--cow-snapshots` and real RTS gameplay deep-vs-COW checkpoint/restore comparison in `tests/run_ecs_gameplay_compare.sh`.
- `99c87d4`: opt-in 256-element pages in `Pool<T>`; contiguous default storage unchanged. Shared dense/sparse metadata and page-level COW data mean ordinary single-component writes detach only one page, rather than a whole pool. Structure modifications detach metadata/touched pages.
- `2c41d47`, `7118822`: avoid sparse metadata copies on existing component replacement; make conversion between contiguous/paged representations exception-safe.
- `f726be5`: snapshot test spanning 800 Entities and 4 pages, including nontrivial string components, swap-remove and generation reuse.
- `a9c876b`: opt-in FPS speculative parallel motor planning for eligible actors during combat, with immutable actor snapshot and revalidation at sequential commit; if earlier actor interactions modified the actor, move is recalculated sequentially. All damage/RNG/bots/projectiles/modes still use canonical sequential application.
- `89acfd9`, `4ae7443`: FPS fixture exercises movement and firing, and compares full-tick checksum histories of two builds.
- `2a90016`, `8f2514e`: before checked per-entity Jobs, detach **all** component types declared writable, not only leading pool. This avoids shared COW page/pool detachment races across worker lanes.
- `bb73a4f`: test checked parallel writes to Position+Audit per Entity, preserving earlier shared snapshots.
- `0b6b66c`, `97642fc`, `7c52df8`, `9b31824`: profiling-only FPS motor batch and commit counters; compare script now checks the actual opt-in path ran in the enabled build and did not run in default build, instead of passing a false-positive checksum test.

**Latest CI observation:** the GitHub workflow endpoint returned `ECS 2.0 experimental checks` run ID `44180927827` and `CI` run ID `44180927913`, both **queued** for commit `9b318245`. Earlier push CI runs were mostly cancelled by subsequent pushes. No latest build/sanitizer/TSan/gameplay PASS has been verified yet.

**Still not production-complete:** FPS motor planning is parallel but damage, RNG, combat application, AI and global modes remain sequential. This is deliberate to preserve authoritative result ordering. Generic ECS components have 256-element page-level COW, but resources such as FPS `State` remain whole-resource COW and are still expensive on mutation; allocator metadata is not page-granular. Retained writable references across snapshots remain a contract violation, not automatically blocked by the engine.

Recommended final validation:
```sh
git switch feature/ecs-architecture-benchmarks && git pull --ff-only
bash -n tests/run_ecs_experiment_suite.sh tests/run_ecs_gameplay_compare.sh
bash tests/run_ecs_experiment_suite.sh
bash tests/run_ecs_experiment_suite.sh --sanitize
bash tests/run_ecs_experiment_suite.sh --bench
bash tests/run_ecs_gameplay_compare.sh --full
```
Do not merge to main until latest CI tests and real Mac Studio RTS/FPS parity are confirmed.

## 2026-11-18 — Exception safety, opt-in execution coverage and CI gate

- `8c71e55`: paged `Pool<T>::emplace` now unwinds a new payload page and appended component when value construction or dense metadata push fails, so a subsequent insertion can safely retry at both page boundaries and partially filled pages.
- `fc88ef6`: `tests/ecs_cow_exception_test.cpp` injects copy and move exceptions during default-to-COW conversion, single-page insertion, and new-page insertion at offset 256; verifies snapshot isolation, retry and structural recovery.
- `2347eb2`, `8994301`: registers exception safety test in standalone ECS suite and CTest.
- FPS diagnostics `0b6b66c`, `97642fc`, `7c52df8`, `9b31824`: Module exposes profiling-only precompute batch and commit counters. The real FPS comparator now **requires** both movement and combat checksum parity and asserts the enabled parallel-motor build actually schedules and commits results (and disabled build does neither).
- Most recent GitHub workflow inspection: the ECS workflow run `44180927827` for source commit `9b318245` had six queued jobs (Linux+macOS normal/sanitized, Linux TSan, Ubuntu RTS/FPS gameplay). It had **no passing results**; newer source commits remain unverified. Prior main CI run for that commit was cancelled by subsequent pushes.
- These features remain **OFF by default**. Full authoritative FPS AI/combat/projectile parallel execution and page-level COW of the monolithic FPS `State` are still not implemented. Generic ECS component payloads are paged; singleton resources copy whole objects on first write. This is deliberate in order not to destabilize existing gameplay without determinism proof.
- Strict merge gate: run both `bash tests/run_ecs_experiment_suite.sh --sanitize` and `bash tests/run_ecs_gameplay_compare.sh --full` successfully, plus native Linux/macOS CI and TSan, inspect actual tick performance. Do not claim complete or merge on microbenchmarks alone.

### FPS benchmark fixture coverage — latest addition

- `1f9c92d` records wall-clock time for the fixed 100-actor/120-tick FPS motor fixture as `FPS_MOTOR_TICKS_MS`. The benchmark includes command injection, full tick, checksum, and test overhead: it is **not** a clean isolated motor kernel time.
- `9276c6b` validates and prints that timing for each baseline/opt-in build in `tests/run_ecs_gameplay_compare.sh`, alongside parallel batch/commit coverage. This allows judging whether speculative actor copies and state validation cost more than jobs save.
- No timing or checksum claims for these changes until the user or CI has run the updated suite. If the opt-in path is consistently slower in real-frame measurements, retain the default OFF setting or revise its workload threshold.

### FPS parity output fix

- `48a87ca`: fixed C++ string escapes in `tests/fps/tests.cpp` for `FPS_COMBAT_PARITY_HASH`, `FPS_MOTOR_BATCHES`, `FPS_MOTOR_COMMITS` and `FPS_MOTOR_TICKS_MS`. They previously contained escaped backslash characters followed by `n`, which would print literal `\\n` and make two parity records run together. All five FPS fixture outputs now use actual C++ newline escapes.
- This was detected by inspecting source character counts for each output format and corrected before a native run. Latest suite status is **not yet verified**.

## 2026-11-18 — COW transaction safety and FPS motor allocation hardening

Further changes committed in PR #8 after the earlier page-COW stage:

- `b8a0f06`: `World::enableCowSnapshots` now uses a staged, all-or-nothing conversion of *all* component pools, and detaches shared resources before committing the switch. Any throwing component copy or resource clone leaves the old mode usable, without mixed paged/contiguous pool state.
- `f818952`: when leaving COW mode, copies only genuinely shared singleton resources (ownership checked against the original list before staging) rather than needlessly copying every resource.
- `0fb3d6a`, `1db43f1`: expanded `ecs_cow_exception_test.cpp` to cover multi-pool conversion failure, and shared resource exception during disable, with snapshot isolation checks.
- `b9920c3`, `f43be33`: fixed and tested phantom sparse-set membership on a throwing component append in legacy contiguous storage. Sparse map membership is now committed only after the component and dense Entity columns append.
- `d4886a8`: FPS motor speculative snapshots now retain only state needed by `moveActor` (pose/velocity/input/lifecycle and stance flags), instead of deep copying all Actor bags/melee metadata every tick. At canonical actor-order commit, compare those relevant inputs; recompute sequentially on mismatch. This is still an opt-in movement stage, not fully parallel damage/AI.
- Latest GitHub workflow runs for recent commits are **queued**. There is no confirmed latest CI PASS, TSan PASS or real FPS/RTS parity result for these new changes.

Validation remains mandatory:
```sh
git switch feature/ecs-architecture-benchmarks && git pull --ff-only
bash tests/run_ecs_experiment_suite.sh
bash tests/run_ecs_experiment_suite.sh --sanitize
bash tests/run_ecs_experiment_suite.sh --bench
bash tests/run_ecs_gameplay_compare.sh --full
```

Known remaining scope: page COW covers ordinary ECS components, but snapshot allocator vectors and the monolithic FPS State resource are not page-granular. COW misuse through retained raw pointers is not statically prevented. Full FPS AI/combat/projectile parallelism requires deterministic gather/commit refactors. Neither opt-in should be default until actual verified gameplay and race checks.

## 2026-11-18 — Real FPS RollbackSession + non-detaching checksums (latest)

- `dfb6463`: `tests/fps/tests.cpp` now constructs actual FPS `Simulation` + `net::RollbackSession` in both legacy and opt-in COW modes; a remote player's late input at tick 2 arrives after five predicted ticks, triggers replay, and validates full authoritative FPS checksum/tick parity. Complements isolated toy RollbackSession test.
- `2692223`: `fps::Module::hashState` now reads FPS State via `World::resourceConst<State>()` to avoid detaching and deep-copying the shared singleton resource solely to compute a checksum. The Archive writer requires mutable references but `stateIo` is read-only on its `writing()` branch; a local writer alias is used, with that contract explicitly documented.
- `e13d9e9`: FPS real-module test asserts a checksum after `SimSnapshot` leaves `sharedResourceCount()` unchanged. This guards the regression where diagnostics/checksum changed snapshot sharing.
- Previous FPS motor optimization `d4886a8` stores only moveActor-relevant fields in speculative motor snapshots instead of copying each Actor's inventory, melee metadata and bot internals.
- World COW transitions are all-or-nothing across multiple pools/resources (`b8a0f06`, `f818952`), with failure injections (`0fb3d6a`, `1db43f1`). The legacy sparse-set component append is now rolled back on throwing construction (`b9920c3`, `f43be33`).
- **All these recent changes are still unverified by native Mac Studio or GitHub CI execution**. Do not assert pass or merge until tests and real RTS/FPS comparison are green.

### Additional zero-worker recursion regression

- `6ccbdf5`: `PersistentWorkers::parallelFor` now installs/restores the active pool guard also when workerCount=0. Nested submission to the same pool is rejected in sequential mode instead of executing an uncontrolled recursive callback.
- `4dc046e`: added a zero-worker nested submission and reuse regression to `ecs_experiments_test.cpp`.
- Both are newly committed and must be covered by the next manual/CI test run; the preceding full Mac Studio PASS was from an older branch state.

## 2026-11-18 — Generic ECS Parent hierarchy and transform graph (NEW)

User requested efficient nested objects with `Parent` component and derived world transforms. Added an **opt-in generic engine API**, separate from the legacy RTS world-space `Transform` and the existing presentation-only `modules/rts/view/EntityHierarchy.h`.

### Commits and files

- `1cb1aed`, `e651e14`, `4d3a6a6`, `f57e6e1`, `e68a267`, `30d74f5`, `155ce775`: `engine/sim/SceneHierarchy.h` — deterministic parent/children graph, generation-aware cycle detection, dirty subtree propagation through clean ancestors, pending-dirty-roots scheduling, top-down iterative world-transform calculation and repair of invalid/cyclic parent links on rebuild.
- `ba0705c`: common ECS component definitions in `engine/sim/CommonComponents.h`: `Parent` (`Entity` handle), `Rotation` (Fixed quaternion), `LocalTransform` and `WorldTransform` (cached 3x4 fixed affine). Existing `Transform` not changed.
- `e68a267`: affine composition preserves shear from nested rotations and nonuniform scale; `WorldTransform::affine` is authoritative, quaternion/scale fields convenience only under shear.
- `263d6e1`: `engine/render/SceneHierarchyRender.h` converts fixed affine to `render::mat4`, subtracting render origin in fixed point before float conversion.
- `3ceb6c1`, `74c1b54`: `Simulation::addStateRestoredHook` and callbacks after `loadState` / successful `deserialize` enable rebuilding non-authoritative hierarchy caches following rollback.
- `b146673`: `engine/sim/SceneHierarchySystem.h::installSceneHierarchy` registers the hierarchy update as an optional `Simulation` system and installs restore hook.
- `1bf73ce`, `ca52898`, `3051015`, `43271c2`, `da0e3d2`: `tests/ecs_scene_hierarchy_test.cpp` covers nested transforms, rotations/scales, sheared affine, dirty-leaf tracking, cycle rejection, stale generations, reparent, detach, deep 3000-level chain, World snapshots and COW rollback.
- `60c4e3c`: `tests/ecs_scene_hierarchy_simulation_test.cpp` verifies actual Simulation integration, rendering matrix and automatic SimSnapshot restore.
- `c6ede97`, `2e4363e`: `tests/ecs_scene_hierarchy_bench.cpp` benchmarks 10k and 100k entities for initial/full, clean and sparse-dirty frames.
- `96e0a79`, `94fb217`: registered hierarchy correctness tests in standalone ECS suite and CTest.
- `8815507`: `apps/editor/core/WorldInspect.cpp` exposes `Parent`, `LocalTransform` and derived `WorldTransform` in ECS details.
- `266c514`: `docs/ecs-scene-hierarchy.md` documents native C++ usage, simulation integration, lifecycle, render origin, rollback, performance and known limitations.
- `7cf42f3`: `AGENTS.md` links hierarchy documentation for future agent work.

### Design decision

Use *components* for authoritative Parent + local transform, a separate cached children index for structural relations and a derived WorldTransform component. Do not maintain expensive mutable `Children` vectors on arbitrary ECS components; maintain sorted adjacency in `SceneHierarchy`.

- Dirty changes mark affected descendants and ancestor path.
- `pendingRoots_` means zero hierarchy traversal when no node changed.
- Updates traverse parent-first and prune clean subtrees; no recursion.
- Opt-in system ordering chooses when to commit world transforms.
- No float simulation; renderer receives fixed-origin-relative matrices.

### Verification / remaining scope

New hierarchy changes have **not yet passed compiler or runtime checks on the user's Mac**. The source was committed through the GitHub connector. Test using:
```bash
git switch feature/ecs-architecture-benchmarks && git pull --ff-only
bash tests/run_ecs_experiment_suite.sh
bash tests/run_ecs_experiment_suite.sh --sanitize
bash tests/run_ecs_experiment_suite.sh --bench
bash tests/run_ecs_gameplay_compare.sh --full
```
Explicit lifecycle contract: callers use `SceneHierarchy::attach/setParent/setLocal/detach`, or `invalidate` / `rebuild` after raw ECS writes/deletes; the hierarchy does **not** intercept mutations of `Parent`/ `LocalTransform` through raw `World`. After restoring any snapshot, `rebuild` is required; `installSceneHierarchy` installs callbacks for Simulation save/load. Custom game modules must serialize new authoritative component pools themselves. No automatic editor drag/drop reparent action has been implemented; Inspector read-only visibility is provided. The hierarchy updater is sequential but dirty-pruned; a parallel depth-wave evaluator is a future optimization only if measured necessary. Do not claim production readiness until tests and real scene benchmarks are confirmed.

## 2026-11-18 — Coordination with parallel RTS/FPS universal ECS migration (user instruction)

**The user confirmed another agent is actively migrating RTS and FPS onto a shared universal ECS architecture.** This PR's job is to supply **genre-independent core primitives**, not to simultaneously replace the same gameplay systems.

Integration contract:

- `Parent`, `LocalTransform`, `WorldTransform`, `SceneHierarchy` and `installSceneHierarchy` are generic ECS facilities under `engine/sim`, exposed to native projects via `CommonComponents.h`.
- Existing `Transform` continues to mean legacy world-space pose until the migrating agent explicitly performs its own transition. Do not change or reinterpret it in this branch.
- The new hierarchy is opt-in; do not automatically attach old RTS/FPS entities, overwrite their transforms, or duplicate their simulation systems. The migrating agent should replace old usage explicitly.
- `Parent` stores generation-safe `Entity`; `LocalTransform` is authoritative; `WorldTransform::affine` is derived from hierarchy and used as the exact matrix for nested nonuniform scales.
- Structural parent/reparent/destruction changes must go through hierarchy API or trigger index rebuild; rollback invokes restored-state hooks when the system is installed.
- Register hierarchy update as a deterministic barrier between transform writers and world-space readers. Do not let other jobs mutate hierarchy while it evaluates.
- Migration agent owns RTS/FPS refactor and genre system access declarations; this PR owns shared ECS primitives, dirty-tree evaluation, validation, tests and documentation. Avoid concurrent edits to `modules/rts`, `modules/fps`, and editor genre-specific migration files from this branch.
- For integration, the other agent should cherry-pick or merge the **verified** generic primitives after ECS tests pass rather than independently rewriting hierarchy components.
- The user's exact active migration branch is **not known**. GitHub branch search for `universal` currently returned `codex/universal-projectiles`, `feature/universal-entity-inspector-fps`, `feature/universal-model-batching-policy`; search for `ecs` returned this experimental branch. Do not assume which one holds their in-progress changes.

This coordination rule supersedes earlier suggestions in this document to directly migrate RTS/FPS gameplay here. Retain the existing experimental opt-in RTS/FPS changes, but avoid expanding them in ways that conflict with parallel migration. Updated after the user's explicit clarification.

## Outstanding work and risks (priority order)

1. **Validation accomplished:** five correctness suites and normal/sanitizer/benchmark runs passed on Mac Studio; rerun after any further change.
2. **Enforce access contracts:** GuardedWorld now validates calls routed through it, but raw World references, retained pointers and external captures bypass checks; harden safe lifecycle and declarations before production scheduling.
3. **Deterministic job commands**: current `DeterministicCommands` merges lane-local commands, but order-dependent operations must use stable semantic lane IDs independent of OS scheduling; validate against replay and rollback.
4. **Wave executor safety**: a rejected/misdeclared dependency can race. Add robust instrumentation and integration tests for write/write, resource/RNG and structural conflicts; avoid concurrent writes to shared event queues.
5. **Production integration**: add explicit opt-in system registration and keep existing sequential path as fallback; do not blanket-parallelize RTS modules.
6. **Snapshots**: `DirtyPageSnapshot` only works for explicitly-marked trivially copyable vector pages and is not a full rollback replacement. Require tracking of all mutations, entity allocator state and nontrivial components.
7. **Perf**: evaluate scheduling overhead and real gameplay work, not only independent microbenchmarks. Tune fixed `ParallelPolicy{8192,2048}` for system costs and platform worker topology.
8. **Regression**: run existing unit tests, RTS/FPS simulations, replay/lockstep and cross-worker checksum comparisons before merging.
9. **Docs discipline**: update this document in the same PR after each meaningful code step with commit, exact changed files, verification status and next actions.

## Operating rule for the next agent

Work on the current feature branch and existing PR #8. Do not merge to main without explicit approval and full verification. Every implementation step must update `docs/ecs-agent-handoff.md` in the same branch. Report specific confirmed outcomes separately from assumptions.


## Cross-branch integration into universal Editor PR #7 (2026-11-18)

- Original source snapshot: `feature/ecs-architecture-benchmarks` commit `b75b873b1b2091a4a4a9bb386d39f3237f5d2585`. It remains independent; **PR #8 was NOT merged into main**.
- Git blob-exact core and experiments/test import commit in PR #7: `f4cf94d1` (42 files). CMake test and off-by-default RTS experiment configuration: `5866850a`.
- FPS experimental motor planning merged alongside ongoing FPS ECS actor mirror, rather than replacing either implementation: `3cf9aee4`, `48fae9dd`, `a272d1c4`. Existing FPS save v9 and synchronous gameplay authority preserved.
- Ported RTS opt-in preprocessing, headless COW flag, FPS comparative tests, and Scene Hierarchy/WorldInspect support, with intentional overlap resolution. See `docs/editor-unification-handoff.md` for exact commit list and audit.
- The opt-in ECS mechanisms remain **unverified on the integrated branch**. Latest GitHub Actions for `e2c12d0d` were all queued (CI, Studio validation and ECS experiments). Do not enable FPS/RTS experimental parallelism by default or call a COW/rollback optimization production-safe without real builds, replay parity, sanitizer, and gameplay regression checks.
- Two branches may still receive independent updates. Future cherry-picks should compare source blob histories and resolve overlapping FPS/editor changes manually; never overwrite the universal Editor's runtime entity provider or `syncActorEntities` with a stale FPS `FpsModule` copy.

## Review-fix checkpoint: ECS core + scene residency (2026-11-21)

Branch `feature/aiv20261121_fix_ecs_scene` (base main `973b32970`). Full
record: `docs/agent-handoffs/review-fixes-ecs-scene-2026-11-21.md`.
- `engine/core/ECS.h` **unchanged**. Review claim that
  `World::prepareExistingWrite<T>()` writes through a pool shared with a COW
  snapshot is NOT A BUG: the non-const `poolIfExists<T>()` already clones a
  shared pool (`use_count()!=1`) and `Pool::prepareParallelWrite` detaches
  pages. Regression block added to `tests/ecs_cow_exception_test.cpp`
  (`abb8983`). Note: the comment in `engine/world/TerraWeaveShorelineRaster.h`
  ("prepareExistingWrite ALONE does not detach pool ownership") is stale.
- Scene residency/rollback (`SceneLoadCoordinator`, `SceneResourceBackend`):
  rollback now diffs resident leases instead of releasing all; restored
  Loading records get full jobs; Cancelled backend entries are resurrected
  by `pin`; terminal jobs are pruned (`21d763a`, `238d57d`).
- New generic persistence hook: `PersistentEcsRegistry::addLoadValidator` /
  `Simulation::addPersistentLoadValidator` (setup-only, staged-World check,
  no save-format/hash change); used by `registerSharedPersistence` (`7fbb192`).
- Verification actually run: standalone g++ builds of the ECS/scene tests
  listed in the handoff, all PASS (plus ASan/UBSan for the two scene tests).
  Full CMake build, CTest, RTS/FPS headless matches: **NOT RUN** (builds only
  on KINGSTON). Deterministic simulation code paths are unchanged except the
  load-time validator, which only rejects invalid saves.
