# PRISMA Stage 19 — graph-owned Bloom mip textures

**Date:** 2026-11-18. **Branch:** `feature/prisma-postfx`. **PR:** https://github.com/chromealex/AIVERSE/pull/26. **Previous stage:** `docs/handoffs/prisma-stage-18.md`.
Main integration remained the earlier PR #31 merge; no new branch merges or changes to `main` during this stage.

## Code SHAs and changed files

- `69f07cb004fe93336dbcf7586f9e6a4afbf0b9e1` — `engine/render/PrismaRhiExecutor.h`: `PrismaRhiExecution::resources` returns the RHI handle corresponding to each graph resource ID, with graph-owned targets still owned by the persistent executor, not the result object.
- `2be829b34af2a6106398cb17a421a4dd719570cb` — `engine/render/PrismaBloomGraph.h`: `configure(...,graphOwnedTargets=false)` controls whether Bloom down/up output resources are external or internally allocated. Flag is part of the cached topology key.
- `6dd53a5eea20b3f2d70197e631793ef20e82353c` — `engine/render/Prisma.cpp`: if full Bloom graph and Bloom pool opt-in are both enabled, declare mips as graph-owned and let the persistent per-viewport RHI executor allocate/reuse them. Per-pass depth attachments remain in `Sized::depths`. Composite receives the pooled output from `run.resources[cache.outputId()]`. On pre-dispatch failure, allocate legacy mip targets and use the old fallback. On any partial GPU submission failure, do not replay passes.
- `76543733636f208ac2b9dfbb0a6cfc7773aaa5bb` — `docs/prisma.md`: mode, ownership boundaries and deferred-testing status.

## Opt-in mode and preserved functionality

Set BOTH `AIVERSE_PRISMA_GRAPH_BLOOM_CHAIN=1` and `AIVERSE_PRISMA_GRAPH_BLOOM_POOL=1` to enable graph-owned render targets. The Stage 16 full-chain flag alone still renders to external per-view cached targets; legacy behavior remains default when flags are absent.

The graph planner uses internal target lifetime/slot aliasing to reduce redundant render targets where compatible. All shader variants, HDR/SDR quality settings, bloom normalization and downstream composite behavior are intended to remain equivalent. No changes to game/map JSON, MCP names, editor panels, SpectraFX or deterministic gameplay/ECS.

The RHI executor persists per `Prisma::Sized` since Stage 18; graph pool allocations persist across frames. There is no automatic pool trim on resize or a quality change because the GPU-completion event API is not yet wired across Metal and GL; old allocations can remain until an explicit safe reset/retirement or view shutdown. No memory/performance benefit is asserted without GPU benchmarks.

## Verification and risk

**Actually performed:** inspected relevant code and design, committed new opt-in graph resource bindings, cache mode, runtime path, and docs through connected GitHub operations. No tests were added in this stage at user's request to prioritize functions.

**NOT RUN (by user request):** native build, CTest, shader validation on Metal/GL/GLES, RHI fake device tests, GUI, screenshot parity, profiler, CI. Do not assert that this code compiles, executes on a GPU, or produces identical output.

Risks: graph-owned textures must never be destroyed before all GPU submissions that reference them complete. `PrismaRhiExecution::resources` holds borrowed handles; do not retain them beyond executor/view lifetime. Per-view eviction currently follows legacy immediate-release semantics, not a completed-fence retirement path. RHI graph passes depend on the backend's sequential pass transitions; there is no portable explicit barrier contract or implemented GPU completion token. The experimental flags must stay off by default.

## Next feature work (tests remain deferred)

1. Implement an explicit GPU-completed event/fence interface across Metal and GL/Vulkan backends, then connect it to Stage 18 `collectCompleted` and safe per-view retirement.
2. Use graph pool lifetime accounting to bound stale allocations after quality-tier or viewport topology changes and prevent indefinite memory retention, without using a CPU frame index as a fence.
3. Continue migrating other multi-pass postprocessing to cached graph topologies (Tilt Shift and Sun Shafts are natural next candidates), keeping each transition opt-in until native validation.
4. Add full Stage 20 handoff after code is committed; record actual NOT RUN testing status rather than falsely claiming PASS.
