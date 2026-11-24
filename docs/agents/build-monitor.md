# Agent rules: Terminal build monitor, warnings/errors reporting

Moved verbatim from AGENTS.md on 2026-11-21 (AGENTS.md keeps the cross-cutting rules; this file is
mandatory reading before working in this area). Newest entries are at the end.

## Stage 75 — universal terminal build monitor + isolated Place triage (2026-11-19)

- New `tools/aiverse-progress.py` (code commit
  `d83716dcd835df40c7861828a92064e961a63bc6`): standard
  library Python 3.8+ CMake/CTest wrapper, animated ANSI terminal
  dashboard, CTest active names and elapsed duration, bounded recent
  output/warning counts, plain CI fallback with 15s heartbeat, preserved
  unabridged per-stage logs and `summary.json`, real exit codes.
- **Actually tested on a local isolated Linux CMake fixture**, not
  AIVERSE: configure/build/smoke CTest 2/2 PASS, deliberate no-tests
  and failing-test exit 8, 16s silent test printed `[alive]`
  heartbeat at 15s. `py_compile` PASS; GitHub blob SHA matches
  the exact locally tested file. macOS ANSI UI remains unverified.
- **User's Stage74 re-run**: `skill_audit` PASS (0.42s),
  `place` FAIL after 309.14s, tick avg **8.110ms** (threshold
  `<5.0ms`), worst 19.245ms; all other 51 Place internal
  checks passed. This is an isolated CTest run after
  `RUN_SERIAL TRUE`, so contention with other CTests is no
  longer an adequate explanation. Do NOT relax the threshold.
- Next: test `place` in a separate optimized Release build with
  the monitor and examine `CMAKE_BUILD_TYPE`, actual Mac architecture/
  Rosetta and then phase profile PlaceServer if needed.
- This stage changes tooling/docs only; no engine PHYSIS/NEREID,
  physics, network, ECS or genre API changes. PR #25 remains Draft.
- Guides: `docs/aiverse-build-monitor.md`,
  `docs/agent-handoffs/physis-stage75-progress-monitor-place-2026-11-19.md`.

## Universal AIVERSE Build Monitor warnings/errors (2026-11-19)

Tool `tools/aiverse-progress.py` now keeps a **pinned colored**
Warnings/Errors panel in interactive Terminal, a diagnostic count
for each configure/build/CTest stage and a complete timestamped
`diagnostics.log` plus `summary.json`. The display retains
recent compiler errors even if later warning lines arrive.
Warn/error counts are matching *output lines*, not distinct compiler
diagnostics. Warnings do not automatically cause build failure.
Python regression CTest `tool_aiverse_progress` is registered
when `AIVERSE_BUILD_TESTS` and Python interpreter are available.
Source commit `47a320624a118d3f5dd23a7e9d4c81d58e095676`.
Only the monitor/test CMake and docs changed, not engine runtime.
Local standalone Python unittest 3/3, standalone CMake CTest 1/1
and CMake warning/success-log integration passed. Actual
AIVERSE macOS Terminal UI is **NOT RUN**. Guide:
`docs/aiverse-build-monitor.md`; handoff:
`docs/agent-handoffs/physis-stage76-build-monitor-diagnostics-2026-11-19.md`.

