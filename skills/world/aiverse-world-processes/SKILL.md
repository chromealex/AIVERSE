---
name: aiverse-world-processes
description: Use experimental deterministic world-process policies, then integrate them into a single ECS authority without duplicating PHYSIS, TerraWeave, NEREID, physics, power grids or narrative state.
---
# World processes integration recipe

Read `AGENTS.md`, `docs/agents/ecs.md`, `docs/ecs-mechanic-composition.md`, and `docs/handoffs/world-processes-stage01-2026-11-23.md`.

Ten policy experiments live in `libraries/include/aiverselib/world/WorldProcesses.h`:
- `supported` (FRACTURE), `diffuse` (AETHER), `relax` (MORPH), `audibility` (ECHO), `assignSlots` (CROWD),
  `erode` (IMPRINT), `energize` (CIRCUIT), `settle` (GRANULAR), `age` (EVOLVE), `shareKnowledge` (SOCIETY).

These functions do not touch `Simulation`. They are intentionally NOT self-contained game mechanics. Integrate only the chosen process:
1. Search the owners' existing components and source first; extend shared engine capability rather than reimplementing it.
2. Define data-only component contracts, deterministic unit/scale, validation, stable schema ID, `io` persistence, explicit read/write stage and event barrier.
3. Use integer/fixed authoritative state only. Do not write hidden mutable arrays or genre modes. Keep presentation decoupled.
4. Implement native consumers and verify test coverage for deterministic ordering, invalid indexes, boundaries, save/load, rollback/replay and different worker counts.
5. For anything seen in the editor, add generic component metadata and a minimal website demo with genuine frames when it becomes a usable capability.
6. Document actual verification; never claim the provisional pure-policy probes are shipped ECS features.
7. At the end of each stage, update the handoff (branch, SHA, changed files, API, tests run, limitations and exact next action). Respect the five-area WIP limit and main-merge gate.

Smoke test: `bash tests/libraries/world_processes_run.sh`.


## Stage 02: ECHO's first ECS integration
`libraries/include/aiverselib/world/EchoEcs.h` is the reusable, opt-in gameplay hearing pass. Call `aiverse::echo::install(sim)` once inside the native `GameModule::setup`; create `AcousticZone`, `AcousticPortal`, `AcousticEmitter`, `AcousticReceiver`, and `AcousticHearing` components on entities. Source, zone and portal references use generation-safe ECS `Entity` handles. The system publishes authoritative `AcousticHearing`; do not create a parallel AI perception cache. A host can instead call `propagate(world,tick)` explicitly at its read/write barrier.

The native test is `ecs_echo_acoustics_test` (source `tests/ecs_echo_acoustics_test.cpp`). It covers COW rollback, save/reload, deterministic graph tie ordering, stale refs and bounded oversized inputs, but **has not been executed or validated on the current branch yet**. Run on a real build before calling the feature verified. ECHO currently models authored zone portals, not full audio ray tracing or reverb. See `docs/handoffs/world-processes-stage02-echo-2026-11-23.md`.

## Stage 03: FRACTURE structural ECS

Public headers: `aiverselib/world/FractureEcs.h` (always) and `aiverselib/world/FracturePhysisBridge.h` (optional PHYSIS integration). Use the **existing** PHYSIS nature stress/outbox and host's Box3D/TerraWeave physics; never recalculate wind/fire/quake stress or spawn physics inside FRACTURE.

In `GameModule::setup`, call `fracture::install(sim)` after any PHYSIS installer. Place `fracture::Member` on each structural part Entity, `fracture::Bond` on each link Entity; attach `fracture::Collapse` to parts whose physical detachment needs durable dispatch. Provide one or more anchored members. A damage-capability/authorized command changes `Member.broken` or `Bond.damage`; `fracture::advance(world,tick)` marks unsupported members detached and emits one pending collapse transition. Call `fracture::pending(world)`, stage/deduplicate the actual gameplay/Box3D/TerraWeave effect **durably**, then `fracture::acknowledge(world,event)`. Only a trusted repair should call `rearmAfterRepair` after the original event is acknowledged, original tick has passed, and bonds/physics are repaired.

