# Boss encounters in RTS games (rules `encounters`)

Boss fights, telegraphed area attacks, status effects, add waves, personal boss windows and mob behaviour on
real units of the RTS simulation (Drone Fall port). The decisions come from the `boss-encounters` library
package (`libraries/include/aiverselib/genre/{Telegraphs,StatusEffects,BossEncounter,BossSchedule,MobBehaviour}.h`,
[genre-packages.md § Boss encounters](genre-packages.md)); `modules/rts/RtsEncounters.*` owns the bodies:
orders, damage, statuses, spawning, events, saves. Sample: `templates/boss_arena` ("Golem Pit": a top-down hero
against a three-phase golem with adds and crawler packs).

## Who is driven

- **Monster players**: map players with `"monsters": true`, and always the hostile creeps (owner 255, e.g. units a
  game's Lua director spawns). The computer player leaves them alone; their units of a boss type get a boss brain,
  their units of a mob type get mob behaviour. Other players' units are never steered. Creeps are a side of their
  own for telegraphs (they never hit each other unless `friendlyFire`).
- A boss walks with a move order and stops at `approach`: the weapon system neither stops nor redirects a unit with
  orders, so a boss whose (other) weapons outrange its pattern bands still closes in; in reach its weapons fight.
- **Telegraphed attacks** work for every unit: bosses and mobs start them, a unit ability can start one
  (`"ability"`), and Lua can (`encounters.cast`).

## Rules block

All times in seconds, distances in world units (cells), multipliers as numbers (1 = unchanged), fractions of max hp
as 0..1. Sizes must be positive (`encounters.attacks.rock.length: must be positive (line)`). Unknown ids fail with readable errors (`encounters.bosses.golem.phases[1].pattern: unknown attack 'kick'`).

```json
"statuses": {   // ordinary rules statuses: what an attack applies
  "chilled": {"mods": [{"stat": "speed", "mul": 0.55}]},
  "stunned": {"flags": ["stun"]},
  "burning": {"tick": {"damage": 5, "interval": 0.5}, "stacking": "independent", "maxStacks": 3},
  "rooted":  {"flags": ["root"]}
},
"encounters": {
  "attacks": {
    "slam":  {"shape": "circle", "anchor": "aim", "radius": 2.2, "maxRange": 7, "windup": 1.0, "recovery": 0.6,
              "cooldown": 3, "damage": 90, "damageMin": 45, "falloff": true,
              "statuses": [{"status": "stunned", "duration": 0.6}]},
    "swipe": {"shape": "cone", "radius": 3.2, "angle": 120, "windup": 0.7, "damage": 70, "maxRange": 3},
    "wave":  {"shape": "ring", "radius": 6, "inner": 3.5, "windup": 1.3, "damage": 60, "statuses": ["chilled"]},
    "charge": {"shape": "line", "length": 9, "width": 2.4, "windup": 1.1, "active": 0.5, "sweep": true, "damage": 80},
    "blast": {"shape": "circle", "anchor": "aim", "radius": 2, "windup": 0.3, "damage": 60, "ability": "missile"}
  },
  "bosses": {
    "golem": {
      "engageRange": 13, "loseRange": 40, "approach": 2.4, "immune": ["stunned", "rooted"], "archetype": 1,
      "phases": [
        {"mode": "byRange", "globalCooldown": 0.6,
         "pattern": [{"attack": "swipe", "maxRange": 3}, {"attack": "slam", "maxRange": 7}]},
        {"hpBelow": 0.66, "transition": 1.5, "damage": 1.2, "speed": 1.1, "cooldown": 0.85,
         "pattern": ["wave", "slam"],
         "adds": [{"unit": "crawler", "count": 3, "every": 15, "maxAlive": 6, "minRadius": 2, "maxRadius": 4}]},
        {"hpBelow": 0.33, "after": 120, "pattern": [{"attack": "charge", "minRange": 4, "maxRange": 10}]}
      ],
      "enrage": {"after": 150, "hpBelow": 0.05, "damage": 2, "speed": 1.4, "cooldown": 0.6}
    }
  },
  "mobs": {
    "crawler": {"aggroRange": 9, "loseRange": 14, "stopDistance": 1.1, "wanderMin": 1.5, "wanderMax": 4,
                "wanderDelay": 1.5, "wanderLeg": 5, "leash": 0, "fleeBelow": 0.15, "fleeRecover": 0.5,
                "fleeRange": 5, "fleeTime": 2, "attack": "bite", "think": 0.2, "flowDistance": 10}
  },
  "schedule": {
    "personal": true, "rotation": false, "archetypes": 0, "spawnDistance": [8, 12],
    "windows": [{"start": 60, "end": 120, "breather": 20, "pauseWaves": true, "excludePrevious": false,
                 "variants": [{"boss": "golem", "weight": 1, "archetype": 1}]}]
  }
}
```

### `attacks.<id>`

| Key | Meaning |
|---|---|
| `shape` | `circle`, `cone`, `line` or `ring` (required) |
| `radius` / `inner` / `angle` | circle, cone and ring size; `inner` = ring hole; `angle` = full cone opening in degrees |
| `length` / `width` | line (rectangle from the origin forward) |
| `anchor` | `source` (default): at the caster, facing the aim, plus `offset` [right, forward]; `aim`: centred on the aimed point clamped to [`minRange`, `maxRange`] |
| `windup`, `active`, `recovery` | telegraph timing; the first active tick strikes. The caster stands still in windup and recovery |
| `interval` | > 0: strike again every interval while active (`oncePerTarget` false to hit the same unit again) |
| `cooldown` | own cooldown after the attack ends (bosses, mobs) |
| `damage`, `damageMin`, `falloff`, `damageType` | damage (pure unless `damageType`), with falloff to `damageMin` at the edge |
| `statuses`, `statusDuration` | rules statuses applied to every unit struck: ids (with `statusDuration`, default 2) or `{"status", "duration"}` |
| `trackAim`, `sweep`, `cancelOnDeath`, `friendlyFire` | re-aim during windup; line that grows over `active` and carries the caster (charges); the caster dying in the windup cancels it (default true); hit allies too |
| `minRange`, `maxRange` | aim clamp; for mobs `maxRange` (or the area's extent) is the reach to start it |
| `ability` | a unit ability with this id starts the attack toward its point (the ability keeps its own cooldown, cost and effects) |

### `bosses.<unit type>`

`engageRange` (a hostile this close starts the fight), `loseRange` (the target is dropped beyond it), `approach`
(walks to this distance from its target), `immune` (rules statuses encounter attacks don't apply to it), `archetype`
(schedule rotation), `phases` (first = start; later phases need `hpBelow` and / or `after` seconds since the fight
began), `enrage` (`after` seconds and / or `hpBelow`: multipliers on top of the phase).
Per phase: `pattern` (`mode` `byRange` = first ready entry whose band [`minRange`, `maxRange`] contains the target
distance, `sequence`, `weighted` with `weight`; `alternate` = every second use starts that attack instead),
`damage` / `speed` / `cooldown` multipliers, `globalCooldown` after every attack, `transition` (stands still, no
attacks), `adds` waves (`unit`, `count`, `every` seconds (0: once at phase start), `maxAlive`, `minRadius`,
`maxRadius` ring around the boss). Adds of a mob type get mob behaviour; adds of a personal boss prefer its
participant's hero.

### `mobs.<unit type>`

Wander legs in [`wanderMin`, `wanderMax`] around the spawn point with `wanderDelay` pauses (a grid line test picks
reachable points), hunt the nearest hostile within `aggroRange` (kept to `loseRange`; `switchMargin` before
switching to a nearer one), flee below `fleeBelow` of max hp until `fleeRecover` / `fleeTime` / out of `fleeRange`,
return home beyond `leash`. Swarm pressure (`spacing`, `correction`, `lookAhead`, `neighbours`) shifts the goal
around the crowd. Decisions become ordinary orders every `think` seconds: attack (armed units), attack-move along
a shared flow field when the target is farther than `flowDistance` (rules `movement.flowField`), move (wander,
flee, return, unarmed hunters to `stopDistance`). `attack`: a telegraphed attack used within its reach.

### `schedule`

Encounter windows (`start`, `end`, `breather`, `pauseWaves`, `excludePrevious`, `variants` of boss types with
`weight` / `archetype`). `personal` (default): one boss per participant (alive non-monster players with a unit;
the top-down hero is the anchor), spawned by the first monster player on a free cell `spawnDistance` [min, max]
from the participant; else one shared boss. `rotation` with `archetypes`: every archetype once before a repeat,
never the last one twice. A request not placed by the window's `end` expires. Lua `encounters.waves_allowed(p)`
is the "ordinary waves now?" gate (false while the participant's boss is pending or alive with `pauseWaves`, and
during the breather after it died).

## Presentation

Telegraphs draw as ground decals (background shape + a fill growing with the windup; own team blue, enemies red)
and units with a slow / root / stun / burn status get coloured rings (blue, green, yellow, orange), both through
the ground canvas of the view (`modules/rts/view/EncounterDecals.*`, hooked into the client's ground pass, also
the editor's Game window). The rings follow the status kind derived from the rules status (stun / root flags, a
speed multiplier below 1, positive tick damage).

## Lua

Events (`on(name, fn(e))`, e = {player, unit, other, value, x, z}):

| Event | unit / other / value |
|---|---|
| `boss_engaged` | boss / target / boss type index |
| `boss_phase` | boss / target / phase index (0 = first) |
| `boss_enraged` | boss / target / boss type |
| `boss_adds` | boss / - / unit type \| count << 16 |
| `boss_defeated` | boss / - / boss type (player = owner of the last damage source) |
| `boss_window` | boss (spawned) / participant anchor / window index \| state << 16 (0 opened, 1 spawned, 2 expired) |
| `telegraph_start` | caster / aimed unit / attack index |
| `telegraph_strike` | caster / unit struck / attack index \| damage << 16 |
| `telegraph_end` | caster / aimed unit / attack index \| 1 << 16 when cancelled |
| `status_start` | the engine's own status event: statuses applied by attacks arrive here |

Table `encounters`: `active()`, `bosses()`, `boss(unit)` -> {phase, enraged, engaged, defeated, participant,
target}, `mob(unit)` -> idle | wander | hunt | flee | return, `telegraphs()` -> {attack, caster, x, z, yaw, phase,
progress 0..1000}, `waves_allowed(player)`, and in simulation scripts `cast(unit, attack, x, z [, target])`.

## Determinism, saves, rollback

State is the `EncounterStore` world resource (boss brains, mob brains, adds, running telegraphs, schedule), created
only in games with the block: rollback snapshots copy it, `hashState` folds its canonical bytes (the library's
versioned records) into the checksum, `RtsSave` saves it. Fixed point and integers only; loops in entity order;
randomness from `sim.rng()`. Games without the block keep their checksums and save layout.

## Hooks (shared files, additive)

`RtsModule.h` (include, events appended to `GameEvent`, member, accessor), `RtsModule.cpp` (setup, `encounters`
system before `raid` / `death`, speed stat hook, hash), `RtsSave.cpp` (io), `RtsCombat.cpp` (`castAbility` ->
`onAbility`), `RtsAI.cpp` (monster players skipped), `RtsScript.cpp` (event names), `RtsScriptApi.cpp` /
`RtsScriptInternal.h` (`encounters` table + reference), `apps/client/GameClient.cpp` (ground decals),
`CMakeLists.txt` (sources, `AIVERSE::lib_boss_encounters`), `tests/tests.cpp` (include).
