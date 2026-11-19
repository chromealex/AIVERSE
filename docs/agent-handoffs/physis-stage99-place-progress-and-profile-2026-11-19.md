# PHYSIS Stage 99 — Place live progress and phase profiling for repeatable 8.3ms load-test failure

Date: 2026-11-19
Repository: `chromealex/AIVERSE`
Branch: `integration/physis-terraweave-complete-20261119`
Draft PR: https://github.com/chromealex/AIVERSE/pull/40
Previous handoff: `docs/agent-handoffs/physis-stage98-full-165-of-167-skill-audit-2026-11-19.md`.

## Actual Mac Studio result before this stage

After fetching Stage98 commit `d3d38523f57627c49474aa8b0f54a58aaab97c3c`,
the user ran `python3 tools/skills/audit.py --strict` from
`~/Projects/AIVERSE-physis-terra-final`, which printed
**0 error(s), 0 warning(s)**.

The isolated, unchanged `place` CTest (parallel=1) still failed after
**319.53 s**: 16 clients over simulated 4G, 523 bodies, 30 seconds of
virtual load + 6 seconds settling, **server tick average 8.362ms**
(worst 19.817ms) vs the **unchanged `tickAvg < 5.0` assertion**
at `tests/place/place_tests.cpp:139` in the older source. 52 checks,
only the speed budget failed; all other functional convergence/security/
prediction scenarios continued to report PASS.

This repeated result rules out a single accidental full-suite contention as
the entire explanation but does NOT yet determine whether the primary cost is
Box3D, actor movement, interest management or replication. The benchmark
is a wall-clock measurement in a macOS CMake **Debug** build. No separate
phase profile existed in the native evidence.

## Why users saw no progress

The existing `tests/place/place_tests.cpp` registered 8 test scenarios
and emitted one line **before** each. Its long scenarios ran 30s at 60
simulated ticks/sec in multiple modes (full predicted bodies, stand-ins and
40s moving-platform rider) without interior output. Depending on the device
and Debug execution, the CTest `place` binary ran for more than 5 min.
CTest's default `--output-on-failure` buffers child stdout until failure;
adding printf without running `-V` would still not show live updates.

## Source changes, API and ownership

1. `apps/place/PlaceServer.h`: opt-in
   `PlaceServer::setTickPhaseProfiling(bool)` and
   `PlaceServerStats::TickPhases` with 5 cumulative wall-clock
   fields and `samples`. Disabled by default in gameplay; timing
   reset clears both the original tick averages and the optional phase sums.
   Code commit `e257aed05e390b016d31ba4d4ebda69478bb583c`.
2. `apps/place/PlaceServer.cpp`: `PlaceServer::update()` samples
   5 phases **only when enabled**: network poll; player input, physical
   character movement, kinematic motion and expiry; Box3D world step;
   replication body and character sync; replica tick and broadcast.
   The original total server tick measurement remains in place and
   includes all these stages. No gameplay, network timing parameters,
   physical step or packet budgets changed.
   Commit `a5d1aa25554e83eae2370fb81759fd50802d96f9`.
3. `tests/place/place_tests.cpp`: every top-level `place` scenario
   emits `[place n/N] START` and `PASS/FAIL ... wall N s`, calling
   `fflush(stdout)`. In the 30s 16-client stress, 30s full-prediction
   and stand-in modes, 6s quiet convergence and 40s moving-platform
   scenarios, emit `[place progress]` every 5 simulated seconds:
   current scenario/mode, virtual elapsed/target, REAL elapsed wall time,
   server average/worst tick, client count and current body count.
   When profiling is enabled, the following `[place phases]` line
   displays avg poll/players/Box3D/sync/broadcast ms per sampled tick.
   The test retains the original workload, deterministic simulated
   network, physics entities, convergence checks and **5ms threshold**.
   The existing 500-body settlement scenario now verifies exactly
   600 profiling samples, nonnegative phase times and sums not exceeding
   total tick time. Code commits
   `9a8dd1e7b47c887baead0d53bba12ef880649bee` and
   `318429aa872e2b22f51ca4a79a68e59f2aad3be8`.
4. `tools/physis-terraweave-final-validation.sh` accepts optional
   `AIVERSE_CTEST_VERBOSE=1` to use one **unfiltered**
   `ctest --verbose` instead of default buffered
   `--output-on-failure`. It still configures full Debug Box3D,
   builds all targets and runs exactly one unfiltered CTest; 
   the only changed behavior is stdout visibility.
   Commit `3bfffe944cb55053c6c9877230b816bb1099ba1e`.

No source fixes to the `place` hot path are claimed until phase
profiling supplies actual measurements. No CTest threshold or client
load reduction, no genre-specific change to PHYSIS, TerraWeave or
NEREID. Changes are isolated on the same integration branch.

## Actual tests for this new stage

**NOT RUN** after these new instrumented changes (no local Mac tool access).
Existing Stage98 native `skill_audit` **PASS 0/0** is prior evidence.
The existing `place` benchmark remains FAIL in that native
report; the new per-phase values are not known yet.

## Exact next diagnostic (shows live progress)

```bash
cd ~/Projects/AIVERSE-physis-terra-final
git status --short
git fetch origin
git switch --detach origin/integration/physis-terraweave-complete-20261119
cmake --build build/physis-terraweave-onepass --config Debug \
  --target aiverse_place_tests --parallel 4
ctest --test-dir build/physis-terraweave-onepass --config Debug \
  -R '^place$' --verbose --parallel 1
```

The `--verbose` flag is important: default CTest captures stdout until
the test finishes. Alternatively, for direct terminal streaming use:

```bash
./build/physis-terraweave-onepass/aiverse_place_tests \
  examples/place/scene.json
```

For only the performance-critical scenario, the test's existing
optional third argument supports:

```bash
./build/physis-terraweave-onepass/aiverse_place_tests \
  examples/place/scene.json sixteen_clients_over_4g_converge_predict_and_late_join
```

After observing the distribution of measured phase times, optimize the
largest real bottleneck without weakening budget, load or functional
checks. Then confirm the unchanged load scenario `tickAvg < 5.0`
on Mac and finish one FULL unfiltered acceptance using
`AIVERSE_CTEST_VERBOSE=1 bash tools/physis-terraweave-final-validation.sh`
only after source is ready. Record exact evidence and Stage100 handoff;
do not merge into `main` before 167/167 passes.
