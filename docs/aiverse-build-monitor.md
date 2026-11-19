# AIVERSE Build Monitor — live CMake / CTest terminal dashboard

Source: `tools/aiverse-progress.py`
Requires: **Python 3.8+**, `cmake`, `ctest`. Uses only the Python standard library.
Applies to **any AIVERSE game genre/subsystem**; no dependency on PHYSIS, NEREID, VFX or a renderer.
Added in Stage 75 to make long tests (notably `place`) visibly alive.

## Quick start

After checking out the wanted branch, launch from the repository root:

```sh
python3 tools/aiverse-progress.py --mode test \
  --build-dir build/physis-nereid-validation \
  --test-regex '^(place|skill_audit)$' --jobs 2
```

This uses an **already configured** CMake build directory. It does
not silently rebuild source. For configure + build + test in one
new directory:

```sh
python3 tools/aiverse-progress.py --mode all \
  --build-dir build/aiverse-progress \
  --build-type RelWithDebInfo --jobs 4
```

For a focused, optimized Place server benchmark without changing
its strict 5ms budget:

```sh
python3 tools/aiverse-progress.py --mode all \
  --build-dir build/place-release-validation --build-type Release \
  --define AIVERSE_ENABLE_PHYSICS=ON \
  --define AIVERSE_BUILD_EDITOR=OFF \
  --define AIVERSE_BUILD_TOOLS=OFF \
  --define AIVERSE_BUILD_CLIENT=OFF \
  --define AIVERSE_BUILD_HEADLESS=OFF \
  --define AIVERSE_BUILD_SERVER=OFF \
  --target aiverse_place_tests \
  --test-regex '^place$' --jobs 4
```

`place` still performs its original 16-client/4G, 500+ body scene
and still checks `tickAvg < 5.0`. This new build directory avoids
mixing Debug and Release objects. Do **not** treat Release speed as
proof that a Debug run meets the same budget.

## Features

- macOS Terminal/iTerm ANSI alternate-screen dashboard: configure,
  build and CTest stage name; elapsed time and time since last output.
- Live **CTest progress** from `ctest --show-only=json-v1` and CTest
  `Start N: name` / completion lines, including active test names,
  per-test elapsed time and pass/fail count.
- For CMake builds, shows the latest `[NN%]` **per-target** status;
  CMake percentages can restart for multiple targets and must not
  be interpreted as a globally accurate build percentage.
- Live errors/warnings count, bounded tail of last output lines.
- On redirected output/non-interactive CI, falls back to plain logs
  plus an **alive heartbeat every 15 seconds** even for silent tests.
- Streams the complete unabridged output to
  `<build-dir>/aiverse-reports/<timestamp>-<pid>/`, including
  `configure.log`, `build.log`, `ctest.log`, and `summary.json`.
- Propagates real command exit codes, fails if CTest has no tests
  (`--no-tests=error`), and sends SIGTERM to the child process group
  on Ctrl+C. This is a wrapper around CMake/CTest, not its own compiler.

## Flags

| Flag | Meaning |
| --- | --- |
| `--mode all\|configure\|build\|test` | Which stages to execute (default `all`) |
| `--source PATH` | Root CMake source (defaults to parent of `tools/`) |
| `--build-dir PATH` | Existing/new CMake build dir, relative to source unless absolute |
| `--jobs N` | Build and CTest parallelism (default 4) |
| `--build-type Debug\|Release\|RelWithDebInfo\|MinSizeRel` | CMake configure-only build type |
| `--target NAME` | Repeated CMake build targets |
| `--define KEY=VALUE` | Repeated CMake `-D` cache definitions |
| `--test-regex REGEX` | Passes CTest `-R` |
| `--test-exclude REGEX` | Passes CTest `-E` |
| `--verbose-tests` | Ask CTest to stream individual test output |
| `--plain` | Disable interactive ANSI screen; plain log/heartbeats |
| `--dry-run` | Preview selected commands without executing |

## Stage 75 actual testing

This tool was tested in a **separate local Linux CMake fixture**
(not a complete AIVERSE native checkout):

- `python3 -m py_compile tools/aiverse-progress.py`: PASS.
- `--mode all --plain`: CMake configure and build succeed;
  two fixture CTests `smoke_fast` and `smoke_silent` PASS (2/2);
  timestamped `*.log` and `summary.json` written.
- `--mode test` with a deliberately empty `-R` selection:
  exit code **8** (correct failure propagation).
- `--mode test` against deliberate `cmake -E false` test:
  exit code **8** (correct failure propagation).
- Sixteen-second silent test: the plain terminal printed
  `[alive] ctest elapsed 00:00:15; smoke_heartbeat (00:00:15)`;
  test PASS, summary recorded.
- Local `git hash-object` of the tested script matches the
  committed GitHub blob `b884de9e133cdb6a0e4a73cc0cd75cf092fbb8fa`.

