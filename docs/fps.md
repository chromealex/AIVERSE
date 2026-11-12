# FPS: reference runtime and examples

FPS projects use `game.json` with `module: "fps"` and `genre: "fps"`. Choose FPS in
New Project: `fps_training` (stationary targets), `fps_arena` (5 vs 5 bots), or
`fps_survival` (100 slots, sparse large world). The editor launches a separate FPS client.
Existing copied projects are not overwritten when engine templates change.

## Controls and weapons

LMB captures the mouse and fires. Short clicks are retained until an input tick is
submitted. WASD moves, Shift sprints, Ctrl crouches, Space jumps, R reloads, Escape
releases the cursor. Keys 1–4 select the corresponding entry in `fps.weapons`.

The templates provide pistol, automatic rifle, nine-pellet shotgun and sniper rifle.
Pistol, shotgun and sniper require a new press for each shot. All are hitscan weapons.
Weapon changes preserve separate magazine/reserve counts, cancel the current reload,
and apply a switch delay. Fire cooldown survives switching. Reload completion transfers
only the missing rounds from reserve. Respawn restores the configured loadout.

Optional `rules.json` block (1–4 entries):

```json
{"fps":{"weapons":[
  {"nameKey":"fps.weapon.pistol","damage":28,"magazine":12,"reserve":72,
   "automatic":false,"intervalTicks":15,"reloadTicks":75,"switchTicks":15,"range":150},
  {"nameKey":"fps.weapon.rifle","damage":25,"magazine":30,"reserve":120,
   "intervalTicks":6,"reloadTicks":120,"range":400},
  {"nameKey":"fps.weapon.shotgun","damage":14,"magazine":8,"reserve":40,
   "automatic":false,"intervalTicks":54,"reloadTicks":150,"range":60,
   "pellets":9,"spread":0.006},
  {"nameKey":"fps.weapon.sniper","damage":100,"magazine":5,"reserve":25,
   "automatic":false,"intervalTicks":72,"reloadTicks":160,"range":1500}
]}}
```

This is an excerpt, not a complete rules file. Times are simulation ticks; these
values assume 60 Hz. `spread` is a turn offset in the deterministic pellet grid;
`damage` is per pellet, one shell is consumed for the entire shot. `nameKey` is a
localization key. The legacy `fps.weapon` block remains supported when `weapons`
is absent. The editor weapon panel edits each loadout entry. Existing projects
can add the array in the FPS panel's advanced rules editor and Apply.

The client displays a procedural placeholder weapon, flash, cosmetic shot streak,
magazine/reserve, reload state and a hit marker. Flash and hit markers follow accepted
server shot/hit counters (they may be delayed by network latency). They are not a
complete animated weapon asset. Extended mode adds synthesized audio. Damage and ammo are server-owned.

## Runtime and verification

Build `aiverse_fps_client`, `aiverse_fps_server`, `aiverse_fps_tests`, `aiverse_headless` with CMake.
The first three binaries are under `<build>/modules/fps/`.

```sh
<build>/modules/fps/aiverse_fps_client --project templates/fps_training
<build>/modules/fps/aiverse_fps_client --project templates/fps_arena --listen
<build>/modules/fps/aiverse_fps_server --project templates/fps_arena --port 27100
<build>/modules/fps/aiverse_fps_client --project templates/fps_arena --connect udp://127.0.0.1:27100
<build>/modules/fps/aiverse_fps_tests examples/fps/arena
<build>/aiverse_headless --project templates/fps_arena --seed 3 --verify-restore-at 1 --verify-restore-ticks 20
```

Client and server require identical content and protocol. Weapon support uses FPS
save version 7 (version 3 and 6 saves still load); earlier experimental FPS saves and older peers
are rejected. RTS save/protocol versions are unchanged.

## Scope

Offline loopback, listen host and dedicated reference server share the authoritative
simulation. It has bounded input queues, movement prediction/reconciliation, snapshots,
interest filtering and bounded hitscan rewind. UDP latest snapshots use fragmentation;
WebSocket uses its reliable transport. Production authentication, matchmaking, admission,
reconnect, anti-cheat operations, snapshot deltas and WAN capacity qualification remain
separate work. A 100-client loopback test is not evidence of production PUBG capacity.

