---
name: aiverse-production-networks
description: Integrate bounded integer buffers, atomic recipes and deterministic one-hop transport in custom native AIVERSE factory networks. Reuse built-in conveyors when their rules suffice.
---

# Buffered production

Read `libraries/include/aiverselib/strategy/Production.h` and
`docs/strategy-libraries.md`. Built-in RTS conveyors/crafters and resource ownership
remain authoritative for games already using them; don't mirror their stocks.

- Give nodes positive stable IDs and equally sized resource stock/capacity arrays.
  Recipes must be ordered by unique node ID; links by unique positive link ID.
  `compatible(rules,state)` checks shape, capacities, endpoints and overflows.
- One recipe runs per node/tick. Inputs are consumed atomically only when all
  outputs fit. Duplicate ingredient amounts are aggregated safely.
- After recipes, links transfer in stable ID order. They use source availability
  and destination capacity from the phase snapshot. Arrivals cannot leave again
  that tick, and departed capacity opens next tick. Preserve this rule in previews
  and offline models; link ordering is deterministic contention priority.
- `stepFactory` accepts exactly `state.tick+1`. This is discrete transport, not
  fluid pressure, electrical load flow, shortest-path dispatch or rail signaling.
- Link `AIVERSE::strategy` after `aiverse_native_module`. Persist FactoryState
  through `strategy/Persistence.h` in NativeState; version recipes/topology. Keep
  graph changes as validated Commands and check compatibility before committing.
- Follow `examples/native/strategy`. Run `strategy_libraries` and
  `python3 tests/libraries/strategy_native.py`. Verify blocked outputs consume
  nothing, fan-out conserves stock, chains cannot multi-hop, duplicate ticks do
  nothing and a fresh restore preserves future transfers.
