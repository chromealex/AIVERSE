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

The module is **not linked automatically into all existing games**; NativeModule compilation and a rules.json opt-in remain required. Other GameModule hosts can call `worldprocess::authoring::hydrateMap(sim,map,placed)` in their own setup.

Only these three self-contained component types currently hydrate automatically. ECHO portals, FRACTURE bonds, AETHER ducts, source/receiver references and actual debris/audio/VFX presentation are later authoring/integration stages. PHYSIS remains responsible for weather and primary natural fracture events.

A map with no supported components leaves the native ECS schema manifest unchanged. Invalid config, count mismatches, or an authored component attached to an entity that was not actually spawned fail with an explicit setup error.

Tests: `tests/world_processes_map_hydration_test.cpp` and `tests/world_processes_gamecore_native_test.cpp` (real native GameCore). Follow `docs/handoffs/world-processes-stage08-native-hydration-2026-11-23.md` for current verification.
