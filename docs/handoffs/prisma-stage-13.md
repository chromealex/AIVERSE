# PRISMA Stage 13 — opt-in gfx::Device graph adapter

**Date:** 2026-11-18
**Branch:** `feature/prisma-postfx`
**Prior stage:** `docs/handoffs/prisma-stage-12.md`
**Current main base:** main was merged into PRISMA branch by PR #31, merge SHA `29dd5cd366b45e400c58f91c598d7559fa0613a0`.
**New code commits:** `59a285d80545081706ce943e878d5a0c3dc31018`, `9b8bab8ea83a0c674d934c84292e7f1c5200fe36`, `b2f526cd982a37759f7b76f93c4e1e312a4142fd`.
**Test commits:** `7170d4e5a6004f824da7e0085f0edeced994a222`, `903398dcf70d6811d2b54627351dd053df491113`.
**Design doc commit:** `32a52f4458d67bcbe573982232a895756ab4ad30`.
**Feature PR:** https://github.com/chromealex/AIVERSE/pull/26

## What changed

1. Added `engine/render/PrismaRhiExecutor.h`. This opt-in executor maps `PrismaGraphFormat` to actual `gfx::PixelFormat` and rejects formats absent from today's RHI (`RG16F`, `R32F`). It uses Stage 12 `PrismaGraphResourcePool<gfx::TextureHandle>` and Stage 11 `dispatchPrismaGraph`.
2. New `PrismaRhiExecutor(device).execute(graph,plan,external,raster,compute,optionalDepth)` validates graph shape, external bindings, device capabilities and pass restrictions before allocations/commands; prepares render-target textures by calling `gfx::Device::createTexture`; runs Copy passes using `copyColor`; runs Raster callbacks inside `beginPass/endPass`; calls Compute callbacks only where `supportsCompute` is true.
3. All callback reads resolve through `PrismaGraphDispatchView<gfx::TextureHandle>::resource(id)`. Raster callback must bind its shader pipeline/textures/uniforms and draw within the open pass. Compute callback owns its compute encoder. Raster host may supply matching depthTarget because Metal render pass validation may require it.
4. Copy preflight strictly requires one input and one output with matching dimensions and RGBA8 format, reflecting the actual RHI copyColor API contract; no fake HDR copy or conversion.
5. Added lightweight RHI format regression checks to existing `tests/render/prisma_graph.cpp` so no CMake target was added; these checks are test *source* only, not evidence of a passing executable.
6. Updated `docs/prisma.md` with the opt-in API and limitations.

## What did NOT change

- The production `Prisma::resolve` path is untouched and STILL runs the original fixed postprocess pipeline. This stage is not equivalent to converting the full postprocess chain to the graph.
- No PRISMA JSON, MCP operation, editor window, asset format, ShaderGraph source or SpectraFX file was changed.
- GPU barrier/texture transitions are backend-managed by `gfx::Device`; no universal explicit barrier API exists yet. The adapter does not provide synchronization/fences, screen backbuffer external bindings, transient use-before-written color validation or automatic shader pipeline selection.
- Temporary HDR/scene and postprocess resource lifetimes are not yet end-to-end integrated; actual render-quality and performance improvements must not be claimed.
- `reset()` destroys textures immediately; caller MUST wait for all in-flight GPU uses to complete first. The pool has no GPU retirement queue, and `PrismaRhiExecutor` is deliberately not automatically active.

## Public API illustration

```cpp
#include "engine/render/PrismaRhiExecutor.h"
using namespace aiverse::render;
PrismaRhiExecutor executor(device);
PrismaGraphPlan plan = graph.compile();
std::vector<gfx::TextureHandle> external(graph.resources().size());
auto status = executor.execute(graph, plan, external,
    [&](gfx::Device& dev, PrismaGraphId id,
        const PrismaGraphDispatchView<gfx::TextureHandle>& view) {
        // device.beginPass is already called; set viewport, pipeline,
        // textures and uniforms using view.resource(...), then draw.
        return executeRasterPostPass(dev, id, view);
    },
    [&](gfx::Device& dev, PrismaGraphId id,
        const PrismaGraphDispatchView<gfx::TextureHandle>& view) {
        return executeComputePostPass(dev, id, view);
    },
    optionalDepthTexture);
```

The example callbacks and optionalDepthTexture are host-defined placeholders,
not functions/objects already implemented in AIVERSE. Both callbacks return bool.
Call outside an open RHI pass. See `PrismaRhiExecutor.h` for complete rules.

## Verification

**Actually done:** inspected current `engine/gfx/Gfx.h`, `Renderer3D.h`, `Prisma.{h,cpp}`, Stage 11/12 graph/pool; committed RHI adapter, source-level tests and documentation using GitHub connector.

**NOT RUN:** compiler/CMake, CTest, headless tests, native macOS/Metal, GL/GLES, Android/WebGL, GPU screenshots, editor runtime, cost profiling or CI passes. No checkout with connected GitHub code is mounted for native build. Do not report any test as PASS.

## Precise next stage

1. Native compile and run: `cmake -S . -B build/prisma -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_HDR_TESTS=ON -DAIVERSE_BUILD_EDITOR=ON`; `cmake --build build/prisma --target aiverse_prisma_graph_tests aiverse_prisma_volume_tests aiverse_hdr_policy aiverse_editor -j4`; `ctest --test-dir build/prisma --output-on-failure -R 'prisma_graph|prisma_volume|hdr_policy'`; `python3 tools/skills/catalog.py check`. Fix any compile and static-contract errors first.
2. Add a dedicated fake `gfx::Device` (complete virtual interface) unit test of RHI adapter: pass balancing, graph-order execution, failure cleanup, denied format, invalid bindings, copyColor failure, per-view separation, history reset and resize.
3. Address GPU ownership and in-flight lifetime: explicit per-view executor ownership plus deferred retire callbacks/fences for GL and Metal. Ensure callback early failure does not leave pending encoders.
4. Integrate a *single* postprocess stage using opt-in feature flag, with true scene/composite resource bindings and shader/texture handles; do not replace `Prisma::resolve()` wholesale. Compare screenshot bytes/numeric linear HDR against existing path before extending.
5. Document every additional implementation stage under `docs/handoffs/prisma-stage-14.md` with exact SHA, commands actually run and limitations. Keep PR #26 draft until verified. Do not cherry-pick other feature branches.