PHYSIS hookup: call `addPhysisFractureBridgeSystem(sim)` **after** `physis.nature_stress` and **before** registering `fracture.support`. It reads `physis::NatureStressOutputComponent` and latches Member.broken; PHYSIS remains the source of environmental fracture facts. Do not attach a duplicate Collapse outbox to the PHYSIS primary fall event's body; let the PHYSIS durable outbox handle that object's falling reaction, and use Collapse for downstream structural members. The host reconciles the single physical-effects ledger.

Native regression: `ecs_fracture_structure_test` from `tests/ecs_fracture_structure_test.cpp` (CTest), including support cascade, damaged bonds, PHYSIS snapshot bridge, event ack/rearm, COW replay, save/load and stale Entity generation. Newly added code has **not passed a full native build or CI acceptance gate** yet; never mark ready just from presence of tests. Stage handoff: `docs/handoffs/world-processes-stage03-fracture-2026-11-23.md`.

## Stage 04: AETHER gas diffusion (experimental ECS integration)

Public interfaces: `aiverselib/world/AetherEcs.h`, optional `aiverselib/world/AetherPhysisBridge.h`. To enable, call `aiverse::aether::install(sim)` during native `GameModule::setup`, then attach `GasCell` to ECS room/volume/cell entities, `GasDuct` to ventilation/link entities and optional `GasFlux` to sources or sinks. These are **gas species only**: PHYSIS retains weather, climate, wind and fire. Equal-volume cells use integer-quantized masses of `smoke`, `toxin`, `oxygen`; a duct implements concentration-driven diffusion with a maximum mass transfer per tick, not a full computational-fluid-dynamics solver.

`aether::advance(world,tick)` stages all three species and fluxes before committing. The policy enforces signed rate/mass bounds and stable entity sorting, and refuses mixed partially-processed tick states. Call once per authoritative tick. State persists through registered `aether.gas_cell`, `aether.duct`, `aether.flux` v1 schemas.

To connect PHYSIS fire, call `aether::addPhysisFireSystem(sim)` after PHYSIS environment and after gas transport, during setup, when PHYSIS `State` and `HazardCell` exist. Attach `FireCoupling` to PHYSIS cell entities also containing `GasCell`. `applyPhysisFire(world)` reads current normalized fire intensity and creates smoke/consumes oxygen once per PHYSIS climate tick. It **does not extinguish or modify PHYSIS fire**; combustion chemistry, smoke light scattering and physical wind advection are not implemented. `FireCoupling` is a persistent opt-in `aether.fire_coupling` v1 component; do not register twice.

Acceptance source `tests/ecs_aether_gas_test.cpp` / CTest `ecs_aether_gas_test`: conservation, deterministic cross-module hashes, invalid flux fail-closed, duplicate tick, stale generations, PHYSIS coupling, COW save/replay. Tests were authored, **not verified as compiled/passed** in this session. Handoff: `docs/handoffs/world-processes-stage04-aether-2026-11-23.md`. Follow the five-area WIP limit and do not enable 4th+ system before closing current ECHO/FRACTURE/AETHER gates.

## Stage 07: bounded Studio map authoring with real Undo/Redo

The approved Studio Scene Inspector uses `StudioSession::componentCatalog`,
`studio.componentAdd`, `studio.componentRemove`, and
`studio.setProperty` / `studio.setProperties`. MapDoc supplies
single-step Undo/Redo and project serialization. The new pure, validated
`libraries/include/aiverselib/world/WorldProcessesAuthoring.h` can author
only three self-contained components initially:

- `EchoZone`: `enabled` boolean.
- `FractureMember`: `anchored` boolean. Runtime `broken`/`detached`
  are simulation state and MUST NOT be editable as authoring data.
