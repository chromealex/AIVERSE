---
name: aiverse-echo
description: Use AIVERSE ECHO when implementing Акустическое распространение через зоныпорталы, слух NPC и игровые сигналы, независимые от аудиорендера.
---
# ECHO — AIVERSE skill

## When to activate
Акустическое распространение через зоны/порталы, слух NPC и игровые сигналы, независимые от аудиорендера.

Typical request: звук сквозь комнаты, слышимость охраной/NPC, акустические двери, игровая аудиоперцепция, шум. Use this skill only for this system; for cross-system configuration consult `skills/world/aiverse-world-processes/SKILL.md`.

## Canonical code and API
- **Gameplay headers:** `libraries/include/aiverselib/world/EchoEcs.h`.
- **Authoritative ECS:** `echo::AcousticZone, AcousticPortal, AcousticEmitter, AcousticReceiver, AcousticHearing`.
- **Lifecycle:** `aiverse::echo::install(sim)`; per-tick behavior: `echo::propagate(world,tick)`. Install once in the authoritative `Simulation::setup` phase, before the persistence registry seals; don't install the same system twice via suite and direct calls.
- **Studio-authored components:** `EchoZone, EchoPortal, EchoEmitter, EchoReceiver`. These are map authoring fields, not a writable duplicate of live ECS output.
- **Adjacent adapters:** `WorldProcessesLinkedAuthoring.h`, `WorldProcessesMapHydration.h`, `WorldProcessesInspector.h`. Native project integrations are optional; include only in the target that already owns those subsystems.

## Minimal C++20 usage pattern
```cpp
#include "aiverselib/world/EchoEcs.h"
// In a custom GameModule::setup, when installing without the map adapter:
aiverse::echo::install(sim);
// Game host authors AcousticZone / Portal / Emitter / Receiver on live
// generation-safe ECS Entities. Read AcousticHearing after the ECHO tick.
```

If using normal GameCore map projects instead of a custom module, prefer the compiled opt-in `world_processes` native module and Studio. Example fragments: `examples/native/world_processes/authoring-examples.json`; native setup/lifecycle: `examples/native/world_processes/README.md`.

## Do not cross these ownership boundaries
Audio playback/spatialization/occlusion renderer is not implemented here; zones/portals represent gameplay acoustic topology, not HRTF or physical ray tracing.

## Acceptance checklist (execute when user lifts test deferral)
- Unresolvable zones, stale portals, attenuation bounds, deterministic route tie-breaking, snapshot/replay and read-only hearing consumers.
- Verify C++ compilation, CTest source and actual run outcome; include GCC/Clang, ASan/UBSan where supported.
- Check authoritative tick order, World snapshot/COW, persistent schema IDs, clean reload, rollback/replay checksums, malformed input rejection, and expired Entity generation references.
- If the feature is visually observable, capture **real** native Studio/game screenshots with source SHA and platform/backend provenance; do not use fabricated renders as QA proof.
- tests/ecs_echo_acoustics_test.cpp.

## Handoffs and current verification
- Read matching `docs/handoffs/world-processes-stage02-echo-2026-11-23.md` (some stage handoff names contain extra suffixes; search `docs/handoffs/world-processes-stageNN-*` when needed).
- The PR is **DRAFT**; latest source changes have **not** been verified by a completed full native build or end-to-end scene test. Do not infer production readiness from the presence of API/docs.

## Cross-cutting contract
- Read `AGENTS.md`, `docs/agents/ecs.md`, `docs/ecs-mechanic-composition.md`, and `skills/world/aiverse-world-processes/SKILL.md` only as necessary for system boundaries.
- All live game state must be ECS-owned: `World` + `Simulation`. C++20, stable packed-generation `Entity` links in memory; map authoring stores only stable document ID strings. Never serialize ephemeral ECS slot numbers.
- Integration is opt-in. For regular GameCore maps, compile `examples/native/world_processes/WorldProcessesModule.cpp`, enable `{"id":"world_processes"}` in the project's `rules.json` `nativeModules`, and use the existing `StudioSession` map component authoring. Actual adapter: `aiverselib/world/WorldProcessesNativeModule.h` consumes `GameCore::matchMap()` + `mapPlaced()`.
- Shared standalone/linked map schemas: `aiverselib/world/WorldProcessesAuthoring.h`, `WorldProcessesLinkedAuthoring.h`, `WorldProcessesExtendedAuthoring.h`, `WorldProcessesExtendedLinks.h`. `WorldProcessesMapHydration.h` resolves live ECS owners with strict validation before installing/creating components.
- In Studio use existing Add Component, reference picker, Scene gizmos and native `MapDoc` Undo/Redo. For live observation use `WorldProcessesInspector.h`/`WorldProcessesExtendedInspector.h`, never writable runtime clones.
- After each implementation step add a new `docs/handoffs/world-processes-stageNN-*.md` describing code paths, API, ownership, branch/commit, actual tests/results, missing features and exact next action. Update agent guidance; do not merge unverified code into main.
- Current source is in draft PR #112 on `feature/world-processes-experimental`. New Stage11+ features are **source-only, not confirmed compiling or passing runtime tests**. The user deferred testing; do not describe old Stage05 successful tests as proof of these newer files.
