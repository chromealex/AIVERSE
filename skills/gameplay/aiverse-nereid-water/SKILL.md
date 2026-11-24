---
name: aiverse-nereid-water
description: Use NEREID deterministic ECS water bodies and contact sampling in native projects of any AIVERSE game genre.
---

## Mandatory TerraWeave + NEREID ownership

User instruction: construct game environments through TerraWeave + NEREID first.
Terrain/biomes/trees/grass/rocks belong to TerraWeave; all new rivers/lakes/oceans
belong to NEREID. Never substitute a custom height/noise generator or a static PBR
water plane and describe it as the engine feature. Read
`skills/workflow/aiverse-terra-first/SKILL.md` and
`skills/gameplay/aiverse-nereid-water/SKILL.md`. Existing legacy maps retain their
compatibility until an explicit tested migration. For missing host integration,
add the adapter to the shared subsystem; record a real capability gap before any
custom fallback. Do not fake waves, flow, foam or buoyancy in project scripts.
Sky stays with PRISMA, climate with PHYSIS, VFX with SpectraFX; compose owners.

Planets: the sea of cube-sphere bodies draws with NEREID's wave model from `engine/render/NereidWaveShaders.h`
(shared with plane water; `nereidSphereNormal` / `nereidSpherePatch` / `nereidSphereFoam` sample it triplanar),
colour by depth and coast foam (`engine/view/game/GamePlanetShaders.h` only calls them). Water bodies on spheres:
`"engine/world/nereid/NereidSphereWater.h"` (`WaterSphereComponent` shells, `sampleSphereWater`,
`addSphereWaterSamplingSystem` / `addSphereWaterBuoyancySystem`, `registerNereidSphereWaterPersistence`; radial depth
and up, a current about an axis, optional `WaterSphereWavesComponent` swell, `waterSphereFromJson` / `spawnWaterSpheres`;
events in `NereidSphereInteractions.h`: `attachSphereWaterActor`, `addSphereWaterInteractionSystem`; or
`NativeWaterPipelineConfig::enableSphereWater`). Genre-neutral: any host may use them. The planet host creates them for
bodies with `"nereid"` (docs/planets.md §1);
extend sphere water there, not in the planet code. Test: `ecs_nereid_sphere_water_test`.


# NEREID: universal water system

Read `AGENTS.md`, `docs/nereid/ARCHITECTURE.md`,
`docs/nereid/HANDOFF.md`, and `docs/water.md` before editing water.

## Use the shared package

Link `AIVERSE::lib_nereid_water`, include
`"engine/world/nereid/NereidWaterSystems.h"`, and use
`aiverse::lib::nereid`. Register `registerNereidWaterPersistence(sim)`
from native `GameModule::setup`. Configure ECS WaterBodyComponent Entities;
attach WaterProbeComponent+WaterContactComponent to any unit, actor, ship or
object. Supply WaterProbe position after movement, optionally by
`addWaterProbeFromTransformSystem`, then `addWaterSamplingSystem`.

The sampling policy is common across genres; do NOT implement separate FPS
or RTS water sampling. `sampleWater(world,position)` selects priority,
surface, then stable generation-safe Entity. `depth` is clamped 0..surface-
bottom and `submerged` is true only within the fluid volume.

## Do not conflate different sources of truth

Existing RTS maps use `waterNetwork` in editor/presentation, and baked
water tiles in game simulation. Do NOT replace the old tile-based mechanics,
change beach/cliff bake or reinterpret world coordinates without explicit
migration and parity tests. NEREID starts opt-in.

The NEREID values are integer/Fixed; GPU displacement and refraction must
never affect authoritative damage, swimming or network state. Add future
shapes, water physics, VFX and editor integrations as separate systems or
renderer adapters, not genre forks.

## Tests, handoff, status

`ecs_nereid_water_test` tests two independent native GameModules, native
ECS persistence, checksum replay and COW. Until it passes on a real build,
the system is **experimental/unverified**.

After each implementation commit, update `docs/nereid/HANDOFF.md` with exact
SHA, changes, verification status, unfinished work and next action. Also
update this skill and `docs/nereid/ARCHITECTURE.md` when API changes.

## Stage 2 — buoyancy and drag

For buoyant objects also link `AIVERSE::lib_nereid_buoyancy`, include
`"engine/world/nereid/NereidBuoyancySystems.h"`, and register
`registerNereidBuoyancyPersistence(sim)` at setup.

Attach WaterBuoyancyComponent, WaterVelocityInputComponent and
WaterForceComponent to the querying Entity. Host physics supplies the
per-tick velocity. Run `addWaterBuoyancySystem` after the water sample
and before host integration, then consume WaterForceComponent.acceleration
with the host's *existing* gravity/collision solver. Never duplicate the
velocity owner or integrate gravity twice. Missing/stale contact returns
zero, and outputs are deterministic Fixed values.

Test target: `ecs_nereid_buoyancy_test` (unverified).


## Stage 3 — polygon lakes and spline rivers (experimental)

Use `"engine/world/nereid/NereidGeometry.h"` or the
`NereidWaterSystems.h` umbrella and register
`registerNereidGeometryPersistence(sim)` when enabling shape overrides.

Add a `WaterGeometryComponent` to the same Entity as a
`WaterBodyComponent`. Use `WaterGeometryKind::Polygon` for an X/Z
closed simple ring (no closing duplicate necessary) or
`WaterGeometryKind::RiverSpline` for a centerline with a half-width
per `WaterGeometryPoint`. Attach 3..256 polygon points or 2..256
spline points; all spline half-widths must be positive. Surface, bottom
and flow are still defined by the regular water body. Only the footprint
is overridden. `sampleWater` already reads it.

