# AIVERSE PHYSIS

PHYSIS is the **genre-independent** nature simulation for AIVERSE. Weather, local climate zones, wet ground, snowfall, fire, shallow flooding and quake intensity are authoritative ECS state. It is **not** coupled to FPS/RTS or any renderer.

## Modules

| Header | Responsibilities |
|---|---|
| `engine/physis/Physis.h` | Fixed-point authoritative cell grid, Atmosphere/Surface/State, seeded tick, queries |
| `engine/physis/PhysisClimate.h` | ClimateZone, WeatherFront, seasonal forcing, regional tick |
| `engine/physis/PhysisEnvironment.h` | SurfaceMaterial, snow/wetness/ice, generic response and friction/runoff hints |
| `engine/physis/PhysisDisasters.h` | Optional fuel/fire, shallow flooding, QuakeEvent, deterministic old/new buffers |
| `engine/physis/PhysisWind.h` | 3D wind zones, deterministic gusts, shelter attenuation and `windAt` |
| `engine/physis/PhysisGravity.h` | World gravity baseline, additive/priority gravity fields, `gravityAt` |
| `engine/physis/PhysisStorm.h` | Opt-in hurricane/tornado vortex, storm tracks, climate rain/pressure forcing |
| `engine/physis/PhysisForces.h` | Quadratic wind drag, force receiver/output ECS and pure force sampling |
| `engine/physis/PhysisPresentation.h` | Unified read-only `NatureFieldSample` for native Terrain/FX/Physics hosts |
| `engine/physics/PhysisBox3D.h` | **Optional Box3D** physics::Scene adapter; does not modify PHYSIS core |
| `engine/physis/PhysisDiagnostics.h` | Read-only bounded debug raster, scalar layers and 16-bit normalization |
| `engine/physis/PhysisDebugPalette.h` | Renderer-independent CPU diagnostic colours and invalid-cell masking |
| `apps/editor/windows/PhysisWindow.cpp` | Optional dockable editor heatmap with separate demo World and read-only live ECS source |
| `engine/physis/PhysisSpatialIndex.h` | Transient COW snapshot, 2D bins, ordered candidate selection for wind/gravity/storm fields |
| `engine/physis/PhysisSceneGeometry.h` | Pure Fixed-metre tile quads for Scene projection (presentation only) |
| `engine/physis/PhysisProjectConfig.h` | Optional JSON `physis` map block → `InstallOptions` / `installFromMap` for any native GameModule |
| `engine/physis/PhysisFieldAssets.h` | Validated, stable-ID wind/storm/gravity asset parser and deterministic ECS spawning |
| `apps/editor/PhysisFieldEditor.h` | Universal editor field list, typed inspector, Scene markers and one-Undo drag gizmo |
| `apps/editor/PhysisAuthoring.h` | Validated RTS/FPS map authoring, one `MapDoc::beginEdit` per Apply, Undo/Redo |
| `apps/editor/PhysisSceneOverlay.h` | Editor-only camera projection of read-only PHYSIS fields and zone rings |
| `engine/physis/PhysisIntegration.h` | Generic Simulation::addSystem, persistent ECS schemas, read-only presentation feed |

## Quick integration for any C++ native game

```cpp
#include "engine/physis/PhysisIntegration.h"
void MyGame::setup(aiverse::Simulation& sim, const aiverse::Json&, const aiverse::Json&) {
    aiverse::physis::InstallOptions options;
    options.climate.seed = sim.config().seed;
    options.climate.width = 32;
    options.climate.height = 32;
    options.enableDisasters = true;
    options.enableStorms = true;        // optional hurricane/tornado update
    options.enableForces = true;        // optional ECS ForceReceiver/ForceOutput pass
    options.enableWindZones = true;     // persist WindZone/WindShelter when used
    options.enableGravityFields = true; // persist GravityField/GravitySettings when used
    // When NEREID owns water: options.floodMode = physis::FloodMode::ExternalAuthority;
    aiverse::physis::install(sim, options);
}

// After committed Simulation::step() (e.g. from renderer/editor):
aiverse::physis::forEachPresentationCell(sim.world(), [](const auto& cell) {
    // cell.environment.wind / wetness / rain / snow / ice
    // cell.hasHazards ? cell.hazards.fire / waterDepth / quake : none
});
```

All values used by gameplay use `Fixed`; never feed presentation floats back into PHYSIS. PHYSIS world coordinates are a static X/Z grid; `Cell::elevation` is project-assigned. For custom host scheduling choose exactly one `advance` variant per tick. To register PHYSIS into generic Simulation and persistent ECS schema, install during `GameModule::setup`.

## Current implementation boundary

- The implemented weather evolution is a deterministic simplified local model, not a physically accurate global circulation model; seasons are triangle waves, fronts have Manhattan falloff.
- Fire, flood and quakes are base ECS mechanics; hurricane/tornado are simplified deterministic vortex/forcing fields, **not** complete atmospheric CFD, volcano, avalanche, fluid, terrain deformation or destruction solvers.
- Responses like traction multiplier and runoff potential are coefficients/hints. An **optional, explicit** `engine/physics/PhysisBox3D.h` adapter now applies PHYSIS wind/ gravity corrections to `physics::Scene` dynamic bodies, but no automatic Scene/WorldScene hookup is wired. Since 2026-11-21 an opt-in **nature presentation host** drives TerraWeave grass (burn/wind), SpectraFX fire/smoke, fire light and audio, and the editor shows it live without a play-test (see «Nature presentation host» below); the native game client is not hooked up yet.
- Grid updates can run on several threads (bit-identical) and only in active chunks (see "Automatic integration, parallel update and streaming" below). Still no distributed weather, disk paging of cells or grids above 1,048,576 cells.
- No compile/CTest/CI verification has been completed in this connector session; **code present does not mean tests passed**.

## Tests

Use the project-wide unified runner (required by AGENTS.md):

```sh
./run-tests.sh --filter '^physis_'
```

This performs full Debug configure/build before filtered CTest. For investigating a standalone target, see the individual handoff for exact CMake commands.

## AI agent continuity

- Stage 1: [Core](agent-handoffs/physis-core-2026-11-18.md)
- Stage 2: [Climate](agent-handoffs/physis-climate-2026-11-18.md)
- Stage 3: [Environment](agent-handoffs/physis-environment-2026-11-18.md)
- Stage 4: [Disasters](agent-handoffs/physis-disasters-2026-11-18.md)
- Stage 5: [Simulation/persistence](agent-handoffs/physis-integration-2026-11-18.md)
- Stage 6: [PHYSIS / NEREID / TerraWeave ownership and reuse](agent-handoffs/physis-ownership-terra-water-2026-11-18.md)
- Stage 7: [External water authority mode](agent-handoffs/physis-flood-authority-2026-11-18.md)
- Stage 8: [Wind, gust and shelter](agent-handoffs/physis-wind-2026-11-18.md)
- Stage 9: [Gravity fields](agent-handoffs/physis-gravity-2026-11-18.md)
- Stage 10: [Hurricane and tornado events](agent-handoffs/physis-storm-2026-11-18.md)
- Stage 11: [Forces and Box3D adapter](agent-handoffs/physis-forces-box3d-2026-11-18.md)
- Stage 12: [Numerical bounds and native CI](agent-handoffs/physis-numerical-ci-2026-11-18.md)
- Stage 13: [Unified environment sampling and save compatibility](agent-handoffs/physis-sampling-persistence-2026-11-18.md)
- Stage 14: [Read-only diagnostic rasters](agent-handoffs/physis-diagnostics-2026-11-18.md)
- Stage 13: [Unified nature field sampling and save-schema compatibility](agent-handoffs/physis-sampling-persistence-2026-11-18.md)


- Stage 15: [Fast diagnostic ECS reads](agent-handoffs/physis-diagnostic-fastpath-2026-11-18.md)
- Stage 16: [Snapshot-backed spatial index](agent-handoffs/physis-spatial-index-2026-11-18.md)
- Stage 17: [PHYSIS editor debug view](agent-handoffs/physis-editor-debug-view-2026-11-18.md)
- Main merge: [merged InputFlow/main verification](agent-handoffs/physis-main-merge-2026-11-18.md)
- Stage 18: [Scene overlay + Inspector authoring/Undo](agent-handoffs/physis-editor-scene-inspector-2026-11-18.md)
- Stage 19: [Field assets, Gizmos and single-Undo editing](agent-handoffs/physis-field-assets-gizmos-2026-11-18.md)Each next stage gets code commit(s) followed by a separate handoff commit with SHA, paths, real tests, gaps, and the next exact task. Agent-specific constraints: `engine/physis/AGENTS.md`.

## Automatic integration, parallel update and streaming (2026-11-22)

### PHYSIS from map data in every game

`Simulation::setup` calls `physis::autoInstallFromMap` (`engine/physis/PhysisAutoInstall.cpp`)
after `GameModule::setup`, for **every** module. A map with an enabled `"physis"` block now runs
the nature simulation in the client, headless, server and editor play-test alike (lockstep peers
and replays agree). Opt-outs: `"physis.enabled": false`, `"physis.autoInstall": false` (the module
installs it itself later), or a module that already called `installFromMap` in its own setup.
`"physis.trackedFields": true` selects `installTrackedFromMap`. CMake `AIVERSE_SIM_PHYSIS=OFF`
removes it from the link. Maps without the block are unchanged (no state, no system, same hash).

### Presentation without hand wiring — `engine/world/PhysisNatureRuntime.h`

One object per view, called once per rendered frame with optional targets. The native game client
(`apps/client/GameClient.cpp`, `natureStart/natureUpdate/natureStop`) does this for every map with
a `physis` block. What each part does:

| Part | Consumer | What it does | Data switch |
|---|---|---|---|
| Fire / smoke / fire light | SpectraFX `fx::ParticleSystem` via `PhysisNatureFxHost` | stable flame points + smoke from burning PHYSIS cells, particle drift from wind; project may override `fx/physis_flame.json`, `fx/physis_smoke.json` | `presentation.fx` |
| Sound | `audio::AudioEngine` via `PhysisNatureAudioHost` | positional looping fire per fire cluster, wind bed by wind speed; synth fallbacks `physis_fire_loop` / `physis_wind_loop` are added to the bank unless `audio.json` defines them | `presentation.audio` |
| Vegetation wind | TerraWeave through `WorldScene::setVegetationWind` + `setVegetationWindField` | region-mean PHYSIS wind (cm/s, gust by speed) and a bounded spatial lattice (`windFieldNodes`, ≤64², equal spacing) incl. WindZone/WindShelter/storms; only republished when it changes; authored wind restored on stop | `presentation.vegetationWind`, `windFieldNodes` |
| Water wind | NEREID through `WorldScene::setNereidVisualWind` (`NereidPhysisWindBridge.h`) | PHYSIS wind at the camera focus drives NEREID's visual waves/ripples; authored NEREID wind zones are not touched | `presentation.waterWind` |
| Sky | PRISMA physical sky via `PhysisSkyBridge.h` | humidity/precipitation → cloud coverage, haze, extinction; cloud drift by wind. Only when the map already enabled the physical sky; authored sky restored on stop | `presentation.sky` |

