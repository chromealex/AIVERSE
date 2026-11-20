# TerraWeave Stage70 — real Metal forest rendering hardware PASS

Date: 2026-11-20  
Repository: `chromealex/AIVERSE`  
Integration branch: `integration/terraweave-physis-stage70-20261120`  
Draft PR #53: https://github.com/chromealex/AIVERSE/pull/53

## Exact provenance and user-reported execution

**Tested SHA**: `0133eb21b211af62d205952db77c50589ae55a13`.

The user ran `bash tools/terraweave-forest-metal-acceptance.sh` on
Mac Studio in `/Users/aleksandrfeer/Projects/AIVERSE-terraweave-stage70-local-qa`.
The transcript contains a successful compiled and linked production
`aiverse_terraweave_metal_forest_test` target and exact hardware
CTest registration `Test #214: terraweave_metal_forest`.

Observed terminal output:

```text
[metal-assets] libraries=0 sourceMisses=12 pipelines=18 pipelineFailures=0 archive=0 strict=0
[terraweave-metal-forest] near=6480 far=3891 holes=7729 resized=1502 azimuthA=0.00024 azimuthB=0.12524
[terraweave-metal-forest] failures=0
PASS [terraweave-metal-forest] real Metal BranchLOD, masked 8-view impostor, RT resize and GPU-idle retirement
1/1 Test #214: terraweave_metal_forest .... Passed 2.42 sec
100% tests passed, 0 tests failed out of 1
Total Test time (real) = 2.43 sec
PASS: real TerraWeave Metal forest GPU/PPM acceptance; sha=0133eb21b211af62d205952db77c50589ae55a13
```

**Hardware forest QA: PASS 1/1, 0 failures.** The source
hash in the footer is an explicit on-device result, not an
inference from GitHub branch HEAD.

Six *actual hardware-produced* frames were written at
`build/terraweave-main-acceptance/forest-metal-qa/0133eb21b211/`:
`forest-baseline.ppm`, `forest-branch-lod.ppm`,
`forest-impostor-a.ppm`, `forest-impostor-b.ppm`,
`forest-baseline-resized.ppm`, `forest-branch-resized.ppm`.
The files live on the user's Mac; they have **not** been copied
into this authoring environment or visually inspected here.

Interpret the pixel measures carefully:
- `near=6480`: visible BranchLOD geometry compared with baseline.
- `far=3891`: visible alpha-masked impostor compared with baseline.
- `holes=7729`: background-matching pixels inside the impostor
  drawn bounding rectangle; supports, but does not alone establish,
  production-quality cutout.
- `resized=1502`: visible geometry in the resized offscreen target.
- `azimuthA=0.00024` vs `azimuthB=0.12524`: distinct atlas
  slice material overrides for the two camera orientations.
- `pipelineFailures=0`: the test did not log pipeline failures.
- RHI GPU-idle barrier succeeded before releasing model resources.

No user-visible runtime editor window was exercised here; a
headless Metal scene is not the same as real scroll/resize/camera
movement of the Studio editor, and these pixel deltas are not a
subjective review of tree appearance.

## Code and source integrity

The tested source includes `tests/terraweave_metal_forest.mm`,
`tools/terraweave-forest-metal-acceptance.sh`, the new macOS-only
CTest in `CMakeLists.txt`, and the earlier exact
`b17e775a` native 271/271 acceptance and
`b1bb27c8` SpectraFX real Metal 4/4 PASS.

GitHub branch readback at report intake returned **exactly**
`0133eb21b211af62d205952db77c50589ae55a13`, with
`compare(tested, branch)` indicating ahead=0, behind=0.
Newer `main` at this checkpoint:
`ad175f624ef6f25e7c6dc19cabe199cb30623201`.
The integration branch was then ahead=179, behind=0;
PR #53 remained open Draft and mergeable.

This reporting stage changes documentation only — no
`ForestDeadwoodResource` v3 schema, World authority, API, foliage
geometry, shaders, CMake testing behavior or GPU resources.

## Remaining separate acceptance requirements

1. Visually inspect the six saved PPM images, and separately test
   actual Studio viewport resize/hot-reload lifetime and forest LOD
   transitions across real camera distances.
2. Linux Vulkan and GLES: verify **actual backend build and runtime**,
   not merely installed SDK headers or macOS Metal rendering.
3. Extended authentic same-World PHYSIS × NEREID × TerraWeave ×
   Box3D erosion/uproot/fall/nutrient conservation, rollback and
   stress. The bounded tracer/sampler fixture is not the same as
   long integration of all four subsystems.
4. Relevant GitHub CI must finish on a verified SHA; a queued
   or cancelled workflow is not a PASS.
5. PR #53 stays Draft until the release-scope decision addresses
   the remaining gates.

**Next concrete action:** record this PASS in `CURRENT_HANDOFF.md`,
then implement/run an independent Vulkan/GLES acceptance gate with
actual configured backend and rendered readback where possible.
Append a new committed handoff to this same branch after every stage.
