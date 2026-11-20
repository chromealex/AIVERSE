# TerraWeave Stage70 — bounded public NEREID micro-cell search

Date: 2026-11-20
Branch: `integration/terraweave-physis-stage70-20261120`
Draft PR: [#53](https://github.com/chromealex/AIVERSE/pull/53)

## Defect / scope

`buildNereidMicroJunctions` validated
`ForestNereidMicroJunctionSettings` before selecting nearest 4m cells.
But the public native helper
`nearestNereidMicroCell(mouth, bodyKey, waterAt, settings)`
did NOT. A direct caller could pass `searchRadiusFineCells=UINT32_MAX`,
causing an unbounded nested loop, or pass a null body key / absent
sampler. This is a bounded work/authoritative native input issue,
not a new NEREID river simulation.

## Exact source and test commits

1. `4cd0ca4dafe705ed29916ed9b6c795d6eb854a49` —
   `engine/world/TerraWeaveNereidMicroNutrients.h`.
   Validates all native search settings at the *public helper* entry
   using the existing validator (default radius 8, valid 1..16,
   maxJunctions 1..256) and rejects zero body ID / empty sampler
   with `std::invalid_argument`, BEFORE coordinate processing
   or any water query.
2. `1316a51c0a8c081132a06c5e3ce562526682597d` —
   `tests/terraweave_native_micro_compile_test.cpp`.
   Adds headless tests that radius UINT32_MAX, radius zero, body
   key zero, and an empty water sampler all fail with
   `std::invalid_argument`. The 'neverQuery' fixture throws if
   an invalid request reaches NEREID water sampling.
   Existing negative-coordinate in-range sample and overflow
   cases remain unchanged.

## Public API / compatibility

- No change to public function signatures or legal input behavior.
- No changed persistence format: `ForestDeadwoodResource` v3 remains
  unchanged; the canonical 32m soil and 4m NEREID-tagged nutrient
  grids are unchanged.
- Invalid direct helper arguments now fail fast. Validation was
  already mandatory for the internal junction planner and defaults.
- No new per-blade ECS entities, second climate/hydrology authority,
  renderer handles, physics writes or genre dependencies.

## Actual verification

- **DONE:** both source/test files committed through connected GitHub
  to the draft integration branch.
- **NOT RUN:** CMake configure/build, AppleClang/GCC compile, CTest,
  sanitizers, authoritative NEREID wet-body simulation, PHYSIS,
  Box3D, Metal/Vulkan/GLES and full acceptance. A successful GitHub
  file write is not a passing native test.
- At previous inspection, CI on the PR head was queued and earlier
  checks were cancelled by newer commits. This stage generates a new
  head and requires NEW CI validation. Older 136/136 Mac PASS is
  pre-merge evidence only.

## Next agent — strict acceptance

1. Read new PR #53 head and CI results, compile the
   `aiverse_terraweave_native_micro_compile_test` and Box3D-enabled
   smoke on Linux/macOS. Record exact commands, SHA, and error lines.
2. Run the full integrated native CTest under the same completed
   build/flags, with Place strict 5ms gate and no reclassification
   of cancelled/queued jobs as passes.
3. Exercise actual NEREID wet-body fixtures, two streams in one 32m
   coarse cell, Stage70 v1/v2 migration to v3, rollback, mass ledger,
   65k memory/time budget and PHYSIS climate ordering.
4. Verify renderer special-draw/static packet parity and
   Metal/Vulkan/GLES build/runtime where available.
5. Do not merge the Draft PR until the acceptance gates are
   evidenced; leave a new handoff for every further code/QA stage.
