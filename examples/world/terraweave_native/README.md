# Native TerraWeave + NEREID composition example

This opt-in example shows a *real, genre-neutral C++ integration seam* for a native AIVERSE `GameModule`. It does **not** change default RTS/FPS modules or create a second water solver. NEREID remains canonical for water velocity, bank contact and sediment; TerraWeave owns soil heightfield, root strength, plant ages/lighting and grass coverage.

Source: [Integration.h](Integration.h).

## GameModule setup phase

```cpp
#include "examples/world/terraweave_native/Integration.h"
using namespace aiverse;

terraweave::TerraWeaveEcsAuthorityConfig cfg;
cfg.generator.seed=1234;
cfg.soilGridCm=100;
cfg.bedrockCm=-1000000;
// Must identify generator version, seed, content hashes and height assets.
cfg.immutableRecipeId="terraweave:v1:seed1234:your-content-digest";

// Call once in GameModule::setup, before the persistence registry seals.
examples::terraweave_native::registerSystems(sim,cfg,
    [](const terraweave::EcsBankTerrainCommit& tick) {
        // Only enqueue event data; do NOT step Box3D or render inside ECS.
        // In particular, tick.vegetation.fell contains server fall triggers.
    });

// The game/authoring system creates a lake/river/ocean body, e.g.
lib::nereid::WaterBodyComponent lake;
lake.kind=lib::nereid::WaterKind::Lake;
lake.footprint=lib::nereid::WaterFootprint::Box;
lake.center={0_fx,0_fx,0_fx};
lake.halfExtents={20_fx,20_fx};
lake.surfaceY=1_fx;
lake.bottomY=-3_fx;
examples::terraweave_native::addWaterBody(sim.world(),lake);

// Attach one ECS bank-cell on a real shoreline; choose the X/Z probe on
// the wet side, and an Earthworks grid vertex on the soil side.
lib::nereid::WaterBankSoilComponent bank;
bank.soil.soil=lib::nereid::ShoreSoil::Sand;
bank.wetContactPermille=1000;
const terraweave::EarthVertex bankVertex{21,0};
const int32_t groundCm=terraweave::heightAtCm(
    cfg.generator,2100,0);
examples::terraweave_native::addShoreCell(
    sim.world(),bankVertex,groundCm,{19_fx,0_fx,0_fx},bank);
```

The bank probe's water contact must be valid for the same completed ECS tick. Real games should author/rasterize many bank-cell entities at bounded sample density, and may configure current fields, vortices, tides and PHYSIS waves from existing NEREID systems. **Never** create one bank ECS entity per grass blade.

### Player digging and ECS rollback

```cpp
terraweave::EarthworkBrush dig;
dig.centerXcm=1000; dig.centerZcm=0;
dig.radiusCm=150; dig.depthCm=50;
auto changed=terraweave::excavateAuthoritativeEcs(
    sim.world(),cfg,dig,sim.tick(),
    /*enableSlopeFailures=*/true);
// changed.slope.slides contains deterministic moved-soil events.
// changed.rooted.fell can become Box3D fallen-tree events at the host
// physics barrier; changed.grass is aggregated patch damage.
// Save/replay/rollback is canonical through TerraWeaveAuthorityResource.
// Do not also run a separate standalone NEREID Earthworks bridge on the
// same soil vertices; choose ECS outputs OR the standalone bridge.
```

Register `TerraWeaveAuthorityResource` via the helper once; `Simulation` copies it with World snapshots and stores it in `PersistentEcsRegistry` using schema version 2. When adding game-specific plant age/light or grass mutations to this resource, increment `stateRevision`; the provided dig/ECS bank helpers do so automatically.

### Render/collision/native owner phase

Create, once, an `EarthworksLayer` with immutable generator settings and a `TerrainMutationCoordinator`, `RootSupportRegistry`, `GrassCoverRegistry` plus `NativeTerraWeaveWorldBinding`. Their reference lifetimes must exceed all callbacks. After each completed simulation tick:

```cpp
std::string navError;
auto outcome=examples::terraweave_native::publishFrame(
    sim, binding, terrain, worldScene, device, spatialCollision,
    navmesh, navSource, simulationPinnedCells, grassAreaCm,
    [](world::Cell tile) -> uint32_t { /* stable reserved IDs per tile */
        return reservedTerrainCollisionId(tile);
    }, &navError, /* afterRollback = */ false);

if (!outcome.groundReady || !outcome.navigationReady) {
    // Hold simulation movement into affected terrain until collision and
    // navigation are consistent. Keep previously resident ground until
    // the new GPU/collider material has been published.
}
// WorldScene::update(...) + draw(...) occur in the regular app frame.
// After worldScene.draw(...), terrain.draw(renderer,originCm).
```

