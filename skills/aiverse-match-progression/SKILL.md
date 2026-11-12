---
name: aiverse-match-progression
description: Reuse AIVERSE in-match progression (perk/modifier packages with an ordered stat pipeline, hero/player xp and level curves, seeded level-up offers with rerolls and stacking/upgrade rules, xp orb pickups with magnet/attraction, summon circles, charges and summoned allies, pre-match battle deck limits) when building native C++ survivor/bullet-heaven, extraction shooter, hero-squad or roguelite games.
---

# match-progression

Use when a native/host owner needs run-time progression inside one match: kills and pickups give xp, level ups
offer N choices (recruit a deck unit, a perk for one deck slot, a player-wide perk), perks stack into stat
modifiers, allies are summoned/restored at circles, and the loadout comes from a pre-match battle deck. Not for
meta progression between matches (talent trees, shops, unlocks): use `engine/meta`. Not for built-in RTS hero xp
(`xpLevels`/`perLevel` in RtsData) or RtsModule stat mods: those stay authoritative in RTS games.
RTS games built from JSON + Lua get the whole package on real units through the rules block `progression`
(`docs/progression.md`: orbs from kills, `kCmdProgression` choices, perk stats on unit pipelines, bullet perks,
on-hit payloads, summon circles, battle deck from the map's `players[i].deck`, Lua `progression`); do not rewire it
in game scripts.

1. Read `docs/genre-packages.md` (match-progression) and the catalog entry; inspect `genre/Perks.h`,
   `genre/XpPickups.h`, `genre/Summons.h`, `genre/BattleDeck.h` for exact behaviour.
2. Data: definitions JSON (`readPerkDefinitions`, `readSummonPoints` from `genre/PerksData.h`; example
   `examples/libraries/genre/perks.json`): stats with clamps, perks (scope player|slot, effects stat/op/value/stage,
   maxSelections, minLevel, weight, requires/excludes/units, upgrades, hook, aiPriority), units (cost, hero, initial
   perks), deck rules (maxSlots, maxSameUnit, budget, heroSlot0, choice count by tech level), xp table/curve and kill
   rewards, offer variants per level, summon points. Match start payload per player: `readBattleDeck`
   (`examples/libraries/genre/perks-deck.json`; slot recruitable = opened && inTeam unless enforceUnlocks=false).
   Scalars: optional `perks` section of `libraries/spec/gameplay.json` -> `perkConfig`, `pickupConfig`,
   `summonConfig`, `perkSettingsErrors` (`genre/PerksConfigured.h`). Errors are path-qualified.
3. Start: `seedPerks(state, matchSeed)` once, `addPlayer(defs, state, deck)` per player (validates the deck and
   grants initial perks once), `addSummoner(summonConfig, summons, player, tick)`.
4. Per authoritative tick: `stepPickups(pickupConfig, orbs, tick, {spawns, collectors})` (collector radius = the
   player's `perk_stat::PickupRadius` via `evaluateStat`); feed `collected` into `grantXp(..., pickupBonus=true)`;
   kills without orbs use `grantXp(defs, cfg, state, player, killXp(defs, kind))`. `openOffer` when a level is
   banked (or only at stations, owner's choice), `rerollOffer`/`selectOffer(revision, index)` from player commands
   (stale revisions are rejected), `botOfferChoice` for bots. Apply `PerkEvent`s: Recruit -> spawn the slot's unit
   and `registerAlly`; PerkSelected/PerkReplaced/InitialPerk carry the perk `hook` for Lua/C++ callbacks.
   `stepSummons(summonConfig, points, summons, tick, {presences, died, removed})` returns `SummonRequest`s: spawn,
   then `confirmSummon` (or `cancelSummon`); `despawn` lists expired allies; `removeSummoner` on defeat/leave.
5. Read stats where the owner computes them: `evaluateStat(defs, progress, stat, base, slot, bearer, &steps)`
   (Set -> Add -> Multiply stages, slot packages before player packages, occurrence order, per-step truncation,
   clamp; `steps` explains the result). `scaleAmount` for damage, `rescaleHealth` when max health changes.
6. Persist `PerkState`, `PickupState`, `SummonState` with `genre::save/load` (`genre/PerksPersistence.h`, record
   kinds 104..111 reserved, 104..106 used) in separate NativeState keys; feed `progressionHash` of each into the
   checksum and bind `perkDefinitionsHash(defs)` + `progressionConfigHash(...)` in the save envelope.
7. Verify: `aiverse_library_progression_tests <repo>`, `aiverse_progression --root <repo> --verify` (scripted bot run,
   replay + mid-match restore must give the same checksum), `python3 tools/libraries/catalog.py check`,
   `generate_gameplay.py --check`. Not implied: UI cards, VFX, damage/movement application, network wire format,
   station recruitment UX, AI beyond `botOfferChoice`, or performance at bullet-heaven densities.

Build selection: `AIVERSE::lib_match_progression` (depends on `aiverse_engine` only); native OBJECT modules call
`aiverse_native_use_libraries(target match-progression)`.
