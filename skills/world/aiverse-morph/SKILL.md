---
name: aiverse-morph
description: Use AIVERSE MORPH when implementing ECS-деформации узлов, пружин и одноразовых физических импульсов с реальным графическим выводом.
---
# MORPH — AIVERSE skill

## When to activate
ECS-деформации узлов, пружин и одноразовых физических импульсов с реальным графическим выводом.

Typical request: мягкие деформации, сгибание, упругая геометрия, разрушение формы, контакт с Box3D. Use this skill only for this system; for cross-system configuration consult `skills/world/aiverse-world-processes/SKILL.md`.

## Canonical code and API
- **Gameplay headers:** `libraries/include/aiverselib/world/MorphEcs.h`.
- **Authoritative ECS:** `morph::Node, Spring, Force, Impact`.
- **Lifecycle:** `aiverse::morph::install(sim)`; per-tick behavior: `morph::advance(world,tick)`. Install once in the authoritative `Simulation::setup` phase, before the persistence registry seals; don't install the same system twice via suite and direct calls.
- **Studio-authored components:** `MorphNode, MorphSpring, MorphForce`. These are map authoring fields, not a writable duplicate of live ECS output.
- **Adjacent adapters:** `examples/world/world_processes_native/MorphModelStream.h`, `MorphBox3DContactBridge.h`, `PhysicalSceneHost.h`, `WorldProcessesQueries.h`. Native project integrations are optional; include only in the target that already owns those subsystems.

## Minimal C++20 usage pattern
```cpp
#include "aiverselib/world/MorphEcs.h"
aiverse::morph::install(sim);
// Nodes/Springs/Forces belong to World. At the fixed ECS barrier:
// morph::enqueueImpact(world,node,scheduledTick,impulseX,impulseY,impulseZ);
// For visuals use the optional native MorphModelStream on a ModelDraw.
```

If using normal GameCore map projects instead of a custom module, prefer the compiled opt-in `world_processes` native module and Studio. Example fragments: `examples/native/world_processes/authoring-examples.json`; native setup/lifecycle: `examples/native/world_processes/README.md`.

## Do not cross these ownership boundaries
Box3D remains sole rigid-body collision authority. MorphModelStream is per visual instance and requires a dedicated vertex stream; deformable GPU vertices are not automatically deformable collision geometry.

## Acceptance checklist (execute when user lifts test deferral)
- Mass/range bounds, stale node handles, idempotent impact scheduling, no double consumption, rollback recovery, GPU resource fences and vertex weights.
- Verify C++ compilation, CTest source and actual run outcome; include GCC/Clang, ASan/UBSan where supported.
- Check authoritative tick order, World snapshot/COW, persistent schema IDs, clean reload, rollback/replay checksums, malformed input rejection, and expired Entity generation references.
- If the feature is visually observable, capture **real** native Studio/game screenshots with source SHA and platform/backend provenance; do not use fabricated renders as QA proof.
- Stage11–19 new MORPH compilation/Scene/COW tests not yet executed.

## Handoffs and current verification
- Read matching `docs/handoffs/world-processes-stage11-morph-crowd-2026-11-23.md`, `docs/handoffs/world-processes-stage17-morph-render-box3d-2026-11-23.md`, `docs/handoffs/world-processes-stage19-physical-native-host-2026-11-23.md` (some stage handoff names contain extra suffixes; search `docs/handoffs/world-processes-stageNN-*` when needed).
- The PR is **DRAFT**; latest source changes have **not** been verified by a completed full native build or end-to-end scene test. Do not infer production readiness from the presence of API/docs.

## Cross-cutting contract
- Read `AGENTS.md`, `docs/agents/ecs.md`, `docs/ecs-mechanic-composition.md`, and `skills/world/aiverse-world-processes/SKILL.md` only as necessary for system boundaries.
- All live game state must be ECS-owned: `World` + `Simulation`. C++20, stable packed-generation `Entity` links in memory; map authoring stores only stable document ID strings. Never serialize ephemeral ECS slot numbers.
- Integration is opt-in. For regular GameCore maps, compile `examples/native/world_processes/WorldProcessesModule.cpp`, enable `{"id":"world_processes"}` in the project's `rules.json` `nativeModules`, and use the existing `StudioSession` map component authoring. Actual adapter: `aiverselib/world/WorldProcessesNativeModule.h` consumes `GameCore::matchMap()` + `mapPlaced()`.
- Shared standalone/linked map schemas: `aiverselib/world/WorldProcessesAuthoring.h`, `WorldProcessesLinkedAuthoring.h`, `WorldProcessesExtendedAuthoring.h`, `WorldProcessesExtendedLinks.h`. `WorldProcessesMapHydration.h` resolves live ECS owners with strict validation before installing/creating components.
- In Studio use existing Add Component, reference picker, Scene gizmos and native `MapDoc` Undo/Redo. For live observation use `WorldProcessesInspector.h`/`WorldProcessesExtendedInspector.h`, never writable runtime clones.
- After each implementation step add a new `docs/handoffs/world-processes-stageNN-*.md` describing code paths, API, ownership, branch/commit, actual tests/results, missing features and exact next action. Update agent guidance; do not merge unverified code into main.
- Current source is in draft PR #112 on `feature/world-processes-experimental`. New Stage11+ features are **source-only, not confirmed compiling or passing runtime tests**. The user deferred testing; do not describe old Stage05 successful tests as proof of these newer files.
