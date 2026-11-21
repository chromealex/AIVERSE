# PRISMA Stage 23 — HDR GPU Auto Exposure

**Date:** 2026-11-19. **Branch:** `feature/prisma-postfx`. **PR:** https://github.com/chromealex/AIVERSE/pull/26. **Previous handoff:** `docs/handoffs/prisma-stage-22.md`.
No merges or updates to `main` during this stage. The branch already included the earlier main sync via PR #31.

## Code commits and files

1. `36e1ac0ff1eaff6a4c1c0a4b90d2621a7edf991e` — `engine/render/PrismaData.h`: appended `PrismaType::AutoExposure` before Count, preserving existing enum values.
2. `013b25f2beb75b61cec5df60514e391dafd984aa` — `engine/render/PrismaData.cpp`: catalog `autoExposure`, defaults, quality gate High, Heavy device cost estimate; six adjustable parameters `keyValue`, `minEV`, `maxEV`, `speedUp`, `speedDown`, `compensation`.
3. `bf775a6775dfaba2665d5bb6a54f5a63c66c4d28` — `engine/render/PrismaShaders.h`: GLSL/MSL common-subset HDR shaders `kExposureLog`, `kExposureReduce`, `kExposureAdapt`. They compute geometric-mean luminance from a multi-level log2 pyramid and temporally adapt the EV exposure factor into a 1x1 RGBA16F history texture.
4. `5fa05244f2a16e44c85978d45f9c3b57be51ec86` — `engine/render/Prisma.h`: per-size cached ping-pong exposure state (previous 1x1 texture index, validity, timestamp), shader pipeline slots, public `resetAutoExposure()`.
5. `178325ff6f67a08ca84e2ba7f1105028803ef6c2` — `engine/render/Prisma.cpp`: opt-in `AIVERSE_PRISMA_AUTO_EXPOSURE=1` HDR metering and temporal adaptation, raster passes before DoF/FXAA/Bloom. Uses existing `Prisma::target`, `depthFor`, `draw` and RHI shader cache, without per-frame CPU texture readback.
6. `41641273edde0fdf45b479bbc7a4250b7a27c454` — `engine/render/PrismaShaders.h`: add optional `PF_AUTO_EXPOSURE` composite branch, applying TEX8 exposure factor before ACES tone mapping/sRGB conversion.
7. `4966acf0675121b4b13c850a03b26296b55cfca6` — `engine/render/Prisma.cpp`: reserve texture slot 8 only when Shader Graph uses at most three custom texture slots (5..7), preserve user fourth texture at slot 8 by skipping auto exposure. Mask/variant and sampler binding are adjusted consistently.
8. `bd424a384ea1db7b872d045e1560e4de2cef8bd0` and `b458435fc53d15b6170ff1224c0b29aa6c749273` — reset temporal adaptation upon quality change, enable toggle, game/map configuration; explicit API for camera cuts.
9. `742a10dfb74cba5b07cab6ab12593fe70fe6366b` — `docs/prisma.md`: API, source example, quality/opt-in and limitations.

## Public API and user-facing configuration

`postfx.effects` supports:
```json
{"type":"autoExposure","keyValue":0.18,"minEV":-6,"maxEV":6,"speedUp":3,"speedDown":1,"compensation":0}
```
The normal parsing, editor/MCP catalog and volume override mechanisms use the new type automatically; external identifiers `postfx`, `postfx_*`, `Prisma` remain stable. Auto Exposure defaults to `minQuality: high` and is rendered **only** when `AIVERSE_PRISMA_AUTO_EXPOSURE=1` and the scene uses HDR RGBA16F. Other configurations do not invoke the GPU path. Explicit native `Prisma::resetAutoExposure()` clears history when the camera teleports/cuts. The 1x1 multiplier is applied pre-tonemap to scene+bloom/shafts linear color; UI is still after composite and not auto-exposed.

Per-view adaptation state currently lives in the preexisting `Prisma::Sized` cache. **Important:** this cache is indexed by target dimensions and can be shared by different same-sized cameras; do not claim true stable camera ID isolation yet. Quality/configuration toggles and disabling Auto Exposure clear validity; first frame writes without sampling initialized previous pixels.

## Actual validation

**Performed:** fetched/reviewed current RHI and PRISMA interfaces, shader generator and composite logic, serialized data catalog, and changes through GitHub connector. Performed static readback consistency checks for new pipeline symbols and shader hooks.

**NOT RUN (explicit user instruction to focus on features):** CMake configure, native C++ compile, CTest, fake RHI device, Metal/GL/GLES shader compilation, screenshots, pixel comparisons, profiling, CI. No claim that Stage 23 is verified or production-ready.

## Known limitations / risks

- Auto exposure currently measures geometric mean **log luminance** with approximate 2x2 sampling (not a histogram/percentile with highlight rejection). Source sampling relies on the existing GLSL/MSL render-target UV convention. SDR path intentionally disabled.
- The RHI's `gfx::PixelFormat::RGBA16F` and sampling render targets must support signed log-luminance; Metal/GL driver behavior still unverified. ~log2(screen max dimension) extra raster passes per frame.
- Custom ShaderGraph may use up to four texture slots (5..8). Slot 8 is shared with exposure only if the graph has at most three custom textures. If all four are in use, auto exposure is skipped; this is an intentional compatibility fallback rather than sampling arbitrary resources.
- Manual ColorGrade exposure stacks with automatic EV. The minEV/maxEV bounds are configured separately in the common catalog; no sophisticated spot/center weighted metering or exposure compensation curves yet.
- No stable view ID: Scene/Game windows of equal dimensions may share exposure history; next step is a proper camera-keyed temporal context API.
- No GPU timestamp or dynamic quality governor integration for exposure passes. Memory/performance not measured.
- This feature is NOT physically accurate ISO/aperture/shutter exposure simulation and does not implement local tone mapping.

## Next implementation stages (features first; tests later)

1. Add explicit stable camera/view IDs and a temporal context map, or extend renderer's per-view token contract; prevent Scene/Game exposure cross-talk while keeping current target cache compatibility.
2. Add center-weighted metering / histogram trimming with highlight protection and configurable metering mask. Avoid CPU readback.
3. Implement motion vectors and motion blur in PRISMA, with explicit camera-cut invalidation and temporal reprojection history.
4. Later native acceptance: compile `aiverse_prisma_graph_tests`, `aiverse_editor`, `aiverse_hdr_policy`; run CTest, Metal/GL screenshots, HDR exposure transitions between dark and bright scenes, repeated scenes, same-size dual viewports, and shader graph texture slot 8 compatibility.
5. Continue the required separate `docs/handoffs/prisma-stage-24.md` after next code stage, with actual verification reported as NOT RUN until the user requests tests.
