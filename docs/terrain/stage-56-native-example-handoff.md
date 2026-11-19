# TerraWeave Stage 56 — native NEREID + TerraWeave application composition

Date: 2026-11-19. Source branch `feature/terraweave`, Draft PR #24.

## Changed source
- `d5b22247cbe5db20776e4e6bbc9a8903067a62a1`: `TerraWeaveEcsAuthority.h` now detects when the completed NEREID ECS tick has ZERO terrain-changing bank outputs, skipping expensive materialization/copying of up to 1m soil/root records when only fractional erosion carry changed (NEREID still persists its own bank component).
- `c208f8016a77f9bd7d624224d32482aa5052da64`: NEW `engine/world/TerraWeaveNativeDriver.h`, `publishNativeTerraWeaveFrame` one-call native frame update barrier with `NativeTerrainPublishResult`. It rehydrates canonical ECS state when revision/rollback changed, publishes up to 1–8 simulation-pinned dynamic soil tiles to the existing gfx::Device / SpatialWorld, synchronizes immutable WorldScene Forest/Horizon snapshot, marks the same terrain revision dirty in existing `nav::Mesh`, and updates bounded grass GPU patch meshes. Returns groundReady, navigationReady, remainingTiles, uploads and ECS revision. Caller MUST gate actor movement on collision/nav readiness and must not call it inside ECS tick or worker thread.
- `8b41cfa0a5d94e8a870d7502b176a9ac1ad5646b`: NEW `examples/world/terraweave_native/Integration.h`, reusable example `registerSystems` wires the real `registerNativeWaterPipeline(...enableBankErosion=true)`, bank vertex persistence, canonical TerraWeave ECS resource and after-NEREID bank soil commit system. `addWaterBody` / `addShoreCell` place sample water and bank entities with complete existing WaterProbe/WaterContact/BankSoil/BankErosionOutput/BankEarthVertex components. `publishFrame` delegates to the native driver.
- `039df4430e0679dae45c077e37c53e07da995496`: NEW `examples/world/terraweave_native/README.md`, step-by-step native registration, water bank placement, dig command, post-authority frame publication and explicit rollback.

## Module interfaces and ordering
1. GameModule::setup: register NEREID native water pipeline with bank erosion on, then bank EarthVertex component and persistent TerraWeaveAuthorityResource v2, then add TerraWeave authoritative bank system AFTER NEREID bank system.
2. A native authoring routine creates one NEREID water body per lake/river/etc and bounded water bank cells (`WaterBankSoilComponent` + water probe/contact/output + `BankEarthVertexComponent`) at the real shore; no per-blade ECS.
3. Each fixed tick: NEREID samples water and computes bank sediment once, then TerraWeave stages the changes and saves canonical soil/root/grass ECS resource. Local digging uses `excavateAuthoritativeEcs` from a validated game command.
4. Device/SpatialWorld owner frame: `publishNativeTerraWeaveFrame` or example `publishFrame` rehydrates on ECS state changes, publishes dynamic terrain, refreshes NavMesh/grass/WorldScene and returns readiness to gameplay. Draw the WorldScene first, then the dynamic TerraWeave coordinator; no double old static terrain draw/collider.
5. Save/rollback: `World` COW and PersistenceEcs resources recover soil/roots/grass data, while Box3D float physics and its dynamic fallen-body transforms are independently server-owned and replicated. On rollback force scene rehydration even if stateRevision has the same numeric value on another timeline.

## Unfinished and QA
These are **opt-in native interfaces and an example header**, not automatically activated in all FPS/RTS game clients. The host must still supply stable collision-ID allocator, authoritative water shoreline rasterization, physical Box3D collider handoff, any network socket/transport and real VFX/audio policy. The existing NavMesh supports a heightfield ground span; true volumetric caves/overhangs and sophisticated grass GPU instancing/material shaders remain separate capabilities.

**Actual verification:** GitHub commit and readback only; **NOT RUN** CMake, AppleClang/GCC/MSVC, CTest, GameModule simulation, full save/restore, NavMesh regression, Box3D dynamic physics, Metal/GL/Vulkan screenshot, frame-time budget or network transport. Previous TerraWeave 9/9 and GPU timer 1/1 validation were on older revisions only. User instructed feature implementation before final QA. Leave PR #24 Draft.

**Next:** Finish remaining true volumetric digging/overhang design only if prioritized; otherwise start native smoke compile and focused tests for Stage53–56, then run complete test suite and fix first errors. Do not assume code works until tests pass.
