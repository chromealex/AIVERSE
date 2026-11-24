---
name: aiverse-fracture
description: Use AIVERSE FRACTURE for Structural support bonds, deterministic collapse and durable destruction events using the existing PHYSIS/Box3D owners.
---
# FRACTURE — AIVERSE skill

## When to activate
Structural support bonds, deterministic collapse and durable destruction events using the existing PHYSIS/Box3D owners.

Typical requests: building collapse, structural damage, broken supports, cascade failure, repair. Use this skill only for this mechanic; for multi-system orchestration see `skills/world/aiverse-world-processes/SKILL.md`.

## Canonical code and API
- **Gameplay headers:** `libraries/include/aiverselib/world/FractureEcs.h`.
- **Authoritative ECS:** `fracture::Member, Bond, Collapse, Event`.
- **Lifecycle:** `aiverse::fracture::install(sim)`; per-tick behavior: `fracture::advance(world,tick)`. Install once in the authoritative `Simulation::setup` phase, before the persistence registry seals; don't install the same system twice via suite and direct calls.
- **CMake target:** no dedicated `AIVERSE::lib_*` target for this skill. This is an opt-in existing-native-module gameplay recipe (`runtime: none` in `skills/catalog.json`). The actual project links its established `AIVERSE::libraries` and any optional host owners (Box3D, Renderer3D, PHYSIS, TerraWeave) only when needed.
- **Studio-authored components:** `FractureMember, FractureBond`. These are map authoring fields, not a writable duplicate of live ECS output.
- **Adjacent adapters:** `FracturePhysisBridge.h`, `WorldProcessesLinkedAuthoring.h`, `WorldProcessesMapHydration.h`. Native project integrations are optional; include only in the target that already owns those subsystems.

## Minimal C++20 usage pattern
```cpp
#include "aiverselib/world/FractureEcs.h"
aiverse::fracture::install(sim);
// Create Member and Bond on live ECS entities.
// Read fracture::pending(world) after advance; deliver via Box3D owner,
// acknowledge only after the external effect was durably handled.
```

If using normal GameCore map projects instead of a custom module, prefer the compiled opt-in `world_processes` native module and Studio. Example fragments: `examples/native/world_processes/authoring-examples.json`; native setup/lifecycle: `examples/native/world_processes/README.md`.

## Do not cross these ownership boundaries
PHYSIS owns weather, fire and primary natural damage; Box3D owns rigid-body movement. Do not spawn a second physics engine or duplicate PHYSIS collapse outboxes.

## Acceptance checklist (execute when user lifts test deferral)
- One anchored member at minimum, bond break/damage, disconnected support cascade, event key/ack/rearm, COW rollback, generation-safe references.
- Verify C++ compilation, CTest source and actual run outcome; include GCC/Clang, ASan/UBSan where supported.
- Check authoritative tick order, World snapshot/COW, persistent schema IDs, clean reload, rollback/replay checksums, malformed input rejection, and expired Entity generation references.
- If the feature is visually observable, capture **real** native Studio/game screenshots with source SHA and platform/backend provenance; do not use fabricated renders as QA proof.
- tests/ecs_fracture_structure_test.cpp.

## Handoffs and current verification
- Read matching `docs/handoffs/world-processes-stage03-fracture-2026-11-23.md` (some stage handoff names contain extra suffixes; search `docs/handoffs/world-processes-stageNN-*` when needed).
- The PR is **DRAFT**; latest source changes have **not** been verified by a completed full native build or end-to-end scene test. Do not infer production readiness from the presence of API/docs.

