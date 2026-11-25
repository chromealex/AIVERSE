# Stage 123 — FRACTURE planetary debris delivery and Box3D projection
Date: 2026-11-24. Owner: World Processes. Branch: feature/world-processes-stage31-bidirectional-handles.
Base 5062f95e92fc563ebc95439bf7d6050164b6ecb0. Plan 7ec26648f21beafb9c0a56d3fab680f81fc05094. Implementation 43aa65c76118effd23de6fece2857dfc550dc3b1.

## Delivered
- libraries/include/aiverselib/world/PlanetFractureDelivery.h: commitPlanetCollapse validates pending support-loss event and completed tick, stages the existing owner's durable consequence in a copied World, revalidates event/PHYSIS overlap, acknowledges and commits by move. False/exception discards staging. planetCollapseReceipt validates acknowledged identity, generation and absence of active primary PHYSIS fracture.
- examples/world/world_processes_native/PlanetDebrisPublisher.h: actual Box3D Scene::create for owner-supplied dynamic non-mesh nonsensor body description and canonical durable pose; fixed body-anchor subtraction before float publication. One selected body and scene; exact event replay is idempotent, conflicting event or lost external handle rejects. Explicit retire/reset, no duplicate physics world or persistent store. Real handle lookup and before-step PHYSIS bridge with validated current positions.
- engine/physics/PhysisBox3D.h: optional translation-only simulationOffset parameter in existing applyPhysisToBody, canonical PHYSIS sample position with bounded Fixed conversion. Existing gravity compensation remains mass*(PHYSIS-native)*gravityScale; zero-offset callers unchanged. No independent gravity solver.
- examples/world/world_processes_native/PhysicalSceneHost.h: optional debris publisher accessor and reset/rollback/shutdown lifetime integration.
- tests/world_processes_gamecore_native_test.cpp: source fixture for stale/future tick, rejected staged mutation, receipt/duplicate commit, rollback; existing PHYSIS primary overlap case retained. Fixture is not a physical contact test.

## Owner contract
At authoritative serialized barrier after FRACTURE support evaluation, call commitPlanetCollapse with a callback that persists the actual consequence and original support removal in staged ECS, never external Box3D state. Acknowledgement alone is not a substitute for the owner's durable record. On physics thread project owner-supplied saved shape/pose/velocity through PlanetDebrisPublisher after that commit; do not leave the original collider in the same place. Call applyGravity once before each physics step; on partial force error abort/reconcile, not retry forces on the same step. Frame uses body-local translation only, axes aligned to canonical simulation. Explicitly retire repaired/deleted effects; on rollback reset projection and recreate from restored owner records. PHYSIS primary events use their own dispatch, not this support-loss path.

No automatic GameCore wiring, persistent debris shape/pose owner, float-authority replay, collider replacement, renderer binding or real Box3D contact validation supplied. Actual production delivery remains host integration work. No new ECS schema or alternate simulation. No new CMake target; optional native physics dependency only. Existing flat physics adapter source remains compatible for direct calls.

## Verification
GitHub source inspection and lease writes performed. Native compile, CTest, real Box3D spawn/contact/gravity, regression, browser/MCP/Studio, performance/platform checks and merged-tree Mac quick NOT RUN (owner test deferral). Skill/compatibility updated. No main sync/merge or publishing acceptance.

## Next
Stage 124: MORPH actual model-stream binding through existing Renderer3D owner and planet model frame; preserve existing deformation/physics ownership. Stage 126 host integration must provide durable debris owner, original collider replacement and render lifecycle, plus remaining TerraWeave dependencies.
