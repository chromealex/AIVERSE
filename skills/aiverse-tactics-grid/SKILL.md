---
name: aiverse-tactics-grid
description: Reuse AIVERSE turn-strategy policies (square/hex grids with weighted movement and deterministic reachable sets, half/full cover and flank angle, line-of-fire previews with integer hit chance, action points, initiative with speed modifiers and delay, overwatch reaction fire, WeGo simultaneous resolution, per-team visibility) when building native tactical RPG, XCOM-like or turn-based strategy games.
---

# tactics-grid

Use when a native/host owner runs tactical turns on a grid: move previews and paths, cover/flanking, shot previews,
AP spending, initiative order, overwatch, simultaneous turns. Prefer the built-in RTS `turns` block for plain
alternating turns; use `turn-scheduler` (`strategy/Turns.h`) when you only need a fixed initiative order with
nested LIFO reactions and host-applied effects. This package owns its own dynamic order (speed modifiers, delay).
Not a navmesh, campaign layer, animation or UI.

1. Read `docs/genre-packages.md` (Cards and turn strategy section) and `genre/TacticsGrid.h`, `genre/TacticsTurns.h`,
   `genre/TacticsWeGo.h`.
2. Data: `readTacticsScenario(json, tacticsRulesFromSettings(settings))` (`genre/TacticsData.h`, example
   `examples/libraries/genre/cards-turns/skirmish.json`): `grid` (`square`|`hex`, ASCII rows, optional legend: cost,
   cover none/half/full, blocksSight), `rules`, `unitTypes`, `units`. Errors: `tactics.grid.rows[1][3]: ...`.
   Optional generated `tactics` settings section (cover/flank/range/overwatch modifiers, round limit, bot budget).
3. Geometry: `tacticsReachable(grid, start, budget, occupancy)` (Dijkstra, ties by (cost, cell) so paths never
   differ between platforms; square diagonals cost 3 half-units, no corner cutting), `tacticsPath`,
   `tacticsLine`, `tacticsCoverAgainst(grid, target, attacker, coverCosMilli)`, `tacticsPreviewShot` (hit chance
   milli = aim - cover + flank - range penalty - overwatch penalty, clamped; 0 when blocked/out of range).
4. Initiative turns: `tacticsBegin(scenario, units)`; per command `stepTactics(scenario, state, {seq, team, unit,
   action}, sim.rng())` for the active unit only (`tacticsActiveUnit`, `tacticsLegalActions`). Actions: Move (AP =
   ceil(path cost / mobility)), Attack (one RNG draw; ends the turn by default), Overwatch (reaction shot at the
   first enemy moving through its line of fire, unit-id order, once), Delay (re-queue at round end once), EndTurn.
   Order per round: initiative + `tacticsSetSpeedModifier` deltas, ties by ascending id.
5. WeGo: `tacticsSubmitOrders` per team (validated: own living unit, move within 1 AP, visible target, overwatch
   alone), then `tacticsResolveWeGo(scenario, state, orders, seq, rng)` once all living teams submitted: overwatch,
   lockstep movement (contested cell -> higher initiative then lower id; swaps and blocked moves stop), simultaneous
   fire (damage lands together), cleanup. Do not mix with stepTactics in one round.
6. Visibility: send each team `tacticsViewFor(scenario, state, team)` and `tacticsEventsFor(view, events)` only.
7. Persist `TacticsState` (record kind 98) and pending `TacticsOrders` (99) via `genre::save/load`
   (`genre/TacticsPersistence.h`); hash with `tacticsHash`, bind `tacticsScenarioHash`.
8. Example: `examples/native/libraries/CardsTurnsConsumer.cpp`, executable skirmish in
   `examples/libraries/genre/cards-turns/main.cpp`; tests `tests/libraries/genre_cards_turns_tests.cpp`.

Build selection: `AIVERSE::lib_tactics_grid` (depends on `aiverse_engine`); native OBJECT modules call
`aiverse_native_use_libraries(target tactics-grid)`. Bots: `aiverse-cards-turns-bots`.
