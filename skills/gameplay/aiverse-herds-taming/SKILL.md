---
name: aiverse-herds-taming
description: Herds with leader, cohesion, spreading panic and succession, off-screen aggregate herds, and taming by trust with follow/stay/guard orders.
---

# Herds and taming

Header: `<aiverselib/mechanics/HerdsTaming.h>`. Library catalog id `herds-taming`, CMake target `AIVERSE::lib_herds_taming`.
Capability area S2 in `docs/capability-status.json`. Tests: `tests/libraries/policy_mechanics_tests.cpp`.

Deterministic policy: Fixed/integer math, ascending ids, strictly increasing ticks; repeated or invalid input returns nullopt and leaves state untouched. Works in any project: the owner reports facts and applies outputs.

## Use

1. `addHerd(state, herd)` with member ids; each tick pass `MemberFact` for on-screen members and the ids of off-screen herds.
2. `stepHerds(config, tamingDefs, state, tick, facts, commands, offscreen)` returns desired directions/speeds; steer bodies with navmesh or flow fields.
3. Taming commands: Feed (preferred food gives more trust), Scare, Order. `tamedOwner(state, animal)` returns the owner.
4. Off-screen herds move their home and change size by daily birth/death rates.
5. Persist HerdState (kind 209). Single-animal decisions stay in EcologyAnimals.h.

## Limits

No breeding of tamed animals or saddles/riding.