On `Simulation` rollback or restoring a saved game, invoke `publishFrame` with `afterRollback=true` even when the ECS state revision equals the previous one. This invalidates cached terrain and grass meshes and restores the fallen-tree visibility set without interfering with separate harvested-tree removals. The host must keep native Box3D and other physics ownership synchronized on its own owner thread.

**Known limits:** Native code must reserve disjoint collider IDs and avoid duplicate old static terrain colliders. `WorldScene` only hides the original static ground draw where the dynamic replacement is GPU resident. `nav::Mesh` must be initially built, and its source must use the same EarthworksLayer. A resource's large sparse state may cost memory when COW snapshots are detached. Box3D falls need a separate authoritative server transform stream, and the heightfield cannot create underground caves or overhangs.

**QA status:** Source committed but this example and Stage 53–55 features have NOT been compiled, executed, tested or benchmarked; tests are scheduled after functional completion. Do not merge Draft PR until native build and gameplay/graphics tests confirm behavior.


## Stage 57–59: automatically discover shore and riverbed sediment carriers

The shoreline/riverbed rasterizer reads the *same* ECS water bodies, river splines, polygon footprints, confluence/cascade connections and TerraWeave terrain used by the native game. It does not simulate water separately. On map load or AFTER water geometry/soil topology changes, invoke this during a safe structural ECS barrier **before** NEREID sampling:

```cpp
terraweave::ShorelineRasterSettings scan;
scan.area={-6400,-6400,6400,6400}; // 128x128 1m lattice at most
scan.includeWetBedCells=true;      // also carry sediment through the interior riverbed
scan.minWetBedDepthCm=5;
scan.maxWaterDepthCm=3000;         // avoid unbounded deep-ocean interior cells
scan.splashHeightCm=100;
auto soil=terraweave::ShorelineSoilAt{
    [&](terraweave::EarthVertex p,int32_t elevation)->lib::nereid::ShoreSoil {
        (void)elevation;
        const auto c=terraweave::climateAt(cfg.generator,biomeSettings,
            p.x*cfg.soilGridCm,p.z*cfg.soilGridCm);
        return terraweave::biomeShoreSubstrate(c.biome);
    }
};
auto refreshed=examples::terraweave_native::refreshDetectedShoreline(
    sim.world(),cfg,scan,soil);
// refreshed.created / updated / disabled / manualSkipped
```

Prefer an **authored soil-material map** over the fallback biome→substrate mapping: rocks should block erosion, sand/silt can migrate, clay/gravel require stronger currents. An optional root-binding callback returns 0..1000 from saved root/vegetation support state. The raster detects wet/dry transition banks using actual NEREID coverage (polygon, river spline, ocean or lake) and bounded wave splash. With `includeWetBedCells=true`, fully submerged interior cells also participate, allowing sediment to cross broad rivers rather than only the perimeter. These remain one bounded bank/bed ECS entity per *terrain grid vertex*, never one per grass blade.

`Integration.h::registerSystems` installs stages in canonical order:

1. NEREID water sampling; local erosion/deposition from `WaterBankSoilComponent`.
2. `nereid.sediment_transport`: simultaneous mass-conserving micro-centimetre transport down the Fixed current, with saved carry. Source and target must be in the same body OR explicitly linked by a valid NEREID confluence/cascade inlet→outlet.
3. `terraweave.bank_commit`: applies NEREID's **already calculated** local ground edits once to canonical ECS Earthworks, then root support and grass coverage.

A former bank that becomes dry is disabled but keeps its suspended sediment in saved ECS state; moving water cannot silently discard that material. The terrain owner must rerasterize when river course/water geometry changes (NOT every frame), and all generated cells must remain under the configured 16,384 sample budget. Deposition on a low-flow wet-bed cell raises its height. Once it emerges above the surface, subsequent topology refresh can turn it into a new sandbar/shoreline. This is a bounded sediment policy, not a solved full 3D Navier–Stokes sediment model.

**Stage57–59 source is committed, but this example and the new kernels have NOT been compiled or run in a native game or CTest yet.** The native host still owns the terrain mesh/collision/NavMesh publication, visual terrain LOD, physics and replication barriers.


