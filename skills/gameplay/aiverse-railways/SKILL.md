---
name: aiverse-railways
description: Trains on a track graph with block signalling, acceleration and braking, looping routes and station stops with dwell times.
---

# Railways and block signals

Header: `<aiverselib/mechanics/Railways.h>`. Library catalog id `railways`, CMake target `AIVERSE::lib_railways`.
Capability area C2 in `docs/capability-status.json`. Tests: `tests/libraries/policy_mechanics_tests.cpp`.

Deterministic policy: Fixed/integer math, ascending ids, strictly increasing ticks; repeated or invalid input returns nullopt and leaves state untouched. Works in any project: the owner reports facts and applies outputs.

## Use

1. Segments carry length, speed limit, block and station. `placeTrains(network, trains)` reserves starting blocks.
2. Each tick `stepRail(network, trains, state, tick, dt)`; trains enter the next block only when it is free (lowest id first), brake in time and wait at the signal.
3. Arrived/Departed events at stations are where the owner loads and unloads cargo.
4. Persist RailState (kind 213).

## Limits

No junction pathfinding (routes are explicit), no chain signals, no train length.
