# TerraWeave Caverns — mandatory AIVERSE area rules

This is the Caverns-specific supplement to the modular root `AGENTS.md`. Follow *all* common contracts in `docs/agents/contracts/`, particularly architecture, world ownership, reuse, workflow and build/testing. Read `skills/world/aiverse-terraweave-caverns/SKILL.md`, `skills/world/aiverse-pathfinding/SKILL.md`, `docs/pathfinding.md`, and the latest applicable `docs/agent-handoffs/caverns-stage*-2026-11-24.md` before changing cave code.

## Source and resource ownership

- The authoritative signed-centimetre SDF is immutable and versioned. Cave geometry, streaming, physics collision, native navigation and GPU visuals must derive from one committed epoch. CPU worker completion does not ACK gameplay publication.
- Cave CPU/streaming/physics APIs live in `engine/world/TerraWeaveCaverns*.h`; presentation GPU ownership lives in `engine/render/TerraWeaveCaverns*.h`. Do not invent new genre-specific cave streams or duplicate owners. Retain GPU resources until genuine completion proof; STRATA sidecars still need explicit idle in Stage38.
- `CaveGameplayRuntimeFrame` coordinates stream and all-owner publication with a real exclusive game-world barrier. On partial native commit failure, quarantine and rebuild from the last-good authored SDF. Preview-only `GameClient` / `HostedClient` rendering is NOT an authoritative Box3D/SpatialWorld/NavMesh commit.
- The existing `engine/net/server/Server.h` is a lockstep **input relay**, not a simulation host. Do not install GPU, Box3D or a second SDF authority in that server.

## Algorithm-neutral navigation

- The generic implementations of graph A*, Dijkstra, HPA-style search and Flow Fields live under `engine/nav`. Caverns provides the common SDF navigation data provider, 3D grid, portals, integer costs, dirty-range events, adapters and custom solver registration. Native NavMesh is just one optional solver. Do not copy pathfinders into Caverns, actor, FPS or RTS.
- Existing `game::MapGrid` A*/HPA*/Flow/Hybrid remain specialized to 2D path-layer state; they have not been migrated to generic 3D cave graphs. Do not claim otherwise.
- Stage43 ECS navigation persists `CaveRouteFollower` schema v2 and `CaveRouteIntent` v1 with SDF/environment epochs and rollback invalidation. NEREID and PHYSIS are read from the same frozen authoritative ECS World rather than duplicated water/weather simulations. Box3D float queries must not be treated as deterministic across cross-platform lockstep peers.
- Stage44 `engine/nav/GraphRegionCache.h` and `engine/world/TerraWeaveCavernsNavigationRegionCache.h` cache derived 3D adjacency, HPA portals and goal-based flow. Dirty updates rebuild affected graph edges and portal groups; flow integration is invalidated rather than incrementally repaired.

## Stage45 architectural correction (binding)

Stage44 `ActorHost` integration is **superseded and reverted**. `engine/actor/ActorModule.h` and `ActorModule.cpp` returned to their pre-Stage44 contents. Obsolete `engine/nav/CaveActorMotorSteering.h` and ActorHost-specific collision headers were deleted. DO NOT reintroduce actor/genre-specific imports in Caverns or shared nav.

Use `engine/world/TerraWeaveCavernsSpatialRevisionOwner.h` for a generic versioned SpatialWorld collision candidate; publish only with actual native world quiescence and all necessary other-owner coordination. Use `engine/nav/CaveEcsSteering.h` to emit a versioned ECS desired-motion intent without writing physical position or velocity. An actual game's existing motor is the **only** position writer. `nav::installCaveRepathSimulation` installs fixed-tick route persistence and repath WITHOUT a Transform follower. Do not install two movement controllers on the same entity. External custom solvers remain valid through `CavePathRouter`.

## Stage handoff and verification

Read `docs/agent-handoffs/caverns-stage45-2026-11-24.md` and later dated handoffs. Stage44 handoff is historical/superseded where it mentions ActorHost. After each stage update the skill and independent handoff, with exact SHA, paths, actual PASS/FAIL/NOT RUN evidence, risks and next work. The Caverns feature branch contains many source changes **not yet natively compiled or acceptance tested**; do not claim them complete. All modular main contracts remain binding, including the Mac quick gate before any merge into main.

## Main sync (2026-11-24)

Main commit `71aa5c93afa5993c4e45ba39f1bf5454343389ea` was merged into Caverns feature commit `6a9c437da0208dc9060da0057d5be93545a32294`. AGENTS.md conflict resolution keeps main's modular entry point and this area-specific file; main remains unchanged.
