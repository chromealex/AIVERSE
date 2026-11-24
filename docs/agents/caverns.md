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

## Stage46 — motor-neutral ECS integration (2026-11-24)

Use `nav::installCaveMotorNavigationBefore(sim, hooks, position, motorSystem, maxEntities)` from `engine/nav/CaveEcsSteering.h` after the real movement system is registered, or `CaveGameplayRuntimeFrame::installMotorNavigationInSimulation`. The installer inserts repath and steering in that order immediately before the uniquely named existing motor and rejects duplicate navigation installation. No actor module, new motor, or position/velocity writer is created. `CaveWorldPositionSource` reads an optional signed-centimetre point from the current const ECS World and entity; do not capture component pointers across rollback. `readCaveTransformPosition` is the default for direct Transform following only; custom positions need no Transform component or collision callback for route search. Actual movement/contact checks remain with the motor.

Use `assignCaveMotorGoal` on an already authorized command/tick to create the route intent, follower and steering request; `cancelCaveMotorGoal` clears intent, `reportCaveMotorBlocked` invalidates the request and queues repath on matching epochs. Consume `caveMotorRequest` only after the producer has run this tick and only against the same committed provider. A missing request means no cave steering. Dynamic-obstacle cache invalidation remains the existing authority's responsibility. Each tick clears old requests, including those outside the rotating deterministic steering budget, missing route, canceled goal, and unavailable published source. Both SDF and environment revisions must match, and waypoint count and coordinates are bounded. If the budget is below entity count, only that tick's selected slots receive movement intent. Restoration invalidates derived caches without rewriting restored persistent route/intent/request bytes; the next tick validates epochs before movement. The component schema versions are unchanged. The authoritative runtime accessor `gameplayNavigationSource()` refuses reads while closed, quarantined or pending nav invalidations. This is an opt-in generic host integration API, not proof of a game-specific motor connection or native world publication. Never use the visual-only preview or lockstep relay as gameplay authority.

Native compile, save/replay, actual motor, physics and GPU acceptance: NOT RUN (features-first sequence). See `docs/agent-handoffs/caverns-stage46-2026-11-24.md`.

## Stage56a — shared GI publication (2026-11-24)

Use `engine/world/TerraWeaveCavernsPlanetGiPublication.h` for real `GiGeometryScene` publication through the existing CPU/GPU GI bridge. It leases exact baseline leaf IDs and globally reserved volume IDs, replaces the actual same-space triangles, and restores original material/geometry on retirement. Bind canonical navigation inputs first; existing GI/nav callbacks must not be overwritten. Require stable body-relative projection, host resource readiness and real exclusive reader barrier. Changed orbital/rebase transforms require coordinated retirement and republish. Never treat a recording-target fixture as GPU evidence. Actual default host wiring, flight/swim integration, surface transition acceptance and native tests remain pending.

## Stage56b — optional shared native host

Use `engine/world/TerraWeaveCavernsPlanetNativeHost.h`: `installPlanetCavesInNativeSimulation` registers existing map authority and canonical navigation in module setup; `CavePlanetCpuNativeHost` / `CavePlanetGpuNativeHost` bind existing Simulation, Box3D, SpatialWorld, GameView and real GI scene/bridge. Supply genuine host exclusion, coverage, baseline GI identity/material/projection and allocation proofs. Call beforeFrame at exclusive owner barrier, admitNativeTick for occupied/requested charts, beforeMotor after canonical capture, then the existing motor once. Restore notifications coalesce and invalidate derived native generation before frame publication; never write GPU state from simulation restore. bindIndirect refuses stale GI; shutdown explicitly, retain owner if it fails. Default GameClient and GameCore remain unmodified; their missing authoritative planetary physics and baseline GI owner must be supplied by a real host. Moving-body/rebase, swim/fly and transitions remain pending. No native tests run in this feature-first stage.


## Stage56c — coordinated GI coordinate transaction (source)

`CavePlanetGiPublication::reproject` / `CavePlanetNativeHost::reprojectGi` use exact fixed body-space cut/volume/original triangles, preserving original material indices, and publish the existing shared scene once with full GI history invalidation. Supply value-captured `CavePlanetGiProject` for all body IDs and `CavePlanetGiOtherProject` for every unleased chunk (explicit identity for unaffected chunks). Call at the existing exclusive frame barrier, before changing presentation coordinates; on refusal retain the old frame and retry. Coordinate light/environment/probe-focus inputs with the actual GI owner before binding; do not change Box3D simulation anchors or navigation revisions. This does not migrate GameClient or create baseline leaves; native host adoption, real GPU, performance and Mac QA remain pending. No new authored schema.


## Stage56d — native terrain GI source and view coordinates

