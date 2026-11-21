# PRISMA Stage 18 — GPU fence retirement contract and persistent view executors

Date: 2026-11-18. Branch: `feature/prisma-postfx`, PR https://github.com/chromealex/AIVERSE/pull/26.
Prior handoff: `docs/handoffs/prisma-stage-17.md`. Main had been merged through PR #31 before this stage. No additional branch merges.

## Code commits

- `baa3696face543b9b6b81c8ef7e74f5e5ab08bcd` — new `engine/render/PrismaGpuRetirement.h`, generic `PrismaGpuRetirementQueue<Handle>` owning deferred handles and monotonic submission fence values.
- `f5afb2724f2c701c03ce0b2415eeab5b8fe981ee` — `engine/render/PrismaGraphResourcePool.h` adds `retireUnused(Retire)` and `retireAll(Retire)` transfer callbacks, leaving direct release APIs available for proven GPU-idle paths.
- `fb4a308d4f3da150601582a6de41a428fbcb6453` — `engine/render/PrismaRhiExecutor.h` owns a GPU retirement queue; exposes `retireUnused(submittedFence)`, `retireAll(submittedFence)`, `collectCompleted(completedFence)`, `retiredCount()`, and `resetAfterGpuIdle()`.
- `ffd330d0c2d75862fc7e70e76cb2b3a9ef406a80` — `engine/render/Prisma.h` adds optional persistent `PrismaRhiExecutor` to `Prisma::Sized`.
- `df5426ec8dc6ad1b23c60cec4ffde30a1a31a58e` — `engine/render/Prisma.cpp` lazily creates per-view RHI executor once, reused by the FXAA, Bloom Prefilter and Bloom Chain experimental paths, and releases on viewport eviction or PRISMA shutdown using existing safe-point assumptions.
- `c31f599361d4f363b0c6c0eb5dd715f18700b509` — `docs/prisma.md` Stage 18 design and integration limitations.

## Why / API semantics

A GPU resource may not be destroyed simply because the CPU is on a later frame. A resource is transferred from the pool to the retirement queue with the **last GPU submission** fence that can reference it. `collectCompleted` may be called only using a fence/event the GPU backend has definitively completed. Completion values are monotonic on the same GPU timeline. `gfx::Device::frameSerial()` is NOT a GPU-completed value and must not be passed as one.

The queue retains pending resources while in flight and can release them without blocking once the host reports completion. `drainAfterGpuIdle` and `resetAfterGpuIdle` require explicit caller guarantee that all submissions referencing resource handles have finished. An executor is now persistent for each viewport size; repeated opt-in render passes no longer construct short-lived executor instances and can retain graph pool resources between frames.

## Ownership and compatibility

No JSON, MCP, shader, gameplay ECS or SpectraFX changes. No new genre dependency. Legacy `postfx` and FXAA/Bloom flags unchanged. Current full Bloom graph uses `Sized::targets` as EXTERNAL resources, so Stage 18 queue does NOT yet retire those legacy textures. Legacy `Prisma::sized` LRU eviction and `shutdown` still immediately release textures under existing engine assumptions. The new queue alone does not fix those old release points.

**Missing backend contract:** `gfx::Device` has no portable GPU completed-fence API. Metal and GL must provide completion-backed callbacks/tokens before this can be wired automatically. Do not use a fixed number of CPU frames or `frameSerial()` as a substitute. Avoid calling the executor's reset path from a potentially in-flight context without guaranteed quiescence.

## Actual verification

**Performed:** fetched stage 16/17 code and handoff; wrote the queue, resource pool, RHI executor and per-view ownership changes; updated documentation through GitHub connector.

**NOT RUN:** native C++ build, CMake, CTest, all unit tests, fake graphics device smoke, GPU/Metal/GL screenshot parity, CI, benchmarks and editor. User explicitly prioritized features and asked to defer tests. Do not infer compilation success from GitHub writes.

## Next feature stage (tests remain deferred)

1. Implement portable GPU completion signaling/retirement in Gfx backend owners, using native command-buffer completed handlers for Metal and actual sync objects for GL/Vulkan, rather than CPU frame serial. Define error/device-loss behavior and thread ownership.
2. Switch opt-in full Bloom chain to internally allocated aliasable RenderGraph resources managed by persistent per-view executor, use Stage 12 pool, and bind graph output as bloomTex. Keep the legacy path by default, preserve existing HDR/SDR targets and per-mip depth attachments. Implement safe deferred retirement on resize and view eviction based on backend completion.
3. Add expected per-pass resource state and transitions where RHI requires explicit barriers. Keep UI/HUD outside PRISMA. Continue Stage 19+ handoffs separately with code SHA, API and NOT RUN status while tests are intentionally postponed.
