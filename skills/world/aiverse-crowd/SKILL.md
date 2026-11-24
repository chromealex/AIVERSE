---
name: aiverse-crowd
description: Use AIVERSE CROWD for Deterministic crowd placement, slots, local avoidance and existing NavMesh route following.
---
# CROWD — AIVERSE skill

## When to activate
Deterministic crowd placement, slots, local avoidance and existing NavMesh route following.

Typical requests: NPC crowds, queues, formations, path routing, destination allocation. Use this skill only for this mechanic; for multi-system orchestration see `skills/world/aiverse-world-processes/SKILL.md`.

## Canonical code and API
- **Gameplay headers:** `libraries/include/aiverselib/world/CrowdEcs.h`, `libraries/include/aiverselib/world/CrowdRoutes.h`.
- **Authoritative ECS:** `crowd::Agent, Slot, Preference, Route`.
- **Lifecycle:** `aiverse::lib::worldprocess::installSuite(sim, flags) with flags.crowd=true`; per-tick behavior: `crowd::advanceRoutes(world,tick) BEFORE crowd::advance(world,tick)`. Install once in the authoritative `Simulation::setup` phase, before the persistence registry seals; don't install the same system twice via suite and direct calls.
- **CMake target:** no dedicated `AIVERSE::lib_*` target for this skill. This is an opt-in existing-native-module gameplay recipe (`runtime: none` in `skills/catalog.json`). The actual project links its established `AIVERSE::libraries` and any optional host owners (Box3D, Renderer3D, PHYSIS, TerraWeave) only when needed.
- **Studio-authored components:** `CrowdAgent, CrowdSlot, CrowdPreference (Route is runtime-generated)`. These are map authoring fields, not a writable duplicate of live ECS output.
- **Adjacent adapters:** `examples/world/world_processes_native/CrowdNavMeshBridge.h`, `ImprintCrowdBridge.h`, `WorldProcessesQueries.h`. Native project integrations are optional; include only in the target that already owns those subsystems.

## Minimal C++20 usage pattern
```cpp
#include "aiverselib/world/CrowdEcs.h"
aiverse::lib::worldprocess::SuiteFlags flags;
flags.crowd=true;
aiverse::lib::worldprocess::installSuite(sim,flags);
// Native host uses CrowdNavMeshBridge.h to query existing nav::Mesh;
// crowd::assignRoute(world,agent,quantizedWaypoints,navRevision);
```

If using normal GameCore map projects instead of a custom module, prefer the compiled opt-in `world_processes` native module and Studio. Example fragments: `examples/native/world_processes/authoring-examples.json`; native setup/lifecycle: `examples/native/world_processes/README.md`.

## Do not cross these ownership boundaries
engine/nav/NavMesh is sole pathfinding owner. This stage has deterministic local separation, NOT full ORCA/RVO or automatic transform/animation. Do not clone NavMesh.

## Acceptance checklist (execute when user lifts test deferral)
- Deterministic slot allocation, route cursor/checksum, stale targets, snap/rollback, nav revision replan, no forced movement into blocked cells.
- Verify C++ compilation, CTest source and actual run outcome; include GCC/Clang, ASan/UBSan where supported.
- Check authoritative tick order, World snapshot/COW, persistent schema IDs, clean reload, rollback/replay checksums, malformed input rejection, and expired Entity generation references.
- If the feature is visually observable, capture **real** native Studio/game screenshots with source SHA and platform/backend provenance; do not use fabricated renders as QA proof.
- Stage11/20 current CROWD builds/replay tests not yet run.

## Handoffs and current verification
- Read matching `docs/handoffs/world-processes-stage11-morph-crowd-2026-11-23.md`, `docs/handoffs/world-processes-stage20-crowd-navmesh-imprint-movement-2026-11-23.md` (some stage handoff names contain extra suffixes; search `docs/handoffs/world-processes-stage*` when needed).
- The PR is **DRAFT**; latest source changes have **not** been verified by a completed full native build or end-to-end scene test. Do not infer production readiness from the presence of API/docs.

## Cross-cutting contract
- Read `AGENTS.md`, `docs/agents/ecs.md`, `docs/ecs-mechanic-composition.md`, and `skills/world/aiverse-world-processes/SKILL.md` only as necessary for system boundaries.
- All live game state must be ECS-owned: `World` + `Simulation`. C++20, stable packed-generation `Entity` links in memory; map authoring stores only stable document ID strings. Never serialize ephemeral ECS slot numbers.
- Integration is opt-in. For regular GameCore maps, compile `examples/native/world_processes/WorldProcessesModule.cpp`, enable `{"id":"world_processes"}` in the project's `rules.json` `nativeModules`, and use the existing `StudioSession` map component authoring. Actual adapter: `aiverselib/world/WorldProcessesNativeModule.h` consumes `GameCore::matchMap()` + `mapPlaced()`.
- Shared standalone/linked map schemas: `aiverselib/world/WorldProcessesAuthoring.h`, `WorldProcessesLinkedAuthoring.h`, `WorldProcessesExtendedAuthoring.h`, `WorldProcessesExtendedLinks.h`. `WorldProcessesMapHydration.h` resolves live ECS owners with strict validation before installing/creating components.
- In Studio use existing Add Component, reference picker, Scene gizmos and native `MapDoc` Undo/Redo. For live observation use `WorldProcessesInspector.h`/`WorldProcessesExtendedInspector.h`, never writable runtime clones.
- After each implementation step add a new `docs/handoffs/world-processes-stage*.md` describing code paths, API, ownership, branch/commit, actual tests/results, missing features and exact next action. Update agent guidance; do not merge unverified code into main.
- Current source is in draft PR #112 on `feature/world-processes-experimental`. New Stage11+ features are **source-only, not confirmed compiling or passing runtime tests**. The user deferred testing; do not describe old Stage05 successful tests as proof of these newer files.

