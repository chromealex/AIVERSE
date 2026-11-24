---
name: aiverse-diplomacy
description: Relations from decaying modifiers, treaties with truce and break penalties, defensive alliances that join wars, deal evaluation and a strategy advisor for AI factions.
---

# Diplomacy and strategy AI

Header: `<aiverselib/mechanics/Diplomacy.h>`. Library catalog id `diplomacy`, CMake target `AIVERSE::lib_diplomacy`.
Capability area T3 in `docs/capability-status.json`. Tests: `tests/libraries/policy_mechanics_tests.cpp`.

Deterministic policy: Fixed/integer math, ascending ids, strictly increasing ticks; repeated or invalid input returns nullopt and leaves state untouched. Works in any project: the owner reports facts and applies outputs.

## Use

1. Each turn `stepDiplomacy(config, state, turn, commands, facts, values)`; commands add factions, modifiers, proposals, breaks and war declarations.
2. AI factions accept a deal when `dealUtility` >= 0 (relation, power, threats, value of items).
3. `adviseStrategy(state, config, facts, faction)` gives a stance and proposals for the AI turn.
4. Persist DiplomacyState (kind 217).

## Limits

No espionage or vassal tribute flows; the economic value of items comes from the owner.
