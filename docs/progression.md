# In-match progression on real units (rules `progression`)

Hero xp, levels and level-up cards for hero-squad shooters, survivors / bullet-heaven runs and roguelites (reference
port: Drone Fall). The policy is the `match-progression` library package (`libraries/include/aiverselib/genre/`
`Perks.h`, `XpPickups.h`, `Summons.h`, `BattleDeck.h`; docs/genre-packages.md § Match progression); this block
connects it to the RTS simulation: kills of real units drop xp orbs, the hero picks them up, banked levels open seeded
offers, a player command (UI, bot, Lua, MCP) picks a card, and the perks act on the units' real stats, bullets and hits.
Recruits and summoned allies are ordinary units of the player.

Everything is simulation state (fixed point, ECS order, the package's own PCG32 stream seeded from the match seed,
hashed, saved, copied by rollback snapshots). A game without a `progression` block loads, plays, saves and hashes
exactly as before.

Code: `modules/rts/RtsProgressionData.*` (parsing, bullet variants), `RtsProgression.*` (store, system, hooks, bot),
`RtsProgressionApi.cpp` (Lua `progression`). Sample: `templates/perk_arena`. Tests: `aiverse_tests progression_`.

## What is reused

| Need | Existing mechanic |
|---|---|
| perk definitions, stat pipeline, xp curve, offers, rerolls, upgrades, bot choice | `match-progression` `Perks.h` (unchanged) |
| xp orbs, magnet, landing, lifetime, orb budget | `XpPickups.h` |
| summon circles, restore / recruit, charges, ally lifetime | `Summons.h` |
| battle deck, unlock flags, choice count by tech level | `BattleDeck.h`, `readBattleDeck` (match start payload format) |
| the hero | `topDown` heroes (docs/topdown.md) when the game has them, else the first own unit of the deck's slot-0 type |
| split / ricochet / fly-through bullets | the shared projectile programs (`engine/sim/ProjectileProgram.h`, docs/projectile-programs.md) |
| burn, stun, slow, poison on hit | the effect engine's payloads / statuses (`onHit` effects, rules `statuses`) |
| perk hooks, offer veto, UI | Lua `callAction` (perk `hook`), `allowNamed` (`progression_can_offer`), view scripts + `view.ui_call` |

## Rules block

The block is the library's JSON format (`stats`, `perks`, `units`, `deck`, `xp`, `offers`, `summonPoints`; ids are
integers, multiply values are milli: 1250 = x1.25) plus engine keys:

```jsonc
"progression": {
  "players": [1],                         // slots that take part (default: every player)
  "units": [                              // deck units; engine key "type" = the unit type
    {"id": 1, "type": "drone", "hero": true},
    {"id": 2, "type": "gunbot", "cost": 1}],
  "perks": [
    {"id": 1, "key": "damage_boost", "maxSelections": 3, "effects": [{"stat": "outgoingDamage", "op": "multiply", "value": 1250}]},
    {"id": 3, "key": "split_shot", "bearer": 1,                       // engine key "bullet": projectile program modifiers
     "bullet": {"modifiers": [{"kind": "split", "count": 3, "angle": 36}]}},
    {"id": 6, "key": "burning_hits", "hook": "on_burning_hits",       // engine keys "onHit" (+ "onHitChance")
     "onHit": [{"type": "status", "status": "burn", "duration": 3}], "onHitChance": 0.5}],
  "deck": {"maxSlots": 4},
  "xp": {"toNext": [4, 6, 8, 10]}, "xpGrowthPerLevel": 3, "maxLevel": 0,
  "offers": {"default": ["anyPerk", "anyPerk", "anyPerk"], "levels": [{"level": 1, "variants": ["recruit", "recruit"]}]},
  "offerChoices": 3, "offerRerolls": 1, "rerollAvoidsRepeat": true, "maxSquad": 15, "maxRecruitsPerSlot": 0,
  "killXp": {"default": 0, "crawler": 2, "turret": 6},   // or one number for every type
  "orbs": true,                           // false: kill xp goes straight to the killer
  "orbOwner": "anyone",                   // "killer": only the killer can pick its orbs up
  "autoOpen": true,                       // false: offers wait for an "open" command (stations)
  "offerAt": "anywhere",                  // "summonPoints": cards open only while the hero stands in a circle
                                          // (Drone Fall stations); leaving closes them, xp stays banked
  "pickups": {"radius": 2.5, "magnetRadius": 40, "grabRadius": 0.5, "attractSpeed": 0, "attractAccel": 0, "maxSpeed": 40,
              "landing": 0.3, "lifetime": 40, "maxOrbs": 1024},       // seconds, units, units / s
  "summons": {"progress": 2, "cooldown": 0, "maxAllies": 4, "charges": 0, "chargeRecharge": 0, "resetOnExit": true},
  "summonPoints": [{"id": 1, "position": [20000, 0, 13000], "radiusMilli": 1600, "action": "restoreOrRecruit",
                    "singleUse": false, "cooldownTicks": 200}],       // library format: milli-metres, ticks
  "squad": {"follow": 4, "lifetime": 0},  // idle allies walk back farther than this from the hero; recruit lifetime (s)
  "critDamage": 2000,                     // base crit multiplier (milli) of the critDamage stat
  "decks": {"1": {"player": 1, "slots": [...]}},   // per player (or a list with "player")
  "defaultDeck": {"player": 1, "techLevel": 0, "slots": [{"slot": 0, "unit": 1}, {"slot": 1, "unit": 2}]}
}
```

