# PHYSIS Stage 75 — AIVERSE terminal build monitor and isolated Place performance evidence

Date: 2026-11-19
Repository: `chromealex/AIVERSE`
Working branch: `feature/physis-nature-simulation`
PR #25: https://github.com/chromealex/AIVERSE/pull/25 (DRAFT)
Previous handoff: `docs/agent-handoffs/physis-stage74-full-ctest-skill-place-2026-11-19.md`
Code commit: `d83716dcd835df40c7861828a92064e961a63bc6`
Documentation commit: inspect `git log -1 -- docs/agent-handoffs/physis-stage75-progress-monitor-place-2026-11-19.md`.

## Trigger and user-visible issue

The previous all-repository CTest had 148/150 PASS. Stage74 fixed
the PHYSIS `skill_audit` path and made performance-sensitive
`place` `RUN_SERIAL`. On the owner's macOS machine the user
re-ran only those two tests:

```text
1/2 Test #99: place ..... FAILED 309.14 sec
523 bodies, tick avg 8.110 ms worst 19.245 ms
52 checks, 1 failed
FAIL tests/place/place_tests.cpp:139: tickAvg < 5.0

2/2 Test #105: skill_audit ...... Passed 0.42 sec
50% tests passed, 1 tests failed out of 2
```

All 51 other Place scenario checks passed: network/replica
convergence, late join, correction measurements, physics
interactions, input rate limits and bad-message handling.

**Important correction to Stage74 hypothesis:** isolating CTest
does NOT bring `place` below its 5ms budget (8.204ms parallel,
8.110ms serial). Do not pretend the earlier mitigation solved the
performance problem. The cause must be investigated in a native
optimized benchmark.

The user also requested an actual live terminal visualization
during long builds/tests, replacing the frozen-looking `Start
99: place` line. The tool below addresses this visibility issue,
NOT the simulation performance issue.

## Implemented

- `tools/aiverse-progress.py`: universal, dependency-free Python
  3.8+ CLI for CMake `configure`, `build`, `test` or `all`.
- Interactive ANSI terminal view: stage, total/stage elapsed,
  build directory, per-target CMake output percentage (explicitly
  NOT global progress), CTest total/done/failed, actively running
  test names and their elapsed time, recent output, warnings/errors
  and time since last output. Repaints ~3/s without allowing
  long silent processes to look frozen.
- Captures complete stdout/stderr to timestamped logs and writes
  `summary.json` in `<build-dir>/aiverse-reports/<stamp>-<pid>/`.
- On non-TTY/CI: prints ordinary output with 15-second
  `[alive]` messages listing active tests. No external pip deps.
- Forwards the underlying CMake/CTest exit status; CTest
  `--no-tests=error` causes an empty selection to fail.
  Handles Ctrl+C by sending SIGTERM to the child process group.
- Supports `--mode`, `--source`, `--build-dir`, `--jobs`,
  `--build-type`, `--define`, repeated `--target`,
  `--test-regex`, `--test-exclude`, `--verbose-tests`,
  `--plain`, `--dry-run`.

Public documentation: `docs/aiverse-build-monitor.md`.
No change to gameplay ECS components, PHYSIS/NEREID water authority,
Box3D/Place simulation, network protocol, editor GUI or save schema.

## Actual verification in this stage

On a local Linux test fixture, *not* the full AIVERSE checkout:
- Python `py_compile` PASS; tested exact source blob matches
  committed GitHub blob `b884de9e133cdb6a0e4a73cc0cd75cf092fbb8fa`.
- Standalone CMake fixture configured and built; test selection
  `^smoke_` with fast and 2s silent test: **CTest 2/2 PASS**,
  correct stage logs and `summary.json`.
- Empty CTest selection `-R '^no-match$'`: exit 8.
- Deliberate `cmake -E false` test: exit 8.
- Separate 16s silent CTest: PASS, emitted
  `[alive] ctest elapsed 00:00:15; smoke_heartbeat (00:00:15)`.
- Source test was run with `--plain` (non-ANSI). The live
  macOS Terminal/iTerm TTY interface is **NOT RUN/UNVERIFIED**.
  No PHYSIS, Place or full AIVERSE native compile was executed
  by this agent for this tooling change.

## Next concrete steps

1. On the user's macOS checkout, pull this branch and test the
   dashboard with the existing build:

```sh
cd ~/Projects/AIVERSE &&
git fetch origin &&
git switch feature/physis-nature-simulation &&
git pull --ff-only origin feature/physis-nature-simulation &&
python3 tools/aiverse-progress.py --mode test \
  --build-dir build/physis-nereid-validation \
  --test-regex '^(skill_audit|place)$' --jobs 2
```

2. To isolate the **real** Place physics/network performance,
   run an optimized native `Release` benchmark in its own build:

```sh
python3 tools/aiverse-progress.py --mode all \
  --build-dir build/place-release-validation --build-type Release \
  --define AIVERSE_ENABLE_PHYSICS=ON \
  --define AIVERSE_BUILD_EDITOR=OFF \
  --define AIVERSE_BUILD_TOOLS=OFF \
  --define AIVERSE_BUILD_CLIENT=OFF \
  --define AIVERSE_BUILD_HEADLESS=OFF \
  --define AIVERSE_BUILD_SERVER=OFF \
  --target aiverse_place_tests --test-regex '^place$' --jobs 4
```

3. Record `CMAKE_BUILD_TYPE` in original cache and macOS
   `uname -m`, `sysctl -n sysctl.proc_translated`; if Release
   still misses 5ms, profile `PlaceServer::update` split into
   network poll, character/input processing, Box3D `Scene::step`,
   body synchronization and replica tick/broadcast. Preserve
   determinism and convergence. Do not simply raise `5.0`.
4. The source merge PHYSIS+NEREID Stage73 focused CTest 40/40
   remains valid, but the full 150-test repository suite is NOT
   green. Keep PR #25 DRAFT and leave `main` untouched.
   Update another handoff with actual result, including reports.
