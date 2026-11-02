# Scripting (Lua)

Mechanics that the data blocks of `rules.json` can't express are written as **Lua 5.4 scripts** in
the game folder. Scripts are content: they are read when a match starts, packed into
`content.rpak` like any other file, edited in the editor with hot reload, and the engine never has
to be rebuilt.

There are two kinds of scripts:

| | simulation scripts | view scripts |
|---|---|---|
| listed in | rules / map `"scripts": ["scripts/x.lua"]` | rules / map `"viewScripts": ["scripts/view/x.lua"]` |
| runs | inside the deterministic tick, on every machine of a match, in replays | on the local machine only, every frame |
| numbers | integers and `fx` (fixed point); **no floats** | normal Lua numbers |
| may | read and change the game (`game.*` actions) | read the game, show notices / effects / sounds |

A game without `"scripts"` runs only the engine's standard library (below): no "scripts" system, no budget, identical checksums.

```json
// rules.json
{
  "scripts": ["scripts/rules.lua", "scripts/abilities.lua"],
  "scriptBudget": 1000000,            // optional: VM instructions per tick for all simulation scripts
  "types": {
    "archer": {"abilities": [{"id": "mend", "target": "none", "cooldown": 20, "autocast": true,
                "effects": [{"type": "script", "fn": "chain_heal", "jumps": 3, "amount": 60}]}]}
  }
}
```

Sample: `templates/scripted` (Iron and Gold + a war chest, veterans, a scripted chain heal and a
custom defeat condition).

## A first script

```lua
-- scripts/rules.lua
local util = require("lib.util")            -- scripts/lib/util.lua (only files of the game)

function on_start()                          -- global on_<event> functions are handlers
  state.kills = {}                           -- persistent data lives in `state`
  game.message(0, "Hold the bridge!")        -- 0 = every player
end

-- every 30 seconds the player with the most units gets 50 gold
every(30, function(tick)
  local best, most = nil, -1
  for _, p in ipairs(game.players()) do
    local n = game.count{owner = p, kind = "unit"}
    if game.player_alive(p) and n > most then best, most = p, n end
  end
  if best then game.give(best, "gold", 50) end
end)

on("unit_died", function(e)
  state.kills[e.owner] = (state.kills[e.owner] or 0) + 1
  if state.kills[e.owner] >= 50 then game.defeat(e.owner) end
end)
```

## Determinism rules (simulation scripts)

Lockstep multiplayer and replays re-run the same scripts on every machine and must get bit-identical
results on Windows, macOS, iOS, Android and the web. The engine enforces most of it; the rest is a
small contract.

### Numbers: integers and `fx`

Lua 5.4 has integers and floats. Floats can differ between platforms, so a simulation script never
sees one:

1. **Float literals are rejected when a file loads** — `x * 1.5`, `1e3`, `.5`, `0x1p4` give
   `scripts/x.lua:12: float literal 1.5 - use fx("1.5") or an integer`.
2. **Float arithmetic raises an error** — `/` and `^` always produce floats in Lua, so `7 / 2`,
   `n / 2`, `2 ^ 3` fail with *"float arithmetic is not allowed in simulation scripts (use fx()
   numbers, or // for integer division)"*. Use `7 // 2` (integer division, = 3) or `fx(7) / 2`
   (= 3.5). The guard sits inside the VM (`third_party/lua/rts_luaconf.h`), so it also catches
   floats made from strings (`"1.5" + 1`).
3. **Every API function that takes a number accepts only integers or `fx`**; a float gives
   *"got a float - use fx() or integers"*.

`fx` is the engine's fixed-point number (Q47.16, `engine/core/Fixed.h`), the same type all
gameplay math uses:

```lua
local a = fx(3)              -- 3
local b = fx(3, 4)           -- 0.75  (3 / 4)
local c = fx("1.25")         -- 1.25  (decimal string, parsed with integers only)
local d = a * b + 2          -- fx mixes with integers: + - * / // % unary -
if d >= 4 then ... end       -- < <= > >= work between fx and integers
print(d, tostring(c))        -- "4.25"  (formatted without floats)
fx.sqrt(d) fx.abs(x) fx.min(a, b) fx.max(a, b) fx.lerp(a, b, t)
fx.floor(d) fx.ceil(d) fx.round(d) fx.int(d)   -- -> integers (also d:floor() ...)
fx.is(v)  math.type(v) == "fx"
```

**Pitfall:** `fx(1) == 1` is `false` — Lua only calls `__eq` when both values are tables or
userdata. Compare `fx` with `fx` (`v == fx(1)`) or use `<`, `<=`, `>=`.

Values from the game are `fx` when they can be fractional (positions, hp, resources, time) and
integers when they can't (ticks, counts, players, levels, handles). `math` keeps `abs`, `min`, `max`,
`floor`, `ceil`, `sqrt` (→ fx), `tointeger`, `type`, `maxinteger`, `mininteger` and
`math.random` (simulation RNG: `math.random(n)`, `math.random(a, b)` integers, `math.random()` an
fx in [0, 1)). `tonumber("12")` works; `tonumber("1.5")` is an error (use `fx("1.5")`).

### IEEE floats as bit patterns: `f32`, `f64`

A script that must reproduce another program's floating-point results exactly (a port of a game that
simulates in `float`) keeps each value as its IEEE bit pattern in an integer: binary32 as `0..0xffffffff`,
binary64 as the signed 64-bit reinterpretation (`BitConverter.DoubleToInt64Bits`). The `f32` and `f64`
tables do one correctly rounded IEEE operation per call (round to nearest even; NaN results are the
canonical quiet NaN `0x7fc00000` / `0x7ff8000000000000`), so results are identical on every platform:

| | |
|---|---|
| `f32.add/sub/mul/div(a, b)`, `f32.sqrt(a)` | binary32 arithmetic |
| `f32.dot2(a, b, c, d)` | `a*b + c*d` rounded once (fused two-term dot product) |
| `f32.fromint(i)`, `f32.tof64(a)` | integer → binary32 (rounded); binary32 → binary64 (exact) |
| `f32.lt/le/eq(a, b)` | IEEE comparisons (NaN unordered, `-0 == +0`) |
| `f32.fdot2(a, b, c, d)` | `dot2` of finite operands (a non-finite operand is an error) |
| `f32.sqdist(p, q)` | `fdot2(x, x, y, y)` of `x = p.x - q.x`, `y = p.y - q.y` (tables with `x`, `y`; each difference rounded) |
| `f32.floorint(a)` | floor as an integer; `a` finite with `|a| < 2^31`, otherwise an error |
| `f32.atan2(y, x)`, `f32.sincos(a)` | binary32 atan2 / sine and cosine: an exact series in 96-bit fixed point rounded once (sticky bit), as an integer-only port computes them; `|a| <= 2^33`, finite operands, otherwise an error |
| `f32.cellat(p, w, h)` | grid cell `y*w + x` of `x = floorint(p.x)`, `y = floorint(p.y)`, or `-1` outside `0..w-1 × 0..h-1` |
| `f64.add/sub/mul/div(a, b)`, `f64.sqrt(a)`, `f64.fromint(i)`, `f64.tof32(d)`, `f64.lt/le/eq(a, b)` | the same for binary64 |

