# TerraWeave Caverns — Stage 43: ECS repathing, native collision, NEREID/PHYSIS epochs and nav caches

Date: 2026-11-24

## Branch, commits, status

Repository: `chromealex/AIVERSE`
Branch: `feature/terraweave-caverns-core-20261123`
**No merge to main.** Stage42 base HEAD: `c8502ab3765419871ac4201fb01ff73ae1cab578`.

Actual Stage43 commits acknowledged by the GitHub connector:
- `0c0ee8864e36e68b0f566daf31a4291999253653` — new `engine/nav/CaveSimulationNavigation.h`: persistent ECS route intents, bounded deterministic repath, follower system and rollback hook.
- `09382c9420bef8b97204af7c7f6d22075fe8097c` — new `engine/nav/CavePathQueryCache.h`: bounded route-result LRU.
- `dd3b23c22780e6ed1c6a9a4febac7a1c1269df7c` — new `engine/world/TerraWeaveCavernsNavigationEnvironment.h`: exact existing NEREID+PHYSIS sampling over frozen ECS snapshot.
- `4c07aa77911f19556f2ad17d8575b45eed376aba` — `engine/world/TerraWeaveCavernsNavigationData.h`: independent environment revision in immutable provider.
- `ee4d0f57080b7424597f8bbf43bb838314e5e5f3` — `engine/world/TerraWeaveCavernsNavigationPublication.h`: environment-only publication/invalidation without false Carve/Fill revision.
- `062e0a4c262a238ac776b454037e6856e186b115` — route-cache tuple include.
- `145285ad798f9107b8151d4fbeac7a6b3c01016d` — `engine/world/TerraWeaveCavernsRuntimeFrame.h`: opt-in Sim installation, cache, NEREID/PHYSIS environment publication.
- `a4b90d017b97c52d07a93e38a7757693a70f8ebe` — `nav::assignCaveGoal` on ECS and corrected per-agent retry deadline.
- `e96c6b85c5b15bd4f53a118d985f346624eb2acd` — invalidate path cache on actual physics blockage and Simulation restore.
- `9f571bbbfe371ab3a023c99e08c6be6b27d8c1d4` — wire cache invalidation through gameplay runtime.
- `b0df142a2d6473fee1808476f3365c984ab7bd58` — correct Archive ADL namespace for PointCm/CaveAgentProfile serializers.
- `f57802beefccefbfa4619bca43870a8b70d7f995` — new `engine/nav/CaveMovementPhysics.h`, actual SpatialWorld Fixed collision and optional native Box3D authority query.
- `da6c2f43edb5d5fe61751c9b6f8145bba8fe6ca5` — new `engine/world/TerraWeaveCavernsNativeNavMeshCache.h`, actual native NavMesh reuse.
- `97e11a4b9d6f5d208a6813c68d64f9bc92cf3bdd` — use native NavMesh cache in runtime and purge it on publication changes.
- `05127146a897a27d7b882befa9303d74e256e581` — custom solver callbacks bypass memoization (may depend on external dynamic state).
- `3649968170ae0185aee3e632c3cd0fb7b56f0f80` — `CavePathResult.environmentRevision` and strict source validation.
- `d14d90268b324d4e89408ce6c47945d18d2ca046` — `CaveRouteFollower.environmentRevision`; stale environment forces repath.
- `294b0fc906796811f8b49475377c229cd571121b` — persist environment revision, bump `caverns.route` schema to v2.
- `303cd2eab3f29b0cef8f9513a1373f820cb3a740`, `431f7910e53c37f69441b845b1ec7456faa6eff4`, `e0afc61ad3f948eae7ff0e36c62af5a67fbcd78d` — preserve environment epoch in direct A*/NavMesh solver outputs.
- `2cebb6ade2bd88859b6451d24103001ce21db7ef` — rollback rechecks both authored and environment route epochs.
- `b8e88de8f5b6f8c4aabe1408e07d65f1154e8844` — graph settings changes clear old routes.
- `23554e902cff14c50ee37283375f5ee6c0876a33`, `941f71c67f2ac88695110e8f286e37c042126185` — self-contained header include fixes.
- `2d8dfcca3e48818fb6ac096d4d656d9acbe01a91` — helper to publish frozen environmental state from the actual Simulation tick.
- `b993c50aea136d05491090e8e2eb3e0408c60505` — `skills/world/aiverse-pathfinding/SKILL.md` update.
- `c7d9795a5b67a6a08d20c9ddf3a77d399cc20523` — `skills/world/aiverse-terraweave-caverns/SKILL.md` update.
- `f95df870ec632c4c2b97bfefda0969967dfec2f6` — `docs/pathfinding.md` update.
- `48154e2cff4855a18d116079b3580e6776e3957b` — `AGENTS.md` integration ownership update.
- This document is the subsequent handoff commit. Read current branch HEAD for exact final SHA.

