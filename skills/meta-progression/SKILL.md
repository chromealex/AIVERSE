---
name: meta-progression
description: Configure AIVERSE quests, achievements, seasons, battle passes, daily login rewards, energy and segment-specific settings without generating new progression algorithms.
---

- Read progression sections of `docs/meta-game.md`; reuse tracks/daily/energy in `Meta.h`.
- Express goals as metric + increasing thresholds + reward per tier. Use separate free/premium tracks with premiumTag supplied by a trusted entitlement owner.
- Use recordEvent(metric, delta, context, immutableEventId) for recurring quests/passes. Keep event time unchanged on retry. Plain record only updates lifetime progress.
- Set start/period for reset alignment. Periods are half-open; tests must cover the exact boundary. Context conditions apply at claim, not at earning progress.
- Configure daily resetOnMiss/loop explicitly. Set initial energy in Profile; regenerate before each spend. Do not infer real-money entitlements from a client clock.
- Use cohort for stable A/B assignment and resolveSettings for conditional patches. Keep experiment weights/version stable; validate resulting configs before use.
- Keep meta state out of live deterministic simulation. Import only validated match setup or Commands.
- Validate definitions, then check repeat claims, new season without old progress, missed days, cap behavior and save/load.

Server library: `AIVERSE::meta_progression`. Select `progression` in
`aiverse_add_meta_server(game_meta MODULES ...)`, include `game_meta.h`, and construct
`Service(kits::game_meta(), config, state, commit, draw)`. Native OBJECT modules use
`aiverse_native_use_meta(target game_meta)`. Client callers link only `AIVERSE::meta_client`;
SQL and private definitions remain server-side. Dependencies: economy.
