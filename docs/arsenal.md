# Arsenal (weapons, ammo and money of the top-down hero)

The hero of a top-down game (docs/topdown.md) carries a row of weapons with their own ammo, fires the one in hand
along his aim, collects pickups from the ground and earns money for kills, as in GTA2. Simulation:
`modules/rts/RtsArsenal.{h,cpp}`. Tests: `aiverse_tests arsenal_`. City project data: `tools/citygen/add_arsenal.py`.

## How it works

- **Slots.** `arsenal.weapons` lists weapon ids of the hero's unit type in slot order. Damage, range, cooldown,
  projectile speed, splash and the projectile program (`flight`) stay ordinary weapon data of the type; the slot adds
  the ammo (`start`, `max`; `start: -1` = unlimited), `pellets` per shot and their `spread` (degrees). At the start
  and after a respawn the best slot that has ammo is in hand.
- **Free fire.** Holding fire (the fire bit, or the aim when `topDown.aim.fires`) shoots along the aim, else along the
  facing. There is no target lock: only a soft snap toward a hostile inside `topDown.aim.snapCone`. A weapon with a
  projectile speed launches its projectile program at a point, so `flight.blockTerrain` stops it at walls and the
  first body in the way takes it. Speed 0 is a hit-scan along the aim (fists). The unit's own weapon system is kept
  quiet for the hero. A weapon that runs dry switches to the next lower slot with ammo.
- **Neutral bodies.** `hitsNeutral` lets shots hit passive neutral units (owner 0: parked cars) and `payNeutral`
  pays their `bounty` to the killer. A splash weapon fired this way goes off where its flight ends (a rocket into a
  wall), not only on a direct hit.
- **Switching.** `kCmdTopDownWeapon` (Lua / MCP `topdown_weapon`): param `1` next, `-1` previous (slots without ammo
  are skipped), `100 + slot` picks a slot. Client: X / Tab next, Z previous, 1..9 slots; touch: game.json
  `topDown.buttons` entries `{"weapon": "next" | "prev", "anchor", "offset", "radius"}` (drawn with an arrow).
- **Pickups** are units of the types in `arsenal.pickups`, placed by the map or spawned later. Walking over one (or
  driving: the car's length collects) gives `ammo` of a `weapon`, `money`, `heal` (hit points), or sells for `price`.
  A pickup that would give nothing (full ammo, full health) stays. A taken one is off the map until `respawn` seconds
  pass (0: never). Give the types `"move": {"speed": 0, "layer": "air"}` and `invulnerable`, so nothing bumps into
  them and bullets pass.
- **Money** is the stock resource named by `money` (default: the first resource). Unit `bounty` pays into it as in any
  game; a kill with a car counts for the driver.
- **Death.** `loseOnDeath` resets the slots to their `start` ammo on respawn, `deathFine` is taken from the money.
- **At the wheel** the hero does not fire (the fire bit is the handbrake).
- **State / determinism.** `ArsenalStore` world resource: hashed, saved after the vehicles, copied by rollback
  snapshots. Fixed maths only. Games without the block keep their checksums and save layout.

## rules.json

```json
"arsenal": {
  "money": "cash", "loseOnDeath": true, "deathFine": 100, "hitsNeutral": true, "payNeutral": true, "pickupReach": 0.12,
  "weapons": [
    {"weapon": "fists", "name": "Fists", "start": -1},
    {"weapon": "pistol", "name": "Pistol", "start": 24, "max": 99},
    {"weapon": "shotgun", "name": "Shotgun", "max": 40, "pellets": 6, "spread": 26}
  ],
  "pickups": {
    "pickup_pistol": {"weapon": "pistol", "ammo": 24, "respawn": 45},
    "pickup_cash":   {"money": 250, "respawn": 90},
    "pickup_health": {"heal": 60, "respawn": 60},
    "shop_shotgun":  {"weapon": "shotgun", "ammo": 10, "price": 500, "respawn": 5}
  }
}
```

Needs the `topDown` block; every hero type must have every listed weapon.

## Client

The top bar shows the money (as every resource) and the weapon in hand with its ammo. `kEvAttack` without a target
plays the shooter's attack animation; `kEvItem` (a = hero, b = pickup, value = pickup kind) marks a pickup taken.

## City project

`tools/citygen/add_arsenal.py <project>` (called by `make_city.py` at the end of a build; safe to run again) adds
fists, pistol, machine gun, shotgun and rocket launcher to the hero, six pickup types with ground icons
(`sprites/pickups/`), bounties for people and cars, a starter row of pickups around the hero and one pickup per
22 x 22 cells on the pavements.

## Not done yet

Thrown weapons (grenades, molotovs), flamethrower, vehicle weapons and drive-by fire, weapon shops as places,
dropped weapons of the dead, armed people and police with a wanted level, explosions of wrecked cars, weapon and
pickup sounds, weapon icons and a kill-frenzy style HUD.
