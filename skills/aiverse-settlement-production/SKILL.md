---
name: aiverse-settlement-production
description: Reuse AIVERSE settlement/production policies (colonist needs and mood, schedules, job preferences with deterministic assignment and stock reservations, production recipes and bills, hauling, construction and gathering orders, disaster alerts with response jobs, power/fluid graphs with brownout shedding, colony planner bot) when building native colony sims, city builders or factory/logistics games.
---

# settlement-production

Use when a native/host owner needs an authoritative colony layer: colonists whose food/rest/joy needs decay and
drive mood, hour-block schedules, work priorities 1..4 per work type, cooking/crafting bills at powered workstations,
hauling between stockpiles, build and gather orders, fire/flood/raid alerts that pull responders off their jobs,
power and water networks that shed low-priority consumers, and a bot that plans the colony with player commands.
Not a pathfinder, renderer, inventory UI or building placement system; instant factory links and offline
fast-forward stay with `production-networks` / `offline-production`. Body temperature, weather, animals and
renewable nodes are `survival-ecology`.

1. Read `docs/genre-packages.md` (settlement-production section) and the catalog entry. Headers:
   `genre/ColonyTypes.h` (definitions, state, commands, validation), `genre/ColonyRunner.h` (`stepColony`, setup
   helpers), `genre/ColonyPower.h` (`stepPower`), `genre/ColonyData.h` (JSON), `genre/ColonyConfigured.h`,
   `genre/ColonyPersistence.h`, umbrella `genre/Colony.h`; bot: `bots/ColonyPlanner.h`, `bots/ColonyPlannerData.h`.
2. Data: `readColonyDefinitions(json, colonyConfig(settings))` (needs with mood steps, restores, meals, work types,
   recipes, blueprints, gathers, hauls, alerts), `readPowerGrid(json)` (nodes producer/consumer/storage/conduit with
   a medium, links, `restoreDelayTicks`), `bots::readColonyPlanner(json, defs)`. Errors are path-qualified
   `JsonError`s (`colony.recipes[0].outputs[1][0]: duplicate id 1`, `colony.recipe 5.workType: unknown work type 9`).
   Scalars: optional generated `colony` settings section (`colonySettingsErrors` checks band order).
3. Per authoritative tick, in this order: `stepPower(grid, tick, {outputMilli from weather/fuel, switches})` ->
   `bots::planColony(...)` (optional) -> `stepColony(defs, colony, tick, input)` with `input.commands` (players and
   bot, ascending seq), `input.unpowered = power.unpowered`, `gatherAvailable` from renewal/ecology owners,
   `moodModifiers` (e.g. `survivalMood(body)`), `raise`/`clear` alerts (grid-field fire, flood, raid owners).
   Then route events: `Gathered` -> harvest renewal/ecology, `Built` -> register power nodes / buildings,
   `AlertWorked` -> extinguish/repair in the hazard owner. Strictly increasing ticks; nullopt = rejected input.
4. Jobs use `lib::claim` (Tasks.h) per preference tier, effective priority = base + active alert boosts, ties by
   job id; materials are reserved on claim (`systems::reserve`, ticket = job id) and committed on completion.
   Preempting alerts move eligible colonists onto response jobs; `ignoreSchedule` keeps responders awake.
5. Power: each connected component balances alone; storage covers shortages (rate-limited), consumers are kept in
   (priority desc, id asc) order with skip-fit, shed consumers wait `restoreDelayTicks` (hysteresis).
6. Persist `ColonyState` (104), `PowerGridState` (105), `bots::ColonyPlannerState` (110) with `genre::save/load`
   in dedicated NativeState keys; hash with `colonyHash`, `powerHash`, `bots::colonyPlannerHash`; bind
   `colonyDefsHash(defs)` in the save envelope. Kinds 104..111 are shared with survival-ecology.
7. Examples: `examples/libraries/genre/production-survival` (10 colonists, 20 days, CTest
   `library_production_survival_example`), `examples/native/libraries/ProductionSurvivalConsumer.cpp`.
   Run `tests/libraries/genre_production_survival_tests.cpp` (CTest `library_genre_production_survival`),
   `python3 tools/libraries/catalog.py check`, `generate_gameplay.py --check`.

Build selection: `AIVERSE::lib_settlement_production` (depends on tasks, reservations, graphs, configuration and
`aiverse_engine`); native OBJECT modules call `aiverse_native_use_libraries(target settlement-production)`.