Maps may add / replace circles by id: `"progression": {"summonPoints": [...]}`.

## The battle deck (match start payload)

Each participating player gets a `BattleDeck` (library payload format: `player`, `techLevel`, `choiceCount`,
`enforceUnlocks`, `slots` with `unit`, `level`, `opened`, `inTeam`, `perks`), first match wins:

1. the map's player entry `players[i].deck` — hosted battles (docs/battle-net.md) put each seat's loadout here when
   they build the match map from `MatchPayloadMsg` (the payload is the same bytes on every client, so every peer builds
   the same deck); raids (docs/raid.md) use the same map;
2. rules `decks` by player;
3. rules `defaultDeck`;
4. none: slot 0 = the lowest hero unit, then every other deck unit, `enforceUnlocks: false`.

`player` in a payload must be positive; it is replaced by the seat's slot. The deck is validated (`battleDeckError`:
slot numbering, known units / perks, `maxSlots`, `maxSameUnit`, `budget`, hero in slot 0) at setup: a bad payload is a
readable setup error. Unit and slot initial perks are granted once at the start.

## Units, slots, bearers

The store keeps a table of real units that belong to a deck slot: the hero (slot 0, bearer 1) and recruits / summoned
allies (their slot, bearer 2). Slot-scoped perks reach only that slot's units, player-scoped perks every unit of the
player; a perk or effect `bearer` 1 = hero only, 2 = allies only. A respawned top-down hero is adopted again (a fresh
unit at its type's health gets the perked maximum). Units outside the table (mobs, buildings, other players) are never
touched.

## Stats on the unit pipelines

Effects are evaluated by the library pipeline (stage order Set -> Add -> Multiply, slot before player scope, occurrence
order; Multiply truncates per step; stat clamps) when the engine asks, never written into units. Absolute values are
milli units (add 1000 = +1 speed / hp / range).

| perk stat | engine pipeline | base the pipeline sees |
|---|---|---|
| `outgoingDamage` | `Stat::Damage` (weapon damage multiplier) | the engine multiplier x 1000 |
| `incomingDamage` | `dealDamage` after armor, before `before_damage` | 1000 |
| `moveSpeed` | `Stat::Speed` (also top-down hero movement) | speed x 1000 |
| `fireInterval` | `Stat::AttackSpeed` | first weapon's interval in ms (`cooldown / attackSpeed`) |
| `attackRange` | `Stat::Range` | weapon range + range bonus, x 1000 |
| `maxHealth` | `Stat::MaxHp`; on a change health is rescaled (`rescaleHealth`: growth keeps the fraction) | max hp x 1000 |
| `splashRadius` | projectile splash of the shot | splash x 1000 |
| `critChance` / `critDamage` | `dealDamage` from the unit (roll on `sim.rng()`, only when the chance is > 0) | 0 / `critDamage` |
| `healthRegen` | the progression system, per tick | 0 (milli hp / s) |
| `pickupRadius` / `pickupXp` | xp orb collection | `pickups.radius` x 1000 / 1000 |

A stat without an effect on the unit stays exactly the engine's value (no milli rounding). `fireCount`,
`summonCharges`, `summonCooldown` and game stats (ids >= 16) are readable with `progression.stat(unit, stat, base)`.
Techs, buffs, statuses, auras and the rest of `RtsModule::stat` apply first; the perk pipeline is the last step.

## Bullets and hits

A perk's `bullet.modifiers` (split, ricochet, flyThrough and the damage / speed / radius scalars, in the projectile
programs' order semantics: a scalar after a split gate affects only the children, e.g. Drone Fall's Sniper Contact-Split
`[{"kind": "split", "count": 2}, {"kind": "damage", "factor": 0.5}]`) extend
the shooter's projectile weapons: at load every projectile weapon (`projectileSpeed` > 0) of a deck unit type gets one
**variant** per non-empty set of bullet perks: the weapon's own `flight` program (or a default homing one) followed by
each perk's modifiers in perk id order (at most 6 bullet perks, 16 modifiers per program, 126 variants). A shot of a
unit with bullet perks uses its variant (`Projectile::weapon = -2 - variant`); ricochet / fly-through counts grow with
the number of times the perk was taken. The chain kernel, collisions, damage and saves are the projectile programs'.
A perk's `onHit` payload (any effects list: burn, stun, slow, ...) is applied to what the bearer's weapons hit
(projectiles carry it when their weapon has no on-hit of its own), with `onHitChance` rolled on `sim.rng()`.

