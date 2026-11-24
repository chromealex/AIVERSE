---
name: aiverse-echo
description: Use AIVERSE ECHO for Acoustic gameplay hearing through zones, portals, emitters and receivers without owning audio playback.
---
# ECHO — AIVERSE skill

## When to activate
Acoustic gameplay hearing through zones, portals, emitters and receivers without owning audio playback.

Typical requests: sound through rooms, NPC hearing, acoustic doors, sound alerts and perception. Use this skill only for this mechanic; for multi-system orchestration see `skills/world/aiverse-world-processes/SKILL.md`.

## Canonical code and API
- **Gameplay headers:** `libraries/include/aiverselib/world/EchoEcs.h`.
- **Authoritative ECS:** `echo::AcousticZone, AcousticPortal, AcousticEmitter, AcousticReceiver, AcousticHearing`.
- **Lifecycle:** `aiverse::echo::install(sim)`; per-tick behavior: `echo::propagate(world,tick)`. Install once in the authoritative `Simulation::setup` phase, before the persistence registry seals; don't install the same system twice via suite and direct calls.
- **CMake target:** no dedicated `AIVERSE::lib_*` target for this skill. This is an opt-in existing-native-module gameplay recipe (`runtime: none` in `skills/catalog.json`). The actual project links its established `AIVERSE::libraries` and any optional host owners (Box3D, Renderer3D, PHYSIS, TerraWeave) only when needed.
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

## Stage 85: shared multi-source acoustic propagation
ECHO builds a single directed `lib::WeightedGraph` from active ECS portals, then calls `lib::multiSourceDijkstra` from `engine/core/Graphs.h`. Seed cost is kMaxIntensity minus emitter intensity; cutoff kMaxIntensity-1 removes inaudible zero/negative paths. Minimum cost wins; equal cost chooses the smallest packed emitter Entity ID. Directed links, disabled/stale endpoints, invalid intensity/threshold handling, hearing output fields and persistence remain unchanged. No new persistent cache, graph owner or spatial/raycast physics. The previous single-source `lib::worldprocess::audibility` remains a reference API. Regression source compares against it; build and tests not run. See Stage 85 handoff.

## Stage 113: planetary live ECHO acoustic graph and hearing

Enable Live 3D overlay and the independent ECHO acoustic graph checkbox during local Play current map. The snapshot reads actual ECS AcousticZone, Portal, Emitter, Receiver and AcousticHearing after a completed tick. Receiver hearing must have the exact completed tick and an in-range intensity; stale or missing outputs remain unavailable, not falsely silent. The reported winning emitter, intensity, threshold and validity come from the existing propagation output, never a second graph traversal. Zone/portal enabled state and loss are current configuration, not recorded path attenuation.

Native Scene uses actual Transform and canonical Surface body-local coordinates for zone, emitter and receiver markers. Unplaced nodes remain numeric-only; no fabricated spatial extent. Directed portal lines show enabled topology and configured loss with directional arrowheads; inactive portals are interrupted. Blue zones, amber active emitters, green audible receivers, muted red silent valid receivers, grey unavailable/inactive markers. Dashed green receiver-to-winner relation is not an acoustic route or a physical ray. Cross-body relations stay numeric and are not drawn; the existing logical ECHO graph is unchanged and may deliberately contain cross-body links. No physical acoustic volume, sound playback, dB conversion, audio propagation, or new persistence.

Stable packed-ID ordering and owner component limits; at most 256 zones, 512 portals, 256 emitters and 256 receivers per snapshot. Counts of unavailable/omitted/cross-body refer to sampled candidates. Existing local Play/map/revision guards, native renderer, typed transient settings and HTML5 summary are reused. Browser reports first eight emitters, receivers and portals with actual values. Planar ecology unchanged.

Source regression exercises directed topology, actual attenuation and threshold, cross-body logical propagation without geometry, unplaced receivers, stale hearing, disabled portal and sample cap. Build, CTest, browser typecheck, interactive UI, real captures and performance NOT RUN. Next: CROWD live movement and slot reservation diagnostics. Actual audio and physical/render integration remain separate owners; no main merge.
