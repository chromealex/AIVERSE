# TerraWeave Stage 67 — deadwood decomposition, soil fertility, and Box3D retirement

**Date:** 2026-11-19. **Branch:** `feature/terraweave`, Draft PR #24.
**Task:** implement decaying timber from natural tree/shrub death and fallen
trunks, conserve organic resource units while releasing local fertility,
feed living rooted plants and aggregate grass, and detach rotten collision
bodies without changing NEREID or PHYSIS authority.
**Status:** source written to remote; CMake/CTest/native GPU/Box3D verification
NOT RUN. This is a feature-first stage by user request.

## GitHub commits, paths, completed behavior

- `6cdb36d93d1195762393b81dedf8cc180f2a4ce2` — NEW
  `engine/world/TerraWeaveForestDeadwood.h`: persistent `ForestDeadwoodResource`
  v1, sorted unique deadwood IDs, bounded 32m local mineral pool,
  integer fixed-period decay and carry, conservation ledger, explicit
  extraction of organic matter, stable local fertility sampling.
- `ca12664d274e95980aae5a18501a8ba94706fb2c` — allow newly fallen
  or naturally dead plants to be registered between scheduled decay ticks.
- `1b43c665020fda55f738747f196daff9fa9849d7` — NEW
  `engine/world/TerraWeaveForestDeadwoodEcs.h`: versioned simulation
  persistence, bounded ECS decay system, explicit fallen-tree intake at a
  safe ECS structural barrier.
- `c0f69f1b73a625e0d171f1ec6db04d43587bc074` —
  `engine/world/TerraWeaveForestEcologyEcs.h`: Stage65 natural deaths
  seed the deadwood ledger in the SAME staged World copy as the mortality
  flag, and root-zone nutrient samples use previously released organic
  matter on future ecology intervals.
- `1f8012e7d910d73739511b460c12d129a2db5778` —
  `engine/world/TerraWeaveForestHarvestEcs.h`: the prior Stage66 native
  harvest now exports any remaining fallen timber to an explicit
  `totalExportedUnits` sink in the same ECS commit, avoiding simultaneous
  soil mineralisation and harvested inventory of the same matter.
  Explicit replant clears a spent old log ID but preserves ledger totals,
  allowing a new biological life cycle for that identity.
- `6e58641f7f84532e108b26d2949b9859838fb70e` —
  `examples/world/terraweave_native/Integration.h`: opt-in deadwood
  persistence and decay system after rooted forest ecology, no changes to
  old game setup unless `enableForestDeadwood=true`.
- `4ef3fd42e52881726c01de83ccb3e5977bc94058` —
  `examples/world/terraweave_native/PhysicsIntegration.h`: bounded,
  duplicate-validated posttick `removeFullyDecayedFallenBodies` using the
  EXISTING Box3D `FallenPlantPhysics::erase`, not a new physics owner.
- `839d2403e26f095f35afae27f0d6caeca3027f84` —
  simplify the exact 1024-event Box3D cleanup cap.
- `f473f3005f1a658079c08a4dd1f65daf77410851` —
  validate proper tree/shrub root habit even with explicit mass overrides.
- `85672af27dfbea9aadb24effa0d6fd172a8c8c6f` —
  `engine/world/TerraWeaveShoreRecoveryEcs.h`: 5 deterministic spatial
  sample points per grass PATCH to apply local deadwood mineralisation to
  existing Stage64 cover/seed-bank recovery; never one ECS entity per blade.
- `8d7857a4c5863937a77b9cc0312251cd1d4847a2` —
  retain fractional nutrient carry through the final wood decay step.
- `77080cef89e6dbed9d9d230c4cdbdfe8ce2bfb9d` —
  batch intake for a single ECS fall-event span.
- `a191d1014d3e473f9062f2cf41b75c432cdf2d0a` —
  optimized batch fall intake using ordered merge O(N+E) rather than
  repeated vector insertion/O(N^2) on a large storm/landslide.
- `5cb29f7985fb078066694a5ced0eba91e618eb78` —
  prevalidate large fallen-event coordinate bounds before publication.

## Public API and integration order

Namespace `aiverse::terraweave`. All positions are SIGNED WORLD CM;
durations are authoritative SIMULATION TICKS (not wall clock); organic
matter and nutrients use *abstract integral units*, not mass in kg.