## Stage 82: optional budgeted reports
Include `aiverselib/world/WorldProcessBudgetedQueries.h`; `lib::worldprocess::prepareCrowdReports` uses canonical Agent x/z centimetres, planar authoritative observer cells and shared EntityBudgetState/Options. Return batch includes samples, request/sample ticks, LOD diagnostics and staged next state. Publish only on success. Separate state per workload; no automatic registration or persistence. Never throttle `crowd::advance`, slot assignment, local avoidance or route movement based on this advisory report. See stage82 handoff; tests not run.

## Stage 90: planetary CROWD through existing movement owner
`CrowdSurfaceSlots.h` plans stable exclusive slot reservations using canonical Transform/Surface and same-body filtering, owner eligibility/passability callbacks and existing assignSlots. `CrowdGameCoreSurface.h` selects mobile grounded/non-air GameCore agents, sends normal Move orders only to idle non-combat actors with empty queues, and staggers retries for unchanged allocation at 16 ticks. Existing GameCore owns routes, speed, radius, obstacle checks, seam traversal, slope, tangent separation and position. Planet maps select this installer instead of legacy XZ CROWD/Route; flat host behavior unchanged. On planets author actual at/body/face placements and CrowdPreference references; legacy Agent/Slot x/z/goal/speed/radius and CrowdRoutes waypoint APIs are not planetary movement inputs. Never treat their x/z as live coordinates on planets. Use actual Transform and native Move command/route owner. PHYSIS gravity-aware dynamic locomotion is not yet wired; radial geometric projection does not imply actual gravity. Do not duplicate integration. See Stage90 handoff; test source added, not run.

## Stage 100: PHYSIS-aware CROWD support eligibility

`SurfaceGravitySupport.h` exposes a shared Fixed-point, read-only classifier: actual PHYSIS acceleration, terrain normal, signed radial ground clearance, inward and tangential acceleration, and supported/airborne/weak/outward/steep status. Planetary sampling uses the existing Surface face normal and coordinates. The native World Processes option `"crowdGravitySupport": true` enables CROWD and requires a planetary Surface installer. It re-evaluates actor and destination support on each reservation tick, in addition to existing body isolation and grid passability, and withholds new CROWD Move orders or releases unsupported reservations. Defaults: 0.125 m ground tolerance, 0.05 m/s² minimum gravity and 0.5 minimum inward cosine. Existing maps remain unchanged by default. The authored planet demo enables the option.

No Transform, velocity, movement layer, existing route or player/AI order is changed by this classifier. Existing GameCore motion still projects actors onto the surface; falling, free flight, adhesion, slope slip, along-path support checks and dynamic collision integration are not implemented. The pure classifier accepts a host-provided normal for planar consumers, but the new GameCore native flag is planetary-only; planar native behavior is unchanged. Tests authored for all six faces, zero/reverse gravity, inclined acceleration, ground clearance, rollback and live reservation release/reacquisition in the demo; build/tests NOT RUN.

## Stage 114: planetary live CROWD reservations and native routes

Enable Live 3D overlay and the independent CROWD reservations / routes checkbox during local Play current map. Snapshot reads completed-tick CROWD Agent allocation and actual canonical Transform/Slot locations, enabled state, ordered preference candidates (up to 16 per sampled agent), and occupancy of valid same-body reserved slots across all current agents. Invalid, stale, disabled or cross-body allocation is not drawn as a valid reservation. Numeric rows for unplaced entities are retained.

The movement owner remains GameCore. The diagnostic reads existing Mobile moving/flow flags, actual velocity, path cursor/count, current goal, remaining path points and Orders queue size. Legacy Agent x/z/goal and planar CROWD Route are never interpreted as planetary positions. Native path points are body-local samples joined with lifted diagnostic segments, not newly computed routes; invalid or incompatible points break the polyline. A flow-field agent need not have explicit path points. Slot relation lines represent allocations, not paths or evidence that a CROWD-issued move is in progress. Current owner eligibility is read through the existing surfaceCrowdEligible policy when the host is available.

When loaded stock world_processes config enables crowdGravitySupport, the snapshot calls existing samplePlanetGravitySupport for placed agents and slots. It returns current supported/airborne/weak/outward/steep status, clearance, terrain normal and acceleration. It is not a historical rejection reason, does not prove navigation passability, and does not cancel routes, change gravity or issue Move commands. Above 1024 gravity fields support sampling is unavailable while other readback remains.

Native Scene uses green native path lines, amber dashed reservations, cyan free slots, amber occupied slots, red duplicate slots or unsupported agents, muted disabled markers and purple goals. HTML5 panel reports first eight sampled agents/slots, counts, cursor and support. Stable packed-ID ordering, owner component budgets, max 256 agent rows, 512 slot rows, 64 path points per agent and 2048 total path points. Counts except occupancy refer to visited candidates. Existing local Play and map/revision guards remain; flat ecology unchanged.

Source fixture checks seam-crossing native waypoint, invalid cross-body reservation and route break, zero-gravity support, occupancy, stale Agent tick, output caps and read-only behavior. Build, CTest, browser typecheck, interaction, real captures and performance NOT RUN. Next CIRCUIT power/signal/device diagnostics, then SOCIETY. No main merge.