## Levels, offers, choices

Kills (killer = the owner of the last damage, hostile only) drop `killXp` orbs at the victim; the hero collects them
within its `pickupRadius` (a magnet: `progression.magnet(slot)`). `grantXp` banks levels; with `autoOpen` the next offer
opens at once (`openOffer`: variants of `offers.levels[level]` or `offers.default`, drawn with the package's stream,
`progression_can_offer(player, key | "recruit:<type>")` may veto a card). Choices are commands:

`kCmdProgression`, `param = op | card << 8 | revision << 16`: op 1 choose (card 0-based), 2 reroll, 3 close, 4 open.
A stale revision (the offer changed meanwhile) is ignored. Lua: `game.command(p, {type = "progression", op = "choose",
card = 1, revision = r})`; MCP `play_command {"command": "progression", "op": "choose", "card": 1}` (revision filled
in). A recruit card spawns the slot's unit next to the hero; a perk card applies at once (max health rescaled) and runs
the perk's `hook` global: `hook({player, unit, x, z, args = {perk, id, slot, level}})`.

**Bot.** `RtsAI` sends the library's `botOfferChoice` (lowest `aiPriority`; `aiPriorityLowHealth` under 35% health;
recruits `recruitAiPriority`), and an `open` when `autoOpen` is off.

## Summons and the squad

Circles (`summonPoints`) fill while the player's hero stands inside (`stepSummons`: team filter, progress, reset on
exit, cooldown / single use, `maxAllies`, charges). Completion restores the dead acquired ally with the lowest slot, or
recruits the lowest recruitable slot (`restoreOrRecruit`), as a new unit at the circle. Allies that die are reported to
the policy; recruits from cards are acquired allies too, so circles can bring them back. Allies with a `squad.lifetime`
are removed silently when it runs out. Idle allies (no orders, no live target) walk back to the hero when farther than
`squad.follow`.

## Lua and UI hooks

Queries (simulation and view scripts): `progression.active()`, `players()`, `player(slot)` -> `{level, xp, xp_to_next,
progress (0..1, the xp bar), levels (banked), kills, hero, offer_open, perks = {{id, key, slot, initial}}, recruits}`,
`offer(slot)` -> nil or `{revision, level, rerolls, cards = {{kind = "perk" | "recruit", perk, id, slot, unit, hook,
scope, letter (R / H / P / G)}}}`, `orbs()`, `points([slot])` (circle state for visuals: ready / progress / used / cooldown / squad_full /
blocked / pending), `unit(u)`, `stat(u, stat [, base])`. Actions (simulation scripts): `grant_xp(slot, xp [, pickup])`,
`grant_perk(slot, perk [, deck slot])`, `clear_perks(slot [, deck slot [, keep_initial]])`, `choose(slot, card [, rev])`,
`reroll`, `open`, `close`, `magnet(slot)`, `spawn_orb(x, z, xp [, owner])`.

View scripts (the card UI): `view.progression_choose(card)` / `view.progression_reroll()` send the local player's
command for the open offer. The game's UI Lua shows the cards; the pattern in `templates/perk_arena/scripts/view/levelup.lua`:
on `progression_offer` call `view.ui_call("LevelUp.show", offer.cards, offer.revision, offer.rerolls)`, poll
`view.ui_call("LevelUp.take")` each frame and send the picked card; `LevelUp.xp(progress, level)` feeds the xp bar.

| event | value |
|---|---|
| `progression_xp` | xp gained (after `pickupXp`), `unit` = hero |
| `progression_level` | levels banked \| level reached << 32 |
| `progression_offer` | revision \| cards << 32 \| kind << 40 (0 opened, 1 rerolled, 2 closed, 3 empty) |
| `progression_perk` | perk id \| (slot + 1) << 32 (0: player scope) \| 1 << 48 when it replaced a lower tier; `unit` = the slot's unit |
| `progression_recruit` | deck slot; `unit` = the new ally |
| `progression_orb` | xp \| kind << 40 (0 spawned, 1 collected, 2 expired, 3 merged); `player` = collector |
| `progression_summon` | point id \| SummonEventKind << 32 (Entered 0, Left 1, ProgressReset 2, Requested 3, Confirmed 4, ...); `unit` = ally |

