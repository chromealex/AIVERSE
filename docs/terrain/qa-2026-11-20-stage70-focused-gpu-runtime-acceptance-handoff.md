# TerraWeave Stage70 — bounded longevity stress and strict real Metal acceptance

Date: 2026-11-20  
Repository: `chromealex/AIVERSE`  
Working branch: `integration/terraweave-physis-stage70-20261120`  
Integration Draft PR: https://github.com/chromealex/AIVERSE/pull/53

## Source commits for this bounded implementation stage

1. `bf06b6095c33f4f1ada4082aac41b3527746477b` — NEW
   `tests/terraweave_stage70_sustained_test.cpp`.
2. `35c618f4aaafa6a9511a5a629e2103492c7d3b57` —
   `CMakeLists.txt` registers native executable
   `aiverse_terraweave_stage70_sustained_test`, CTest
   `terraweave_stage70_sustained`, labels `TERRAWEAVE;PERSISTENCE`,
   and enables runtime assertions in release on Clang/GNU.
3. `99721c448b5c3ec4152e101dd2a03095211e6d55` — NEW
   `tools/terraweave-stage70-gpu-runtime-acceptance.sh`, a focused
   source-pinned Mac Studio GPU + runtime acceptance gate. No engine
   or editor behavior changed.

This is a QA-source stage, **not** Stage71. Previous full 271/271
native PASS was independently tied to exact SHA
`b17e775a922d9dc50981011c8f6e25d85fc7adec`,
which is an ancestor, not a claimed validation of these new files.

## New stress fixture

Uses the production deterministic `advanceForestMicroNutrients`
and real v3 `ForestDeadwoodResource` binary `io(Archive&, ...)`
code with a **pure bounded sampler fixture** standing in for
read-only NEREID wetness/identity. It does not implement a new
water solver or mutate ECS/render state.

- 128 initial 32m soil cells and 2048 initial body-tagged 4m
  water cells, two body identities, signed negative X cells.
- 128 due nutrient processing steps with 256 virtual simulation
  ticks each, **32,768 virtual ticks**; soil budget 32 and water
  budget 128 cells per due tick.
- Rain leaching, real fixed integer soil uptake, same-body
  cardinal advection, one directed cross-body junction, and
  per-step mass conservation with upper bounds.
- Test a non-due scheduler tick: it must leave the archive
  bit-identical.
- Save actual v3 archive after 64 due steps; continue to 128,
  load the midpoint, replay the second half and compare the
  complete serialized ledger **byte-for-byte**.
- Distinguish storage capped at 65,536 saved micro tiles
  from this smaller sustained fixture. Existing Stage70
  `terraweave_micro_nutrient` covers the 65k cap.

**Scope warning:** This **pure kernel** stress is not a full
PHYSIS/NEREID/Box3D live simulation. The existing
`physis_terraweave_fall_bridge` exercises real integrated
wind/fracture/ECS/Box3D, native water body identities and v3
migration/replay for shorter authored cases. A prolonged
single-World four-owner storm/erosion/forest-fall scenario is
still a separate unsatisfied acceptance gate.

## New focused GPU/runtime acceptance script

Usage (macOS; isolated clean QA checkout):

```bash
cd ~/Projects/AIVERSE-terraweave-stage70-local-qa
git status --short
git fetch origin main integration/terraweave-physis-stage70-20261120
git switch --detach origin/integration/terraweave-physis-stage70-20261120
AIVERSE_TERRA70_REQUIRED_SHA="$(git rev-parse HEAD)" \
AIVERSE_TERRA70_JOBS=4 \
  bash tools/terraweave-stage70-gpu-runtime-acceptance.sh
```

The script reuses `build/terraweave-main-acceptance`, so expensive
full CTest is **not** rerun. It reconfigures (to register the new
CTest), compiles only required native targets, checks that all
nine required CTest registrations exist, and executes a small
set of targeted real ECS/NEREID/forest and pure longevity tests.

It then executes the four existing **real Metal** GPU CTests:
`spectrafx_metal_gpu`, `spectrafx_metal_billboard`,
`spectrafx_metal_rhi` and `spectrafx_metal_scene`
with verbose output and real P6 capture to
`stage70-focused-qa/<SHA>/spectrafx-scene-*.ppm`.
This uses the EXISTING production Metal code and
`tests/spectrafx_metal_scene.mm` capture path.

The existing
`tools/spectrafx/verify_metal_ctest.py --require-all`
rejects any FAIL, UNKNOWN, MISSING **or SKIP (exit 77)**,
even if CTest itself returns success for a skipped device.
Existing `tools/spectrafx/review_frames.py --strict`
then verifies seven actual source PPM frames (two views,
reset, and PRISMA baseline/effect), producing a contact sheet
and JSON deltas. These tests do not require generated
placeholder screenshots.

Reports:
`build/terraweave-main-acceptance/stage70-focused-qa/<SHA>/`
with `provenance.txt`, configure/build, inventory,
`runtime.xml`, `metal.xml`, strict Metal summary,
visual evidence and PPM/JSON artifacts. A failed command
must exit nonzero (Bash `pipefail`). Clean-source and
ancestor checks reject unaudited worktrees; the
`AIVERSE_TERRA70_REQUIRED_SHA` variable pins the tested HEAD.

## Checks actually performed in this authoring environment

**DONE:** inspected current engine API, CMake registrations,
SpectraFX's real GPU tests, skip-evidence parser, capture test
and image-review code through connected GitHub source; committed
all three changed source files; GitHub readback verified the
new C++ fixture, test registration and script text are present.
No production runtime code or save schema was altered.

**NOT RUN:** macOS Bash parser on actual checkout, CMake configure,
C++ compiler, new sustained CTest, any target execution, GPU
hardware frames, visual PPM metrics, Vulkan/GLES, full 272-test
suite or long single-World storm/erosion test. No Apple Metal
device / full local AIVERSE checkout is accessible from this
authoring environment. **Do not mark these tests PASS** until the
Mac Studio outputs are supplied. The historical 271/271 result
is not the status of this new test.

## Platform ownership and open gates

- The new script intentionally **fails on non-macOS** and does
  not claim Vulkan or GLES coverage; use a Linux target with
  those installed backends for their distinct compile/run gate.
- Metal SpectraFX scene frames validate HDR/PRISMA/VFX composition
  but **do not** prove the final TerraWeave forest renders visually
  correctly on GPU. Forest material cutout, LOD transitions,
  terrain and GPU retirement still need a real editor capture
  and inspection with resource-lifetime observation.
- For any native compile or CTest failure, record the first real
  diagnostic and fix it without lowering test strictness.
- If new source tests pass, record exact SHA, report paths,
  pass/skip counts, GPU visual metrics and original build flags.
  Because new executable/CMake sources were added, keep the earlier
  `b17e775a` full 271/271 native acceptance distinct from
  this new target. Decide whether a fresh full 272-test run
  is required before final PR promotion.
- PR #53 remains Draft. No merge to main has been made.

**Next:** run the focused script on Mac Studio, save the logs,
fix any real failures, then separately validate Vulkan/GLES and
long-duration authentic PHYSIS×NEREID×TerraWeave×Box3D erosion.
A subsequent bounded step MUST commit a separate handoff.
