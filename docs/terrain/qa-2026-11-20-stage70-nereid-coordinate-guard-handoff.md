# TerraWeave Stage70 — native NEREID micro coordinate overflow guard

Date: 2026-11-20.
Branch: `integration/terraweave-physis-stage70-20261120`.
Working PR: [Draft #53](https://github.com/chromealex/AIVERSE/pull/53)
against `main`, **do not merge until acceptance**.

## Goal

Harden two exported native NEREID-adapter boundary functions against
untrusted/malformed `int64_t` or `Fixed` world locations. Before this
stage `makeNereidMicroWaterSampler` multiplied arbitrary signed 4m tile
indices by 400cm, and `nearestNereidMicroCell` multiplied arbitrary
`mouth.x.raw`/`mouth.z.raw` by 100cm, **before** checking world
limits. An extreme input could overflow signed `int64_t`, invoking
undefined C++ behavior.

## Implementation commits, paths and API

1. `730271f783db18779612635ee0b8234477eaa220`:
   `engine/world/TerraWeaveNereidMicroNutrients.h`.
   - In the public `makeNereidMicroWaterSampler` callback, check
     `x4,z4` against signed 4m tile bounds before calculating
     `x4*400+200`/`z4*400+200`. Invalid inputs return dry/no body.
   - In `nearestNereidMicroCell`, check PHYSIS/NEREID `Fixed::raw`
     against `(kWorldLimitCm-10000)*Fixed::kOne/100` before any
     multiplication. Invalid authored mouth locations return
     `std::nullopt`, without sampling water.
   - Existing centimeter/meter coordinate conventions, strict
     NEREID body identity, runoff ledger and normal sample behavior
     are unchanged.
2. `83c452799912e3370e78ef14c597eee402e5a37d`:
   `tests/terraweave_native_micro_compile_test.cpp`.
   - Added out-of-world `Fixed::fromRaw(INT64_MAX/MIN)` mouth fixtures
     for both axes, with a sampler that throws if queried.
   - Added signed-negative-world in-range discovery test for
     `(-2m,-2m)` -> 4m cell `(-1,-1)` and correct NEREID body 7.
   - Explicit `<limits>` include. This smoke remains pure headless
     without creating a real NEREID water simulator.

**API compatibility:** no new functions or signature changes; old
successful in-range inputs retain their results. No changes to
`ForestDeadwoodResource` v3 binary layout, 4m/32m fixed units,
authoritative PHYSIS/NEREID ownership, ECS, renderer, or Box3D.
Out-of-world inputs previously had undefined signed behavior and
now safely return invalid samples/keys.

## Verification / honest status

- **DONE:** GitHub accepted both source commits on the draft PR branch.
- **NOT RUN:** CMake, C++ compiler, native-smoke CTest, unfiltered CTest,
  Mac/Ubuntu CI, numerical sanitizers, NEREID live river sampling, actual
  rollback, or GPU parity. No local full checkout is available through
  the authoring container. Do not infer PASS from source commits.
- Previous CI was QUEUED at last observation; new branch pushes may
  supersede or cancel older runs under workflow concurrency.
  Read the NEWEST head workflow on this branch, not an earlier run.

## Next agent / acceptance criteria

1. Inspect current `main`, PR head, full Linux/macOS CI statuses and
   exact failing diagnostic before making follow-up changes.
2. Compile `aiverse_terraweave_native_micro_compile_test` and its
   physics-on variant on Clang/GCC; run focused CTest and record PASS/FAIL.
3. Run full integrated CTest only after complete build; record
   exact SHA, CMake flags, Test Inventory and failure logs.
4. Add actual sampler fixture with a valid `EarthworksLayer` and
   `WaterQueryIndex` to exercise direct extreme `x4/z4`
   rejection and in-bounds wet NEREID queries, plus a sanitizer run
   if practical. The current test only exercises the mouth guard.
5. Verify PHYSIS climate tick before Terra/NEREID nutrients,
   mass-conserving 4m junction transfer, save/rollback and NEREID
   surface + Earthworks + Box3D navigation interaction. Retain
   genre-independent engine ownership. Update this handoff after
   each bounded stage.

**Do not merge Draft PR #53 into main on the basis of source audit.**
