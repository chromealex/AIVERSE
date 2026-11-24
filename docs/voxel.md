# Voxel worlds (blocks you can dig and build)

Seven header packages for Minecraft-like games: a mutable world of 16x16x16 block chunks, its light, meshes,
generation, block updates, mob navigation and network streaming. They close the core of gap W2 in
`docs/capability-status.json` (mutable volumetric world) and part of W1 (regional saves, streaming of mutable
state). A playable game on top of them is the `voxel` module (`engine/voxel`, template `templates/voxel_sandbox`,
section "The game" below).

| Package | Header | Layer | Skill |
|---|---|---|---|
| voxel-world | `libraries/include/aiverselib/systems/voxel/VoxelWorld.h` (+ Types, Codec, Query, Data, Persistence) | simulation | `skills/world/aiverse-voxel-world` |
| voxel-light | `libraries/include/aiverselib/systems/voxellight/VoxelLight.h` | simulation (derived) | `skills/world/aiverse-voxel-light` |
| voxel-mesh | `libraries/include/aiverselib/systems/voxelmesh/VoxelMesh.h` | presentation | `skills/visuals/aiverse-voxel-mesh` |
| voxel-gen | `libraries/include/aiverselib/systems/voxelgen/VoxelGen.h` (+ Data) | simulation | `skills/world/aiverse-voxel-gen` |
| voxel-ticks | `libraries/include/aiverselib/systems/voxelticks/VoxelTicks.h` (+ Data, Persistence) | simulation | `skills/world/aiverse-voxel-ticks` |
| voxel-nav | `libraries/include/aiverselib/bots/voxelnav/VoxelNav.h` | simulation | `skills/ai/aiverse-voxel-nav` |
| voxel-replication | `libraries/include/aiverselib/net/voxel/VoxelReplication.h` | host (network) | `skills/network/aiverse-voxel-replication` |

How to put them together into a game: `skills/workflow/aiverse-voxel-game`. Example data:
`examples/libraries/voxel/` (`blocks.json`, `worldgen.json`, `ticks.json`).

## Data model

- 1 world unit = 1 block. Chunk coordinates are block coordinates `>> 4`; the world has a vertical chunk range
  (`VoxelBounds`, default -4..19, i.e. y -64..319) and a horizontal limit of 2^22 chunks; coordinates stay inside
  ±2^27 blocks.
- A chunk holds `blocks` (uint16 ids), `meta` (fluid level: `& 7` 0 = source, 1..7 distance; `& 8` falling; free
  for orientation in other blocks), `light` (sky `<< 4` | block), `revision` (bumped by every simulated edit).
- Block types (`blocks.json`): shape `cube | cross | fluid | none`, layer `opaque | cutout | translucent | none`,
  `solid`, `replaceable`, `opacity`, `emission`, `breakTicks`, `unbreakable`, `drop` / `dropsSelf` / `dropCount`,
  textures per face, fluid group / spread / ticks / infinite source, `falls`, `growInto` / `growChance`.
  Air is id 0 and implicit; ids follow list order and are stored in saves, so never reorder a shipped list.

## What is state and what is derived

| Data | Hashed / saved | Notes |
|---|---|---|
| Blocks, meta, revisions of resident chunks | yes (`VoxelWorld::hash`) | |
| Edited chunks (resident or parked after unload) | yes, record 120 | untouched chunks are regenerated from the seed |
| Pending block updates | yes, record 121 (`voxelTicksHash`) | |
| Light | no | deterministic; recompute after load (`lightPending`) |
| Meshes, `renderDirty`, `lightDirty`, `edits` | no | presentation and transport queues |

After `loadVoxelWorld` the world is empty with every edited chunk parked; the host must make the saved resident
chunks resident again (unpark edited ones, regenerate the rest) before the next tick. Then the hash equals the
saved world's hash (checked by the tests and by `aiverse_voxel --verify`). Record kinds 120..127 are reserved for
voxel packages (AGENTS.md area table).

## One tick (authoritative side)

1. Streaming: for every player wanted columns -> `unpark` or `VoxelGenerator::generate` -> `insert`;
   far columns -> `unload` (edited chunks are parked). Light new chunks (`lightPending`).
2. Commands: break / place after `voxelRaycastSolid` and `voxelOverlapsSolid`; `set` returns the old block ->
   `relightBlock`, `notifyVoxelChange`.
3. Bodies: `voxelMoveBox` for players, mobs and dropped items; queries treat unloaded chunks as solid.
4. `stepVoxelTicks` with the simulation Random; relight every reported change.
5. Mobs: `voxelSpawnSpots` (light level, night darkening), `voxelPath` under a node budget.
6. Network: `pumpVoxelPeer` per client, then clear `VoxelWorld::edits`. Clients apply messages to a mirror world,
   light and mesh it (`voxelMeshInput` -> `meshChunk` on workers).