- `AetherGasCell`: integer initial `smoke`, `toxin`, `oxygen`
  (0..1,000,000,000), `enabled` boolean. `sampledTick` is a runtime value.

Editing requires `components[ComponentId]` in real map DocEntity extras,
and fails closed on wrong types, unknown keys, partial schemas, fractional
or out-of-range numeric values. `StudioSessionTests` tests add/edit,
batch rollback, Undo, Redo, save and reload.

**Critical:** Editor authoring DOES NOT automatically install
world-process ECS components into existing game modules. A native game's
entity-spawn step must map its stable authored document ID to a
generation-safe ECS Entity, then call
`lib::worldprocess::authoring::apply(world, entity, componentId, json)`
before registering/stepping the ECHO/FRACTURE/AETHER systems. This is
explicitly opt-in and returns false on invalid data or duplicate
components. The Live Inspector remains read-only. Do not claim
end-to-end gameplay authoring until a concrete game module consumes
these extras and a real scene verifies it.

Tests: `world_processes_authoring_test` in focused CI plus the
native `apps/studio-native/tests/StudioSessionTests.cpp` fixture
and CMake/Studio test targets. Source added does not prove CI green
until real logs are recorded. Follow handoff
`docs/handoffs/world-processes-stage07-authoring-2026-11-23.md`.

## Stage 08: real map-to-native ECS hydration

Use `aiverselib/world/WorldProcessesMapHydration.h`:
`authoring::hydrateMap(Simulation&, map, placedEntities)` inside an owning
native `GameModule::setup` **after map Entity creation** and before the
persistence registry seals. The `placedEntities` vector is the host's
exact index-order mapping of `map["entities"]` to actual generation-safe
ECS Entity handles. A mismatched count, dead/unspawned Entity, invalid
value or budget exceed fails before registration/mutation. Only categories
actually present are registered; a map without World Processes leaves the
legacy v9 schema alone.

For the existing GameCore host use the standard native module adapter
`aiverselib/world/WorldProcessesNativeModule.h`. The native module's
`setup(GameCore&,Simulation&,NativeState&,Json)` reads
`host.matchMap()` (post-generation map) and `host.mapPlaced()`.
It is invoked at the established `NativeModules::load` lifecycle. No
modification to engine/GameCore sources or second simulation is required.

**Ready-to-copy CMake/project template**:
`examples/native/world_processes/`. Register
`aiverse_native_module(ID world_processes VERSION 1 ENTRY
createWorldProcessesModule SOURCES WorldProcessesModule.cpp)`, link the
native OBJECT target to `AIVERSE::libraries`, and enable
`{"id":"world_processes"}` in the project `rules.json`
`nativeModules` list. This is explicitly opt-in, not automatic for
unmodified games. Built-in generic real-GameCore test:
`tests/world_processes_gamecore_native_test.cpp`; lightweight
two-GameModule map/save/rollback test:
`tests/world_processes_map_hydration_test.cpp`.
They are staged and **must be built/run before being reported green**.

Current automatic hydration supports only three standalone editor-authorable
types: EchoZone, FractureMember, AetherGasCell. Generational links to ECHO
Portal/Receiver/Emitter, FRACTURE Bond, AETHER Duct/Flux require a separate
validated map-ref resolution stage and a Studio picker. No numeric index
fallback. See
`docs/handoffs/world-processes-stage08-native-hydration-2026-11-23.md`.

## Stage 09: stable map-object connections (ECHO, FRACTURE, AETHER)

