# Texture Graph Stage 38BD1 — buffer-backed image source

Branch: `feature/aiv20261124_texture_graph_gpu_slots`. Implementation SHA: `0e110ad01756fabb93cb2f3e154116f072100b51`. Previous handoff: `8acf692bae28b53ab3fd8bd2c391b05bcac092bb`. No main merge.

## Implementation
- `engine/render/graph/TextureGraph.h`: embedded RGB sources now permit up to 1024 pixels per dimension and 32768 total pixels, preserving existing 256 KiB source cap and nearest sampling semantics.
- `engine/render/graph/TextureGraphKernelSource.h`, `TextureGraphGpu.h`: explicitly opt-in uint storage-buffer input for image-only kernels, fixed-size kernel source independent of pixel content; source byte reservation deducted from existing materialized DAG budget (including unused image nodes conservatively). The generic typed DAG still rejects packed input unless explicitly enabled for image source.
- `engine/render/graph/TextureGraphGpuOwner.h`: dirty image nodes upload packed RGB words via immutable RHI storage buffers; retain request-local source handles to completed fence, release on retirement or failure. Cache keys include exact image parameter bytes; unchanged incremental image slots may reuse computed contents without upload. Retained bytes include source allocations. Reviewed Metal and Vulkan createBuffer implementations for synchronous CPU input copying, not hardware execution.
- `apps/editor/core/GraphOps.h`: plan reports imageSourceBytes.
- `apps/studio-web/src/graphs/TextureGraphPanel.tsx`: bounded image import, explicit downsampling status, stale asynchronous result rejection and serialized document size check.
- `tests/render/texture_graph_gpu_acceptance.h`, `texture_graph_kernel_source.cpp`: authored 64x64 strict GPU parity, shader ABI and budget assertions.
- Existing feature manifest updated; roadmap remains in progress.

## Verification
GitHub accepted commits 9adaf797 and 0e110ad0. Reviewed changed source and RHI upload implementations. No compile, TypeScript build, CPU tests, native Metal/Vulkan acceptance or live Studio/MCP checks executed. Main synchronization and merged-tree Mac gate NOT RUN.

## Remaining
This is the GPU source-buffer foundation, not full-resolution external asset import. Embedded source total pixel cap remains 32768, with a 256 KiB source document cap; oversized imports downsample. Next 38BD2: project-scoped original image assets resolved as immutable binary snapshots, not huge JSON or shader strings; separate source memory admission and deterministic offline/native behavior. Preserve cancellation/fence ownership and exact cache invalidation; do not silently increase graph JSON/queue caps. HDRI/IBL and custom compute remain open.
