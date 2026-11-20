# TerraWeave Stage70 — live PHYSIS weather + NEREID 4m nutrient rollback acceptance

**Date:** 2026-11-20
**Branch:** `integration/terraweave-physis-stage70-20261120`
**Draft PR:** [#53](https://github.com/chromealex/AIVERSE/pull/53)
**Current main at stage start:** `e4129c5062a6ab47e7c70f3fdd483392fde1f679`
**Source SHA after test changes:** `5ccba305e8a097ad0edc38388c02bcd0b1078ee3`

## Purpose

The previously user-reported full Mac native acceptance passed **181/181**
on `f55d8d4e8622d297c4c27266f9289f82cc59baed`. It included
`physis_terraweave_fall_bridge`: real PHYSIS wind -> rooted TerraWeave
tree falls -> host Box3D body; COW snapshot, archive save, replay and
host physics rollback checks.

That did NOT explicitly prove that **4m body-keyed NEREID nutrient water**
consumes real PHYSIS weather and conserves organic mass in the
same World. This stage adds that scenario to an existing physically
linked cross-owner CTest WITHOUT duplicating hydrology, physics or climate.

## Changed code

1. `2a7c5b660211accac1370f7ad02eef767fe6a8fc` — extend
   `tests/physis_terraweave_fall_bridge_test.cpp` with
   `nativeNereidFourMetreNutrientReplay(bool cow)`, called once
   with ordinary World and once with COW enabled.
2. `5ccba305e8a097ad0edc38388c02bcd0b1078ee3` —
   repair the test's rollback-value lifetime (copy `done` rather
   than retaining a dangling reference across `ecs=snapshot`);
   compare existing plain `ForestDeadwoodSoilCell` fields instead
   of assuming an unavailable `operator==`.

**No production engine or CMake changes.** Existing target:
`aiverse_physis_terraweave_fall_bridge_test` registered as
`physis_terraweave_fall_bridge` only with
`AIVERSE_BUILD_TESTS=ON` and `AIVERSE_ENABLE_PHYSICS=ON`.
The test is linked against the real engine, Box3D, models, zstd and
the NEREID native library. All existing actual fall/fracture tests
remain active and unchanged.

### New fixture/expected behavior

- One authoritative `aiverse::World`; initialize one-cell real
  PHYSIS climate and advance 256 completed weather ticks.
  Set explicit valid PHYSIS atmosphere rain=0.75 and wetness=1
  at the completed tick. Read it through production
  `makeTerraWeavePhysisNutrientWeather` at XZ=(16m,16m).
  Expected 750/1000 permille. No independent weather simulator.
- One real TerraWeave Earthworks authority with flat ground
  (0cm) and immutable recipe.
- Two real NEREID `WaterBodyComponent` entities (wide river A and
  higher-priority narrow river B) inside a **single 32m soil tile**,
  with separate generation-safe Entity-packed body IDs.
  `WaterQueryIndex` must return body A at 4m tile (0,0),
  B at (3,3), and refuse a cross-body preferred identity.
- `ForestDeadwoodResource` v3 starts with 200 soil units,
  100 per water body, exactly 400 initial units. The production
  `advanceForestMicroNutrientsEcs` consumes PHYSIS weather and
  native NEREID flow at completed tick 256. With the fixture
  policy, expected leach=48 and advected=16, and ledger balance.
- The bodies retain separate identities; the v3 archive
  `Archive` writer/reader roundtrip must preserve water keys,
  amounts and cursors.
- Copy the original World snapshot; restore it and repeat
  the exact completed nutrient tick. Results, saved reservoir,
  cursor, leached units and total conserved mass must match.
- Both ordinary and `World::enableCowSnapshots(true)` regimes
  run. The same CTest also invokes the existing PHYSIS wind,
  Box3D plant fall, host barrier and native Simulation
  persistence/rollback checks.

**Not claimed by this fixture:** NEREID's fully scheduled bank-erosion,
long-running interlinked rivers, real Vulkan/GLES render parity, and
GPU screenshot quality. The test checks native NEREID body sampling
against an authoritative live ECS world, not GPU RHI surfaces.

## Verification / actual statuses

- **DONE:** GitHub accepted the C++ test change and follow-up
  C++ test lifetime fix; source read/architecture review performed.
- **NOT RUN after test edits:** AppleClang/GCC native build,
  the augmented `physis_terraweave_fall_bridge` test,
  any complete 181/181 CTest, sanitizers, standalone Metal GPU,
  Vulkan, GLES, combined multi-step bank erosion.
- **Historical PASS belongs only to SHA `f55d8d4e`.**
  Never transfer that pass to the new test revisions.

## Next agent / local verification

In the isolated Mac QA worktree, first check `git status --short`,
then fetch and detach at the current PR head. Preserve any existing
changes; do not overwrite another agent's worktree.

```bash
cd ~/Projects/AIVERSE-terraweave-stage70-local-qa
git status --short
git fetch origin integration/terraweave-physis-stage70-20261120
git switch --detach origin/integration/terraweave-physis-stage70-20261120

# The previously accepted Box3D/editor build tree already has this target.
cmake --build build/terraweave-main-acceptance \
  --target aiverse_physis_terraweave_fall_bridge_test --parallel 4 &&
ctest --test-dir build/terraweave-main-acceptance \
  -R '^physis_terraweave_fall_bridge$' --output-on-failure
```

Capture the exact compiler diagnostics and exit code. If passing,
run the full `tools/terraweave-main-integration-acceptance.sh`
to get a **new** 181/181 acceptance for the new test source SHA.
Then run separate Metal/Vulkan/GLES and genuine bank/river simulation
parity tests. `main` must remain unchanged; PR #53 stays Draft
until explicit final approval.

## Transfer

This new fixture lives in a physics-enabled universal ECS target;
it has **no FPS/RTS ownership**, does not create 4m or grass blade
entities and never allows TerraWeave to replace NEREID or PHYSIS.
Record any failure and exact SHA before a further bounded fix
and add a new handoff after that stage.