## Determinism, saves, rollback

`ProgressionStore` (world resource, only with the block): `PerkState`, `PickupState`, `SummonState` (library records),
the unit table, magnets, kill counters, queued orbs. It is hashed after the top-down state (the definition / config
hashes too), saved after it in `RtsSave` and validated on load (`valid(...)`), and rollback snapshots copy it. The
system runs right before `death`. The `progression_can_offer` and perk hooks are simulation Lua (budgeted, deterministic).

## Drone Fall mapping (Ext-Client `knowledge/systems/gameplay/modifiers.md`)

| Drone Fall | here |
|---|---|
| combat level-ups offer only `SpawnFromDeck` (2 alternatives before tree node 5, 3 after; `campaignChoiceCount` override) | `"offers": {"default": ["recruit"]}`, `"deck": {"choices": [{"minTech": 0, "count": 2}, {"minTech": 5, "count": 3}]}`, payload `techLevel` / `choiceCount` |
| `preferNewRobotAtLevel` | `offers.preferNewRecruitAtLevel` |
| unlocked flags `IsOpened && IsInTeam` (sandbox / AI catalog exempt) | payload `opened` / `inTeam`, `enforceUnlocks: false` |
| initial abilities (`GDCInitialModifiers`), purchased personal perks of a robot level | `units[].perks` (offerable: false), payload `slots[].perks` |
| Proto (slot) / Player scope, bearer Robot / Weapon / Bullet / Wander | `scope`, bearer 1 hero / 2 allies; Wander perks (speed, pickup radius, pickup xp) are player-scope stats |
| Max HP keeps the health fraction on growth, clamps on shrink | the same rule (floor, Fixed) |
| pickup EXP bonus keeps the fractional remainder; one-shot magnet | `pickupXp` stat (library remainder), `progression.magnet(slot)` |
| Split (contact / time, children count, fan angle), Ricochet (fresh nearest target, count), Fly Through with falloff | perk `bullet.modifiers` over the projectile programs |
| `levelUpRevision`, stale clicks rejected | offer `revision` in `kCmdProgression` |
| console `levelup` / `modifiers give` / `clear` / `clearproto` | Lua `grant_xp`, `grant_perk`, `clear_perks(slot, -2)`, `clear_perks(slot, deck slot)` |
| level-ups taken at summon circles / stations (two / three alternatives, keep EXP, heal the squad) | `"offerAt": "summonPoints"` (cards open in a circle, close when the hero leaves; xp stays banked); healing in Lua on `progression_offer` |
| card letters R / P / H / G per level (`experienceLevelingConfig`) | `offers.levels[].variants`: R `recruit`, P / H `slotPerk` (hero perks: `units: [hero]`, robot perks: robot units), G `playerPerk`; each card has `letter` in `progression.offer()` |
| script command `df_pick {index}` | keep it: `function df_on_pick(player, id, args) return progression.choose(player, args.index) end`, or send `kCmdProgression` directly |

Differences: crits roll on every damage instance of the unit (Drone Fall: direct hits only); "every second / third
bullet" perks (Universal, Sniper), Double-Shot `fireCount`, Push and rocket ground effects need Lua (`hook` + effects);
level-up VFX is the game's view script on `progression_perk`.

## Verification (2026-11-12, container gcc 13 Release)

`aiverse_tests` 9663 checks / 0 failed (7 `progression_` tests); `tests/checksums/check.sh`: every existing line
identical, `perk_arena` added (Pilot wins seeds 1..3); `perk_arena --verify-restore-at 20`: snapshot / serialized /
fresh restore OK. Drone Fall project (KINGSTON game main, converter draft `tools/out/progression.draft.json` + circles
from the `summon_N` areas, a few perks): Bridge loads and plays to a result; with a 30 m pickup radius the bot hero
reaches level 3, recruits 2 robots, takes a perk, restore verification OK; with `offerAt: summonPoints` xp banks
(770) because the standing bot hero never walks to a circle. Draft fix needed: `preferNewRecruitAtLevel` belongs in
`offers`.

## Not done

`fireCount` (multi-barrel shots) and summon-charge stats are not mapped to engine pipelines; bullet perks need
projectile weapons (instant-hit weapons get on-hit payloads and stats only); offers are not modal (the match does not
pause); no built-in card UI in the engine client (the game's UI Lua draws it); planets untested.
