# AIVERSE LENS Stage 47A.2 — correct unpublished GroundFX kit cleanup on refused GPU idle

Date: 2026-11-20
Repo chromealex/AIVERSE
Branch feature/lens-motionweave-integration
Parent 325a75d9203c43fa2372f7ed61836a6ebd80b6d0
Earlier: docs/agent-handoffs/lens-motionweave-integration-stage47a1-resident-groundfx-materials-2026-11-20.md

## Changed

- RtsGroundFx::init catch now preserves original exception with std::exception_ptr; releases only unpublished prepared shader/pipeline, white fallback, and staged TextureCache references directly without calling published-owner shutdown's full-GPU-idle barrier. No GPU submission could have referenced these newly created kit resources. The live old kit is never touched on failed reload.
- This fixes an existing risk from the previous init staging code: when reload's waitForResourceIdle() explicitly refused, calling staged->shutdown() would itself refuse cleanup, leaving staged textures/shaders allocated and preventing the cleanup-count test from being valid.
- Added two generated material textures in previous Stage47A.1; their private ownership now releases on refused idle, allocation failure or invalid kit.

## Validation

Connected GitHub source read, exact unique source patch, post-push source fetch. No native CMake/CTest or backend compilation executed. Existing groundfx_gpu_kit_staging_recovers_from_pipeline_texture_idle_failure checks refused-idle teardown (updated expected 3 textures). This test remains unexecuted.

## Next

Stage 47B: external Terrain/Detail prepared rebuild and owner transaction; then cache optimization and cross-owner GPU fault/secondary-camera validation. Each stage adds independent handoff. Run consolidated native CMake/CTest and Metal/Vulkan/GLES checks only after all planned features are implemented.
