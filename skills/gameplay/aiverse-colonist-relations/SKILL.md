---
name: aiverse-colonist-relations
description: Colonist families, memories with decay and stacking, opinions, friendships and rivalries with hysteresis, partnerships, item ownership and social mood.
---

# Colonist relations

Header: `<aiverselib/mechanics/ColonistRelations.h>`. Library catalog id `colonist-relations`, CMake target `AIVERSE::lib_colonist_relations`.
Capability area C1 in `docs/capability-status.json`. Tests: `tests/libraries/policy_mechanics_tests.cpp`.

Deterministic policy: Fixed/integer math, ascending ids, strictly increasing ticks; repeated or invalid input returns nullopt and leaves state untouched. Works in any project: the owner reports facts and applies outputs.

## Use

1. Each tick `stepSocial(config, memoryDefs, state, tick, commands)`; commands add colonists, family links, memories, proposals, claims and item use.
2. `opinion(config, defs, state, a, b)` and `moodOf(config, defs, state, a)` feed the colony mood and job choices.
3. Using an item owned by another colonist emits ItemUsedByOther and the configured memory for the owner.
4. Persist SocialState (kind 210).

## Limits

Births and ageing belong to the owner; no conversation generator.
