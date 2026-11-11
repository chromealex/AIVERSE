---
name: meta-loot
description: Configure AIVERSE nested chests weighted rewards pity duplicate protection using the existing aiverse::meta service. Use when adding or changing these mechanics in a game's native C++ meta layer.
---

1. Inspect existing project `meta/extensions.json`; reuse `extensions.liveops.loot`. Do not regenerate algorithms.
2. Run `python3 tools/meta/catalog.py describe loot` for only this schema and example. Read `docs/meta-liveops.md` for authority, persistence or action semantics when needed.
3. Configure IDs, bounds and rewards; reuse economy assets. Keep presentation labels localized in metadata where the schema permits it.
4. Call `Service::execute` with service `loot`, action `open`, stable operation ID, and arguments such as `{"id":"chest"}`. Select one action per request. Read the action table for its exact arguments.
5. Inject trusted RewardDraw uniform in [1,total]. No RNG means Unavailable. Root cost is charged once; child tables contribute rewards only. Pity counts rare entry selections; uniqueAsset converts duplicates to duplicateReward.
6. Link `aiverse::meta` (registered native modules use `aiverse_native_use_meta`). Host supplies identity/time/authority. Preserve the same operation ID on retries; Commit must atomically persist the whole snapshot.
7. Run `tools/meta/compose.py --project PROJECT --validator PATH_TO_aiverse_meta_validate`, then `tools/meta/catalog.py pin --project PROJECT` for a new unpinned project or `verify --project PROJECT` for an existing pin. Never replace a differing pin without reviewing the upgrade. For implementation changes run `tests/meta` including native consumer and LiveOps tests.
8. Report what was configured and integrated. JSON alone does not deploy a server or install a screen.
