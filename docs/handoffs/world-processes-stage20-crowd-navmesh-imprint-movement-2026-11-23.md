# AIVERSE World Processes — Stage 20: CROWD navigation + IMPRINT live movement

**Date:** 2026-11-23. **Branch:** `feature/world-processes-experimental`. **PR:** https://github.com/chromealex/AIVERSE/pull/112 (DRAFT). `main` unchanged.
User request: implement functional integrations first, tests later.

## Implemented: CROWD pathfinding with existing AIVERSE NavMesh

**Source:** `libraries/include/aiverselib/world/CrowdRoutes.h`, commit `ca72e4b52a69cebecab8dbc04fbdeefe651e9ccc`.

This adds `crowd::Waypoint` and persistent `crowd::Route` (bounded list of at most 128 integer X/Z route points, cursor, navigation revision, arrival radius, last sampled tick). `crowd::assignRoute(World&,Entity,std::vector<Waypoint>,revision,radius)` is called at the authoritative command barrier. `crowd::advanceRoutes(World&,tick)` advances cursor by actual agent proximity and writes each agent's next movement goal into the existing `crowd::Agent` component, immediately before `crowd::advance` handles slot allocation and local avoidance. It never changes a NavMesh or introduces one into the deterministic ECS.

`crowd::installRoutes(Simulation&)` registers `crowd.route` persistent schema v1, plus the ordered route system. `WorldProcessesSuite.h` now installs routing before `crowd.avoidance` (commit `a81871bec7fdf1a5fe0b788a6d5c6cce7c9086d0`).

**Native adapter:** `examples/world/world_processes_native/CrowdNavMeshBridge.h`, commit `08e772512518e4c060f55a18fbf02573888ee2eb`.

`planCrowdNavRoute(World&,const nav::Mesh&,const TerraWeave EarthworksLayer&,Entity,start→target quantized cm,options,error)` invokes **existing** `engine/nav/NavMesh.h` `nav::Mesh::findPath` using the actual TerraWeave soil height for start/destination Y. Waypoints are converted from Fixed metres to bounded integer centimetres once and stored in `crowd.route`, so replay/rollback loads deterministic waypoints and is independent of any subsequent float presentation. A route is rejected if no NavMesh path exists, the graph is unbuilt, or waypoint/visit budgets are exceeded.

**Ownership:** nav::Mesh remains the one produced by the user's existing NavMesh/TerraWeave system and is rebuilt/refreshed on the nav owner barrier; CROWD stores only a route snapshot and its cursor. After a real terrain/NavMesh revision change the project host must call `planCrowdNavRoute` with its new revision; this stage does not autonomously access mutable TerraWeave workers or re-route every NPC.

## Implemented: IMPRINT physical/movement trail production

**Source:** `libraries/include/aiverselib/world/ImprintTrails.h`, commit `3abc41c459799c8cfedb966e4206a2179cf73fea`.

New persistent `imprint::Tracker` stores a full-generation source/target Cell link, last and current quantized X/Z, emission spacing, footprint/scent/residue rates, motionTick/emittedTick and initialization state. `imprint::sampleMotion(World&,Entity tracker,tick,x,z)` receives a validated authoritative movement sample without using a second transform authority. `imprint::stampMotion(World&,tick)` stages all tracker increments after the completed movement step, emits each movement footprint only when distance threshold is met, atomically updates the real `imprint::Cell` pools and the tracker clocks. Multiple actors may deposit into the same cell. `imprint::installTrails` registers `imprint.tracker` persistence and the system **before** existing `imprint::advance` (decay + diffusion).

**Direct source integration:** `libraries/include/aiverselib/world/ImprintCrowdBridge.h`, commit `e8e69a249d523f60ad087c11d8a8f7757a97a5b8`. `sampleCrowdFootprints(World&,tick)` reads actual `crowd::Agent` ECS after its `sampledTick` and applies position samples to opted-in IMPRINT trackers. If `WorldProcessesSuite` enables both CROWD and IMPRINT, the normal system registration automatically installs the bridge in the order:

1. `crowd.nav_routes`
2. `crowd.avoidance`
3. `imprint.crowd_sampling`
4. `imprint.motion_trails`
5. `imprint.diffusion`

Suite ordering commit `62e745fbaf0341a26aa3d685956539000976a6ca`; base trail installation commit `c258399ac138fcc8a3d604059b245143fdebac8d`. The host is responsible for creating `imprint::Tracker` components with correct source and cell links, or it can use `sampleMotion` from an unrelated Box3D/ActorMotor/game movement source. No prefab/genre-specific duplicate ECS trackers are introduced.

## Scope and hard limitations

- CROWD still uses simple local collision rejection, **not a full ORCA/RVO solver**. Existing engine/game/Avoidance and nav::Mesh remain separate, and no claim of automatic actor transform updates/animation is made.
- `crowd::Route` contains quantized 2D waypoints only; height is resolved through the navmesh, not stored as an authoritative CROWD Y coordinate. Multi-floor selection remains host-specific.
- IMPRINT tracks quantized motion in ECS and emits into an explicit target `imprint::Cell`. Automated world-position→trace-cell spatial lookup and physical contact decals/VFX/AI sensing are future integrations. Independent movement hosts must call `sampleMotion`.
- A newly initialized Tracker records its first position but deliberately emits no footprint until a later motion sample. Under load/rollback the owner must restore the tracker pool, not reconstruct its clock from presentation frames.
- Both systems remain entirely genre-independent and use existing `Simulation` persistence rather than a new save subsystem.

## Validation status

**Tests, builds and visual QA not run** for this stage per explicit user direction. No new GCC/Clang/ASan, CTest, Mac native app, NavMesh gameplay scene, IMPRINT decals or AIVERSE Studio screenshot evidence has been obtained. Previous Stage05 CI success is historical and cannot validate this code. The additions have been written to GitHub, not merged to `main`.

## Next exact steps

1. Expose `CrowdRoute` and `ImprintTracker` data through the existing World Processes Studio authoring and Live Inspector, preserving stable ID rules for source/target entities and normal MapDoc Undo/Redo.
2. Integrate a **real** authored sample scene: create NavMesh from TerraWeave, spawn two CROWD agents and an IMPRINT Cell; assign route and sample footsteps through an existing Scene/ActorMotor.
3. Move onward to CIRCUIT device events and IMPRINT presentation traces/AI sensing, then EVOLVE/SOCIETY adapters as requested.
4. When user later authorizes testing, verify C++ deterministic peer checks, save/rollback and numerical bounds, real NavMesh/Box3D scene, HTML5 Studio builds, all platform GPU compatibility and full merged-tree Mac `./run-tests.sh --quick`. Maintain PR #112 as DRAFT until green.
