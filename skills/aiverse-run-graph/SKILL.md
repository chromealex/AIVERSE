---
name: aiverse-run-graph
description: Generate connected seeded AIVERSE roguelike run graphs with per-layer encounter budgets and persistent reward choices from native game modules. Use for run lifecycle and reward idempotency.
---

# Seeded runs

Read `libraries/include/aiverselib/strategy/RunGraph.h`. Reuse its catalog BFS and
weighted selection instead of generating another graph/loot algorithm in a game.

- Configure 2..64 layers, 1..16 rooms each, one start and one final, positive weighted
  encounter IDs/costs, per-layer budgets and 1..16 distinct reward choices.
  Every room is reachable from the start and can reach the final. Allocation reserves
  the cheapest encounter cost for remaining rooms, so a valid budget cannot dead-end.
- Supply `draw(total)` from `sim.rng().below(uint32_t(total))+1`, in [1,total].
  All configured totals are bounded below uint32. Validate before drawing. A bad callback
  can consume caller RNG before failure; rollback RNG together with the host transaction.
- `generateRun` creates the complete saved graph and offers. Do not regenerate on load
  or roll offers again when reopening a reward screen. Immutable config revisions still
  belong in NativeModule VERSION/config and the persistence envelope.
- `finishRunRoom` takes an authoritative encounter result, not an untrusted win button.
  A win enters Reward; a loss enters Failed. `claimRunReward` only accepts a saved offer.
  Its callback must atomically grant through the existing inventory owner and return false
  without changes on failure. Commit run state and the inventory in one host transaction.
  Do not make asynchronous HTTP calls from this simulation callback.
- A successful claim records the room/reward and enables `travelRun` only along outgoing
  edges. The final claim completes the run. Repeated requests cannot call grant again.
  Sequence numbers are monotonic and owned by the command authority.
- Persist the entire RunState with `strategy/Persistence.h` in NativeState. Link
  `AIVERSE::lib_run_graph`. `examples/native/strategy` VERSION 3 uses commands 10/11/12
  to demonstrate finish/claim/travel; its win command and reward counter are test fixtures,
  not production combat validation or a parallel wallet design.
- Run strategy tests (200 seeds, budgets, reachability, failed grants, duplicate claims)
  and `python3 tests/libraries/strategy_native.py` (save during reward selection, restore,
  finish a seeded run; repeat from relocated source exports).

This does not generate room geometry, play encounters, implement relic/card effects,
adapt a difficulty director, render a route UI or deploy durable backend rewards.

Build selection: `AIVERSE::lib_run_graph`; native OBJECT modules call
`aiverse_native_use_libraries(target run-graph)`. Only this header library and its
declared dependencies are selected. See `skills/catalog.json` for dependency and responsibility routing.