Opt-in `CavePlanetGiBindings::baselineUnowned` + `baselineAlbedo` use exact `PlanetRenderer::captureCaveSurfaceSlots` before native staging; existing baseline geometry must match and missing leaves require host-reserved IDs and proof of no overlapping GI producer. Refusal retains the candidate for retry; restored terrain remains the host's ordinary GI coverage, so the host owns its eviction and ID lifecycle. `CavePlanetGiViewFrame::capture(planet.world(), frame, seconds)` captures the same LENS frame and orbit centers as the terrain renderer. Use `projector()` at construction and `remapFrom(previous, bodyForChunk)` at reprojectGi; the host maps every other GI chunk explicitly (including other independently moving objects via its own callback), coordinates lighting and retains the accepted frame until publication succeeds. No default GameClient adoption; actual native physics host and end-to-end GPU acceptance remain open.


## Stage56e — native volumetric locomotion

`physics::sweepCharacterVelocityOriented` reuses Box3D's capsule mover, true-plane sweep and velocity clipping, with no ground snap, gravity or Scene step. `stepAdmittedCavePlanetVolume` admits current scoped Flying/Swimming routes, checks the actual native proxy/Scene/environment lease, applies the route cap, sweeps, validates the resulting segment and publishes the same proxy/state once. Flying requires the existing host's read-only velocity proposal (zero-G chart orientation supported); Swimming reuses `LocomotionModes` and NEREID depth compatible with PHYSIS up, with caller-owned existing locomotion state, committed only after collision admission. No second ECS position writer, independent flight policy, water solver or new save schema. Host must choose one motor per tick; stateful flight proposals stage state until success. Surface/swim exits, climbing and actual playable host wiring remain separate; native tests not run.


## Stage57a — native cave travel handoff

Opt in to `registerCavePlanetTravel(sim)` before sealing the save schema. `CavePlanetTravel` records occupied region and pending mouth separately from route scope; `cavePlanetTravelPins` augments native host admission and streaming, even after route arrival or cancel. `requestCavePlanetCrossing` is a gameplay command; the existing motor chooses exactly one `cavePlanetTravelMotor` (exterior/interior/crossing). `commitCavePlanetCrossing` uses authoritative position readback, committed cut rim, capsule clearance, current native navigation lease and the actual collision owner's swept-body/exterior-coverage proof, then switches existing route scope without writing transforms. Refusal retains pins; cancel requires safe original-side occupancy proof. `changeCavePlanetTraversal` validates the new profile at current position and goal before replacing the existing route. Native API is not lockstep admission. Optional component changes save manifest; no old-save migration claimed. Actual exterior motor integration and live crossing QA remain pending.


## Stage57b — multi-tick crossing receipts

`CavePlanetTravel` schema v2 retains approach/clearance phase, last accepted tick and authored SDF revision of cap crossing. `advanceCavePlanetCrossing` takes actual swept movement and whole-capsule side proof from the existing motor owner, returns Refused/Approaching/Clearing/Completed. Keep exterior and cave coverage until Completed; refusal requires halt/recovery. A cap crossing alone cannot release occupancy. Entering additionally needs a traversable landing and goal; exiting requires whole-body destination clearance. Reverse crossing resets progress; changed SDF invalidates prior evidence. Use `cavePlanetCrossingGuidance` for chart-local rim approach/clearance targets; not a movement or path solver. No teleport, no native epoch persisted. New save manifest v2, migration/restore verification pending. No default client integration. The legacy one-step commit is for whole-body cleared steps only; use explicit multi-tick API for normal travel.


## Stage57c — staged native motor and traversal selection

`chooseCavePlanetTraversalGoal` selects legal ground/swim/flight for the actual start and goal using committed provider, PHYSIS gravity, NEREID immersion and explicit project capabilities (swim hysteresis; no granted flight). The existing pathfinder still computes the route. `CavePlanetNativeHost::stepCrossing` / `stepAdmittedCavePlanetCrossing` run the existing oriented ground motor or velocity sweep against the same Box3D proxy; require host proof of complete swept envelope and exterior coverage; classify the real capsule; submit motion only after receipt preflight. An irreversible proxy or ECS publication failure quarantines runtime; no false rollback claim. Caller owns exactly one Scene step, authoritative position and staged policy state. No new saved fields. Actual exterior coverage owner, playable host integration, automatic mixed-mode path planning and real QA remain pending.


## Stage58a — single native gameplay motor dispatch

Use `CavePlanetNativeHost::dispatchMotor` from the existing motor system with the bound Simulation/Scene, a tracked `CavePlanetTravel` entity and actual proxy/state. `CavePlanetMotorTickGuard` claims each entity/proxy at most once per physics tick; the same physics tick cannot be reused after the simulation tick advances. Restore clears only the derived dispatch receipt at the host barrier; the project must restore the actual proxy/state. `CavePlanetMotorDispatchInput` provides existing exterior controller, crossing policy/coverage, optional swim/flight capability selection and the one authoritative position publisher/readback. Dispatch routes exterior/crossing/interior, holds unavailable routes, defers mode-change movement until canonical repath, and quarantines on irreversible publication failure. Caller owns Scene::step, existing policy state and idle/gravity behavior. Do not mix direct lower-level motor calls with this dispatch path. No additional persisted state. Native example exposes dispatchPlanetCaveActor; runnable world/exterior coverage and actual QA still pending.
