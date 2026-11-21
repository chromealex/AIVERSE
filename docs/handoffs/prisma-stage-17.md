# PRISMA Stage 17 — persistent compiled Bloom graph cache

Date: 2026-11-18. Branch: `feature/prisma-postfx`. PR: https://github.com/chromealex/AIVERSE/pull/26.
Previous: `docs/handoffs/prisma-stage-16.md`. Main was synced earlier via PR #31; no other branches imported in this stage.

## Implementation

- `e0721502d9a9b914e41fbc20a2790accbfd145cf`: added `engine/render/PrismaBloomGraph.h` (`PrismaBloomGraphCache`), a pure C++ graph topology cache keyed by source dimensions, format and down-mip dimensions. It builds the prefilter/down/upsample pass dependency graph once and keeps its compiled plan and per-pass logical bindings, plus a rebuild count for diagnostics.
- `47f8761a5cd54be0cd96a499542772e6939cff69`: added a cache member to `Prisma::Sized` in `engine/render/Prisma.h`, making it per-size/per-view instead of one global mutable plan. Existing render target and depth ownership stays unchanged.
- `f64ae31044b5026a88e47c59aca4f68c476f6f5e`: migrated the opt-in full Bloom chain in `engine/render/Prisma.cpp` to `Sized::bloomGraph.configure(...)`, with per-frame shader uniforms, input/output RHI handles and depth attachments rebound on every draw; no per-frame graph recompilation when source dimensions, HDR mode and mip topology are unchanged.
- `2bfd4e40b19edadc2d530dbd4e7d4001d0f31b55`: `docs/prisma.md` architecture reference updated.

## Public APIs and compatibility

`PrismaBloomGraphCache::configure(width,height,format,std::vector<PrismaBloomMip>)` returns false for invalid mip layouts. Immutable `graph()`, `plan()`, `operations()`, `sourceId()`, `outputId()`, `downIds()`, `upIds()` expose the cached structure. `clear()` invalidates it and `rebuilds()` provides diagnostic counter. Resource IDs are stable until reconfiguration.

The graph is still used **only** with `AIVERSE_PRISMA_GRAPH_BLOOM_CHAIN=1` (off by default), using existing external `Sized` GPU textures. No new JSON, MCP, editor, SpectraFX, genre or ECS changes. Existing SDR/HDR shader math, pipeline selection, and fallback retain prior behavior. Stage 12 GPU resource pool is **not yet wired into production**; this stage caches graph topology rather than images/targets.

## Real verification

**Actually performed:** fetched and inspected Stage 16 source and handoff; wrote the new header, header integration, runtime integration and design documentation through GitHub connector, then read back modified Bloom code.

**NOT RUN, as explicitly requested by user:** native C++ compiler, CMake, CTest, GPU/Metal/GL tests, screenshots, CI, performance counters, editor smoke and skill catalog. No claim of tested performance improvement. Changes should be validated before enabling the opt-in path by default.

## Limitations and next feature stage

The cache still constructs dynamic pass uniform and RHI binding vectors each frame. A cache rebuild occurs when dimensions/format/mip list changes. `PrismaBloomGraphCache` is not thread-safe: one render thread must own each `Prisma::Sized` instance. The RHI executor itself remains a short-lived object for current external-resource passes.

**Next stage (features, tests later):** provide a safe deferred GPU resource retirement contract for future graph-owned targets and per-view persistent executor ownership, without assuming `frameSerial()` signals GPU completion. Do not destroy an in-flight resource using only a CPU frame count. Integrate pool ownership only behind opt-in until real Metal/GL parity and GPU completion fences are available. Maintain a separate Stage 18 handoff on this branch.