## Stage 60–61 — slumping banks and excavation-driven collapse

The opt-in example `registerSystems` enables unstable-slope resolution by default. A material's effective angle of repose comes from `SlopeSoil` (NEREID bank soil class and wetness, with optional caller-provided nonbank soil maps). Water saturation weakens sand/silt/clay; root binding strengthens plant-covered ground. After NEREID bank erosion and downstream suspended sediment transfer, `TerraWeaveEcsAuthority` stages a slope failure preview, merges downhill soil movements with the original bank erosion outputs and publishes ONE `Earthworks` soil revision. Existing NEREID bank ECS groundCm values at landslide vertices are synchronized to the final height, so the next water tick sees the same terrain. Sediment suspension/carry is preserved, never recomputed by the terrain system.

A player dig may use `excavateAuthoritativeEcs(ecs,cfg,brush,tick,true,slopeSettings)` to trigger the same local slide without any water. The returned `TerraWeaveAuthoritativeEcsChange` contains `slope.slides`, `rooted.fell` and `grass`. Route each fall event to the existing optional Box3D `FallenPlantPhysics` on the native server owner thread; do not step Box3D within ECS. The same final soil revision is consumed by GPU/SpatialWorld/NavMesh and near/far vegetation via `NativeTerraWeaveWorldBinding`.

`SlopeFailureSettings` bounds max slides per tick and soil moved per slide, default max 4096 disjoint source/destination pairs and 40cm displacement each. Genuine caves, overhangs, granular avalanche dynamics, debris bodies and secondary effects on water flow are not implemented by this bounded heightfield policy. New Stage60–61 sources have NOT been compiled or tested yet; PR stays Draft. Read `docs/terrain/stage-60-wet-slope-failure-handoff.md` and `docs/terrain/stage-61-nereid-slopes-ecs-handoff.md`.


## Stage 62 — fallen logs, landslide debris and NEREID flow feedback

`Integration.h::registerSystems` enables optional persistence for `nereid.flow_obstruction` and `terraweave.auto_flow_obstruction` without requiring Box3D to be linked by ordinary native water users. Only the native server that actually uses Box3D includes [PhysicsIntegration.h](PhysicsIntegration.h).

A completed authoritative soil tick may produce `EcsBankTerrainCommit::slope.slides` and `EcsBankTerrainCommit::vegetation.fell` (or `TerraWeaveAuthoritativeEcsChange::slope/rooted.fell` for player digging). At the Box3D owner-thread barrier, consume these ONCE:

```cpp
#include "examples/world/terraweave_native/PhysicsIntegration.h"

// PhysicsScene is your existing server-owned physics::Scene.
// Allocate these one time; they keep stable event IDs and bounded handles:
terraweave::FallenPlantPhysics fallenTrees(physicsScene);
terraweave::SlopeDebrisPhysics slopeDebris(physicsScene);

// Once per completed soil tick, outside ECS systems:
auto effects = examples::terraweave_native::applySlideAndTreePhysics(
    sim.world(), cfg, commit.slope, commit.vegetation.fell,
    commit.completedTick, slopeDebris, fallenTrees, forestScene);
// effects: dynamic debris bodies and actual fallen tree bodies created.
// The source soil mass has ALREADY moved in Earthworks.
// Debris bodies are collision/visual proxies, not a second ground-mass store.

// On the authoritative Box3D step:
physicsScene.step();
// At the next ECS structural barrier, BEFORE NEREID samples water:
auto obstructionChanges =
    examples::terraweave_native::publishObstructionsAfterPhysics(
        sim.world(), worldScene, physicsScene,
        &fallenTrees, &slopeDebris,
        [&](Entity waterBody)->std::optional<uint64_t> {
            // Map canonical ECS body to NereidSceneSurface.id
            return sceneIdForWaterEntity(waterBody);
        });
```

NEREID's existing `sampleWater` and `WaterQueryIndex::sample` now both apply the exact same **Fixed-point** local obstruction policy AFTER river currents, vortices, confluences and cascades: debris and tree trunks slow water, with bounded lateral deflection based on which side of the obstruction a probe lies. This affects bank erosion, sediment advection and water interactions **on the following authoritative tick**. A mirrored immutable copy of those obstruction records is sent into `WorldScene`; water whitewater shader packets and ripple drift use the same deflection. Manual NEREID flow-obstruction entities are not overwritten by the generated tree/debris publisher.

