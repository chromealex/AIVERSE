# Stage 11 handoff — Building StructureGraph + exact placement
Date: 2026-11-23

## Provenance

Repo: chromealex/AIVERSE
Branch: feature/aiv20261123_building-grammar
PR: https://github.com/chromealex/AIVERSE/pull/114 (Draft)
Base branch remains feature/aiv20261122_building_styles, behind main.
Do not merge into main without rebase and parallel Studio coordination.

## Delivered code and API

- libraries/include/aiverselib/systems/BuildingStructureGraph.h:
  generic, style/genre-independent `BuildingFeature` types with optional
  `position`, `positionZ`, `spacing`; `BuildingGraphSolid` and
  `BuildingGraphSocket`; `buildFunctionalStructureGraph` reserves
  authored wall spans and automatic fill without moving pinned openings.
  Stable sockets: entrance/<featureIndex>/<copy> or roof/<featureIndex>.
- libraries/include/aiverselib/systems/BuildingFeatureGenerator.h:
  parser extended with exact placement keys; geometry, collision boxes
  and semantic anchor metadata now generated from one StructureGraph.
  `FunctionalBuildingResult::graph` contains building-local structural
  representation; `anchors`/gate anchors are transformed using origin/yaw.
- apps/studio-web/src/types.ts and
  apps/studio-web/src/panels/generated/BuildingGeneratorPanel.tsx:
  controlled fields for wall centre, repeat gap, roof centre X/Z; -1 is auto.
  Studio uses the existing shared control library (not browser file writes).
- apps/studio-native/StudioBuildingGeneration.h:
  save exact-placement properties to canonical *.glb.recipe.json sidecar.
- engine/assets/building-recipes/multiuse-open.json:
  actual pinning example (front big gate X=7, back small pair centred 12
  with gap=1, personnel door X=18, roof centre X=13,Z=10).
- tests/libraries/building_feature_tests.cpp:
  strict graph tests, geometry/physics common origin, socket coordinates,
  fixed and automatic mix, overlap and roof-retract rejection.
- tests/studio/BuildingGenerationTests.cpp:
  exact-placement sidecar round-trip checks.
- docs/building-structure-graph.md: API/scope/schema and Mac commands.
- Studio old preset selection remains unchanged/available.

## Verified vs unverified

GitHub connector wrote and read back source revisions. Tests have been
AUTHORED, not confirmed run on the latest commit in macOS/Linux CI.
Earlier Stage 4 C++ tests passed on a much older commit; the user's
legacy 20-case Mac CLI returned 19 PASS and one factory failure.
Do NOT claim 20/20, metal screenshot, new Studio UI functional pass, or
animated GPU panels without fresh evidence.

## Boundaries and next agent

This is a **single-shell P0 layout graph**, not a multi-floor load-bearing
architectural structure graph. It does not yet generate structural joints,
corridors or interiors. No dynamic GPU node rig for doors/roof; open fraction
currently returns a new CPU geometry state. Collision uses conservative
boxes; physics and navmesh publication remain existing engine owners.
The graph's sockets are building-local, whereas output anchors are
world-transformed. Runtime generation must remain genre-independent.

Next: run focused GCC/Clang C++ tests and Mac Metal view, fix failures.
Expand graph with windows, window collision/visibility/occlusion, load
frames, precise opening side placement, parent/child wing attachments,
physical roof actuators and multi-floor partition/navmesh.
After each stage commit a new handoff with tested evidence and branch SHA.

## Post-handoff fixes (same stage)

- `2f16c8215bc080c0f186f4bf9cb6c48deda93328`: remove obsolete local
  geometry layout constants after switching to the shared StructureGraph.
- `8087f4e051da2669df8aef0df977464b013651ee`: require -1 (not arbitrary
  negative values) for automatic placement, and reject partly retracted door
  leaves that would cross the building envelope.
- `cbc1405e37c30c8620099c007e917d7d25591dac`: regressions for partial
  sliding and invalid positions.
- `ff99cdc02bfaefffc73c1dd9b9b00370426049b8`: native Studio bridge
  explicitly includes the feature generator it consumes (no transitive include).
- Current verification still requires the queued focused C++/Studio CI checks,
  then a Mac run with the exact pinned recipe and GPU screenshot.
