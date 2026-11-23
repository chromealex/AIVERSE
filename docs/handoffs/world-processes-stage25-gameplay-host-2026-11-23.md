# AIVERSE World Processes — Stage 25: unified gameplay host (CIRCUIT + EVOLVE + SOCIETY)

Date: 2026-11-23. Repo `chromealex/AIVERSE`.
Branch `feature/world-processes-experimental`; PR #112 draft. Main unchanged.
User direction: implement remaining functionality before running tests.

## Delivered functionality

**Generic opt-in facade:** `examples/world/world_processes_native/WorldProcessGameplayHost.h`, commit `015071873cb8a85ab44a3074a2e1446c028d8d49`.

`WorldProcessGameplayHost` provides typed access to the real previously implemented native adapters:
- `circuitDevices().bindDoor(Entity,CircuitDoorBinding)` binds an existing `physics::Scene` kinematic door body to a canonical CIRCUIT Device. `setCallback` attaches existing project light/FX, motor, pump or alarm handlers.
- `setSwitch(World&,Entity,bool)` submits a validated authoritative input to an existing CIRCUIT Switch; its ECS systems run ordered switch inputs → power graph → device state. Neither Box3D nor graphics writes power logic.
- `npcReactions().setDispatch` accepts a native game AI/dialogue/quest action handler. The SOCIETY NPC Trigger supplies generation-safe person/trigger, required topic mask, actionId, completed tick and monotonic transition number. The project host is responsible for deduplication of irreversible game actions.
- `attachClimate(World&,producer,region,physisCell,ClimateLink)` binds the real PHYSIS ECS `Atmosphere/Surface` source to an EVOLVE Region via the already-defined `EvolveClimateLink` and persistent `EvolvePressure`. No second weather system.
- `forestClimate(World&,zones,existingBaseline)` wraps the **existing TerraWeave** `ForestEcologyClimateSampler` with region fertility/succession/pollution limits; the baseline from PHYSIS/NEREID/canopy is respected and not increased.
- `reconcile(World&,completedTick,physicsScene,force)` applies completed ECS CIRCUIT device outputs and dispatches SOCIETY NPC event transitions at the authoritative/presentation barrier.
- `resetAfterRollback()` invalidates only transient external event acknowledgements, then a restored ECS World can be reconciled. This is not another gameplay save authority.

**Concrete native usage guide:** `examples/world/world_processes_native/README.md`, commit `a44b3d53325320a4060a39b923c3dde62f7000a9`. Includes host code demonstrating Box3D kinematic door, project callbacks, actor switch interaction, optional PHYSIS ClimateLink, real TerraWeave forest climate callback, post-tick event dispatch and rollback recovery.

**Map content fragments:** `examples/native/world_processes/authoring-examples.json`, commit `ae630428f352b3b892340675da442249d3675a57`, adds authorable switch, powered door node/wire/device, regional influence and knowledge-triggered NPC action. These are JSON component examples, not a complete playable game map (game entity type/position/owner are supplied by existing project). The state/control is genre independent.

## Architecture and API references

- Stage22 `CircuitDevices.h`: actual persistent Switch/Device state, transition clocks; native `CircuitDeviceHost.h` moves existing Box3D kinematic doors and calls project device handlers.
- Stage23 `EvolvePhysisBridge.h`, `EvolvePhysisHost.h`, `EvolveTerraEcologyBridge.h`: authoritative PHYSIS → EVOLVE input and EVOLVE → existing TerraWeave forest sampler.
- Stage24 `SocietyNpcTriggers.h`, `SocietyNpcHost.h`: knowledge-mask reactions and project NPC/quest event dispatch, Stage24 Editor/MapDoc authoring for `SocietyNpcTrigger`.
- Canonical ECS `World` and `Simulation` remain the sole authoritative state. Neither renderer, Box3D nor editor authoring panels own a competing simulation.

## Tests and completion honesty

**No tests run per user's request.** No compile/CTest, sanitizer, native Mac app, actual Box3D door, forest evolution, NPC quest action, GPU screenshot or full merged-tree acceptance was performed for Stage22–25. Source edits are committed only. The project callbacks and host variables are supplied by an actual game; the native facade cannot invent a complete NPC, light system, or project-specific game mode.

Potential follow-up risks requiring a later QA phase:
- Real bridge timing versus a concrete `NativeModule::tick`, game render and network barrier; command/replay identity; duplicate action handling after rollback.
- EVOLVE climate links must attach only after actual PHYSIS entities spawn; no authored weather cell ID picker/automatic biome binding yet.
- Box3D requires actual kinematic door handles and owner-thread calls. CIRCUIT Device is a logical state, not automatic art animation.
- SOCIETY triggers are deterministic knowledge predicates, not a complete autonomous social reasoning/dialogue system.
- Need full Mac CMake, browser and RHI validation before PR merge.

## Next exact step

Functional additions worth doing before user-authorized tests:
1. Add Studio gizmos/3D preview for CIRCUIT edges/device nodes, MORPH deformable bounds, GRANULAR fields and EVOLVE region bounds, reusing the current Studio `studio-html5` design.
2. Integrate a real demo project using normal game entity types (not just JSON fragments), native callbacks for one Box3D door and NPC action.
3. Complete standardized host-to-game event payloads for IMPRINT visual decals and CROWD navigation display. Avoid replacing other system owners.
4. Only after functionality is complete, run consolidated CMake/CTest and Mac `./run-tests.sh --quick`; fix actual errors; reconcile `main` and `studio-html5` without overwriting parallel work.

Other handoffs: Stages22–24 in `docs/handoffs/world-processes-stage22-circuit-devices-2026-11-23.md`, `stage23-evolve-physis-terra-2026-11-23.md`, `stage24-society-npc-actions-2026-11-23.md`.
