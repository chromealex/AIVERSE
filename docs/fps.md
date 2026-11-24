# FPS: reference runtime and examples

Optional FPS skeletal animations are powered by **AIVERSE MotionWeave**.
New native modules link `AIVERSE::MotionWeave` and include
`"engine/animation/MotionWeave.h"`.
See [MotionWeave](motionweave.md). The old animation ECS target remains valid.

FPS projects use `game.json` with `module: "fps"` and `genre: "fps"`. Choose FPS in
New Project: `fps_training` (stationary targets), `fps_arena` (5 vs 5 bots), or
`fps_survival` (100 slots, sparse large world, battle royale with air insertion and a phased zone). The editor launches a separate FPS client.
Existing copied projects are not overwritten when engine templates change.

## Shared blocks at the top level of rules

The actor-session host (game.json `"module": "actor"`, legacy name) reads its settings from `rules.actor`. The blocks
that describe capabilities rather than this host - `royale`, `gear`, `crate`, `vehicleTypes`, `bots`, `weapons`,
`weapon`, `locomotion` - may also be written at the top level of the rules (`rules.royale`, ...), which is where shared systems
read them as they are extracted from the legacy hosts (docs/no-genre-split-audit.md, G6). A block may sit in only one
of the two places; both at once is a load error. Existing projects keep `rules.actor.<block>` unchanged.

## Controls and weapons

LMB captures the mouse and fires. Short clicks are retained until an input tick is
submitted. WASD moves, Shift sprints, Ctrl crouches, Space jumps, R reloads, Escape
releases the cursor. Keys 1–4 select the corresponding entry in `actor.weapons`.

The templates provide pistol, automatic rifle, nine-pellet shotgun and sniper rifle.
Pistol, shotgun and sniper require a new press for each shot. All are hitscan weapons.
Weapon changes preserve separate magazine/reserve counts, cancel the current reload,
and apply a switch delay. Fire cooldown survives switching. Reload completion transfers
only the missing rounds from reserve. Respawn restores the configured loadout.

Optional `rules.json` block (1–4 entries):