Never feed float renderer tessellation or shader waves back into gameplay.
Do not re-encode legacy `waterNetwork` until migration tests prove
unchanged water baking and shoreline behavior. New test target
`ecs_nereid_geometry_test` is authored but NOT RUN; also run previous
NEREID tests before declaring this stage verified.


## Stage 4 — optional indexed water sampler

Include `"engine/world/nereid/NereidQueryIndex.h"`. For large water-body
counts, replace `addWaterSamplingSystem` with
`addIndexedWaterSamplingSystem(sim,"water.sample")`. **Do not register
both.** It generates an in-tick temporary AABB index, scans infinite oceans
separately and uses the same deterministic selection and contact schema.

`WaterQueryIndex(world).sample(position)` is suitable for read-only
batch queries only: constructing the index holds a reference to the World.
Never retain it across World mutations, reloaded saves or rollback.
Always rebuild when source bodies or spline geometry have changed.
For few bodies and probes, the linear sampler may be cheaper.

Stage 4 authored test: `ecs_nereid_query_index_test`, not yet executed.
Run all four `ecs_nereid_.*_test` cases plus existing RTS water tests,
and record actual build/CTest results in `docs/nereid/HANDOFF.md`.

## Stage 5 — legacy waterNetwork / tile-map importer (experimental)

For **existing** RTS/editor water, read
`engine/view/game/NereidLegacyWaterAdapter.h` and
`docs/nereid/ARCHITECTURE.md`.

Do not import control points directly: old `waterNetwork` uses
Catmull-Rom and a 0.75-cell bake margin; straight-line conversion
changes river edges. Use
`game::water::importBakedWaterCellsToNereid(cells,w,h,config)`
for the simulation's canonical `~` raster, or
`game::water::importWaterNetworkToNereid(network,w,h,legacyCm,config)`
only when source-network (pre-postprocessing) parity is desired.
The bridge returns Box `WaterBodyComponent` values for a native
project to attach; it does not modify the old map, editor, or ECS
implicitly. `WaterRasterImportConfig` provides Fixed coordinates
and surface/depth/priority, and must use the map's cell-metre scale.

This is a **load-time import**, not a per-tick conversion. For
runtime-updated maps, perform a deliberate body replacement and
rebuild indexed sampling next tick. The resulting rectangular cover
matches the legacy mask at **cell centres**, not at all sub-cell
positions; baked `~` cells also lose per-body semantic WaterKind.

The generic raster conversion lives in
`engine/world/nereid/NereidRasterImport.h` and does **not** depend
on RTS. Test: `ecs_nereid_legacy_raster_test` (authored, NOT RUN).
Record actual results and any fixes in `docs/nereid/HANDOFF.md`.

## Stage 6 — easiest opt-in native water setup

Include `"engine/world/nereid/NereidNativeWater.h"`, link public
`AIVERSE::lib_nereid_water` and (when buoyancy is enabled)
`AIVERSE::lib_nereid_buoyancy`. From native GameModule::setup, call
`registerNativeWaterPipeline(sim,"water",cfg)` exactly once.
`NativeWaterPipelineConfig` toggles geometry persistence, buoyancy
output and optional Transform->WaterProbe synchronization.
`spawnNativeWaterBodies(sim.world(),bodies)` creates ECS-only water
body entities in deterministic source order. Host still owns velocity,
gravity, collision, structural changes and entity lifecycle.

`addAdaptiveWaterSamplingSystem(sim,name,AdaptiveWaterSamplingConfig{})`
is available if manually composing a pipeline. It uses only ECS
water-body/probe counts to select reference or indexed sampling, not
frame timings; tune thresholds via profile data. Do not register it
alongside other NEREID sampling systems. No extra persistence schema.

New test: `ecs_nereid_native_pipeline_test`. All six NEREID
fixtures are also included in `tests/run_ecs_experiment_suite.sh`,
which CI runs. Do not claim green until GitHub Actions completes.

## Stage 7A — sampling performance benchmark

Profile the water broadphase before tuning
`AdaptiveWaterSamplingConfig` thresholds. Build/run
`aiverse_ecs_nereid_sampling_bench` or use
`bash tests/run_ecs_experiment_suite.sh --bench`.
The test reports microseconds per tick for 16/64/256 bodies with
8/128/1024 probes, explicitly including index rebuild time.
It is a benchmark with gameplay parity checks, not a CI timing gate.
No measurements were run during stage 7A.

## Stage 7B — surface meshes for any genre

Include `"engine/world/nereid/NereidWaterSurfaceMesh.h"` for a
read-only, backend-agnostic water mesh. Call
`buildWaterSurfaceMesh(body, optionalGeometry, options)` in
presentation/assets preparation, NOT authoritative Simulation
updates. Outputs vertices (X,Y,Z,UV,shore factor) and 32-bit indices.
Supported: box/disc/infinite patch, simple polygons (concave allowed)
and variable-width river strips. Near/Medium/Far modes control LOD;
ocean patch is camera-relative, so rebuild when its reference moves.
Zero/invalid polygons return empty. GPU material hookup is a separate
renderer stage; never present this as completed refractive water.

Test `ecs_nereid_surface_mesh_test` is authored but UNVERIFIED.

## Stage 7C — draw water surfaces with existing renderer

With a presentation-owned `gfx::Device` and
`render::Renderer3D`, include
`engine/render/NereidWaterRenderer.h`:

