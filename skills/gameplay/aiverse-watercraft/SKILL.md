---
name: aiverse-watercraft
description: Boats and ships from assembled hull blocks with point buoyancy, water drag, thrust and rudder, capsize and sinking, plus an aircraft model with lift, drag, stall and control surfaces.
---

# Boats, ships and aircraft

Header: `<aiverselib/mechanics/Watercraft.h>`. Library catalog id `watercraft`, CMake target `AIVERSE::lib_watercraft`.
Capability area V2 in `docs/capability-status.json`. Tests: `tests/libraries/policy_mechanics_tests.cpp`.

Deterministic policy: Fixed/integer math, ascending ids, strictly increasing ticks; repeated or invalid input returns nullopt and leaves state untouched. Works in any project: the owner reports facts and applies outputs.

## Use

1. `assembleHull(blocks)` builds mass, centre of mass and buoyancy points; reassemble after a block is destroyed.
2. Each tick `stepBoat(config, hull, state, input, tick, dt, water)`; `water(point)` returns the NEREID surface height and current.
3. `stepAircraft(config, state, input, tick, dt, wind)` for planes; arcade aircraft stay in vehicle-controllers.
4. Seats and passengers use VehicleSeats.h; persist BoatState (kind 220) and AircraftState (kind 221).

## Limits

Simplified rigid model (no wave slamming, no propeller wash); multi-seat network play is not verified here.
