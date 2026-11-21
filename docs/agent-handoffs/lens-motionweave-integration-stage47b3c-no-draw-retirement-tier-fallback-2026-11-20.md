# AIVERSE LENS Stage 47B.3C — no LOD retirement during prepared draw, shader-tier parity

Date: 2026-11-20
Repo: chromealex/AIVERSE
Branch: feature/lens-motionweave-integration
Parent: cfc4ff9f50b61c7bda682d46f0d796b8593ac659
Prior: docs/agent-handoffs/lens-motionweave-integration-stage47b3b-terrain-pipeline-trace-staging-2026-11-20.md

## Changed in this stage
- TerrainRenderer::drawInternal no longer retires LRU Detail LOD buffers mid-draw in a staged LENS primary frame. Standalone legacy primary still retains its prior LRU path; secondary reads remain allocation-free.
- A staged primary or secondary camera selecting a resident lower-tier shader now uses effectiveTier for material mode uniforms; it no longer asks a low-tier compiled shader for high-tier normal/splat material features.
- TerrainRenderer::prepareFrameVisible now rejects a frame when both the requested and all fallback terrain shader pipelines are unavailable. It does not publish an incomplete/blank terrain frame while the generic frame epoch says 'ready'.
- Added trace height GPU failure injection regression: when tracing is active, an injected failure of the private height texture prevents any live height texture change or GPU retirement. Successful retry commits the new height generation, then draw must not issue new shader, pipeline, texture or buffer allocation. Test gracefully skips traced specifics when AIVERSE_TRACE=off, still checking nontrace preflight.

## Verification
Code changes authored against exact cfc4ff9f50b61c7bda682d46f0d796b8593ac659, static source anchor checks performed, pushed to GitHub and refetched. CMake/CTest/Metal/Vulkan/GLES and real rendering were NOT RUN. Tests authored but UNEXECUTED.

## Risks and next
- Legacy TerrainRenderer::draw on non-LENS hosts can still update dynamic textures, create shaders and retire LOD resources. It must remain intentionally separate until those hosts are migrated.
- Terrain map source mutations/sculpting in RtsView::buildTerrain are still published outside the shared owner cohort, so full Terrain/GroundFX/Road/Shore/Fog map-change atomicity is unfinished.
- New fail-closed shader behavior needs actual Vulkan/GLES tests on feature-complete milestone; some devices may require additional shaders/fallbacks.
- RenderDeviceLifetime and old-resource retirement completion must be validated with actual multi-camera GPU fences.
- Continue Stage48 map change cohort, then MotionWeave sprites, lighting/overlays, consolidated build/test.