**Important:** The real water solver/geometry is still owned exclusively by NEREID. This is a local obstruction current approximation: it does NOT integrate hydrodynamic pressure, raise upstream water levels, flood new terrain or reroute the water body mesh. `WorldScene` needs an explicit ECS body → stable scene-water ID mapping; never assume ECS index equals the scene ID. Physics poses are float/server-only and must be replicated; clients only use authoritative water/terrain and rendered transforms. Transient Box3D debris has a bounded lifetime and count and does NOT create a new terrain mass source. Do not generate a physics body for every grain of sand or grass blade.

On shutdown, call `slopeDebris.shutdown()` and `fallenTrees.shutdown()` **before** destroying their `physics::Scene`. Refresh generated water obstacles to an empty snapshot if physics sources disappear. After ECS rollback/load, the native host must restore/recreate authoritative physical bodies before recapturing water obstruction records.

**Verification:** Stage62 source is committed, but no Clang/GCC, CMake, CTest, native ECS replay, Box3D scene, Metal/GL/Vulkan screenshot or performance test was run. Keep PR #24 Draft; the next agent should validate the first native compile before integration sign-off.


## Stage 63 — water accumulation behind logs/landslides and dry-bank overtopping

The **existing NEREID** water pipeline optionally owns a bounded equal-area flood reservoir (`WaterFloodResource`, ECS resource schema `nereid.flood` v1); TerraWeave only supplies its canonical terrain heightfield. Native sample `registerSystems` enables this opt-in pipeline and the post-terrain ground sync system. No separate water-body renderer/physics solver is created.

**Seed once** after real NEREID water-body creation and before the first ECS flood-routing tick:

```cpp
// Already registered in the sample's GameModule::setup via
// examples::terraweave_native::registerSystems(sim,cfg,...).
// Example: 100 x 100 cells at a 1m grid. This window includes the bank
// into which water could overtop, not just the original river footprint.
terraweave::NereidFloodGridRegion riverWindow;
riverWindow.area={-5000,-5000,5000,5000};
riverWindow.maxCells=16384;
examples::terraweave_native::seedFloodReservoir(
    sim.world(),cfg,riverWindow);
```

**Optionally** pass a matching, bounded `ShorelineRasterSettings` scan as the **last** argument to `registerSystems`. This installs the automatic bank-refresh system after the NEREID sampling/flood/erosion/sediment stages AND after TerraWeave's slope/ground sync. The refresh system persists its last wet/dry and soil revisions, and rerasterizes when a meaningful topology change occurs; it only creates/modifies auto-tagged bank entities. If NEREID water geometry changes without a flood wetness change, request a rescan explicitly:

```cpp
terraweave::requestAutomaticShorelineRefresh(sim.world());
// At the next soil/flood system barrier the bank raster is regenerated
// from authoritative NEREID water footprints + TerraWeave ground.
```

**Tick order and volume:** NEREID flood routing moves water between grid-neighbour columns along the hydraulic-head gradient, respecting `WaterFlowObstructionComponent` permeability. The volume-equivalent sum of equal-area water columns is checked for exact conservation after every tick. Water can pile up upstream of a fallen tree/rock blockage, take an open side route and overtop a lower dry bank. The already-existing NEREID base WaterBodyComponent remains unchanged. After NEREID bank erosion and TerraWeave cliff slides/digging, `TerraWeaveFloodGroundSyncSystem` resamples changed ground heights while keeping the saved water column volumes. A new wet/dry boundary triggers auto-bank updates for the following ECS tick; no grass-blade ECS entities are created.

**Visuals:** `publishNativeTerraWeaveFrame` copies up to 2048 flood surface tiles from the current NEREID ECS resource into `WorldScene::setNereidFloodCells` for the active terrain/grass viewing rectangle. One batch of transparent/unlit quads is uploaded/drawn by the existing `gfx::Device`/`Renderer3D` pipeline. Only water that rises ABOVE the authored base surface or overflows OUTSIDE the original water footprint gets an overlay, avoiding duplicate river/ocean meshes. The fallback material is deliberately simple; full NEREID surface refraction/foam and exact seamless triangulation around arbitrary coastlines remain future work.

