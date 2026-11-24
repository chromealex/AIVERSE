# Stage 38C — bounded incremental GPU nodes

Branch: `feature/aiv20261124_texture_graph_gpu_slots`

Plan: `d8c9bc82e447951885eeed5e686f3c385b372d1e`
Implementation: `e059cbb8288017795f5d3be12df4ab522e4900dc`

Stage 38C: `incremental:true` is opt-in on unfused materialized DAG requests (native `ResidentMaterialOptions::incremental`, Studio/MCP start and plan). The existing lowerer reserves one dedicated slot per reachable node instead of aliasing intermediate values, within the unchanged logical 32 MiB DAG cap including packed result (resident total cap 64 MiB). One retired exact-layout buffer set retains contents and up to 1 MiB of exact selected-backend kernel source keys plus ordered input slots; only matching completed GPU request, device lease generation, scope and layout permit reuse. Changed sources/edges propagate dirtiness topologically; unchanged nodes skip dispatch. Cache miss/oversized keys rerun all nodes, oversized layout refuses admission; no CPU fallback, no fusion combination yet. Cancellation/failure/project/device change invalidates or retires data under the existing owner. Pack/copy and selected validation still run for all requested outputs, so this is incremental node execution, not partial output publication or guaranteed speedup. `incrementalReusedNodes` counts skipped dispatches; `materializedNodePasses` counts encoded node passes. Plan-only uses dedicated slots and reports planned memory, not cache hit. Isolated exact-blob C++20 test PASS; real GPU fixture extended, integrated build and hardware NOT RUN.

## Changed paths

- engine/render/graph/TextureGraphIncremental.h
- engine/render/graph/TextureGraphDagLowering.h
- engine/render/graph/TextureGraphGpu.h
- engine/render/graph/TextureGraphGpuOwner.h
- apps/editor/core/GraphOps.h
- apps/editor/EditorPlayOps.cpp
- apps/studio-web/src/graphs/TextureGraphPanel.tsx
- tests/render/texture_graph_dag_lowering.cpp
- tests/render/texture_graph_gpu_acceptance.h

## Actual evidence

Reconstructed five files from the exact commit and verified with git hash-object:
- TextureGraphDagLowering.h: 75e9891eeffbd33aea5f321835581a40fd71e2cc
- TextureGraphIncremental.h: 757829fcae5eb7fd35a78fd52db4411ba79c6647
- TextureGraphLiveness.h: 313d7640cb2c1fccc65745d5569ccf1d80dc1995
- TextureGraphPassSchedule.h: c298267e63b69c59f805f9e7ecfa49dc45e33097
- texture_graph_dag_lowering.cpp: 92da39da5b9b6d2d9042961d92c3d907b287b98c

```
g++ -std=c++20 -Wall -Wextra -Werror -I. tests/render/texture_graph_dag_lowering.cpp -o dag-gcc && ./dag-gcc
clang++ -std=c++20 -Wall -Wextra -Werror -I. tests/render/texture_graph_dag_lowering.cpp -o dag-clang && ./dag-clang
```

GCC 14.2.0 (Debian 14.2.0-19), Clang 17.0.0: both exit 0. Existing typed-slot regression, dedicated slot budget, all-clean, cold cache, changed node and descendants, reordered input edges, aliased schedule refusal all covered. Output: PASS: typed capacity, multi-input order, duplicate ports/targets, output pinning, pruning, budget, binding limit, slot reuse. Isolated Linux test only, not full clean checkout evidence report. Integrated owner lifecycle, actual GPU values and barrier behavior, native build, Studio, MCP and Mac quick gate NOT RUN. Real-RHI fixture expects 3/0 dispatched/reused, 0/3 unchanged, 2/1 after a parameter change; these are assertions, not measured results.

## Next

Run integrated native GPU fixture on Metal/Vulkan, including epoch changes and cache invalidation. Stage 38D: bounded asynchronous request queue on top of the same native owner; do not introduce another device resource owner. 3.1.0 release candidate: optional incremental materialized node evaluation, experimental. Do not merge until the actual integration gates pass.