Arguments must be integers (`f32` ones in range), otherwise the call is an error. Both tables exist in
simulation and view scripts. (Built on the host's IEEE operations with one operation per call, no FMA
contraction and a check of the default floating-point environment: `modules/rts/RtsScriptIeee.cpp`.)

### State: `state`, `game.data(h)`, not upvalues

Everything a script remembers between ticks must be in the global table **`state`**, in an
entity's own table **`game.data(h)`**, or in trigger variables (`game.set_var`). The engine writes
them into the simulation canonically (sorted keys) **when the world is observed** — before a
checksum, a rollback snapshot or a save — not after every tick: they are hashed into the checksum
(desync detection), copied by rollback snapshots, saved, and read back into the VM when the
netcode rewinds or a save is loaded. A tick that only changes Lua tables costs nothing until then.

- **`game.data(h)`** returns the entity's own table (created on first use, gone when the entity
  dies, kept by `transform`). Only tables handed out since the last write are serialized again, and
  only changed ones are re-hashed, so per-unit records belong here, not in one big `state` table:
  ```lua
  local d = game.data(h)            -- {} the first time
  d.morale = (d.morale or 10) - 1
  for _, h in ipairs(game.units{has = "morale"}) do ... end   -- entities whose table has the key
  game.has_data(h, "morale")  game.clear_data(h)
  ```
  View scripts get a read-only copy of the stored table.
- Allowed in `state` and `game.data`: integers, `fx`, strings, booleans and nested tables whose keys
  are integers or strings. A value that can't be stored (function, float, table key, cycle) is left
  out of the saved state and logged; keep such values out of persistent tables.
- **Globals are read-only after loading**: assigning a new global in a handler is an error, reading
  an undefined one too (typos show up immediately). Define functions and constants at the top level.
- **Don't change file-level `local` variables from handlers** (`local count = 0` ... `count = count + 1`
  in a handler): such state isn't part of the snapshot, so a rollback can't restore it. Use `state`.
  Constants and helper functions as locals are fine.
- Don't keep references to `state` sub-tables or `game.data` tables in locals across ticks (a rollback
  replaces them); call `game.data(h)` again.

### Order and randomness

- `pairs()` iterates in **sorted key order** (booleans, integers, then strings), never in hash order,
  so loops over tables are deterministic. `sorted_pairs` is the same function. `next` is not
  available (iteration order would leak). Keys of other types can't be iterated with `pairs`.
- `game.units{...}` returns handles sorted by entity index.
- Randomness only from `math.random` / `game.random` (the simulation RNG).
- `tostring` never shows addresses (`"table"`, `"function"`), `string.format("%p")` is refused.
- String hashing and `table.sort` pivots use constant seeds (identical on all machines).
- Not available: `os`, `io`, `debug`, `package`, `load`, `loadfile`, `dofile`, `collectgarbage`,
  `string.dump`; `setmetatable` refuses `__gc` and `__mode` (they depend on garbage collection).

### Budget and errors

- All simulation scripts together get `scriptBudget` VM instructions per tick (default 1,000,000).
  An endless loop is stopped with *"instruction budget exceeded"* — even inside `pcall` — and the
  game goes on; handlers that didn't get to run in that tick are skipped once.
- Any error is logged with `file:line` (headless output, editor **Console** and **Скрипты** windows,
  MCP `play_status.scriptErrors`, `script_check`) and **disables that handler** for the rest of the
  match (deterministically: the flag is simulation state). Functions called from data
  (`"fn"` of effects and trigger hooks) are disabled the same way.
- Loading errors (syntax, float literals, a missing file) skip that file; the match still starts.
- Script cost per tick: `rts_headless --tick-times FILE` writes the wall time of every simulation step
  (`tick microseconds` per line).

## Events

Handlers are registered at the top level of a file (while it loads):

| registration | called | argument |
|---|---|---|
| `on("start", fn)` / `function on_start()` | once, first tick | — |
| `on("tick", fn)` / `on_tick` | every tick | tick |
| `every(seconds, fn)` | every N seconds (seconds: integer or fx) | tick |
| `on("unit_died", fn)` / `on_unit_died` | a unit or structure died | `e.unit` (dead handle), `e.owner`, `e.unit_type`, `e.x`, `e.z`, `e.killer` (last damage source, may be dead too), `e.killer_owner` (when the killer lives) |
| `on("spawned" \| "built" \| "trained" \| "upgraded", fn)` | created / construction done / trained / upgraded | `e.unit`, `e.owner`, `e.unit_type` (`trained`: `e.producer`) |
| `on("researched", fn)` | a tech finished | `e.player`, `e.tech`, `e.producer` |
| `on("captured", fn)` | changed owner | `e.unit`, `e.owner`, `e.old_owner`, `e.by` |
| `on("player_defeated", fn)` | a player is out | `e.player`, `e.surrendered` |
| `on("ability", fn)` | a unit used an ability | `e.caster`, `e.player`, `e.ability`, `e.index`, `e.target`, `e.x`, `e.z` |
| `on("damage", fn)` | damage dealt | `e.unit`, `e.source`, `e.player` (source owner), `e.amount` (fx) |
| `on("level_up", fn)` | a hero levelled | `e.unit`, `e.player`, `e.level` |
| `on_area(name, "enter" \| "leave", fn)` | a unit entered / left a map area (map `"areas"`, checked every 5 ticks) | `e.unit`, `e.owner`, `e.area`, `e.x`, `e.z` |
| `game.after(seconds, "fn_name", arg)` | once, later (a timer kept in the simulation state) | `arg` (storable like `state`) |

Every event table also has `e.event`, `e.tick`, `e.player`. Scripts run in the **scripts** phase at
the end of the tick (after triggers) and see every event of that tick.

**Every other simulation event** works with `on(name, fn)` too and gets a generic table `{event,
player, tick, x, z, unit (a), other (b), value}` plus `unit_type` (spawn / train / build / upgrade
events), `status` (`status_start`, `status_end`), `resource` (`gathered`, `delivered`, `produced`),
`amount` (`damage`), `text` (`message`). Names: `attack hit miss train_started build_placed gathered
not_enough ready_to_place sold region_claimed wave low_power message dug territory room_painted
recruited creature_left picked_up dropped slapped possessed payday player_ability produced delivered
road_built lanes_changed status_start status_end area_start area_trigger area_end projectile_launch
impact chain_jump cast_start channel_start channel_end alert garrison_changed transit_start
transit_end trigger_fired loaded unloaded cycle_phase traded diplomacy item merged objective dialog
camera mission_end learned crew_killed crewed infected submerged trap_fired group_teleport paved
morale turn` (what `unit` / `other` / `value` mean per event: `GameEvent` in modules/rts/RtsModule.h).
View scripts get the same names with `view.on` (read-only).