**Important finite-volume limitation:** The reservoir conserves volume only in its declared closed window. The base NEREID water body's static volume/level is **not** globally coupled to this separate finite-grid storage; this is a local flood policy, not pressure-based Navier–Stokes or unlimited river discharge. Do not reseed the resource after simulation starts, and do not migrate/change its cell size or region by discarding existing volume. Server authority and ECS save/rollback must include `WaterFloodResource` and all NEREID bank/obstruction resources; the TerraWeave standalone Stage53 save payload does not contain the NEREID flood reservoir.

**Status:** Source committed, NOT built/tested for Stage63. See `docs/terrain/stage-63a-nereid-flood-volume-handoff.md` and `stage-63b-flood-visual-native-handoff.md`. PR #24 stays Draft. After remaining feature work run native CMake/CTest, flood mass/rollback/reference-vs-indexed parity fixtures, Box3D physical obstruction scenarios and Metal screenshots.


### Finite upstream supply, blocked channel and downstream outflow (Stage 63c)

A closed grid cannot accumulate new water upstream of a blockage unless there is an initial higher hydraulic head or an **explicit source of inflow**. To model a finite river reach, configure source/outlet grid cells with a conserved external water reserve **before the first water-routing tick**. This is not an infinite pump: `WaterFloodResource::externalVolumeCm` is included in the exact before/after volume budget, with no new water magically created.

```cpp
// riverBody is the existing, generation-safe Entity returned by
// examples::terraweave_native::addWaterBody(ecs,body).
// The previously seeded grid is 1m cells in [-50,50) x [-50,50).
using lib::nereid::WaterFloodPort;
using lib::nereid::WaterFloodPortKind;
examples::terraweave_native::configureFloodRiverPorts(
    sim.world(),
    {
        WaterFloodPort{-30,0,WaterFloodPortKind::Inflow,
                       6,riverBody},
        WaterFloodPort{30,0,WaterFloodPortKind::Outflow,
                       6,kNullEntity}
    },
    /*finiteExternalReserveCm=*/100000
);
```

The inlet can supply up to 6cm-equivalent of the source cell's area per fixed simulation tick from the **already-counted finite reserve**. The outlet returns up to 6cm-equivalent to the same reserve. If a fallen tree or landslide reduces permeability, water backs up along the higher-head upstream cells, then can overtop lower adjacent dry banks instead of moving through the blocked edge. As the reservoir empties, supply stops naturally; there is no infinite static river discharge unless the game explicitly replaces or extends the model with its own accounted boundary conditions.

`WaterFloodTick` reports grid transfers, overtopped edges, new wet/dry cells, water supplied and drained. `totalBeforeCm` and `totalAfterCm` include BOTH the grid water and external store and must match. Changes in wet/dry **final state** increment `WaterFloodResource::wetnessRevision`, triggering optional shoreline rerasterization. The ECS save/rollback resource includes the reservoir amount and port configuration; replication should send the canonical NEREID flood resource/deltas from server. Do not directly mutate the reservoir from an async graphics job, and never reseed it after simulation starts.

**Status:** Source committed but native compilation/CTest, actual blockage/overflow, Metal shader visuals and water mass invariants are NOT yet tested. Refer to `docs/terrain/stage-63c-finite-river-ports-handoff.md` and the Stage63a/b handoffs.


## Stage 65 — budgeted rooted tree/shrub ecology, shade and native saplings

The native sample optionally registers a persistent rooted forest ecology, separate from Stage64 aggregate grass recovery. It covers the interactable rooted trees/shrubs; the many cosmetic Arbor/Flora trees remain procedurally scattered without individual ECS entities.

```cpp
terraweave::ForestEcologySpecies pine;
pine.speciesId=1; // same Arbor/Flora prototype species ID
pine.matureTicks=120000;
pine.lifespanTicks=2400000;
pine.seedIntervalTicks=24000;
pine.shadeTolerancePermille=350;
pine.shadeDeathTicks=240000;
pine.floodTolerancePermille=450;
pine.floodDeathTicks=80000;
pine.droughtDeathTicks=240000;
pine.seedSpreadCm=700;

terraweave::ForestEcologySettings forest;
forest.intervalTicks=32;
forest.maxPlantsPerStep=256;
forest.maxTrackedPlants=65536;
forest.maxRecruitsPerStep=8;

// Trailing args: optional rooted species policies, budgets, event sink.
examples::terraweave_native::registerSystems(
    sim,cfg,
    {},true,{},std::nullopt,true,{},
    std::vector<terraweave::ForestEcologySpecies>{pine},forest,
    [](const terraweave::ForestEcologyEcsOutcome& changed){
        // Queue changed.ecology.born/died for native post-tick consumers.
        // NEVER step Box3D or draw from within an ECS system.
        (void)changed;
    });
```

