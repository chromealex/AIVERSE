# TerraWeave Stage70 + PHYSIS current-main — Linux/macOS CI gate

**Date:** 2026-11-20. **Branch:**
`integration/terraweave-physis-stage70-20261120`.

## Source / CI commit

- `97b4e165cae1ab74b907ba2b934665a530715432`:
  true two-parent PHYSIS-current-main + Stage70 Terra merge; see
  [merge decisions](qa-2026-11-20-stage70-physis-main-merge-handoff.md).
- `c1d7e9f979f2abd34a77e9ff84ec31a2eb05440c`:
  source merge handoff.
- `25cc5e2e4874daa5e6852984537aa4eb66819490`:
  `docs/terrain/CURRENT_HANDOFF.md` links to merge stage.
- `a3dfba63bc4c8671b9a92a8098aa61a721f6bd89`:
  `.github/workflows/terraweave-main-integration-qa.yml` now
  additionally triggers on the NEW staging branch; retains trigger
  for the original `integration/terraweave-main-20261120` branch.
  No weakening of test inventory, full-build gate, compiler flags,
  physics flag or strict Place 5ms budget.

## Actual workflow evidence

New GitHub Actions run:
[Stage70 PHYSIS merged-main Linux/macOS #44624069071](https://github.com/chromealex/AIVERSE/actions/runs/44624069071).

At last API read, the run had
`status=queued`, `conclusion=null` and head SHA exactly
`a3dfba63bc4c8671b9a92a8098aa61a721f6bd89`.
This is **NOT a passing or failing test result**.
Previous staging branch run #44621814322 also remained queued,
and was not cancelled by this independent integration workflow.

The CI matrix is Ubuntu 24.04 + macOS 15. Both configure Debug
with editor, headless, tools, native tests and Box3D on; Place
hot paths use opt-in optimized Debug; both build ALL targets before
running unfiltered CTest with Place serial; logs are uploaded from
the corresponding workflow steps.

## Not run / blockers / compatibility

No native `cmake`, `ctest`, desktop GPU capture, C++20
Clang/GCC compiler output or real PHYSIS+NEREID+Terra+Box3D
integration success has been observed on this merge SHA.
The authoring container has no terminal GitHub DNS, so does not
have a local repository build to run.

All `main`, `feature/terraweave`, and previous integration
branch refs were left untouched. No third-party worktree overwritten
or speculative fix made to current main. New CMake native Stage70
targets and real PHYSIS header presence require compiler validation.

## Next agent

1. Read workflow #44624069071 (both OS jobs and full logs)
   after it reaches a terminal state; capture exact failed step and
   error lines or verified PASS/FAIL counts.
2. Resolve concrete compilation failures only on this staging
   branch; leave a separate handoff and retrigger CI.
3. On a Mac Studio, run a clean isolated worktree full build and
   CTest under the same flags and record actual SHA + CMakeCache.
4. Run the multi-owner terrain erosion/root-fall/NavMesh/NEREID
   river nutrient/PHYSIS weather/save-rollback scenario; no
   duplicate water or weather authorities.
5. Validate Metal render, Vulkan/GLES builds, performance and
   source merge vs any newer main before promotion.

Per-stage agent documentation remains mandatory. No passing test
claims until exact native logs are observed.

## Draft PR created for review (after CI trigger)

- [Draft PR #53](https://github.com/chromealex/AIVERSE/pull/53):
  `integration/terraweave-physis-stage70-20261120` -> `main`.
- Readback after creation: `draft=true`, `state=open`,
  mergeable=true but mergeable_state=unstable (required checks not
  green); latest main still
  `e4129c5062a6ab47e7c70f3fdd483392fde1f679`.
- The compare endpoint showed `behind=0` and `ahead=107`
  commits on this branch. A larger ahead count includes history
  inherited from the TerraWeave parent and is not 107 new changes
  authored during this step. The compared changed file set was 37.
- #44624069071 remained QUEUED on the workflow commit at readback.
  No automated pass or failure exists yet. **DO NOT MERGE** the PR
  before full native and runtime QA is independently observed.