## Feature architecture

### Fixed-tick ECS navigation, goals, persistence and rollback

`engine/nav/CaveSimulationNavigation.h` defines `nav::CaveRouteIntent` with goal, solver name (`astar`/`hpa`/`flow`/`hybrid`/`navmesh`/custom), retry timing and search caps. It adds `assignCaveGoal(World&,Entity,...)` to set authoritative ECS intent and a matching `CaveRouteFollower` component, never a parallel FPS or RTS actor list. `nav::repathCaveEntities(Simulation&,CaveSimulationNavigationHooks)` runs deterministically in ECS dense-pool iteration order with a bounded per-tick repair budget. Failed queries honor per-entity retry deadlines, source-epoch mismatch marks `needsRepath`, and successful results replace the ECS path.

`nav::installCaveNavigationSimulation(Simulation&,hooks)` registers persistent `caverns.route` **schema v2** and `caverns.route.intent` v1, installs the two ordered `Simulation::addSystem` systems `caverns.navigation.repath` and `caverns.navigation.follow`, and registers a `Simulation::addStateRestoredHook`. The route stores both immutable SDF revision and independent environment epoch; both are serialized field by field with `Archive`. On rollback it invalidates derived cache and marks mismatched routes for repath. A late Box3D/SpatialWorld obstruction similarly invalidates memoized routes to prevent blindly reassigning a blocked cached result. Stage43 changed the route schema deliberately; loading old v1 saves requires an explicit migration.

`CaveGameplayRuntimeFrame::installNavigationInSimulation(sim,collision,budgets)` is a convenient opt-in adapter for a **real actual game authority**, not the visual-only GameClient or the input-relay `net::Server`. It provides the currently committed Stage41 navigation source and Stage42 shared named graph solvers, and takes an actual same-epoch collision callback. The host must call this during `GameModule::setup` before persistence registration is sealed and own deterministic tick ordering relative to Box3D/SpatialWorld/NEREID/PHYSIS. No unconditional `GameCore` installation was added because unrelated maps cannot be assumed to have a safe cave collision authority.

### Native movement collision

`engine/nav/CaveMovementPhysics.h` adds `makeCaveMovementPhysics(spatial,optionalBox3D,selfBody,config)`. The SpatialWorld route uses existing fixed-point conservative swept-AABB `cast` and destination `blocked` queries; the optional native Box3D route uses the game-owned `physics::Scene::raycast` and `overlapCapsule`, ignoring the moving entity's own body handle. The callbacks never create a second physics scene. **Box3D is floating point**; this check is appropriate to an authoritative server/host and cannot be represented as deterministic across heterogeneous peer-to-peer lockstep clients without additional agreement/prediction. Missing both owners is rejected and movement fails closed.

### NEREID/PHYSIS costs from real ECS

`engine/world/TerraWeaveCavernsNavigationEnvironment.h` reads real existing `lib::nereid::sampleWater(*frozenWorld,position)` and `physis::environmentAt(*frozenWorld,x,z,response)` (mapping XZ cm to configurable PHYSIS cells). Published NEREID submerged state determines swimming traversability and optionally adds integer surcharge for other agent types; PHYSIS traction responds to ice/wetness/snow and changes integer ground/climb path costs. These are pure read-only queries against one retained immutable ECS snapshot, not duplicated water/weather simulation. The owner can publish this via `CaveGameplayRuntimeFrame::publishNavigationEnvironment(frozenWorld,environmentEpoch,bounds)` or `publishNavigationEnvironmentFromSimulation(sim,bounds)`, called after a committed tick with ECS writers excluded.

