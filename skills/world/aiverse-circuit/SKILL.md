---
name: aiverse-circuit
description: Use AIVERSE CIRCUIT for Deterministic power routing, Boolean gates, switches and logical device outputs.
---
# CIRCUIT — AIVERSE skill

## When to activate
Deterministic power routing, Boolean gates, switches and logical device outputs.

Typical requests: electricity, switches, light controls, powered doors, motors, alarms. Use this skill only for this mechanic; for multi-system orchestration see `skills/world/aiverse-world-processes/SKILL.md`.

## Canonical code and API
- **Gameplay headers:** `libraries/include/aiverselib/world/CircuitEcs.h`, `libraries/include/aiverselib/world/CircuitDevices.h`.
- **Authoritative ECS:** `circuit::Node, Wire, Gate, Switch, Device`.
- **Lifecycle:** `aiverse::lib::worldprocess::installSuite(sim,flags) with flags.circuit=true`; per-tick behavior: `circuit.switch_inputs -> circuit.power_logic -> circuit.device_outputs`. Install once in the authoritative `Simulation::setup` phase, before the persistence registry seals; don't install the same system twice via suite and direct calls.
- **CMake target:** no dedicated `AIVERSE::lib_*` target for this skill. This is an opt-in existing-native-module gameplay recipe (`runtime: none` in `skills/catalog.json`). The actual project links its established `AIVERSE::libraries` and any optional host owners (Box3D, Renderer3D, PHYSIS, TerraWeave) only when needed.
- **Studio-authored components:** `CircuitNode, CircuitWire, CircuitGate, CircuitSwitch, CircuitDevice`. These are map authoring fields, not a writable duplicate of live ECS output.
- **Adjacent adapters:** `examples/world/world_processes_native/CircuitDeviceHost.h`, `WorldProcessGameplayHost.h`, `WorldProcessesExtendedLinks.h`. Native project integrations are optional; include only in the target that already owns those subsystems.

## Minimal C++20 usage pattern
```cpp
#include "aiverselib/world/CircuitEcs.h"
aiverse::lib::worldprocess::SuiteFlags flags;
flags.circuit=true;
aiverse::lib::worldprocess::installSuite(sim,flags);
// Authoritative switch command:
circuit::setSwitch(world,switchEntity,true);
// After the tick the native CircuitDeviceHost reconciles device outputs.
```

If using normal GameCore map projects instead of a custom module, prefer the compiled opt-in `world_processes` native module and Studio. Example fragments: `examples/native/world_processes/authoring-examples.json`; native setup/lifecycle: `examples/native/world_processes/README.md`.

## Do not cross these ownership boundaries
Game economy is authoritative for supply. Box3D controls kinematic door collision; renderer owns lights, game owns pumps/alarms. Native callbacks must be registered by the actual project.

## Acceptance checklist (execute when user lifts test deferral)
- Power budget, directed wire topology, feedback delay, duplicate gate/switch owners, stale inputs, activation transitions, reapply state on rollback.
- Verify C++ compilation, CTest source and actual run outcome; include GCC/Clang, ASan/UBSan where supported.
- Check authoritative tick order, World snapshot/COW, persistent schema IDs, clean reload, rollback/replay checksums, malformed input rejection, and expired Entity generation references.
- If the feature is visually observable, capture **real** native Studio/game screenshots with source SHA and platform/backend provenance; do not use fabricated renders as QA proof.
- Stage12/22/25 unverified compiler/Box3D/Studio acceptance.

## Handoffs and current verification
- Read matching `docs/handoffs/world-processes-stage12-imprint-circuit-2026-11-23.md`, `docs/handoffs/world-processes-stage22-circuit-devices-2026-11-23.md`, `docs/handoffs/world-processes-stage25-gameplay-host-2026-11-23.md` (some stage handoff names contain extra suffixes; search `docs/handoffs/world-processes-stageNN-*` when needed).
- The PR is **DRAFT**; latest source changes have **not** been verified by a completed full native build or end-to-end scene test. Do not infer production readiness from the presence of API/docs.

## Cross-cutting contract
- Read `AGENTS.md`, `docs/agents/ecs.md`, `docs/ecs-mechanic-composition.md`, and `skills/world/aiverse-world-processes/SKILL.md` only as necessary for system boundaries.
- All live game state must be ECS-owned: `World` + `Simulation`. C++20, stable packed-generation `Entity` links in memory; map authoring stores only stable document ID strings. Never serialize ephemeral ECS slot numbers.
- Integration is opt-in. For regular GameCore maps, compile `examples/native/world_processes/WorldProcessesModule.cpp`, enable `{"id":"world_processes"}` in the project's `rules.json` `nativeModules`, and use the existing `StudioSession` map component authoring. Actual adapter: `aiverselib/world/WorldProcessesNativeModule.h` consumes `GameCore::matchMap()` + `mapPlaced()`.
- Shared standalone/linked map schemas: `aiverselib/world/WorldProcessesAuthoring.h`, `WorldProcessesLinkedAuthoring.h`, `WorldProcessesExtendedAuthoring.h`, `WorldProcessesExtendedLinks.h`. `WorldProcessesMapHydration.h` resolves live ECS owners with strict validation before installing/creating components.
- In Studio use existing Add Component, reference picker, Scene gizmos and native `MapDoc` Undo/Redo. For live observation use `WorldProcessesInspector.h`/`WorldProcessesExtendedInspector.h`, never writable runtime clones.
- After each implementation step add a new `docs/handoffs/world-processes-stageNN-*.md` describing code paths, API, ownership, branch/commit, actual tests/results, missing features and exact next action. Update agent guidance; do not merge unverified code into main.
- Current source is in draft PR #112 on `feature/world-processes-experimental`. New Stage11+ features are **source-only, not confirmed compiling or passing runtime tests**. The user deferred testing; do not describe old Stage05 successful tests as proof of these newer files.
