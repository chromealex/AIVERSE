# AIVERSE World Processes — Stage 22: CIRCUIT switches and actuated devices

Date: 2026-11-23. Branch: `feature/world-processes-experimental`, draft PR #112. Main unchanged.
User requested features first, tests later.

## Implementation and API

- `libraries/include/aiverselib/world/CircuitDevices.h` commit `ab18320a0fe45f9fa25fd669f6c09eb78c183025`: real persistent `circuit::Switch` (source CIRCUIT Node, closed, enabled) and `circuit::Device` (generation-safe Node, kind enum Generic/Light/Door/Motor/Pump/Alarm, requireSignal, active, valid, sampledTick, changedTick, monotonic transition). `setSwitch(World&,Entity,bool)` validates the authoritative switch and its Node; `synchronizeSwitches(World&)` applies only uniquely owned switch drives before graph propagation. `updateDevices(World&,tick)` consumes same-tick computed Node.powered/signal and writes stable device outputs/transition counter; the game and renderer do not own a duplicate logical state. `registerDevicePersistence` adds schemas `circuit.switch` and `circuit.device`, v1; caps 8192 switches/devices. Monotonic transition + tick permits deterministic external actuation and rollback dedupe.
- `WorldProcessesSuite.h` commit `5fddad8ad9e9edf8f463a4f13bd71e919af7a714`: fixed phase order: `circuit.switch_inputs` -> `circuit.power_logic` -> `circuit.device_outputs`; one opt-in CIRCUIT flag registers persistence and systems.
- `WorldProcessesExtendedLinks.h` commit `6d85c3224cbb766b11260a9f2873cc7950f604ea`: stable map-ID schema/defaults/validation/real ECS spawn for `CircuitSwitch{node,closed,enabled}` and `CircuitDevice{node,kind,requireSignal,enabled}`; enabled targets must reference an authored `CircuitNode`. Runtime outputs are not authorable.
- `WorldProcessesMapHydration.h` commit `4645e23826dbafba5080e779f014c0d0a32064bb`: extends linked config preflight to 15 categories and enforces switch/device budgets; actual ECS Entity references resolved by generation at map setup.
- `apps/studio-native/StudioSession.cpp` commit `93bbf50227b9ff51322fde1caec47a95cc42f00e`: Add Component, typed Inspector fields, stable map-object picker, batch Undo/Redo and reference removal protection for the two new types.
- `WorldProcessesExtendedInspector.h` commit `e30d04017a9b4378b73d49a6111dd20d316b6342`: real read-only Studio Live Inspector for switch and device output state.
- `examples/world/world_processes_native/CircuitDeviceHost.h` commit `5ddafe08342606ea117fb133c3a185e9615c40cc`: optional native presentation/action adapter consuming actual `circuit::Device` transitions. It calls existing Box3D `physics::Scene::moveKinematic` on a user-supplied bound door body (only Kinematic), and a host callback for light intensity/VFX, motor operation, alarms and pump/water systems. The host supplies genuine device integrations; this header never fabricates a renderer light, destroys other physics state or changes canonical ECS values. `resetAfterRollback()` drops transient acknowledgement data so restored states can be reapplied.

## Architecture, limits, testing

CIRCUIT is a deterministic ECS logic/power authority, not a replacement for the game economy, PHYSIS or Box3D. The host provides generator supply and handles resulting game actions after the device output phase. A door has a coarse kinematic target and must be registered via a real `BodyHandle`; collision shape/door animation and movement speed are still governed by the physics/animation owner. Light and pump callbacks intentionally depend on project-specific existing APIs, not a guessed engine method.

**Tests deliberately not run per user.** Source committed but no CMake/GCC/Clang/ASan, macOS Studio, Box3D interaction, shader backend, save/replay or full merged-tree acceptance has passed for this stage. Previous historical CI is not proof.

## Next

Continue EVOLVE→PHYSIS/TerraWeave environmental driver and SOCIETY→NPC event/knowledge bridge. Keep both genre neutral, with stable Entity refs and opt-in host ownership. After the final functionality stage run consolidated validation (when user authorizes). Update per-stage handoffs and keep PR draft.