```cpp
auto cpu=nw::buildWaterSurfaceMesh(body,geometry,meshOptions);
auto gpu=aiverse::render::uploadNereidWaterSurface(device,cpu,style);
// Every scene frame, after Renderer3D::beginScene and before render:
aiverse::render::drawNereidWaterSurface(renderer,gpu,style);
// During scene shutdown or before replacing the uploaded mesh:
aiverse::render::releaseNereidWaterSurface(device,gpu);
```

`kDrawWaterSurface` is the opt-in water shader flag; at any
world Y it enables the existing open-water normal-wave shading
and the renderer's existing reflection quality tier. No
separate NEREID shader copies are necessary. A single planar
reflection plane is currently shared across the scene; SSR only
works in trace-enabled quality modes. This is not yet an
automatic RTS/FPS integration or realistic refraction/underwater
system.

Run `ecs_nereid_surface_mesh_test` and
`ecs_nereid_renderer_contract_test`, then actual GL/Metal
rendering, before marking this verified. Maintain GPU handle
lifetime and avoid changing ECS from renderer code.

## Stage 8 — water at several heights

The current Renderer3D planar mirror is single-plane. NEREID's
`kDrawWaterSurface` surfaces at a different Y now explicitly skip
the primary plane reflection instead of showing the wrong scene
image. The matching policy is
`render::sameWaterReflectionPlane` in
`engine/render/NereidWaterReflectionPolicy.h`.
Secondary surfaces retain wave shading and trace-tier SSR when
active. Do not claim separate multi-plane reflections or correct
refraction exist yet.

When working on render paths, test multiple water heights and both
OpenGL and Metal; the new `ecs_nereid_renderer_contract_test`
contains basic CPU/shader-source checks but cannot substitute GPU
compilation or screenshots.

## Stage 22 — universal water interactions (feature work, tests deferred)

**Source:** `engine/world/nereid/NereidInteractions.h`,
`engine/world/nereid/NereidInteractionPresentation.h`.
**Link target:** `AIVERSE::lib_nereid_interactions` (native C++, independent of genre).
Opt in with `NativeWaterPipelineConfig::enableInteractions=true` before
`registerNativeWaterPipeline`; this installs the system AFTER water sampling.
Do not also install another interaction system.

For an actor, install `WaterProbeComponent`, `WaterContactComponent`,
`WaterInteractionComponent`, `WaterInteractionStateComponent` and
`WaterInteractionOutputComponent`. Synchronize actor position into the probe
using the existing native probe stage. Each tick, the interaction stage emits
at most one `Enter`, `Exit`, `SwitchBody` or `SurfaceWake` output when
conditions change; initial immersion does not produce a fake entry.
Wake spacing, near-surface depth, radius and strength are fixed-point.
Only near-surface movement emits wakes.

Post-tick, read `snapshotWaterInteractionEvents(world,completedTick)`.
It returns stable actor-ID-ordered events suitable for spawning SpectraFX
effects, audio or ripple decals. Never clear or mutate authoritative outputs
from a presentation thread. The host owns actual VFX/audio presentation.
`WaterInteractionStateComponent` is saved for rollback consistency;
`registerNereidInteractionPersistence` is invoked by the optional native
pipeline stage. Do not wire this API to RTS-specific actors or duplicate
the core physics integrator.

**Verification:** source authored only; C++ builds/CTest/visual checks
NOT RUN by design, as requested by the user. After every implementation
stage update `docs/nereid/HANDOFF.md` with exact SHA, changed files,
limits and next features.

## Stage 23 — presentation ripples, expanding wakes and SpectraFX hook

Include `"engine/world/nereid/NereidInteractionPresentation.h"` in the native
game's presentation code. `WorldScene` now also exposes an optional
bounded `NereidRippleField` visual renderer. Do not put it inside
Simulation, ECS checksums, rollback state, Box3D or the water physics
library. Use once per completed simulation tick at the read barrier:

```cpp
auto events=lib::nereid::snapshotWaterInteractionEvents(sim.world(),sim.tick());
scene.ingestNereidInteractions(events,sim.tick(),
    [&](const lib::nereid::WaterPresentationEvent& event) {
        // Dispatch to SpectraFX/audio based on Enter, Exit, Wake or SwitchBody.
        // This callback is a host hook; it is not an automatic SpectraFX API.
    });
// Each rendered frame, on the scene/presentation thread:
scene.advanceNereidRipples(frameDeltaSeconds);
// Existing scene.draw(renderer,renderOrigin,originCellSize) draws the rings.
```

Event tick deduplication avoids repeated visual emissions from duplicate
host calls. A rollback to an earlier tick clears old VFX history.
`NereidRippleField` holds up to 256 pulses, oldest first eviction;
individual lifetimes are short and their radius grows with age.
Rendering uses the standard Renderer3D triangle queue and subtracts
the current render-origin in double before converting to float.
This is a zero-new-backend, GPU-allocation-free overlay, not yet
a water-surface shader displacement or real SpectraFX implementation.

**Verification:** tests deliberately deferred; do not claim green.
See `docs/nereid/HANDOFF.md` for exact stage commits/limits/next task.

## Stage 25 — scaled water ripples and real SpectraFX cue adapter

NEREID now supports **up to 16** radial normal/foam impulses per
water-surface draw and **up to 256** recent visual ripples across the scene.
Ripple selection uses the active scene observer, not the first vertex
of a large polygon, and GPU data is stored on the per-draw item.
The GLSL/Metal code adds impact foam over existing NEREID procedural
wave normals. No extra shaders/RHI targets/textures are required.

