# TerraWeave Caverns — Stage44 ActorHost single-motor + persistent graph regions handoff (2026-11-24)

## Provenance

Repository: `chromealex/AIVERSE`
Feature branch: `feature/terraweave-caverns-core-20261123`; **not merged to main**.
Base Stage43 handoff: `docs/agent-handoffs/caverns-stage43-2026-11-24.md`.

Stage44 GitHub commits:
- `85bfde2d49896d9613df0f12d1d3069bf1486f18`: new `engine/nav/CaveActorMotorSteering.h`, cave waypoints -> native actor::Input steering without second motor.
- `3ae5c3e839e5fee5000ba9ee8890d3a6f79da957`: ActorHost CharacterBody-based deterministic repath using Stage42 shared graph solvers.
- `08eef6de546e365e798dd24de1148b1f194a0d69`: ActorHost optional per-map navigation source state.
- `7f3df02c3011ba56a517bd894d70ee2ec1a5fcfe`: ActorHost opt-in cave SDF collision geometry into existing SpatialWorld before character spawns; route system feeds existing motor.
- `73d32f8bbc7c5f83b8187fbd1795c402dfdedab7`: correct Json array API and steering order after stale input clearing.
- `84fea2e9b15e44b46aad81a7d1520dd93412ac67`: new shared `engine/nav/GraphRegionCache.h`: cached deterministic directed graph, HPA portals and bounded per-goal flow integrations.
- `9538e0377554443dc1eec6e83ae91b0e796e3076`: only fully integrated (not visit-truncated) flow fields may be cached.
- `754aae7f70cd92d98e568cdd64d1b3da05ca14e8`: new `engine/world/TerraWeaveCavernsNavigationRegionCache.h`: 3D dirty-bound projection to reusable regional graph updates.
- `d0c648c5c4236ceff8652141c00bc61912377ad1`: initialize region source before cluster-id construction.
- `86f594402c73d66a9f0951c66d77a805a53bb7d6`: CaveGameplayRuntimeFrame opt-in regional 3D caches and named solvers `regional.astar`, `regional.hpa`, `regional.flow`, `regional.hybrid`.
- `c3b39f3f780c4ba5a0d56fcfc93157ac1602e792`: actor steering respects per-route speed, fixes large-coordinate safety, adds custom resolver injection.
- `f71f8e33f5d99e97e547ae4a06ce211063eda955`, `b8f3ce37a672c3bbb6de8de3fc2569d57a0933e8`: ActorHost native custom route resolver and motor-speed hookup.
- `e01358d7ce3d606122c3fcd06222a36115f8b42f`: actor-specific ECS goal adapter (CharacterBody, not Transform).
- `42031f84fb6c7b5aa0e990c059134a6c8f40c3d4`, `144a26d6ffb6fbf0d923b9fc5e20c3539a327e2d`: public `ActorHost::navigateCaveActor` API, no new unvalidated network command.
- `5440bc3673456095822fdd0241ce0924afda5662`, `6635d40cd217ba1bf5c3eb36d33bb46703d339e2`: optional map-configured persistent ActorHost region cache and shared fallback.
- `1b7aa469e56ca9f4b6ce6c092044366510753649`, `68a818e17df03595d990e0e474bd304496d5991d`: explicit exclusive native-collider-commit notification to refresh ActorHost regional HPA/Flow and source epoch.
- `649d4c5609f67e4211a779f082de8cea538d5ab3`, `6a8c4cce9c1b9edd8a65eda6c9f15dfa50cba113`: pass a REAL shared immutable source lease into custom path callbacks rather than a borrowed unsafe pointer.
- `bbd85427f5b6cf23a72cf2abd6fe16a7c444f882`: `skills/world/aiverse-pathfinding/SKILL.md` update.
- `7ee8cc20e43739f58e55bb39ba225091fbce9e5f`: `skills/world/aiverse-terraweave-caverns/SKILL.md` update.
- `96aa723254b590e24f4d909445f85c1d606415bc`: `AGENTS.md` Stage44 actor and shared navigation owner contracts.
- `ecbff16e95e044fd0491ceeb7c265ff9c74e6866`: `docs/pathfinding.md` Stage44 API and limitations.
- This dated handoff commit is the final Stage44 commit. Read feature HEAD for exact latest SHA.

