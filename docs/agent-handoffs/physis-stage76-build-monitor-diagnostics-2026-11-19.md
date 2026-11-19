# PHYSIS Stage 76 — AIVERSE Build Monitor pinned warnings/errors

Date: 2026-11-19
Repository: `chromealex/AIVERSE`
Branch: `feature/physis-nature-simulation`
PR #25: https://github.com/chromealex/AIVERSE/pull/25 (DRAFT)
Prior handoff: `docs/agent-handoffs/physis-stage75-progress-monitor-place-2026-11-19.md`
Implementation commit: `47a320624a118d3f5dd23a7e9d4c81d58e095676`
Documentation/handoff commit: inspect `git log -1 -- docs/agent-handoffs/physis-stage76-build-monitor-diagnostics-2026-11-19.md`.

## User request and observed Release checkpoint

User asked that Build Monitor show **compiler warnings and errors
somewhere visible during the build**. They also reported that the
previous Stage75 native macOS optimized `place` run completed:

```text
AIVERSE: PASS
Elapsed: 00:01:41
Logs: /Users/aleksandrfeer/Projects/AIVERSE/build/place-release-validation/aiverse-reports/20261119-172509-59587
```

The prior command used `--build-type Release`, target
`aiverse_place_tests`, `--test-regex '^place$'`. The result is
an actual user-reported optimized native Place CTest PASS, but
the excerpt contains **no measured tickAvg**; do not invent it.
Earlier isolated Debug Place gave `tickAvg=8.110ms`, failing
the original strict `<5.0ms` assertion. The original threshold
was not changed. The Release result does not imply Debug now passes.

## Source code and functionality

- `tools/aiverse-progress.py` (Python stdlib-only):
  - Interactive ANSI alternate-screen dashboard now has a
    permanently displayed `WARNINGS / ERRORS` section placed
    ABOVE the regular recent-output lines.
  - Error lines are red; warning lines yellow. Up to three recent
    errors are kept in priority view, with remaining space given
    to warnings. Error visibility is preserved even after a
    long stream of new warnings.
  - GCC/Clang, MSVC and CMake `warning`/`error` diagnostics
    recognized, plus explicit failing CTest result and assertion
    lines. Actual source line locations are shortened to
    `File.cpp:line: message` in the panel only.
  - Fixed a false positive found in fixture QA: the successful
    CTest summary `100% tests passed, 0 tests failed`
    is **not** an error. Match explicit FAIL forms instead.
  - Cumulative warning/error **line counts** survive
    configure/build/test stage transitions; they are also recorded
    per-stage in `summary.json`.
  - Timestamped and stage-tagged diagnostic messages go to
    `<build-dir>/aiverse-reports/<stamp>-<pid>/diagnostics.log`.
    The raw configure/build/ctest logs are unchanged and retain
    the full source path and any follow-up lines.
  - Console prints final warning/error totals and last five
    diagnoses even after restoring the normal terminal screen.
  - Existing `--plain` CI mode still prints raw output and
    heartbeat; diagnostics persist in the log and summary.
- `tools/tests/test_aiverse_progress.py`: new Python unittest
  using standard library, exercises diagnostic classification,
  log persistence, non-rewindable error visibility, mocked TTY
  draw and source-path shortening.
- `CMakeLists.txt`: registers
  `add_test(NAME tool_aiverse_progress ...)` with label TOOLS
  when `AIVERSE_BUILD_TESTS=ON` and Python is found.
- `docs/aiverse-build-monitor.md`, root and PHYSIS `AGENTS.md`,
  `docs/physis.md`, PHYSIS AI skill and this handoff updated.

There are **no PHYSIS/NEREID ECS authority, Box3D physics,
networking, shader, VFX runtime or schema changes** in this stage.

## Actual verification vs outstanding

- The exact `tools/aiverse-progress.py` source was edited and
  compiled with Python 3.13.5; blob
  `97764f502edfada862a739198dca4c69cf187ca8` matches
  GitHub's published content exactly.
- `tools/tests/test_aiverse_progress.py` unittest:
  **3/3 PASS** (blob
  `3c05bb2a704c406a3e98b2d3e6c2a8d975830ca4`).
- An independent temporary CMake project registered the exact
  Python test path as `tool_aiverse_progress`; real CTest
  **1/1 PASS** (0.90s).
- Standalone CMake/CTest fixture emitted CMake warnings,
  a simulated compiler warning, then **2/2 PASS** smoke tests.
  Build Monitor returned `AIVERSE: PASS`, persisted
  `diagnostics.log`, and `summary.json` reported exactly
  **2 warning lines, 0 errors** across its stages after filtering
  the successful CTest summary correctly.
- Mocked 100x24 ANSI terminal draw confirmed the pinned error
  is above recent output and colored red, even after 20 normal
  build messages.
- **NOT RUN:** real macOS AIVERSE build using this revised
  monitor, interactive Terminal/iTerm screenshot QA, Windows,
  full repository 151+ test CTest, actual GPU/Metal and combined
  PHYSIS/NEREID running in one ECS host. The fixture is not
  a substitute for native integration testing.
- PR #25 remains DRAFT. Main was not changed.

## Next agent step

On the owner's Mac after pulling PHYSIS branch:

```sh
cd ~/Projects/AIVERSE &&
git fetch origin &&
git switch feature/physis-nature-simulation &&
git pull --ff-only origin feature/physis-nature-simulation &&
python3 tools/aiverse-progress.py --mode build \
  --build-dir build/physis-nereid-validation --jobs 4
```

Then test the new Python monitor CTest and inspect the report:

```sh
ctest --test-dir build/physis-nereid-validation \
  --output-on-failure -R '^tool_aiverse_progress$'
```

Verify warnings/errors in the live macOS terminal, optionally
triggering a deliberately bad standalone fixture rather than
changing production source. Send screenshot or logs for any
layout/panel problems. On success continue with PHYSIS+NEREID
same-World authoritative integration and full-repo regression.
Preserve another exact-SHA handoff; no status inflation.
