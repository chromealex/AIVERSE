---
name: meta-tournaments
description: Configure seasonal ranked tournament cohorts, league promotion/demotion and durable reward mail using AIVERSE C++ meta Service.
---

1. Read the tournament RPC/config sections in `docs/meta-service.md`.
2. Add an immutable ID per season in extensions.tournaments: start/end, groupSize, promote/demote, maxLeague, board and rank rewards. Use UTC integers. End is exclusive.
3. Enroll through tournament.join. The server assigns sequential cohorts within each league; do not send a client-selected group or league.
4. Submit only verified match scores with authority=true. Board policy selects best/latest/sum and rank style; tie-breaks use player ID. Enrollment records membership without a synthetic score; only verified submitters receive ranks/rewards or league changes.
5. Schedule the trusted host to call settle at/after end. This writes final results, next league and inbox rewards in one transaction. Claim rewards through economy.mail, not a second grant implementation.
6. Test ties, incomplete groups, late submissions, duplicate settlement, save/reload and failed CAS. Promotion uses ordered position; reward lookup uses displayed rank.
7. Report this as ranked cohorts, not a bracket engine or network matchmaking. Preserve pending definitions and archived results explicitly.

Server library: `AIVERSE::meta_tournaments`. Select `tournaments` in
`aiverse_add_meta_server(game_meta MODULES ...)`, include `game_meta.h`, and construct
`Service(kits::game_meta(), config, state, commit, draw)`. Native OBJECT modules use
`aiverse_native_use_meta(target game_meta)`. Client callers link only `AIVERSE::meta_client`;
SQL and private definitions remain server-side. Dependencies: social.