Large-world support uses sparse static AABB collision, relative render origins and
budgeted loading of presentation box chunks through the generic
[large-world streamer](large-worlds.md) (multiple observers, local chunk coordinates,
load priorities, retry delays and resident resource reservations). Maps with `world.async`
use background terrain/model preparation and streamed box/triangle collision, with a
pre-tick authority gate and bounded GPU publication. See `examples/world/streamed`.
Legacy maps retain resident collision. Dynamic-world persistence and streamed navmesh
are separate work.
Bots aim and strafe; they do not have production navigation/tactics.

### Projectile modifier chains

Weapons with positive `bulletSpeed` can opt into the shared `flight` block: ordered Split,
Ricochet, Fly Through and scalar modifiers. This works independently of `mechanics.enabled`;
`flight.gravity` defaults to `bulletGravity`. Authoritative 3D sweeps, actor-life hit history,
material penetration, save/rollback and existing snapshot presentation are supported.
See [projectile programs](projectile-programs.md) for the JSON schema and trajectory semantics.

## Close combat (opt-in)

`fps.melee.enabled: true` adds an authoritative melee strike, guard, parry and lock-on.
It is independent of `fps.mechanics`; rules without the block behave as before.
Swing timing, input buffering, chaining and one-hit-per-target-per-swing come from the
shared `melee-combat` library (`libraries/include/aiverselib/genre/Melee.h`).

| Behavior | Implementation | Current limits |
|---|---|---|
| Strike | Windup, active and recovery ticks; the blade tip is swept from its previous position each active tick against actor bounds widened by `radius` | One attack type, no combos or weapon-specific swings; no skinned animation |
| Guard | Held Block negates hits whose attacker is within `guardTurns` of the facing direction; Block also stops firing | No stamina or guard break |
| Parry | Pressing Block opens a `parryTicks` window; a strike landing in it deals no damage and staggers the attacker | Parry gives no riposte bonus |
| Stagger | A landed hit staggers the target and a parry staggers the attacker for `staggerTicks`; staggered actors cannot strike or fire | — |
| Lock-on | Holding LockOn selects the nearest living enemy within `lockRange`; strikes aim at the locked target until it dies or moves out of twice the range | Camera does not follow the lock |

Data: `fps.melee` accepts `enabled`, `damage`, `windupTicks`, `activeTicks`,
`recoveryTicks`, `bufferTicks`, `parryTicks`, `staggerTicks` (ticks; defaults scale with
tick rate), `range`, `radius`, `lockRange` (world units) and `guardTurns` (half-angle in
turns, at most 0.25). Simulation events: 3 = swing started, 4 = contact (value = target
slot | contact << 8, where contact is hit 0 / blocked 1 / parried 2). Melee state is saved
from FPS save version 7 and hashed when the block is enabled.

Desktop client controls when enabled: **Z** strike, hold **Left Alt** guard (press to
parry), hold **T** lock-on. The client has no melee viewmodel or hit sounds yet; check
it live before treating the feel as finished.

## Extended mechanics (opt-in)

`fps.mechanics.enabled: true` enables the extended authoritative systems. Existing
rules without this block retain their single-round behavior. The bundled training,
arena, survival and tactical templates enable this block.

