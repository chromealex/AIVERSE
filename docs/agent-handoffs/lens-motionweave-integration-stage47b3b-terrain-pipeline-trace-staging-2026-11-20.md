# AIVERSE LENS Stage 47B.3B — staged terrain pipeline and trace-height GPU resources

Date 2026-11-20
Repo chromealex/AIVERSE
Branch feature/lens-motionweave-integration; main not changed
Parent d3cb1cf900bc89ac895319fb9447fb03f2aed9a9
Previous docs/agent-handoffs/lens-motionweave-integration-stage47b3a-detail-lod-staging-2026-11-20.md

## Implemented code

- Extended TerrainRenderer::PreparedFrame with private material shader/pipeline and traced terrain height texture handles. Its RAII rollback now destroys any newly created pipeline, shader and RHI texture in addition to COW field images and fine Detail LOD buffers.
- prepareFrameVisible compiles one suitable terrain shader/pipeline tier into unpublished handles if no already published tier is available. It falls back from high to low tier, preserving the original GLSL/MSL shader bodies and vertex layout. Errors trigger owner rollback rather than publishing a partial frame; the active published pipeline remains untouched until commitFrame.
- For trace-enabled High shadow frames, heightDirty terrain height pixels are generated and uploaded into a private GPU height texture (with the original 0.3-second throttle). The old trace height remains live until full cohort publication.
- commitFrame installs staged pipeline, shader and traced-height handles with no new RHI allocation or wait; previous traced-height texture retires with NativePresentationRetirement and new terrain height metadata is published.
- Primary TerrainRenderer::drawInternal now uses already resident pipelines and trace height whenever a prepared owner frame was committed. Standalone legacy draw retains on-demand creation. Secondary cameras remain read-only.
- Added a fake-RHI regression test asserting primary draw does not increase pipeline or shader creation attempts after frame preparation.

## Verification
GitHub source examined; commit authored and mechanically checked. No CMake/native C++ build, CTest, Metal/Vulkan/GLES compile or real GPU screenshot/parity test executed. Tests are UNEXECUTED.

## Remaining
- Terrain map topology / editor same-map detail changes are still published by RtsView::buildTerrain before per-frame owner transaction. Full editor multi-owner map change cohort is a separate stage.
- Terrain LRU retirement after draw remains allowed, but new resource allocation/upload for staged primary Terrain is now removed from its draw path. Initial GPU material-kit loading occurs via TerrainRenderer::build outside the frame cohort.
- No GPU memory staging budget or shader compile warmup telemetry yet; backend compile/test and runtime quality need validating.
- Later stage: unify editor map source transactions, then Sprite/MotionWeave, lighting, overlays and full multi-camera parity. Perform one consolidated CMake/CTest before merging.
