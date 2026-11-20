# TerraWeave Stage70 — strict real Metal GPU and focused runtime PASS

Date: 2026-11-20
Repository: `chromealex/AIVERSE`
QA branch: `integration/terraweave-physis-stage70-20261120`
**Exact tested SHA:** `b1bb27c8f1e9f803ff49af29ea7dc0769b9cb264`
Draft PR: https://github.com/chromealex/AIVERSE/pull/53

## Real Mac Studio evidence

The user ran the committed focused runner `tools/terraweave-stage70-gpu-runtime-acceptance.sh`
in the existing isolated checkout:
`/Users/aleksandrfeer/Projects/AIVERSE-terraweave-stage70-local-qa`.
The terminal footer explicitly identifies `sha=b1bb27c8f1e9f803ff49af29ea7dc0769b9cb264`
and the report directory:
`build/terraweave-main-acceptance/stage70-focused-qa/b1bb27c8f1e9/`.

**Four genuine Apple Metal GPU tests** (with hardware skip detection):
- `spectrafx_metal_gpu`: PASS
- `spectrafx_metal_billboard`: PASS
- `spectrafx_metal_rhi`: PASS
- `spectrafx_metal_scene`: PASS

Actual CTest summary: **4/4 Passed, 0 failed**, total 3.01 sec.
Strict `tools/spectrafx/verify_metal_ctest.py --require-all` returned
**PASS=4/4, SKIP=0, MISSING=0, FAIL/UNKNOWN=0**. The scene fixture
reported `failures=0` and production Renderer3D depth, two camera render
targets, SceneHost and reset retirement PASS.

The real GPU-produced P6 screenshot analysis
`tools/spectrafx/review_frames.py --strict` reported:
- Changed pixels: camera A **192**, camera B **322**, PRISMA **232**.
- PASS: `effect_visible_in_both_views`.
- PASS: `camera_changes_screen_position`.
- PASS: `opaque_center_depth_test`.
- PASS: `reset_restores_baseline_red`.
- PASS: `prisma_effect_visible`.

Generated evidence paths (reported by the user's terminal, not downloaded
into this authoring session):
- `stage70-focused-qa/b1bb27c8f1e9/spectrafx-visual-review.ppm`
- `stage70-focused-qa/b1bb27c8f1e9/spectrafx-visual-review.json`
- Also the runner's `provenance.txt`, `runtime.log`, `runtime.xml`,
  `metal.log`, `metal.xml`, `metal-evidence.log`, `visual-evidence.log`.

The runner concluded `PASS: targeted Stage70 runtime and strict real MetalGPU/PPM checks`.
Because the committed runner uses `set -Eeuo pipefail` and runs
five focused native tests **before** its four Metal GPU tests, this is
strong evidence that its targeted CTest command completed successfully.
**The separate per-test focused runtime log was not pasted**; do not invent
its timings, test names as displayed by CTest, or exact pass-count footer.
The suite selected by script is:
`terraweave_stage70_sustained`, `terraweave_micro_nutrient`,
`terraweave_forest_scene`, `physis_terraweave_fall_bridge`,
`physis_nereid_contacts`.

## Prior accepted full native baseline

The previous whole native/editor/Box3D CTest **271/271 PASS**, zero
failures, 882.51 sec, was independently pinned by the Mac provenance to
`b17e775a922d9dc50981011c8f6e25d85fc7adec`.
The new sustained fixture, CMake target, and strict runner were introduced
later and validated through the **focused runner** at `b1bb27c8`.
Do not misattribute the 271/271 suite to this newer SHA; a full new
272-test run is a separate final promotion policy decision.

## GitHub integrity / concurrent main

At receipt of this report, connected GitHub readback confirmed branch
HEAD **exactly equals the Mac-tested SHA** `b1bb27c8`.
No branch commits existed after the tested SHA at that time.

Meanwhile, main had advanced independently from `d6c8fa4646cf` to
`ad175f624ef6f25e7c6dc19cabe199cb30623201` (two commits,
including PR #76). It touched only:
- `tests/render/async_resource_retirement.cpp`
- `tests/render/resource_retirement.cpp`
- `docs/handoffs/aiverse-stage-114-release-retirement-tests.md`

These fix Release-mode regression assertions for GCC ARM64 via existing
`AIVERSE_TEST_CHECK`; main's production engine/renderer is unchanged.
At that point the integration branch was **ahead 166, behind 2**,
and PR #53 was `draft=true`. Any later main sync must have its own
real two-parent merge provenance/handoff, and the post-merge test status
must not be falsely inferred from this premerge PASS.

## Scope / ownership / actual verification

**Actually run by user:** targeted Stage70 native runner through
its PASS footer; four real non-skipped Metal GPU tests; strict visual
pixel checks. These are not synthetic screenshot claims.
**Checked via connected GitHub:** branch exact SHA, subsequent main
delta and sources of the native runner.
**Not run here:** new local commands, Linux Vulkan/GLES build or visual
tests, long authentic PHYSIS × NEREID × TerraWeave × Box3D terrain
erosion/fall simulation, native editor TerraWeave forest screenshot
parity (alpha-cutout, LOD, retirement), resource stress profiling.

No NEREID hydrology, PHYSIS climate, gameplay ECS authority,
v3 nutrient archive or render source changed in this documentation stage.

## Next acceptance gates

1. Preserve the original Mac evidence directory (do not clean it).
2. Merge current main safely into Stage70 integration without
   overwriting collaborators and record exactly which files changed.
3. Test any new main-only Release retirement assertions on target
   GCC/Clang; do not claim the previous Mac 271 pass covers new tests.
4. Add/run a long real four-owner erosion/fall scenario, not merely
   a pure tracer approximation.
5. Validate the native TerraWeave forest in an actual GPU scene,
   including LOD, cutout, and retirement; validate Vulkan and GLES
   independently on supported platforms.
6. Decide whether to promote Draft PR #53 with explicitly scoped
   exceptions or retain Draft until remaining gates pass.

After EVERY bounded stage, commit a standalone agent handoff with
the exact tested SHA, changes, observed results, unrun paths and next step.