## Cross-cutting contract
- Read `AGENTS.md`, `docs/agents/ecs.md`, `docs/ecs-mechanic-composition.md`, and `skills/world/aiverse-world-processes/SKILL.md` only as necessary for system boundaries.
- All live game state must be ECS-owned: `World` + `Simulation`. C++20, stable packed-generation `Entity` links in memory; map authoring stores only stable document ID strings. Never serialize ephemeral ECS slot numbers.
- Integration is opt-in. For regular GameCore maps, compile `examples/native/world_processes/WorldProcessesModule.cpp`, enable `{"id":"world_processes"}` in the project's `rules.json` `nativeModules`, and use the existing `StudioSession` map component authoring. Actual adapter: `aiverselib/world/WorldProcessesNativeModule.h` consumes `GameCore::matchMap()` + `mapPlaced()`.
- Shared standalone/linked map schemas: `aiverselib/world/WorldProcessesAuthoring.h`, `WorldProcessesLinkedAuthoring.h`, `WorldProcessesExtendedAuthoring.h`, `WorldProcessesExtendedLinks.h`. `WorldProcessesMapHydration.h` resolves live ECS owners with strict validation before installing/creating components.
- In Studio use existing Add Component, reference picker, Scene gizmos and native `MapDoc` Undo/Redo. For live observation use `WorldProcessesInspector.h`/`WorldProcessesExtendedInspector.h`, never writable runtime clones.
- After each implementation step add a new `docs/handoffs/world-processes-stageNN-*.md` describing code paths, API, ownership, branch/commit, actual tests/results, missing features and exact next action. Update agent guidance; do not merge unverified code into main.
- Current source is in draft PR #112 on `feature/world-processes-experimental`. New Stage11+ features are **source-only, not confirmed compiling or passing runtime tests**. The user deferred testing; do not describe old Stage05 successful tests as proof of these newer files.

## Stage 88: component-local support
`engine/core/GraphSupportCache.h` provides a derived undirected topology cache via shared weakComponents. Installed FRACTURE compares exact generation-safe member IDs and canonical edge sets; rebuild only old components touched by changed edges, root changes recompute support without graph rebuild. Cache is disposable, not persistent authority, exact after rollback. Stateless advance overload preserved. Support graph is surface-neutral; physical collapse uses PHYSIS/Box3D gravity bridge and planet-local transforms, not extra gravity loads. See stage88 handoff and planetary contract; tests not run.

## Stage 111: planetary live FRACTURE structural diagnostics

Enable Live 3D overlay and the independent FRACTURE structural state checkbox during local Play current map. The existing Scene renderer shows actual ECS Member positions and same-body Bond connections in body-local coordinates. Cyan active anchors, green intact members, orange broken members, red detached members, yellow pending-event crosses. Bond colors represent stored damage/strength; severed, disabled or inactive support edges are interrupted. Active edge eligibility reuses fracture::validBond and existing member broken/detached flags, not a second support traversal. Intact flags alone are not proof of independently verified connectivity.

The read-only snapshot reports generation-safe member IDs, actual latched structural state, strength/damage, pending/acknowledged Collapse delivery and event key/tick. Optional PHYSIS NatureStressOutput and NatureFractureOutbox on those same members are reported independently, with their own climate tick, normalized load, accumulated damage, wind force and bending moment. PHYSIS stress is not synthesized for missing components. A pending event remains pending; the diagnostic never acknowledges, spawns debris, applies forces or repairs a member. No duplicate graph, ECS state, persistence or physics solver.

Bounds: owner member/bond budgets, maximum 256 placed members and 512 sampled bonds, stable packed-ID ordering. Global member status counts include unplaced members before the output cap; cross-body and omitted bond counts describe the visited candidates, not an exhaustive audit after truncation. Cross-body visual edges are omitted without changing authoritative graph topology. Native local Play/map/revision guards retained. HTML5 shows sampled counts and first eight members and bonds, optional stress and delivery. Planar ecology unchanged.

Source fixture exercises existing support cascade, damage/severing, pending outbox, climate-clock distinction, body isolation and caps. Native build, CTest, browser typecheck, interactive UI, GPU screenshots and performance NOT RUN. Next: MORPH constraint/node displacement diagnostics. Physical debris delivery, full force/structural stress solver, actual terrain mutation and shared Studio integration remain separate owner work.

## Stage 123: planetary FRACTURE debris projection

`commitPlanetCollapse` stages the existing owner's durable support-loss consequence in a copied World, revalidates event identity and PHYSIS primary overlap, and acknowledges only on commit. Optional native `PlanetDebrisPublisher` projects an acknowledged owner-supplied shape/pose into the existing Box3D scene with body-local coordinates and idempotent event handling. The existing PHYSIS Box3D bridge accepts a fixed translation back to canonical sample coordinates and compensates native gravity once. Host must persist consequences, remove original colliders, reconcile rollback and own render/physics lifecycle. No automatic GameCore integration or actual runtime acceptance yet. Tests NOT RUN. See `docs/handoffs/world-processes-stage123-planet-debris-2026-11-24.md`.
