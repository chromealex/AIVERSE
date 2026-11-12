---
name: meta-clans
description: Configure persistent friends, invitations, clan roles, donations and shared goals using AIVERSE C++ meta Service.
---

1. Read `engine/meta/Service.h` and the friends/clans rows in `docs/meta-service.md`.
2. Find capabilities with `python3 tools/meta/catalog.py search clan`; configure only `meta/extensions.json` friends/clans sections. Use `examples/meta/service.json` as the strict schema example.
3. Route request/accept/block and clan actions through `Service::execute`. Supply the authenticated Principal from the host. Never let client JSON choose actor or authority.
4. Use stable operation IDs. Store the whole cross-player snapshot through atomic Commit/CAS. Do not debit the old Lua wallet in parallel.
5. Treat clan bank as cumulative donations and goals as one reward per current member. Transfer ownership before the owner leaves a populated clan. Do not implement duplicate membership/role logic in game scripts.
6. Compose and validate project definitions, then exercise denied invites/kicks, full clan/friend lists, duplicate donations, failed commits and save/reload.
7. Bind `view` friends/requests/clan/invites to the project's UI. Global reference chat does not become a clan chat just by renaming it; private scope requires Chat authorization in the host.

Server library: `AIVERSE::meta_clans`. Select `clans` in
`aiverse_add_meta_server(game_meta MODULES ...)`, include `game_meta.h`, and construct
`Service(kits::game_meta(), config, state, commit, draw)`. Native OBJECT modules use
`aiverse_native_use_meta(target game_meta)`. Client callers link only `AIVERSE::meta_client`;
SQL and private definitions remain server-side. Dependencies: social.
