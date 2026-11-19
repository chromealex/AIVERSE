# PHYSIS × TerraWeave × Box3D — native fall integration

## Scope and ownership

Genre-independent runtime integration. PHYSIS owns authoritative weather,
forces, nature stress and fracture outbox. TerraWeave owns rooted tree/shrub
identity, soil/root support, forest state and canonical `TerraWeaveAuthorityResource`.
NEREID alone owns water/erosion. Box3D owns float-body presentation/physics,
not the authoritative deterministic World simulation.

Implementation lives in `engine/world/`:
- `PhysisNatureFallHost.h`: optional persistent ECS `NatureFallHostState`,
  schema `world.physis_nature_fall` v1, stable entity-generation -> plant
  binding, exact event ledger/tombstones, bounded cleanup.
- `PhysisTerraWeaveFallBridge.h`: canonical Terra rooted-plant event mapping,
  transactional stage + outbox ACK + `rooted.fallen` mutation, explicit
  repair/replant, Box3D reconciliation, optional float pose checkpoint.

No PHYSIS core includes TerraWeave, Box3D, renderer, FPS or RTS headers.
No duplicate water, root or rigid-body solvers.

## Opt-in setup (native GameModule::setup)

Register all persistent components/resources before the Simulation registry
seals, and attach interactive tree/shrub receiver + output ECS components:

```cpp
physis::InstallOptions climate;
climate.enableForces=true;
climate.enableNatureContacts=true;
climate.enableNatureStress=true;
climate.enableFractureOutbox=true;
climate.enableWindZones=true; // if authored WindZones are persisted
climate.floodMode=physis::FloodMode::ExternalAuthority; // if NEREID owns water
physis::install(sim,climate);

world::installNatureFallHost(sim);
terraweave::registerTerraWeaveAuthorityResource(sim,terraConfig);

world::bindNatureFallPlant(sim.world(),interactiveTreeEntity,
    {speciesId,stablePlantKey});
```

Any project that uses both Terra + PHYSIS must register the *same* canonical
Terra species/key pair on both systems. The host creates normal
`ForceReceiver/ForceOutput`, `NatureProbeComponent/NatureContactComponent`,
`NatureStressReceiverComponent/NatureStressOutputComponent`, and
`NatureFractureOutboxComponent` before fracture. For dense grass, represent
coverage as patch/cluster proxies only; there is NO one-entity-per-blade Box3D.

## Runtime ordering

At each committed authoritative simulation/host barrier:

1. PHYSIS environment, forces, nature contacts, nature stress, fracture outbox
   via its `Simulation` scheduler.
2. NEREID bank soil / TerraWeave authoritative root and terrain updates.
3. On the HOST thread, call
   `world::advanceNatureFallHostBarrier(sim.world(),fallenPhysics,reconciler,
   speciesBodySpec,maxEvents,optionalForest)`.
4. Then step Box3D, update its collisions/navigation, render falling bodies
   and forest silhouettes. Never feed float poses into PHYSIS Fixed simulation.

`dispatchPhysisTerraWeaveFalls` processes at most 1..4096 events, builds one
requested-root index and scans canonical Terra roots once. It stages a World
copy for the entire batch. On successful event it writes the persistent
host fall ledger, acknowledges that *exact* PHYSIS fracture identity and
marks canonical `TerraWeaveAuthorityResource.rooted[].fallen=true`, incrementing
Terra stateRevision once per changed batch. Only after successful staging are
all three committed together via World move. Failure/exception before commit
leaves the original World unchanged. This is intentionally expensive only
on batches with pending fractures, and supports COW snapshots when enabled.

`NatureFallBox3DReconciler` never owns the Terra soil solver. It calls the
existing `terraweave::FallenPlantPhysics::spawn/erase`, including Box3D
Capsules and forest visibility. It removes only PHYSIS-owned bodies when
the authoritative ledger rolls back or an acknowledged tree is repaired.
A pre-existing soil-driven falling body remains Terra-owned.

## Repair, replacement, and long-running histories

After the user/game authoritatively replants or repairs a fallen tree/shrub,
on a *later* climate tick than the original acknowledged fracture, call
`repairPhysisTerraWeavePlant(world, entity, priorKey, priorTick, repairedPlant)`.
This validates the replacement with TerraWeave's `RootSupportRegistry`, resets
the PHYSIS stress latch, rearms the outbox while retaining old tick/key,
retires the old active fall journal entry and sets the replacement rooted
plant standing. It is one World transaction; Box3D removal happens on next
`reconcile`. A later fracture produces a new event identity.

Only after the host has passed its maximum rollback/replay/replication
retention window may it call `pruneNatureFallHistory(world,beforeTick,budget)`;
never prune active falls. `unbindNatureFallPlant` safely drops a stale
generation-safe mapping only after all its fall records are inactive.

## Float Box3D mid-fall snapshots

`NatureFallBox3DReconciler::captureFrame` produces a
`NatureFallPhysicsFrame` for owned bodies with exact position, rotation,
linear/angular velocity and Box3D scene tick. The separate
`io(Archive&,NatureFallPhysicsFrame&)` serializes the float **bit pattern**
for a compatible native host's private physics save. After ECS restore and
`reconcile`, call `restoreFrame` to reapply transforms/velocities. It validates
IDs, event keys, finite floats and rotation norms before applying changes.

This does **not** checkpoint Box3D contacts, broadphase, joint or world solver
internals, and should not be advertised as cross-platform deterministic
lockstep. Full Box3D scene ticks and collision replay remain host-owned.
Frames intentionally do NOT enter PHYSIS persistent ECS resources/checksum.

## Verification and acceptance

New combined source is in
`integration/physis-terraweave-complete-20261119`. It was composed from
pinned PHYSIS and TerraWeave commit trees; concurrent other-agent branches
were not changed. The following tests have been **authored, NOT RUN** here:

- Existing `physis_nature_fall_host` permanent resource ledger test.
- Real `physis_terraweave_fall_bridge`: authoritative tree/shrub/grass
  semantics, actual Box3D bodies and no duplicates, Terra rooted state and
  repair, rollback and re-fracture, separate float physics pose snapshot,
  native combined PHYSIS/Terra save/load and COW replay, actual wind-to-physics
  E2E via PHYSIS install/scheduler.
- Existing full PHYSIS, NEREID, TerraWeave, ECS, editor and engine CTests.

Run **one** final full-debug native acceptance from a *clean isolated*
worktree at the combined integration branch (not the older conflicted Mac
worktree):

```bash
bash tools/physis-terraweave-final-validation.sh
```

The runner configures `AIVERSE_ENABLE_PHYSICS=ON`, builds all desktop test
targets, asserts that the actual `physis_terraweave_fall_bridge` and
TerraWeave CTests are registered, and runs the entire unfiltered CTest suite
once. It logs configure, build, target list, JUnit and CTest results.
If the build/CTest fails, preserve the first error and log in a follow-up
handoff. The old native PHYSIS 29/29 PASS applies only to the *pre-combined*
source. Do not claim combined pass without actual native logs.
