# TerraWeave Caverns — stage 9: STRATA presentation leases (2026-11-23)

## Provenance
- Repository `chromealex/AIVERSE`, branch `feature/terraweave-caverns-core-20261123`.
- Created on main at stage 1; not rebased onto current main and not merged.
- `04b9d3b38a5343c4a8738929198ec7b67ac8c939`: `engine/render/TerraWeaveCavernsPresentationLeases.h`
- `665367dc36a0a163f195950c3f51a76ff2125c8c`: `tests/terraweave_caverns_presentation_test.cpp`
- `302d8bab7f35cd47214aa56f1815b14a791755c8`: `skills/world/aiverse-terraweave-caverns/SKILL.md`
- This dated handoff is a subsequent commit; inspect branch HEAD.

## Existing code inspected
- STRATA `engine/render/GeometryScene.h` and `GeometryScene.cpp` define the ownership of sources, source-key preflight, and Renderer3D submission. `prepareStrataRenderCandidate` is preflight only and does not itself hand over draw ownership.
- Stage-5 `CutTerrainResident` commits Renderer3D+SpatialWorld immediately, stage-6 `CavernsBox3DPublisher` commits Box3D immediately. Stage-8 `CavePublicationCoordinator` tracks stage completion, not a real atomic transaction.
- Visual STRATA meshes are presentation only. Gameplay terrain collision and navmesh must continue from authoritative geometry.

## Added contract
- `CavePresentationLeases(maxChunks)` is a bounded versioned registry keyed by `ChunkKey3`.
- `begin(key,sourceRevision,renderRevision)` issues a unique generation token and invalidates older STRATA candidate routing.
- `confirmFallback(ticket)` asserts host has uploaded a viable Renderer3D fallback. `confirmStrataPrepared(ticket)` asserts external GeometryScene accepted/certified immutable matching source data.
- `activateStrata(ticket,exclusiveDrawProof)` gates visibility route on both acknowledgements AND externally proven exactly-once pass routing; `deactivateStrata` restores Renderer3D first.
- `requestRetirement(ticket)` and `finishRetirement(ticket,resourcesReleased)` distinguish intention to unload from completed RHI fence, GPU asset lease, collider and navigation teardown.
- `route(key)` exposes Hidden/Renderer3D/Strata host submission choice. It performs no renderer calls.
- `tests/terraweave_caverns_presentation_test.cpp`: route ordering, stale ticket protection, retirement without premature hiding.

## Validation actually performed
- GitHub create/update operations succeeded.
- **No compile, CTest, RHI GPU, real GeometryScene source registration, live draw or physics/navmesh integration test was performed this stage.** Added tests are not yet executed. Earlier standalone stage-2 artifact in the container is not proof of this branch's present compile status.
- Existing `GeometryScene::prepareStrataRenderCandidate` cannot serve as proof of actual renderer exclusivity or source adoption. No live STRATA rendering is claimed.

## Remaining critical work
1. Implement concrete GeometryScene cave-source injection using existing scene model lifecycle, handling geometry revision keys and cooked hierarchy invalidation, avoiding heavy full-scene reloads if possible.
2. At graphics owner barrier stage and commit the cut terrain, underground cave mesh, Box3D and SpatialWorld together. Current individual immediate publishers cannot guarantee cross-owner rollback on failure; split preparation and visible commit or implement reversible rollback.
3. Make STRATA route actually suppress the matching Renderer3D primitive exactly once in camera+shadow passes with hardware parity; on any lease failure fall back.
4. Revise streaming to retire only after the host acknowledges GPU fences and all physics/navmesh unloads. Avoid dangling models and stale IDs.
5. Verify same-resolution cave mesh seam and LOD-mismatched transitions; test underground walkable spans against real capsule radius.
6. Register stage 1–9 tests in CMake, run actual compiler+CTest, ASAN/UBSAN, Metal/OpenGL/Vulkan QA, and record exact logs.
7. Preserve a new skill appendix and handoff after each further stage. Do not merge feature branch into main without build verification.

## Note on practical readiness
The code currently makes a deterministic **decision protocol** for handoff, not a functioning STRATA/renderer integration. The user requested functionality-first development, but production readiness and claims must remain tied to actual runtime tests and explicit host wiring.
