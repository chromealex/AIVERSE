---
name: meta-events
description: Configure AIVERSE global personal events phases late joins catchup event currencies using the existing aiverse::meta service. Use when adding or changing these mechanics in a game's native C++ meta layer.
---

1. Inspect existing project `meta/extensions.json`; reuse `extensions.liveops.events`. Do not regenerate algorithms.
2. Run `python3 tools/meta/catalog.py describe events` for only this schema and example. Read `docs/meta-liveops.md` for authority, persistence or action semantics when needed.
3. Configure IDs, bounds and rewards; reuse economy assets. Keep presentation labels localized in metadata where the schema permits it.
4. Call `Service::execute` with service `events`, action `join, claim`, stable operation ID, and arguments such as `{"id":"festival","phase":"final"}`. Select one action per request. Read the action table for its exact arguments.
5. Rewards are claimable after phase end for claimSeconds. Joining a new period replaces old unclaimed participation. Personal origin is join time; global origin is period start. Define event currency in economy.assets and phase costs/rewards.
6. Select this server policy with `aiverse_add_meta_server(game_meta MODULES events)` and link `game_meta`. Host supplies identity/time/authority. Preserve the same operation ID on retries; Commit must atomically persist the whole snapshot.
7. Run `tools/meta/compose.py --project PROJECT --validator PATH_TO_aiverse_meta_validate`, then `tools/meta/catalog.py pin --project PROJECT` for a new unpinned project or `verify --project PROJECT` for an existing pin. Never replace a differing pin without reviewing the upgrade. For implementation changes run `tests/meta` including native consumer and LiveOps tests.
8. Report what was configured and integrated. JSON alone does not deploy a server or install a screen.

Server library: `AIVERSE::meta_events`. Select `events` in
`aiverse_add_meta_server(game_meta MODULES ...)`, include `game_meta.h`, and construct
`Service(kits::game_meta(), config, state, commit, draw)`. Native OBJECT modules use
`aiverse_native_use_meta(target game_meta)`. Client callers link only `AIVERSE::meta_client`;
SQL and private definitions remain server-side. Dependencies: economy.
