---
name: aiverse-city-policy
description: City economy and politics: zoned lots that grow by demand, residents and jobs, taxes, service coverage, budget with loans, policies, faction approval and elections.
---

# City economy and politics

Header: `<aiverselib/mechanics/CityPolicy.h>`. Library catalog id `city-policy`, CMake target `AIVERSE::lib_city_policy`.
Capability area C3 in `docs/capability-status.json`. Tests: `tests/libraries/policy_mechanics_tests.cpp`.

Deterministic policy: Fixed/integer math, ascending ids, strictly increasing ticks; repeated or invalid input returns nullopt and leaves state untouched. Works in any project: the owner reports facts and applies outputs.

## Use

1. Zone lots with commands (`CityCommand::Zone` with a position); each tick `stepCity(config, data, state, tick, commands)`.
2. Demand per zone comes from jobs, workers and taxes; lots grow or shrink one level per review day; services cover residents within their radius.
3. Daily budget: taxes in, service and policy upkeep and loan interest out; `bankrupt` when the balance stays negative with a loan.
4. Factions rate service coverage, taxes and policies; elections every `electionDays` return the winner.
5. Persist CityState (kind 214). Traffic stays in city-traffic, transit in transit-schedules.

## Limits

Aggregated lots (no individual citizens); land value is data the owner sets.
