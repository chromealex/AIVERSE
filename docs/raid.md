# Extraction raids (rules `raid`)

The match loop of extraction shooters (Drone Fall, Tarkov-likes): raiders enter the map, open containers
and other hold-to-use objects, carry loot, complete objectives and leave through extraction points. A raider
that dies loses what it carried; one that gets out secures it. When every raider is resolved, or the clock
runs out, the raid is over and each player gets a **receipt**: a deterministic JSON payload with a stable
operation id that the meta layer applies exactly once.

Everything is simulation state (fixed point, ECS order, `sim.rng()` only, hashed, saved, copied by rollback
snapshots). A game without a `raid` block loads, plays and hashes exactly as before.

Code: `modules/rts/RtsRaidData.*` (parsing), `RtsRaid.*` (store, system, receipts), `RtsRaidBot.cpp` (bot),
`RtsRaidApi.cpp` (Lua `raid` table). Editor / MCP: `apps/editor/core/RaidOps.*`. Sample: `templates/extraction`.

## What is reused

| Need | Existing mechanic |
|---|---|
| carried loot, pick up / drop, drop on death | rules `items` + type `inventory` (`ItemBag`), `MechState::groundItems`, `kCmdPickItem` / `kCmdDropItem`, `mechDropItems` |
| item values | the item's `cost` (sum), unless `raid.items.<id>.value` |
| hostile PvE mobs | map entities with `"owner": -1` (creeps, leash) |
| quest texts / campaign objectives | raid objectives are their own list (progress counters per player); trigger `objective` actions still work beside them |
| Lua | generic `on(event, fn)` tables, before-hooks (`allowNamed`), `state` |
| meta | `engine/meta` `Economy::transact(operation, cost, reward)` is idempotent per operation id |

## Rules block

```jsonc
"raid": {
  "mode": "pve",                 // or "pvp" (map may override)
  "duration": 300,               // raid seconds (0: no clock)
  "startDelay": 1,               // briefing before the clock runs
  "raiders": ["drone", "tag:hero"],   // the bodies that make a player a raider (default: types with an inventory)
  "loseOnDeath": "drop",         // drop: the bag falls on the ground (PvP looting); destroy: gone
  "pickupRadius": 1.5,           // raiders pick up ground items this close (0: only the pick_item command)
  "removeOnExtract": "all",      // or "raiders": what leaves with the helicopter
  "endOnFirstExtract": false,
  "items": {"battery": {"value": 20, "asset": "battery"}},   // receipt value, meta asset id
  "loot": {                      // loot tables (rolled with sim.rng())
    "crate": {"rolls": [1, 2], "entries": [
      {"item": "scrap", "weight": 5, "count": [1, 2]},
      {"resource": "coins", "amount": 10, "weight": 3},       // straight into the carried resources
      {"none": true, "weight": 1}]}
  },
  "interact": {                  // hold-to-use objects, keyed by unit type
    "crate": {"hold": 2, "range": 1.2, "loot": "crate"},
    "robot": {"hold": 3, "range": 1.2, "objective": "rescue", "fn": "on_robot_freed", "leave": "pause",
              "interruptOnDamage": true, "uses": 1, "cooldown": 0, "consume": true, "single": true}
  },
  "extraction": {"radius": 3, "channel": 4},   // defaults of every point
  "points": [ ... ],             // see below; maps add / replace points by id
  "objectives": [ ... ]
}
```

Interactables progress while exactly one raid player stands within `range` (+ both radii) — `"single": false`
lets the current user keep it with others around. Leaving resets (`"leave": "pause"` keeps progress); damage
resets with `interruptOnDamage`. On completion: the loot table is rolled onto a fixed ring of ground spots,
the objective progresses, the Lua `fn` runs (`fn({player, unit, x, z, args = {target}})`), and when `uses`
are spent the object is removed silently (`consume`).

## Extraction points

```jsonc
{"id": "lz", "pos": [43.5, 27.5], "radius": 3,
 "channel": 4,                  // seconds inside (0: at once)
 "opensAt": 20,                 // raid seconds before it can open
 "open": 40, "sleep": 15,       // open / sleep cycle (open 0: stays open)
 "arrival": 0, "departure": 2.5,// inbound phase before opening, departing phase after a window
 "requires": ["rescue"],        // objectives the raider must have completed
 "scope": "player",             // "point": one shared fill, everyone eligible inside extracts together
 "contest": "block",            // hostile raider inside: "block" (pause), "reset", "ignore"
 "leave": "reset",              // "pause" keeps a channel when the raider steps out
 "interruptOnDamage": true,
 "capacity": 0,                 // extractions per window (0: unlimited)
 "call": false, "follow": false,// called points wait for raid command "call"; follow: opens around the caller
 "team": 0,                     // >0: only this team
 "fn": ""}                      // Lua veto for this point: fn(player, id) -> false refuses
```

