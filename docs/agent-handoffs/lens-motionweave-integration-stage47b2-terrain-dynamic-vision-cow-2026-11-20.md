# AIVERSE LENS Stage 47B.2 — dynamic/vision Terrain RHI copy-on-write owner transaction

Date: 2026-11-20
Repo: chromealex/AIVERSE
Branch: feature/lens-motionweave-integration; main unchanged.
Parent: f4d569d1d3d7f3f4d52a45ebd94c4d1a2a5d4fb8
Prior: docs/agent-handoffs/lens-motionweave-integration-stage47b1-terrain-map-staging-2026-11-20.md

## Source implementation
- `TerrainRenderer::PreparedFrame` is an opaque RAII candidate for private GPU dynamic (RGBA8) and visibility (R8) textures plus the matching CPU buffers, the scan cursor, and device/owner/revision identity.
- `prepareFrame(grid,vision,filteredVisibility)` scans exactly the incremental dynamic rows used by legacy `updateDynamic`, creates a whole-image private texture only when its CPU pixels changed; if a heightfield vision mask was supplied, stages either the prepared Fog blurred field or raw 0/128/255 mask. No `updateTexture` touches the published texture. Unchanged maps create no new textures.
- `commitFrame` noexcept retires the old changed GPU generations with NativePresentationRetirement and moves prepared resources into live state, with a one-shot guard and fail-closed revision check. New copies are NOT submitted until the coordinator publishes every owner.
- `drawInternal` suppresses legacy in-place dynamic/visibility texture writes for the one primary frame whose copy-on-write handles were published by the shared owner transaction. Standalone `TerrainRenderer::draw` retains its old update path.
- `RtsView::draw` registers a Terrain participant after GroundFX, Roads, Shores, and Fog. Terrain preparation uses Fog's private, processed visibility field when Fog's new candidate changed, otherwise its old published field; a late terrain GPU failure cancels the entire earlier cohort before publish.
- tests/tests.cpp: added late-owner rollback of both private terrain textures, commit without full idle, and same-input no-upload regression.

## Checked
Exact-parent GitHub source modifications with single-occurrence guards and static source checks. No C++ compile, CMake configure, CTest, Metal/Vulkan/GLES build, actual rendering or backend parity run. The tests are written but UNEXECUTED.

## Known limitations
1. Stage 47B.1 map topology is still committed in `RtsView::buildTerrain` before `RtsView::draw`; a single universal map/topology+GroundFX+Fog transaction has NOT been completed.
2. Terrain lazy Detail LOD, pipeline creation, trace height-field and certain map/detail sculpting GPU changes remain in `drawInternal` or legacy `build`, outside unified pre-render transaction. Multi-camera parity has not been validated.
3. The COW path allocates a complete replacement texture generation on each changed partial-row update, an accepted correctness-first tradeoff; large maps need dirty-rectangle staging and upload limits.
4. `setFog` currently occurs after Terrain surface layer in RtsView, so look settings/late fog flags require follow-up during whole-frame transaction integration.
5. Unverified native compile and test assertions may need adjustments.

## Next
Stage 47B.3: pre-build camera-visible detail LODs, trace height field and pipeline variants before publishing; expose a primary draw that is submission-only. Then unify external Terrain map source staging with dependent GroundFX and Roads height policy, followed by Sprite/MotionWeave, lighting/overlays. Continue per-stage handoff and perform one consolidated native CMake/CTest at functional completion.
