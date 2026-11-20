# TerraWeave — Mac Studio CTest triage, 2026-11-19

**Context:** The user supplied a CTest output fragment from
`/Users/aleksandrfeer/Projects/AIVERSE-static-scene/build/terraweave-final-qa`.
This absolute path DOES NOT establish the current checkout Git branch or
commit. Its errors must not be attributed to current
`origin/feature/terraweave` without checking `git rev-parse HEAD`.
The log begins partway through the test stream and ends in the full
CTest summary.

## Actual source evidence (user-provided log)

- **131 registered tests; 72% passed**: exactly **94 passed**,
  **37 classified failed** by CTest. Of those 37,
  **33 were Not Run** (missing executable) and only **4 executed
  tests ended with Failed**.
- Not Run examples:
  `aiverse_replica_tests`, `aiverse_tests`,
  `aiverse_terraweave_native_micro_compile_test`,
  `aiverse_terraweave_forest_scene_test`,
  `aiverse_terraweave_horizon_test`,
  `aiverse_headless` was also absent for importer integration tests.
  CTest prints `Could not find executable` at the configured
  build directory. This means the intended executable was not
  present when CTest attempted to run; it is NOT evidence that
  the test assertions failed, nor proof of the cause of the
  missing binary.
- Three of four *executed* failure records were importers:
  `project_importer`, `world_importer` and `ta_importer`.
  Each terminated with `FileNotFoundError` trying to launch
  `build/terraweave-final-qa/aiverse_headless`. They are
  missing-binary/dependency failures, not confirmed faulty
  project/world/TA import logic.
- The fourth executed failure was `skill_audit`. It reported
  **3 missing path references**, all in
  `skills/world/aiverse-terraweave/SKILL.md`:
  1. `apps/client/GameClient.h/.cpp` (not one path).
  2. `docs/nereid/TERRAIN_EROSION.md\` (stray backslash).
  3. `examples/world/terraweave_native/Integration.h::configureFloodRiverPorts`
     (function name appended to file path).
- Notable green tests *in that user log*: `terraweave`,
  `terraweave_micro_nutrient`, `terraweave_arbor`,
  `terraweave_flora`, `terraweave_model`,
  `terraweave_heightmap`, `terraweave_ops`,
  `terraweave_preview`, `gpu_timing_router`,
  `ecs_nereid_water_test`, `ecs_nereid_geometry_test`,
  `ecs_nereid_native_pipeline_test` and many other
  registered simulation/library tests.
  These passes apply to the tested local checkout/binaries ONLY,
  not necessarily to the latest TerraWeave feature head.

## Actual fixes committed to feature/terraweave

- `c8ddbbbe4a5dc2c89ddbe22493fc0f0d7163737c`:
  fixed all three TerraWeave SKILL.md references. Subsequent
  GitHub source readback verified corrected tokens, actual
  `apps/client/GameClient.h`, `apps/client/GameClient.cpp`,
  `docs/nereid/TERRAIN_EROSION.md`, native Integration.h,
  and its `configureFloodRiverPorts` implementation.
  **No rerun of the actual `skill_audit` executable occurred
  in the assistant environment.**
- `234618e669c071eafcff767a7f576512c5e88f6d`:
  repaired the **actually still-unterminated** closing
  `ctest -R` quote in `.github/workflows/terraweave.yml`.
  The earlier attempted workflow fix did NOT leave a valid
  complete `-R` line: this follow-up replaced the entire
  last workflow step and verified the final `$'` suffix in
  source. The corrected regex now selects `skill_audit`
  alongside GCC/Clang TerraWeave and 4m native smoke tests.
- `0a7f33e5add49ea7c6949e3db6af2ac47d2f01c8`:
  `.github/workflows/terraweave-full-qa.yml` now records
  `ctest -N` test inventory AFTER successful build, before
  running the full suite. This helps distinguish missing binaries
  from genuine assertion failures and uploads inventory diagnostics.

## Actual CI state observed after fixes

- [Focused GCC+Clang run #44597745329](https://github.com/chromealex/AIVERSE/actions/runs/44597745329):
  `queued`, source SHA `234618e669c071eafcff767a7f576512c5e88f6d`.
- [Full Linux+macOS native run #44597871291](https://github.com/chromealex/AIVERSE/actions/runs/44597871291):
  `pending`, source SHA `0a7f33e5add49ea7c6949e3db6af2ac47d2f01c8`.
- Old runs were cancelled by workflow concurrency and are not
  passing test evidence. No real C++ compiler or CTest result
  has been received yet on the new SHA.

## Reliable Mac Studio reproduction / build-first gate

Use a separate **detached** worktree to avoid changing the active
`AIVERSE-static-scene` checkout and other agents' branches.
If the target worktree directory already exists, pick a new empty
directory, do not overwrite it.

```bash
cd ~/Projects/AIVERSE-static-scene
git status --short
git rev-parse HEAD
git branch --show-current
git fetch origin feature/terraweave
git worktree add --detach ~/Projects/AIVERSE-terraweave-qa-1919 \
  origin/feature/terraweave

cd ~/Projects/AIVERSE-terraweave-qa-1919
set -euo pipefail
cmake -S . -B build/terraweave-final-qa \
  -DAIVERSE_BUILD_TESTS=ON \
  -DAIVERSE_BUILD_HEADLESS=ON \
  -DAIVERSE_BUILD_EDITOR=OFF \
  -DAIVERSE_BUILD_CLIENT=OFF \
  -DAIVERSE_BUILD_SERVER=OFF
cmake --build build/terraweave-final-qa --parallel 4 \
  2>&1 | tee build/terraweave-final-qa/build.log
python3 tools/skills/audit.py --skills aiverse-terraweave
ctest --test-dir build/terraweave-final-qa \
  --output-on-failure --parallel 2 \
  2>&1 | tee build/terraweave-final-qa/ctest.log
```

**Why `set -euo pipefail` matters:** CMake configure/build failure
must prevent launching CTest against a partially populated build
tree; otherwise the user gets dozens of misleading `Not Run`
errors and importer `FileNotFoundError` messages instead of the
FIRST actionable build diagnostic. If the build fails, preserve
`build.log`, quote the first `error:` / CMake failure and
fix that root cause BEFORE rerunning the full CTest suite.

If `git worktree add` fails due to a preexisting QA path, do not
delete another agent's worktree; choose a fresh uniquely named
directory. Keep the active checkout untouched. Always review the
current branch and origin SHA before merging.

## Unresolved until native outputs

- Why 33 test executables were missing is UNKNOWN: build may
  have failed, been incomplete, built in another directory,
  or used incompatible options. User pasted CTest output only;
  **the CMake build log was not supplied**.
- `skill_audit` source fix has GitHub readback, not a passing
  post-fix run.
- Native `terraweave_native_micro_compile` was **Not Run**,
  not failed; it still needs an actual compile and execution.
- Reconcile `feature/terraweave` with `main` only after
  CMake, full CTest, true NEREID/PHYSIS integration, Box3D,
  and renderer QA. No main merge/rebase in this triage.
- Follow each fix with an updated standalone QA handoff
  including the exact commit SHA and actual test log evidence.