## Measured (`aiverse_voxel --radius 6`, `examples/libraries/voxel`)

Dev VM, Linux ARM64 (4 cores, one thread used), GCC 11 `-O2`; 1356 chunks (113 columns x 12 chunks):

| Step | Result |
|---|---|
| Generation | 287 ms total, 0.21 ms per chunk |
| First light of all chunks | 441 ms, 0.33 ms per chunk |
| Meshing | 154 ms, 0.11 ms per chunk (793 non-empty chunks, 206,728 quads, 23 MB of vertices) |
| 1956 random surface edits with relight | 5.7 ms (about 3 us per edit); re-mesh of the 202 dirty chunks 28 ms |
| Save (regions + NativeState), restore, regenerate, compare | 304 ms, hash equal; 4 region files, 44 KB |
| Streaming a radius-4 view to an empty mirror | 588 chunks, 143 KB, 28 ms; mirror equal |
| Mob path over the terrain (20,000 node budget) | 52 steps, 22 ms |

Phones and web are not measured. Budget meshing and light per frame on the client, and A* per tick on the server.

## Determinism

Simulation packages use integers, `Fixed` and the engine `Random` only, `std::map` / `std::set` order and fixed
neighbour order. The generator is pinned by a golden hash in `tests/libraries/voxel_tests.cpp`: a change of the
algorithm changes every unsaved chunk of existing worlds, so it must be deliberate and recorded here.

## The game: `engine/voxel` (template `templates/voxel_sandbox`)

A playable sandbox on top of the packages: `game.json` `"module": "voxel"`.

| Part | Where | What |
|---|---|---|
| Simulation | `engine/voxel/VoxelGame.h` | players (walk, sprint, sneak without falling off edges, swim, automatic step up one block, fall damage), digging with break time and drops into a 9-slot hotbar, placing (not into bodies), torches and light, fluids and falling blocks, day and night (`dayTicks`), mobs that spawn in the dark, chase players along voxel paths, attack and burn in daylight, respawn, column streaming around players with parking of edited chunks, hash and save (`Simulation::serialize`) |
| Session | `engine/voxel/VoxelSession.h` | authoritative server and client over any `net::Conn`: hello with content id, inputs, snapshots (own player in full, nearby players and mobs), chunk streaming and edits through voxel-replication, client prediction by replaying unacked inputs on the mirror world |
| Client | `apps/voxel/client.cpp`, `engine/voxel/view/VoxelView.h` | chunk meshes through `Renderer3D` (unlit, vertex colours from block `color`, face shade, AO, sky light scaled by daylight, warm block light; translucent water), fog and sky colour by time of day, mobs and players as boxes, block outline, crosshair, hotbar, health, break progress, F3 debug |
| Server | `apps/voxel/server.cpp` | `aiverse_voxel_server --project templates/voxel_sandbox [--port 27200]`: UDP 27200, WebSocket 27201 |
| Tests | `tests/voxel/game_tests.cpp` | determinism, walking, digging and building, torch light, save and identical continuation, night mobs, loopback session (mirror equals server, prediction, edits, wrong content rejected) |

Run: `aiverse_voxel_client` (single player with a local server), `aiverse_voxel_client --listen` (friends connect with
`--connect udp://<host>:27200`), `aiverse_voxel_server` for a dedicated world. Rules: `blocks`, `voxelGen`, `voxelTicks`
and `voxelGame` (tick rate, radii, reach, creative, day length, mobs, start items, vertical chunk range) in `rules.json`.

## Not done

- Textures: blocks are drawn with their `color`; texture arrays for block art and the `textures` layers are not
  used by the client yet. Mobs and players are boxes (no models, no animation).
- No dropped item entities (drops go straight into the hotbar), no crafting screen or inventory beyond the hotbar
  (`skills/gameplay/aiverse-inventory` holds the rules), no hunger.
- The client was compile-checked on Linux only; it has not been run on macOS / Windows / phones / web here. Touch
  and gamepad controls are not mapped. The editor's New Project list does not offer the template yet.
- Packed vertex format, LOD for distant chunks, snapshot compression; structures (W3), rivers; redstone-like
  signals; 3D fire; flying / climbing mobs, doors in paths.

## Checks

```sh
g++ -std=c++20 -O2 -Ilibraries/include -I. tests/libraries/voxel_tests.cpp engine/data/Json.cpp engine/core/Fixed.cpp -o /tmp/voxel_tests && /tmp/voxel_tests
cmake --build <build> --target aiverse_library_voxel_tests aiverse_voxel aiverse_voxel_game_tests aiverse_voxel_server aiverse_voxel_client
ctest --test-dir <build> -R '^(library_voxel|voxel_headless|voxel_game|voxel_server)$' --output-on-failure
<build>/aiverse_voxel --content examples/libraries/voxel --radius 6 --verify
```