## What was implemented

### Shared world-space ActorHost instead of a second movement system

The existing `actor::ActorHost` stores authoritative character positions/velocities in `CharacterBody`, uses `actor::Input` for commands, and mutates physical state inside its own `moveActor` or `moveActorModes` motor. Directly invoking Stage43's `Transform` route follower on those entities would create a second independent mover and break physics and rollback.

Stage44 adds `nav::CaveActorMotorSteering` under shared `engine/nav`. `nav::repathCaveActorRoutes(sim,sharedImmutableProvider,perTickBudget,resolver)` processes only ECS actors with `CaveRouteIntent+CaveRouteFollower+CharacterBody`, uses the same full 3D cave graph/A*/HPA*/Flow/Hybrid as Stage42 (no new cave algorithm), and honours solver visit limits and retry deadlines. It passes a TRUE `shared_ptr<const CaveNavigationDataProvider>` to custom resolver callbacks, so asynchronous custom backends cannot retain a dangling borrowed provider.

`nav::steerCaveActorMotor` checks the active SDF/other revision, route's current waypoint, conservative native SpatialWorld cast, motor speed cap and actual CharacterBody position, then writes only normal yaw/forward input. It runs in the existing ActorHost actor tick AFTER stale network input has been cleared and BEFORE the current character motor. ActorHost continues handling gravity, crouch, locomotion modes, combat, vehicles, contact response and position. No default bot/controller replacement: this applies only to entities explicitly carrying a cave route. Vertical-only segments require an actual flight/swim/climb motor implementation; they are never faked by teleporting the actor.

`nav::assignCaveActorGoal(world,actor,goal,agent,algorithm,speed,maxVisits)` works with CharacterBody rather than requiring the general Transform component. `ActorHost::navigateCaveActor(sim,entity,...)` exposes it to a game's real authoritative C++ commands. `ActorHost::setCaveRouteResolver` accepts named custom algorithm callbacks, including those not registered under default graph solver names.

### Initial real ActorHost cave collision ownership

When the map includes `cavernsNavigation: {"enabled":true,...}` and a versioned `terraweaveCaverns` authored snapshot, ActorHost explicitly creates a frozen Stage39 indexed SDF and generates complete authoritative cave geometry for the requested list of 1..64 `collisionChunks`. Native collision is generated through the existing `prepareCaveVolumeCollision` path and inserted as triangle colliders into the **same ActorHost `collision_` SpatialWorld** used by player spawn checks, physics ray queries, hit tests and the standard character motor. Its triangle IDs use the reserved 0x60000000 range. The source is generated before spawn validation to avoid actors spawning through authored cave rock. No parallel collision engine or FPS-specific pathfinder is installed.

Cave authoring stays opt-in; maps without the block behave as before. The user can add an optional bounded `cavernsNavigation.region` with signed origin, (width,height,depth), cellCm, maxCells, clusterCells, maxFlows and an optional agent profile for warm region topology. If present, the ActorHost constructs and retains `CaveNavigationRegionCache` once, and its shared route resolver tries the cached region before falling back to the general bounded 3D solver.

### Reusable graph region & hierarchical/flow caches

New generic `engine/nav/GraphRegionCache.h` is a genre-independent derived navigation cache. It builds and retains actual directed node adjacency/passability and region membership, with cached cross-region portal edges. `refresh(newGraph,changedNodeIds)` updates only supplied changed nodes (callers must include a one-cell inbound-edge halo), then rebuilds only the outgoing HPA portals of those touched regions. Queries use the SAME `nav::findGraphDijkstra` for portal-graph route and constrained fine-graph refinement, with full-graph fallback; no duplicated solver algorithm.

It also retains complete reverse-directed Flow Field integrations keyed by goal under bounded LRU and reuses the field for repeated agent paths to that destination. An authored/environment/stream dirty graph event invalidates the flow fields and they recompute on demand. **This is NOT incremental Dijkstra relaxation within an existing field:** after edits the relevant full goal field may need rebuilding. The graph and HPA portal caches, however, support incremental dirty region updates.