```json
"physis": { "...": "...", "presentation": { "fx": true, "audio": true, "vegetationWind": true,
  "waterWind": true, "sky": true, "radiusMeters": 160, "maxRegionCells": 96,
  "fxWindScale": 0.35, "windFieldNodes": 16 } }
```
`"presentation": false` keeps the simulation and switches all presentation off. Grass burn
overlay (`PhysisNatureGrass.h`) is still editor-only: `WorldScene` does not expose a
`GrassPatchRenderer` to hosts yet.

### Parallel grid update — `engine/physis/PhysisParallel.h`

`InstallOptions::workerThreads` / map `"physis.workerThreads"` (1 = serial default, 0 = hardware,
N lanes). Atmosphere, surface and the disaster read/compute/commit passes split their cell ranges
over a persistent thread pool; flood transfer is gathered per receiving cell, so results are
**bit-identical** to serial for every lane count (test `physis_parallel`; serial checksum A/B
against the previous sources is equal). Custom providers passed to `advanceWith` while lanes are
active must only read the World. Measured on the user's Mac (Release, 28 logical cores, other
builds running, load ≈24), 1024×768 grid with storms + disasters, 10 ticks, 3 runs:
1 lane 161.6–162.5 ms/tick; 2 lanes ×1.92–1.95; 4 lanes ×3.69–3.73; 6 lanes ×5.33–5.40;
8 lanes ×6.62–7.00. Linux VM (Debug, 4 cores): ×3.54 on 4 lanes.

### Chunk streaming — `engine/physis/PhysisStreaming.h`

`"physis.streaming": {"chunkCells": 32}` splits the grid into chunks. Any entity with an
`ActivityAnchor` (position + radius; players, cameras, vehicles, scripted regions) keeps chunks
within its radius simulated; other chunks are dormant (weather, surface, fire, flood frozen;
dormant cells are walls for flood transfer and never ignite). The mask (`ActiveChunks`) and
anchors are persistent, hashed ECS data recomputed by the `physis.streaming` system at the start
of every tick. No anchors = whole grid. Static anchors can be listed in `"physis.anchors"`.
This is simulation streaming (what is updated); cell data stays resident — no disk paging.
Test `physis_streaming`; bench `aiverse_physis_streaming_test bench 1024 10`.

### CI

`.github/workflows/physis.yml` builds the aggregate CMake target `aiverse_physis_tests` (every
`aiverse_physis_*_test(s)` target, plus `aiversemcp` when present) and runs `ctest -R ^physis_`
on Linux x64, Linux arm64, macOS and Windows MSVC, and builds the core tests with Emscripten and
runs them in Node. Before this, the hand-written target list missed `physis_nature_presentation`
/ `physis_nature_cues`, and the workflow failed on main with ctest exit 8 (run 44956413363).

## Tornadoes (2026-11-22) — `engine/physis/PhysisTornado.h`

A tornado is simulation, not a visual: a `StormEvent` (kind `Tornado`) plus a `TornadoTrack` on
the same entity. Every PHYSIS wind reader already sees the vortex through `windWithStormsAt`
(tangential swirl, radial inflow, updraft): forces, nature contacts / stress (trees fracture),
`sampleNatureField`, the presentation frame. The system `physis.tornadoes` runs once per tick right
after the climate/storm/disaster pass and before forces:

| Part | Rule |
|---|---|
| Path | `drift` (StormEvent `driftPerTick`), `wander` (heading + random turn from the track's own RNG state, reflected inside `bounds` or the PHYSIS grid), `waypoints` (constant speed, optional `loop`) |
| Life | `startTick/durationTicks` of the storm + `rampTicks` spin-up/decay; `TornadoTrack::intensity` 0..1 scales tangential wind, updraft and inflow |
| Fire (`enableDisasters`) | cells under the core lose `fire.suppression` of their fire per second (blown out); burning cells with fuel in the ring grow by `fire.fanning` per second (fanned) |
| Debris | `debris.perSecond` pieces torn up in the core (up to `debris.max` per tornado), carried by the same wind with quadratic drag + PHYSIS gravity, land on the cell elevation, expire after `lifeSeconds` or 3 s on the ground. Stored in the `TornadoDebrisField` resource — no entities, so other modules' entity allocation is unchanged |
| Carried objects | host entities with `Transform` + `NatureCarriable` (mass, area, drag, `liftWindSpeed`) are lifted when the horizontal wind exceeds the lift speed and carried until they land. Not for Box3D bodies (use `applyPhysisToBody`) |
| Damage | entities with a `Transform` within `damageRadiusMeters` (full in the core, linear to 0 at the radius) get `damagePerSecond × intensity` through `GameModule::environmentDamage(sim, target, amount, tornado)`; the module applies its own health/armour rules. RTS: pure damage via `dealDamage` (invulnerable types such as trees and units off the map are skipped). Default implementation: no damage |
| Vegetation | trees/structures with `NatureStressReceiverComponent` (forces + nature contacts + stress enabled) load from the vortex wind and fracture through the existing outbox → TerraWeave fall bridge. RTS tree units are invulnerable in the sample rules |

Determinism: Fixed-point only, tornadoes and carried objects in entity-index order, debris in
creation order, explicit RNG in the track. Persistent + hashed: `physis.tornado_track`,
`physis.nature_carriable`, `physis.tornado_debris` (registered only when tornadoes are enabled, so
maps without them keep the old save manifest and checksums).

### Map data

A non-empty `"tornadoes"` array turns the feature on (and storms with it). Per-second values are
converted with the simulation tick rate; `startSeconds`/`durationSeconds`/`rampSeconds` may replace
the `...Ticks` keys. The same parser (`PhysisTornadoAssets.h`) backs MCP `physis_validate` /
`physis_set`, so authoring and runtime accept the same documents.

```json
"physis": {
  "grid": { "originX": 0, "originZ": 0, "width": 16, "height": 12, "cellSizeMeters": 4, "seed": 11 },
  "climate": { "baseTemperature": 21, "baseHumidity": 0.55 },
  "tornadoes": [{
    "id": "valley_twister", "center": [16, 9],
    "coreRadiusMeters": 2.5, "outerRadiusMeters": 12, "heightMeters": 45,
    "tangentialWind": 42, "updraft": 16, "radialInflow": 6, "clockwise": false,
    "startSeconds": 2, "durationSeconds": 150, "rampSeconds": 4,
    "path": { "mode": "waypoints", "speed": 1.4, "loop": true, "points": [[24, 16], [32, 22], [40, 30]] },
    "damagePerSecond": 6, "damageRadiusMeters": 5,
    "fire": { "suppression": 0.8, "fanning": 0.5 },
    "debris": { "max": 40, "perSecond": 8, "massKg": 0.4, "areaSqMeters": 0.06, "lifeSeconds": 14 }
  }]
}
```
Wander instead: `"path": {"mode": "wander", "speed": 2, "turnDegreesPerSecond": 30, "seed": 3,
"heading": 0, "bounds": [minX, minZ, maxX, maxZ]}`. Sample map:
`templates/skirmish/maps/tornado_valley.json` ("Tornado Valley", map index 2).

C++ hosts: `InstallOptions::enableTornadoes = true` (persistence), then
`spawnTornado(world, storm, track)` at runtime; `advanceTornadoes(world, hooks)` is the pure pass
for custom schedulers. No Lua API yet.

### Presentation

`buildNaturePresentation` now samples `windWithStormsAt` (vegetation wind lattice and particle wind
include storms) and fills `frame.tornadoes` / `frame.debris`. `PhysisNatureRuntime` drives:

* SpectraFX funnel (`physis_tornado`: rising, widening dust spiral, ground skirt, dark specks;
  local space, moved every frame with extrapolation between ticks; scale = core radius) and one
  `physis_tornado_debris` chunk per airborne piece. Projects may override both in `fx/`.
* Sound: positional `physis_tornado_loop` roar (synth fallback added by `addPhysisTornadoSounds`).
* TerraWeave: the vortex shows in the spatial vegetation wind lattice (`windFieldNodes`).
* NEREID: within 3 outer radii of a tornado the water wind takes the swirl speed at the camera focus.
* Switch: `"presentation": {"tornado": false}` (funnel/debris effects off; simulation unchanged).

Tests: `physis_tornado` (paths, envelope, wind, fire, debris, carried objects, host damage, map
install, save/load/continue checksums, parser errors), `physis_tornado_cues` (frame, FX/audio
planners, built-in effects and sound parse). Limits: no Lua API; debris is a point cloud (no
collisions with units/buildings); damage reaches only entities with `Transform`; vortex is an
authored analytic field, not CFD; the editor nature preview does not draw funnels yet.

### Fire whirls, water and ground (2026-11-22) — `engine/physis/PhysisTornadoNature.h`

Every map tornado carries a `TornadoNature` next to its `TornadoTrack` (C++: the
`spawnTornado(world, storm, track, nature)` overload, defaults from
`defaultTornadoNature(tickRate)`). A tornado without it keeps exactly the rules above.

| Part | Rule |
|---|---|
| Fire drawn in | Each tick the vortex takes `fire.intake` (share per second) × intensity × ring weight of the fire of every PHYSIS cell under its core and ring (1 in the core, linear to 0 at the outer radius). The drawn fire (in burning m²) is added to `heat`, which loses `fire.decay` per second. `fireness = heat / (heat + fire.halfHeat)` (0..1): it grows with the amount and strength of the fire, a small fire gives a weak whirl, without fire it fades. |
| Blow-out vs. fire whirl | Core blow-out is `fire.suppression × (1 − fireness)`: a dust tornado still snuffs fire in its core, a fire whirl no longer does. Ring fanning is multiplied by `1 + fireness`. |
| Ignition | Every fuel cell under the vortex (not over water) catches fire with chance `fire.ignite × fireness × intensity × weight` per second, strength `fire.igniteStrength × fireness`. Random numbers come from the tornado's own RNG state (`TornadoNature::rng`), in grid order. |
| Embers | `fire.embersPerSecond × fireness` burning embers are thrown from the ring (up to `fire.maxEmbers`), carried by the vortex wind like debris (`TornadoEmberField`), burn out after `fire.emberLifeSeconds`, and set a fuel cell on fire with chance `fire.emberIgnite` where they land. |
| Heat damage | Entities with a `Transform` inside the outer radius take `fire.heatDamagePerSecond × fireness × intensity` (full in the core, linear to 0 at the outer radius) through `GameModule::environmentDamage`, in addition to the wind damage. |
| Water | Open water comes from `GameModule::environmentWaterDepth(sim, x, z)` (new, default 0; the generic game core returns 2 m for `~` cells and 0.3 m for `=` fords) or the PHYSIS local flood depth. The vortex is over water when 3 of 5 samples (centre + 4 on the core ring) are deeper than `surface.waterThresholdMeters`. |
| Land tornado over water | Loses `surface.waterDecay` vigor per second, recovers `surface.recover` on land; at 0 it dissipates (intensity 0, storm disabled). Heat is quenched by `fire.quench` per second, no ignition, no embers. |
| Waterspout | `surface: "auto"` decides at the first active tick: born over water = waterspout (`"water"` / `"land"` force it). Its wind is `surface.spoutStrength` × the authored wind, it raises spray (presentation) and loses `surface.spoutLandDecay` vigor per second on land, so it breaks up within seconds ashore. |
| Ground scour | On land the core (out to 1.5 core radii) removes `erosion.rate × intensity²` metres per second from `GroundScourField` cells (`physis.scourCellMeters`, default 1 m, sparse, at most 65536 cells) down to `erosion.maxDepthMeters`; the soil (up to `erosion.capacityCubicMeters`) is carried and dropped (`erosion.deposit` share per second) in a band 1.5–2.5 core radii around the centre. Over water the carried soil sinks. `groundScourAt(world, x, z)` gives the change in metres (negative = scoured). |

Map data (inside a tornado; all values per second, all optional):

```json
"fire": { "suppression": 0.8, "fanning": 0.5,
          "intake": 0.6, "decay": 0.2, "halfHeat": 50, "quench": 1.5,
          "ignite": 0.4, "igniteStrength": 0.6,
          "embersPerSecond": 6, "maxEmbers": 48, "emberLifeSeconds": 8, "emberIgnite": 0.5,
          "heatDamagePerSecond": 3 },              // "whirl": false = old blow-out only
"surface": { "kind": "auto", "waterThresholdMeters": 0.2, "waterDecay": 0.08, "recover": 0.1,
             "spoutStrength": 0.55, "spoutLandDecay": 0.6 },   // or "auto" / "land" / "water"
"erosion": { "rate": 0.04, "maxDepthMeters": 0.6, "deposit": 0.25, "capacityCubicMeters": 40 }  // or false
```
Defaults: intake 0.5, decay 0.2, halfHeat 60 m², quench 1.5, ignite 0.4, igniteStrength 0.6, embers 6/s
(max 48, 8 s, ignite 0.5), heat damage 4/s, waterDecay 0.08, recover 0.1, spoutStrength 0.55,
spoutLandDecay 0.6, erosion 0.04 m/s, max 0.6 m, deposit 0.25, capacity 40 m³.

New `physis` keys for fire scenes: `"ignitions": [{"center": [x, z], "radiusMeters": 6, "strength": 1,
"startSeconds": 1}]` (needs `enableDisasters`; applied by the stateless `physis.ignitions` system on
that climate tick), `"disasterConfig": {"fireSpreadRate", "fireGrowthRate", "fuelConsumptionRate",
"rainSuppressionRate", "wetnessSuppressionRate"}` (per tick, 0..1), `"noFuelOnWater": true` (cells the
host reports as water start without fuel), `"scourCellMeters": 1`.

Determinism and saves: Fixed-point only, tornadoes in entity order, cells in grid order, embers in
creation order, own RNG. `physis.tornado_nature`, `physis.tornado_embers`, `physis.ground_scour` are
persistent and hashed, registered only when tornadoes are enabled — maps without tornadoes keep their
save manifest and checksums.

Presentation (`PhysisNatureRuntime`, no extra wiring): the frame carries `fireness`, `spray`,
`waterspout`, embers and scour blocks (≥ 2 m). SpectraFX layers: the dust funnel fades as fireness
or spray rise; `physis_tornado_fire` (spinning flame column, glow, fire light, sparks, smoke on top,
alpha = fireness, 6 levels — a level change starts the new emitter while the old one fades out),
`physis_tornado_spray` (spray column, base cascade, rings on the water), `physis_tornado_ember`
(spark + small light per ember), `physis_tornado_scar` (flat dark soil mark per scoured block, up to
160). Sound: a second voice `physis_firestorm_loop` (synth fallback added by
`addPhysisTornadoSounds`), volume by fireness × intensity. Projects override any of them in `fx/` /
`audio.json`.

Fire textures: the built-in `physis_flame`, `physis_smoke` and `physis_tornado_fire` use CC0
flipbooks from Unity's free VFX image sequences (`engine/assets/fx/physis/`: `flame_16x4.png`,
`flame_tall_16x4.png`, `fireball_8x8.png`, `smoke_8x8.png`; source, licence and processing in
`LICENSE-fx.txt`). Ground flames are small upright additive licks standing on the ground (base pivot),
the whirl's tongues are aligned to their spiral motion, smoke is alpha-blended. A project overrides any
of them with `fx/<name>.json`.