`CaveNavigationPublication::publishEnvironment` advances an independent environment epoch but **does not falsely increment authored SDF sourceRevision**; it dispatches change notifications to all subscriber algorithm caches. Both route results and ECS `CaveRouteFollower` encode the environment revision, so changed water/weather forces reconsideration of a path even if the cave SDF mesh was not modified. Because environment publication is opt-in, hosts must actually call it on relevant committed NEREID/PHYSIS changes; this is not a background process.

### Native cache and resource budgets

`engine/nav/CavePathQueryCache.h` provides a bounded LRU of successful route results keyed by exact authored SDF revision, independent environment revision, algorithm, start+goal, full agent profile, and maxVisits. Defaults: 256 entries and 8 MiB of waypoint bytes. Cache is queried by `CaveGameplayRuntimeFrame::findPath` and invalidated by Stage41 publication callbacks, Simulation rollback, dynamic motion blockage, and graph-solver setting changes. Custom third-party solvers bypass result memoization by default.

`engine/world/TerraWeaveCavernsNativeNavMeshCache.h` stores up to four actual `engine/nav/NavMesh.h::Mesh` objects keyed by SDF revision, environment revision, `CaveNavConfig` and agent profile. Repeated native NavMesh queries reuse a mesh rather than building it each time. Stage41 events clear old mesh entries when authored/stream/environment state changes.

**This is not yet a full incremental HPA cluster or Flow integration cache.** Repeated identical A*/HPA/Flow/Hybrid query outputs are memoized. Cold HPA queries still construct abstract portals and cold flow queries still build reverse Dijkstra. A truly cross-query 3D HPA/flow-field object keyed by destination and streaming region is a later optimization.

## Verified ownership / limits

- Existing native `engine/actor/ActorModule.h` owns a real `world::SpatialWorld`, and the engine has a separate `physics::Scene` Box3D owner API. No automatic integration into this existing ActorHost was made because it may move bodies through its own character motor and needs explicit one-owner movement scheduling, command/save/replay policy and collision epoch. Installing two movement systems on the same actor is forbidden.
- The Stage40 GameClient cave adapter is visual only. This Stage43 simulation installation helper is opt-in; it is **not** proof that all default game characters immediately navigate through dynamic underground geometry.
- Existing `MapGrid` 2D A*/HPA*/Flow/Hybrid remains unchanged, and new shared generic `engine/nav` 3D algorithms from Stage42 are used instead; custom backends still supported.
- NEREID/PHYSIS environment mapping needs the real PHYSIS cell dimensions, authoritative commit timing and a real frozen ECS snapshot. In a no-water/no-PHYSIS map, callbacks naturally fall back without inventing simulated environmental state.
- The environment epoch uses a monotonic publication counter; replays/rollback must coordinate cache invalidation and source rebuild when replay time goes backwards. The provided restored-state callback marks ECS route freshness, but full native world rollback is owned by the authority game module.
- STRATA sidecar retirement and GPU paging remain incomplete from Stage38, and all-owner Box3D/SpatialWorld/GPU/NavMesh world recovery remains gated by Stage35/Stage40 native owner callbacks.

## Tests actually run

**None for Stage43.** The user explicitly requested features before testing. No CMake/CTest, C++ compilation/linking, deterministic replay/serializer tests, movement collision tests, Box3D, NEREID/PHYSIS samples, native NavMesh queries, benchmark or GPU acceptance was executed. The GitHub connector acknowledges source/doc commits only, not correctness or successful build. Do not report production-ready native gameplay pathing.

## Concrete next steps (Stage44)

1. Connect the opt-in authoritative cave navigation systems to the real actor/world-space ECS host with a single movement owner and correct collision epochs, avoiding duplicated motion alongside character controller.
2. Introduce shared long-distance streaming HPA/Flow integration caches keyed by graph region, goal, profile and author/environment epoch; support incremental dirty cluster updates instead of rebuilding every cold query.
3. Finish native owner recovery from Stage35 partial-commit failures and full STRATA-sidecar GPU fence retirement or bounded paging, maintaining every stage's agent handoff.
4. After functional completion, run full C++ compilation, ECS save/load migration, lockstep replay, native Box3D/SpatialWorld/NavMesh, NEREID/PHYSIS parity, and GPU checks before merging main.
