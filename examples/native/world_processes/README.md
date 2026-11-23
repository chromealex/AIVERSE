# AIVERSE world_processes native module

This is a real, opt-in native C++ module for ordinary GameCore map games.
No engine fork or genre-specific code is required.

1. Copy `CMakeLists.txt` and `WorldProcessesModule.cpp` into your project's `native/` directory. If it already has `native/CMakeLists.txt`, add the two CMake lines instead of replacing the file.
2. Add to the project's `rules.json`:
   ```json
   {"nativeModules":[{"id":"world_processes"}]}
   ```
   Preserve any other entries already in `nativeModules`.
3. In HTML5 Studio Scene Inspector, select an ordinary authored map entity, click Add Component and choose EchoZone, FractureMember or AetherGasCell. Edit enabled/anchored/initial gas amounts, Save and Undo/Redo as usual.
4. Build the game/editor with the project's native C++ sources. The module hydrates `MapDoc` components onto the *actual* ECS entities in `GameCore::mapPlaced`, registers only the required opt-in persistent schemas, and runs shared genre-neutral systems.

The module is **not linked automatically into all existing games**; NativeModule compilation and a rules.json opt-in remain required. Other GameModule hosts can call `worldprocess::authoring::hydrateMap(sim,map,placed)` in their own setup. Explicit suite flags are optional; authored component categories enable their systems automatically.

Originally this module handled three standalone types. It now hydrates all ten systems, including authored ECHO portals, FRACTURE bonds, AETHER ducts, and the additional seven mechanics with their stable-ID graph links. Actual debris bodies, advanced audio/VFX presentation, host-specific producer events and physics coupling remain separate integration work. PHYSIS remains responsible for weather and primary natural fracture events.

A map with no supported components leaves the native ECS schema manifest unchanged. Invalid config, count mismatches, or an authored component attached to an entity that was not actually spawned fail with an explicit setup error.

Tests: `tests/world_processes_map_hydration_test.cpp` and `tests/world_processes_gamecore_native_test.cpp` (real native GameCore). Follow `docs/handoffs/world-processes-stage08-native-hydration-2026-11-23.md` for current verification.


## Seven additional world-process systems

The same opt-in module now recognizes the following ECS mechanisms on
map entities through the ordinary Studio **Add Component** menu:
MORPH (`MorphNode/Spring/Force`), CROWD (`CrowdAgent/Slot/Preference`),
IMPRINT (`ImprintCell/Link/Deposit`), CIRCUIT
(`CircuitNode/Wire/Gate`), GRANULAR (`GranularField/Source`),
EVOLVE (`EvolveRegion/Pressure`) and SOCIETY
(`SocietyPerson/Contact/Message`). Link fields use stable authored map
IDs resolved to live generation-safe ECS handles during setup.
These mechanisms are genre-independent and do not replace Box3D,
navigation, PHYSIS, TerraWeave or a project's narrative/economy owners.

A project may also pre-enable systems in its existing `world_processes`
native module configuration (booleans, all optional):

```json
{
  "nativeModules": [{
    "id": "world_processes",
    "config": {
      "morph": true,
      "crowd": true,
      "imprint": true,
      "circuit": true,
      "granular": true,
      "evolve": true,
      "society": true
    }
  }]
}
```

The example `authoring-examples.json` illustrates the format of
`entities[].components` data to merge into a regular project's existing
map; it is *not* a complete standalone playable map. Supply ordinary
map entity type/owner/position according to the game's rules.
The current GRANULAR grid supports up to 128×128 authored cells via the
`GranularGridEditor` in Scene Inspector; each brush or resize operation
is an Undo step and does not touch the running World.

Presentation queries are exported in
`aiverselib/world/WorldProcessesQueries.h`: `morphPoses`,
`crowdPose`, `imprintLevels`, `circuitState`,
`granularCell`, `evolution`, `societyPerson` and `knowsTopic`.
These return read-only views of canonical ECS components; visual mesh
skinning, physics bodies, terrain edits and NPC script integration stay
with their existing host owners.

**Status:** The seven new systems and Studio integration are source
implementations awaiting compilation and validation; tests were
deliberately postponed. Refer to the Stage11–16 handoffs for the
actual completion boundaries.
