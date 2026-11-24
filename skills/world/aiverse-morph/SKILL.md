---
name: aiverse-morph
description: Use AIVERSE MORPH for Quantized deformable nodes, springs and physics impacts presented through the existing Renderer3D and Box3D adapters.
---
# MORPH — AIVERSE skill

## When to activate
Quantized deformable nodes, springs and physics impacts presented through the existing Renderer3D and Box3D adapters.

Typical requests: soft-body deformation, springs, model bending, contact-driven shape changes. Use this skill only for this mechanic; for multi-system orchestration see `skills/world/aiverse-world-processes/SKILL.md`.

## Canonical code and API
- **Gameplay headers:** `libraries/include/aiverselib/world/MorphEcs.h`.
- **Authoritative ECS:** `morph::Node, Spring, Force, Impact`.
- **Lifecycle:** `aiverse::morph::install(sim)`; per-tick behavior: `morph::advance(world,tick)`. Install once in the authoritative `Simulation::setup` phase, before the persistence registry seals; don't install the same system twice via suite and direct calls.
- **CMake target:** no dedicated `AIVERSE::lib_*` target for this skill. This is an opt-in existing-native-module gameplay recipe (`runtime: none` in `skills/catalog.json`). The actual project links its established `AIVERSE::libraries` and any optional host owners (Box3D, Renderer3D, PHYSIS, TerraWeave) only when needed.
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

## Stage 88P: optional PHYSIS gravity projection
`WorldProcessGravity.h::morphGravityForce` converts local PHYSIS acceleration to existing quantized Force using explicit host-owned impulse-per-tick scale. Supply actual node/body frame, not global XZ; do not double-apply gravity from rigid body motion. It returns a Force value for an authoritative producer, not an automatically installed system. Planetary rendering/contact and producer integration pending; see planetary contract. Tests not run.

## Stage 93: MORPH gravity integration
`MorphGravityBinding.h` persists opt-in frame owner, unitsPerMetre, impulseMilli (1000 = one solver impulse unit per m/s^2), relative mode and enable flag. `MorphPhysisGravity.h` samples canonical PHYSIS at node position, using actual WorldTransform rotation or Transform yaw in the canonical surface frame. Relative mode subtracts owner acceleration (free-fall approximation); absolute mode for supported local structures. No duplicate rigid-body gravity, no mass/velocity physics solver, no automatic time-step calibration. `advanceWithLoad` adds load after continuous authored forces and before one-shot impacts inside original staged solver. Missing/stale owner rejects update before writes. Map MorphNode accepts optional gravityImpulseMilli and gravityRelative (default true), self frame owner, 100 node units/metre; native `morphGravity:true` reserves schema for dynamic bindings. Native setup installs one solver for both plane/planet only when requested; unbound nodes and legacy maps retain old behavior and schema. PHYSIS fields must be authored/persisted by their owner; no implicit radial gravity. Render/physics actual-frame and Studio UI acceptance remain pending. Tests authored, not run.

## Stage 112: planetary live MORPH displacement diagnostics

Enable Live 3D overlay and the independent MORPH node displacement checkbox during local Play current map. Actual completed-tick Node values are read without advancing the solver. Quantized current/rest/displacement/velocity, configured enabled Force sum (not a saturated applied impulse), pending Impact, binding metadata and Spring stiffness/residual are returned. No second deformation state.

Space projection requires an explicit existing GravityBinding with a valid live owner and unitsPerMetre; the same MORPH ownerFrame and simulationVector used by PHYSIS loads project current and rest positions, validate body identity, then subtract the canonical body anchor. A disabled gravity binding can still provide its explicit frame/scale. Unbound or unresolved nodes remain numeric-only; do not guess a metre scale or attach their local coordinates to their own Transform. Spring geometry is only drawn for projected endpoints with the same owner, scale and body; raw solver residual remains visible for other sampled edges with a mismatch count. Amber displacement segments, grey rest markers, cyan anchored/green enabled/grey disabled nodes and purple stiffness-colored springs are diagnostic lines, not model vertex deformation.

For the loaded stock world_processes morphGravity mode, the existing physisNodeLoad resolver reports the current potential quantized gravity load on enabled nonanchored projected nodes, including relative/free-fall configuration. This is not a recorded prior-tick impulse. Unknown/disabled mode or gravity component cap suppresses load sampling; no gravity application or physics integration here. Same local Play/project/revision and selected-body guards as prior stages.

Stable packed-ID sampling: owner component budgets, 256 nodes, 512 springs, 1024 gravity fields for optional load. Counts of mismatch/rejection/omission refer to visited sample candidates. HTML5 reports the first eight nodes and springs and counts. Planar ecology unchanged. Existing MorphModelStream and Box3D remain separate owners; no actual visual mesh attachment, collider deformation or new persistence.

Source fixture covers shared frame/scale, body isolation, unbound nodes, exact residual, canonical gravity query, pending impact preservation, stale tick and cap. Build, CTest, TypeScript typecheck, interactive UI, GPU screenshots and performance NOT RUN. Next: ECHO live acoustic zones/portals/receiver audibility. Physical/render integration and shared Studio merge remain pending.
