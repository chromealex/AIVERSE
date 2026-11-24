---
name: aiverse-item-affixes
description: Seeded item generation with prefix/suffix affix pools, tiers, groups and item level, crafting rerolls, coloured sockets and gems, elemental auras and reactions, item migration.
---

# Item affixes, sockets and reactions

Header: `<aiverselib/mechanics/ItemAffixes.h>`. Library catalog id `item-affixes`, CMake target `AIVERSE::lib_item_affixes`.
Capability area A2 in `docs/capability-status.json`. Tests: `tests/libraries/policy_mechanics_tests.cpp`.

Deterministic policy: Fixed/integer math, ascending ids, strictly increasing ticks; repeated or invalid input returns nullopt and leaves state untouched. Works in any project: the owner reports facts and applies outputs.

## Use

1. Data: `AffixData` (affixes, item bases with socket colours and implicit stats, gems, version).
2. `rollItem(data, id, base, itemLevel, prefixes, suffixes, seed)` is deterministic; crafting: `addAffix`, `removeAffix`, `rerollAffix`, `rerollValues` with their own seeds.
3. `socketGem` / `unsocketGem`; `itemStats` sums values per stat id for attribute-modifiers.
4. Reactions: `applyElements(reactionData, auras, tick, hits)` keeps auras per target and emits reactions with damage multipliers.
5. Content updates: `migrateItem(data, item, migration)` renames, removes and rescales affixes.
6. Persist ItemInstance (kind 205) and AuraState (kind 206).

## Limits

No UI; affix text and balance are content.
