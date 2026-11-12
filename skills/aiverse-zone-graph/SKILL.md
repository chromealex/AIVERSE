---
name: aiverse-zone-graph
description: Build and validate AIVERSE ability-gated zone graphs, visited maps and checkpoints from native game modules. Use for metroidvania reachability and softlock proofs.
---

# Zone graph

Read `libraries/include/aiverselib/strategy/ZoneGraph.h` and `docs/strategy-libraries.md`.
Use dense zero-based zone IDs, permanent uint32 ability masks and explicit directed doors.
A reverse door is never inferred. Start in a declared checkpoint.

- `beginZones` collects the start grants. `moveZone` checks the door mask and monotonic
  command sequence, then collects target grants and records visits/checkpoint.
  The host must validate player ownership, physical doorway contact and transition readiness.
- `respawnZone` keeps abilities/visits and returns to the last checkpoint. Final zones
  terminate the graph. This policy does not stream regions or restore character health.
- `proveZones` explores zone, ability mask and checkpoint together, then uses the catalog
  BFS on reverse edges to prove every reachable state can reach a final. `Softlock`
  includes a witness path. `NoFinal` means no final is reachable. `BudgetExceeded`
  is not a proof. Run after changing any grant, door or checkpoint.
- Set the proof's respawn flag only when the game always permits that command. Otherwise
  a proof can hide a one-way trap. Locks with consumable keys, ability loss or temporary
  switches require a different state model; do not represent those as permanent bits.
- Link `AIVERSE::lib_zone_graph` after `aiverse_native_module`. Persist `ZoneState` through
  `strategy/Persistence.h` in NativeState. Bind definition revision/module VERSION;
  check `compatible(graph,state)` after restore. Mutable state never lives on the module.
- Follow `examples/native/strategy` (commands 6/7/8). Run strategy unit tests and
  `python3 tests/libraries/strategy_native.py`: save after an unlock, restore, respawn,
  then enter the final. Test one-way traps with and without automatic checkpoint changes.

Physical door interactions, regional loading, map UI and checkpoints for other subsystems
remain host integration. This proof covers the declared graph only, not level geometry.

Build selection: `AIVERSE::lib_zone_graph`; native OBJECT modules call
`aiverse_native_use_libraries(target zone-graph)`. Only this header library and its
declared dependencies are selected. See `skills/catalog.json` for dependency and responsibility routing.