**NOT RUN:** actual macOS AIVERSE build/CTest THROUGH this wrapper,
interactive Terminal/iTerm ANSI rendering acceptance, long `place`
Release benchmark, complete repository CI, Windows terminals.
Do not claim those are green without real user output.

## Place benchmark triage (NOT SOLVED)

The user reran `^(place|skill_audit)$` in the existing
`build/physis-nereid-validation` on macOS after Stage 74:
`skill_audit` **PASS**, but `place` still **FAIL** after
309.14 seconds, with average server tick **8.110 ms**, worst
19.245ms, despite `RUN_SERIAL`. It passed all other 51 Place
checks and deterministic convergence measures. Previous parallel
run had average 8.204ms. This rules out co-scheduled CTest work
as the *sole* cause. Its exact `tickAvg < 5.0` budget is unchanged.

On the owner's Mac also check:
```sh
grep '^CMAKE_BUILD_TYPE:' build/physis-nereid-validation/CMakeCache.txt
grep '^CMAKE_OSX_ARCHITECTURES:' build/physis-nereid-validation/CMakeCache.txt || true
uname -m
sysctl -n sysctl.proc_translated 2>/dev/null || true
```

If the isolated **optimized native Release** run still exceeds 5ms,
profile `PlaceServer::update` by phase (replica `poll/tick/broadcast`,
Box3D `Scene::step`, body sync), then optimize the cause. Do not
silently adjust or disable the performance assertion.

See `docs/agent-handoffs/physis-stage75-progress-monitor-place-2026-11-19.md`.

## Stage 76 — pinned compiler diagnostics (warnings and errors)

The terminal dashboard now reserves a fixed section **WARNINGS / ERRORS**
above the general recent-output list. In a real TTY, compiler errors
appear in red and warnings in yellow. The display shows the most recent
errors separately from warnings, so numerous warnings cannot immediately
hide a previous error. A recognizable compiler file/line is shortened
to `File.cpp:42: actual message`; the entire original diagnostic
line, including the full path, is retained in the log.

- Recognizes Clang/GCC `error:`/`warning:`, MSVC `error Cxxxx` /
  `warning Cxxxx`, `CMake Error`/`CMake Warning`, failing CTest
  result lines and explicit `FAIL` assertions.
- **Does not** classify `100% tests passed, 0 tests failed`
  as an error. This false positive was found and corrected during
  the implementation's end-to-end fixture testing.
- Counters persist across configure, build and test instead of
  clearing at each stage. Warnings do **not** fail builds by
  themselves. Counts refer to matching diagnostic *lines*, not
  deduplicated C++ compiler diagnostics.
- The complete timestamped and stage-tagged diagnostics are saved to
  `<build-dir>/aiverse-reports/<run>/diagnostics.log`; raw
  `configure.log`, `build.log` and `ctest.log` remain unchanged.
  `summary.json` now includes per-stage warning/error totals and
  the aggregate `diagnostics` object with a path to that log.
- At program exit the terminal also prints the counts and latest five
  diagnostics, even if the dashboard was using the alternate screen.
- Registered the Python stdlib-only regression tests as CTest
  `tool_aiverse_progress` (no additional build dependency).

To use the panel for an already configured AIVERSE build:

```sh
python3 tools/aiverse-progress.py \
  --mode build --build-dir build/physis-nereid-validation --jobs 4
```

To inspect only the monitor's test:

```sh
ctest --test-dir build/physis-nereid-validation \
  --output-on-failure -R '^tool_aiverse_progress$'
```

The user-reported native macOS **Release** `place` runner
finished `AIVERSE: PASS` in 1:41 (report:
`build/place-release-validation/aiverse-reports/20261119-172509-59587`).
The short terminal summary did not include the actual mean tick
time; do not invent a value or infer that Debug also passes.
Previous isolated Debug `place` averaged 8.110ms and failed
the 5ms performance gate.

### Verification of this stage

- Local Python 3.13 `py_compile` and new regression unittest:
  **3/3 PASS** (Clang/CMake/MSVC diagnostic detection, ignoring
  successful CTest summaries, sticky error visibility, TTY render
  color code and log contents).
- External standalone CMake fixture registered
  `tool_aiverse_progress`: real CTest **1/1 PASS**.
- End-to-end local CMake/CTest fixture emitted real CMake and
  simulated compiler warnings, then ran two passing smoke tests:
  `AIVERSE: PASS`, `diagnostics.log` had two warning lines
  and **0 error lines**, with per-stage/total counts in JSON.
- The complete native AIVERSE macOS build with the NEW monitor
  and actual Terminal/iTerm rendering has **NOT RUN**; the
  dashboard draw behavior was tested using a mocked terminal.
- See the mandatory handoff:
  `docs/agent-handoffs/physis-stage76-build-monitor-diagnostics-2026-11-19.md`.