For real event-to-particle playback, include
`engine/world/NereidSpectraFxBridge.h`, construct it from the native
host's already initialized `aiverse::spectra::Runtime`, and supply
graph names that SpectraFX has loaded/defined:
```cpp
world::NereidSpectraFxBridge splash(particles);
splash.setCue(lib::nereid::WaterInteractionKind::Enter,
              {"fx/water_splash",1.0f});
splash.setCue(lib::nereid::WaterInteractionKind::SurfaceWake,
              {"fx/water_wake",0.4f});
auto events=lib::nereid::snapshotWaterInteractionEvents(
    sim.world(),sim.tick());
worldScene.ingestNereidInteractions(events,sim.tick(),
    [&](const auto& event){splash.emit(event);});
```
The graph keys above are illustrative and must be defined by each native
project; no built-in SpectraFX particle asset is assumed. Call
`splash.reset()` on rollback/new scene to kill tracked one-shots.
The adapter holds up to 256 handles and calls the actual
`ParticleSystem::play` API. NEREID does not own particle ticking/rendering.
**Tests remain deferred by explicit user direction.**

## Large open seas and legacy RTS map water (2026-11-22)

**Sea over a big world:** `"shape": "infinite", "oceanLod": "rings", "oceanPatchRadius": 6000`
(+ optional `oceanInnerCell` metres, `oceanInnerCells` even 4..64) - stitched LOD rings around
the camera, bounded triangles, rebuild every `cell*cells/2` metres. Default `"patch"` keeps one
square, now centred on its cell (r/2 of water in every direction). See `docs/nereid/ocean-lod.md`.

**Old RTS cell water (`~`):** do not hand-convert. Set the game's
`terrain.water.renderer: "nereid"` (optional `terrain.water.nereid` style overrides); the engine
converts the legacy sheet/bodies layout to NEREID surfaces and fails closed to the legacy mesh.
`AIVERSE_WATER_RENDERER=legacy|nereid` for A/B. See `docs/nereid/legacy-water-migration.md`.

## Stage 26/27 — ocean tile waves and universal waterfalls

**Ocean/large boxes:** use `"rippleTileGrid": 4` (range 1..8) on an
individual `nereidWater.surfaces[]` manifest entry. The water builder
subdivides rectangular Box/Infinite water into a static N×N grid; one
resident GPU mesh retains disjoint index ranges. Each range selects up
to 16 NEREID ripple impulses based on the tile's true world AABB.
Default 1 is backward-compatible. Avoid 8 on weak mobile devices
unless profiling justifies up to 64 indexed draws per water body.
Nonrectangular geometries currently use the legacy draw.

**Waterfall/cascade geometry:** include
`"engine/world/nereid/NereidCascades.h"` and link
`AIVERSE::lib_nereid_cascades`. For ECS-native projects set
`NativeWaterPipelineConfig::enableCascades=true` to persist
`WaterCascadeComponent`. The component is genre independent:
`points[{position,halfWidth}]`, `widthAxis` (horizontal X/Z),
`foam`, `speed`, `enabled`. Points descend monotonically, allow
horizontal shelves, and represent a vertical water ribbon, not a
separate water-contact volume.

For a pure CPU mesh use
`buildWaterCascadeMesh(cascade, WaterCascadeMeshOptions{})`.
WorldScene's `nereidCascades` JSON is an optional presentation
manifest (no legacy RTS migration), and native ECS hosts may call
`setNereidCascades(snapshotNereidEcsCascades(world))` after a read
barrier. Use the existing `WorldScene::update/draw/shutdown` device
lifecycle. NEREID CascadeScene owns GPU handles; the existing
Renderer3D `kDrawNereidCascade` flag disables incorrect horizontal
planar reflections and enables animated vertical water streaks.

Example scene fragment:
```json
{
  "nereidCascades": {
    "maxUploadsPerFrame": 2,
    "sources": [{
      "id": 3,
      "widthAxis": [1,0],
      "foam": 0.6,
      "speed": 7,
      "stepsPerSegment": 4,
      "points": [[10,20,0,4],[12,9,0,5],[14,3,0,6]],
      "style": {"opacity":0.75,"foamStrength":0.8}
    }]
  }
}
```

This is a double-sided static/stepped waterfall mesh with animated
visual normals, not a true fluid simulation or automatic SpectraFX
spray. Keep ECS force, geometry, presentation, Prisma and SpectraFX
responsibilities separate. The owner requests features before tests;
no builds/tests should be claimed PASS without execution. See the
newest sections of `docs/nereid/HANDOFF.md`.

## Stage 28 — visual wave advection and persistent SpectraFX cascade spray

`WorldScene::advanceNereidRipples(dt)` now advects each visual
ripple's world X/Z centre using the matching `WaterBodyComponent::flow`
from the scene's copied NEREID water sources. Flow is clamped for
presentation; there is **no ECS mutation**, new physics integrator or
network side effect. If a matching source has disappeared the ripple
continues to decay without advection. Genre-neutral and independent
of PHYSIS.

To add continuous spray at a waterfall's foot, include
`engine/world/NereidCascadeSprayBridge.h` in a native project's
presentation layer:
```cpp
world::NereidCascadeSprayBridge spray(particleSystem);
spray.setGraph("fx/waterfall_spray",1.0f); // host-defined SpectraFX graph
// Each presentation frame:
spray.sync(worldScene);
// On reset/rollback, after stopping the scene:
spray.reset();
```
The bridge obtains a read-only `WorldScene::forEachNereidCascade`
snapshot and maintains a bounded map of `FxHandle` per stable
cascade ID. It calls the REAL `ParticleSystem::play/move/stop/kill`
APIs; particle graph assets are not bundled or magically defined.
The host owns the particle runtime and its rendering order. Spawning
spray is optional and never modifies a water ECS component. See
`docs/nereid/HANDOFF.md` for ownership/limits/next work.

