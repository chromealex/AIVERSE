# AIVERSE World Processes — Stage 14: seven ECS systems in the native project and Studio (2026-11-23)

Branch `feature/world-processes-experimental`, PR #112 DRAFT. Main unchanged.
Implementation from Stages11–13: `MorphEcs.h`, `CrowdEcs.h`, `ImprintEcs.h`, `CircuitEcs.h`, `GranularEcs.h`, `EvolveEcs.h`, `SocietyEcs.h`.

## What changed
- `libraries/include/aiverselib/world/WorldProcessesSuite.h`, commit `f12db0ff506ecbd7d1ba22c99cc881bea5a2e267`: explicit per-category `SuiteFlags`, `suiteFlags(Json)` strict bool parsing and deterministic `installSuite(Simulation&,flags)`. Seven standalone ECS schedulers and portable v1 schemas are installed in stable order. Unselected categories have zero runtime/persistence impact.
- `libraries/include/aiverselib/world/WorldProcessesExtendedAuthoring.h`, commit `7589955d7a3b129ce0088e506349b1b96df5301f`: seven bounded typed map/Studio authorable configs:
  `MorphNode` (rest XYZ, drag, anchored), `CrowdAgent` (position, goal, radius/speed), `ImprintCell` (traces and fade), `CircuitNode` (supply/demand/input drive), `GranularField` (first 1x1 cell mass/capacity), `EvolveRegion` (durability/fertility/succession/pollution), `SocietyPerson` (64-bit topics as 16-digit hex string, credibility/acceptance).
  `supports/defaults/validate/edit/apply(World&,Entity,...)` reject malformed/extra fields, use exact bounded integers and native real ECS. No fictional runtime entities.
- `WorldProcessesMapHydration.h`, commit `a1ec73d11bd187d3be076a384a7f7bee8dd9fa15` + empty-map flags fix `949b5a6a9dedf23dd653f9e419ae66d7d9285d8d`: two-phase validated map application now discovers seven standalone categories, enables the corresponding native system once and hydrates actual spawned game Entity pools alongside ECHO/FRACTURE/AETHER. Optional `SuiteFlags` argument permits explicit categories even without authored nodes.
- `WorldProcessesNativeModule.h`, commit `a4bd84bfc66f729a0b2027507133b7df8378458e`: standard opt-in native module accepts bool config flags `morph/crowd/imprint/circuit/granular/evolve/society`; uses existing `GameCore::matchMap()`/`mapPlaced()`, no modification to engine/ or genre logic.
- `apps/studio-native/StudioSession.cpp`, commits `5476ba679b839c8b5bd0e7df16e09f5f3a14faba`, `591dca7e0c576a2978e4df99addbb6079582b568`: existing Add Component and Scene Inspector fields include the seven new types with appropriate min/max and type validation, stored via existing MapDoc Save/Undo/Redo and applied by the opt-in native module on next game startup.

## Scope limits and status
This is **substantive ECS core behavior**, not full cinematic mesh/physics visuals: MORPH is quantized deformable node graph, CROWD local allocation/avoidance, IMPRINT trace propagation, CIRCUIT discrete power/gates, GRANULAR bounded 2D cells, EVOLVE region dynamics, SOCIETY trust-based topic spread. Do not override owners of Box3D, PHYSIS, TerraWeave, pathfinding, Meta narrative graph or graphics.
Full link authoring for the other components (MorphSpring/Force, CrowdSlot/Preference, ImprintLink/Deposit, CircuitWire/Gate, GranularSource, EvolvePressure, SocietyContact/Message) is **not yet implemented** in Stage14 Studio; native C++ can add them directly. GRANULAR Studio default is deliberately 1×1, and needs dedicated grid authoring for larger fields. No standalone demo scene, mesh deformation binding or external AI event pipeline.

**Testing deferred by explicit user request.** Source committed, but new Stage11–14 C++ was NOT compiled, sanitized, executed or visually verified on Mac. No tests were run, no CTest/CI green claim and no main merge. Older Stage05 successful CI predates these files and cannot validate them.

## Next step
1. Complete Stage15 linked-component authoring for all seven through stable map IDs and native Setup; preserve hard type bounds. Do not store raw ECS indices.
2. Complete presentation/gameplay binding contracts and actual game-core sample scenes while keeping other subsystem ownership. Keep human/editor data in MapDoc and live dynamic fields in ECS.
3. After requested functionality is present, begin one consolidated test phase (native CMake CTest, deterministic snapshots, malformed v10 saves, multi-compiler ASan/UBSan, Studio browser/editor, full `./run-tests.sh --quick` on merged Mac tree) before PR merge.
