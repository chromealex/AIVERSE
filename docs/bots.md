# Reusable C++ bots and automatic navigation mesh

## Coverage

| Package | Coverage | Target |
|---|---|---|
| bots-rts | Full existing RtsAI through a project-local configuration/restore adapter | AIVERSE::aiverse_bots |
| bots-fps | Perception, remembered target, reaction, bounded aim, firing/reload, retreat, patrol, safe-zone movement | AIVERSE::fps_bots |
| navmesh | Automatic layered polygon mesh from static AABB collision; agent clearance/erosion, step links, connected regions, bounded routes | AIVERSE::navmesh |
| bot-matches | Headless actual-engine match runner and deterministic restoration scenarios | aiverse_bot_match |
| genre-bots | Genre-independent melee/shooter bot policies, squad roles + formations, objective choice over owner-supplied perception ([genre-packages.md](genre-packages.md#genre-bots-genre-bots-010)) | AIVERSE::lib_genre_bots |
| settlement-production (colony planner) | Rule-based colony/survival planner choosing builds, bills, gathering and work priorities from needs, stock, power and owner facts; emits player-equivalent ColonyCommands ([genre-packages.md](genre-packages.md#settlement-and-production-settlement-production-010)) | AIVERSE::lib_settlement_production |

Public headers live in `libraries/include/aiverselib/bots`. No Lua or project-specific engine edits are
needed. Executable host targets link the appropriate target. Registered OBJECT native modules call
`aiverse_native_use_bots(aiverse_native_<id> RTS FPS)` after `aiverse_native_module(...)`; choose only the needed
RTS/FPS/NAVMESH arguments. The helper propagates backend links to aiverse_game without creating an object
module dependency cycle. The targets exist
before the engine reads the project's native CMake. FPS symbols require the FPS module introduced by
the parallel FPS engine work; pin that engine revision together with this package.

`examples/native/libraries/BotConsumer.cpp` checks project-native includes and state persistence.
`examples/libraries/bots/main.cpp` is an executable integration example for both game modules.

## Host ownership and commands

Run bot controllers on the authoritative host **before** `sim.step()`. RTS bots call `sim.submit`
internally. FPS `fpsCommand` returns an ordinary `fps::command` for the host to submit via its existing
command/network route. Do not write Actor positions, health or weapon fields to drive a bot.
Do not run the same controller on every network client. Replay recorded commands without re-running
host AI. Bot difficulty never changes damage, health, resource balances or weapon fire rate.

Set map player `bot:false` for slots controlled by the new FPS adapter: `bot:true` selects the
reference FPS module's fallback bot until its first sequenced input. The library does not silently
replace this separate controller or editor/client ownership policy. Only one controller may own a slot.
Native C++ access is available, but the existing RtsNative hook itself belongs to RtsModule; it is
not an automatic FPS NativeModule lifecycle. Connect FPS controllers in the project's FPS host.

## RTS profiles and knowledge

Use `RtsBot(player, seed, settings)` with a project-owned JSON object:

```json
{"preset":"rush","thinkTicks":6,"firstAttackArmy":4,"maxAttackArmy":16}
```

Presets: `easy`, `normal`, `hard`, `rush`, `defender`, `economist`. Overrides include worker count,
wave sizes/growth, aggression, towers and scouts. Unknown keys and out-of-range values fail loading.
Existing `rules.ai` overrides still win when RtsAI applies its data configuration.
The adapter reuses the engine's economy, construction, production, research, combat, garrison,
terrain and planet tactics instead of implementing another strategy AI. Not every mechanic exists
in every game's data. Scripted AI remains the owner when the rules request it.

The existing RTS AI's knowledge policy is unchanged: this wrapper does **not** certify fog-fair
competitive behavior. FPS LOS/FOV guarantees below do not apply to RtsAI. Tests cover actual RTS
command production and restoration rather than claiming human-level play or competitive strength.
RtsAI retains its existing seeded host RNG, saved by its own serializer; the adapter adds no RNG.

`RtsBot::save/load` binds player, seed and exact settings JSON. Load is transactional through a
candidate AI object. Save/restore the simulation and every bot together. The terrain-cache snapshot
fix preserves its saved walkability mask if a loaded bot is immediately saved before thinking.

## FPS profiles and decision state

Presets: `easy`, `normal`, `hard`, `guard`, `rusher`, `survivor`. Settings are in **simulation ticks**
and **turns** (one turn = 360 degrees). Reaction and sensor cadence therefore depend on tick rate.

```json
{"preset":"normal","reactionTicks":12,"sensorTicks":6,"memoryTicks":180,
 "sightRange":80,"halfFov":0.2,"turnPerTick":0.016,"aimError":0.0005,
 "preferredRange":15,"retreatHealth":25,"strafe":250,"navigationBudget":2048}
```

`fpsConfig` validates all fields. `fpsCommand(sim,module,slot,config,waypoints,state,objective,mesh)`
returns at most one command per increasing tick. Target selection is stable by distance then slot.
Targets must be alive enemies within FOV/range and unobstructed by collision. Current stance height,
weapon range and temporary blindness are read from the expanded FPS module. Lost targets retain
only their last observed position until TTL expires. A target respawn has a new life ID and resets
reaction. Aim changes are bounded per tick; deterministic aim bias models inaccuracy without using
platform randomness. A friendly actor in the firing ray suppresses fire.

Empty magazines request the actual reload command. Low health/reloading selects the nearest authored
occluded cover waypoint, or moves away from the visible threat if no cover is defined. Navigation
must approve the route; unreachable cover is a blocked decision, not a teleport. Automatic navmesh
currently supplies walkable space, not tactical cover annotations. Zone movement outranks chasing.
Patrol can use authored waypoints, a project objective, or reachable polygons in the generated mesh.

Decision enum values are Idle, Patrol, Investigate, Engage, Reload, Retreat, Zone, Blocked. They are
codes for diagnostics, not localized display strings. More complex game goals (bomb placement,
hostages, loot pickups, vehicles, weapon selection) require a game adapter for the actual mechanics;
the generic adapter does not drive those newer FPS systems automatically. Use the authoritative
FpsSystems actions when extending objectives rather than synthesizing inventory or round state.

## Save and rollback

FPS state is explicit `FpsState`: slot/life, target/life, sensor/acquisition/observation ticks, last-known
position, input sequence and patrol cursor. `saveFps/loadFps` use versioned engine Archive encoding;
invalid/truncated/trailing data is rejected without replacing state. `storeBytes/loadBytes` bridge
saved bytes to NativeState-compatible arrays. Those arrays hash/save with the native owner.
Do not store mutable bot objects on NativeModule instance members. For host controllers include bot
bytes in the host checkpoint alongside the simulation and restore both before generating more input.
FPS state bytes do not embed config/geometry: bind the same pinned settings, map, agent dimensions
and rebuilt mesh in the host save envelope. Do not reuse future state after simulation rollback.

## Automatic navmesh

`buildNavMesh(collision, config)` builds actual rectangular walkable polygons, adjacency and connected
components, with separate layers for stacked floors. It uses the FPS world's authoritative AABB
colliders: floors and roofs supply horizontal surface candidates. The configured radius erodes edges
and narrow passages; height rejects ceilings; stepHeight controls traversal between neighboring levels.
Exact rectangle-union support checks prevent bridging floor holes or missing seams between floor boxes.
Body sweeps check transitions. Vertices use Fixed and construction order is deterministic.

```cpp
using namespace aiverse::lib::bots;
NavBuildConfig config;
config.bounds = {{-18_fx,-1_fx,-18_fx},{18_fx,6_fx,18_fx}};
config.cellSize = 1_fx;
config.radius = module.config().motor.radius;
config.height = module.config().motor.height;
config.stepHeight = module.config().motor.stepHeight;
auto built = buildNavMesh(module.collision(), config);
// Handle built.error if !built.mesh; keep the mesh immutable for this collision revision.
auto command = fpsCommand(sim, module, slot, fpsConfig(settings), {}, botState, {}, &*built.mesh);
if (command) sim.submit(*command);
```

`navPath` returns the cell corridor with exact endpoints, `navStep` supplies the next movement target.
BFS chooses the fewest connected cells with deterministic ordering. Routes never shortcut across holes.
A query fails for outside/off-mesh endpoints, disconnected regions, changed collision or exhausted
visit budget. Rebuild after collision changes; a hash prevents silently using stale geometry. Meshes
are derived immutable data, so saves rebuild them from the same pinned source rather than retaining
pointers. Host tools may export polygons with `navMeshJson` for inspection.

This is a conservative raster polygon mesh for **horizontal AABB surfaces**, not a Recast triangle
importer. Cell size controls accuracy; narrow legal passages may disappear if the resolution is too
coarse. Boundary strips smaller than one full cell are omitted. Slopes, triangle collision, jumping,
climbing, off-mesh links and streaming connections between independently baked regions are not yet
implemented. Do not claim those capabilities based on the word navmesh.

Limits: max 262144 sampled columns/polygons, 1024 collider candidates per column; defaults are 65536
and 128. Exceeding limits fails explicitly rather than truncating a connected world. Build once during
loading/tool execution, never every frame. Path search has a deterministic visit budget. Large maps
must bake bounded regions; the current implementation does not promise PUBG-scale runtime throughput.

## Examples and verification

The match runner loads the real project, disables the reference FPS fallback bot for controlled slots,
and steps actual simulation/commands. FPS uses automatic navmesh; no hand-authored route is required.
With no navmesh settings it infers a bounded region around the spawns. For large worlds specify a region.

```sh
aiverse_bot_match templates/skirmish examples/libraries/bots/rts-rush.json 36000
aiverse_bot_match templates/fps_arena examples/libraries/bots/fps-normal.json 7200
aiverse_navmesh templates/fps_arena examples/libraries/bots/fps-normal.json /tmp/arena-navmesh.json
```

The navmesh exporter refuses to overwrite an existing output. Example FPS regions target the arena;
edit bounds for training/survival/custom maps instead of silently using those bounds everywhere.

Run catalog checks, tests/libraries/run.py, external native/export consumer checks, root
aiverse_library_aiverse_bot_tests, aiverse_library_fps_bot_tests and aiverse_library_navmesh_tests. Engine integration
covers command-only control, firing/reloading, no-shoot through allies, occlusion/memory, actual motor
navigation around a wall and fresh restore comparisons. The RTS save fix also requires full engine
checks and every template match. No rendered UI changed. Windows, Metal rendering, mobile and web
runtime validation remain unverified.
