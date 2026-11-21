# PRISMA Stage 15 — Bloom Prefilter RHI graph opt-in and clear parity

**Date:** 2026-11-18. **Branch:** `feature/prisma-postfx`.
**Previous handoff:** `docs/handoffs/prisma-stage-14.md`.
**Main sync:** only `main` was merged earlier via PR #31 / `29dd5cd366b45e400c58f91c598d7559fa0613a0`.
**Stage 15 changes:**
- RHI clear parity: `234fd7ec434dab89eb25809b1aecf0b24ae1ad6c`
- Bloom prefilter opt-in: `a686bab1b88d8bcd5a61370326092f174a41e574`
- CPU test sources: `644ae63633829d5b3512461221bc17ec44b91a4d`
- Design documentation: `ab008611987910fedee4387e7d64633bad6f48f2`
**PR:** https://github.com/chromealex/AIVERSE/pull/26

## Implementation

1. `engine/render/PrismaRhiExecutor.h`: `execute(..., rasterDepth={}, clearRasterTargets=true)` now uses the **same** color/depth clear defaults as existing `Prisma::draw()`: clear opaque black `{0,0,0,1}`, depth clear `1.0`. This addresses a concrete Stage 14 discrepancy for the FXAA graph path. For overwrite-only passes a host can explicitly pass `false`; do not change defaults without parity tests.
2. `engine/render/Prisma.cpp`: before the existing Bloom prefilter `draw()`, optional env `AIVERSE_PRISMA_GRAPH_BLOOM_PREFILTER=1` creates a one-pass RenderGraph and invokes `PrismaRhiExecutor` using existing `src.tex`, `down[0].tex`, `depthFor`, `pipes_[kPrefilter]`, `triangle_`, `SimpleU` and texture slot 0. Graph resources are external and preserve exact SDR RGBA8 / HDR RGBA16F formats and original source/downsample dimensions.
3. On no opt-in, invalid handles, unavailable depth or graph dispatch failure, existing prefilter draw is used. On graph success increments `stats_.passes` once; no duplicate direct draw. Other Bloom levels/upsampling, final composite, sun shafts, tilt shift, TAA and FXAA remain unchanged. FXAA opt-in remains separately controlled by `AIVERSE_PRISMA_GRAPH_FXAA=1`. Both experimental flags are OFF by default.
4. `tests/render/prisma_graph.cpp`: new `bloomPrefilterGraphContract()` checks SDR and HDR external graphs, no internal slots, one Raster pass and correct source/output bindings; validates that missing output aborts before callbacks. The existing test target already includes this file.
5. `docs/prisma.md`: documents both changes and verification status.

## Compatibility and risks

No JSON `postfx` schema changes, MCP renames, editor IDs or gameplay ECS changes. This remains PRISMA-owned image processing; SpectraFX remains world VFX. No new shader, render target allocation mode or RHI-wide barrier API was introduced.

**Known risks:** No real GPU image comparison has occurred. Existing `PrismaRhiExecutor` supports only a limited RHI graph subset; it does not automatically manage GPU in-flight target retirement or explicit barriers. Current Bloom path uses external allocations so the experimental executor has zero pool-owned render targets. The raster callback always returns true after drawing; GPU command submission errors cannot be rolled back after execution. No switch to a full RenderGraph pipeline has occurred.

## Tests and verification (factual)

- **Executed:** GitHub connector read existing `Prisma.cpp`, graph headers, Stage 14 handoff and updated source, test-source and documentation files on `feature/prisma-postfx`; local container probe `git ls-remote https://github.com/chromealex/AIVERSE.git HEAD` failed with `Could not resolve host: github.com`.
- **NOT RUN:** native CMake configure, C++ compilation, CTest, shader/pipeline compilation on Metal/GL/GLES, device screenshots, pixel parity, timing or editor smoke. Tests were added, **not demonstrated to pass**. No changes to `main` were made in Stage 15.

## Next agent: Stage 16 / verification

1. On a native checkout of branch `feature/prisma-postfx`, run:
   ```sh
   git fetch origin main feature/prisma-postfx
   git switch feature/prisma-postfx
   git pull --ff-only origin feature/prisma-postfx
   cmake -S . -B build/prisma -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_HDR_TESTS=ON -DAIVERSE_BUILD_EDITOR=ON
   cmake --build build/prisma --target aiverse_prisma_graph_tests aiverse_prisma_volume_tests aiverse_hdr_policy aiverse_editor -j4
   ctest --test-dir build/prisma --output-on-failure -R 'prisma_graph|prisma_volume|hdr_policy'
   python3 tools/skills/catalog.py check
   ```
   Record exact results and fix any failures; no claimed PASS without execution.
2. Compare Metal and GL screenshots at identical camera/time/settings with flags `AIVERSE_PRISMA_GRAPH_FXAA` and `AIVERSE_PRISMA_GRAPH_BLOOM_PREFILTER` unset/set separately and together; include SDR, HDR, resize and Scene/Game view. Confirm the same clear behavior, correct Y-flip and alpha.
3. Add a fake gfx::Device regression for pass balancing, clear descriptors, external binding errors, and failures before GPU submission. The current CPU tests do not execute real device methods.
4. Only after native/GPU verification, migrate Bloom downsample/upsample with proper graph resource lifetimes and per-view pooling. Do not enable a new pipeline globally prematurely.
5. Commit a separate `docs/handoffs/prisma-stage-16.md` describing new code, exact tests and next step. Avoid force pushing or mixing SpectraFX with PRISMA.
