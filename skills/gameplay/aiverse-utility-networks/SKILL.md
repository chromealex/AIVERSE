---
name: aiverse-utility-networks
description: Pressure-driven pipe networks with sources, sinks, tanks, pumps and valves, and heat exchange between rooms with doors, outdoor boundary and thermostats.
---

# Pipes with pressure and heat

Header: `<aiverselib/mechanics/UtilityNetworks.h>`. Library catalog id `utility-networks`, CMake target `AIVERSE::lib_utility_networks`.
Capability area C2 in `docs/capability-status.json`. Tests: `tests/libraries/policy_mechanics_tests.cpp`.

Deterministic policy: Fixed/integer math, ascending ids, strictly increasing ticks; repeated or invalid input returns nullopt and leaves state untouched. Works in any project: the owner reports facts and applies outputs.

## Use

1. Fluid: `FluidNetwork` (nodes, pipes); each tick `stepFluid(network, state, tick)`; valves in `state.valves`; report has flow per pipe, delivered/shortfall per sink, spill.
2. Heat: `HeatNetwork` (rooms, links, devices); each tick `stepHeat(network, state, tick, outdoorMilliDegrees)`; doors in `state.doorOpen`, devices in `state.deviceOn`.
3. Electricity stays in ColonyPower.h; item transport in production-networks.
4. Persist FluidState (kind 211) and HeatState (kind 212).

## Limits

Pressure is a relaxed approximation, not a hydraulic solver; no gas mixing.
