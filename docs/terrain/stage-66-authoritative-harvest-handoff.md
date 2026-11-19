# TerraWeave Stage 66 — authoritative harvest and explicit rooted-plant restoration

**Date:** 2026-11-19. **Branch:** `feature/terraweave`, Draft PR #24.
**Scope:** Complete one source-functionality slice: harvesting must be an ECS
transaction and survive procedural chunk streaming, save and rollback.
**Division of work:** features first; independent QA agent owns builds and tests.

## Source changes and exact GitHub commits

1. `0ee3de0d0617599a9bb516cd9f1c1ab57e75a8e5` — new
   `engine/world/TerraWeaveForestHarvestEcs.h`: persistent sorted/bounded
   harvest tombstones, `harvestNativeForestTree`, explicit
   `restoreHarvestedForestTree`, save schema `terraweave.forest_harvest`
   v1, COW transactional ECS writes.
2. `b13ea274240244634228674fb6d97fac8873b755` — existing
   `engine/world/TerraWeaveNativeWorldBinding.h`: forward saved
   harvest IDs into the existing `TerraWeaveForestScene::setRemovedTrees`
   (not a new renderer).
3. `4c83b08587fba72f237d93f75a0fb1015fe70dad` —
   `examples/world/terraweave_native/Integration.h`: separately opt-in
   `enableForestHarvest=false` final setup parameter, registers persistence
   before sealing the Simulation.
4. `143582ede6d5a941429685798ba0274f0ce64755` — harvest
   tombstones preserve `wasRecruited`; explicit replant restores the
   Stage65 recruited sapling's sparse ecology/visibility marker.
5. `c0b6c25568d16bb38a3a7dd7a52c607676cc1c11` —
   validate full harvest snapshot before terrain/root/grass scene publication,
   preserving legacy presentation-only removals in non-opted-in games.
6. `11bb10cc24e582585950f8e9f2e32d9c32ff97f6` —
   `engine/world/TerraWeaveForestEcologyEcs.h`: detect conflicts when a
   native game reintroduces rooted identities which are still harvested;
   fail clearly rather than letting invisible trees compete/seed.
7. `b8384ba092e780dfc463e7fe199753a092ff0385` —
   `examples/world/terraweave_native/README.md`: native opt-in/usage,
   owner-thread, save/rollback and collision/inventory responsibilities.

## Authority and public API contract

`ForestHarvestResource` v1 is a separate persistent World/COW resource with
up to 1,000,000 sorted `ForestHarvestRecord` entries keyed by
`AnchoredPlantId{speciesId,plantKey}`, and monotonically increased
`revision`. It is independent of `ForestEcologyResource` v1, which only
tracks shade/age/flood/natural death and whether a plant was recruited.
The new harvest record stores `harvestedTick` and `wasRecruited`.

- `registerTerraWeaveForestHarvestPersistence(sim)` at setup before
  registry sealing. Registration is optional for binary/source compatibility
  with games that haven't opted into authoritative forestry.
- `harvestNativeForestTree(World&,AnchoredPlantId,uint64_t completedTick)`
  at a server-authoritative command/structural barrier. Returns
  `NativeForestHarvestOutcome`, including `changed`, previous rooted
  payload (`uprooted`), ecological/recruitment status, and new ECS state
  revision. On first harvest, writes the tombstone, removes authoritative
  interactable root and sparse ecology history atomically. A cosmetic
  procedural tree needs only a stable species/key tombstone. Repeated harvest
  is a no-op: caller MUST gate inventory awards and physics removals on
  `outcome.changed`.
- `restoreHarvestedForestTree(World&,AnchoredPlantId,
  optional<AnchoredPlant>)` clears the saved tombstone on explicit gameplay
  action. For native recruited saplings, a validated new rooted plant is
  REQUIRED and the `recruited` ecological marker is reinstated. For
  decorative procedural trees, a no-root restore simply unhides scatter.
  There is no implicit regrowth or natural-death resurrection.
- `NativeTerraWeaveWorldBinding::reconcile` still uses canonical ECS
  `stateRevision` and rollback `force=true` publication. If the optional
  harvest resource exists, the renderer receives the entire ordered
  tombstone set via `setRemovedTrees`. A visual-only legacy `removeTree`
  set is unchanged if the new resource was not registered.
