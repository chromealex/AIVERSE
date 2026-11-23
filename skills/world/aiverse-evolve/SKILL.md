---
name: aiverse-evolve
description: Use AIVERSE EVOLVE for Long-term ecological region changes driven by PHYSIS and consumed through TerraWeave forest ecology.
---
# EVOLVE — AIVERSE skill

## When to activate
Long-term ecological region changes driven by PHYSIS and consumed through TerraWeave forest ecology.

Typical requests: fertility, contamination, forest growth, environmental aging, succession. Use this skill only for this mechanic; for multi-system orchestration see `skills/world/aiverse-world-processes/SKILL.md`.

## Canonical code and API
- **Gameplay headers:** `libraries/include/aiverselib/world/EvolveEcs.h`, `libraries/include/aiverselib/world/EvolvePhysisBridge.h`.
- **Authoritative ECS:** `evolve::Region, Pressure, ClimateLink`.
- **Lifecycle:** `aiverse::lib::worldprocess::installSuite(sim,flags) with flags.evolve=true`; per-tick behavior: `evolve.physis_climate -> evolve.succession`. Install once in the authoritative `Simulation::setup` phase, before the persistence registry seals; don't install the same system twice via suite and direct calls.
- **CMake target:** no dedicated `AIVERSE::lib_*` target for this skill. This is an opt-in existing-native-module gameplay recipe (`runtime: none` in `skills/catalog.json`). The actual project links its established `AIVERSE::libraries` and any optional host owners (Box3D, Renderer3D, PHYSIS, TerraWeave) only when needed.
- **Studio-authored components:** `EvolveRegion, EvolvePressure`. These are map authoring fields, not a writable duplicate of live ECS output.
- **Adjacent adapters:** `examples/world/world_processes_native/EvolvePhysisHost.h`, `EvolveTerraEcologyBridge.h`, `WorldProcessGameplayHost.h`. Native project integrations are optional; include only in the target that already owns those subsystems.

## Minimal C++20 usage pattern
```cpp
#include "aiverselib/world/EvolveEcs.h"
aiverse::lib::worldprocess::SuiteFlags flags;
flags.evolve=true;
aiverse::lib::worldprocess::installSuite(sim,flags);
// After real PHYSIS and EVOLVE entities spawn, the native host may use
// attachEvolveClimate(world,producer,region,physisCell,link).
```

If using normal GameCore map projects instead of a custom module, prefer the compiled opt-in `world_processes` native module and Studio. Example fragments: `examples/native/world_processes/authoring-examples.json`; native setup/lifecycle: `examples/native/world_processes/README.md`.

## Do not cross these ownership boundaries
PHYSIS owns weather, TerraWeave owns real ecology/tree population, NEREID water. Editor worldProcessGizmo.radiusMetres is ONLY a visual helper and does not set EvolvePlantRegion's actual rectangle.

## Acceptance checklist (execute when user lifts test deferral)
- Climate tick ordering, bounded rates/catchup, overlapping biome regions, no second weather authority, valid PHYSIS sources, explicit real host region bounds.
- Verify C++ compilation, CTest source and actual run outcome; include GCC/Clang, ASan/UBSan where supported.
- Check authoritative tick order, World snapshot/COW, persistent schema IDs, clean reload, rollback/replay checksums, malformed input rejection, and expired Entity generation references.
- If the feature is visually observable, capture **real** native Studio/game screenshots with source SHA and platform/backend provenance; do not use fabricated renders as QA proof.
- Stage13/23–28 native forest and editor verification deferred.

## Handoffs and current verification
- Read matching `docs/handoffs/world-processes-stage13-granular-evolve-society-2026-11-23.md`, `docs/handoffs/world-processes-stage23-evolve-physis-terra-2026-11-23.md`, `docs/handoffs/world-processes-stage25-gameplay-host-2026-11-23.md`, `docs/handoffs/world-processes-stage28-interactive-studio-gizmos-2026-11-23.md` (some stage handoff names contain extra suffixes; search `docs/handoffs/world-processes-stageNN-*` when needed).
- The PR is **DRAFT**; latest source changes have **not** been verified by a completed full native build or end-to-end scene test. Do not infer production readiness from the presence of API/docs.

