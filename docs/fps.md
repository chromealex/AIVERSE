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
complete animated weapon asset or audio system. Damage and ammo are server-owned.

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
protocol/save version 2; earlier experimental FPS saves and version-1 peers are
rejected. RTS save/protocol versions are unchanged.

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

Next useful mechanics: ADS/scopes; recoil and movement-dependent accuracy; head/body/limb
hitboxes and armor; bullet penetration and ballistic projectiles; frag/smoke/flash grenades;
pickups, dropped weapons, attachments and loot; interaction/revive; buy/freeze/bomb rounds;
spectator/death camera; sound propagation/footsteps; vaulting, prone and vehicles.

### Projectile modifier chains

Weapons with positive `bulletSpeed` can opt into the shared `flight` block: ordered Split,
Ricochet, Fly Through and scalar modifiers. This works independently of `mechanics.enabled`;
`flight.gravity` defaults to `bulletGravity`. Authoritative 3D sweeps, actor-life hit history,
material penetration, save/rollback and existing snapshot presentation are supported.
See [projectile programs](projectile-programs.md) for the JSON schema and trajectory semantics.
