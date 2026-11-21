# TerraWeave — integrated QA kickoff and CI hardening (2026-11-19)

**Branch:** `feature/terraweave`; PR #24 targets `main` and remains Draft.
**Goal:** stop adding Stage71 features, compile Stage70 native ECS/NEREID APIs,
run the TerraWeave suite under GCC+Clang, then build all enabled native
targets and run the registered CTest suite once each on Linux/macOS.
This is a QA/integration pass, NOT another gameplay feature stage.

## Actual source/CI changes

- `c806e8baed10e7dccab3974746e2d99272e623b5` —
  `.github/workflows/terraweave.yml`: fixed the existing truncated,
  unterminated single-quoted `ctest -R` regular expression at EOF;
  added the Stage70 `aiverse_terraweave_micro_nutrient_test` to the
  GCC+Clang native target build and exact CTest suite regex.
  Previously the single-quote was never closed and the new Stage70
  test was not included in the existing workflow.
- `f44889fd821e31306be456edb5a468648e66b027` —
  NEW `tests/terraweave_native_micro_compile_test.cpp`, complete
  header-compilation smoke including the real native Integration,
  PhysicsIntegration, Stage70 4m NEREID adapter and optional PHYSIS
  weather bridge in one translation unit.
- `94d6a7b8e2db21c62d4e7914f0636aa9812084ef` —
  test fixture macro safety: no template-comma inside assert macro.
- `5a5df9f06d0608f7b67d805ddc9028a2741a1cae` —
  `CMakeLists.txt`: register
  `aiverse_terraweave_native_micro_compile_test`,
  CTest `terraweave_native_micro_compile`.
  Uses existing `aiverse_models`, `aiverse_world` and
  `aiverse_zstd_dec` targets; does NOT add another renderer or ECS.
- `9f32de97ec4cf41dfb5dbbf82f251ea9ff1f56ed` —
  `.github/workflows/terraweave.yml`: both GCC and Clang
  must now build/run the native header integration smoke in addition
  to the pure Stage70 nutrient test and existing TerraWeave tests.
- `fb20e7fc83b62e23abb848189334fa3948efe790` —
  NEW `.github/workflows/terraweave-full-qa.yml`, **separate
  concurrency group** from the lightweight TerraWeave checks. It
  has one ubuntu-24.04 and one macos-15 job. Each configures native
  CMake with editor/client/server disabled but tests+headless enabled,
  builds ALL enabled targets once, and calls the full
  `ctest --output-on-failure --parallel 2` only after a successful
  build. `if: always()` uploads configure/build/CTest logs and
  LastTest.log. Trigger is a push that changes this workflow file or
  workflow_dispatch; unrelated documentation commits will not
  replace/cancel the current comprehensive run.

## Actual GitHub Actions records

1. Focused matrix: [TerraWeave isolated GCC/Clang run
   #44593585448](https://github.com/chromealex/AIVERSE/actions/runs/44593585448),
   source SHA `9f32de97ec4cf41dfb5dbbf82f251ea9ff1f56ed`.
   At last query: **queued**, `terraweave (clang)` and
   `terraweave (gcc)` both queued; no results or compiler
   logs exist yet.
2. Full native suite: [TerraWeave full native run
   #44593862162](https://github.com/chromealex/AIVERSE/actions/runs/44593862162),
   source SHA `fb20e7fc83b62e23abb848189334fa3948efe790`.
   At last query: **queued**, Linux and macOS jobs both queued.
   No CMake/CTest success/failure logs exist yet.
3. Earlier TerraWeave push runs were automatically **cancelled** by
   the workflow's `cancel-in-progress` when new source commits
   arrived. Cancelled is NOT passed and NOT failed.
4. Assistant's isolated container has no network DNS access to
   github.com (local `git ls-remote` failed with
   `Could not resolve host: github.com`), and there is no
   mounted AIVERSE repository. Therefore no local full CMake or
   Apple GPU test can be legitimately claimed.
5. **Absolutely no tests are claimed passed** for this QA kickoff.
   The presence of two queued runs confirms Actions accepted the
   new workflow definitions; it does not prove native compilation
   or runtime integration.

## What the new test gates cover

The focused GCC+Clang matrix builds:
- `aiverse_terraweave_micro_nutrient_test` (Stage70 authored
  archive v2→v3 migration, 4m body-tagged water, no double-hop,
  organic ledger, outlet deduplication).
- `aiverse_terraweave_native_micro_compile_test` (native
  Integration.h + PhysicsIntegration.h + NEREID fine transport
  + optional PHYSIS include guards).
- Existing TerraWeave base, Arbor, Flora, model, heightmap,
  ops, preview, forest scene, Horizon, GPU timing router
  target names, with CTest regex now fully closed.

The full native workflow configures with
`-DAIVERSE_BUILD_TESTS=ON`, `-DAIVERSE_BUILD_HEADLESS=ON`,
editor/client/server/tools OFF, builds all enabled native
targets, then runs **all registered native CTests once**, with
Linux and macOS diagnostic artifacts even after failure.
This does NOT substitute for Metal GUI screenshot, editor runtime,
actual Box3D gameplay, mobile GPU or multi-process replication QA.

## Follow-up actions once real Actions logs exist

- Read each workflow run's jobs/steps. If a failed configure,
  native C++ compile/link or CTest appears, retrieve the specific
  job logs, record **the first actual diagnostic** and fix the
  source in isolated commits; re-run the suite. Do not infer the
  error from a previously cancelled run.
- Add independent native ECS fixture with two real NEREID rivers
  within one old 32m nutrient cell, valid confluence/cascade
  connections, a simulated 4m current, explicit migration from
  legacy unassigned 32m nutrient mass, rollback and replicated
  authoritative receipts.
- Verify PHYSIS direct weather only in a worktree containing BOTH
  current TerraWeave code and compatible `feature/physis-nature-simulation`
  headers; its absent-header fallback should compile on TerraWeave alone.
- Run stress for 65k sparse 4m records, forest chunk streaming,
  root collapse after excavation/erosion, Box3D fallen-tree cleanup,
  NavMesh obstacle rebuild, NEREID conservation, Arbor/Flora/Horizon
  near/far visual GPU metrics on macOS Metal.
- After all QA fixes and actual green outputs, reconcile the
  diverged `main` shared graphics/editor changes and only then
  consider marking PR #24 ready for review/merging. No automatic
  merge, fast-forward assumption or rebase of other agents' work.

## Commands for a local Mac Studio agent

```bash
cd ~/Projects/AIVERSE-terraweave-qa
git fetch origin main feature/terraweave
git switch feature/terraweave
git pull --ff-only origin feature/terraweave

cmake -S . -B build/terraweave-final-qa \
  -DAIVERSE_BUILD_TESTS=ON \
  -DAIVERSE_BUILD_EDITOR=OFF
cmake --build build/terraweave-final-qa --parallel 4
ctest --test-dir build/terraweave-final-qa \
  --output-on-failure --parallel 2

git status --short
```

**Latest branch status before writing this handoff:** source HEAD
`fb20e7fc83b62e23abb848189334fa3948efe790`;
approximately **936 ahead / 305 behind** `main`, status `diverged`.
PR #24 was open, Draft, reported mergeable=false.
No tests have run successfully in this chat's tool results.

### Handoff contract

This QA file is the current stage-specific completion record. Another
agent may continue by reading the queued/failed workflow log results,
updating this handoff with actual outcomes and hashes, and fixing the
first real error; do not start Stage71 visual features or merge `main`
before the existing source and current-HEAD CI pass.
