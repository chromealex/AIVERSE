# AIVERSE buildings — Stage 23U.1: physical authored progress, zero-revision rollback and compile-coverage hardening

Date: 2026-11-24
Repository: chromealex/AIVERSE
Branch: feature/aiv20261123_building-grammar
PR: https://github.com/chromealex/AIVERSE/pull/114 (Draft)
Base observed: feature/aiv20261122_building_styles (not main)

## Work accomplished

User requested to finish the feature, beyond the initial source-only implementation. Reviewed actual branch implementations for Simulation/World ECS state, physical/graph owners, native Studio preview, Rendering, material styles and CI. GitHub Actions for Building Style Focused, HTML5 Studio and platform CI remained pending/queued at time of inspection. No successful native compile or GPU result was available; do not claim the feature is fully verified.

### Functional fixes

1. `libraries/include/aiverselib/systems/BuildingStyle.h`: moved newly introduced `surfacePattern` and `surfaceSeed` fields to the END of the C++ `StyleMaterial` aggregate, preserving positional initializer mapping for all preexisting in-engine and native client code. Material JSON schema and generated PBR texture behavior unchanged. Commit `50b8b7fb3b10e185e0b1e3459953e1a0c1a52880`.
2. `engine/buildings/BuildingDualLeafOwner.h`: allow restored revision zero, which occurs legitimately when ECS rollback restores a scene before any door motion. Nonzero revision is generated in `restoreBuildingEcsLeafScene` during explicit graph invalidation and checked before commands resume. Commit `a3fd314d43f193169ca9d8c5f795c6155e6b5887`.
3. `engine/buildings/BuildingApertureOwner.h`: faithfully convert source functional socket `open` fraction into 0..1000 physical travel progress; preserve conservative collision/closed access for all non-1000 progress. Added direct algorithm include for std::clamp. Commits `cd7298d7018b196b8443e282ab49d23419551622` and `1b123306dfafccdd248d63ce9ceadeccac2dd128`.
4. `engine/buildings/BuildingDualLeafOwner.h`: use authored `pose.physicalPermille` when initially attaching the paired physical leaf colliders, instead of unconditionally snapping to endpoint 0 or 1000. Reject progress outside 0..1000. Partial progress stays physically blocked until full opening. Commit `bb24b2486e2ae38482621bf4300a5b31fdbcd2e0`.
5. `engine/buildings/BuildingAperturePresentation.h`: initialize the visual progress from physicalPermille rather than pose.open, preventing initially half-open doors from popping visually closed on the first frame. Commit `6432a4c2b8e06d32961fe79ba6865f80bca709b9`.

### Verification additions

6. `tests/render/runtime_building_model_compile.cpp`: in the existing g++ -fsyntax-only contract, include and typecheck shared ECS/obstruction/reconcile/Renderer3D headers and physical restore signatures, in addition to the base geometry-to-model API. Commit `060d4d2c90a7eb1a6b2125be975537e381ccbb1b`.
7. New `tests/libraries/building_dual_leaf_restore_tests.cpp`: real SpatialWorld two-leaf attach at 500/1000, exact rollback restoration from revision zero, successful graph-ACKed step 500→600, blocking-sweep rejection, and release of both collider IDs on teardown. Header-only owner plus real SpatialWorld.cpp, Fixed.cpp and Json.cpp, no obsolete FPS/RTS. Commit `7024488aa0ec09c79ec16251df17a81342fbcc69`.
8. `.github/workflows/building-style-focused.yml`: build and execute the standalone physical dual-leaf test alongside preexisting geometry, renderer compile-only, all-12 style, GLB/PBR tests. Commit `69dc8f461f1d7a2b3607408bf8906c84d593f137`.
9. Parent agent `skills/world/aiverse-building-style/SKILL.md` updated with zero-revision, physical progress and test requirements. Commit `854e17f5290f6dbee69cb98772c2ad93ca680485`.

## Architecture invariants

- No genre/game-specific FPS/RTS modules were modified. Existing shared ECS/Simulation, SpatialWorld and graph router are the only authoritative state owners. The renderer interpolates the exact verified physical progress but never decides access.
- The initial `open` parameter represents physical opening fraction, not authorization to walk through a partially open portal. Valid access requires both fully open geometry and graph consumer acknowledgment.
- At rollback from revision 0, a new graph invalidation is published as revision 1, and missing graph domain owners must keep the runtime suspended.
- GPU resources continue to be explicitly owned by the existing Renderer3D and StudioScene, with device generation checks and full-idle teardown barriers.

## Actual executed verification and blockers

**What was actually checked:** GitHub metadata/state and source API signatures were inspected, and previous Stage 23U already performed 623 static checks over 12 JSON settings with no issues. This is not a native parser/build execution.

**What has NOT passed:** local C++ build, newly authored tests, TypeScript/Vite build, real GLB/PBR encoder, macOS Metal, Vulkan, GLES, PHYSIS-rollback runtime, Studio preview capture, GPU memory/retirement trace, actual native/NavMesh graph refresh. GitHub Actions jobs were observed queued or pending, with earlier runs canceled by new commits. This environment has no mounted private AIVERSE checkout and no network DNS for github.com; the connected GitHub tool can read/write source but cannot execute a native build. Stop coding when ready for a stable CI SHA rather than repeatedly canceling queued workflows.

## Next agent exact next steps

1. Read PR #114 latest head_SHA after this handoff. Follow active Building Style Focused, HTML5 Studio, PHYSIS, ECS and CI workflows for that SHA (not old canceled runs). Inspect job logs for real failures.
2. Repair any C++ errors from newly compiled BuildingEcsRuntime/BuildingEcsReconcile/BuildingRenderer3DScene headers or native Studio/BuildingGlbWriter with specific compiler evidence. Check platform-specific Box3D availability for optional PHYSIS adapter.
3. Execute the new physical 500/1000 test and all 12 GLB/PBR exporter validation on a runner. Verify actual interactive native preview and GPU-safe replacement/teardown and a real screenshot; fix results before declaring a PASS.
4. Remaining design integration: common game host must opt into `registerBuildingEcsPersistence` and `attachAfterSetup`, supply real ECS collider extents, graph consumers, unique collision ID leases, and GPU render owner. The source adapter does not prove a particular executable instantiated those owners.
5. Integrator must reconcile current draft PR's base with main and ensure full platform acceptance before merging. Do not publish a successful visual devblog without real output frames. Keep separate agent handoff after each new stage.

This stage is code-level hardening complete and **verification pending**. Do not claim production-ready completion or merge until all gates are proven.
