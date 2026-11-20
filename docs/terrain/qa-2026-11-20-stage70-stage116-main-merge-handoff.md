# TerraWeave Stage70 — integrate main Stage116 Release test-only fixes

Date: 2026-11-20
Repo: `chromealex/AIVERSE`
Working branch: `integration/terraweave-physis-stage70-20261120`
PR #53: https://github.com/chromealex/AIVERSE/pull/53 (Draft)

## Scope and unmodified accepted milestones

- 271/271 native/editor/Box3D CTest PASS tied to `b17e775a`.
- SpectraFX real Metal GPU 4/4 + real PPM 5/5 tied to `b1bb27c8`.
- TerraWeave forest real Metal 1/1 PASS, 2.43 sec,
  six true PPM frames, pipelineFailures=0, exact
  `0133eb21b211af62d205952db77c50589ae55a13`.
- User-reported post-Stage114 Mac Release retirement tests 2/2 PASS.
- New Linux Vulkan/GLES3 forest QA source and isolated workflow were
  authored and committed in the Terra parent, but **not executed**.

## New main delta

Current main advanced from
`ad175f624ef6f25e7c6dc19cabe199cb30623201`
to `f9aed9a705982e2c6b77b29869e309995ce8b163`,
exactly two commits including Stage116 PR #81.
GitHub compare reported exactly three changed paths, none overlapping
the Terra parent:
- `tests/render/model_resource_provider_contract.cpp` —
  replace NDEBUG-elided `assert` with existing
  `AIVERSE_TEST_CHECK`, blob
  `8176282004f047ac0565ab871db1ec971bdb0777`.
- `tests/render/retirement_pressure.cpp` —
  same Release safety, blob
  `da357a2da57810a8ad6c52a86544819fdfc986fc`.
- `docs/handoffs/aiverse-stage-116-release-asserts-pressure-model-owner.md`
  — upstream standalone documentation, blob
  `2e0af24a7a9e55a6d69354b37be6995c81ebe3e2`.

No production TerraWeave/PHYSIS/NEREID, RHI, model renderer
or CMake source was modified in this upstream delta.

## Exact Git merge executed

Created full tree
`ebcaff8a1e05915453e333f47dfd935aa890a42e`
from Terra tree
`b29164ea4856ed50a6355fa25a20cafc317753b4`,
reusing three upstream blob SHAs **exactly**.

True two-parent merge commit:
`910461a6d910dc0583762cb6dedfa4e9b97858b7`
- Terra first parent:
  `8f2606da3ffee0fd3db269be8f24307cc5b8ca26`.
- main second parent:
  `f9aed9a705982e2c6b77b29869e309995ce8b163`.

The branch was fast-forwarded with an explicit
`expected_sha=8f2606da...` precondition and `force=false`.
Remote compare against previous Terra parent confirmed exactly
the expected three files. Compare against new main returned
`ahead=188, behind=0`.
Neither main nor another agent's feature branch was changed.
No history rewrite or blindly combined renderer source occurred.

## Actual validation and limitations

DONE: GitHub commit parent/tree verification, exact upstream file
comparison, protected branch update, postmerge changed-path
diff and main ancestry.

NOT RUN: native C++ compile/CTest on merge SHA, Stage116 GCC
ARM64 Release tests or AppleClang Release, Linux Vulkan/GLES3
GPU parity, prolonged authentic four-owner PHYSIS/NEREID/TerraWeave/
Box3D erosion, final forest image review.
Historical Mac GPU PASS stays attributed to exact premerge tested SHA;
a test-only main delta does not change its source rendering behavior.

## Next actionable checks

1. On Mac or Linux, build and run the two changed Stage116 Release
   CTests (confirmed CMake names `render_retirement_pressure` and
   `model_resource_owner_contract`; executables
   `aiverse_retirement_pressure_tests` and
   `aiverse_model_resource_owner_tests`).
   Capture exact tested HEAD and outcomes; Linux GCC `-Werror` CI
   is separate from a Mac Clang success.
2. Inspect the new standalone
   `.github/workflows/terraweave-linux-gpu.yml` and
   `tools/terraweave-linux-gpu-acceptance.sh` output. A queued CI
   run is NOT backend acceptance; require completed Linux
   Vulkan/GLES3 output and six real PPM captures, fix the first
   compiler/runtime issue if any.
3. Test a long real four-owner deterministic authoritative world
   including climate, erosion, water, fall and Box3D rollback.
4. Keep PR #53 Draft until remaining release gates have evidence.
5. Record any new bounded stage in its own committed handoff.
