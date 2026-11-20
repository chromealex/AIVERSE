# TerraWeave Stage70 — fail-closed merged-native acceptance gate

Date: 2026-11-20  
Repository: `chromealex/AIVERSE`  
Branch: `integration/terraweave-physis-stage70-20261120`  
PR: https://github.com/chromealex/AIVERSE/pull/53 (Draft)

## Scope and exact source commit

Implementation commit: `62d0566853d5a37a233fcf18d3325247ecc9d1fc`  
Path changed: `tools/terraweave-main-integration-acceptance.sh` only.  
The existing Stage70 4m body-tagged nutrient schema, NEREID/PHYSIS/Box3D authority, CMake targets, renderer, save format and gameplay code are **unchanged**.

This is a QA reliability stage, **not** Stage71 or a claim that Stage70 is ready to merge.

## What changed

1. Record and fail on a dirty source worktree before configuring. This avoids a seemingly green test result for local changes that cannot be tied to the logged Git SHA.
2. Require the last audited main ancestor by default: `d6c8fa4646cf6aa205eea5f6a0b7c0c34ac479cb`. Allow `AIVERSE_ACCEPTANCE_REQUIRED_MAIN_SHA` to pin a newer audited main commit. If `origin/main` exists locally, reject a stale Terra branch that is behind it; fetch main before native acceptance.
3. Optional `AIVERSE_ACCEPTANCE_REQUIRED_HEAD` asserts exactly which Terra HEAD was actually tested. Continue recording `git_sha`, `git_branch` and the additional required/observed main provenance.
4. After all native targets build, parse `ctest -N` and fail if ANY of the four mandatory registered tests is absent: `aiverse_tests`, `terraweave`, `terraweave_micro_nutrient`, `physis_terraweave_fall_bridge`. A CTest invocation with zero tests or an accidentally excluded PHYSIS/Box3D bridge may otherwise return success.
5. Preserve the existing **single unfiltered CTest** invocation, JUnit/report paths, full editor + native physics configuration and `set -euo pipefail`. No filter, test disable, skipped feature or reduced suite was introduced.

The required test names were read back from `CMakeLists.txt` in the integration branch. `physis_terraweave_fall_bridge` is conditional on real TerraWeave/Box3D paths; missing or disabled physics must therefore cause acceptance to fail, not silently count as success.

## Checks performed versus outstanding

**Performed:** connected GitHub source read of the prior acceptance script, direct GitHub Contents write, returned implementation commit SHA, code review of current Stage70 CMake test registrations, GitHub compare `main...integration/terraweave-physis-stage70-20261120` before this change (`ahead=145, behind=0`). GitHub Actions for previous head `b40faf25b051d6261f1360e69d1660f418b1b019` were QUEUED/PENDING, not passing.

**NOT RUN:** Bash parser/acceptance execution in a local checkout, native CMake configure, AppleClang/GCC build, focused or full CTest on `62d05668`, live NEREID+PHYSIS+Box3D ecology, Metal/Vulkan/GLES runtime. The prior Mac **191/191 PASS** is only attributable to earlier tested SHA `346dc66aeb0a89e1dc69ad889134fe7079ecb169`, not this commit. This authoring environment does not have a local AIVERSE checkout or network git clone; do not infer a successful run from committing the script.

## Exact next Mac Studio QA

Use an existing **isolated** TerraWeave QA checkout. Do not reset, clean or overwrite other agents' worktrees. A detached HEAD is supported, but all tracked and untracked source changes must be resolved before using the script:

```bash
cd ~/Projects/AIVERSE-terraweave-stage70-local-qa
git status --short
git fetch origin main integration/terraweave-physis-stage70-20261120
git switch --detach origin/integration/terraweave-physis-stage70-20261120
git rev-parse HEAD
df -h . "${TMPDIR:-/tmp}"
AIVERSE_ACCEPTANCE_REQUIRED_HEAD="$(git rev-parse HEAD)" \
AIVERSE_ACCEPTANCE_REQUIRED_MAIN_SHA=d6c8fa4646cf6aa205eea5f6a0b7c0c34ac479cb \
AIVERSE_INTEGRATION_JOBS=4 AIVERSE_INTEGRATION_CTEST_JOBS=2 \
  bash tools/terraweave-main-integration-acceptance.sh
```

Collect `build/terraweave-main-acceptance/acceptance-reports/provenance.txt`,
`git-status.txt`, `inventory.log`, `required-tests.txt`, `build.log`,
`ctest.log`, `ctest.xml` and the actual terminal result. A failing freshness
gate means integrate the actual newer main first; do not bypass it merely to get green.

## Limitations and next step

- A successful source/inventory preflight says nothing about native correctness.
- Full CTest can still require significant disk; check free storage before compiling.
- CI workflow `.github/workflows/terraweave-main-integration-qa.yml` independently builds/runs tests but does **not** invoke this local script; its status must be tracked separately.
- Once the new exact HEAD passes a complete native CTest with real PHYSIS/NEREID/Box3D and credible provenance, investigate any remaining live/sustained ecology simulation and real GPU Metal/Vulkan/GLES parity before Draft PR #53 is promoted or Stage71 begins.
- Continue with a **new committed handoff after each bounded stage**; do not rewrite or erase earlier agent records.
