# TerraWeave Stage70 / main Stage114 — Mac Release retirement tests PASS

Date: 2026-11-20
Repository: `chromealex/AIVERSE`
Integration branch: `integration/terraweave-physis-stage70-20261120`
Draft PR: https://github.com/chromealex/AIVERSE/pull/53

## Reason for this targeted QA

A real two-parent integration merge
`25828ffce44f27d449f15f3cf992e2ee736fa2e8`
integrated upstream main
`ad175f624ef6f25e7c6dc19cabe199cb30623201`
with parent Terra
`2d9446dc5a5027708b1bbe8a4202dc26c139d1c0`.
Main Stage114 changed exactly:
- `tests/render/resource_retirement.cpp`
- `tests/render/async_resource_retirement.cpp`
- `docs/handoffs/aiverse-stage-114-release-retirement-tests.md`.

The existing source tests were updated to use
`AIVERSE_TEST_CHECK` rather than `assert`, preserving their
side effects in Release and addressing the upstream Linux ARM64 GCC
unused-variable warning. TerraWeave gameplay, shaders, ECS, Box3D,
`ForestDeadwoodResource` v3 persistence and public APIs are unchanged.

## Actual Mac Studio result

The user ran the proposed targeted Release configuration in the
isolated QA checkout:
`/Users/aleksandrfeer/Projects/AIVERSE-terraweave-stage70-local-qa`,
build directory `build/terraweave-retirement-release`.

Terminal evidence shows **both executables compiled and linked**:
- `aiverse_resource_retirement_tests` — Built target.
- `aiverse_async_resource_retirement_tests` — Built target.

Then CTest output:

```text
Start 49: resource_retirement_queue
1/2 Test #49: resource_retirement_queue ......... Passed 0.57 sec

Start 50: async_resource_retirement_queue
2/2 Test #50: async_resource_retirement_queue ... Passed 0.31 sec

100% tests passed, 0 tests failed out of 2
Total Test time (real) = 0.88 sec
```

**QA status: Mac Release targeted 2/2 PASS, 0 fail.**
The user did not separately paste `git rev-parse HEAD` or a
release-build `provenance.txt`; the test source is attributed to
the recommended postmerge integration checkout by workflow context,
not independently attested in this terminal excerpt.
Do not claim exact tested SHA from this excerpt alone.

GitHub readback at report time showed integration branch HEAD
`2a8fb8bd8ee405eda670adb6a847a9f2c12cabf2`,
GitHub compare `main...integration` **ahead=170, behind=0**,
and PR #53 still open, Draft and mergeable. No new source
changes were made while recording this QA result.

## Separate historical acceptance evidence

- Full native/editor/Box3D CTest **271/271 PASS**, 0 failed,
  882.51 sec, confirmed exact Mac source
  `b17e775a922d9dc50981011c8f6e25d85fc7adec`.
- Strict Stage70 focused runtime and real Metal GPU test **4/4 PASS**
  (SKIP=0, FAIL=0), five actual PPM visual checks PASS,
  exact source `b1bb27c8f1e9f803ff49af29ea7dc0769b9cb264`.
- The Stage114 Release changes were test-only; the previous
  acceptance results are tied to their earlier SHAs, not
  automatically reattributed to this postmerge HEAD.
- This Mac Release PASS does not prove the original Linux ARM64
  GCC `-Werror` regression: track completed CI for that platform.

## Remaining gates / handoff to next agent

1. Preserve all previous acceptance reports and this test result.
2. Check Linux ARM64 GCC Release CI for the new Stage114 source
   and record actual output; queued workflows are not a PASS.
3. Validate a native TerraWeave forest GPU screenshot, masked
   foliage/LOD and viewport-resize resource retirement on Metal.
4. Validate Vulkan/GLES compilation and runtime on their
   supported target hosts; a Mac Metal PASS does not cover them.
5. Run a sustained **real single-World** four-owner PHYSIS ×
   NEREID × TerraWeave × Box3D terrain erosion/plant-fall
   replay scenario. The 128-due-tick pure nutrient stress has
   only modeled water sampling and is not that acceptance.
6. Decide PR #53 promotion only with explicit evidence for
   the agreed scope. Do not call the 271 native CTests a
   full production GPU/backend acceptance.

Every subsequent bounded stage requires a committed handoff
with actual commands/results, exact SHA when proven, limitations,
affected files and next action.