**Verification:** implementation only; compilation, GPU visuals and
tests deferred by explicit user direction.

## Stage 29 — spatial river/rapids currents (features-first)

Use `"engine/world/nereid/NereidCurrentField.h"` and link
`AIVERSE::lib_nereid_currents` in native projects. On a water-body
Entity with an enabled `RiverSpline` `WaterGeometryComponent`,
attach `WaterCurrentFieldComponent` with exactly one
`WaterCurrentNode {downstream,lateral,shear}` per river point.
Enable persistence using `NativeWaterPipelineConfig::enableCurrentField`.

`waterCurrentAt(body,geometry,field,probePosition)` computes
fixed-point local velocity: body base `flow`, interpolated
downstream tangent speed, lateral side-bank velocity and signed
cross-river shear, faded toward the banks. Existing reference and
indexed NEREID samplers automatically pass the current field into
`WaterSample.flow`; buoyancy/drag already consumes that same sample.
There is no extra genre system and no PHYSIS dependency. Invalid or
disabled fields fall back to `body.flow`.

Read-only ECS water snapshots include the field. Native worlds
using `WorldScene::setNereidSurfaces` therefore advect visual ripples
by the same locally sampled current. Static authored rivers may use:
```json
{
  "kind":"river","shape":"river","surfaceY":10,"bottomY":0,
  "points":[[0,0,3],[20,0,4],[32,10,5]],
  "currentField":{
    "bankFade":0.25,
    "nodes":[[2,0,0],[8,0.5,2],[4,-0.3,-1]]
  }
}
```
Do not confuse `WaterCurrentNode` with geometry vertices. Negative
node speeds represent upstream or counterflow. Do not use this
presentation bridge as authoritative fluid simulation; PHYSIS/Box3D
and game movement retain ownership of integrators and gravity.
No tests/compiles performed per user direction. Exact stage commits,
limitations and next implementation are in `docs/nereid/HANDOFF.md`.

## Stage 30 — directional stern wakes, preserving existing interaction saves

The optional `WaterWakeDirectionComponent` is now a separate
persisted actor ECS component: `direction` (normalized X/Z),
`travelled` (metres since previous tick), and `sampledTick`.
Attach it to actors which need directional wakes, alongside the
existing `WaterInteraction*Component` set.
`registerNereidInteractionPersistence(sim)` registers this extra
optional component without modifying the v1
`WaterInteractionOutputComponent` wire/save schema.

For `SurfaceWake` events, `addWaterInteractionSystem` samples
movement in deterministic `Fixed` from prior probe position,
publishes optional direction, and the read-only presentation snapshot
copies that direction only when the tick matches. Other contact
events preserve their circular wave visuals.

The Stage 23 `NereidRippleField` now creates aligned
`NereidRippleShaderPacket {rings,shapes}` for the 16 closest
events per water surface or spatial tile. The shared Renderer3D
draw item and GLSL/MSL U0 have a second `vec4[16]`
(`waterWakeShapes`) parallel to the original ripple array:
X/Z is normalized heading, Z contains a transverse shape scale,
W marks directional wakes. W=0 keeps the original radial ripple.
The fragment shader produces a stern-biased elliptical wake and
normal/foam response. No new textures, passes, physics integration
or genre dependency. GPU shader validation remains deferred by
explicit owner instruction. Exact file/commit list is in
`docs/nereid/HANDOFF.md`.

## Stage 31 — SpectraFX emitters at intermediate waterfall steps

`NereidCascadeSprayBridge` now supports two independent, optional
continuous particle graph mappings, supplied by the **native game**:
`setGraph(graph,scale)` for the final cascade foot and
`setStepGraph(graph,scale,minDropMetres)` for qualifying
intermediate segment endpoints (the last point is reserved for the
existing foot emitter). Up to eight intermediate emitters are
presented per cascade, each keyed by `(stable cascade id, step index)`.

```cpp
world::NereidCascadeSprayBridge spray(particles);
spray.setGraph("fx/cascade_foot",1.f);
spray.setStepGraph("fx/cascade_step",0.65f,0.75f);
// Each presentation frame, using the read-only WorldScene snapshot:
spray.sync(worldScene);
// On rollback/new scene:
spray.reset();
```

The bridge uses the REAL SpectraFX `play/move/stop/kill` calls,
keeps existing handles alive on unchanged geometry, stops removed
steps, and kills only handles it owns on reset. Both graph keys
above are placeholders: load/define them in the project first.
NEREID does not allocate a separate particle runtime or change
`WaterCascadeComponent` persistence. No tests/builds were run,
per owner direction; see HANDOFF Stage 31.

## Stage 32 — optional PHYSIS/weather wind presentation bridge

Wind is **owned by PHYSIS or the native game's weather source**,
not by NEREID. The current water branch contains no linked PHYSIS
component contract, so NEREID accepts a generic sampled
`Vec3` velocity and `Fixed` gust, after the ECS read barrier:

```cpp
#include "engine/world/NereidPhysisWindBridge.h"
// Supply these values from the external PHYSIS wind query:
world::applyNereidPhysisWindSample(
    worldScene, sampledWorldWindVelocity, gustFraction, 1_fx);
// Existing WorldScene draw receives the current NEREID visual wind.
worldScene.advanceNereidRipples(deltaSeconds);
// On PHYSIS disconnect/world reset:
world::clearNereidPhysisWind(worldScene);
```

