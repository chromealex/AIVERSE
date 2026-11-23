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
- Read matching `docs/handoffs/world-processes-stage11-morph-crowd-2026-11-23.md`, `docs/handoffs/world-processes-stage20-crowd-navmesh-imprint-movement-2026-11-23.md` (some stage handoff names contain extra suffixes; search `docs/handoffs/world-processes-stageNN-*` when needed).
- The PR is **DRAFT**; latest source changes have **not** been verified by a completed full native build or end-to-end scene test. Do not infer production readiness from the presence of API/docs.

## Cross-cutting contract
- Read `AGENTS.md`, `docs/agents/ecs.md`, `docs/ecs-mechanic-composition.md`, and `skills/world/aiverse-world-processes/SKILL.md` only as necessary for system boundaries.
- All live game state must be ECS-owned: `World` + `Simulation`. C++20, stable packed-generation `Entity` links in memory; map authoring stores only stable document ID strings. Never serialize ephemeral ECS slot numbers.
- Integration is opt-in. For regular GameCore maps, compile `examples/native/world_processes/WorldProcessesModule.cpp`, enable `{"id":"world_processes"}` in the project's `rules.json` `nativeModules`, and use the existing `StudioSession` map component authoring. Actual adapter: `aiverselib/world/WorldProcessesNativeModule.h` consumes `GameCore::matchMap()` + `mapPlaced()`.
- Shared standalone/linked map schemas: `aiverselib/world/WorldProcessesAuthoring.h`, `WorldProcessesLinkedAuthoring.h`, `WorldProcessesExtendedAuthoring.h`, `WorldProcessesExtendedLinks.h`. `WorldProcessesMapHydration.h` resolves live ECS owners with strict validation before installing/creating components.
- In Studio use existing Add Component, reference picker, Scene gizmos and native `MapDoc` Undo/Redo. For live observation use `WorldProcessesInspector.h`/`WorldProcessesExtendedInspector.h`, never writable runtime clones.
- After each implementation step add a new `docs/handoffs/world-processes-stageNN-*.md` describing code paths, API, ownership, branch/commit, actual tests/results, missing features and exact next action. Update agent guidance; do not merge unverified code into main.
- Current source is in draft PR #112 on `feature/world-processes-experimental`. New Stage11+ features are **source-only, not confirmed compiling or passing runtime tests**. The user deferred testing; do not describe old Stage05 successful tests as proof of these newer files.
