# Top-down heroes (twin-stick direct control)

One hero per player is steered directly (virtual joystick / WASD / gamepad) instead of with RTS orders, as in
Drone Fall and other mobile top-down shooters. Simulation: `modules/rts/RtsTopDown.{h,cpp}`; client model
(sticks, buttons, keys, camera): `modules/rts/view/TopDownControls.{h,cpp}`; client glue: `apps/client/ClientTopDown.cpp`.
Sample: `templates/topdown_arena` ("Scrapyard Arena").

## How it works

- **Heroes.** rules `topDown.heroes` lists unit types; the first own unit of such a type (placed by the map or the
  faction start, or trained later) becomes the player's hero. It carries `kUnitPossessed`: RTS orders, the
  computer player's army code and automatic target acquisition ignore it. `respawn` (seconds) brings a dead hero
  back where it first stood.
- **Input = commands.** One wire format for every hero game, the battle server and the load test
  (`engine/net/HeroInput.h`): `kCmdTopDown` = `net::kCmdHeroInput` (0xF100) and `kCmdTopDownAbility` =
  `net::kCmdHeroAbility` (0xF101). `kCmdTopDown` carries `topdown::pack(Input)`: move angle in binary turns (0 = +z, 16384 = +x),
  magnitude per mille, aim angle, aim / fire bits. The sim re-quantizes every command to `topDown.input`
  (`sectors`, `magnitudeSteps`, `deadzone`; Drone Fall: 45 sectors, step 1, dead zone 0.05), so all peers see the
  same integers. Input is **sticky**: clients send on change only. Under rollback netplay (`engine/net/Rollback.h`)
  the "no commands" prediction for a remote player therefore keeps his hero moving with its last input (dead
  reckoning); confirmations that differ roll back as usual. Lockstep latency is the session's input delay.
- **Movement.** Velocity eases toward the stick (`move.accel` up, `move.decel` when slowing / reversing; 0 =
  instant) at the unit's `Speed` stat (buffs, statuses apply; stun / root stop it). The body box (`move.radius`)
  stays on passable cells, cliffs need `canStep`, a blocked axis is dropped so heroes slide along walls; sub-steps
  of a quarter cell. `move.collideUnits` pushes the hero out of other ground (or air) bodies; structures block
  through the grid. Other units' local avoidance (ORCA) treats heroes as moving obstacles. Facing follows the aim,
  else the movement, limited by `move.turnRate` (degrees / s).
- **Targeting (Drone Fall rules, as data).** With an aim (aim stick, right stick, held mouse) the hero fires at the
  nearest valid target inside `aim.snapCone` (15 degrees) around the aim, also while moving. Without an aim,
  `autoFire` picks the nearest valid target in weapon range (`range` overrides) inside `cone` around the facing:
  `whileMoving: false` holds fire while the hero moves (Drone Fall's hero), `lineOfFire` rejects targets behind
  rock / structures, `retarget: "nearest"` re-evaluates every `interval` seconds and switches only to a target closer
  by more than `switchMargin`, `"keep"` stays on a valid target, `priority` types come first, `preferArmed` prefers
  things that shoot back. Valid = hostile, alive, visible to the owner, a layer the weapons hit, not invulnerable.
  The ordinary weapon system then fires (cooldowns, projectiles, damage, events).
- **Abilities.** `kCmdTopDownAbility` carries `topdown::abilityParam(Ability)`: ability index of the hero type, a
  phase (tap, press, release, cancel), optionally the button's stick direction (binary turns) and a distance (per
  mille of the range). Older clients send the bare index, which is a tap. Self / none abilities cast on the hero;
  point abilities go to the given point, else along the button's stick (at `distance` of the range), else along the
  aim (or facing) at full range, clamped to the range; unit abilities take the given target, else the nearest valid
  one inside the snap cone around the button's stick, else the current target, else the nearest valid one in range.
  Phases: **tap** casts; **press** remembers the stick and starts a **hold** ability (`abilities.hold`), which then
  recasts whenever it is ready until **release**; release casts an aimed (non-hold) ability along its stick;
  **cancel** casts nothing. A `castTime` runs as a windup in the sim (`kEvCastStart`); stun / silence / death cancel
  it, the stick keeps steering unless `abilities.rootWhileCasting`. A unit target that dies during the windup
  cancels the cast.
- **Free fire** (`aim.freeFire`). Aiming (or fire held) with nothing inside the snap cone fires the weapons along
  the aim anyway (Drone Fall): a weapon with a flight program flies it (the program stops on bodies / walls); any
  other shot travels to the first hostile body on the line or the first wall, within the weapon range. Off by
  default: the hero then only shoots at targets.
