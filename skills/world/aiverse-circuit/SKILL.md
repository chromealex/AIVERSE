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

## Stage 95: component-local power routing
`engine/core/GraphPowerAllocation.h` reuses shared weak-component topology from GraphSupportCache. `circuit::PowerCache` checks exact packed membership; allocator compares component member IDs, directed adjacency, supply/demand/enabled. Only changed components re-run stable generator-ID/sorted BFS finite-budget routing, including directed reversal, splits, joins and rollback. Drive and prior signal never invalidate power cache; gates still evaluate every tick using prior signals, switches and device ordering unchanged. Cache is derived, not serialized, full input scan remains. Explicit stateless advance remains. `CircuitSurfaceWiring.h::localWireCandidates` provides bounded read-only proximity search using canonical Transform/Surface, same body, actual 3D chord and distance/ID tie-break; never automatically changes wires, does not block deliberate authored cross-body links, no new network solver. Native consumer/Studio auto-wiring not installed. Tests authored, not run.

## Stage 115: planetary live CIRCUIT power and logic diagnostics

Enable Live 3D overlay and the independent CIRCUIT power / logic checkbox during local Play current map. Read the existing ECS Node supply, demand, drive, powered and signal values; output availability requires exact completed-tick stamps. Switch input state, gate operation and references, device kind, requireSignal, actual active/valid and transition counter/changedTick are read from canonical components. Gate output means the actual destination Node output, not recomputation of its operation from current signals (the authoritative gate solver uses prior-tick inputs). Stale outputs are unknown, not falsely unpowered. No extra power allocation or logic evaluation.

Native Scene uses real Transform and canonical Surface body-local positions. Unplaced elements remain numeric-only. Explicit directed/bidirectional Wire topology and gate/switch/device bindings are shown only when endpoints share a body. Cross-body links remain numeric; no changes to authoritative topology. Yellow high signal, green powered/active, muted red unpowered/inactive, purple valid-input gate and grey unavailable/disabled markers. Green wire means both endpoints report powered, NOT measured current, allocation amount or electricity flow. Direction arrows show authored topology; dashed auxiliary lines are logical bindings.

Stable packed-ID ordering and owner component caps; maximum 256 nodes, 512 wires and 128 each gates, switches and devices per snapshot. Counts of powered/high/active and rejected/omitted/cross-body refer to sampled candidates. Existing local Play, map/revision and selected-body guards, typed transient settings and native renderer reused; flat ecology unchanged. Browser shows bounded diagnostic lists and actual output states. No second simulation, persistence, physical device actuation, wire creation or economy changes.

Source regression covers directed supply, deliberate cross-body logical power without spatial line, switch synchronization, one-tick NOT gate delay, device activation transitions, stale output and cap. Build, CTest, browser typecheck, interaction, actual captures and performance NOT RUN. Next SOCIETY live relationships, messages and NPC trigger diagnostics. No main merge.
