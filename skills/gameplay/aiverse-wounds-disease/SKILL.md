---
name: aiverse-wounds-disease
description: Wounds by type and body part with bleeding, pain, healing, infection and treatments; diseases with incubation, symptoms, contagion, cures and immunity.
---

# Wounds and diseases

Header: `<aiverselib/mechanics/WoundsDisease.h>`. Library catalog id `wounds-disease`, CMake target `AIVERSE::lib_wounds_disease`.
Capability area S1 in `docs/capability-status.json`. Tests: `tests/libraries/policy_mechanics_tests.cpp`.

Deterministic policy: Fixed/integer math, ascending ids, strictly increasing ticks; repeated or invalid input returns nullopt and leaves state untouched. Works in any project: the owner reports facts and applies outputs.

## Use

1. Data: `HealthData` (wound types, diseases, seed).
2. Each tick `stepHealth(data, state, tick, newWounds, treatments, contacts)`; contacts are pairs of bodies close enough to infect this tick.
3. Outputs per body: blood, pain, penalty (0..1000), bleeding and death; feed them into Survival.h and movement/work speed.
4. Rolls are hashed from (seed, tick, body, id): results do not depend on processing order.
5. Persist HealthState (kind 208).

## Limits

No per-limb loss of function; medical UI is not included.
