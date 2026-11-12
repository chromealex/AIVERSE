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
- **Input = commands.** `kCmdTopDown` carries `topdown::pack(Input)`: move angle in binary turns (0 = +z, 16384 = +x),
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
- **Abilities.** `kCmdTopDownAbility` (param = ability index of the hero type): self / none abilities cast at once;
  point abilities go to the given point, else along the aim (or facing) at full range, clamped to the range; unit
  abilities take the given target, else the current target, else the nearest valid one in range. Cast time /
  windup of the Cast order is not used (instant cast).
- **Bots.** `RtsAI` steers its hero with the same commands (`TopDown::botCommand`): walk (path around walls) to
  the nearest hostile, preferring armed ones, stop inside 85% of the weapon range so auto-fire works, give ground
  to armed foes closer than 35%.
- **State / determinism.** `TopDownStore` world resource (rollback snapshots copy it), hashed, saved after the grid
  fields in `RtsSave`. Games without the block keep their checksums and save layout. Planets are not supported.

## rules.json

```json
"topDown": {
  "heroes": ["drone"], "respawn": 8,
  "move": {"accel": 36, "decel": 54, "turnRate": 720, "collideUnits": true},
  "input": {"sectors": 45, "magnitudeSteps": 1, "deadzone": 0.05},
  "aim": {"snapCone": 15, "fires": true, "range": 0},
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
  "buttons": [{"ability": "missile", "key": "Q", "gamepad": 5, "anchor": [1, 1], "offset": [-70, -170], "radius": 32}],
  "gamepad": {"deadzone": 0.15},
  "camera": {"pitch": 59, "yaw": 0, "distance": 22, "minDistance": 14, "maxDistance": 34, "smoothing": 6,
             "lookAhead": 2.5, "aimLookAhead": 1.5, "lookAheadSmoothing": 3, "lockOffsetIfNoInput": true,
             "bounds": true, "margin": 4}
}
```

Sticks: `floating` appears where the finger lands inside `area` (normalized screen rect) and follows a finger that
runs past 1.5 radii; `fixed` sits at `center`. Desktop: WASD / arrows move (camera-relative), held LMB aims at the
cursor, RMB / Space fire, button keys cast (aimed at the cursor while aiming). Gamepad: `TopDownFrameInput`
takes pad sticks / fire; the platform layer has no gamepad polling yet. The camera follows the interpolated hero
with exponential smoothing (aiverselib gameplay camera), a look-ahead along the stick / aim and map bounds; the
wheel / pinch zoom stays inside min / max. `AIVERSE_TOUCH_CONTROLS=1` draws the touch controls on desktop (screenshots).

## Tools

- MCP `play_command`: `{"command": "topdown", "player": 1, "move": [1, 0], "aim": [0, 1], "fire": true}`,
  `{"command": "topdown_ability", "ability": "missile", "at": [x, z]}`. Lua `game.command(p, {type = "topdown",
  angle = 16384, magnitude = 1000, aim = true, aim_angle = 0})`.
- `rules_schema` / `rules_lint` read the loader, so the `topDown` keys are listed and typos reported.
- Tests: `aiverse_tests topdown_`.

## Not done (Drone Fall gaps)

Squad followers around the hero (Drone Fall's wander + unit group), free fire without a target (bullets along the
aim stopped by walls), cast-time abilities through the stick, perk / hold-release buttons, hero escape mode,
platform gamepad polling, client-side visual prediction beyond the lockstep input delay, planets.