- **Squad** (`squad`). Own units of `squad.types` (up to `size` per hero, 0 = all) follow the hero: Drone Fall's
  Wander + unit group. Members carry `kUnitFollower` (RTS orders, the computer player's army code and selections
  leave them alone) and hold position (no chasing). Each member walks (pathfinding) to its slot on a ring of
  `radius` around the hero, member 0 behind it, turning with the hero's facing (`rotate`); a wall between hero and
  slot pulls the slot in front of the wall. Dead members leave the ring, which closes up. While the hero moves the
  members hold fire (`fireWhileMoving: false`, Drone Fall SupressFireOnMove); standing, they shoot with the
  ordinary auto-targeting. `repath` (seconds) is the slot refresh period. Respawned heroes keep their squad.
- **Escape** (`escape`, Drone Fall HeroEscape, PvE). The client starts a preview: for `duration` seconds the move
  stick walks a ghost point at the hero's speed, walls kept (a single-player match pauses meanwhile when `pause`;
  netplay keeps running), then sends the point as `kCmdTopDownAbility` with index 255. The sim checks it: hero
  alive, not cooling down, within speed * duration in a straight line and as walked (path length), passable; then
  the hero and its squad (in their ring slots) move there, `kEvGroupTeleport` fires per unit at the departure
  (value 2) and the arrival (value 0), and `cooldown` starts (per player, kept across hero replacement). State,
  HP and statuses stay; there is no invulnerability. Losing focus cancels the preview.
- **Bots.** `RtsAI` steers its hero with the same commands (`TopDown::botCommand`): walk (path around walls) to
  the nearest hostile, preferring armed ones, stop inside 85% of the weapon range so auto-fire works, give ground
  to armed foes closer than 35%.
- **Raid bots and other AI** never give a hero move / attack orders (it ignores them): `TopDown::steerCommand(goal,
  stand)` walks the hero there with the stick (straight when walkable, else the next path waypoint; stops inside
  `stand` so channels work) and kites: foes whose single hit is at least 5% of the hero's health (Drone Fall
  guardians' clubs), or, below 60% health, a swarm of 6+, push it away once they are within their weapon range
  + 2.5, with wall probes at 45 / 90 / 135 degrees; a hostile telegraph area (rules `encounters`) in windup or active
  that covers the hero outweighs everything: it steps out (away from a circle / cone origin, sideways off a line, to
  the nearer edge of a ring). Otherwise the hero stands and auto-fires. The raid bot
  (`RtsRaidBot.cpp`) uses it for objectives, kills, loot and extraction, keeps kiting before the raid starts and while
  it waits for a called point, and re-steers every 3 ticks. With rules `progression` it first walks over xp orbs
  within 14 units and, with a level banked and offers at circles, to the nearest summon circle
  (`Progression::botGoal`). Skirmish bots (`botCommand`) do the same only with `"bot": {"progression": true}`.
- **State / determinism.** `TopDownStore` world resource (rollback snapshots copy it), hashed, saved after the grid
  fields in `RtsSave`. Games without the block keep their checksums and save layout; so do top-down games that use
  none of squad / escape / hold / windups / free fire (`rules.extended`: only then is the extra pilot state — held
  button, windup, escape cooldown, squad — hashed and saved). Planets are not supported.
- **Outcome.** `RtsModule` freezes the match outcome at the end of the first tick with a result (`MatchOutcome`
  world resource, not hashed or saved): `result()` keeps returning it and `outcomeBlob()` (result, tick, per player
  slot / team / alive / units / structures / team score) never changes again. Hosted battle clients report that blob.

## Network (hosted battles, rollback)

- The battle server lets only the two hero commands through (`allowedCommands = {kCmdTopDown, kCmdTopDownAbility}`).
  `apps/battle/RtsBattle.h` runs a top-down project through the dedicated server, the secure transport and the
  simulated mobile network (`aiverse_battle load --project templates/topdown_arena`, test
  `hosted_rts_topdown_battle_agrees_on_the_outcome`).
- **Rollback corrections** of heroes and squad members are blended: the client installs
  `RollbackSession::setRollbackHook`, reads positions before and after the re-simulation and feeds the difference
  to the correction smoother (`modules/rts/view/TopDownPrediction.h`, `net::CorrectionSmoother`); large jumps
  (respawn, escape) snap.