`CaveNavigationRegionCache` projects half-open authored centimetre dirty regions plus a one-cell halo onto the loaded 3D graph, and feeds only those nodes into the shared graph cache. Stage41 `CaveGameplayRuntimeFrame::enableRegionalNavigation` exposes named `regional.astar/hpa/flow/hybrid` backends, which reuse persistent graph/portal/flow data. The navigation publication bus refreshes the region only after actual committed SDF/environment/stream notifications. Out-of-region queries fail closed, and ActorHost's configured resolver can use the standard Stage42 graph fallback instead.

### Native edit publication, still an explicit barrier

`ActorHost::acceptCommittedCaveNavigation(publishedProvider,change,exclusiveProof)` is an integration hook for live authoring. Another REAL game owner must first publish updated matching SpatialWorld collision plus any required Box3D/nav/GPU state under exclusive simulation/readers exclusion. The method then refreshes ActorHost's affected regional graph, swaps its immutable SDF provider and lets version mismatches trigger route repath. The callback cannot claim native collision was updated when it was not. Merely returning true without holding a genuine owner-wide barrier is unsafe. Stage44 does not independently implement the Stage35 all-owner dynamic Carve/Fill transaction for ActorHost.

## Changed/new paths

New:
- `engine/nav/CaveActorMotorSteering.h`
- `engine/nav/GraphRegionCache.h`
- `engine/world/TerraWeaveCavernsNavigationRegionCache.h`

Modified:
- `engine/actor/ActorModule.h`
- `engine/actor/ActorModule.cpp`
- `engine/world/TerraWeaveCavernsRuntimeFrame.h`
- `skills/world/aiverse-pathfinding/SKILL.md`
- `skills/world/aiverse-terraweave-caverns/SKILL.md`
- `AGENTS.md`
- `docs/pathfinding.md`
- This handoff.

## Test status and exact limitations

**No tests were run** in Stage44: no native compilation, no CMake/CTest, no ActorHost tick, Box3D, SpatialWorld collision runtime, NavMesh, GPU preview, deterministic replay, custom path callback, HPA portal/Flow cache parity or benchmark. User explicitly requested completing features before tests. GitHub acknowledgements prove source commits only; do not claim native code compiles or gameplay is production-ready.

Remaining:
1. The initial ActorHost cave SpatialWorld collider set is static. Full runtime Carve/Fill requires native reversible triangle replacement and binding to the current Stage35 game world owner with genuinely exclusive reads and compatible Box3D/NavMesh/GPU publication. The `acceptCommittedCaveNavigation` hook is only the derived cache/source update after that owner commit.
2. Long-distance region streaming is bounded by configured max node count. Cached HPA portals handle a bounded region and are invalidated incrementally; flow integrations are kept per destination but fully discarded on dirty updates. A truly incremental Flow repair algorithm over changed reverse edges has not yet been implemented.
3. The actor motor is ground-oriented. This stage does not invent vertical flight/swimming/wall-climbing commands for it; projects with those modes must connect their actual locomotion controllers. Flying/swimming 3D path solvers remain available in `engine/nav`.
4. Actors only follow an explicit `CaveRouteFollower` assigned by a game-authority C++ command. No new unvalidated packet-level control command was added; it must be assigned through the authoritative game's normal command validation/replay mechanism. Existing actor controls, bot AI and combat remain the default.
5. Stage40 GameClient/Editor Play retains visual-only cave streaming. `net::Server` remains a lockstep input relay, not a game simulation. No genre-specific duplicate navigation algorithms have been created.
6. Native STRATA sidecar retirement, large-shard GPU paging, full-recovery of mixed native Box3D/SpatialWorld and legacy 2D MapGrid solver migration remain outstanding before final feature-complete QA.

## Stage45 next

Implement a real safe dynamic Authoring/Carve/Fill collision publication for the existing ActorHost (including old collider ID retirement, world exclusivity, cross-owner version validation, and actual game route cache invalidation), and where applicable a native Box3D scene owner, without ever creating a second character motor. Add per-agent movement policies for 3D flying/swimming/climbing only through their real shared locomotion owners. Keep all features genre-neutral and maintain mandatory per-stage skills and handoff. Once the user says functionality is complete, perform a full feature branch build, fixes, deterministic replay/save/physics acceptance and GPU verification, then decide about merging main.
