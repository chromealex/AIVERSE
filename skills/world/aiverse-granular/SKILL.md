---
name: aiverse-granular
description: Use AIVERSE GRANULAR for Bounded cellular loose solids, Studio grid painting and TerraWeave terrain mass transfer.
---
# GRANULAR — AIVERSE skill

## When to activate
Bounded cellular loose solids, Studio grid painting and TerraWeave terrain mass transfer.

Typical requests: sand, soil settling, granular cells, dig and fill, loose debris. Use this skill only for this mechanic; for multi-system orchestration see `skills/world/aiverse-world-processes/SKILL.md`.

## Canonical code and API
- **Gameplay headers:** `libraries/include/aiverselib/world/GranularEcs.h`, `libraries/include/aiverselib/world/GranularDocument.h`.
- **Authoritative ECS:** `granular::Field, Source`.
- **Lifecycle:** `aiverse::granular::install(sim)`; per-tick behavior: `granular::advance(world,tick)`. Install once in the authoritative `Simulation::setup` phase, before the persistence registry seals; don't install the same system twice via suite and direct calls.
- **CMake target:** no dedicated `AIVERSE::lib_*` target for this skill. This is an opt-in existing-native-module gameplay recipe (`runtime: none` in `skills/catalog.json`). The actual project links its established `AIVERSE::libraries` and any optional host owners (Box3D, Renderer3D, PHYSIS, TerraWeave) only when needed.
- **Studio-authored components:** `GranularField, GranularSource`. These are map authoring fields, not a writable duplicate of live ECS output.
- **Adjacent adapters:** `examples/world/world_processes_native/GranularTerraWeaveBinding.h`, `GranularWorldIntegration.h`, `GranularTerraWeaveTransfer.h`, `PhysicalSceneHost.h`. Native project integrations are optional; include only in the target that already owns those subsystems.

## Minimal C++20 usage pattern
```cpp
#include "aiverselib/world/GranularEcs.h"
aiverse::granular::install(sim);
// For 2D authored grids:
auto doc=granular::authoring::create(8,8,1000);
granular::authoring::paint(doc,2,3,200,false);
// Game host later calls granular::authoring::apply(world,entity,doc).
```

If using normal GameCore map projects instead of a custom module, prefer the compiled opt-in `world_processes` native module and Studio. Example fragments: `examples/native/world_processes/authoring-examples.json`; native setup/lifecycle: `examples/native/world_processes/README.md`.

## Do not cross these ownership boundaries
TerraWeave is sole canonical soil authority; NEREID water and Box3D rigid collisions are not replaced. Editor brush operates on authored MapDoc; render/collider are derived tiles. Not full 3D SPH/DEM.

## Acceptance checklist (execute when user lifts test deferral)
- Grid ≤128×128, conservation except Source and explicit Terra transfers, blocked cells empty, world revision/cache invalidation, GPU fence and rollback.
- Verify C++ compilation, CTest source and actual run outcome; include GCC/Clang, ASan/UBSan where supported.
- Check authoritative tick order, World snapshot/COW, persistent schema IDs, clean reload, rollback/replay checksums, malformed input rejection, and expired Entity generation references.
- If the feature is visually observable, capture **real** native Studio/game screenshots with source SHA and platform/backend provenance; do not use fabricated renders as QA proof.
- Stage13/18/19 GPU, Box3D, TerraWeave and CTest not yet run.

## Handoffs and current verification
- Read matching `docs/handoffs/world-processes-stage13-granular-evolve-society-2026-11-23.md`, `docs/handoffs/world-processes-stage18-granular-terra-box3d-2026-11-23.md`, `docs/handoffs/world-processes-stage19-physical-native-host-2026-11-23.md` (some stage handoff names contain extra suffixes; search `docs/handoffs/world-processes-stageNN-*` when needed).
- The PR is **DRAFT**; latest source changes have **not** been verified by a completed full native build or end-to-end scene test. Do not infer production readiness from the presence of API/docs.

## Cross-cutting contract
- Read `AGENTS.md`, `docs/agents/ecs.md`, `docs/ecs-mechanic-composition.md`, and `skills/world/aiverse-world-processes/SKILL.md` only as necessary for system boundaries.
- All live game state must be ECS-owned: `World` + `Simulation`. C++20, stable packed-generation `Entity` links in memory; map authoring stores only stable document ID strings. Never serialize ephemeral ECS slot numbers.
- Integration is opt-in. For regular GameCore maps, compile `examples/native/world_processes/WorldProcessesModule.cpp`, enable `{"id":"world_processes"}` in the project's `rules.json` `nativeModules`, and use the existing `StudioSession` map component authoring. Actual adapter: `aiverselib/world/WorldProcessesNativeModule.h` consumes `GameCore::matchMap()` + `mapPlaced()`.
- Shared standalone/linked map schemas: `aiverselib/world/WorldProcessesAuthoring.h`, `WorldProcessesLinkedAuthoring.h`, `WorldProcessesExtendedAuthoring.h`, `WorldProcessesExtendedLinks.h`. `WorldProcessesMapHydration.h` resolves live ECS owners with strict validation before installing/creating components.
- In Studio use existing Add Component, reference picker, Scene gizmos and native `MapDoc` Undo/Redo. For live observation use `WorldProcessesInspector.h`/`WorldProcessesExtendedInspector.h`, never writable runtime clones.
- After each implementation step add a new `docs/handoffs/world-processes-stageNN-*.md` describing code paths, API, ownership, branch/commit, actual tests/results, missing features and exact next action. Update agent guidance; do not merge unverified code into main.
- Current source is in draft PR #112 on `feature/world-processes-experimental`. New Stage11+ features are **source-only, not confirmed compiling or passing runtime tests**. The user deferred testing; do not describe old Stage05 successful tests as proof of these newer files.

## Stage 87: derived active rows and cell masks
`granular::settle(Field&,tick)` retains its existing contract. Optional three-argument overload accepts `SettlementActivity*` (sourceRows, activeRows, skippedRows, candidateCells, transfers, movedMass). Build two 64-bit occupancy words per row while validating current field; derive free destinations after the lower row has settled, dilate by one column to include diagonals across word boundaries, and visit candidate bits in original ascending x order. Empty or blocked/full rows skip transfer work. No persistent sleep state or new ECS fields; direct mass/obstacle edits, sources, enabled changes and rollback automatically affect the next call. Keep bottom-to-top traversal, one-row movement, parity direction, capacity and mass exactly. Disabled and quiet fields still stamp sampledTick. Activity is published only on success. Existing dense reference in linked test fixture compares 128 cases across 8 ticks; no tests executed. See Stage87 handoff.
