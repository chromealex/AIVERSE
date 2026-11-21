# TerraWeave QA — PHYSIS Stage100/101 Place-performance backport

Date: 2026-11-19.
Branch: `feature/terraweave` (Draft PR #24).
Feature work is frozen; this is a **cross-branch native QA/performance fix**,
not a TerraWeave Stage71 gameplay feature.

## Input symptom from current TerraWeave native full CTest

User submitted failure of CTest `place`, first case
`sixteen_clients_over_4g_converge_predict_and_late_join`:
- 523 Box3D bodies, 16 simulated 4G clients, one late joiner.
- Server tick avg **8.256ms**, worst **25.319ms**;
  original strict `CHECK(tickAvg < 5.0)` FAILED.
- Resting-body worst discrepancy 0.0069m, awake mean 0.022m,
  missing/extra zero, late-join discrepancy 0.0069m.
- Walking probe and subsequent tests' gameplay checks passed;
  network/replication integrity was not the observed failure.
- The whole `place` CTest ran for roughly 317s.

The user noted **PHYSIS already solved a comparable problem**.
Source actually located at
`integration/physis-terraweave-complete-20261119`,
`docs/agent-handoffs/physis-stage100-place-replica-perf-2026-11-19.md`
and `physis-stage101-place-perf-native-green-2026-11-19.md`.

## Grounded prior PHYSIS measured result (not THIS branch)

In PHYSIS Stage101, the user reported the identical strict 30s
virtual Place scenario **PASS 11/11**, real ~13.4s, on an optimized
Debug build (`-DAIVERSE_PLACE_FAST_DEBUG=ON`). It measured:
- 523 bodies, 17 clients after late join,
- **0.789ms** average server tick, **1.905ms** worst,
- 0.0069m resting-body worst, 0.022m awake mean, missing/extra 0,
- per-tick phases: **poll 0.019 + players 0.128 +
  Box3D 0.301 + sync 0.050 + broadcast 0.290ms**.
- The previous PHYSIS unoptimized Debug performance was
  8.362ms average, 19.817ms worst.
- **Caveat:** the speedup is the COMBINED effect of new batch field
  APIs, allocation-free viewer and optimized `-O2` Debug hot-path
  compilation. It was NOT a controlled A/B isolating algorithmic
  improvement. Those measurements are from the *other*
  integration branch, not yet from this TerraWeave backport.

## Exact backported changes into feature/terraweave

1. `ad62c4ee79a94623f270318781bebd0b4829bc45` —
   `engine/net/replica/Replica.h`: public
   `FieldUpdate{int field,Value value}`,
   `Server::setFields(ObjectId,std::span<const FieldUpdate>)` and
   `Server::setViewer(uint8_t,std::array<float,3>)`, including
   C++20 `<span>`.
2. `e5e683ae3d3228b68cd5e256245574bf17499818` —
   `engine/net/replica/ReplicaServer.cpp`: copy exactly
   PHYSIS's `setResolvedField` shared low-level field path,
   batch per object/schema-layout lookup, and single-camera viewer
   update. Old `set` forwards through the same validated field
   quantization; duplicate/invalid field handling remains ordered.
   This does NOT change the serialized replica wire/schema.
3. `2231a8fbb07fc5ee73ba00d81fd68ec51170331e` —
   `apps/place/PlaceServer.cpp`: port PHYSIS's identical four-field
   body, three-field away-character and five-field connected-character
   stack-backed updates. Replace per-client temporary viewer vector
   with `setViewer`.
4. `c2e64a790d455f083902a896298a399886578115` —
   `CMakeLists.txt`: add OFF-by-default option
   `AIVERSE_PLACE_FAST_DEBUG`. When explicitly ON,
   compiles `aiverse_place`, `aiverse_net_replica`,
   `aiverse_net`, `aiverse_physics`, `box3d` with
   `-O2` only for Debug on AppleClang/Clang/GNU.
   Does NOT set `NDEBUG`, alter assertions, tick rates,
   4G packet profiles or the original 5ms threshold.
5. `21925cd1d82e34a526197a16098a486e27644f83` —
   NEW `tests/replica/replica_batch_tests.cpp`: 240-frame
   PHYSIS-original legacy-vs-batch replica wire/field parity.
   Existing `aiverse_replica_tests` glob picks it up.
6. `1bc142fbdc6f731a2dca9a900f5db38a9eaf3d11` —
   `CMakeLists.txt` sets CTest `place` `RUN_SERIAL TRUE`,
   so `ctest --parallel 2` does not co-schedule CPU-heavy tests
   with the strict measured wall-clock tick acceptance.
7. `83822f6b313990fef1c2c1eee74f8dbe830a922a` —
   `apps/place/PlaceServer.h` opt-in
   `setTickPhaseProfiling`, five timing sums and sample count;
   `resetTickTiming` clears phase accumulators.
8. `d832835e48759eea528eda9c51695a63174b094d` —
   `apps/place/PlaceServer.cpp`: same exact PHYSIS Stage99
   five phases: poll/players/Box3D/sync/broadcast around
   ONE normal authoritative server tick, disabled by default.
9. `171d34eb6dbf1a38f49eedf488295bbadcdff2df` and
   `643fd0a85b1d44f6fd4704294d498dea795b4b75` —
   `tests/place/place_tests.cpp`: 5s virtual progress and actual
   five-phase aggregates for the original Place load case.
   The latter fixes proper C++ `\n` escapes for readable output.
   Original `CHECK(tickAvg < 5.0)` and all convergence/late join,
   4G, bodies, physics-step tests remain untouched.
10. `237be4995838e4e70a20788e84c0e608239713ef` —
    NEW `.github/workflows/terraweave-place-perf.yml`:
    isolated Linux GCC and macOS Clang optimized-Debug
    `aiverse_replica_tests` parity and exact single
    `sixteen_clients_over_4g_converge_predict_and_late_join`
    scenario, logs uploaded even on failure. Uses separate
    concurrency group from the existing full native QA.

## Actual validation and honest status

- Inspected real PHYSIS source changes, exact Stage100/Stage101
  handoffs, unchanged actual TerraWeave `place` failure and
  underlying `Replica.h`/`ReplicaServer.cpp`/Place files.
- All backport source files were successfully written via connected
  GitHub and re-fetched by blob SHA; flags and callsites exist.
- [Place replica performance parity GitHub Action
  #44608910877](https://github.com/chromealex/AIVERSE/actions/runs/44608910877)
  was **queued** when checked. Its Linux and macOS real
  compile/CTest/Place measurements are **NOT YET AVAILABLE**.
- Do not claim that the 0.789ms PHYSIS measurement was reproduced
  on this branch. The user must compile and test the backport
  or inspect completed GitHub Actions logs.
- PR #24 stays Draft; `main` and PR #40 remain untouched.
  No cherry-pick or merge from the large PHYSIS integration
  branch was performed; only narrow compatible hot-path changes
  and a parity test were explicitly copied.

## Exact Mac Studio performance acceptance (isolate Debug build)

If `git status --short` is clean:

```bash
cd ~/Projects/AIVERSE-terraweave-qa-fix
git fetch origin feature/terraweave
git switch --detach origin/feature/terraweave

cmake -S . -B build/terraweave-place-perf \
  -DCMAKE_BUILD_TYPE=Debug \
  -DAIVERSE_BUILD_TESTS=ON \
  -DAIVERSE_ENABLE_PHYSICS=ON \
  -DAIVERSE_BUILD_PHYSICS_DEMO=OFF \
  -DAIVERSE_PLACE_FAST_DEBUG=ON

set -euo pipefail
cmake --build build/terraweave-place-perf \
  --target aiverse_replica_tests aiverse_place_tests --parallel 4

ctest --test-dir build/terraweave-place-perf \
  -R '^aiverse_replica_tests$' --output-on-failure --parallel 1

./build/terraweave-place-perf/aiverse_place_tests \
  examples/place/scene.json \
  sixteen_clients_over_4g_converge_predict_and_late_join
```

Native acceptance: the original strict `tickAvg < 5.0`,
late-join/convergence, predicted corrections and
240-frame replica byte/field parity MUST PASS.

After a focused pass, run full all-target build and unfiltered
CTest once in a suitable QA build configured with
`-DAIVERSE_PLACE_FAST_DEBUG=ON`; since `place` is now
`RUN_SERIAL`, a parallel CTest will not skew its wall-clock
performance with other tests. If still over 5ms, use
`[place phases]` to identify actual cost, do NOT weaken
the original workload or threshold.

For a checkout with uncommitted agent work, do not execute a
destructive `git switch --detach` in place: create a new
clean worktree from the fetched feature SHA.

## Next owner action

Review completed workflow jobs and inspect first real compile
diagnostics if any. Confirm optimized Debug Place and replica
parity on Mac Studio. Follow with the original full 136-test
TerraWeave/Box3D/NEREID/PHYSIS native run. Keep exact
phase timings, commit SHA, and real CTest outcomes in a separate
QA handoff before PR #24 readiness.
