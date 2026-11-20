# TerraWeave Stage70 — native 4m NEREID sampler boundary test

**Date:** 2026-11-20. **Branch:**
`integration/terraweave-physis-stage70-20261120`.
**PR:** [Draft #53](https://github.com/chromealex/AIVERSE/pull/53).

## Preceding stage / implemented source

The previous stage implemented checked signed-coordinate conversion in
`engine/world/TerraWeaveNereidMicroNutrients.h` (source commit
`730271f783db18779612635ee0b8234477eaa220`) and extreme authored
mouth tests (`83c452799912e3370e78ef14c597eee402e5a37d`).
Its independent handoff is
[Stage70 coordinate guard](qa-2026-11-20-stage70-nereid-coordinate-guard-handoff.md).
That stage introduced no save/API/engine ownership changes.

## This bounded QA stage

- **Code commit:** `09ff998b05d6e5aa56da63d33240ca312b6b42f4`.
- **Changed file:** `tests/terraweave_native_micro_compile_test.cpp`.
- Instantiates a real default-constructed native `aiverse::World`
  (empty ECS), an `EarthworksLayer` using the normal public
  400cm grid and zero-height base sampler, and a read-only
  `lib::nereid::WaterQueryIndex` over the empty World.
- Calls the public `makeNereidMicroWaterSampler(terrain, index)`
  at `INT64_MAX/INT64_MIN` on BOTH signed 4m axes and at
  in-bounds negative `(-1,-1)`. Every sample must safely return
  dry with no water-body authority invented.
- Existing mouth extreme guards and signed nearest wet body tests
  remain in the same native smoke test.
- The test calls the actual adapter with actual native types, not
  an invented stand-in source function. It is headless and never
  duplicates NEREID water simulation.

## Status / actual evidence

- **DONE:** source was committed to the PR branch through the
  connected GitHub Contents API.
- **NOT RUN:** native Clang/GCC compilation, CMake, CTest,
  sanitizers, PHYSIS/NEREID runtime integration, GPU Metal/Vulkan/GLES,
  full engine build, memory/performance.
- The earlier 136/136 PASS is older TerraWeave, not this change.
  At previous inspection the latest GitHub Actions runs for PR #53
  were QUEUED. Read the latest head SHA after this commit; do not
  attribute a previous queued/cancelled run to this code.

## Next agent

1. Build the `aiverse_terraweave_native_micro_compile_test`
   and `aiverse_terraweave_native_micro_physics_compile_test`
   against the current branch on macOS Clang and Linux GCC/Clang.
   Record compiler diagnostics and actual CTest outcome. Build
   with Box3D explicitly ON for the physics variant.
2. Add real active NEREID water-body fixture to check nonzero
   in-bounds wet/flow samples and stale body identity, then
   save/rollback and signed-neighbour tests.
3. Run the complete editor/headless/full CTest once with
   `AIVERSE_PLACE_FAST_DEBUG=ON`; preserve the strict Place 5ms gate.
4. Validate PHYSIS climate ownership and correct two-water-body
   Stage70 nutrient mass conservation; compare actual Metal,
   Vulkan and GLES paths where available.
5. Do not merge Draft PR #53 before these gates are green.
   Every subsequent stage gets its own committed handoff.
