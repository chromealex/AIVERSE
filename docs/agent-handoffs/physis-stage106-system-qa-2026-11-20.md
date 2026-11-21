# PHYSIS Stage 106 — system QA on current main

Date: 2026-11-20.
Branch: `codex/physis-system-qa`.
Tested source: `1dc657128d6ae04a6e65b3e5265b1b6a1306dc59` (pinned origin/main).
QA report commit: `de5549064984f16e31573905c745faa121d54f1d`.
Previous: [Stage105](physis-stage105-main-merge-2026-11-20.md).
Report: [PHYSIS system QA](../qa/physis-system-2026-11-20.md).

## Scope and compatibility

User requested checking the whole PHYSIS system; all builds must use KINGSTON.
This is a QA/documentation stage, not an implementation or merge. No product
source, tests, public API, persistence schemas, wire formats or defaults changed.
The prior structured-fog branch and main remain untouched.
Affected repository paths: the QA report, this handoff, and an appended
continuation link in Stage105. No unverified code fix is claimed.

## Executed evidence

All products, logs, compiler TMPDIR/module cache and temporary reproduction
are under `/Volumes/KINGSTON/AIVERSE-builds/physis-system-qa`.
Source checkout: `/Users/aleksandrfeer/Projects/AIVERSE/.worktrees/physis-system-qa`.
Platform: macOS 15.6 arm64 host, AppleClang 17; CMake emitted x86_64 binaries
executed through Rosetta. Debug with assertions enabled.

- CMake configure: PASS, tests/physics/tools/editor ON; headless/server/client OFF.
  Exact command is in the report, output in `configure.log`.
- Extracted all 32 `aiverse_physis_*` executable targets from root CMake,
  saved `targets.json`, built with `cmake --build .../build --parallel 4 --target`
  followed by that list: PASS, exit 0, no compiler warnings/errors in `build.log`.
- `./run-tests.sh --quick --no-build --build-dir /Volumes/KINGSTON/AIVERSE-builds/physis-system-qa/build --filter '^physis_'`:
  **32/32 PASS, 0 failures, 14.77 s**. Includes Box3D/TerraWeave actual fall
  integration, NEREID contacts, stress/outbox/host, sky, MapDoc authoring,
  save/rollback/COW, real loopback WebSocket, and POSIX process-kill recovery.
  XML/log: `build/test-reports/20261120-144011-5311/`.
- `cmake --build .../build --parallel 4 --target aiverse_editor`: PASS, exit 0.
  `editor-build.log` contains two unrelated SpectraGraphWindow unused warnings.
- Temporary `config-probe.cpp`, compiled against current engine: reproduced
  malformed flood authority fallback. Build/log retained, no new repository
  test infrastructure created.
- GUI: **BLOCKED**. App launched on a disposable template copy with PHYSIS
  window requested, but CUA reported locked macOS and could not unlock.
  Asked user to unlock; no reply before final cleanup. No demo/gizmo/button
  interactions or screenshot PASS. Stopped only the verified QA executable
  with SIGTERM (exit 143 is deliberate cleanup). User editor preferences and
  project registry restored from a local private backup; backup removed.
  Automatic review rejected exporting those private settings onto KINGSTON;
  a safer local backup was approved instead. No private settings exported.
- CI: NOT CHECKED/NOT RUN for this QA branch; no CI URL or green claim.
  Historical Stage104 178/178 results belong to their recorded older SHA.

## Findings and remaining work

1. P2, reproduced: `PhysisProjectConfig.h:71` uses getStr with a LocalShallow
   fallback without validating a provided floodMode's type. false, 42 and {}
   are silently accepted as LocalShallow; an unknown string is rejected.
   This can choose local flood simulation for malformed NEREID configuration.
   Missing/null defaults were not classified as a defect. Actual concurrent
   water-authority effects were not executed.
2. P2, source-confirmed: `tools/physis-validate-native.sh --full` and
   `.github/workflows/physis.yml` build the old 24-target set but run all
   ^physis_ tests. Six current headless targets are omitted: contacts, stress,
   outbox, host fall, real TerraWeave fall bridge and sky. Fresh builds lack
   those executables; reused builds may run stale ones. This audit enumerated
   the complete set and avoided that mismatch. The old script/remote workflow
   was not itself rerun; distinguish this source finding from CI job status.

Next implementation stage, if requested: reject non-string provided floodMode,
cover it in existing config tests, update/derive full script+CI targets, run
focused verification on KINGSTON and append separate implementation/handoff
commits. On an unlocked Mac complete demo tick, authoring gizmo/Undo and live
host GUI scenarios. Product fixes were outside this QA-only request.

NOT RUN: actual production game adoption, visual sky/fog parity, native arm64,
Linux/Windows, deployed TLS/admin security, long soak/performance and full
repository-wide current CTest. Opt-in library tests are not automatic FPS/RTS
integration. No production-readiness or cross-platform determinism claim.


## 2026-11-20 continuation — fixes

User-authorized implementation `07b930463f1d2c8c4a497a472d5b229f02e4a735` resolves both findings. Validation: 30/30 headless + 2/2 authoring PASS on KINGSTON; CI target set matches the headless CTest graph. See [Stage107](physis-stage107-qa-fixes-2026-11-20.md). Historical GUI blockage remains unverified.