`engine/world/NereidWindPresentation.h` defines opt-in
`NereidVisualWind {east,north,gust,coupling}`.
`WorldScene::setNereidVisualWind` is a direct non-PHYSIS
alternative. Values are finite-checked and capped to ±100 m/s
and [0,1] strengths. The shared Renderer3D water draw uniforms,
GLSL and Metal water normals now respond to wind direction,
steady strength and gusts; the existing presentation ripple field
gets a small additional wind drift **after** the local river current.
A zero wind vector or coupling zero preserves old water behavior.
Untagged legacy water and genre-independent simulation/physics
are not changed.

PHYSIS may provide this snapshot from its own wind system, but
do not include PHYSIS internals in the NEREID ECS library and do
not imply a full native PHYSIS adapter has been compiled/integrated
until both workstreams merge. Tests deferred by owner request.
See `docs/nereid/HANDOFF.md` Stage 32.

## Stages 33–35 — spatial wind, eddies, and water↔cascade connections

**Spatial PHYSIS wind** (`engine/world/NereidWindField.h`):
`WorldScene::setNereidWindZones(std::vector<NereidWindZone>)` accepts
up to 64 bounded local wind samples `{x,z,radius,wind,influence}`
after the weather read barrier. Base wind continues through
`setNereidVisualWind`; per-water tile shading and drifting
presentation ripples sample wind at their own world X/Z.
`applyNereidPhysisWindZones(scene,zones)` is a convenience adapter
but PHYSIS still owns its ECS/physics. No direct PHYSIS dependency.

**Eddies/rapids** (`"engine/world/nereid/NereidVortices.h"`):
opt into `AIVERSE::lib_nereid_vortices` and register
`NativeWaterPipelineConfig::enableVortices`. Add
`WaterVortexFieldComponent` to an existing water-body Entity
with at most 16 `WaterVortex {center,radius,circulation,radial}`
entries. Both reference/indexed NEREID samplers now share
`waterCurrentWithVortices`: constant body flow + optional
RiverSpline profile + local fixed-point swirl/radial velocities.
`WaterSample.flow` remains the single authoritative output used
by generic buoyancy and motion. A manifest water surface can include
`"vortices": [[x,z,radius,circulation,radial]]`.
This is analytic flow, not a Navier-Stokes solver.

**River → cascade → lake visual connections**:
`NereidCascadeSource` adds `inletWaterId`, `outletWaterId`,
`joinLength` (0.1–50m). Optional JSON fields with the same names
link stable surface IDs; connected water surface heights are read
from the existing `NereidWaterScene`. If a linked surface is
present and elevation ordering permits it, `NereidCascadeScene`
extends a presentation-only ribbon into that water surface with
foam-masked seams. Changes of water levels invalidate the
cascade GPU resident and rebuild it with existing upload budgets.

Native ECS projects use the separately persisted
`WaterCascadeConnectionsComponent` on the cascade Entity:
`{inlet: Entity, outlet: Entity, transitionLength: Fixed, enabled}`.
The read-only `snapshotNereidEcsCascades` converts these IDs into
NEREID scene IDs `Entity::packed()+1`. Never mutate either
`WaterBodyComponent` or `WaterCascadeComponent` during GPU
preparation. Endpoints must be authored within/overlapping
the intended river/lake footprint; automatic river topology
and physical mass continuity are **not** implied.

Example:
```json
{
  "nereidCascades": {
    "sources": [{
      "id": 31, "inletWaterId": 10, "outletWaterId": 11,
      "joinLength": 3.0,
      "widthAxis": [1,0],
      "points": [[5,15,0,3],[7,5,0,4]]
    }]
  }
}
```

Per-stage implementation notes, changed files and exact commits
are in `docs/nereid/HANDOFF.md`. Owner requested **functions
before tests**, so implementation must not be described as
build-verified or GPU-validated.

## Stages 36–38 — shoreline joins, cascade current and rapid whitewater

**Stage 36: author safe join widths and banks** with
`NereidCascadeSource::joinWidthBlend` [0,1] and
`requireFootprint` (default true); these are also valid JSON fields.
The read-only cascade scene interpolates the linked RiverSpline
half-width and validates the exterior join endpoint and both edges
against the linked water footprint. The immutable water body must
be published before cascade GPU update; geometry revisions trigger
join rebuilds even if water elevation stays the same.
ECS-native cascades may attach an optional persisted
`WaterCascadeJoinPolicyComponent` rather than modifying existing
`WaterCascadeConnectionsComponent` v1.

**Stage 37: author optional local current continuity** by attaching
`WaterCascadeFlowPolicyComponent {radius,blend,speedLimit,enabled}`
to the same cascade Entity as `WaterCascadeComponent` and
`WaterCascadeConnectionsComponent`; persistence is registered
with `NativeWaterPipelineConfig::enableCascades=true`.
Normal and indexed NEREID water samples share
`applyWaterCascadeFlow` after ordinary river/vortex flow
selection. A connected cascade mouth steers `WaterSample.flow`
locally without changing `WaterBodyComponent.flow` or applying
host forces. Native scene JSON can set
`"flowContinuity":{"radius":8,"blend":0.75,"speedLimit":30}`.
The NEREID presentation flow bridge uses the same fixed-point
mouth kernel, so visual ripples also follow the mouth current.
No PHYSIS physics ownership is duplicated and this is NOT
mass-conserving fluid simulation.