Add gameplay-interactable rooted trees to the canonical ECS resource during authorized map setup (do not add one ECS entity per decorative tree):

```cpp
auto state=*sim.world().resourceConst<
    terraweave::TerraWeaveAuthorityResource>();
terraweave::AnchoredPlant tree;
tree.anchor.plantKey=123;
tree.anchor.speciesId=1;
tree.anchor.xCm=1500;
tree.anchor.zCm=1700;
tree.anchor.plantedHeightCm=terraweave::heightAtCm(
    cfg.generator,tree.anchor.xCm,tree.anchor.zCm);
tree.anchor.roots=terraweave::defaultRootProfile(
    terraweave::PlantHabit::Tree);
tree.environment={0,pine.matureTicks,pine.lifespanTicks,1000,650,800};
state.rooted.push_back(tree);
++state.stateRevision;
sim.world().setResource<terraweave::TerraWeaveAuthorityResource>(
    std::move(state));
```

Persistent `terraweave.forest_ecology` schema v1 stores stable cursor, last evaluation tick, shade/flood/drought stress, permanent natural death and recruited-sapling identities. Every configured ecology interval (default 32 ECS ticks), a spatial 32m canopy index is built over the bounded rooted subset, and at most 256 selected plants receive expensive age/light/water/seed calculations. New saplings receive a different stable key, exact terrain height and newborn age. Existing `WorldScene` Forest/Flora batching renders the sparse recruited `FoliageInstance` via the existing shared Arbor species prototype/LOD and hides naturally dead plants separately from harvested or fallen trees.

The default native tree/shrub climate uses the NEREID water sample at the root zone (including Stage63 flood overlays) to track prolonged waterlogging; optional `ForestEcologyClimateOverride` supplies external PHYSIS sunlight, moisture and nutrients. Canopy overlap/shade is a cheap integer proxy, not true photometric sunlight/GI or per-leaf shadowing.

A tree that is **naturally dead** is presentation-hidden, not automatically converted into a Box3D fallen log, collision/resource object or decomposing vegetation. Harvesting gameplay must update canonical rooted state, rather than hiding only an Arbor render instance; otherwise its hidden plant could continue to compete/reproduce.

The default evaluation budget bounds costly **per-plant processing**; building the spatial canopy and staging native persistent ECS snapshots still costs O(N) for the active rooted subset at the ecology interval. Mass decorative forest remains procedural and stateless, and new source is not yet performance-tested. Stage65 commits have NOT been compiled or run through CTest/Metal/Box3D/ECS replay. See `docs/terrain/stage-65-rooted-forest-ecology-handoff.md`.


## Stage 66 — authoritative harvest/replant with persistent forest visibility

This optional feature connects old `ForestScene::removeTree` presentation to
the canonical ECS simulation. The new `terraweave.forest_harvest` v1 resource
stores a sorted, bounded list of stable `{speciesId,plantKey}` tombstones.
Enable it by passing `enableForestHarvest=true` as the final argument to
`examples::terraweave_native::registerSystems`, or explicitly call
`terraweave::registerTerraWeaveForestHarvestPersistence(sim)` once before
registry sealing. Existing games retain their legacy presentation-only
removals when harvest persistence is disabled.

At a server-authoritative structural/command barrier, **outside** a
`World::each` callback or active Box3D/render phase:

```cpp
#include "engine/world/TerraWeaveForestHarvestEcs.h"

terraweave::AnchoredPlantId id{speciesId, stablePlantKey};
auto event=terraweave::harvestNativeForestTree(
    sim.world(),id,sim.tick());
if(event.changed) {
    // Award inventory once, enqueue collision removal for Box3D owner,
    // and replicate the authoritative command/snapshot to clients.
    // event.uprooted is populated for an interactable rooted tree.
}

// An explicit replant/undo may remove the tombstone. For an ECS-grown
// sapling, provide its validated new planted AnchoredPlant; ordinary
// generated cosmetic foliage can be unhidden with no rooted record.
bool restored=terraweave::restoreHarvestedForestTree(
    sim.world(),id,optionalNewRootedPlant);
```

