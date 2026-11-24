---
name: aiverse-hero-match
description: Hero match rules: pick/ban draft with timeouts, in-match shop with recipes, sell-back and courier delivery, recall channel, last hits, denies, assists, bounties and respawn timers.
---

# Hero match (draft, shop, credit)

Header: `<aiverselib/mechanics/HeroMatch.h>`. Library catalog id `hero-match`, CMake target `AIVERSE::lib_hero_match`.
Capability area M1 in `docs/capability-status.json`. Tests: `tests/libraries/policy_mechanics_tests.cpp`.

Deterministic policy: Fixed/integer math, ascending ids, strictly increasing ticks; repeated or invalid input returns nullopt and leaves state untouched. Works in any project: the owner reports facts and applies outputs.

## Use

1. Draft: `DraftRules` turns, `stepDraft(rules, state, tick, actions)`; timeouts pick or ban the lowest free hero.
2. Match: `stepHeroMatch(config, state, tick, commands)` with Buy, Sell, Recall, Damage, UnitKilled and HeroKilled commands.
3. Recipes consume owned components; courier deliveries arrive after `courierTicks`.
4. Persist DraftState (kind 218) and HeroMatchState (kind 219).

## Limits

Lanes, creeps and towers are ordinary units of the host; no MMR here (see meta-matchmaking).