## Cross-cutting contract
- Read `AGENTS.md`, `docs/agents/ecs.md`, `docs/ecs-mechanic-composition.md`, and `skills/world/aiverse-world-processes/SKILL.md` only as necessary for system boundaries.
- All live game state must be ECS-owned: `World` + `Simulation`. C++20, stable packed-generation `Entity` links in memory; map authoring stores only stable document ID strings. Never serialize ephemeral ECS slot numbers.
- Integration is opt-in. For regular GameCore maps, compile `examples/native/world_processes/WorldProcessesModule.cpp`, enable `{"id":"world_processes"}` in the project's `rules.json` `nativeModules`, and use the existing `StudioSession` map component authoring. Actual adapter: `aiverselib/world/WorldProcessesNativeModule.h` consumes `GameCore::matchMap()` + `mapPlaced()`.
- Shared standalone/linked map schemas: `aiverselib/world/WorldProcessesAuthoring.h`, `WorldProcessesLinkedAuthoring.h`, `WorldProcessesExtendedAuthoring.h`, `WorldProcessesExtendedLinks.h`. `WorldProcessesMapHydration.h` resolves live ECS owners with strict validation before installing/creating components.
- In Studio use existing Add Component, reference picker, Scene gizmos and native `MapDoc` Undo/Redo. For live observation use `WorldProcessesInspector.h`/`WorldProcessesExtendedInspector.h`, never writable runtime clones.
- After each implementation step add a new `docs/handoffs/world-processes-stageNN-*.md` describing code paths, API, ownership, branch/commit, actual tests/results, missing features and exact next action. Update agent guidance; do not merge unverified code into main.
- Current source is in draft PR #112 on `feature/world-processes-experimental`. New Stage11+ features are **source-only, not confirmed compiling or passing runtime tests**. The user deferred testing; do not describe old Stage05 successful tests as proof of these newer files.

## Stage 30: authoritative region bounds, not visual-only gizmos

The real simulation footprint is `evolve::Area` in
`libraries/include/aiverselib/world/EvolveArea.h`.
It is a persistent `evolve.area` v1 ECS component with
`centerXcm, centerZcm, halfWidthCm, halfDepthCm, enabled`.
The centre is derived from the owning authored map entity's real
`pos` at `WorldProcessesMapHydration.h` setup; the half-extents
come from that entity's `EvolveArea` map component. The same entity
MUST also author `EvolveRegion`. Bounds are half-open rectangles
in centimetres, valid half-extents 50–50000 cm.

**Add in Studio:** Add Component -> `EvolveArea`, then set
`halfWidthCm`/`halfDepthCm`, or drag its X/Z bounds directly in
native Scene. These edits use existing native
`studio.worldProcessAreaSet`, one MapDoc Undo/Redo step.
The read-only Live Inspector shows the real ECS Area.

**Consume in TerraWeave:** `evolve::extents(world)` returns
generation-safe bounded rectangles from authoritative Area ECS.
The preferred native `makeEvolveForestClimateSampler(world,baseline)`
and `WorldProcessGameplayHost::forestClimate(world,baseline)`
use precisely these rectangles to modify the existing
`ForestEcologyClimateSampler`. TerraWeave retains the actual forest,
PHYSIS/NEREID the baseline, and EVOLVE the region state and extents.

Legacy `worldProcessGizmo.radiusMetres` remains a visual-only
circle on maps without an `EvolveArea` component. It does NOT
modify the actual TerraWeave ecology region; do not mistake it for
gameplay bounds. Prefer the typed Area from now on.

For overlapping handles, repeated LMB clicks in the same small
screen-space area cycle stable hit candidates. The active field label
shows component/key and ordinal; Escape cancels a drag without
modifying MapDoc.

**Status:** Stage30 authored source, not compiled or
hardware-accepted. The user's test deferral remains active.
See `docs/handoffs/world-processes-stage30-evolve-area-handle-cycling-2026-11-23.md`.

## Stage 31: bidirectional EVOLVE rectangle authoring

`StudioWorldProcessesEditing.h` exposes four symmetric EvolveArea edges:
`AreaWidth`, `AreaWidthNegative`, `AreaDepth`, `AreaDepthNegative`.
All four write the existing typed `halfWidthCm`/`halfDepthCm` fields
through `studio.worldProcessAreaSet` on mouse release; they never change
ECS Area center or map entity position. The opposite side moves equally.
Negative modes were appended to the mode enum to preserve existing values.
The current center is still the canonical authored map entity `pos`.
No build or runtime tests have been run for Stage31 (deferred by user).
See `docs/handoffs/world-processes-stage31-bidirectional-handles-2026-11-23.md`.