- `advanceForestEcologyEcs` rejects an illegal rooted + harvested overlap
  when ecology is due. A native game must restore the identity before
  reinserting it. Harvest removes the rooted object, so its crown, seed,
  root/soil influence, mortality and fallen-support re-evaluation stop.

**No work in Stage66 touches** NEREID's water volume, current field,
sediment mass or erosion kernel; PHYSIS weather; renderer shaders, RHI
model ownership, terrain geometry, collider, navmesh or per-blade grass.
Host code must keep Box3D handles, harvest inventory/wood yields, debris
cleanup, NavMesh blockers, replication and optional tree-felling animation
in sync AFTER the ECS commit (and only once).

## Actual verification status

**Performed:** GitHub connector read of Stage65 handoff and affected native
ECS/forest source files; source writes to the branch and returned commit SHAs;
static review of current call surfaces and ownership boundaries. The source
has been saved in the remote feature branch.

**NOT PERFORMED:** checkout/CMake configure/compile, AppleClang/GCC/MSVC,
unit/CTest, editor or native GameModule launch, archive roundtrip, rollback
replay, network duplicate-delivery fixtures, 65k-tree performance benchmark,
Metal/GL/Vulkan screenshots, or Box3D collision/navigation removal. A source
commit is NOT test evidence. Draft PR #24 must remain Draft.

## Exact next QA step / acceptance

From a clean, isolated native checkout (not another agent's worktree):

```bash
git fetch origin feature/terraweave
git switch feature/terraweave
git pull --ff-only origin feature/terraweave
cmake -S . -B build/terraweave-qa -DAIVERSE_BUILD_TESTS=ON
cmake --build build/terraweave-qa --parallel 4 --target aiverse_terraweave_forest_scene_test
ctest --test-dir build/terraweave-qa --output-on-failure -R '^terraweave_forest_scene$'
```

Test agent must then add a dedicated Stage66 native header/World fixture:
(1) procedural-only tombstone + streaming + rollback + save/load;
(2) rooted harvest removes canopy/seed candidate and remains hidden;
(3) duplicate same ID causes no second award/event/revision;
(4) recruited sapling harvest then explicit replant is again rendered by
near Arbor (not procedural scatter);
(5) invalid key, duplicate archive ID, 1M budget, failed commit and overflow
preserve live ECS;
(6) disabled opt-in does not clobber existing presentation-only removals;
(7) physics collider and nav blockers actually released at host barrier.
Run native example compile and relevant CTest, then capture screenshots
and measure patch/frame times. Fix failures and record exact logs/commit.
Do not treat the listed commands as executed until run by the QA agent.

## Future feature work (after this slice)

- Optional explicit deadwood decay/nutrient recycling: must preserve soil
  ownership and not create mass from nothing; use budgeted patch updates.
- Harvest event journal for network replication/host post-tick at-most-once
  accounting; engine generic API, never hardwire a game's inventory schema.
- Dense canopy-index update strategy and per-bucket spatial sampling for
  65k rooted interactive plants, plus distant Horizon dead/recruited visuals.
- Resolve `main` vs `feature/terraweave` conflicts in a dedicated worktree
  ONLY after current shared renderer/ECS/gfx branches converge and native QA
  confirms this stage. The branch was previously 846 ahead/305 behind main;
  treat counts as snapshot, not live guaranteed values.

## Stage66 incremental safety/performance follow-ups

- `8fece67de0d3461ef481fad8f991f05033d8b215` — new harvest
  header now explicitly includes `<cstddef>` and constructs a concrete
  `ForestHarvestResource{}` in its Simulation registration call, avoiding
  C++20 template overload / initializer-list ambiguity.
- `9555a7faab82ce43ae57e492bce3faa7467d0d02` —
  `TerraWeaveNativeWorldBinding` caches the last published harvest
  resource revision; ecology-only `stateRevision` changes DO NOT rematerialize
  up to 1,000,000 tree-removal identities on each age/shade tick. First
  reconcile, a changed harvest revision, explicit forced rollback or detach
  still republish; optional resource absence keeps legacy visual removals.
- GitHub source readback confirmed presence of the new harvest header,
  ecology guard, native binding, sample opt-in and this handoff. **No code
  compilation or CTest was executed.**
- Latest observed remote feature branch HEAD:
  `9555a7faab82ce43ae57e492bce3faa7467d0d02`; compared against
  `main`: 856 ahead / 305 behind; PR #24 remains Draft and nonmergeable.
  These are current remote observations, not a claim that `main` was merged.
