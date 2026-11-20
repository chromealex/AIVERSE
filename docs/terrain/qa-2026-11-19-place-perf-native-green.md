# TerraWeave QA — Mac Studio Place perf and replica parity PASSED

Date: 2026-11-19. Source branch: `feature/terraweave`.
This documents real USER-PROVIDED native results for the selective
PHYSIS Stage100/101 Place hot-path backport. This is QA, not Stage71.
Previous agent handoff:
[PHYSIS Place performance source backport](qa-2026-11-19-physis-place-performance-backport.md).

## Confirmed Mac Studio results

The user ran in the QA worktree
`~/Projects/AIVERSE-terraweave-qa-fix`, with build directory
`build/terraweave-place-perf` from the optimized Debug command
in the previous handoff. The new pasted output DOES NOT contain
`git rev-parse HEAD`, `CMakeCache.txt` or a source commit hash;
do not assert the exact tested SHA without checking locally.

### Replica regression suite

```text
Start 16: aiverse_replica_tests
1/1 Test #16: aiverse_replica_tests ............ Passed 2.35 sec
100% tests passed, 0 tests failed out of 1
Total Test time (real) = 2.36 sec
```

This CTest executable includes the Stage100 240-frame
`batched_fields_preserve_legacy_replica_wire_and_values` regression
when built from the current feature HEAD. This is one green
**CTest executable**, not the full native test suite.

### Strict original Place performance acceptance

User-provided direct command ran the unchanged
`sixteen_clients_over_4g_converge_predict_and_late_join`
scenario with 16 simulated 4G clients, late 17th client and
523 Box3D bodies:

```text
[place progress] virtual 30.0 s | server avg 0.766 ms worst 1.729 ms | clients 17 | bodies 523
[place phases] poll 0.017 + players 0.123 + Box3D 0.292 + sync 0.048 + broadcast 0.285 ms/tick (1740 samples)
[place wall] 30s virtual load + 6s convergence: 12.7 s

523 bodies, tick avg 0.766 ms worst 1.729 ms,
31954 B/s per client
resting bodies worst 0.0069 m,
127 awake mean 0.022 m (missing 0, extra 0)
late joiner: worst 0.0069 m, missing 0, extra 0
494 scene bodies moved > 0.5 m
179 pushes, 22 boxes spawned
probe patrolled, 601 reconciles,
corrections > 1 cm 4 (0 while patrolling),
max while patrolling 0.0058 m, mean 0.00292 m
ok
11 checks, 0 failed
```

Intermediate 5s cumulative averages (server ms):
`0.145, 0.290, 0.493, 0.646, 0.708, 0.766`.
The five phase averages at 30s sum to 0.765 ms/tick,
matching the measured total 0.766 ms/tick. No phase
dominated an unexpected share: per-tick Box3D 0.292ms and
replica tick/broadcast 0.285ms were the largest components.

**Previously failed TerraWeave load benchmark** on the same
reported 523-body / 4G workload: 8.256 ms mean,
25.319 ms worst, `tickAvg < 5.0` FAILED.
**New user-provided result:** 0.766 ms mean,
1.729 ms worst; `tickAvg < 5.0` PASSED.
Mean is approximately **10.78× lower**.
The earlier Mac workload's test result is not an exact
controlled A/B against one isolated compiler setting:
the PHYSIS Stage100 backport included batched
`Server::setFields`, allocation-free `setViewer` and
explicit `AIVERSE_PLACE_FAST_DEBUG=ON`, which passes `-O2`
to the selected Place/network/Box3D targets in Debug
while preserving assertions. Therefore do not claim
this improvement is solely from an algorithmic change
or that an ordinary unoptimized `-O0` Debug build
has met the 5ms budget.

Original 5ms limit, physical bodies, workload,
4G simulated connection, late join, convergence and
prediction checks were **not weakened or reduced**.

## What is still not proven

- All eight `place` cases in the CTest `place` executable
  were not rerun as part of this latest focused direct invocation:
  only the named 16-client scenario ran and passed.
- The complete registered native CTest suite
  (previously 136 entries in the user's full run) is NOT
  yet confirmed all-green at this branch head.
- Actual integrated TerraWeave+NEREID+PHYSIS+Box3D lifecycle,
  4m nutrient v3 saves and rollback, forest/GPU Metal quality,
  and integration with diverged `main` remain pending.
- This source was committed on GitHub previously; today's new
  result does not require further gameplay code. The native
  logs are user-provided, not reproduced by the assistant's
  local container or GitHub Actions.
- PR #24 remains Draft until full quality gates pass.

## Acceptance steps for next agent — full build, one full CTest

Use a clean QA worktree, capture its SHA and preserve logs.
The `build/terraweave-place-perf` directory was configured for
`CMAKE_BUILD_TYPE=Debug` and `AIVERSE_PLACE_FAST_DEBUG=ON`,
but until the local CMake cache is inspected do not assume those
flags survived other reconfigurations:

```bash
cd ~/Projects/AIVERSE-terraweave-qa-fix
git status --short
git rev-parse HEAD
git fetch origin feature/terraweave
# Only in a clean detached QA worktree:
git switch --detach origin/feature/terraweave

cmake -S . -B build/terraweave-place-perf \
  -DCMAKE_BUILD_TYPE=Debug \
  -DAIVERSE_BUILD_TESTS=ON \
  -DAIVERSE_ENABLE_PHYSICS=ON \
  -DAIVERSE_BUILD_PHYSICS_DEMO=OFF \
  -DAIVERSE_PLACE_FAST_DEBUG=ON

set -euo pipefail
cmake --build build/terraweave-place-perf --parallel 4 \
  2>&1 | tee build/terraweave-place-perf/build-all.log
ctest --test-dir build/terraweave-place-perf \
  --output-on-failure --parallel 2 \
  2>&1 | tee build/terraweave-place-perf/ctest-full.log
```

The build must finish before full CTest, otherwise missing
executables produce misleading Not Run failures. The
`place` CTest has `RUN_SERIAL TRUE` and retains the original
strict wall-clock criterion; keep this under the optimized
Debug toolchain for performance acceptance.

If `git status --short` shows local edits, DO NOT switch
the active worktree or overwrite another agent's work.
Create a fresh isolated clean worktree from the fetched SHA.
Record all actual failures with their first diagnostics
and update another QA handoff after fixes. Do not merge
PR #24 until full native and visual/integration QA.