Shared C++ validation/adapter:
`aiverselib/world/WorldProcessesLinkedAuthoring.h`. Linked authored
components with stable **map document IDs**, NOT numeric `Entity.index`:
`EchoPortal(from,to,loss,bidirectional,enabled)`,
`EchoEmitter(zone,intensity,enabled)`,
`EchoReceiver(zone,threshold,enabled)`,
`FractureBond(first,second,strength,damage,enabled)`,
`AetherDuct(from,to,maximumPerTick,oneWay,enabled)`,
`AetherFlux(cell,smoke,toxin,oxygen,enabled)`.
New configs start disabled and unbound. For enabled links, all required
targets must exist; two-ended links need distinct IDs. Numeric ranges and
types are validated with exact integer parsing. Do not expose runtime-only
`EchoHearing`, `FractureCollapse` or PHYSIS transient state as authoring.

`WorldProcessesMapHydration.h` does a deterministic two-phase map
preflight, checks unique stable IDs, live generation-safe ECS handle
mapping and compatible endpoint components, then hydrates standalone
components FIRST, linked components SECOND. The source of truth is still
one World; mapping is a transient setup-time lookup, not serialized.
Authored EchoReceiver automatically gets its authoritative
`AcousticHearing` output component.

HTML5 Studio: existing Inspector `componentCatalog/add/remove/setProperty/
setProperties` accepts linked types. Reference fields expose native-backed
options and the shared `EntityReferencePicker` (searchable object list,
drag from Hierarchy via `application/x-aiverse-entity`,
stable `entity:@id` values only). Native validates target ID and
required component on each write. A multi-field link paste is staged
atomically into one MapDoc Undo step. Referenced target components cannot
be removed until incoming links are cleared. Entity *deletion* and map-ID
renames still require a complete graph-aware authoring cleanup workflow;
game setup will reject stale IDs rather than silently rebind.

Tests: `tests/world_processes_linked_test.cpp` and extended
`apps/studio-native/tests/StudioSessionTests.cpp`, plus CMake/CI.
Until latest CI/native Mac builds and screenshots pass, label this stage
**source implemented, not verified**. Handoff:
`docs/handoffs/world-processes-stage09-linked-authoring-2026-11-23.md`.

## Stages 11–16: complete genre-neutral ECS core and opt-in Studio authoring

All ten initially requested world-process mechanic areas now have authored
native ECS source code: ECHO, FRACTURE, AETHER, MORPH, CROWD, IMPRINT,
CIRCUIT, GRANULAR, EVOLVE and SOCIETY. In addition to the earlier three,
the seven opt-in headers and their persistent v1 schemas are:

- `MorphEcs.h`: Node/Spring/Force (deformation and shape-memory constraints).
- `CrowdEcs.h`: Agent/Slot/Preference (deterministic placement/avoidance).
- `ImprintEcs.h`: Cell/Link/Deposit (footprints, scent and trace diffusion).
- `CircuitEcs.h`: Node/Wire/Gate (generator/load routing, AND/OR/XOR/NOT).
- `GranularEcs.h`: Field/Source (bounded 2D granular settling).
- `EvolveEcs.h`: Region/Pressure (time-dependent world succession).
- `SocietyEcs.h`: Person/Contact/Message (trusted information spread).

Every system has a bounded, deterministic `advance(World&,tick)`,
`registerPersistence(Simulation&)` and `install(Simulation&)`. Ownership
stays with existing world/physics/economy/narrative systems; these modules
do not create a second rigid-body, NavMesh, terrain, water, fire or UI engine.

`WorldProcessesSuite.h` provides strict boolean flags to install any
combination through the existing `world_processes` native module. The
`WorldProcessesExtendedAuthoring.h` and `WorldProcessesExtendedLinks.h`
schemas cover all 19 component types from the seven areas, including
generation-safe references to real project map objects. Map setup uses
`WorldProcessesMapHydration.h` to validate/resolve stable IDs, create the
real ECS pools and register schemas, not a JSON mirror of runtime state.

Studio now supports Add Component, typed parameter bounds, the searchable
map object picker, drag/drop from Hierarchy, atomic field changes and
MapDoc Undo/Redo for the seven systems as well as the original three.
`WorldProcessesExtendedInspector.h` appends read-only current ECS values to
the existing WorldInspect/Live Inspector route.

