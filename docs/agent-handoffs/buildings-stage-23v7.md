# Stage 23V.7 — executable native building reference host
Parent: 00397988e2935edb3479f58a22079b53e0dc9de3. Target: feature/aiv20261123_building-grammar / PR #114.

## Implemented
- Real native GameModule + Simulation in examples/world/runtime-building/main.cpp with authored vertical-circulation and independent second building. Uses existing SpatialWorld, namespace catalog, native regional nav meshes, shared ECS Transform and Owner, actual generic net::replica Server/Client over loopback (admitted peer slot).
- CMake executable aiverse_building_world_example, CTest building_native_world_replica, focused compiler check. Authored assertions cover unauthorized actor rejection, server peer slot stamping, lift boarding, rider marker, committed snapshot unicast, graph ACK stalls, mid-trip native serialized checkpoint, stale epoch rejection, exit, unload/reload and namespace release.
- Parent 23V.4–23V.6 added complete managed circulation composition, lift PBR renderer model lifecycle, persistent lift state and generic Replica bridge.
- Skill and native example README updated. No legacy FPS/RTS source changes.

## Actual evidence
The exact parent APIs were inspected and the source/CI changes created. No native source checkout is mounted; the connector creates commits but does not execute CMake. The previous branch HEAD's Actions were queued. Do NOT claim compilation, tests, real GPU capture or successful multiplayer execution before inspecting new exact-head CI results.

## Remaining acceptance
Run the new native executable plus focused C++/GLB/PBR, native Studio and platform GPU acceptance. Fix observed compiler/runtime errors. Reference host is an authoritative server/local checkpoint path; deterministic lockstep resimulation is not supported by this contract. Real game locomotion/PHYSIS integration and remote-client visual application remain project-owned; the common runtime and transport bridge are available without genre modules. Keep draft PR until verified and reconciled with main.
