---
name: aiverse-transit-schedules
description: Public transport and freight lines with timetables, earliest-arrival route planning with transfers, boarding by capacity and stop congestion diagnostics.
---

# Transit schedules

Header: `<aiverselib/mechanics/TransitSchedules.h>`. Library catalog id `transit-schedules`, CMake target `AIVERSE::lib_transit_schedules`.
Capability area C4 in `docs/capability-status.json`. Tests: `tests/libraries/policy_mechanics_tests.cpp`.

Deterministic policy: Fixed/integer math, ascending ids, strictly increasing ticks; repeated or invalid input returns nullopt and leaves state untouched. Works in any project: the owner reports facts and applies outputs.

## Use

1. Lines: stops, travel ticks, first departure, headway, capacity. `planTransit(network, from, to, time)` returns legs with transfers.
2. Each tick `stepTransit(network, demand, state, tick)`: groups spawn at their origin, board in FIFO order up to capacity, transfer and arrive.
3. The report lists waiting units and the oldest wait per stop and the peak load of each line: use it for congestion overlays.
4. Persist TransitState (kind 215).

## Limits

Vehicles are timetable entities (the owner moves bodies along roads/rails); no fares.
