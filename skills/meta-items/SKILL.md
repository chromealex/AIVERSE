---
name: meta-items
description: Add persistent item instances, collection, equipment slots, upgrades, merging and duplicate salvage with AIVERSE meta policies.
---

1. Read the items RPC/config sections in `docs/meta-service.md` and `engine/meta/Service.h`.
2. Configure `extensions.items` with slots, upgradeCosts, salvage, optional mergeCount/mergeTo and localized metadata. Define currencies in economy.assets first.
3. Map an optional asset to the item definition and use items.redeem to convert one shop/quest/mail asset into a unique instance atomically. Grant unique instances only after a trusted server reward/payment event. Keep instance ID/type/level and equipment in the service snapshot. Do not copy the simulation's stacked inventory into the meta profile.
4. Use items equip/unequip/upgrade/salvage/merge actions. Merge's id is one input and the surviving output; inputs contains the other distinct IDs. Require equal levels/types and no equipped inputs. Output starts at level zero.
5. Let Service own the atomic instance+currency transaction and dedupe. UI previews never charge separately.
6. Test insufficient balances, occupied/incompatible slots, repeated input IDs, cap failures, duplicate operations and reload. Use `tests/meta/service_tests.cpp` as a compiling example.

Server library: `AIVERSE::meta_items`. Select `items` in
`aiverse_add_meta_server(game_meta MODULES ...)`, include `game_meta.h`, and construct
`Service(kits::game_meta(), config, state, commit, draw)`. Native OBJECT modules use
`aiverse_native_use_meta(target game_meta)`. Client callers link only `AIVERSE::meta_client`;
SQL and private definitions remain server-side. Dependencies: economy.