## Stage 32: authored EVOLVE center offsets

`EvolveArea` supports `offsetXcm` and `offsetZcm` relative to the owning map entity `pos`. Both default to zero; Stage30 maps missing both remain valid. Native hydration writes the sum to persisted `evolve::Area.centerXcm/centerZcm`; TerraWeave consumes ECS `evolve::extents(world)` unchanged. Studio has separate X/Z center drag handles and persists offset fields through `studio.worldProcessAreaSet` / MapDoc Undo. All four extent handles use the offset center, but still resize symmetrically. Asymmetric resize is not yet implemented. Source-only, no compile/runtime QA (deferred). See `docs/handoffs/world-processes-stage32-evolve-center-offsets-2026-11-23.md`.

## Stage 33: asymmetric EVOLVE edge resize

Four native Scene handles now keep the opposite edge fixed. Dragging an edge sends `edgeSide: positive|negative` and the desired edge coordinate in centimetres relative to the map entity through `studio.worldProcessAreaSet`. The session atomically derives the center offset and half-extent in one MapDoc Undo operation; direct field edits without edgeSide preserve their existing semantics. The ECS area schema remains unchanged. Even-centimetre spans are enforced so center and half-extents remain integral. Tests deferred; see `docs/handoffs/world-processes-stage33-asymmetric-area-resize-2026-11-23.md`.

## Stage 34: live EVOLVE rectangle preview

Native Studio Scene now projects the candidate EvolveArea rectangle during valid center/edge dragging via pure `worldprocess_edit::previewArea` and existing `worldprocess_gizmos::rectangle` overlay shapes. No MapDoc/ECS mutation occurs until release. Current host uses fixed-opposite-edge asymmetric resize; symmetric mode is helper-only, not yet exposed as a user control. Source-only, not compiled/tested. Handoff: `docs/handoffs/world-processes-stage34-evolve-live-preview-2026-11-23.md`.

## Stage 35: Scene resize modifier

Plain LMB edge drag keeps the opposite EVOLVE area edge fixed. Hold Shift during an active edge drag for symmetric resize about the authored center. The live candidate rectangle follows the current modifier and the drag label shows the mode; the host highlights active handles. On mouse-up, symmetric resize sends a typed half-extent to `studio.worldProcessAreaSet` while asymmetric resize sends `edgeSide`; both use one existing MapDoc Undo transaction. No ECS/forest duplication. No compilation or runtime QA performed (user deferred). See `docs/handoffs/world-processes-stage35-evolve-resize-modes-2026-11-23.md`.

## Stage 36: Scene snapping and numeric size preview

Hold Ctrl or Command while dragging EvolveArea center/edge handles to snap the local drag coordinate to a 1 metre grid. Shift still selects symmetric resize; plain drag keeps the opposite edge fixed. Native Scene overlay shows candidate full width/depth in centimetres and active mode. This is transient editor state; the only authored write remains `studio.worldProcessAreaSet` at release. Multi-selection, object snapping and editable numeric Scene fields are not implemented. Source-only; no native build or runtime tests yet. See `docs/handoffs/world-processes-stage36-evolve-grid-snap-dimensions-2026-11-23.md`.

## Stage 37: atomic EVOLVE multi-region center move

Native Scene center-handle drag on a multi-selection submits `studio.worldProcessAreaBatchMove` with stable `entityIds`, axis `x|z`, and integer `deltaCm`. The native session validates every selected EvolveRegion/EvolveArea and coordinate budget, then applies all center offsets with one MapDoc Undo entry. Mixed or locked selections fail atomically. Single-region edits retain the existing RPC. Group edge resize and group preview are not yet implemented. Source-only, no compilation/runtime QA. See `docs/handoffs/world-processes-stage37-evolve-batch-centers-2026-11-23.md`.

## Stage 38: EVOLVE neighbor boundary snapping

Ctrl/Command during native Scene EvolveArea edge drag now prioritizes the closest enabled neighboring authored EvolveArea ±X/±Z edge within 100 cm; stable map ID breaks distance ties. If no neighbor is close, the existing 1m grid snap applies. Center handles remain grid-only. The helper `worldprocess_edit::snapNeighborEdge` is read-only; only the owning area is edited on release through the existing MapDoc RPC. This is axis-only snapping, without perpendicular overlap filtering yet. Source-only, not compiled or runtime tested. See `docs/handoffs/world-processes-stage38-neighbor-edge-snap-2026-11-23.md`.