The harvest transaction copies and commits the authoritative rooted state,
optional Stage65 ecological history and the harvest tombstones atomically via
the existing World COW resource. A rooted harvested plant is **removed**
from canopy competition, seed production and root-support sampling. A newly
recruited sapling's identity is saved with the harvest tombstone; on explicit
replant, its `recruited` ecology marker is rebuilt so near Arbor/Flora can
render the new plant instead of expecting nonexistent procedural scatter.
Repeated harvest commands are idempotent and do not emit another inventory
reward. There is no per-tree render mesh or grass-blade ECS entity.

After the tick, the existing `NativeTerraWeaveWorldBinding::reconcile`
publishes `setRemovedTrees` from the saved tombstones; streaming, load and
rollback reconstruct the same visibility. Soil revision and water flow are
unchanged. Do **not** call `ForestScene::restoreTree` as a replacement for the
authoritative restore when the new resource is enabled. A game-specific
inventory, replanted physical collider, fallen/deadwood debris cleanup and
NavMesh blocker update remain the native host's responsibility. Natural death
is NOT counted as a harvest and a fallen tree is NOT automatically harvested.

**QA gate:** new Stage66 source has been committed, but no native build,
CTest, save/load, rollback, Metal/GL/Vulkan visuals or Box3D interaction has
been run. Keep PR #24 Draft; see
`docs/terrain/stage-66-authoritative-harvest-handoff.md`.


## Stage 67 — deadwood, conserved nutrients and native Box3D cleanup

The new optional module adds a persistent, budgeted organic-matter ledger
`ForestDeadwoodResource`, with one stable deadwood ID per opted-in
interactive tree/shrub and one integer nutrient amount per 32m soil cell.
This is **not** a second heightfield, a second water simulation, an entity
per grass blade or a per-frame forest mesh rebuild.

Enable by passing the new final `enableForestDeadwood=true` argument to
`registerSystems` (defaults to false); optionally provide
`ForestDeadwoodDecaySettings` and an `onDeadwoodDecay` callback.
For direct native setup register persistence, then append
`addTerraWeaveForestDeadwoodSystem` AFTER
`addTerraWeaveForestEcologySystem`. Stage65 natural mortality automatically
seeds newly dead plants in the same World COW transaction. If no
ecological species policy is enabled, a game may still seed fallen
root-supported trees at a safe ECS structural barrier:

```cpp
#include "engine/world/TerraWeaveForestDeadwoodEcs.h"
#include "examples/world/terraweave_native/PhysicsIntegration.h"

// Use the authoritative Earthworks root-support fall events from a
// completed dig/landslide/erosion transaction. Do not step physics here.
const size_t newlySeeded=terraweave::seedFallenForestDeadwoodEventsEcs(
    sim.world(),change.rooted.fell,sim.tick());

// After the ECS decay callback enqueues its outcome, run this on the
// Box3D owner thread, outside ECS simulation:
const size_t bodiesRemoved=
    examples::terraweave_native::removeFullyDecayedFallenBodies(
        completedDecay,fallenTrees);
// Rebuild fallen-tree NavMesh blocker snapshots and refresh NEREID
// obstruction mirrors on the next owner-thread barrier.
```

A harvest of already-decomposing wood goes through
`harvestNativeForestTree`; it exports only the remaining saved organic
units and avoids a duplicate soil nutrient payout. The previous
`NativeForestHarvestOutcome::exportedDeadwoodUnits` value exposes how
much was carried away. Natural death is not automatically a harvest or a
new Box3D fall: native gameplay still decides when a trunk falls,
becomes a collidable log, can be collected, and has its NavMesh blocker
removed.

Every decay step uses integer simulated ticks and bounded persistent
cursors, not camera distance or wall clock. Decomposed organic matter
splits into the local saved nutrient pool and an explicit respiration
sink; `deadwoodLedgerBalanced(resource)` checks that no units appeared
or vanished. Living trees/shrubs receive up to +250 fertility permille
and Stage64 grass patches use five aggregate sample locations. These
are health/regrowth **proxies**: actual mineral uptake by living plants,
runoff/leaching into NEREID, fungal networks, bespoke corpse meshes and
natural-death Box3D spawning remain future feature stages.

**Testing status:** source uploaded and read back through GitHub; this
stage has **not** been compiled, executed in a native game, or verified
on Metal/GL/Vulkan/Box3D/CTest. See
[Stage67 agent handoff](../../../docs/terrain/stage-67-deadwood-soil-nutrients-handoff.md)
for exact commits, budgets, dependencies, QA and next work.
