# AIVERSE LENS — Stage 47B.1: externally staged terrain full-map and detail topology changes

Date: 2026-11-20
Repo: chromealex/AIVERSE
Branch: feature/lens-motionweave-integration; NOT merged into main
Parent commit: 533000e00785d49397b8b6c900b68017eee703e3
Previous handoff: docs/agent-handoffs/lens-motionweave-integration-stage47a2-unpublished-kit-rollback-2026-11-20.md

## Architecture and changed code

- `modules/rts/view/RtsTerrainRender.h/.cpp`: adds `TerrainRenderer::PreparedMap` opaque RAII candidate, `prepareMap(gfx::Device&, MapGrid, flatDig)`, `commitMap(shared_ptr<PreparedMap>) noexcept`, `mapMatches(w,h)`, `canPrepareMap()`, and publication revisions. prepareMap requires a live primed material kit, builds an entirely private full-map candidate by reusing existing `build` algorithms for classic and smooth Detail. It neither borrows existing terrain VB/IB nor mutates live CPU surfaces, R8 textures or render bindings. Abandoned candidates destroy only newly prepared GPU mesh/texture handles. This path uses no GPU-idle barrier.
- commitMap validates the exact owner/device/publication revision before touching live state, retires old map mesh buffers, map and detail textures, plus trace height texture via NativePresentationRetirement, then swaps all prebuilt CPU maps/mesh handles and preserves the current shader/pipeline and external tiling-material kit. The candidate is one-shot. Commit does no fallible upload.
- `modules/rts/view/RtsTerrainDetailMesh.cpp`: `setDetail` advances publication revision so stale prepared map commits fail closed. setMaterialLayer likewise invalidates candidate.
- `modules/rts/view/RtsTerrain.cpp`: RtsView::buildTerrain uses externally staged `prepareMap/commitMap` for map dimensions changes after initial successful build. First build and same-size chunk/detail sculpting keep the existing high-performance incremental `TerrainRenderer::build`. Delays paintLayers shutdown until successful terrain publication.
- `tests/tests.cpp`: adds a two-owner late fault rollback and retry (with GPU idle refused during successful commit), plus smooth Detail map change/discard/one-shot candidate test.

## Verification actually performed

Code examined, exact source replacements performed, and code written in GitHub on a separate LENS feature branch. No CMake configure, C++ compiler, CTest, Metal/Vulkan/GLES compile, GPU parity or runtime benchmark was run. The new tests are UNEXECUTED and may need correction during the final consolidated native run.

## Scope and important remaining risks

1. This Stage 47B.1 exposes a safe external prepare/commit for full-map topology transitions. It is **NOT YET a fully unified Terrain + GroundFX + Roads + Shores + Fog owner cohort**, because `RtsView::buildTerrain` is a host map-change entry point, earlier than `RtsView::draw`; it publishes its own terrain after private prepare. Stage 47B.2 must coordinate staged map changes with the first frame and its dependent height-at policy without breaking editor, minimap and client consumers.
2. The existing same-size fast incremental terrain path remains in `build()` for performance. It still publishes independently before draw and must eventually gain a sparse external staged journal.
3. TerrainRenderer::drawInternal still updates dynamic/vision GPU textures and lazy Detail LOD/trace height resources during the primary camera draw. These should be staged before the owner cohort for complete multi-camera frame atomicity.
4. Full-map candidate duplicates all mesh/texture resources temporarily; do not route every brush edit into this path or lose incremental performance. No hard transient GPU budget yet.
5. Material layer TextureCache remains a separately owned stable kit; prepareMap requires that the live kit was primed by an initial normal build and does not stage kit reload or shader/pipeline creation.
6. Revisions assume one render-owner thread between prepare and commit; external device-loss and ownership generation audits are still pending.

## Specific next stage

Stage 47B.2: stage the per-frame Terrain Dynamic + Visibility texture generations (copy-on-write), ensure camera-specific LOD draw consumes prebuilt packets with no GPU uploads, add a Terrain participant in the primary RtsView::draw owner transaction, then coordinate editor buildTerrain source changes and height policy as a separate map-change transaction. Follow with Sprite/MotionWeave, lighting and overlays. One handoff per stage, consolidated build/tests later.