- **Local visual prediction** (render only). Input reaches the sim after the input delay plus the interpolation lag;
  the client draws the local hero where the input already in flight takes it (sim position + integral of the
  stick's velocity over that lead), clamped by walls, eased. Nothing feeds the sim, so peers never diverge.
  game.json `topDown.prediction`: `false` or `{"enabled", "maxLead": 0.3, "maxOffset": 3, "easeHalfLifeMs": 35,
  "correctionHalfLifeMs": 50}`.

## rules.json

```json
"topDown": {
  "heroes": ["drone"], "respawn": 8,
  "move": {"accel": 36, "decel": 54, "turnRate": 720, "collideUnits": true},
  "input": {"sectors": 45, "magnitudeSteps": 1, "deadzone": 0.05},
  "aim": {"snapCone": 15, "fires": true, "range": 0, "freeFire": true},
  "abilities": {"hold": ["laser"], "rootWhileCasting": false},
  "squad": {"types": ["guard"], "size": 4, "radius": 2.5, "tolerance": 0.5, "rotate": true, "fireWhileMoving": false, "repath": 0.25},
  "escape": {"duration": 5, "cooldown": 30, "pause": true},
  "bot": {"progression": false},
  "autoFire": {"enabled": true, "whileMoving": false, "cone": 360, "range": 0, "lineOfFire": true,
               "retarget": "nearest", "interval": 0.2, "switchMargin": 0.5, "preferArmed": true,
               "priority": ["drone"], "movingSpeed": 0.1}
}
```

`"autoFire": false` switches auto-fire off (aim or the fire bit only).

## game.json (client)

```json
"topDown": {
  "touchControls": "auto",            // auto: on touch devices; always; never
  "mouseAim": "hold",                 // hold: aim at the cursor while LMB is held (Drone Fall); always; off
  "joystick": {"mode": "floating", "radius": 64, "deadzone": 0.12, "area": [0, 0.3, 0.5, 1], "center": [0.14, 0.78]},
  "aimStick": {"enabled": true, "mode": "floating", "radius": 56, "deadzone": 0.2, "area": [0.5, 0.3, 1, 1]},
  "buttons": [{"ability": "missile", "mode": "aim", "dragRadius": 90, "key": "Q", "gamepad": 5, "anchor": [1, 1], "offset": [-70, -170], "radius": 32},
              {"ability": "laser", "mode": "hold", "key": "E", "anchor": [1, 1], "offset": [-150, -90], "radius": 28},
              {"ability": "escape", "key": "R", "anchor": [1, 1], "offset": [-60, -60], "radius": 30}],
  "prediction": {"enabled": true, "maxLead": 0.3},
  "gamepad": {"deadzone": 0.15},
  "camera": {"pitch": 59, "yaw": 0, "distance": 22, "minDistance": 14, "maxDistance": 34, "smoothing": 6,
             "lookAhead": 2.5, "aimLookAhead": 1.5, "lookAheadSmoothing": 3, "lockOffsetIfNoInput": true,
             "bounds": true, "margin": 4}
}
```

Sticks: `floating` appears where the finger lands inside `area` (normalized screen rect) and follows a finger that
runs past 1.5 radii; `fixed` sits at `center`. Buttons: `tap` (default) casts on touch; `aim` sends press on touch,
then the drag away from the button aims it (direction, distance = drag / `dragRadius` of the range) and lifting the
finger casts (a drag shorter than a third of the button: along the aim); `hold` sends press / release (hold
abilities repeat). Keys behave the same (down / up; aimed at the cursor while LMB aims). `"ability": "escape"` is the
escape button: the preview ghost shows as a move marker and the camera follows it. Desktop: WASD / arrows move (camera-relative), held LMB aims at the
cursor, RMB / Space fire, button keys cast (aimed at the cursor while aiming). Gamepad: `TopDownFrameInput`
takes pad sticks / fire; the platform layer has no gamepad polling yet. The camera follows the interpolated hero
with exponential smoothing (aiverselib gameplay camera), a look-ahead along the stick / aim and map bounds; the
wheel / pinch zoom stays inside min / max. `AIVERSE_TOUCH_CONTROLS=1` draws the touch controls on desktop (screenshots).

## Tools

- MCP `play_command`: `{"command": "topdown", "player": 1, "move": [1, 0], "aim": [0, 1], "fire": true}`,
  `{"command": "topdown_ability", "ability": "missile", "at": [x, z]}`, `{"command": "topdown_ability", "ability":
  "missile", "phase": "release", "dir": [1, 0], "distance": 0.5}`, `{"command": "topdown_ability", "escape": true,
  "at": [x, z]}`. Lua `game.command(p, {type = "topdown",
  angle = 16384, magnitude = 1000, aim = true, aim_angle = 0})`.
- `rules_schema` / `rules_lint` read the loader, so the `topDown` keys are listed and typos reported.
- Tests: `aiverse_tests topdown_`.

## Not done (Drone Fall gaps)

Platform gamepad polling (pads feed `TopDownFrameInput` only), an aim-drag indicator drawn in the world (on screen a
knob off the button shows the drag), escape preview effects beyond the move marker (Drone Fall's grayscale camera / telegraph
circle: game view work), squad members walking as one rigid group (they path to their slots individually),
mode rules for escape (PvE only is the game's data decision: leave `escape` out of PvP rules), planets.