**Before-hooks** — global functions the engine calls *before* something happens, in the middle of
the tick (so keep them short; they share the tick budget and don't nest: damage dealt from inside a
hook doesn't call `before_damage` again):

| function | called | return |
|---|---|---|
| `before_damage(e)` | every hit, after armor / mitigation: `e = {unit, source, player, amount, damage_type}` | the new amount (fx / integer, >= 0); nothing = unchanged |
| `before_command(player, c)` | every built-in player command: `c = {type, units, target, x, z, param, queue}` (`type` as in `game.command`) | `false [, reason]` drops it |
| `can_train(player, type, producer)` | a train command passed the engine's checks | `false [, reason]` refuses |
| `can_build(player, type, x, z, builder)` | build order / sidebar placement | `false [, reason]` refuses |
| `can_cast(caster, ability, target, x, z)` | an ability is about to fire (commands, autocast, AI) | `false [, reason]` refuses |

A `reason` string is shown to that player like `game.message`. Games without these functions pay
nothing (the module checks a flag).

## The `game` table

Entities are integer **handles**. A handle of a dead or removed unit is harmless: queries return
`nil` / `false`, actions do nothing.

Queries (simulation and view scripts):

| function | result |
|---|---|
| `game.tick()`, `game.time()`, `game.tick_rate()` | tick, seconds (fx), ticks per second |
| `game.players()` | player slots `{1, 2, ...}` |
| `game.player(p)` | `{slot, team, alive, name, faction, supply, supply_cap}` |
| `game.player_alive(p)`, `game.team(p)`, `game.hostile(a, b)` | |
| `game.resource(p, "gold")`, `game.resources(p)` | fx / `{gold = fx, ...}` |
| `game.units(filter)`, `game.count(filter)`, `game.nearest(x, z, filter)` | handles sorted by index / count / nearest handle |
| `game.unit(h)` | `{id, type, name, owner, x, y, z, hp, max_hp, energy, structure, under_construction, level, garrison, tags, statuses}` |
| `game.alive(h)`, `game.owner(h)`, `game.type_of(h)`, `game.pos(h)` → x, z, `game.hp(h)` → hp, max | |
| `game.stat(h, "damage")`, `game.has_status(h, "slow")` | effective stat (techs, buffs, statuses) |
| `game.type_info("footman")`, `game.types()` | `{id, name, structure, max_hp, speed, armor, cost, tags}` |
| `game.map_size()` → w, h, `game.passable(x, z [, "ground"\|"air"\|"naval"])`, `game.level(x, z)`, `game.height(x, z)`, `game.area(name)` | map |
| `game.var(name)` | trigger variable (fx) |
| `game.data(h)`, `game.has_data(h, key)` | the entity's own persistent table (view: read-only copy) / does it have the key |
| `game.orders(h)`, `game.current_order(h)` | the order queue / its first order: `{order, x, z, target, param, state}` (`order`: `move attack_move attack patrol gather return build repair enter cast dig claim_cell need haul board unload follow rearm trade pick_item merge`) |
| `game.target(h)`, `game.idle(h)` | weapon target (else the current order's target) / no orders, not moving, not fighting, nothing in production |
| `game.facing(h)` → dx, dz, `game.velocity(h)` → vx, vz, `game.path(h)`, `game.energy(h)` → energy, max | `path`: remaining waypoints `{{x, z}, ...}` ending at the goal |
| `game.log(...)`, `print(...)` | script log |

Filter: `{owner = 2, type = "footman", tag = "mechanical", kind = "unit" | "structure", area = "gate", x = 10, z = 12, radius = 6, has = "morale", sort = "id" | "dist", limit = 5}` (all optional; `has`: the entity's `game.data` table has that key; `sort = "dist"`: nearest to x, z first, ties by handle).
Radius queries use a bucket grid of the living units, built on the first radius query and kept while
the script only reads (any action, or C++ running in between, drops it): many `game.units{x, z, radius}`
calls in one handler cost one pass over the units plus the buckets they touch. Results are exactly
those of a full scan (same order).

Actions (simulation scripts only):

| function | |
|---|---|
| `game.give(p, res, amount)`, `game.take(p, res, amount)` → taken | resources |
| `game.spawn(type, owner, x, z)` → handle or nil | owner 0 = neutral, 255 = hostile creeps |
| `game.attack_event(actor, target)` | emit the standard attack animation/FX/audio cue for scripted combat; no damage, projectile or order; false for invalid/dead handles; simulation only |
| `game.kill(h [, silent])`, `game.damage(h, amount [, source [, damageType]])`, `game.heal(h, amount)` | |
| `game.set_owner(h, p)` | ownership change: per-type counts move, production / orders / target are cleared, `captured` fires |
| `game.set_pos(h, x, z [, exact])` (alias `game.teleport`) | mobile units only: the nearest passable cell (unless `exact` or flying), path dropped; false when it can't be placed |
| `game.set_facing(h, dx, dz)`, `game.impulse(h, dx, dz)`, `game.knockback(h, x, z, distance)` | turn; push by a vector / away from a point (negative: towards), stopped by terrain in 8 steps |
| `game.set_hp(h, hp)`, `game.set_energy(h, e)` | clamped to [0, max]; hp 0 = a normal death |
| `game.set_stat_base(h, stat, value \| nil)` | replaces the type's base value of a stat (`maxHp speed armor sight range ...`) for this unit; techs, levels, buffs apply on top; `maxHp` keeps the hp fraction; nil restores |
| `game.transform(h, type)` | the unit becomes another type in place (same handle, `game.data`, hp fraction); unit ↔ structure is refused |
| `game.add_status(h, "status" [, seconds [, caster]])`, `game.buff(h, stat, add, mul [, seconds])` | statuses of rules `"statuses"`; stat modifiers |
| `game.order(units, "move"\|"attack"\|"patrol"\|"stop"\|"hold", x, z \| target)` | through the normal command path |
| `game.command(p, {type = "train", units = {h}, param = "footman", x =, z =, target =, queue =})` | any player command, validated like network input (see below) |
| `game.cast(h, ability, target \| x, z)`, `game.player_ability(p, id, x, z [, free])` | abilities |
| `game.set_var(name, v)`, `game.add_var(name, v)` | trigger variables, shared with rules triggers |
| `game.message(p, text [, x, z])` | a notice (p = 0: everyone) |
| `game.victory(p)`, `game.defeat(p)`, `game.draw()` | end conditions |
| `game.clear_data(h)` | forget the entity's `game.data` table |
| `game.research(p, tech)`, `game.enable_type(p, type, on)`, `game.add_score(p, n)`, `game.enable_trigger(id, on)` | |
| `game.random(n)`, `game.random(a, b)` | simulation RNG |

`game.command` types (all player commands of the engine): `move attack stop hold patrol gather build
train cancel rally smart send research place sell deploy ability reinforce claim auto_build upgrade
surrender player_ability dig paint_room pick_up drop slap possess direct build_road connect_road transit
stance retreat load unload guard follow formation build_line merge split trade trade_route tribute
diplomacy buy_item use_item drop_item pick_item learn bounty rotate lock end_turn`. `param` is a number
or a name: a type (`train build upgrade build_line`), a tech (`research`), a player ability, a stance
(`aggressive defensive hold passive`) or a formation (`line box wedge loose none`). Packed params can
be given by field instead: rect commands (`dig paint_room build_road`) `w, h, extra`; `build_line`
`end_x, end_z` (cells); `trade` `resource, sell`; `tribute` `resource, to, amount`; `diplomacy`
`other, ally`; `bounty` `resource, reward`.

Not available yet: fog reveal from scripts (use a `reveal` effect in an ability).

### Grid fields: the `field` table

Per-cell layers (fire, water, creep, influence...) defined in rules `"fields"` run as C++ kernels; scripts
read and write them: `field.get(id, x, z [, player])`, `field.set` / `field.add` / `field.fill_rect` /
`field.fill_circle`, `field.sum` / `field.max` / `field.count` / `field.find` over a rectangle or disc, and
`on_field_threshold(id, value, fn [, "rise" | "fall"])` hooks. Values are integers (`int` fields) or `fx`
(`fixed`). View scripts get the read-only part. Reference: `docs/fields.md`.

## Data hooks: scripts inside rules

```json
{"type": "script", "fn": "chain_heal", "jumps": 3, "amount": 60}
```

- **Ability effect** (any effect list: abilities, projectiles, areas, statuses...): `fn(ctx)` with
  `ctx = {caster, owner, target, x, z, scale, source_type, targets, args}` — `targets` are the
  handles the effect's filter selected (`radius`, `affects`...), `args` is the effect's JSON. It runs
  in the scripts phase of the same tick.
- **Trigger action** `{"type": "script", "fn": "f", ...}`: `fn(ctx)`, `ctx = {player, unit, x, z,
  tick, trigger, args}`.
- **Trigger condition** `{"type": "script", "fn": "f", ...}`: `fn(ctx)` returns true / false.

Functions named in data that the scripts don't define are reported when the match loads.

## Territory brushes and scripted formations

These APIs let games implement front lines in Lua without adding game-specific C++ behavior.
Simulation arguments use integers or `fx`; persistent assignments belong in `state`.

| API | Contract |
|---|---|
| `game.territory_at(x,z)` | Ground cell owner, including neutral `0`; `nil` outside ground. Flat maps only. |
| `game.territory_count(player)` | Number of owned cells; uses the same counters as victory and the HUD. |
| `game.frontier(player)` | Array of unit-length land boundary segments, sorted by cell index then east/south/west/north edge. Each `{x,z,nx,nz,owner,cell}` has an `fx` midpoint, integer outward normal, neighbouring owner and zero-based neighbouring cell index. Excludes the sea and map rim. |
| `game.order_target(unit)` | `x,z` for the first move/attack-move order; otherwise `nil`. Read this before consuming the order with `set_position`. |
| `game.paint_territory(player,x,z,radius,connected=false)` | Change ground cells whose **centres** lie inside the disk, returning the changed count. Radius must be 0..64, player must exist (0 clears ownership), and `rules.territory` must be enabled. Off-map centre returns 0. With `connected=true`, candidate cells must touch ownership present before this brush call. Uses normal territory events, counters, room invalidation and save/hash state. |
| `game.set_position(unit,x,z)` | Place a living mobile unit on flat-map ground; returns false for stale handles, structures, water or out-of-map destinations. Stops native movement, clears queued orders and preserves `prevPos` for frame interpolation. Does not pathfind, avoid other units or test intermediate terrain. The game script chooses legal trajectories. |

`territory_at`, `frontier`, `paint_territory` and `set_position` currently reject planet maps
with an explicit error. Queries are available in view scripts; the two mutation methods are not.
`frontier` scans the map: cache its result in `state` and refresh a few times per second, not once
per soldier. Read endpoints and distribute slots by length, then match soldiers to nearby free
slots in deterministic handle order. Move a bounded distance per tick; paint only after successful
placement. User destinations must originate in regular commands, obtainable through `order_target`.

```lua
local x,z = game.pos(unit)
if game.set_position(unit, x + fx(1,20), z) then
  game.paint_territory(game.owner(unit), x + fx(1,20), z, fx("0.8"), true)
end
```

Use `movement.avoidance: "none"` to disable both ORCA and body separation for formations;
body radii retain their meaning for range queries. Smooth rendering is optional and independent
of simulation: `game.json` → `terrain.territory: {smooth:true, transition:0.2, fill:0.4}`.
Ownership remains deterministic cell data; the view reconstructs a soft contour and animates it.
The view never writes ownership or decides victory.

Validation for this API: `rts_tests script_territory_brush_frontier_and_placement` checks brush
counts, overwriting ownership, the connected-brush restriction, frontier segment counts,
invalid placement, overlapping units with `avoidance:none`, and snapshot replay equivalence.
The full macOS suite passed 4967 checks. A 3600-tick project battle replay matched all checkpoints.
The 78 template/fixture checksum runs retained current HEAD behavior: the checked-in baseline
contains duplicate obsolete `lastcolony` entries and stale `planets` sums; all six relevant seed
results were reproduced with an independently built unmodified `fd300f2`. The baseline was not
rewritten. Metal visuals were inspected; GL/Android/web were not exercised in this change.
## Script commands (player input with data)

A game defines its own player commands in rules; each becomes a button (with hotkey) on the command
card of the client and of the editor play-test, can be sent from view scripts and through MCP
(`play_command` with `command: "script"`), and arrives in the simulation as one generic command
(`kCmdScript`) that the engine validates before the game's handler sees it.

```json
"commands": {
  "rally_cry": { "label": "Rally cry", "hotkey": "Y", "tip": "+25% damage for 10 s",
                 "targets": ["footman", "archer", "tag:infantry"], "cost": { "gold": 25 }, "cooldown": 30 },
  "muster":    { "label": "Muster", "hotkey": "U", "targets": ["footman"], "cooldown": 5,
                 "args": [{ "name": "where", "type": "point" },
                          { "name": "pace", "type": "enum", "values": ["walk", "run"], "default": "run" },
                          { "name": "count", "type": "int", "min": 1, "max": 20, "default": 5 }] }
}
```

```lua
function on_command(player, id, args)      -- or per command: "fn": "global_function_name"
  if id == "muster" then game.order(args.units, "move", args.where.x, args.where.z) end
  -- return false: refused (cost and cooldown are not taken)
end
```

| key | |
|---|---|
| `label`, `icon`, `hotkey` (one letter / digit), `tip` | the button; script hotkeys win over the built-in ones (A, S, H, P) |
| `targets` | unit types / `"tag:x"` / `"any"`: shown when the selection has such an own unit, and only those units reach the handler (`args.units`, sorted by handle); without `targets` the command needs no selection |
| `args` | array `[{name, type, ...}]` (or an object `{name: {type, ...}}`): `int` (`min`, `max`, `default`), `enum` (`values`, `default`), `bool`, `point` (`{x, z}` in the handler), `unit` (a living unit; `own: true` = of the sender), `entity` |
| `cost`, `cooldown` | resources taken and cooldown (seconds, per player) started when the handler accepts; while unaffordable / cooling down the command is dropped (the card shows the remaining seconds) |
| `fn` | handler function (default `on_command`) |
| `mapClick` | `true`: a plain left click / tap on the map (normal input mode, nothing armed) sends this command with the clicked ground point as its first `point` argument; for games whose input is tapping the map (OpenWar). Needs a `point` argument and no `targets`; the first such command (by id) wins. Client and editor Game view; test `script_command_map_click` |

The first `point` / `unit` / `entity` argument is what a click picks: the button enters a targeting
mode and the next left click fills it in; the other arguments take their defaults (view scripts and
MCP pass them by name). Wire format: `param` = the command's index in id order, `args` = one integer
per value (points: x and z as raw fx), so replays and network play carry them. Invalid input (unknown
index, wrong count, out of range, dead or foreign handles, a point off the map) is dropped silently.
Each player's handlers have their own instruction budget per tick (rules `"commandBudget"`, default
200000): a player who sends endless loops only burns their own commands. `game.commands()` lists the
definitions, `game.command_cooldown(p, id)` gives the remaining seconds; view scripts send with
`view.send("muster", {where = {x = 10, z = 12}, pace = "walk", units = {h1, h2}})`.

## View scripts

```lua
-- scripts/view/hud.lua  (rules: "viewScripts": ["scripts/view/hud.lua"])
view.on("unit_died", function(e)
  if e.owner == view.player() then view.notice("We lost a " .. e.unit_type) end
end)
view.on_frame(function(dt) end)            -- dt in seconds (float)
```

View scripts are presentation: floats are fine, `game.*` queries work, actions don't exist.
`view.screen()` tells what the host shows over the match: `"playing"`, `"paused"` (the pause screen: the simulation
does not tick, view scripts keep running, so a game can draw its own pause panel) or `"over"` (the match has ended). Their
outputs are requests the host shows: `view.notice(text)`, `view.effect(name, x, z[, y])` (particle
effect of `fx/`), `view.sound(name, x, z)`, and script commands as the local player
(`view.send(id, args)`, above). They never change the simulation directly, so they don't need the
determinism rules.

### Sound: the `sound` table (view scripts and the UI Lua)

`view.sound(name, x, z)` plays a sound of `audio.json` fire-and-forget. A game that runs its own mixer (voice pools,
per-play gain / pan / pitch, fades, ducking — e.g. a port of Unity's AudioSources) uses the `sound` table instead;
the same table exists in view scripts and in the client's UI Lua (`ui/*.lua`), so one Lua module serves the menus and
the battle. Presentation only, never part of the simulation.

| call | meaning |
|---|---|
| `sound.play(name, {gain, pan, pitch, variant, at, loop})` → handle \| nil | starts a sound of the bank: `gain` multiplies its `volume`; `pan` -1..1 stereo balance; `pitch` multiplies its random pitch; `variant` 1-based file of `files` (default random, never twice in a row); `at` seconds into the clip; `loop` true / false overrides the sound's. nil when nothing plays (no audio device, headless, unknown sound, the sound's own `cooldown` / `maxInstances`) |
| `sound.set(h, {gain, pan, pitch})` | changes a playing voice (missing fields stay); `pitch` is the absolute rate. Call it every frame for fades / ducking: changes ramp over one mix block |
| `sound.stop(h [, fade])`, `sound.pause(h, on)`, `sound.seek(h, seconds)` | stop (fade seconds, default 0.02); pause holds the voice silent at its place; seek jumps within the clip |
| `sound.state(h)` → playing, time, paused | what the mixer reported last (one audio period behind; a start still queued counts as playing at its start time) |
| `sound.variants(name)`, `sound.length(name [, variant])` | number of files; length of one in seconds (0 unknown) |
| `sound.music(name \| nil [, fade])`, `sound.current_music()` | plays a playlist of `"music"` (crossfade `fade` seconds, default 1.5; a playlist that is still fading out comes back from where it is); nil stops |
| `sound.music_gain(g)` | script level of the music 0..4 (ducking), on top of the bus and the player's setting |
| `sound.settings()` → `{music, sfx, musicOn, sfxOn}` | the player's volumes 0..1 and on / off switches (client data model `music`, `sfx`, `musicOn`, `sfxOn`) |
| `sound.available()`, `sound.trace(text)` | a mixer runs; `trace` prints `audio\| text` on stdout when `RTS_AUDIO_LOG=1` (tools compare a game's sound decisions) |

With `"scripted": true` in audio.json the client never starts or stops music by itself (no `menu` / `match` /
`battle` / `victory` playlists) and starting or leaving a match stops only the `sfx` bus, so a click on a result
screen keeps ringing into the menu. OpenWar ports Unity's GameAudio this way (`scripts/openwar/audio/`). Tests:
`audio_voice_handles`, `audio_scripted_music_and_bus_switches`, `view_script_sound_table`.

## Script UI (view scripts drive in-match panels)

View scripts own a data model **`script`** in the client's RmlUi: they set values, open the game's
documents `ui/hud/<name>.rml` and receive their button events. Simulation state stays read-only:
buttons act through script commands (above), which go through the simulation like any input.

```lua
-- scripts/view/hud.lua
ui.open("war", {title = "War report"})                -- ui/hud/war.rml; params -> {{params.title}}
ui.on("hide", function(args) ui.close("war") end)      -- data-event-click="event('hide', 1)" -> args = {1}
view.on_frame(function()
  local s = view.state()                                -- read-only copy of the simulation's `state`
  ui.set("trained", (s.trained or {})[view.player()] or 0)   -- {{trained}}
end)
```

```html
<!-- ui/hud/war.rml: links ../theme.rcss, ../skin.rcss like the engine HUD; body ignores the mouse, panels take it -->
<div class="root" data-model="script">
  <div class="panel">{{params.title}}: {{trained}}
    <button data-event-click="send('rally_cry')">Rally cry</button>          <!-- rules "commands" -->
    <button data-event-click="send('muster', {where = {x = 10, z = 12}})">Muster</button>
    <button data-event-click="event('hide')">Hide</button>
  </div>
</div>
```

| view Lua | |
|---|---|
| `ui.set(key, value)` | a top-level value of model `script` (numbers, strings, booleans, fx, tables / arrays); unchanged values are not resent. A document binds its `{{names}}` when it loads: set every key it uses before (or in the same frame as) its first `ui.open` |
| `ui.open(name [, params])`, `ui.close(name)` | show / hide `ui/hud/<name>.rml` (only while the match is on screen); `params` -> `{{params.*}}` |
| `ui.on(event, fn(args))` | `event('name', ...)` in a document; `args` = the extra values as an array |
| `view.state()` | the simulation scripts' `state` as of this tick (a copy; writing to it changes nothing) |

`send('id', {...})` in a document sends a script command as the local player (the current selection
is added as `units` unless given). The engine part is `engine/ui/lib/scriptui.lua` (model, documents)
and `apps/client/ClientScriptUi.cpp` (bridge); the UI documents' general rules (styles, `@keys`,
sandbox) are in docs/ui-scripting.md. Sample: `templates/scripted` (`ui/hud/war.rml`).

Effect coordinates are world coordinates; optional `y` is absolute height (default 0.5,
preserving existing calls), not an offset from terrain. Coordinates must be finite.
Both client and editor play these one-shot requests through the particle system and its
quality budgets. View scripts must throttle repeated trail emissions and avoid replaying
historical bursts on load; requests are drained once per presentation update.

## Engine standard library (rule-level mechanics in Lua)

Part of the engine's own mechanics is written in Lua: `modules/rts/lua/*.lua`, compiled into the
engine (`embed.cmake` turns them into a header, so every platform loads them without file access)
and run by every game, before the game's own scripts, in the same deterministic VM. The C++ core
keeps everything hot (ECS, commands, movement, pathfinding, avoidance, targeting and weapons,
projectiles, vision, hashing, data parsing) and offers aggregate queries and primitives in the
low-level `rts` table (`modules/rts/RtsScriptStd.cpp`); Lua decides the rules.

| module | replaces (C++) | called |
|---|---|---|
| `victory.lua` (`std.victory`) | `RtsModule::sysVictory` | "victory" system, every 10 ticks (`std.period.victory`) |
| `waves.lua` (`std.waves`) | `RtsModule::sysWaves` (+ wave objective scan, kept as `rts.wave_objective`) | "waves" system, only in games with waves |
| `triggers.lua` (`std.triggers`) | `RtsModule::sysTriggers`, `triggerConditions`, `runTriggerActions` (parsing stays in `RtsTriggers.cpp`) | "triggers" system, only in games with triggers |
| `commands.lua` (`std.auto_build`, `std.sell`) | the `kCmdAutoBuild` and `kCmdSell` branches of `RtsModule::applyCommand` | from `applyCommand` |

Every module is **bit-identical** to the C++ it replaced: same iteration order (ECS order comes from
the C++ helpers), the same `Fixed` operations (`fx` metamethods call the engine's `Fixed`), the same
RNG calls. `tests/checksums/check.sh` compares the checksums of every sample game and of the fixtures
in `tests/checksums/games` (seeds 1-3) with `baseline.txt`, recorded from the C++ implementation;
`tests/checksums/perf.py` compares two builds (wall time, or exact instruction counts with `--ir`).

- Modules run without the instruction budget (engine code) and keep their state in the C++
  `GameState` (hashed and rolled back as before), so a game with no scripts of its own has
  unchanged checksums.
- A module schedules itself: `std.period.<name> = N` (every N ticks; 0 = never in this game).
- **Override**: a game replaces a module with `scripts/std/<name>.lua` listed in its rules
  `"scripts"` (same contract: define `std.<name>`). Its changes are then the game's own rules.
- Kept in C++ after measuring: the per-player economy pass (upkeep, capacity, income) — it runs over
  all 256 player slots every tick, and the Lua version cost +50% instructions on `ironfront`.


## Editor, hot reload, MCP

- Window **Скрипты** lists the files and the script log of the running play-test; script errors
  also appear in **Console**. When a `.lua` file under `scripts/` changes on disk, its syntax is
  checked and a running play-test restarts with the new scripts (the VM is built from the sources
  when the match starts).
- MCP: `script_api` (this reference as JSON), `script_list`, `script_read`,
  `script_write {path, text, register}` (compiled first — a syntax error or a float literal is
  returned and nothing is saved; `register: true` adds it to rules `"scripts"`), `script_check
  {seconds, seed}` (compiles everything and runs a headless bot match, returns errors with
  file:line). `game_file_write` checks `.lua` files the same way.
- Headless: `rts_headless --project templates/scripted --seed 3` prints the script log (errors also with
  `--quiet`).

## Engine side

`modules/rts/RtsScript.h` (design notes), `RtsScript.cpp` (VM, sandbox, fx, serialization),
`RtsScriptEngine.cpp` (handlers, dispatch, `state`), `RtsScriptApi.cpp` (`game` table and
`scriptApiReference()`), `modules/rts/view/RtsViewScript.*` (view VM). Loaders call
`game::embedScripts(rules, map, reader)` next to `applyTilesToMap`: script sources travel inside the
rules JSON, so the simulation needs no file access. Lua 5.4.7 is vendored in `third_party/lua`
(compiled as C, without the os / io / package / debug libraries; local changes in `README.rts.md`).

### Render textures and paint layers (view scripts)

`view.render_texture{width,height}` creates a persistent mask canvas; `view.paint_layer{bounds,
texture?,color?,...}` displays a canvas over terrain (or creates its own). `:stroke(x0,z0,x1,z1,
{radius,softness})`, `:clear()` and `:destroy()` operate on manual canvases. An optional
`source="territory",owner=N` derives the mask from visible ownership. These APIs are presentation-only;
see [render-texture.md](render-texture.md) for examples, limits, lifetime and backend conventions.

### Script-driven HUD

Optional `game.json.scriptHud` panels bind localized text, numeric counters and choice
indices to `game.set_var` values. Buttons send ordinary player-ability commands; the
simulation validates their effects. Shared client/editor evaluator and schema:
[Script HUD bindings](script-hud.md). This API is presentation-only.

### Local point-target drags

`view.targeting()` returns `{ability, x, z, active, overWorld}` for the local UI's
current player-ability target. Coordinates are world X/Z floats. This is presentation
state only: it is neither replicated nor written into the simulation. Use it for
allowed-area masks and cursor previews; authoritative validation remains in the
command's simulation handler. Client and editor clear it when targeting ends.

The client data-model callback `skill_drag(actionId, progress)` starts a point-target
HUD drag on `mousedown` for an enabled action with nonnegative progress (the support
button convention). Pair it with `support_click(actionId, progress)` on `click` for
non-target actions such as rearm. Release over the world submits the command;
release over UI, Escape, lost focus, and touch cancellation cancel the gesture.
The editor's point-target script HUD buttons use the same press/drag/release flow.

`paintLayer:set_opacity(value)` clamps finite values to 0..1 without clearing its
canvas. Two pairs of layers can retain the previous geometry while animating a new
network, avoiding destructive clear/repaint flicker. The existing 16-layer budget
still applies.

### Camera (view scripts)

`view.camera()` returns the host camera `{x, z, zoom, pitch, yaw, width, height, ortho}`: target on the ground, zoom in
the units of game.json `"camera"` (orthographic: visible view-plane height in world units; perspective: distance),
angles in degrees, viewport in framebuffer pixels. `view.set_camera{x =, z =, zoom =, pitch =, yaw =}` changes any
subset after the frame's view scripts (pitch 5..90, zoom > 0, finite values). A game ports its own framing with it
(OpenWar: `scripts/view/openwar/camera.lua`, Unity's ArmyMapView). Presentation only; the client applies it, the
editor Game view does not yet. Test: `openwar_player_colors_flat_lighting_view_camera`.

### Screen canvas (view scripts)

`view.to_screen(x, z [, y])` returns `sx, sy, onScreen`: the window point (logical points, top-left origin) of a world
point (`y` = height, default 0) through the host camera; `onScreen` is false behind the camera or outside the window.
A `view.set_camera` earlier in the same frame is already applied, so a script listed after the camera script projects
with the frame's final camera. `view.draw(verts [, {image = "ui/x.png", layer = 0}])` adds a triangle list in those
window points for this frame only: without `image` each vertex is `x, y, rgba`, with `image` it is `x, y, u, v, rgba`
(uv 0..1 over the image, `0, 0` = its top left; atlases work). `rgba` is an integer `0xRRGGBBAA`. Consecutive calls
with the same image and layer share one batch; batches are drawn in ascending `layer` (call order within a layer)
after the world, the particles and the overlays, under the UI documents. A call with a partial triangle, a non-finite
number or more than 1048576 vertices a frame raises an error and adds nothing. This is how a game ports a 2D vector
board drawn over the 3D map (OpenWar: Unity's `ArmyBoard` VertexHelper meshes, `scripts/view/openwar/board.lua`).
Client only (the editor Game view does not draw it yet); presentation only. Test: `view_to_screen_and_draw_canvas`.
`transform = {xx, xy, yx, yy, ox, oy}` maps the batch's points on the host side (`x' = ox + x*xx + y*yx`,
`y' = oy + x*xy + y*yy`, binary64 in that order, then float like any vertex): a script keeps a batch in its own
coordinates (e.g. ground coordinates under an orthographic camera, whose ground-to-screen map is affine: three
`view.to_screen` calls give it) and redraws the same table every frame without rebuilding it when only the camera moves
— the result equals the same sum written in Lua. OpenWar's nuclear scorch uses it. Test: `view_draw_transform`.

`view.draw_ground(verts [, {image = "ui/x.png", layer = 0}])` is the world-space counterpart for marks lying on the
map: each vertex is `x, y, z, rgba` (with `image`: `x, y, z, u, v, rgba`) in world units (`y` up, absolute height —
put it a few hundredths above the ground). The triangles are drawn inside the 3D scene after the opaque and
translucent meshes, depth tested without depth writes and with a small bias towards the camera, so units and buildings
standing on a mark hide it (the screen canvas above draws over them). Batches are sorted by `layer`, then call order;
same image and layer merge; one frame only; the texture alpha below .02 is cut (vertex alpha is free). Same limits and
errors as `view.draw` (1048576 vertices a frame). Client only; presentation only (`modules/rts/view/RtsGroundCanvas.*`).
OpenWar draws its ground ability areas, railways and nuclear target rings with it, as Unity's ground canvases under
the units. Test: `view_draw_ground_world_triangles`.

A unit type's view can carry a flat ground shadow image drawn through the same ground canvas (layer -1, before the
view scripts' `draw_ground` batches of layer >= 0): `"groundShadow": {"image": "ui/shadows/Tank.png", "size": [w, d],
"offset": [x, z], "color": [r, g, b, a], "lift": .05, "flip": [u, v]}` — `size` and `offset` in world units in the
model's frame (the drawn model's rotation, without its scale), `color` multiplies the image (default black at alpha
.55), the image's u runs along the model's +x and its top lies towards the model's -z unless flipped. It follows the
interpolated position and facing and replaces the default blob of air units. OpenWar ports Unity's footprint
sprites (`ArmyUnitModelLayer.AttachShadow`) with it. Client only (drawn by `RtsView::drawGround`). Test:
`view_ground_shadow_option`.

View script budget: every handler call (`view.on`, `view.on_frame`, `ui.on`) has its own budget of 5,000,000 VM
instructions (the C functions it calls are not counted), so one heavy handler never starves the others in that frame.
A call over the budget is aborted and skipped for that frame (logged `... [view handler skipped once]`, at most 20
times a handler); the handler is disabled only after 10 calls in a row over the budget (an endless loop, not a spike)
or on an error (`[view handler disabled ...]`). An aborted call may leave its own caches half built, so a script that
builds incrementally should publish a cache only once it is whole. `ViewScripts::profile()` gives the instructions
per handler (calls, total, most expensive call, last frame, overruns, disabled) and per frame; `RTS_VIEW_PROFILE=file`
makes the client write it every frame (`h <index> <file:line>` once per handler, then `f <tick> <frame total>
<per handler ...>`, -1 = disabled), and `RTS_SIM_SPEED=s` (testing) runs the match s times faster than real time
(up to 8*s ticks a frame) so long battles fit a profiling run. Test: `view_handler_budget_per_call`.

View scripts' log (`game.log` lines and errors, which disable the failing handler) is kept by the host; with
`RTS_SCRIPT_LOG=1` the client also prints each line on stdout as `[view] t=<tick> ...` / `[view error] ...`, next to the
simulation scripts' `[script] ...` lines (screenshot and capture runs). Hook: `ViewScripts::onLog`; test
`view_script_log_callback`.

### Categorical cell layers

`view.paint_layer{bounds = {x, z, w, h}, cells = {width = W, height = H}, density = 1..4, smooth = .75..2,
borderWidth = 0..2, borderColor = {r, g, b [, a]}, outside = id, opacity, lift, landOnly}` creates a raster of one id
byte per cell (row-major, z rows) drawn as smooth categorical regions: every pixel takes the id with the largest
smoothstep-weighted coverage of its 5x5 texel neighbourhood (`smooth` = falloff in texels), and a border of
`borderWidth` texels is drawn where the coverage of an id crosses one half (2x2 bilinear coverage on low material
quality). `layer:set_cells(bytes)` takes a Lua string of W * H bytes, `layer:set_palette{[id] = {r, g, b [, a]}}` sets
colours (0..1); `outside` is the id used beyond the raster edge. `transparent = id` draws that id fully transparent (an overlay raster over
another layer, e.g. OpenWar's capture flashes: id 0 empty, the flashed cells white, `layer:set_opacity(v)` animated per
frame); palette alpha is not used (the texel alpha holds the id). `density` repeats each cell as density x density
texels (smoothing and border widths are in texels). One quad per layer, texelFetch on an RGBA8 texture (rgb colour,
a id); presentation only. OpenWar draws its ownership raster with it (ArmyMapWorld look).

`ground = {scale, strength, flipHeight, team = {min, max}, context = {min, max}, linear, coast = {sea, drop, beach,
shallow, earthNear, earthFar, foam}}` adds a ground surface (a port of ArmyMapWorld's meadow and coast): land ids in
`team` keep their colour under a world-anchored meadow relief (patches, blots, pale spots; `scale` = patch size in world
units, `strength` = fine variation), other land ids get the neutral meadow (green / olive / pale), ids in `context` a faint
noise; `flipHeight` > 0 anchors the noise at y = flipHeight - z (north-up source maps); `relief` = 1 / 2 replaces the
meadow of non-context land with the biome surface (the id colour under broad relief and snow / dune ripples of frequency
8 / 12: ArmyMapWorld ApplyMeadow for ice / desert campaigns), 0 (default) keeps the meadow. With `coast`, the id `sea` is
water: a static signed shoreline distance (texels, chamfer + 3x3 smoothing, rebuilt only when the sea mask changes) gives
a beach rim on land and shallows, a shore shadow, a cliff on the shore facing `drop` (texture-row direction, -1 = towards
-z) and an animated foam line on water (colours 0..1). `linear = true` runs the surface maths on linear colour (sRGB in
and out), as a linear-space Unity project. The value noise uses the desktop sine hash, the mobile hash on low material
quality (as the source's ARMY_MOBILE variant). Test: `view_paint_layer_ground_options`.

### Scripted air placement

`game.set_position(unit, x, z)` keeps its flat-map, living mobile-unit and in-bounds validation. Air-layer units can be placed over water; other units still require a ground cell. Previous render position is preserved for interpolation, native movement/orders are cleared, and presentation applies the configured flight height. The API does not paint territory or find routes. Covered by `script_air_position_over_water`, including snapshot replay.

### Immutable match seed

`game.seed()` returns the configured match seed as a Lua integer (the signed two's-complement representation of the full 64-bit seed). It is available in simulation and view scripts, consumes no RNG values, and is unchanged by rollback. Games can use it for deterministic keyed decisions whose result must not depend on unrelated RNG calls. Store accepted decisions and command identities in `state` as usual.

### Custom grid paths

`game.grid_path(width, height, cells, start, goal [, label])` performs a deterministic native A* query over a custom four-neighbour grid in both sim and view scripts. It does not move units or inspect/change the engine map. Cell IDs are zero-based (`z*width+x`); `cells` is a sparse Lua table. Without `label`, only boolean `true` is passable. With an integer label, only cells containing exactly that Lua integer are passable (including label 0); missing values are blocked. Metatables are not consulted.

The returned one-based array excludes start and includes goal; unreachable or out-of-range endpoints return nil. An in-range start equal to goal returns an empty array. A blocked start is allowed so a unit can escape newly blocked ground; all subsequent cells, including goal, must be passable. Width/height must be integers in 1..4096 and their product at most 262144; malformed dimensions/tables/labels raise a script error. Costs are integer, ties use remaining Manhattan distance then cell ID. No persistent query cache or state is created: changed masks take effect immediately, and scripts store accepted paths in hashed `state` as usual. Unit radius, diagonals, height and dynamic avoidance remain the caller's responsibility.

`game.grid_sample_segment(width,height,cells,x,z,tx,tz[,label[,margin[,density]]])` checks a fixed-point segment against the same masks/labels. It returns false for out-of-grid endpoints or any blocked sample. The start is excluded, the destination included, and a zero-length segment samples its single point. There are `floor(length*density)+1` evenly spaced samples; density defaults to 8 and must be an integer in 1..16, with a 65536-sample cap. Coordinates/margin are integer or fx; margin defaults to zero (allowed 0..4096). A nonzero margin adds four probes at ±margin on each axis per sample. This is a sampled cross footprint, **not** continuous collision detection or a swept circle/rectangle: narrow diagonal obstacles can be missed. Use it for steering shortcuts only where that sampling model is appropriate. Out-of-grid margin probes block; malformed arguments raise a script error. The query reads raw table entries, has no cache/mutation, and uses the same integer/fx arithmetic in sim/view; native query work is outside Lua instruction counting, so callers must bound query frequency too.

### Headless snapshot and save-state verification

`rts_headless --project <dir> --max-seconds 35 --verify-restore-at 15 --verify-restore-ticks 200` captures both `SimSnapshot` and `Simulation::serialize()` at 15 seconds, advances 200 ticks, then repeats the same interval after snapshot and serialized restoration in the **same live VM**, and after serialized restoration into a newly initialized simulation/VM. Every tick's simulation checksum must match, and script errors fail the verification. External C++ bot objects are copied along with the verification baseline so their input decisions repeat; ordinary save-state files do not serialize those external controllers. A plain `--load` bot match can therefore receive different commands and is not by itself a deterministic save/load comparison.

The checker retains normal final state and recording behavior. `--record` can be replayed normally; verification wall time includes the repeated intervals and is not a normal performance benchmark. Restore ticks must be 1..100000 and fit before the requested match limit. A natural match end shortens the checked interval and the actual count is printed. A match ending before the restore point fails rather than reporting success. Verification cannot be combined with replay/setup-only/path/group benchmark modes. Exit 1 reports failed verification; exit 2 reports invalid configuration or failed setup. `tests/check_headless_restore.py <binary>` exercises all three restoration paths, tick-zero startup, replay consistency, invalid intervals and a deliberately untracked Lua local that must desynchronize.

This checks identical-input continuation, not arbitrary alternate command histories, cross-platform equivalence, UI/visual state, or migration between game versions. Use targeted game tests for those additional requirements.

### Entity-anchored point actions

`view.targeting()` now also exposes `caster`, the packed source entity ID (0 for
player abilities). The manifest may opt into generic world-anchored entity controls;
see [Entity action controls](entity-action-controls.md). Presentation captures an
explicit source, while normal entity commands and game handlers remain authoritative.