`GranularDocument.h` supports up to 128x128 2D maps, per-cell mass and
obstacles, resizing and brush painting; the Scene Inspector mounts the
shared-theme `GranularGridEditor.tsx`. One completed user paint action
commits one validated full-grid JSON document through the existing
`studio.setProperty({componentId:"GranularField",key:"grid",value:JSON})`
RPC. Native `GranularDocument::apply` creates real ECS field cells.
The simple 1x1 `initialMass/capacity` form remains backward compatible.

Game/audio/AI/render consumers should use
`WorldProcessesQueries.h` (`morphPose/morphPoses`, `crowdPose`,
`imprintLevels`, `circuitState`, `granularCell`, `evolution`,
`societyPerson/knowsTopic`) to read the one authoritative ECS World.
These read-only projections are not secondary physics/NPC state.

A full native template fragment of stable IDs and authored component data
for all seven mechanisms is available in
`examples/native/world_processes/authoring-examples.json`; merge its
components into a *real* project's map entities with valid entity types,
owners and positions. The normal opt-in native project example and
`rules.json` flags are documented in its README.

**Test policy:** The user explicitly deferred testing for these stages.
Do not claim the new headers/Studio components compile or pass CTest,
sanitizers, Mac GUI or merged-tree `./run-tests.sh --quick`: they have
not been verified yet. See the complete stage handoffs in
`docs/handoffs/world-processes-stage11-*-` through Stage16.
Functional limitations: graphical soft-body mesh skinning, actual
Box3D impact/collapse, global RVO/NavMesh, full SPH granular physics,
real sound HRTF, terrain re-meshing, and game-specific NPC social
behavior remain consuming integration responsibilities, not promises
fulfilled by the ECS policy layer.


## Stages 17–19: actual MORPH + GRANULAR Box3D / RHI / TerraWeave host

For optional physical 3D scenes, DO NOT import physics or renderer into the
genre-neutral libraries header layer. Use the native project integration in
examples/world/world_processes_native/ instead:

- MorphModelStream.h: real per-instance ModelVertex GPU stream override;
  supplies weighted ECS MorphNode vertex displacements through the existing
  Renderer3D ModelDraw on Metal/Vulkan/GLES backends. Set Model::batching.allowGeometryPool=false **before** model upload and retain the immutable
  source vertex/index arrays. Never overwrite shared global GPU geometry.
  The separate instance stream owns its buffers and must retire them at a
  completed Device idle/lifetime fence.
- MorphBox3DContactBridge.h: authoritative Box3D Scene begin contacts and
  body velocities to quantized integer MorphContactInput records. Only the
  authoritative host samples float physics; replay clients use recorded
  integer inputs. MORPH now owns morph::Impact, persistent version-1
  morph.impact, applied **once** at the intended ECS tick. Do not reuse the
  continuous MorphForce component for one-shot contacts.
- GranularTerraWeaveBinding.h: sample canonical granular::Field + the
  existing TerraWeave EarthworksLayer ground height, bake granular slabs
  into render::MeshBuilder and the matching Box3D static top collision
  surface. Each optional native publisher has its own revision and resource
  lifetime; a failed collider creation retains the previous ground.
- GranularWorldIntegration.h: choose complete physics+visible 16x16 grid
  tiles, prepare immutable chunks on a completed ECS/soil revision,
  publish on the Box3D owner thread between Scene::step calls and the
  graphics owner thread outside rendered frames. Gate simulation/render
  when any batch patch rejects; individual tile replacement is safe but
  full multi-tile publication is not one atomic cross-device transaction.
- GranularTerraWeaveTransfer.h: real Dig/Fill via the canonical
  TerraWeave ECS excavateAuthoritativeEcs operation. A host explicitly
  defines one granular mass unit = one cubic centimetre for this transfer,
  preflights the actual EarthworksLayer::sculpt movedVolumeCm3, checks
  capacity/mass, then credits/debits the one authoritative granular pool.
  Ground renderer, collider and NavMesh remain TerraWeave owner outputs,
  never mutated by the gameplay policy itself.
