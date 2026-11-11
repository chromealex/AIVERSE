---
name: meta-game
description: Build AIVERSE game meta from reusable C++ policies and JSON presets. Use for shops, offers, leaderboards, chat, progression, daily rewards, energy, inbox, segmentation or a complete meta layer in a project, including native C++ callers.
---

1. Read `docs/meta-game.md` only for the selected subsystem. Inspect the project's existing meta and UI first.
2. Reuse `aiverse::meta` (`engine/meta/Meta.h`, `Social.h`); never rewrite its algorithms in game C++ or Lua.
3. For a new project run `python3 tools/meta/scaffold.py --preset minimal|mobile|competitive --out <project>`. This writes definitions only, not a backend or UI integration.
4. Edit the relevant JSON, not engine code. Keep arbitrary presentation data in metadata; localize all displayed labels.
5. Link native consumers to `aiverse::meta`; for registered object modules use `aiverse_native_use_meta(aiverse_native_<id>)`. Keep time/network/profile mutations outside deterministic simulation callbacks.
6. Choose one authoritative profile owner. Reuse existing Store providers. Online services require a real backend adapter; never describe offline data as global multiplayer data.
7. Read just the matching recipe: `skills/meta-economy/SKILL.md`, `skills/meta-progression/SKILL.md`, `skills/meta-social/SKILL.md`.
8. Run `aiverse_meta_validate economy|boards|chats <file>` on changed definitions. For policy changes run `tests/meta` and external native consumer tests.
9. State precisely which pieces are local, integrated and deployed. Do not claim that writing JSON automatically installs screens, auth or servers.

For API selection read [references/catalog.md](references/catalog.md). For format/authority/save contracts read `docs/meta-game.md`. Change generic policies only when configuration and provider hooks cannot express the requested behavior.

## Extended service routing

Start with `python3 tools/meta/catalog.py search <intent>` and read only its recipe.
Use meta-clans for friends/clans, meta-items for instance inventory, meta-tournaments for
ranked seasons, meta-backend for sessions/storage/transport, and meta-ui for screens.
Use `--preset full --with-ui` to scaffold all definitions and screens, then compose with
`tools/meta/compose.py --project PROJECT --validator PATH_TO_aiverse_meta_validate`.
Read `docs/meta-service.md` only for the selected subsystem. Pin the package through
`tools/meta/catalog.py pin --project PROJECT`; verify before regenerating/upgrading.
These are repository recipes, not globally installed personal skills.


## LiveOps routing

For loot, albums/trades, timed jobs, research/prestige, compound quests, events, clan wars,
async PvP, party queues, Elo/Glicko-2, referrals, rewarded ads, offer chains, notifications
or offline income: search `tools/meta/catalog.py`, run `describe PACKAGE`, and read its
single `meta-*` recipe. Full preset includes `extensions.liveops`. See `docs/meta-liveops.md`
for action arguments and trust boundaries. Do not read all 15 recipes to configure one policy.
