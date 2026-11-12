---
name: aiverse-turn-scheduler
description: Integrate AIVERSE initiative, action points and nested reaction scheduling into a native C++ game. Use for custom turn rules beyond the built-in RTS turns mode.
---

# Turn scheduling

Read `libraries/include/aiverselib/strategy/Turns.h` and
`docs/strategy-libraries.md`. Prefer the built-in `turns` block when alternating
players and movement/attack budgets suffice.

- Construct `TurnActor` records with positive stable IDs, owner, initiative and AP.
  `beginTurns` sorts descending initiative, then ascending ID. The order is fixed
  for that state; do not promise dynamic initiative or simultaneous turns.
- Route input through Commands. `queueAction` validates actor ownership, AP and
  scheduling, but its authorization callback must check the actual kind, target,
  range and reaction window. Use an authoritative monotonic sequence.
- AP is paid on acceptance. `resolveAction` pops the most recent reaction first;
  it returns data and does not deal damage. Apply results through the game's
  existing authoritative owner. Fizzling/cancelling does not refund AP.
- Do not remove actors referenced by pending actions. Mark life state and let
  the owner handle target invalidation, round progression and game over.
- Register with `aiverse_native_module(...)`, then link
  `AIVERSE::lib_turn_scheduler`. Persist `TurnState` using `strategy/Persistence.h`
  `storeState/restoreState` with a definition revision in NativeState.
  Mutable state must not live on NativeModule instances.
- Follow `examples/native/strategy`. Run `strategy_libraries` and
  `python3 tests/libraries/strategy_native.py`; verify a save with nested pending
  actions, no double AP charge on repeated commands, and a rejected opponent order.

Tactical grids, legal-action generation, reaction UI and tactical AI remain
separate integrations. Do not describe this scheduler as full XCOM/D&D rules.

Build selection: `AIVERSE::lib_turn_scheduler`; native OBJECT modules call
`aiverse_native_use_libraries(target turn-scheduler)`. Only this header library and its
declared dependencies are selected. See `skills/catalog.json` for dependency and responsibility routing.