- PhysicalSceneHost.h: convenient native C++ facade for MORPH instance
  streams, recorded contacts, GRANULAR frame prepare/publish/draw,
  rollback invalidation and safe shutdown.
- Full lifecycle integration source code and dependency boundaries:
  examples/world/world_processes_native/README.md.

The adapters are **opt-in to the concrete native project** and must be
linked only when that project already owns a renderer/gfx Device, a Box3D
Scene and a TerraWeave authoritative World. The minimal
examples/native/world_processes/ module remains independent from Box3D.
A MORPH visual 3D shape is not automatically an exact deformable Box3D
collision shape; the optional kinematic proxy stays coarse. GRANULAR is
a bounded 2D projected loose-solid layer, not SPH fluid, per-grain rigid
bodies or a second terrain engine. None of these are default engine
system replacements.

The user explicitly requested tests later. **No CMake/CTest/ASan, backend
GPU frame, actual Box3D scene or Mac screenshot has been verified** for
Stages17–19; do not claim green. Every further stage must add its own
docs/handoffs/ stage file with actual source SHA, design, tests (or
unverified status), limitations and next step. Follow:
- docs/handoffs/world-processes-stage17-morph-render-box3d-2026-11-23.md
- docs/handoffs/world-processes-stage18-granular-terra-box3d-2026-11-23.md
- docs/handoffs/world-processes-stage19-physical-native-host-2026-11-23.md

## Stages 20–21: CROWD NavMesh route + IMPRINT movement trackers

CROWD navigation:
`aiverselib/world/CrowdRoutes.h` adds persistent `crowd.route`
(`Route`, bounded quantized waypoints, cursor and navigation revision).
`crowd::assignRoute` receives an authoritative route, while
`crowd::advanceRoutes` selects the next waypoint ahead of the normal
`crowd::advance` local avoidance system. The optional native
`examples/world/world_processes_native/CrowdNavMeshBridge.h`
`planCrowdNavRoute` runs the EXISTING engine `nav::Mesh::findPath`
with real TerraWeave terrain heights, then converts Fixed metres into
integer centimetres once and stores the path in ECS. The NavMesh is
never cloned or updated by CROWD. A host re-plans following nav revisions.

IMPRINT trace generation:
`aiverselib/world/ImprintTrails.h` adds persistent
`imprint.tracker` (`Tracker`), authoritative `sampleMotion` and
`stampMotion`. A tracker records a source Entity and target
`ImprintCell` plus spacing, trace rates and last sampled positions.
`WorldProcessesSuite` runs `imprint.motion_trails` before IMPRINT
diffusion. If both CROWD and IMPRINT are enabled, the optional
`ImprintCrowdBridge.h` runs after CROWD movement and automatically
feeds CROWD agents to their trackers. Trackers whose source is a
non-CROWD Entity are left for their owning Box3D/ActorMotor game host
to call `imprint::sampleMotion` on the ECS tick barrier.

Studio Add Component supports `ImprintTracker`, with native-validated
stable map-ID source picker (ANY authored entity) and target cell picker
(only real `ImprintCell` components). Enabled link requires both IDs.
Normal MapDoc Undo/Redo, Save and reference-protected delete apply.
Live Inspector exposes read-only current `CrowdRoute` and
`ImprintTracker` ECS fields.

Actual feature commits/handoffs:
`docs/handoffs/world-processes-stage20-crowd-navmesh-imprint-movement-2026-11-23.md`
and
`docs/handoffs/world-processes-stage21-imprint-tracker-studio-2026-11-23.md`.

The user's test deferral is still active: NO current-stage compile,
CTest, sanitizer, Mac GUI, real NavMesh path or screenshot has been
verified. The previous Stage05 passes do not cover these changes.
