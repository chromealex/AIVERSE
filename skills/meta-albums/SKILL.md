---
name: meta-albums
description: Configure AIVERSE seasonal collections albums duplicate trades using the existing aiverse::meta service. Use when adding or changing these mechanics in a game's native C++ meta layer.
---

1. Inspect existing project `meta/extensions.json`; reuse `extensions.liveops.albums`. Do not regenerate algorithms.
2. Run `python3 tools/meta/catalog.py describe albums` for only this schema and example. Read `docs/meta-liveops.md` for authority, persistence or action semantics when needed.
3. Configure IDs, bounds and rewards; reuse economy assets. Keep presentation labels localized in metadata where the schema permits it.
4. Call `Service::execute` with service `albums`, action `claim, offer, accept, cancel`, stable operation ID, and arguments such as `{"id":"launch","trade":"swap1","target":"other","give":{"gold":10},"want":{"gems":1}}`. Select one action per request. Read the action table for its exact arguments.
5. Trades reserve no items. Recipient acceptance validates both balances and commits both profiles atomically. Only assets in required may be traded. A new album season needs a new definition ID.
6. Select this server policy with `aiverse_add_meta_server(game_meta MODULES albums)` and link `game_meta`. Host supplies identity/time/authority. Preserve the same operation ID on retries; Commit must atomically persist the whole snapshot.
7. Run `tools/meta/compose.py --project PROJECT --validator PATH_TO_aiverse_meta_validate`, then `tools/meta/catalog.py pin --project PROJECT` for a new unpinned project or `verify --project PROJECT` for an existing pin. Never replace a differing pin without reviewing the upgrade. For implementation changes run `tests/meta` including native consumer and LiveOps tests.
8. Report what was configured and integrated. JSON alone does not deploy a server or install a screen.

Server library: `AIVERSE::meta_albums`. Select `albums` in
`aiverse_add_meta_server(game_meta MODULES ...)`, include `game_meta.h`, and construct
`Service(kits::game_meta(), config, state, commit, draw)`. Native OBJECT modules use
`aiverse_native_use_meta(target game_meta)`. Client callers link only `AIVERSE::meta_client`;
SQL and private definitions remain server-side. Dependencies: economy.
