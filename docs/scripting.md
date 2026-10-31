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

### State: `state`, not upvalues

Everything a script remembers between ticks must be in the global table **`state`** (or in trigger
variables, `game.set_var`). After every tick the engine serializes `state` canonically (sorted
keys) into the simulation: it is hashed into the checksum (desync detection), copied by rollback
snapshots and restored into the VM when the netcode rewinds.

- Allowed in `state`: integers, `fx`, strings, booleans and nested tables whose keys are integers or
  strings. Anything else (functions, floats, tables as keys, cycles) is an error — the tick's
  changes to `state` are reverted and the error is logged.
- **Globals are read-only after loading**: assigning a new global in a handler is an error, reading
  an undefined one too (typos show up immediately). Define functions and constants at the top level.
- **Don't change file-level `local` variables from handlers** (`local count = 0` ... `count = count + 1`
  in a handler): such state isn't part of the snapshot, so a rollback can't restore it. Use `state`.
  Constants and helper functions as locals are fine.
- Don't keep references to `state` sub-tables in locals across ticks (a rollback replaces them).

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

## Events

Handlers are registered at the top level of a file (while it loads):

| registration | called | argument |
|---|---|---|
| `on("start", fn)` / `function on_start()` | once, first tick | — |
| `on("tick", fn)` / `on_tick` | every tick | tick |
| `every(seconds, fn)` | every N seconds (seconds: integer or fx) | tick |
| `on("unit_died", fn)` / `on_unit_died` | a unit or structure died | `e.unit` (dead handle), `e.owner`, `e.unit_type`, `e.x`, `e.z` |
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
| `game.log(...)`, `print(...)` | script log |

Filter: `{owner = 2, type = "footman", tag = "mechanical", kind = "unit" | "structure", area = "gate", x = 10, z = 12, radius = 6, limit = 5}` (all optional).

Actions (simulation scripts only):

| function | |
|---|---|
| `game.give(p, res, amount)`, `game.take(p, res, amount)` → taken | resources |
| `game.spawn(type, owner, x, z)` → handle or nil | owner 0 = neutral, 255 = hostile creeps |
| `game.attack_event(actor, target)` | emit the standard attack animation/FX/audio cue for scripted combat; no damage, projectile or order; false for invalid/dead handles; simulation only |
| `game.kill(h [, silent])`, `game.damage(h, amount [, source [, damageType]])`, `game.heal(h, amount)`, `game.set_owner(h, p)` | |
| `game.add_status(h, "status" [, seconds [, caster]])`, `game.buff(h, stat, add, mul [, seconds])` | statuses of rules `"statuses"`; stat modifiers |
| `game.order(units, "move"\|"attack"\|"patrol"\|"stop"\|"hold", x, z \| target)` | through the normal command path |
| `game.command(p, {type = "train", units = {h}, param = "footman", x =, z =, target =, queue =})` | any player command (`move attack stop hold patrol gather build train cancel rally smart send research place sell deploy ability reinforce claim auto_build upgrade surrender player_ability`), validated like network input |
| `game.cast(h, ability, target \| x, z)`, `game.player_ability(p, id, x, z [, free])` | abilities |
| `game.set_var(name, v)`, `game.add_var(name, v)` | trigger variables, shared with rules triggers |
| `game.message(p, text [, x, z])` | a notice (p = 0: everyone) |
| `game.victory(p)`, `game.defeat(p)`, `game.draw()` | end conditions |
| `game.research(p, tech)`, `game.enable_type(p, type, on)`, `game.add_score(p, n)`, `game.enable_trigger(id, on)` | |
| `game.random(n)`, `game.random(a, b)` | simulation RNG |

Not available yet: fog reveal from scripts (use a `reveal` effect in an ability).

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

## View scripts

```lua
-- scripts/view/hud.lua  (rules: "viewScripts": ["scripts/view/hud.lua"])
view.on("unit_died", function(e)
  if e.owner == view.player() then view.notice("We lost a " .. e.unit_type) end
end)
view.on_frame(function(dt) end)            -- dt in seconds (float)
```

View scripts are presentation: floats are fine, `game.*` queries work, actions don't exist. Their
outputs are requests the host shows: `view.notice(text)`, `view.effect(name, x, z[, y])` (particle
effect of `fx/`), `view.sound(name, x, z)`. They never change the simulation, so they don't need
the determinism rules.

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