Phases (`kEvRaidPoint`, view events): `locked → (sleep) → inbound → open → departing → sleep/open ...`.
Drone Fall mapping: zone extraction points = `scope: "point"`, `open`/`sleep` = `cooldownActive`/`cooldownSleep`,
`channel` = `activationFillCapacity / activationFillSpeed`, `contest: "ignore"`; the boss-extraction helicopter
= `call: true, follow: true, arrival, channel: 0, capacity: 1` with its offer gated by `requires` / `opensAt` /
`raid_can_extract`; its fly-in / fly-out animation follows `inbound` / `departing`.

## Objectives

`{"id", "text", "kind": "collect" | "reach" | "kill" | "interact" | "script", "item", "type", "pos", "radius",
"count", "shared", "required", "reward": {"asset": n}}`. Collect counts carried + secured items; reach needs a
raider within `radius`; kill counts kills of `type` (any hostile without it); interact completes from
interactables of `type` or naming the objective; script objectives progress only from Lua. `shared` completes
it for the whole team; `required` marks the receipt `completed` only when it is done (and the raider got out).

## Match states

`briefing → running → over` (`kEvRaidPhase`). Per player (`kEvRaidPlayer`): `in_raid → extracted | dead | timeout | abandoned`.
The raid ends when all raid players are resolved, the clock ends (the rest time out and lose their bags), or
on the first extraction with `endOnFirstExtract`. The winner team: most value brought out (+1 per extracted
raider), a tie is a draw; in PvE with nobody out, a hostile player team if there is one, else a draw. The raid
sets `GameState::winnerTeam`, and the standard `victory.lua` steps aside while a raid is active.

Commands: `kCmdRaid` (`game.command{type = "raid", param = ...}`), `param = op | point << 8`: `1` call (point -1:
the nearest callable one), `2` abandon.

## Receipts

```json
{"schema":1,"id":"raid-<match>-p1","match":"<hex>","mode":"pve","player":1,"team":1,"outcome":"extracted",
 "final":true,"point":"lz","ms":187500,"winner":true,"completed":true,"value":140,
 "secured":{"battery":1,"data_core":1},"lost":{},"resources":{"coins":20},
 "objectives":{"rescue":{"done":true,"progress":1,"count":1}},"kills":4,
 "reward":{"battery":1,"coins":70,"data_core":1,"robot_parts":1}}
```

`id` = FNV-1a of (match seed, map JSON) + slot: the same on every peer, the server arbiter and replays, and the
same for any outcome of that player in that match, so a conflicting second payload is detected. Apply it with
`economy.transact(receipt.id, {}, receipt.reward)`: a repeat returns `Duplicate` and grants nothing. `reward` has
only what was brought out (secured items by asset id, resources, completed objectives' rewards). Hosts read
receipts from `RaidStore::receipts` (headless prints `Raid receipt: {...}` lines, MCP `raid_run`, Lua `raid.receipt`).

## Lua

Queries (simulation and view scripts): `raid.active()`, `raid.phase()`, `raid.time_left()`, `raid.players()`,
`raid.player(slot)` (`status`, `value`, `kills`, `carried`, `secured`, `lost`, `objectives`), `raid.points()`,
`raid.point(id)`, `raid.can_use(slot, id)`, `raid.receipt(slot)`. Actions (simulation): `raid.extract(slot [, id])`,
`raid.fail(slot [, "dead"|"timeout"|"abandoned"])`, `raid.progress(slot, objective [, n])`, `raid.complete(slot, objective)`,
`raid.give(slot, item [, n [, secured]])`, `raid.spawn_loot(table, x, z [, slot])`.
Veto: global `raid_can_extract(player, point_id)` (also asked for calls). Events (`on(name, fn)`, view `view.on`):

| event | value |
|---|---|
| `raid_phase` | 1 running, 2 over |
| `raid_point` | phase (0 locked, 1 sleep, 2 inbound, 3 open, 4 departing) \| point << 8; `player` = caller |
| `raid_channel` | point \| kind << 16 (0 start, 1 interrupted) |
| `raid_extracted` | point (-1 forced), `unit` = raider |
| `raid_loot` | item \| count << 16 (bit 40: resource index) |
| `raid_interact` | object type \| kind << 16 (0 start, 1 interrupted, 2 done), `unit` = raider, `other` = object |
| `raid_objective` | objective \| 1 << 16 |
| `raid_player` | status (1 in raid, 2 extracted, 3 dead, 4 timeout, 5 abandoned) |

## Bot

`RtsAI` hands raid players to `raidBotThink`: required objectives (reach, interact, collect, kill), then loot
while the bag has room and more than a third of the clock is left, then the nearest usable point (calling a
callable one). Enemies (owner -1 creeps, other AI players without raiders) keep their usual behaviour.

## Editor / MCP

`raid_info` (merged rules + map config, live check), `raid_set` (rules or map block, validated before saving),
`raid_point` (add / replace / delete a map extraction point), `raid_run` (bot raid on the open map: receipts,
phase timeline, outcome). The **Рейд** window lists points and objectives and runs a bot raid.

## Limits / not yet

Respawns inside a raid, item weights / grid inventories, insurance, and per-item rarity tiers are not part of
the engine block (Lua can build them on `raid.give` / events). The view draws nothing raid-specific by itself:
games use overlays / view scripts on the events above.