**Stage 38: rapid whitewater material** is opt-in via
`WaterSurfaceStyle::rapidFoamStrength` [0,1],
`rapidFlowThreshold` [0,100] metres/sec, and
`rapidNoiseScale` [0.1,8]. Scene JSON style uses the matching
`rapidFoamStrength`, `rapidFlowThreshold` and `rapidNoiseScale`
keys. When enabled, NEREID samples local river/current/vortex flow
(and cascade-mouth transfers if supplied by the native scene),
passes current X/Z + foam intensity + scale in the shared per-draw
`waterHydraulics` vec4. GLSL/MSL use an animated breakup pattern
and normal perturbations. Works with existing tiled-water draws,
no extra texture, compute shader or fullscreen pass. Disabled by
default, and ordinary genres/legacy water remain unchanged.

**Validation status:** features committed, but native build, GPU
compiler, CTest and visual integration are **NOT RUN** by owner
instruction. Handoff for each stage is appended at
`docs/nereid/HANDOFF.md`. Do not make PASS claims.


## Stage 39 — rapid foam colour

Scene `style.rapidFoamColor: [r,g,b]` now controls only the current-generated whitewater foam RGB (linear, each 0..1). C++ `WaterSurfaceStyle::rapidFoamR/G/B` defaults to the previous shader color `{.84,.93,.94}`; regular shoreline foam is still controlled separately by `foamColor`. With `rapidFoamStrength=0`, the new colour has no visual effect. Keep the new shared C++/GLSL/MSL `waterRapidColor` U0 field in sync, including `Renderer3DReflect.cpp`; do not modify authoritative ECS state. The 16-byte U0 extension and all tests are unverified. Read `docs/nereid/stage-39-rapid-foam-color.md` for full stage handoff and next steps.


## Stage 40 — configurable rapid-water shading cost

Use scene `style.rapidDetail` values `"low"`, `"medium"`, `"high"` or native `WaterSurfaceStyle::rapidDetail` enum. Default Medium reproduces the Stage 39 material. Only water presentation changes: `waterRapidColor.a` selects the shader variant without changing U0 or ECS. Keep GLSL/MSL parity. Quality does not imply geometry LOD; choose separately. Full handoff `docs/nereid/stage-40-whitewater-quality.md`; **no build/tests/shader execution performed**. Next agent implements explicit linked river confluence without genre dependencies.


## Stage 41 — confluence current ECS composition

Include `"engine/world/nereid/NereidConfluences.h"`; enable `NativeWaterPipelineConfig::enableConfluences`, then attach a `WaterConfluenceComponent` on a dedicated junction Entity with 2..4 distinct inlet water Entity IDs, one distinct outlet and Fixed mouth/radius/blend/speed limit. All linked bodies must cover the authored mouth; invalid/dead link suppresses junction. The same `applyWaterConfluenceFlow` runs in reference/indexed water sampling before cascade continuity. The component has independent persistent schema `nereid.confluence` v1; do not alter old water archives or genre modules. Rendering bridge remains pending. Handoff: `docs/nereid/stage-41-confluence-ecs.md`. **C++/CTest unverified**.


## Stage 42 — confluence presentation snapshots

After publishing the water-body ECS snapshot to WorldScene, call `scene.setNereidConfluences(aiverse::world::snapshotNereidEcsConfluences(sim.world()))` with `engine/world/NereidConfluenceEcsPresentation.h` after the same ECS read barrier. Water sources must share `packed Entity + 1` stable IDs. Visual NEREID flow is composed as ordinary current -> confluence -> cascade, then used for ripples and rapid-water shader inputs. No implicit JSON scene topology inference or renderer-to-ECS feedback. Read Stage 42 handoff at `docs/nereid/stage-42-confluence-scene.md`; CMake, CTest, shader compilers and graphics execution remain unverified.


## Stage 43 — avoid water mesh upload for shader-only edits

`NereidWaterScene::setSurfaces` does not increase mesh revision for changes to optics, rapid-foam color/quality/threshold, current/vortices and other draw-only fields. A live `NereidSurfaceResident::setDrawStyle(style)` applies those fields with no VB/IB reupload; changes to deepColor, foamColor or foamStrength are vertex baked and still require upload. Async workers keep geometry-only upload-key matching. Full handoff `docs/nereid/stage-43-material-residency.md`. Native build, CTest and GPU verification are pending.


## Stage 44 — JSON confluence authoring

To author presentation-only confluence flow, include `"nereidConfluences":{"sources":[...]}` alongside `nereidWater.surfaces` in a WorldScene JSON manifest. Each source requires `id`, 2–4 `inletWaterIds`, `outletWaterId`, and `mouth:[x,y,z]`; optional `radius`, `blend`, `speedLimit`, `enabled`. All referenced `nereidWater.surfaces` MUST have explicit unique IDs, and the mouth must lie within all their X/Z footprints. The loader rejects unknown/duplicate/self IDs before scene construction. JSON links are stable visual IDs, not ECS Entity IDs; never manufacture fake Entity handles. Authoritative games still use `WaterConfluenceComponent` and `registerNereidConfluencePersistence`. See `docs/nereid/stage-44-confluence-manifest.md` for example, exact files, verification status and next step. **No native test/GLSL/MSL/GPU run has occurred.**


## Stage 45 — authored confluence CPU test

Run `ctest --test-dir build/ecs-test --output-on-failure -R '^ecs_nereid_confluence_contract_test$'` after configuring/building `aiverse_ecs_nereid_confluence_contract_test`. The fixture checks manifest references and errors, ECS reference/indexed water contact parity, snapshot scene flow, disabled junctions, and draw-only water resident upload key. ECS experimental shell CI also includes it. **The tests are authored but NOT RUN here**; do not claim PASS or merge until genuine native/CI verification is available. Full handoff `docs/nereid/stage-45-confluence-contract-tests.md`.