1. During native `GameModule::setup` before persistence sealing:
   `registerTerraWeaveForestDeadwoodPersistence(sim)`, or the sample
   `registerSystems(..., enableForestHarvest, true, settings, onDecay)`
   trailing parameters. The previous configuration remains compatible:
   default `enableForestDeadwood=false`.
2. Stage65 `advanceForestEcologyEcs` seeds one decaying record per newly
   naturally dead tree/shrub. A fallen tree/shrub from Stage60–62 soil
   updates must separately call `seedFallenForestDeadwoodEcs(ecs,id,tick)`
   or `seedFallenForestDeadwoodEventsEcs(ecs,fallEvents,tick)` at the
   authoritative ECS STRUCTURAL barrier after the soil/root commit.
   Both reject a non-fallen canonical root and duplicate event inputs;
   already-registered deadwood is idempotently skipped.
3. `addTerraWeaveForestDeadwoodSystem(sim,name,settings,onDecay)`
   runs AFTER rooted ecology in the example. Default: once every 64
   simulation ticks; at most 256 registered logs evaluated per due
   step. Persistent cursor + each log's `lastEvaluatedTick` guarantee
   deterministic bounded catch-up independent of frame rate.
4. `advanceForestDeadwood` decomposes each log using integer
   `rateUnitsPer1000Ticks` and fractional carry; default 30% becomes
   available soil nutrients and 70% goes into explicit respiration sink.
   Remaining wood + mineral nutrient pool + respired + player-exported
   units sum to `totalInitialUnits` (call `deadwoodLedgerBalanced`).
   All resources are persisted through `terraweave.forest_deadwood`
   schema v1; no transient renderer instance owns organic matter.
5. `deadwoodNutrientsPermille(resource,xCm,zCm,base)` returns a
   capped +250 per-mille local *fertility proxy* from the 32m soil
   aggregate. This is NOT another simulated mass transaction: the soil
   nutrient units remain in the saved pool until a future explicit
   plant-uptake/soil-loss policy. Rooted trees/shrubs and five-point
   grass patch climate sampling consume this scalar through the EXISTING
   ecology policies. They do not create more organic mass.
6. On `ForestDeadwoodDecayOutcome::fullyDecayed`, post-ECS native host
   may call `examples::terraweave_native::removeFullyDecayedFallenBodies`
   on the owning Box3D thread. Then recapture the EXISTING fallen
   navigation snapshot and NEREID flow-obstruction snapshots. Natural
   death does NOT automatically spawn a Box3D corpse; authored genre
   policy may animate/fell it and seed corresponding fall body/events.
7. Harvesting old fallen deadwood calls `harvestNativeForestTree`
   (Stage66), which atomically exports the saved remaining organic
   units, without a second reward/seed on duplicate input. Native
   inventory and collider retirement remain explicitly host-owned.
   Explicit restoration can reuse a spent identity, but must supply
   a new rooted record for recruited saplings.

## Budget, determinism and compatibility

- Up to **65,536** interactable deadwood identities (including spent
  tombstones) and 65,536 populated nutrient cells. Each nutrient cell is
  3200 cm x 3200 cm. Background foliage does NOT create a deadwood
  record for every cosmetic tree. Max 1024 expensive decay evaluations
  per scheduled step (default 256).
- No GPU calls, Box3D stepping or ECS structural writes inside any
  ecology/soil callback; ECS World resource mutation happens by staged
  COW publication at authority barriers. All data ordering, math,
  sampling and per-log carry are deterministic for a given completed
  simulation tick. No wall clock or random global state.
- No changes to NEREID water velocity/volume/sediment ownership, PHYSIS
  weather, Earthworks soil heights, RHI, terrain shader or GPU canopy
  geometry. Changing nutrient scalar alone does not cause terrain
  collision/navmesh rebuild.
- Organic units are an **approximate gameplay ecology resource**, not
  physical carbon/nitrogen/soil mass measured in real units. Decay
  respiration is explicit and not dumped back into the water solver.
- The local mineral pool does NOT YET support plant-uptake debit, leaching
  to NEREID runoff, microbial feedback, weather-dependent decay rate,
  decomposing branch geometry or new photorealistic fallen-log mesh.
  The v1 archive is not backward-compatible with a nonexistent v0:
  optional resource means old saves/games can opt out. Fully spent
  deadwood identities are retained for de-duplication and eventually
  count against the 65,536 budget; future controlled compaction must
  preserve stable-ID provenance.