Sample: `templates/skirmish/maps/firestorm_valley.json` ("Firestorm Valley", map index 3): a meadow
fire, a tornado that becomes a fire whirl, crosses the river and dies over the north-east lake, and a
waterspout on the lake that breaks up ashore. Tests: `physis_firestorm` (growth by amount, small fire
= weak, decay, ignition on the path, embers, heat damage, land tornado over water, waterspout, scour,
determinism, map install, save/load/continue checksums, parser), `physis_tornado_cues` (fire / spray /
ember / scar layers, flame roar).

Limits: the scour is PHYSIS data and a ground mark; the terrain mesh / heightfield / navigation is not
lowered (a TerraWeave Earthworks host may apply `GroundScourField`; no adapter yet). Water rings are
SpectraFX particles, not NEREID surface waves. Fire burns on PHYSIS cells (coarse grid); the fire
whirl's flames follow the vortex, not individual plants.

## Cross-system ownership (PHYSIS / NEREID / TerraWeave)

PHYSIS owns **climate/weather, natural event fields and environmental force/gravity policies**, but never Box3D rigid-body integration itself. NEREID (Water agent,
draft PR #23) owns water-body geometry, flow, contacts and buoyancy; TerraWeave
(Terra agent, draft PR #24) owns terrain, biomes, vegetation and land rendering.
Use the opt-in host integration plan and precise metre/centimetre conversion in
[`physis-nereid-terraweave-integration.md`](physis-nereid-terraweave-integration.md).
The existing PHYSIS shallow-flood prototype is **not** an authoritative
replacement for NEREID water. When a host uses NEREID as the water authority,
set `InstallOptions::floodMode = FloodMode::ExternalAuthority`: PHYSIS continues
to update fire and quake but clears/skips its local shallow-flood depth. This
opt-in switch is authored and covered by tests; cross-module NEREID/Terra
integration and real build/CTest verification remain pending.

This is a **design contract only**. Terra/NEREID integration, rendering and
cross-system tests remain NOT IMPLEMENTED / NOT RUN on this branch.

## Wind, gravity, storms and optional Box3D usage

```cpp
#include "engine/physis/PhysisIntegration.h"
#include "engine/physics/PhysisBox3D.h" // link AIVERSE::physics, if enabled

// During GameModule::setup:
aiverse::physis::InstallOptions options;
options.enableStorms = true;
options.enableForces = true; // only if generic ECS force outputs are required
aiverse::physis::install(sim, options);

aiverse::physis::StormEvent tornado;
tornado.kind = aiverse::physis::StormKind::Tornado;
tornado.center = {10_fx, 0_fx, 10_fx};
tornado.eyeRadiusMeters = 20_fx;
tornado.outerRadiusMeters = 150_fx;
tornado.heightMeters = 500_fx;
sim.world().add<aiverse::physis::StormEvent>(sim.world().create(), tornado);

// Physics owner: after the authoritative PHYSIS tick, just before Box3D Scene::step:
for (aiverse::physics::BodyHandle body : optedInBodies)
    aiverse::physics::applyPhysisToBody(box3dScene, sim.world(), body);
box3dScene.step();
```

PHYSIS authoritative types are `Fixed` ECS data. The external Box3D bridge
converts from/to Box3D floats **only at this boundary**. Box3D already applies
scene gravity; the adapter applies **delta gravity** and wind drag via
`Scene::applyForce`, not a second gravity force. Do **not** also apply
`ForceResult::totalForce` to the same Box3D body. Opt-in kinematic character
controllers need a separate locomotion integration; neither hardcoded scalar
`CharacterParams::gravity` nor native Body motion is overridden here.

Physics is opt-in. Run `./run-tests.sh --filter '^physis_'` and, with
`-DAIVERSE_ENABLE_PHYSICS=ON`, run CTest `physis_box3d` and the existing
`physics_runtime` / `physics_scene`. These tests are **authored but NOT RUN**.
There are no physically accurate Navier–Stokes hurricanes, tornado debris
destruction, N-body orbital solvers, Terra foliage wind deform or water surge
in the current implementation.

## Read-only combined sampling (for Terra, VFX, Physics and tools)

```cpp
#include "engine/physis/PhysisPresentation.h"

aiverse::physis::NatureFieldSample nature;
if (aiverse::physis::sampleNatureField(sim.world(), queryPosition, nature)) {
    const auto wind = nature.windVelocity; // 3D Fixed metres per second
    const auto gravity = nature.gravityAcceleration; // m/s squared
    if (nature.hasAtmosphere) {
        const auto rain = nature.environment.rain; // normalized, not water volume
        const auto snow = nature.environment.snowpack;
    }
    if (nature.hasHazards) {
        const auto fire = nature.hazards.fire;
    }
}
```

`sampleNatureField` is immutable and returns true when PHYSIS is initialized,
including outside the climate grid; `hasAtmosphere`/`hasHazards` indicate
which grid data is present. It does not depend on TerraWeave/NEREID/Box3D
headers. Terra should consume this as a read-only environment overlay and
sample NEREID's canonical waterline separately. **No production Terra/NEREID
adapter or render scene is linked yet.**

**Save compatibility:** `Simulation::PersistentEcsRegistry` stores a strict
list of schema IDs. Therefore PHYSIS **does not register** the new optional
wind/gravity/storm/force component types by default. Set
`InstallOptions.enableWindZones` when persisting `WindZone`/`WindShelter`,
`enableGravityFields` for `GravityField`/`GravitySettings`,
`enableStorms` for `StormEvent`, and `enableForces` for
`ForceReceiver`/`ForceOutput`. Existing Stage 1–5 save registry remains
unchanged when these switches are off. **Switching flags on an existing saved
game changes the manifest and is not an automatic save migration.** Save and
load must use the same feature flags. Tests are authored, not run.

## Diagnostic raster for editor/debug consumers

```cpp
#include "engine/physis/PhysisDiagnostics.h"
aiverse::physis::DiagnosticRequest query;
query.originX = -16; query.originZ = -16;
query.width = 32; query.height = 32;
query.stride = 1;
query.layer = aiverse::physis::DiagnosticLayer::WindSpeedMetersPerSecond;
aiverse::physis::DiagnosticRaster raster;
if (aiverse::physis::sampleDiagnosticRaster(sim.world(), query, raster)) {
    // raster.rawValues = Fixed::raw data; raster.valid = inside PHYSIS grid
    // diagnosticNormalized(raster, i) = 0..65535 for view/shader coloring
}
```

The diagnostic API is **CPU-only and read-only**: it does not render an
overlay in Scene, change physics, mutate the authoritative World, or sample
NEREID water. It is capped at 65,536 cells per query; missing climate/hazard
cells are marked invalid, not replaced with misleading zeros. Only PHYSIS
standalone shallow flooding is displayed by `LocalFloodMeters` (NEREID remains
canonical water authority). A future editor adapter may upload/use the returned
raster as a heatmap, with no PHYSIS dependency on the renderer. Tests are
authored but not yet confirmed by native CTest/CI.

## Indexed reads for large diagnostic or editor sampling sessions

The default `windAt`, `gravityAt`, `stormsAt`, `sampleNatureField`,
and `sampleDiagnosticRaster` remain available and do not require an index.
For repeated queries against **one committed ECS snapshot**, use the opt-in
spatial accelerator:

```cpp
#include "engine/physis/PhysisSpatialIndex.h"
#include "engine/physis/PhysisDiagnostics.h"
aiverse::physis::FieldSpatialIndex index(sim.world(),
    {.tileSizeMeters = 256, .maxTilesPerVolume = 1024});

// Stable values pinned to the index's original COW world snapshot:
aiverse::physis::NatureFieldSample nature;
if (index.sample(queryPosition, nature)) {
    const auto wind = nature.windVelocity;
    const auto gravity = nature.gravityAcceleration;
}

// Optional accelerated debug raster for vector field layers:
aiverse::physis::DiagnosticRequest request;
request.layer = aiverse::physis::DiagnosticLayer::WindSpeedMetersPerSecond;
aiverse::physis::DiagnosticRaster image;
aiverse::physis::sampleDiagnosticRasterIndexed(index, request, image);
```

The transient index groups bounded wind zones, shelters, gravity fields and
storm volumes into **X/Z world-metre tiles**; final 3D range/timing checks
remain in the canonical PHYSIS policy. Global gravity and very large volumes
use a bounded fallback list; candidate traversal preserves ECS insertion/pool
order for Fixed-point accumulation and gravity-priority tie-breaks.

Build once after an authoritative committed tick, use for many queries and
**rebuild after an ECS change, rollback or climate/storm movement**. It is
not persistent state, not a spatial index for climate WeatherFront/QuakeEvent
simulation, and not a world streaming/job scheduler. The index holds a
copy-on-write World snapshot; keeping many historical indexes resident may
retain COW pages and increase memory. No native benchmark or compiler success
has been observed yet; tests are source-only until CTest runs.

## Stage 17 — PHYSIS editor debug window

The editor now discovers `apps/editor/windows/PhysisWindow.cpp` automatically
through its existing `AIVERSE_EDITOR_WINDOW` registry and CMake glob.
Open **Мир → PHYSIS · Природа**. The panel is **closed by default**.

- When the active `Editor::sim()` has PHYSIS initialized, it displays a
  read-only raster of live PHYSIS ECS climate, surface, wind, gravity, fire,
  earthquake or standalone local flood state. The panel **does not call
  `Simulation::step` on the game's World**, does not edit game.json, map
  documents or Box3D, and does not inject genre-specific scene entities.
- When the current game does not initialize PHYSIS, the user can explicitly
  create a separate **PHYSIS demo World** in the panel, with wind, a tornado,
  local gravity, a climate gradient and a fire. Demo step controls advance
  only that independent World; they do not mutate the current project.
- Layers, palette, sample stride, window size (8..64 PHYSIS cells) and X/Z
  centre can be adjusted. Clicking a cell displays the fixed-point value,
  climate cell coordinates, 3D wind and gravity. The panel outlines
  active wind zones (W), storm radii (T/H) and gravity volumes (G).
- Scalar weather/hazard layers read ECS directly; wind/gravity layers use
  the transient `FieldSpatialIndex`. Refresh occurs on source change,
  new tick or explicit **Обновить данные**. Edits/rollback that retain the
  exact tick require manual refresh until the editor gains revision tokens.

`PhysisDebugPalette.h` exposes `DebugColor`, `DebugPalette`,
`debugGradient` and `diagnosticColor` for native CPU tools. This is
presentation-only; colour values are never fed back into simulation.

**Not yet implemented:** world-space overlay projected into the Scene camera,
a live PHYSIS agent Inspector for editing component data, general project
`game.json` PHYSIS auto-install, GPU textures and particle/VFX integration.
The window is an actual editor GUI implementation, but **native editor
compilation, UI screenshot and manual clicking have NOT been verified**.
Use `./run-tests.sh --filter '^physis_'` and build/test the editor on macOS
before claiming it works end-to-end.


## Stage 18 — real Scene viewport overlay and Inspector map authoring

After the main merge `810feba70f01cdc35003ac80ea8486f39dc16049`,
the editor Scene adds an opt-in transparent heatmap drawn **in the actual 3D
view**, projected through the existing `Camera::worldToScreen`, plus
world-space rings for WindZone, StormEvent and GravityField. The toolbar
controls live under **Мир → PHYSIS · Природа**; toggle **Показать 3D-оверлей
в «Сцене»**. This is an ImGui presentation overlay (not shaded 3D geometry or
Box3D collision) and uses the same read-only `DiagnosticRaster` and bounded
`FieldSpatialIndex`. It only reads a live simulation that already has PHYSIS;
it does not install or tick the active world.

The shared Inspector under **Свойства карты** now includes a PHYSIS section:
**Создать PHYSIS**, **Настроить PHYSIS**, **Удалить PHYSIS**. The edit dialog
has structured controls for grid origin/dimensions, seed, temperature,
humidity, wind/gravity/storm/disaster/force opt-in and NEREID external-water
authority, plus an extended JSON tab. **Применить** validates the complete
draft and calls `MapDoc::beginEdit` only once; existing Ctrl+Z/Ctrl+Shift+Z
Undo/Redo is shared with other map edits. Invalid JSON/settings are rejected
without changing the map. Both regular and FPS map formats are supported via
`apps/editor/PhysisAuthoring.h`; no genre-specific ECS logic is added.

Authored data is stored under the map-level `"physis"` JSON key.
It is a **declaration, not automatic live ECS writeback**. Any native project
can opt in from its generic `GameModule::setup`:

```cpp
#include "engine/physis/PhysisProjectConfig.h"

void MyGame::setup(aiverse::Simulation& sim,
                   const aiverse::Json& rules,
                   const aiverse::Json& map) {
    aiverse::physis::installFromMap(sim, map);
}
```

`installFromMap` returns false for absent/disabled configuration; otherwise
it strictly reads grid/climate parameters and flags, registers persistence and
one fixed-tick PHYSIS system, and returns true. It does not install itself
into RTS, FPS or other genre modules automatically. As of **Stage 19**, validated individual `windZones`, `storms` and
`gravityFields` arrays **are** spawned deterministically when explicitly
installed via `installFromMap`. Each asset requires a stable unique `id`;
the native importer sorts IDs before creating ECS entities, including across
field types. Unknown extra keys are preserved by the editor but are not
interpreted by the importer. Runtime IDs are authoritative ECS entities;
the authored JSON ID is not itself a persisted component.

**Validation:** stage 18 source tests `physis_scene_geometry`,
`physis_project_config`, and `physis_authoring` were written and registered
in CTest. The last one needs `aiverse_editor_core` and runs in the editor /
unified build; the PHYSIS-focused headless CI compiles the other two.
Studio validation was updated to build/run all three and compile the editor.
As of this commit, **C++ build, CTest, CI PASS, editor interaction and
screenshot are NOT RUN / UNVERIFIED**. See the separate Stage 18 handoff.

## Stage 19 — individually authored WindZone, StormEvent and GravityField

Open **Мир → PHYSIS · Природа**. The section **PHYSIS — объекты и Gizmo**
can create, list, select and remove wind zones, tornadoes/hurricanes and
local gravitational fields even while the game is not running. Existing
MapProps Inspector shows the selected PHYSIS field's position, radius and
type-dependent properties. The advanced field JSON editor preserves unknown
extra properties but rejects invalid typed values.

Turn on **Редактировать маркеры мышью** and click a W/S/G marker in Scene:
drag in the X/Z plane; hold **Shift** to change Y. The first effective
movement calls `MapDoc::beginEdit` and subsequent movements call
`MapDoc::touched`; **one whole drag equals one Undo/Redo operation**.
When the marker editor is off, normal genre-independent Scene mouse selection
works as before. A PHYSIS marker is a map document asset, **not** a fake live
ECS selection or a Box3D body. An active game `World` is not mutated.

Map example:

```json
{
  "physis": {
    "enabled": true,
    "grid": {"originX": -4, "originZ": -4, "width": 8,
             "height": 8, "cellSizeMeters": 100, "seed": 8},
    "climate": {"baseTemperature": 15, "baseHumidity": 0.5},
    "enableStorms": true,
    "enableWindZones": true,
    "enableGravityFields": true,
    "floodMode": "ExternalAuthority",
    "windZones": [
      {"id": "wind-1", "center": [0,0,0],
       "radiusMeters": 120, "velocity": [15,0,0], "gustAmplitude": 3}
    ],
    "storms": [
      {"id": "storm-1", "kind": "Tornado", "center": [50,0,25],
       "eyeRadiusMeters": 20, "outerRadiusMeters": 150,
       "heightMeters": 300, "tangentialWind": 45, "updraft": 25}
    ],
    "gravityFields": [
      {"id": "gravity-1", "mode": "Uniform", "center": [0,0,0],
       "radiusMeters": 200, "acceleration": [0,-2,0],
       "overrideBaseline": true}
    ]
  }
}
```

The generic native `installFromMap(sim, map)` validates all assets **before**
changing PHYSIS World state and spawns components in lexicographic stable-ID
order; corresponding `enableWindZones`, `enableStorms`,
`enableGravityFields` flags are required for nonempty arrays so persistence
schemas match. Stable IDs are authoring keys and **are not added to the
persistent ECS component schema**; deterministic spawn order and unchanged
map/save manifest are required for consistent replay. Edits to authored
map JSON do not hot-reload live PHYSIS or rebind existing ECS entities.

The core library contains no editor/genre/Box3D/NEREID/TerraWeave dependency.
Water remains NEREID-owned; setting `ExternalAuthority` disables PHYSIS's
separate shallow flood. Box3D dynamics are still an optional independent
consumer of PHYSIS forces.

**Source tests added:** `physis_field_assets` and (editor_core only)
`physis_field_authoring`, plus expanded `physis_project_config` persistence
fixture. Registered in focused Linux/macOS PHYSIS and Studio CI.
**Native compilation, CTest and graphical drag QA NOT RUN / NOT VERIFIED.**

## Stage 20 — Scene field radius gizmos and metre-grid snapping

The shared PHYSIS field panel now offers **Move** and **Radius** tools. When the
radius tool is active, select a W/S/G map field and drag its world-projected
radius handle (on the +X perimeter). The Scene draws the selected field's
outer X/Z perimeter. Storm radius editing changes `outerRadiusMeters`, never
the eye radius; the canonical native validator rejects invalid eye/outer pairs.
The gizmo uses the editor camera's ground-plane ray at the field's current Y;
it does not modify 3D height or perform full 3D volumetric rotation.

**Snap to grid** is opt-in and has a configurable step of 0.25..1000 metres.
Move snaps X/Y/Z to the world origin grid; radius snapping is applied before
validated bounds. Negative coordinates use symmetric rounding. Height dragging
keeps an unsnapped accumulator to avoid losing sub-grid mouse motion. The snap
settings are editor-session preferences, not additional map/ECS schema fields.
Each successful move/resize drag remains **one MapDoc Undo/Redo transaction**.
No live PHYSIS World, Box3D body, NEREID water, or game genre state is edited.

The existing `tests/physis_field_authoring_test.cpp` has new source fixtures
for radius Undo/Redo, invalid radii, no-op edits and negative-coordinate snap.
**Native build, CTest and graphical editor validation remain NOT RUN.** See
`docs/agent-handoffs/physis-editor-gizmo-snapping-resize-2026-11-18.md`.


## Stage 21 — multi-select, group transforms, duplication and deletion

The shared PHYSIS field panel and Scene marker overlay support multiple
selected authored fields at once:
- Ctrl/Shift-click W/S/G markers or rows toggles selection membership.
  A plain row click switches to one field. Select all / Clear selection are
  also available.
- In Move mode, drag an already selected marker to translate the entire set
  by the SAME X/Z or Shift+Y delta. A clicked member becomes the active pivot
  without dropping other members. Relative offsets are preserved across
  WindZone, StormEvent and GravityField. Optional snap affects the pivot only,
  not every field independently. Completed axes remain when Shift mode changes.
- Duplicate selected copies complete JSON field objects, including custom
  properties, and allocates deterministic collision-free ASCII IDs
  (for example wind-1-copy, wind-1-copy-2). Default copy offset is +5 m
  in both X and Z, or the selected snap step when enabled. The new copies
  become selected. Delete selected is atomic across all field types.
- Group duplicate, group deletion and entire group drag each contribute ONE
  MapDoc Undo action. Invalid group selections or out-of-range positions are
  rejected without partial updates. The Radius gizmo edits only the currently
  active field; individual property editing needs a one-field selection.

Authoring-only C++ functions in apps/editor/PhysisAuthoring.h:
  moveFields(MapDoc&, vector<FieldPosition>, bool firstEdit, string* error)
  duplicateFields(MapDoc&, vector<string> ids, dx, dy, dz, vector<string>& newIds, string* error)
  removeFields(MapDoc&, vector<string> ids, string* error)

Drag updates derive absolute targets from initial positions, and the entire
authoring config is validated before committing through the same native
parseFieldAssets parser used by installFromMap. There is no gameplay World,
Box3D, NEREID or TerraWeave mutation and no ECS save-schema change.

Source fixtures for group Undo/Redo, atomic rejection, new ID generation,
custom-key preservation and regular/FPS map roundtrips are in
tests/physis_field_authoring_test.cpp. Native build, CTest and GUI QA
NOT RUN. See docs/agent-handoffs/physis-editor-group-authoring-2026-11-18.md.


### Group rotation and uniform geometric scale (Stage 21 extension)

With any PHYSIS fields selected, choose **Поворот вокруг Y, град** and
**Масштаб геометрии**, then **Применить к выбранным**. The active member's
center is the pivot. Both operations are performed in one atomic MapDoc
transaction by physis_authoring::transformFields. Rotation changes the
world-space X/Z arrangement around the common Y axis; uniform scale
changes the offsets in XYZ and the sources' spatial extents (wind/gravity
radius; storm eye radius, outer radius and height). Wind velocity/direction,
storm forces, gravity acceleration, enabled flags and IDs are NOT changed.
This is intentional: field values are independent from geometry.

Whole-config native validation rejects an invalid transformed position,
radius, height or source combination, leaving the entire original
selection unchanged. Orientation/heading rotation, per-axis nonuniform
scaling, volumetric 3D rotation gizmos and live ECS writeback are not
implemented. Rotation uses editor double math and writes 3-decimal metre
coordinates; authoritative runtime still reads quantized Fixed map inputs.
New rotation/scale/Undo fixtures are authored but native CTest is NOT RUN.


## Stage 22 — Scene marquee, pivot and 3D manipulation

In Мир → PHYSIS · Природа opt into mouse editing and, separately, rectangle
selection on empty Scene space. The rectangle selects projected W/S/G
centres (including reverse drags), not whole source volumes. Ctrl/Shift
adds to selection. Other Scene picking remains active when the tools are
off.

Pivot modes: Active, Average, Bounds Center, World Origin, Custom XYZ.
These choices are session-only; no authored ECS/map schema changes.
Move mode projects X/Y/Z axes and uses Camera::screenRay plus the
ray/axis closest point (not a screen delta) to move a group in world metres.
Parallel/behind-eye solutions are rejected. Pivot-only snap preserves
the internal arrangement of selected sources.

Rotate Y provides a horizontal gold ring for turning selected source
POSITIONS around the pivot. Uniform Scale provides a cyan ring and scales
group spacing plus field radii/storm heights, not wind velocity, storm
strength, gravity acceleration or orientation. The source components
do not carry full 3D orientation; X/Z quaternion rotation is not implied.
Radius still edits only the active field.

Interactive yaw/scale uses transformFields with optional original baseline
JSON and firstEdit. Every mouse update derives from the same initial map,
and first effective change begins a single MapDoc Undo; subsequent updates
use touched. A foreign MapDoc revision cancels such a drag. All candidates
are checked by the canonical native parser before writing; no live World,
NEREID, TerraWeave, Box3D, FPS or RTS mutation occurs.

Source fixtures cover pivot rules, reverse rectangle, ray/axis degeneracy
and frozen-baseline Undo. Native CTest/editor QA NOT RUN; GitHub Actions
jobs observed queued only. See the Stage 22 handoff under
docs/agent-handoffs/physis-editor-marquee-3d-gizmos-2026-11-18.md.


## Stage 23 — opt-in authoritative field reconciliation and command replay

The new engine/physis/PhysisFieldRuntime.h connects the existing pure
PhysisFieldChangePlan with an explicit, rollback-owned ECS field identity
registry. Unlike static startup-only installFromMap, this optional path
supports replacing a set of authored WindZone, StormEvent and GravityField
sources at the Simulation command boundary. It is intentionally generic and
has NO dependency on FPS, RTS, Box3D, NEREID, TerraWeave or the editor.

Host integration example (choose a unique command ID in your own module):

    constexpr uint16_t kPhysisEdit = 53123;

    void MyGame::setup(Simulation& sim, const Json& rules, const Json& map) {
        if (!physis::installEditableFromMap(sim, map))
            return; // PHYSIS absent or disabled
    }

    void MyGame::applyCommand(Simulation& sim, const Command& cmd) {
        if (cmd.type == kPhysisEdit) {
            // The authoritative server MUST authorize system player=0.
            std::string error;
            (void)physis::applyFieldEditCommand(sim, cmd, kPhysisEdit, &error);
            return;
        }
        // Other genre-independent gameplay commands ...
    }

    // Trusted editor/server-side action; NEVER apply World edits in Scene.
    uint32_t scheduledTick = 0;
    std::string error;
    const bool queued = physis::submitEditableFieldsFromMap(
        sim, editedMap, kPhysisEdit, scheduledTick, &error);

This queued result only means the command was accepted for submission;
the authorizing host can reject it later on expected-revision mismatch.
Simulation::submit and CommandSink provide the existing command transport.
The host must reserve a command ID, reject spoofed system commands at the
network ingress, and implement the applyCommand dispatch above. A client
cannot gain authority simply by using Command.player=0.

The runtime registry is a new opt-in persistent World resource named
physis.field_runtime (version 1). It contains monotonic edit revision,
stable authored ID -> (Entity, kind, canonical serialized fingerprint).
A snapshot and native file save therefore retain the same identity and
revision; rollback restores both. Unchanged fields retain dynamic runtime
state (including a drifting storm); edited fields of the same kind keep
the same Entity handle; removed sources are destroyed; type changes cause
Remove+Add with a fresh Entity generation. Changes are staged on a copy
of World and committed together, so validation/plan failures leave it
untouched. A no-op command still consumes one edit revision.

The new optional install enables persistent component schemas for ALL three
field kinds so they can be added/retyped while a session is running.
This deliberately changes the strict save manifest; it must be enabled
on both save and load. Existing installFromMap and saves remain unchanged.
No implicit save migration has been implemented.

The edit wire payload is a field-wise binary snapshot with a protocol ID,
expected revision, sorted unique sources and a size bound. It packs into
the standard Simulation Command.args, so the payload must fit within
4096 total int64 words (up to 32760 bytes, one word reserved for length).
Larger replacement sets are rejected; atomic multi-command chunking is
NOT implemented. The actual maximum number of fields depends on encoded
source properties, not the standalone 2048-field parser limit. Because
the full source set is transmitted, edits with obsolete revisions reject
rather than overwriting concurrent changes.

Only authored field collections are editable at this stage. Changes to
climate grid settings are rejected by submitEditableFieldsFromMap, and
changes to the overall flood/disaster policy are NOT hot-reloaded.
There is no automatic map Inspector -> active GameModule command registration,
network authorization layer or GUI acknowledgement yet; a native host
must opt in and use its existing command system explicitly.

CTest target physis_field_runtime has source fixtures for encoding and
network Command wire roundtrip, identity-preserving updates, add/remove,
type replacement, replay, SimSnapshot rollback, persistent save/load,
revision conflict, invalid network payloads, oversized command rejection
and Simulation::submit tick scheduling. These tests were authored but have
NOT been compiled or run. See the separate Stage 23 handoff under
docs/agent-handoffs/physis-field-runtime-reconciliation-2026-11-18.md.


## Stage 24 — editor live requests, verification and authenticated server admission

The PHYSIS window now contains a Live Editing panel. Its Apply-to-running-
simulation button is opt-in and works only for a LOCAL Simulation with a
FieldRuntimeState resource, no CommandSink, and a GameModule that explicitly
implements aiverse::editor::physis_live::FieldEditHost. Neither opening a
PHYSIS map nor the independent debug World grants authority. The ordinary
RTS/FPS hosts are NOT automatically opted in.

The project's host exposes a unique nonzero command type, explicitly allows
local editor edits, calls installEditableFromMap from setup and dispatches
applyFieldEditCommand from GameModule::applyCommand. The editor prepares
the complete typed source set from MapDoc::toJson and submits it through
Simulation::submit. It NEVER directly changes live World.

The panel reports: Unavailable, Idle, Queued, Applied, Conflict or Rejected.
Queued is NOT Applied. Applied requires the expected ECS registry revision
and matching stable authored fingerprints; the status is invalidated when
another edit supersedes the values or a rollback restores earlier state.
A dropped handler or conflicting revision is reported, not hidden.

For REMOTE servers, physis::admitAuthorizedFieldEdit is a generic server-
only admission hook. A server transport supplies a principal derived from
its authenticated connection and an ACL callback, validates the untrusted
request and re-encodes a new player-0 system Command through Simulation::
submit. A client-supplied player field or role is never authentication.
The existing AIVERSE battle server relay enforces its own input slot
whitelist. It is NOT automatically connected to PHYSIS admin commands.

A real project must add its authenticated editor/admin transport, proper
server dispatch and independent remote ACK/NACK protocol. Networked
Simulation with CommandSink is deliberately disabled in the local panel
instead of injecting player-0 commands through ordinary client input.
No cross-machine editor-server service has been shipped by this stage.

Files: apps/editor/PhysisLiveEdit.h (no ImGui; capability + status),
apps/editor/windows/PhysisWindow.cpp (panel),
engine/physis/PhysisFieldRuntime.h (trusted admission policy), and
engine/sim/Simulation.h (const module accessor).
No new default PHYSIS schemas or water/terrain/Box3D/genre dependencies.
CTest physis_live_edit was authored and registered in CMake/PHYSIS/Studio
workflows; native compilation and tests have NOT RUN. See
docs/agent-handoffs/physis-editor-live-edit-2026-11-18.md.


## Stage 25 — reliable remote PHYSIS editing

PHYSIS has a separate, versioned admin protocol over existing net::Conn
(reliable UDP, WebSocket or loopback). It is NOT the ordinary player
InputFrame or a shortcut around AIVERSE networking authorization.

Runtime protocol files: engine/physis/PhysisRemoteProtocol.h,
PhysisRemoteServer.h and PhysisRemoteClient.h. The client uses unique
instance/request IDs, bounded retries, Query-before-resend on reconnect,
and explicit Unknown/Queued/Applied/Rejected/Conflict/Denied receipts.
The server stores a bounded idempotent receipt cache by authenticated
principal+client instance+request ID and rejects ID reuse with changed
payload. Only one new PHYSIS edit can be queued at once. Applied is
reported ONLY after scheduled Simulation tick and matching ECS
FieldRuntimeState revision plus source fingerprints. Queued != Applied.

The server host must run ServerGateway::poll on a separately
AUTHENTICATED admin/editor net::Conn, supply a trusted connection
principal and ACL callbacks, then call ServerGateway::observe after
authoritative Simulation ticks. It must reserve a PHYSIS command type
and dispatch Stage 23 applyFieldEditCommand in GameModule::applyCommand.
No client-controlled player=0 command is trusted.

apps/editor/PhysisRemoteEdit.h::EditorLink accepts only a host-attached
already authenticated connection. The PHYSIS window shows remote
status, confirmed revision, request ID, and refresh/send/retry controls.
A host must provide actual transport authentication/credentials;
the editor does not open an anonymous privileged socket.
The Stage 24 local play-test route remains separate.

Wire bounds: 40 KiB protocol frame and Stage 23 32760-byte typed
Command.args payload. Large multi-part atomic uploads are NOT yet
implemented. The server cache is IN-MEMORY, so after server restart
an old receipt can become Unknown and client may see Conflict.
Revision checks prevent duplicate effects but a crash-durable
exactly-once ACK requires a persistent journal. Outstanding client
requests can be exported/restored, including original IDs, but the
host must persist request counters or rotate the client instance.

The separate opt-in PhysisRuntimeFields.h/TrackedFromMap registry
from another agent does not replace this Stage 23 FieldRuntimeState
runtime. Do not install both. No change to legacy installFromMap,
FPS/RTS authority, NEREID Water, TerraWeave or Box3D.
CTest physis_remote_edit added to PHYSIS and Studio CI, including
actual net::Conn loopback, reconnect, ACL, duplicate, jitter and
rollback source tests. NATIVE BUILD AND TESTS NOT RUN. Handoff:
docs/agent-handoffs/physis-remote-edit-reliable-2026-11-19.md.


## Stage 26 — durable admin journal and authenticated loopback endpoint

PHYSIS remote editing now includes a file-backed write-ahead receipt journal
and an opt-in, native loopback-only admin WebSocket listener. The existing
FieldRuntimeState / installEditableFromMap / GameModule::applyCommand /
applyFieldEditCommand remains the canonical editor authority. The separate
RuntimeFieldRegistry / FieldCommandHost path introduced concurrently by
other agents is NOT connected to this editor and must not be enabled at the
same time.

New generic engine files:
- engine/physis/PhysisRemoteJournal.h: FileJournal stores a bounded
  checksummed snapshot of the server's request/receipt cache in an
  operator-owned private directory. Entries include trusted principal,
  client instance and request ID, exact original edit bytes, typed authored
  fingerprints, expected revision, scheduled tick and status.
  It uses unpredictable exclusive temporary file creation, write flush/
  fsync, atomic rename/replacement and POSIX directory sync. A bad
  checksum or missing permission rejects startup rather than silently
  dropping history. Only ONE server process may own a journal path.
- engine/physis/PhysisAdminAuth.h: versioned admin challenge/proof handshake
  using a new random nonce and keyed BLAKE2b. The shared secret is not
  sent. Principal string is tied to proof, checked against a host-supplied
  key lookup, followed by server ACL and reserved PHYSIS command type.
  Client-supplied player=0 / instance tokens never grant rights.
- engine/physis/PhysisAdminEndpoint.h: LoopbackAdminEndpoint owns a
  real net::WsListener listening at 127.0.0.1 ONLY, and admits
  authenticated connections to the Stage 25 ServerGateway. It requires
  a nonzero host-reserved PHYSIS command ID, ACL callbacks and a private
  journal file. LoopbackAdminConnector can authenticate a native editor
  connecting over local ws://127.0.0.1. The PHYSIS window uses its
  EditorLink bridge to poll this connection and show pending/status.
- apps/editor/PhysisRemoteEdit.h: EditorLink::connectLocalAdmin accepts
  host-supplied principal, secret from secure storage, nonzero
  instance ID, assigned slot and command type. It does not auto-generate
  secrets, open anonymous network connections or grant admin rights.

Example native setup:
    aiverse::physis::remote::admin::LoopbackAdminEndpoint endpoint(
        privateJournalFile);
    endpoint.open(localPort, reservedFieldCommandType,
                  serverKeyLookup, canRead, canWrite, &error);
    // Restore matching Simulation checkpoint BEFORE opening endpoint.
    // In host event loop:
    endpoint.poll(sim, monotonicMs);
    sim.step();
    endpoint.afterTick(sim);

Credential lookup, key rotation and ACL are host-owned. The admin port is
never allowed to bind publicly from this API. IMPORTANT: the current native
AIVERSE WebSocket client supports ws:// but NOT wss://; this handshake
alone is not transport encryption. For off-machine admin access, use an
already encrypted authenticated tunnel to local loopback or implement
real native TLS/WSS. Never expose plain remote ws:// and call it secure.

Crash recovery:
1. Persist the original edit *intent* BEFORE scheduling the Simulation
   command. A failed journal write prevents any new admission.
2. Persist Queued and later Applied/Rejected/Conflict receipts.
   Restored Unknown intents may be reconciled against saved ECS
   revision+fingerprints; same original packet can be re-admitted only
   after ACL and revision checks. Duplicated packet IDs cannot apply twice.
3. On restart, restore the matching Simulation World/checkpoint and
   pending command state first, then attach the file journal, and only
   then accept authenticated admin connections.
4. The journal and the Simulation checkpoint are two separate stores,
   not a single atomic DB transaction. A power failure between writing
   them can leave an ambiguous outcome. Receipt recovery must report
   Unknown/Conflict rather than inventing Applied. This is more robust
   than Stage 25's volatile-only receipt cache, but it is not a blanket
   guarantee of crash-atomic exactly-once acknowledgment.

No changes to default PHYSIS save manifest or NEREID Water,
TerraWeave terrain/foliage, Box3D rigid-body dynamics and FPS/RTS input
authorities. This is opt-in infrastructure for authenticated native hosts.

A new physis_admin_journal CTest target and PHYSIS/Studio CI registration
contain source fixtures for journal corruption and restart recovery,
write failure, challenge/response rejection and real local WebSocket
handshake plus ECS update. **No native compilation, CTest PASS, TLS
integration or macOS GUI test has been performed.** Handoff:
docs/agent-handoffs/physis-admin-durable-journal-2026-11-19.md.


### Stage 26 crash-recovery correction — persisted Queued != pending command

During Stage 26 hardening, ServerGateway::restoreJournal was changed to
restore every persisted Queued receipt as Unknown and clear the old
scheduledTick. The previous server's in-memory Simulation command
queue may have vanished on restart, even when the sidecar journal
still says Queued. A loaded Queued receipt is not proof of execution.

After the host restores its matching ECS World/checkpoint, observe()
reconciles Unknown against FieldRuntimeState and authored fingerprints.
If already committed, the request is Applied. If revision advanced
incompatibly, it is Conflict. If unchanged, keep Unknown; the EXACT
original authenticated Submit can be re-admitted with the same ID
after ACL and revision checks. No new PHYSIS permission or public
listener is created by this fix.

New source fixtures in physis_admin_journal test both crash before
the queued command executes and crash after its ECS effect but before
the Applied sidecar flush. These tests are AUTHORED, NOT RUN.
The journal and Simulation save remain separate durability domains:
this is safe resubmission/reconciliation, NOT full cross-file
crash-atomic exactly-once commit.

Stage-specific handoff:
docs/agent-handoffs/physis-stage26-queued-recovery-2026-11-19.md.


### Stage 26 crash-recovery fix for durable field receipts
An authenticated PHYSIS editor request may be persisted in the file journal as Queued before the authoritative Simulation step. If the process crashes, its pending in-memory command queue may be gone even though the journal still says Queued. Recovery now converts Queued to Unknown, then compares the restored ECS state: matching revision + fingerprints proves Applied; a still-current revision permits the original authenticated request to be resubmitted once. This prevents premature Rejected or incorrect Applied reports after restart. Test fixtures in `tests/physis_admin_journal_test.cpp` cover both crash-before-commit and crash-after-commit-before-receipt; native CTest has NOT RUN. See `docs/agent-handoffs/physis-stage26-queued-recovery-2026-11-19.md`.

### Stage 64 — ignore stale remote ACK after Apply

`RemoteClient` enforces monotonic receipt handling on an active
connection: an accepted terminal receipt cannot be overwritten by
late Queued/Unknown/etc. When an Applied operation is checked after
reconnect, a Query for the original ID must actually be sent before
accepting its reply; a pre-query response and stale Queued are
ignored. An Unknown during Applied verification is **Uncertain**:
never automatically replay a formerly acknowledged Submit.
Manual `retry()` after a verification timeout is a Query-only
operation, and `begin()` rejects uncertain/in-progress verification.

No wire format, ECS schema, NEREID Water, TerraWeave or Box3D changes.
V1 has no per-Query correlation nonce, so arrival after a Query is
not cryptographic proof that a delayed Applied packet was generated
for that Query. A future protocol version should add correlation and
server generation. Source fixtures are authored, but native engine
CTest has NOT RUN. See
`docs/agent-handoffs/physis-stage64-monotonic-ack-2026-11-19.md`.

### Stage 65 — macOS remote edit and admin journal integration GREEN

Native user-reported PHYSIS `--integration --no-physics` CTest
previously failed `physis_admin_journal` and
`physis_remote_edit` (4/6 PASS). Commit
`e628ba359a31be4e8fa2d16d8fab156e1e212e4d`
preserves the last authoritative Applied status while sending
a reconnect verification Query, and keeps the native WebSocket
admin test's accelerated timestamps monotonic across handshake
and edit, avoiding unsigned elapsed-time underflow.

**macOS native rerun after fix: BUILD PASS, all six CTests PASS,
100%, 0 failed, 2.99 seconds.** Tests:
`physis_journal_posix_io`, `physis_journal_locked_io`,
`physis_journal_crash_recovery`, `physis_admin_journal`,
`physis_remote_edit`, `physis_field_runtime`.
Report:
`/Users/aleksandrfeer/Projects/AIVERSE/build/physis-validation-integration/physis-reports/20261119-102507-474`.
No changes to wire format, saved ECS data, runtime Box3D or other
agents' NEREID/TerraWeave source. Complete PHYSIS/physics/editor
CI remains unverified. See stage handoff:
`docs/agent-handoffs/physis-stage65-macos-integration-fixes-2026-11-19.md`.

### Stage 66 — forces fixed-point rounding test correction

Full PHYSIS runner on macOS AppleClang 17 at commit `3a1c1536`
configured and built all selected targets (including Box3D) and
passed **23 of 24** native CTests. Only `physis_forces` failed
an over-strict expectation that multiplying quantized -9.81 m/s²
by 10 kg must equal a separately quantized -98.1 N. The deterministic
Q47.16 paths differ by **2 raw units** (~0.0000305 N).

Commit `2dda8ccfa242b14b327a01a0cd7ea7ca16ea4d90`
updates only the test to assert exact PHYSIS policy
`gravityForce.y == gravity.baseline.y * body.massKg`
and a bounded two-raw-unit error against ideal -98.1 N.
No production changes. **The corrected test has not been rerun yet.**
Handoff:
`docs/agent-handoffs/physis-stage66-fixed-gravity-rounding-2026-11-19.md`.

### Stage 67 — complete headless PHYSIS suite verified (macOS)

User-provided native `--full` output **after** the Stage66
forces Q47.16 test correction confirms **24/24 tests PASS,
0 failed, 10.51s**, PHYSIS validation PASS. Report:
`/Users/aleksandrfeer/Projects/AIVERSE/build/physis-validation-full/physis-reports/20261119-105206-10283`.
This covers the headless PHYSIS suite with Box3D, nature fields,
network, security and persistence. The editor-only
`physis_authoring` and `physis_field_authoring` targets are
registered only when `aiverse_editor_core` exists; they still
require an editor-core enabled CMake configuration and CTest.
Do not claim those editor tests or GUI QA passed. See handoff:
`docs/agent-handoffs/physis-stage67-headless-green-2026-11-19.md`.

### Stage 68 — native PHYSIS editor authoring tests PASS

With `aiverse_editor_core` present, user-reported native macOS
`physis_authoring` PASS (3.28s) and `physis_field_authoring`
PASS (2.70s), **2/2, zero failures**, with EDITOR/PHYSIS label
5.99 sec*proc. Together with prior headless/Box3D 24/24, this
confirms 26 PHYSIS test executions over two distinct builds.
This does not verify interactive GUI usage, full-engine CTest
or completed cross-platform CI. Stage-specific handoff:
`docs/agent-handoffs/physis-stage68-editor-authoring-green-2026-11-19.md`.

### Stage 69 — actual editor build namespace correction

In `aiverse::editor`, `net::` may mean `aiverse::editor::net`,
not the engine's network `aiverse::net`. The first GUI-editor macOS
build revealed errors for `Key`, `Conn`, `Bytes`, `ConnState`,
`wipe`, and `nowMs` in the PHYSIS remote bridge/window. This stage
fully qualifies the actual transport references with
`::aiverse::net::` and adds a compile regression in the existing
`physis_remote_edit` native test source. There is no networking,
permission, gameplay, save schema, Box3D, NEREID, or TerraWeave change.
The full GUI rebuild and new source test are NOT RUN yet. Handoff:
`docs/agent-handoffs/physis-stage69-editor-net-shadow-2026-11-19.md`.

### Stage 70 — corrected remote bridge ternary compilation

macOS compilation found `expected ':'` and `expected unqualified-id`
at `apps/editor/PhysisRemoteEdit.h:169`, because explicit
`::aiverse::net::Bytes{}` was concatenated with the preceding
ternary `:` into three colons. Commit
`4817aaadebe20dd9f77e2d057d3ff275848e1416` separates
the operator with spaces. The qualified engine transport namespace
remains unchanged. Post-fix native CTest and GUI build are NOT RUN;
the previous headless 24/24 and editor authoring 2/2 remain
historical prior-test evidence, not validation of new source.
Handoff: `docs/agent-handoffs/physis-stage70-ternary-net-qualification-2026-11-19.md`.

### Stage 71 — macOS GUI editor native build now passes

After Stage69 namespace qualification and Stage70 ternary fix,
the user rebuilt the actual macOS editor. The PHYSIS
`physis_remote_edit` CTest **PASSED 1/1**, 0.08s, followed by a
complete `aiverse_editor` build to **100%** with editor core,
network, game view and runtime. This is actual compilation evidence,
not a GUI runtime test. PHYSIS headless 24/24 and editor authoring
2/2 were also user-reported passing in earlier separate builds.
No runtime C++ change this stage. Interactive Scene gizmos,
Inspector Undo/Redo, visual overlays, admin connection, broad
regressions and completed CI remain unverified. See
`docs/agent-handoffs/physis-stage71-macos-gui-build-green-2026-11-19.md`.

### Stage 72 — NEREID canonical water imported from main

Current PHYSIS source branch includes NEREID water from main commit
`d71c6d47e532b25fb816a8bb1f96c41e5275a165` via true merge `7c617d7fbc1a7ff7529e84a8e7e281f5f3298f0b`. This imports
water ECS bodies, sampling/current/buoyancy, confluence and cascade
systems, water renderer/scene bridges, NEREID tests and release-mode
test safeguards while retaining PHYSIS climate/wind/storm/forces and
editor features. `engine/world/NereidPhysisWindBridge.h` forwards
read-only host-supplied PHYSIS wind into NEREID **presentation**, not
authoritative water. With NEREID authority enabled, use
`FloodMode::ExternalAuthority` so PHYSIS does not run its
experimental shallow-flood solver in the same region. The host
must explicitly install and schedule each ECS system; source
merging is not an automatic runtime integration.
GitHub static ancestry/CMake/catalog checks PASS, but no combined
native build/CTest or Metal visual tests have run. Full details:
`docs/agent-handoffs/physis-stage72-main-nereid-merge-2026-11-19.md`.

### Stage 73 — native post-merge 40/40 CTest success

The owner supplied macOS native CTest output from the combined
PHYSIS/NEREID/SpectraFX branch: **40 of 40 PASS; 0 failures;
total CTest 10.25s**. The Stage72 filtered command targeted
`^(physis_|ecs_nereid_|vfx_)`. PHYSIS label had 26 tests,
SpectraFX 5, and additional NEREID ECS tests passed.
This is the first real reported post-merge CTest PASS and
supersedes the "NOT RUN" field for **this focused suite**.
It does not verify all AIVERSE tests, two active authoritative
modules running together in the same host World, GUI interactions,
Metal rendering or remote admin transport. Next: broader
repository-wide CTest and explicit dual-module host smoke with
`FloodMode::ExternalAuthority`; preserve save/rollback parity.
Handoff:
`docs/agent-handoffs/physis-stage73-postmerge-40-green-2026-11-19.md`.

### Stage 74 — broad repository CTest: 148/150, targeted mitigations

The user-reported macOS full 150-test suite after the PHYSIS/main
merge passed 148, failed two: PHYSIS `skill_audit` (a method
name incorrectly appended to a Markdown source path) and
cross-subsystem `place` (strict server tick budget <5ms, measured
8.204ms during parallel CTest). Commit
`a61923df653994a9a94c1916f4077db7fb7e9848` fixes the
skill source path and adds CMake `RUN_SERIAL TRUE` for the
Place timing benchmark, without modifying the tick threshold or
Place/PHYSIS physics. A small external CMake fixture verified the
RUN_SERIAL property, not the actual Place speed.
**Native macOS post-fix CTest remains UNVERIFIED.**
See the dedicated Stage74 handoff at
`docs/agent-handoffs/physis-stage74-full-ctest-skill-place-2026-11-19.md`.

### Stage 75 — build/test visibility and Place performance follow-up

The general engine build monitor `tools/aiverse-progress.py`
was committed to the PHYSIS branch; it wraps CMake/CTest, shows
live active test timing and heartbeat for long silent tests,
provides TTY dashboard and CI plain fallback, and persists
logs/JSON. Tested on a standalone Linux CMake fixture (2/2
PASS; no-tests and deliberate fail return 8; 15s heartbeat).
Full macOS AIVERSE testing of the new tool is NOT RUN.
See `docs/aiverse-build-monitor.md`.

The user's Stage74 isolated macOS CTest rerun confirmed
`skill_audit` PASS but `place` still FAIL at average 8.110ms
versus its strict 5.0ms threshold. This is an unrelated Place
server performance gate, not a PHYSIS/Water simulation assertion.
No threshold/physics code changed. Next: separate Release Place
benchmark and phase profiling if it still exceeds 5ms.
Handoff: `docs/agent-handoffs/physis-stage75-progress-monitor-place-2026-11-19.md`.

### Stage 76 — live terminal warnings/errors and Release Place result

AIVERSE Build Monitor (`tools/aiverse-progress.py`) now has a
sticky warnings/errors section, priority display of errors,
colored warnings and complete `diagnostics.log` with per-stage
counts in `summary.json`. Standalone Python tests 3/3 and
CMake-registered `tool_aiverse_progress` regression 1/1 PASS;
the entire source was tested on isolated fixtures, not macOS
AIVERSE native builds. Real user Release Place benchmark through
the *previous* Build Monitor reported PASS in 1:41; the actual
tick average was not included in the short summary, and the
isolated Debug performance test still failed at 8.110ms. No
PHYSIS/NEREID gameplay, network or physics code changed.
Guide: `docs/aiverse-build-monitor.md`.
Handoff: `docs/agent-handoffs/physis-stage76-build-monitor-diagnostics-2026-11-19.md`.

## PHYSIS Stage 78 — optional per-entity Nature Contacts (2026-11-19)

New `engine/physis/PhysisNatureSampling.h` adds reusable
`NatureProbeComponent` (3D Fixed position/enabled),
`NatureContactComponent` (wind, gravity, atmospheric and surface
fields, fire/quake, validation and two tick stamps), and
`advanceNatureContacts(World&,uint32_t)`.
`InstallOptions::enableNatureContacts` defaults to **false**;
the optional components are persisted only when enabled and
`physis.nature_contacts` runs AFTER the climate system. No
historical save manifest or genre-dependent code is changed by default.

Separate `engine/world/PhysisNereidNatureBridge.h` combines
the published PHYSIS/NEREID read-only contacts on one ECS entity,
checking enabled state, matching tick stamps and probe positions
to avoid stale data. It neither advances water nor writes
NEREID bodies. Host must register both samplers; choose
`FloodMode::ExternalAuthority` if NEREID owns water.

**Code commits:** `354e0689fffcfc4ec9853307fc3a06ba32c86212`,
`a286022cb733c524f30b795501c957ee1ae3f9ec`.
**Verification:** source and test registered in CMake; combined native
`physis_nereid_contacts` test is WRITTEN, **NOT RUN** on macOS
or other full AIVERSE checkout. The earlier 40/40 focused
PHYSIS/NEREID/SpectraFX PASS predates these commits and must NOT
be cited as testing Nature Contacts. Review possible compile or
runtime errors in the new test rather than claiming readiness.

Handoff: `docs/agent-handoffs/physis-stage78-ecs-nature-contacts-2026-11-19.md`.

### Stage 79 — structural environmental impacts without genre coupling

Implemented `engine/physis/PhysisNatureStress.h` with an opt-in
ECS receiver/output pair and `advanceNatureStress(World&)`.
It derives per-entity aerodynamic load from PHYSIS's canonical
`ForceOutput`, computes horizontal bending moment
`|Fxz| × leverArm`, compares against authored force and
moment strength limits, and deterministically accumulates
damage from overload, local fire and earthquake intensity
while permitting bounded recovery in safe conditions.
Catastrophic overload or accumulated damage reaching one
latches `fractured`; `newlyFractured` marks the transition
for the host. It publishes data only — **no physical tree fall,
Box3D step, foliage mutation or water change**.

`InstallOptions::enableNatureStress=false` by default, with
explicit dependency on `enableForces` and
`enableNatureContacts`. The opt-in receiver/output types
use v1 persistence IDs `physis.nature_stress_receiver` and
`physis.nature_stress_output`; previous saves are unchanged
when disabled. Source commits:
`fa0b00d6d60a4fb6b6057610806c1d040fab6897` and
`21c016ede31107f48c50ecc5247cfc0cf98ae56a`.
The first also rejects outdated nature+water snapshots after
a newer PHYSIS climate tick.

Native test `physis_nature_stress` and prior Stage78
`physis_nereid_contacts` are registered but **NOT RUN** on
the latest branch. Do not label them passing based on Stage73's
older 40/40 result. Follow
`docs/agent-handoffs/physis-stage79-nature-stress-2026-11-19.md`.

### Stage79 compile-safety follow-up

In `tests/physis_nature_stress_test.cpp`, 12
`World::add<T>(entity, {})` calls were replaced with
explicit `T{}` values because the actual ECS method
uses variadic `Args&&...` deduction. Test-only commit
`146013f2e27cdefe24d6f224c6e30bad88c22251`.
The expected native Stage79 test outcome remains
**UNVERIFIED** until AppleClang and CTest execute.

### PHYSIS Stage 80 — native macOS recovery after const World fix (2026-11-19)

- Initial user-native AppleClang compile FAILED in `tests/physis_nereid_nature_contacts_test.cpp:103–104` with two `no matching member function for call to 'get'` errors. Test bound `const auto& world=sim->world()`, while `World::get<T>(Entity)` in `engine/core/ECS.h` is non-const.
- Implementation commit `4d9c1adb94f88c7aefef58e3320afda9297e0b48`: replace two reads with `world.tryGetConst<T>(entity)`, check both pointers, copy their component values. Existing mutable test setup continues using `get<T>`. No runtime ECS, PHYSIS, NEREID, Box3D, save schema or renderer changes.
- **Post-fix user-reported macOS native monitor: `AIVERSE: PASS`, 00:00:20, 0 warnings, 0 errors**. Report: `build/physis-nereid-validation/aiverse-reports/20261119-181448-67558`. The pasted output contains no CTest per-test lines: exact number of tests completed must be retrieved from this report's `ctest.log`/`summary.json`.
- Prior combined PHYSIS/NEREID/SpectraFX 40/40 was on older source. Full current-branch repository-wide tests, interactive editor, Box3D/TerraWeave fracture handoff and CI are not verified. PR #25 stays Draft.
- Standalone handoff: `docs/agent-handoffs/physis-stage80-const-world-native-2026-11-19.md`.

### Stage 81 — Stage78/79 confirmed native macOS CTest 2/2 PASS (2026-11-19)

The user supplied the complete AIVERSE Build Monitor JSON and native
`ctest.log` from `build/physis-nereid-validation`,
report `20261119-181448-67558`, on source commit
`4d9c1adb94f88c7aefef58e3320afda9297e0b48`:
`configure` exit=0, `build` exit=0, `ctest` exit=0,
**completed=2, failures=0**, aggregate exit=0, 0 warnings,
0 diagnostic errors, 20.71 seconds for the full command.
Native CTest:
- `physis_nereid_contacts` (#123) PASS 0.85s.
- `physis_nature_stress` (#124) PASS 1.01s.
- 100% PASS 2/2, 0 failed; CTest wall time 1.02s.
This supersedes Stage80's note that the named test results were
not yet available. **It does not establish full AIVERSE suite or
TerraWeave/Box3D physical falling integration.**
No code change; documentation checkpoint only.
Handoff: `docs/agent-handoffs/physis-stage81-nature-tests-green-2026-11-19.md`.

### PHYSIS Stage 82 — persistent fracture event handoff (2026-11-19)

Implementation commit `2b0190cb93e4335e7e458fa03d75239ef65c8424`.
Added `engine/physis/PhysisFractureOutbox.h` defining
`NatureFractureOutboxComponent` with Empty/Pending/Acknowledged
states, a generation-safe Entity/tick event key and persistent
Archive serialization. `advanceNatureFractureOutbox(World&)`
publishes a current-tick newlyFractured stress event once;
`pendingNatureFractures(const World&)` returns sorted pending
messages, retained over later ticks/save, and
`acknowledgeNatureFracture(World&,Entity,key,tick)` rejects
wrong/duplicate acknowledgments.

`InstallOptions::enableFractureOutbox=false` by default.
If enabled, it requires nature stress (already requiring forces
and nature contacts), registers `physis.fracture_outbox` v1
persistence and adds system `physis.fracture_outbox` after
`physis.nature_stress`. Default game save manifests unchanged.
`tests/physis_fracture_outbox_test.cpp` registered as CTest
`physis_fracture_outbox` (PHYSIS/ECS/PERSISTENCE labels):
tests ordering, deterministic keys, pending/ack, native
save/load, rollback and COW.

TerraWeave `feature/terraweave` already owns
`PlantFallEvent`, `AnchoredPlantId` and
`FallenPlantPhysics::spawn` for Box3D. PHYSIS does not
import or duplicate TerraWeave: a game host must map Entity
to plant ID, perform/replicate physical consequences and
then acknowledge. External Box3D float state is not atomic
with ECS ack; host must deduplicate/resynchronize on rollback.
NEREID still owns water. Grass uses per-cluster probes.

Prior Stage81 user macOS 2/2 nature tests PASS confirmed;
Stage82 native compile/CTest **NOT RUN**. PR #25 remains
Draft. Handoff:
`docs/agent-handoffs/physis-stage82-fracture-outbox-2026-11-19.md`.

## MCP map configuration

The engine skill `skills/world/aiverse-physis-authoring/SKILL.md` describes
`physis_get`, `physis_validate`, and `physis_set` in `aiversemcp`.
Read the current config/default and document revision, validate the complete
candidate, then set it with `expectedRevision`. Stale edits fail without
mutation. Explicit `config:null` removes the block; missing config fails.
Edits use ordinary MapDoc Undo/Redo and do not save files, tick/install
PHYSIS, or submit live/remote edits. Existing map/native formats are unchanged.
Physical sky and fog remain presentation APIs documented in the PRISMA skill.

## Nature presentation host — wildfire on grass, wind, smoke, sound (2026-11-21)

Read-only host layer in `engine/world/` (PHYSIS core stays headless):

| Header | API |
|---|---|
| `PhysisNaturePresentation.h` | `buildNaturePresentation(world, NaturePresentationPolicy)` → `NaturePresentationFrame` (bilinear fire / charred = 1 − fuel / wetness / snow / `windAt`, stable sub-cell flame points, fire clusters, `signature(rect)`) |
| `PhysisNatureCues.h` | `NatureFxPlanner`, `NatureAudioPlanner` (device-free), built-in `physis_flame` / `physis_smoke`, `addPhysisNatureSounds(bank)` |
| `PhysisNatureFxHost.h` | `PhysisNatureFxHost::init/update/reset` on `fx::ParticleSystem` (wind via `setWind`) |
| `PhysisNatureAudioHost.h` | `PhysisNatureAudioHost::update/reset` on `audio::AudioEngine` |
| `PhysisNatureGrass.h` | `applyPhysisGrassPresentation`, `applyPhysisGrassWind` → `terraweave::GrassPatchRenderer` |

TerraWeave grass gained an optional `GrassPresentationOptions` overlay
(`TerraWeaveGrassRender.h`): ash stubble, emissive burning blades, wind sway
via `canopyWindGraph`. Without it the model is unchanged.

Editor: PHYSIS window → «Живая природа в «Сцене»» or MCP `physis_nature_preview`.
It shows a running game's PHYSIS read-only, otherwise a private preview World
built from the map's `physis` block; LMB ignites in «Поджигать кликом». The
heatmap overlay reads the same preview World.

Fire spread fix (same date): wind now pushes fire downwind (source-cell wind
along the spread direction, tailwind ×2 at 20 m/s, headwind ×¼). Zero wind is
unchanged; wind+fire replays from before differ.

Tests: `physis_nature_presentation`, `physis_nature_cues`, `physis_nature_preview`,
extended `physis_disasters`. Handoff:
`docs/agent-handoffs/physis-nature-host-2026-11-21.md` (macOS build/visual QA NOT RUN).