## Stage 47 — one skill per registered NEREID library package

The core package remains `aiverse-nereid-water`. Specialized native targets now have their own AI routing skills: `aiverse-nereid-buoyancy`, `aiverse-nereid-interactions`, `aiverse-nereid-cascades`, `aiverse-nereid-currents`, `aiverse-nereid-vortices`. All reference the shared NEREID architecture/handoff and depend on `aiverse-nereid-water` in the generated catalog. This preserves each library's own CMake target, avoids duplicate skill IDs in `tools/skills/catalog.py validate`, and lets agents request only the required mechanic. Do not reintroduce one SKILL.md path across multiple package records. Refer to `docs/nereid/stage-47-skill-catalog.md`.


## Stage 48 — ocean LOD fixture verification

`ecs_nereid_renderer_contract_test` now tests a four-vertex ocean patch using explicit Far LOD, and additionally tests Medium's subdivided geometry and precise local basis. Defaults remain Medium; no renderer or ECS code changed. Fix was source-reviewed and committed, NOT compiled or run. Full handoff: `docs/nereid/stage-48-ocean-lod-fixture.md`.


## Stage 49 — required release gate

Use `bash tools/nereid/validate.sh --focused` for repeated CPU checks of NEREID, SpectraFX compatibility, native ECS and skills; `--full` rebuilds all test targets and runs the entire CTest suite. Override `AIVERSE_NEREID_BUILD_DIR=build/nereid-test` to reuse an existing build directory; logs are written to `<build>/nereid-logs`. The workflow `.github/workflows/nereid-regression.yml` runs focused checks on Linux/macOS. These checks never establish Metal/GLSL GPU visual parity. Stage 49 source code has been committed but no new CI/CTest pass observed in this agent session; see `docs/nereid/stage-49-validation-gate.md`.


## Stage 50 — confirmed CPU pass, Release check follow-up

On Mac Studio, Stage 49 `--full` at commit `65346f91` passed all 118 CTest cases and 16 focused checks. A later test-only commit (`8f426376ecb095017cefdd313cca5dd6c2d70186`) replaced 44 `assert()` calls across six unrelated ECS tests with always-on checks, eliminating Release warning root cause and enabling previously skipped invariants. Re-run `bash tools/nereid/validate.sh --full` on new head before accepting the branch; do not assume the previous success covers the strengthened checks. Full handoff: `docs/nereid/stage-50-release-assertion-correctness.md`.

## River grade and authoring

For inclined rivers use optional `WaterRiverGradeComponent` and register
`registerNereidRiverGradePersistence` (or native config enableRiverGrade=true).
Never derive gameplay flow from shader time. Manual mode uses WaterBody.flow;
automatic mode uses the downhill grade and bounded linear-resistance estimate.
Read the current river-grade section of docs/nereid/ARCHITECTURE.md. Native
sampling and presentation consume the same grade. Editor typed operations
nereid_water_state / nereid_flow_set are Undoable; HTML5 controls belong to Studio.
Rebuild Terra bank/vegetation masks when changing an exported river's grade.

Native authoring may use `"engine/world/nereid/NereidFootprintDistance.h"` for an
optional signed horizontal clearance (invalid/disabled water => no value).
Use it with local graded height for terrain sand/wetness/grass masks, as Terra's
native exporter does. It does not change authoritative footprint membership,
current sampling or persistence. Tapered-river margins are not an exact SDF;
do not replace contact tests or claim a live erosion solver from this helper.


For NEREID optics, read docs/water.md current/contact section. Refraction and
contact foam must not depend on enabling traced shadows/SSR. Preserve the
optics-only capture flag and independent SSR guard. Diagnose foam with a real
foreground obstacle and both flow directions; do not accept repeated sine
ellipses or detached copies as a wake. --freeze-time in renderbench fixes the
shader clock as well as sky/animation; current velocity remains authored data.

Resolved refraction includes background already: never apply ordinary
background alpha blending a second time. Keep optical density controlled by
authored opacity and final alpha controlled by contact coverage. Recheck
minimum valid opacity against the same scene without water. Use the opt-in
AIVERSE_NEREID_DIAGNOSTIC modes from docs/water.md in an owned review process
when depth/coefficients/background dominance disagree; never publish their
false-colour images as final scene screenshots.


## Local current rendering

Use NereidWaterScene's native flow-map preparation, not one scene-wide shader
angle. Automatic river grade follows the canonical local spline tangent; manual
WaterBody.flow remains manual. Current/vortex fields are sampled into bounded
world-space RGBA16F data. Finite grids ignore cameras and mesh LOD. WorldScene
preserves confluence/cascade/obstruction resolution through the prepare callback.
Only supply a nonzero resolver revision when ALL its inputs are revisioned; zero
revalidates bounded CPU grids and deduplicates equal GPU payloads.

Capture/replay must retain NereidFlowBinding leases. Do not destroy textures on
CPU replacement or allocate them inside a draw encoder. Explicitly shutdown the
owner before Device destruction; preserve fence retirement and revoked replay
guards. See docs/nereid/ARCHITECTURE.md. Native GPU regression:
`python3 tests/render/verify_nereid_flow.py <renderbench> <KINGSTON-output>`; run
Metal and desktop GL separately. Diagnostic flow images are technical evidence,
not final beauty screenshots.