- Current batch intake builds one root index and uses O(N+E) ordered
  merge. Individual natural-death seeding still validates/inserts each
  tree on a staged resource and may cost more when hundreds die at
  once; profile and batch that source if real benchmarks demand it.

## Actual tests/verification — DO NOT CLAIM RUN

Source-only GitHub connector edits and readback checks. No local repo build,
CMake generation, AppleClang/GCC/MSVC diagnostics, CTest passes, ECS archive
roundtrip, network duplication, save/rollback, long-running growth/decay
simulation, Box3D world step, NavMesh update, PHYSIS/NEREID perf benchmark,
Metal/GL/Vulkan screenshots or measured timing were executed in this
stage. Existing earlier green suites validate earlier commits ONLY.

## Independent QA next actions

In a clean worktree, not another agent's actively rebasing checkout:

```bash
cd ~/Projects/AIVERSE-terraweave-qa
git fetch origin feature/terraweave
git switch feature/terraweave
git pull --ff-only origin feature/terraweave
cmake -S . -B build/terraweave-qa -DAIVERSE_BUILD_TESTS=ON
cmake --build build/terraweave-qa --parallel 4 --target aiverse_terraweave_forest_scene_test
ctest --test-dir build/terraweave-qa --output-on-failure -R '^terraweave_forest_scene$'
```

Then add dedicated Stage67 C++/ECS tests (source not yet authored):
- Exact ledger equality after small fractional decay, full decay and
  export, extreme rates, overflow and corrupted archive.
- Save/load/replay and World COW snapshot rollback replays identical
  death/dig/fall/harvest/regrowth sequences; large/sparse negative coords.
- Double mortality/fall/harvest idempotence and valid explicit replant
  starts a new biological generation without loss of old ledger totals.
- Valid 65k log capacity, finite catch-up, batch 1024, no quadratic
  frame spikes, and bounded memory on empty scenes.
- Inactive vs opted-in native worlds; old forest-only scenes, PHYSIS,
  NEREID flood/sediment mass remain untouched.
- Actual rooted tree/shrub plus grass response to nearby rotten wood,
  without per-blade ECS state, plus Box3D cleanup and navmesh unblocking
  on `fullyDecayed`.
- Run independent GPU/editor/native game builds and all previous Stage44–66
  pending tests; attach actual tool output and fix compiler/runtime issues
  before merging into `main`.

**Further feature stage:** opt-in finite nutrient uptake/leaching ledger with
bounded spatial dirty updates, climate-dependent decay, multi-species wood
composition and posttick deadwood visual LOD. If introduced, version the
save schema and retain strict conservation with NEREID runoff.

**Integration warning:** PR #24 continues on a diverged feature branch.
Inspect `main` and shared Renderer3D/Gfx/editor paths separately; don't
force-merge or rebase other agents' uncommitted work. Keep this PR Draft
until real native and graphics verification.

## Stage67 follow-up source fixes, current integration snapshot

- `48ef648de2b7198b0d79633a68d1c68ddbb29400` — archive/load
  validation now checks the complete saved organic-matter ledger, rejecting
  missing/created units before any ECS publication.
- `9cb2f184180c0f9b0fcd39a405ae5cba538e07c0` — decay transaction
  validates the staged ledger before committing and
  `deadwoodLedgerBalanced` retains its bool-returning diagnostic contract.
- `859ddf93b557845fa9f48be3587726ff0d4fbaed` — fully spent wood
  clears unpayable fractional carry only after the final mineralisation
  payout has used the previous carry.
- `04845838386e88e5b38f533f01112b35f4eff080`,
  `431144b27ca55373a98eb73a5cc7152f26c7ed8a`,
  `a0e887bc54d9b51e17b643c885b5c2daa78e1026` —
  `examples/world/terraweave_native/README.md` documents native
  fall-event batching, posttick Box3D cleanup, nutrient effects,
  compilation caveats and the correct relative Stage67 handoff link.

**Final source readback:** all eight core/header/handoff paths were
successfully fetched from GitHub on `feature/terraweave` with nonempty
contents. This is NOT evidence of C++ compilation or physical correctness.

**Integration status at this read:** remote head
`a0e887bc54d9b51e17b643c885b5c2daa78e1026`; vs `main`: `diverged` with
`878` ahead and `305` behind.
PR #24: `open`, draft `true`,
mergeable `false`. No merge/rebase performed.
Tests still NOT RUN for Stage67. Any later concurrent commits require
new source/branch readback and a new QA record.
