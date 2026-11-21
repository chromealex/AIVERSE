# PHYSIS Stage 107 — QA defects fixed

Date: 2026-11-20. Branch: `codex/physis-qa-fixes`.
Base: `ebdb1f036a48506653b4d5fa37472a54583a8a0d` (actual implementation parent and tested base).
Implementation: `07b930463f1d2c8c4a497a472d5b229f02e4a735`.
Previous: [Stage106 QA](physis-stage106-system-qa-2026-11-20.md).
User authorized fixes and publication to main.

## Change and compatibility

- `engine/physis/PhysisProjectConfig.h` rejects provided non-null, non-string
  floodMode before installation. Invalid boolean/number/object/array values
  can no longer silently select LocalShallow. Valid modes, absence and null
  keep their old behavior; unknown strings remain rejected.
- `tests/physis_project_config_test.cpp` adds always-active checks for both
  boolean values, numeric values, object/array, unknown/empty strings, both
  valid enum values, null and missing-key default compatibility.
- `tools/physis-validate-native.sh` and `.github/workflows/physis.yml` now
  build the six missing headless PHYSIS targets. The local script keeps
  Box3D and TerraWeave fall bridge conditional on physics being enabled.
- No ECS component, save/wire format, physics solver, renderer or public
  signature changes. Deliberately malformed authored JSON is now rejected.

## Actual verification

All build output, compiler temporary files and module cache on KINGSTON:
`/Volumes/KINGSTON/AIVERSE-builds/physis-system-qa/`.
TMPDIR=`.../tmp`, CLANG_MODULE_CACHE_PATH=`.../modules`.
macOS 15.6, AppleClang 17, Debug x86_64 under Rosetta.
Commands ran on the exact implementation working tree before committing it.

1. `bash -n tools/physis-validate-native.sh`: PASS.
2. `bash tools/physis-validate-native.sh --full --jobs 4 --build-dir /Volumes/KINGSTON/AIVERSE-builds/physis-system-qa/build`:
   configure/build PASS; **30/30 CTests PASS**, 8.25s.
   Log `fix-full-run.log`; detailed report
   `build/physis-reports/20261120-151559-65215/`.
3. Compared actual `ctest --show-only=json-v1 -R '^physis_'` headless
   executable set against CI targets: **30 = 30**, no missing/extra targets.
   Evidence `fix-ci-targets.log`. Initial unprivileged enumeration could not
   write CTest's temporary log on KINGSTON; rerun with filesystem approval
   succeeded. This was a permissions issue, not a product test failure.
4. Enabled tools with `cmake -S . -B .../build -DAIVERSE_BUILD_TOOLS=ON`,
   built `aiverse_physis_authoring_test aiverse_physis_field_authoring_test`
   with `--parallel 4`, then ran
   `./run-tests.sh --quick --no-build --build-dir /Volumes/KINGSTON/AIVERSE-builds/physis-system-qa/build --filter '^physis_(authoring|field_authoring)$'`:
   **2/2 PASS**, 3.26s. Logs `fix-authoring-configure.log`,
   `fix-authoring-build.log`, `fix-authoring-run.log`; XML and detailed log
   `build/test-reports/20261120-151949-68627/`.
5. `git diff --cached --check`: PASS before implementation commit.

The 32 passing tests are two runs (30 + 2), not one 32-test run.
No criteria relaxed, no tests disabled. No new test infrastructure.
Remote CI status: NOT RUN/NOT CHECKED by this agent; workflow target equality
is locally verified, not a remote CI success claim. Native arm64, other OS,
no-physics runtime and GUI scenarios NOT RUN in this stage. Stage106's locked
Mac GUI limitation remains; this patch changes no GUI/rendering behavior.

## Next action

Publish implementation plus this separate handoff with ordinary fast-forward
push to main; preserve concurrent main changes and never force-push. After
publication, remote CI can independently verify macOS/Linux workflow execution.
Resume the remaining interactive GUI scenarios when the Mac is unlocked.

## Publication integration checkpoint

Initial push was rejected non-fast-forward because main advanced to `cdb79a7210b3e30e8e14273381d769555218af2a`. Merged it without conflicts in `754cb231c9d105180a2cffc6674e928eb58bc05e`. Relative to the actual tested base `ebdb1f036a48506653b4d5fa37472a54583a8a0d`, the incoming tree change is only eight documentation lines in the STRATA handoff. Product/test sources are unchanged from the validated implementation, so no redundant native rebuild was run. The base SHA above is corrected using the implementation commit parent and runner log; an earlier observed remote ref advanced before the initial fast-forward. No force-push or removal of concurrent changes.