```json
{"actor": {"weapons":[
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
localization key. The legacy `actor.weapon` block remains supported when `weapons`
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

Client and server require identical content and protocol. The current FPS save version is 9
(gear and battle royale; versions 3, 6 and 7 still load); earlier experimental FPS saves and older peers
are rejected. RTS save/protocol versions are unchanged.

## Scope

Offline loopback, listen host and dedicated reference server share the authoritative
simulation. It has bounded input queues, movement prediction/reconciliation, snapshots,
interest filtering and bounded hitscan rewind. UDP latest snapshots use fragmentation;
WebSocket uses its reliable transport. Production authentication, matchmaking, admission,
reconnect, anti-cheat operations and WAN capacity qualification remain separate work.
Snapshots travel as deltas against the newest snapshot the client acknowledged (its input `viewTick`)
through the [snapshot-delta](../skills/network/aiverse-snapshot-delta/SKILL.md) package (protocol 5);
`Server::snapshotStats()` reports packets, bytes and the full-snapshot equivalent. A 100-client loopback test is not evidence of production PUBG capacity.

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

## Battle royale (opt-in)

`actor.royale` (mode `battleRoyale`) runs the [battle-royale package](mechanics-packages.md#battle-royale-battle-royale-010)
on the FPS actors; a map's own `royale` block replaces the rules block. The JSON is the package format
(`examples/libraries/mechanics/royale/royale.json`): `bounds`, `zone` phases, optional `flight`, `drop`, `bombardment`,
`supplyDrops`, `standings`. `actor.crate` lists the items a landed supply drop spawns (item IDs as in `loot`; default
armor, two medkits, magazine, ammo). Without the block the legacy linear `zone` keeps working. `templates/fps_survival`
uses it (`rules/royale.json`).

| Piece | Behavior | Limits |
|---|---|---|
| Insertion | With `flight` every actor starts in the carrier on a seeded path; Jump leaves (door open after `doorOpen` ticks, eject at the end), Jump again opens the chute above `minDeployHeight`, Sprint dives, movement steers; the chute opens itself at `autoDeployHeight`; landing hands the actor to the motor. Ground height = collision cast down | No carrier model / camera; fallback bots jump at seeded points and steer to their spawn |
| Zone | Seeded nested circles, wait / shrink, per-phase damage to landed actors outside | Damage goes through armor like any hit |
| Bombardment | Shells hit landed actors in the blast circle unless collision is above their head (roof) | No explosion falloff |
| Supply drops | Crates announced inside the next circle; `actor.crate` items spawn as loot on landing | Crate is not a physical object |
| Standings | Downs, knocks (`mechanics.knockdown`), revives and outs are mirrored into the standings each tick; a team with nobody standing has its knocked members finished; the standings decide the winner (or a draw when the last teams fall on the same tick) | Kill credit = last enemy who damaged the victim |

Snapshots carry `RoyaleView` (current and next circle, stage timer, own skydive phase, carrier position, players
and teams left, own placement and kills, bombardment area, up to 8 falling crates); the client keeps the server pose
while skydiving. SimEvents: 5 = kill feed (`player` = actor, `value` = victim | kind << 8), 6 = zone stage
(`value` = phase | kind << 8, `pos` = target centre), 7 = supply drop (`value` = id | kind << 16, `pos` = point),
8 = shell impact (`value` = damage). FPS save version 8 stores the run (versions 3, 6 and 7 still load for games
without `actor.royale` / `actor.gear`); snapshot protocol 5 (older peers are rejected). Test: `royale` in `tests/fps/tests.cpp`.
The client (`apps/fps/RoyaleHud.h`) draws the zone wall near the viewer, landed crates, a minimap (current and next
circle, bombardment, crates, carrier, own position and facing), the phase timer with players / teams / kills, jump /
parachute hints, an outside-the-zone warning, the kill feed (`RoyaleView::feed`, the last six lines) and the result
banner; strings are `actor.royale.*` in engine/assets/lang. Checked on a screenshot during the flight (Linux, Mesa
software GL); the zone wall and the banner have not been looked at on screen yet.

## Typed vehicles (opt-in)

`actor.vehicleTypes` (requires `mechanics.enabled`) holds [vehicle-controllers](../skills/gameplay/aiverse-vehicle-controllers/SKILL.md)
definitions (`examples/libraries/mechanics/vehicles-sports/vehicles.json` format: wheeled / tracked / hover / flight
controller, damage zones, seats). A map vehicle with `"type": id` (and optional `"yaw"` in turns) drives with that
controller: the driver's forward / back become throttle / brake (reverse when stopped), right steers, Jump is the
handbrake, pitch drives flight; the ground height under it comes from collision, static walls stop it through the
package's collision response, and a hard crash damages it. Interact enters the nearest typed vehicle within 6 m
(driver seat first, then the next free seat) and leaves it at the first clear exit point when it is slow enough.
Passengers and gunners keep Fire / Aim / Reload / weapon keys; the driver does not shoot. Knocked, killed or
thrown-out occupants are ejected; a destroyed vehicle kills everyone inside. Vehicles without `type` keep the
legacy car. Typed vehicles run on the one vehicle system of every host (`engine/vehicles`, docs/driving.md): the
entity carries `vehicles::VehicleDrive`, `actor.vehicleTypes` become shared vehicle types (metres, seat lists; a hard
crash costs (impact - 8 m/s) x 10 health), and `engine/actor/ActorVehicles.cpp` is only the metre-map world query
(ground cast, box sweep against the triangle world, seat-list glue). An empty typed vehicle at rest sleeps (not
stepped) like every parked vehicle. FPS save version 10 stores the shared VehicleDrive record. `templates/fps_survival` has ten four-seat cars
(`rules/vehicles.json`). Test: `typedVehicles` in `tests/fps/tests.cpp`.

## Races

A map `"race"` block makes the typed map vehicles race: the shared race capability of every project, documented in
[races.md](races.md) (data, behaviour, HUD). The adapter is the shared `vehicles::VehicleRace` (engine/vehicles);
this module only seats the racers (`RaceSeats` in `engine/actor/ActorVehicles.cpp`): player `i` is seated as the driver
of the `i`-th typed map vehicle (ascending vehicle id); the race decides the round (at the round limit the leader
wins). ECS shape: the `vehicles::RaceState` world resource (racer `i` = slot `i`, `racers[i - 1]` = its car), the
cars are `Vehicle` + `vehicles::VehicleDrive` entities. FPS save version 10 appends the RaceState record only on
maps with a race; snapshots carry the player's `lib::mechanics::RaceView` (authority protocol 6). The client
shows the chase camera (rules / map `"camera": {"chase": {...}}`, first-person look: `"firstPerson"`; presets in
[camera-behaviors.md](camera-behaviors.md)) and the race HUD at the wheel and hides the weapon UI.

## Generated buildings (opt-in)

Map `buildings` lists [building-gen](../skills/world/aiverse-building-gen/SKILL.md) houses:
`{"pos":[x,y,z],"yaw":0..3,"seed":n,"building":{"width":12,"depth":10,"floors":2,...},"loot":[item types]}`.
At setup each becomes static collision boxes (ids from `kBuildingColliderBase`; slabs, walls with a door and
windows, a partition, switchback stairs, roof) sized for the FPS motor (radius, height, step height), and its loot
lies on the generated room spots (requires `mechanics.enabled`). The client draws them also on chunked maps.
`templates/fps_survival` has 25 houses of one to three floors. Test: `buildings` in `tests/fps/tests.cpp` (walk in
through the door, climb a floor with the real motor).

## Library bots (opt-in)

`actor.bots` holds [bots-combatant](../skills/ai/aiverse-bots-combatant/SKILL.md) settings (`{"preset":"normal","sightRange":120}`).
Map players with `"bot": true` then run that controller inside the authoritative tick instead of the reference
fallback bot: perception, reaction, bounded aim, reload, retreat and the battle-royale zone (the announced circle).
On top of it the module adds goals: wanted loot within 25 m (weapons it lacks, better gear, medkits, ammo) and
Interact when in reach, Heal when hurt and out of combat, and a point inside the next circle otherwise. Navigation:
one navmesh per generated building (0.5 m cells, built at setup from collision) plus straight steering in the open;
a goal inside a building outside the bot's region goes through its door, other floors through the stair flights
(the navmesh reaches the flight's hall point, the bot walks the flight straight). Bot state is saved (FPS save
version 9) and hashed. `templates/fps_survival` uses it. Tests: `houseBots`, `upstairsBot`, `houseNavigation`.

## Rotated colliders

A map collider with `"yaw"` (turns) is `{"id","center","half","yaw"}`: a box turned about +Y, stored as its 12
surface triangles (`engine/world/OrientedBox.h`), so casts, the motor and hits use the exact turned box. Its
collision ids are allocated from `kRotatedColliderBase`; the navmesh treats it by its bounding box. Test:
`rotatedColliders`.

## Gear (opt-in)

`actor.gear` (requires `mechanics.enabled`) is a [gear-tiers](../skills/gameplay/aiverse-gear-tiers/SKILL.md) catalog:
pieces with `id` 1..55, `slot`, `tier`, `durability`, `reduction` / `wear` (milli), `zones` (1 = head shots,
2 = every other hit) and `capacity` (extra inventory stacks, up to 24 in total). Loot type `200 + id` is a gear piece
(map `loot`, `actor.crate`); Interact equips it and the replaced piece takes its place on the ground with its
durability. With a catalog the gear replaces the legacy flat armor absorption. Broken pieces emit SimEvent 9
(`player` = wearer, `value` = slot); dying drops every worn piece. The own loadout travels in snapshots
(`Snapshot::gear`); FPS save version 10 stores loadouts. `templates/fps_survival` ships three helmet, vest and
backpack levels (`rules/gear.json`) spread near the spawns and in supply crates.

## Locomotion modes (opt-in)

A `locomotion` block (top level of the rules, or `rules.actor.locomotion`) moves characters through the shared
character-locomotion mode machine (`engine/mechanics/LocomotionModes.h`) instead of the plain motor. Keys are the
`locomotion.*` settings of docs/gameplay-settings.md (milli-units and ticks, same ranges and relations; `{}` = defaults);
the motor's `speed` / `sprintSpeed` / `jumpSpeed` / `gravity` and stances still apply. Rules without the block behave
and hash exactly as before: no locomotion state, no save or network bytes.

| Move | Input | How the host reports the world |
|---|---|---|
| Dash | Dash press (desktop **K**, pad RB): along the move direction, else the view | wall ahead from the side probes ends it |
| Wall slide / wall run | hold the move key into a wall / run along it after a jump | short side probes around the body: wall normal and collider id (same-wall reattach rule) |
| Grapple (pull or swing) | hold Grapple (middle mouse, **J**, pad LB); release lets go | the view ray up to `grappleMaxLengthMilli`; the hit point is kept while attached |
| Glide | hold Glide (**Space** held, pad A) while falling | — |
| Slopes, coyote time, jump buffer | as before | ground normal of the sweep |

The server sends each player its own locomotion state (snapshot-delta record kind 5); the client predicts the modes
with the same motor and reconciles them with the rest of the pose. Saves (version 10) append the per-character state
only with the block; the checksum includes it only with the block. Not reported by this host yet: ledges, ladders,
climbable walls, water and one-way colliders (the map format has no tags for them), so those modes stay idle here.
Tests: `aiverse_fps_tests` (`locomotionModes`: dash, wall run, grapple, save mid-dash, client prediction).

## Close combat (opt-in)

`actor.melee.enabled: true` adds an authoritative melee strike, guard, parry and lock-on.
It is independent of `actor.mechanics`; rules without the block behave as before.
Swing timing, input buffering, chaining and one-hit-per-target-per-swing come from the
shared `melee-combat` library (`engine/mechanics/Melee.h`).

| Behavior | Implementation | Current limits |
|---|---|---|
| Strike | Windup, active and recovery ticks; the blade tip is swept from its previous position each active tick against actor bounds widened by `radius` | One attack type, no combos or weapon-specific swings; no skinned animation |
| Guard | Held Block negates hits whose attacker is within `guardTurns` of the facing direction; Block also stops firing | No stamina or guard break |
| Parry | Pressing Block opens a `parryTicks` window; a strike landing in it deals no damage and staggers the attacker | Parry gives no riposte bonus |
| Stagger | A landed hit staggers the target and a parry staggers the attacker for `staggerTicks`; staggered actors cannot strike or fire | — |
| Lock-on | Holding LockOn selects the nearest living enemy within `lockRange`; strikes aim at the locked target until it dies or moves out of twice the range | Camera does not follow the lock |

Data: `actor.melee` accepts `enabled`, `damage`, `windupTicks`, `activeTicks`,
`recoveryTicks`, `bufferTicks`, `parryTicks`, `staggerTicks` (ticks; defaults scale with
tick rate), `range`, `radius`, `lockRange` (world units) and `guardTurns` (half-angle in
turns, at most 0.25). Simulation events: 3 = swing started, 4 = contact (value = target
slot | contact << 8, where contact is hit 0 / blocked 1 / parried 2). Melee state is saved
from FPS save version 7 and hashed when the block is enabled.

Desktop client controls when enabled: **Z** strike, hold **Left Alt** guard (press to
parry), hold **T** lock-on. The client has no melee viewmodel or hit sounds yet; check
it live before treating the feel as finished.

## Extended mechanics (opt-in)

`actor.mechanics.enabled: true` enables the extended authoritative systems. Existing
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
| Vehicles | Legacy: driver admission, exclusive control, steering, swept static collision, safe exit and weapon damage. Typed (`actor.vehicleTypes`, below): package controllers, seats with passengers and gunners | Legacy car: single seat on flat ground. Typed: no suspension geometry, no vehicle-vehicle collisions |
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

`actor.mechanics` accepts `knockdown`, `startArmor`, `startMoney`, `killReward`,
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
latest-message transport is bounded to 128 KiB. Snapshots are delta-compressed (above); interest filtering still decides what is sent.

All new outcome-affecting fields, objects and timers are saved and hashed.
`tests/fps/systems.inc` covers inventory, attachments, cancellation, vehicles,
ladders, prone, grenade effects, hit regions, penetration, ballistics, revival,
restoration, buying, planting/defusing, round reset and vaulting.

The current client HUD/viewmodel/smoke and audio changes require a successful live
visual/audio check. The automated GUI launch was interrupted; do not treat a compiled
client or the headless scenarios as proof of visual quality. Windows, web and mobile
integration of these new controls has not been verified.

### Opt-in animated remote FPS characters

The reference FPS client now supports an optional character model without
changing authoritative FPS simulation, saves, or network protocol:

```json
{ "actor": { "visual": { "model": "models/hero.glb", "scale": 1.0,
                       "heightOffset": 0.0 } } }