| System | Implemented behavior | Current limits |
|---|---|---|
| Aiming and recoil | RMB zoom/sensitivity, reduced ADS spread, movement spread, server recoil and recovery | Procedural viewmodel, no skinned arms or authored animations |
| Damage | Height-based head/body/leg regions, head and limb multipliers, consumable armor | Regions of the actor AABB, not skeletal hitboxes |
| Penetration | Opt-in collider penetration cost, decreasing damage and a bounded per-projectile penetrated-collider list | Material cost, not thickness/ricochet simulation |
| Ballistics | Swept bullet segments, speed, gravity, range and lifetime | Projectile hits use current state, not hitscan rewind |
| Grenades | Frag radial damage with cover; bouncing smoke and flash grenades; smoke lifetime; view-direction-dependent flash duration | Smoke is a box volume placeholder; no fluid simulation |
| Inventory | Bounded stacks using the shared inventory library, pickups, weapon drop/pickup retaining ammo and attachments | Fixed item catalog; no drag-and-drop inventory UI |
| Attachments | Scope, suppressor, extended magazine, with actual accuracy/audio/capacity effects | Three fixed attachment types |
| Medical | Delayed consumable healing, movement/fire/damage cancellation, teammate knockdown, bleeding and revive | Knockdown requires a living teammate |
| Tactical rounds | Freeze/buy phase, spawn buy zone, money, plant/defuse timers, dropped bomb recovery, score, round restart | Team 1 attacks; team 2 defends; no halftime or overtime; loadout resets each round |
| Movement | Prone with ceiling clearance, explicit ladder regions, checked short-obstacle vault | Axis-aligned collision; vault is a discrete move, not a root-motion animation |
| Vehicles | Driver admission, exclusive control, steering, swept static collision, safe exit and weapon damage | Single-seat arcade vehicle on flat ground; no suspension, passengers or dynamic vehicle collisions |
| Audio | Synthesized original shot/reload/footstep sounds through the engine audio service; remote shot attenuation and simple cover volume reduction | Placeholder sound design; own-player footsteps only |

`fps_tactical` / **Breach Protocol** is a separate bomb-mode example. Existing copied
projects are not upgraded automatically. The default standalone FPS client opens
`fps_training` and resolves engine assets independently of its launch directory.

### Extended controls

| Input | Action |
|---|---|
| RMB | Aim / scope |
| G / X / C | Frag / smoke / flash grenade |
| H | Heal |
| E | Nearby pickup, revive, enter/exit vehicle, ladder, or defuse |
| Q | Drop equipped weapon |
| V / F | Toggle prone / vault |
| P | Plant bomb while carrying it inside a site |
| 1–4 then B | Buy the selected weapon in the buy zone and equip it |
| F1 / F2 / F3 | Install carried scope / suppressor / magazine |
| Hold Tab | Inventory list |

### Data

Weapon entries additionally accept `hipSpread`, `moveSpread`, `recoil` (turns),
`bulletSpeed` (zero = hitscan), `bulletGravity`, `penetration`, `headMultiplier`,
`limbMultiplier`, and `price`. Defaults preserve legacy accuracy and hitscan behavior.

`fps.mechanics` accepts `knockdown`, `startArmor`, `startMoney`, `killReward`,
`winReward`, `lossReward`, `healTicks`, `reviveTicks`, `bleedTicks`, `freezeTicks`,
`buyTicks`, `plantTicks`, `defuseTicks`, `bombTicks`, `roundsToWin`, `grenadeFuse`,
`smokeTicks`, `flashTicks`, `interactRange`, `blastRadius`, `smokeRadius`, `flashRadius`.
Timing defaults scale with tick rate. Distances use world units.

Map arrays: `loot` (`pos`, `type`, `count`), `vehicles` (`pos`, optional `speed`),
`ladders` and `bombSites` (`min`, `max`). Collider `penetration` is an optional
positive cost; absent means impenetrable. Item IDs: 1 medkit, 2 armor, 3 frag,
4 smoke, 5 flash, 6 scope, 7 suppressor, 8 magazine, 9 ammo; 100–103 are weapons.
Bounds: 256 projectiles, 256 loot objects, 32 vehicles, 12 inventory stacks.
Snapshots carry at most 128 nearby projectiles and loot objects, plus 32 vehicles;
latest-message transport is bounded to 128 KiB. This is still full snapshots, not
production bandwidth optimization.

All new outcome-affecting fields, objects and timers are saved and hashed.
`tests/fps/systems.inc` covers inventory, attachments, cancellation, vehicles,
ladders, prone, grenade effects, hit regions, penetration, ballistics, revival,
restoration, buying, planting/defusing, round reset and vaulting.

The current client HUD/viewmodel/smoke and audio changes require a successful live
visual/audio check. The automated GUI launch was interrupted; do not treat a compiled
client or the headless scenarios as proof of visual quality. Windows, web and mobile
integration of these new controls has not been verified.