```

Place the model under the FPS project directory. An optional
`actor.visual.animationGraph` object uses the existing AnimationGraph JSON
format; otherwise, the client builds a basic Idle/Walk or Idle/Run blend
graph from recognized clip names. Input parameters projected from snapshots:
`speed`, `moving`, `dead`, `grounded`, `reloading`.

The reference client runs a separate presentation ECS World, not a gameplay
world, and computes remote actor poses per render frame. Invalid model/graph
assets retain the previous box rendering fallback. The procedural first-person
weapon remains unchanged. This path requires a real model and visual testing.


### Optional instanced GPU crowd skinning

Set `actor.visual.bakedCrowd: true` with a skinned
`actor.visual.model` and **no custom `actor.visual.animationGraph`**.
The FPS client then bakes the model's clips into a joint-matrix texture,
drives per-entity Idle/Walk/Run/Death through the shared
`AnimationCrowdComponent`, and submits
`Renderer3D::ModelDraw::baked` for GPU instancing. If a bake is
unavailable, it uses the existing palette graph or box fallback.
Complex avatars with IK, upper-body layers, and imported controllers
stay in palette mode. The palette path now uses the shared
`AnimationSignificanceComponent` / `assignAnimationBudgets`
rather than hard-coded per-distance pose sampling.


### Hot-reloadable FPS AnimationGraph

Optional `actor.visual.graphFile: "animations/hero.json"` loads a
project-relative AnimationGraph file instead of the generated Idle/Walk/Run
graph. The client polls for changed file timestamps on its presentation
thread (every 30 rendered frames), recompiles transactionally, retains the
last good graph after an invalid edit, and swaps immutable graph pointers
on existing presentation entities. Game/network simulation is unchanged.
Changing a graph restarts its visual state machine. This file is resolved
inside the current project; it must not contain parent-path traversal.

## Automatic MotionWeave CPU/GPU backend

Under `actor.visual`, optional `animationMode` is `"auto"` (default),
`"cpu"`, or `"gpu"`. Existing `bakedCrowd: true` remains equivalent
to forcing `"gpu"` when a suitable animation can be baked. Custom
`animationGraph` or `graphFile`, native callbacks, IK and layered
rigs remain on the CPU graph path.

In auto mode, the client chooses GPU baked instanced skinning for simple
imported clips at `gpuEnterActors` living remote characters (24 by default).
It switches back to full CPU graph below or at `gpuExitActors` (12 by
default), with hysteresis to reduce mode thrashing. Backend changes recreate
**presentation-only** animation entities; simulation actor state, game
networking and saves are not touched. A missing GPU bake retains CPU fallback.

CPU graph animation automatically uses the host-owned persistent
MotionWeave scheduler when 16 or more remote presentation actors exist.
Its cost-aware worker jobs avoid custom callback/rig operations, which
execute on the submitting thread after a barrier. Set
`actor.visual.animationStats: true` to see CPU/GPU path, worker count,
work estimates, jobs, and CPU timings in the FPS HUD. This is
presentation profiling, not deterministic simulation state.


### MotionWeave cosmetic Montages from replicated FPS actions

Optional project-local JSON assets can be authored in the editor
(`animations/montage/`) or through Workspace/MCP:

```json
{"actor": {"visual":{
  "model":"models/hero.glb",
  "montages":{
    "shot":"animations/montage/fire.json",
    "reload":"animations/montage/reload.json",
    "death":"animations/montage/death.json"
  }
}}}
```

Montage files reference imported clip names, sections, optional masks,
blend-in/out and cosmetic notifies. Missing or invalid files are logged
and skipped; no crash or authoritative gameplay action is generated.
While a death Montage is active the dead actor may still render
until its visual section completes. Replicated shot count, reload
deadline and health/life transitions are used for one-shot visual
triggers; stale/duplicate snapshots never replay the same Montage.
A newly visible actor doesn't invent a shot. The FPS game server,
hit detection, ammo, damage and reloading remain authoritative.
When any valid Montage is configured, the backend stays on the
CPU Graph path so its upper-body mask/sections can be evaluated.


When switching between simple CPU Graph and GPU Baked Animation,
MotionWeave now captures each remote actor's normalized idle/walk
clip phase and seeds the new presentation player. This avoids the
previous forced restart at clip time zero. MotionWeave preserves
confirmed replicated animation-action history across backend
changes, so shot/reload/death cosmetics do not replay. The
transition still cannot preserve arbitrary controller blends or
motion-matching state and remains limited to simple clips.

### Engine View for models and primitives

The FPS reference client maps both animated models and ordinary
scene meshes (cube fallback actors, collider debug shapes, world boxes)
to the shared **genre-independent** `aiverse::View`.
The renderer still receives the same world transforms, color and
draw flags: `View::submitOne` for ModelDraw and
`View::submitMesh` for simple Mesh packets. No FPS state is ever
read by the core View, and a missing animated character continues
to fall back to the visible cube.

For groups of independent world objects, the host now collects
`view::MeshObject` records into `view::ScenePacketBatch` and
submits them through `View::submitPackets`. The draw command
sequence for loot, vehicles, projectiles and planted objects
is unchanged. A deliberate batch boundary before the
royale-zone wall preserves the previous wall/crate ordering.
Only generic geometry enters View; game-specific rules stay
in the FPS host. `ScenePacketBatch` does not replace
Renderer3D's existing material/mesh batching logic.
