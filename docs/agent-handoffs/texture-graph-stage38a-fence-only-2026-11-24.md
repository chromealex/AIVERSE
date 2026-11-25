# Stage 38A — fence-only resident Texture Graph

Branch: `feature/aiv20261124_texture_graph_gpu_slots`

Base: `5f3556279a8df2e975d5d6e05ca32a8e3cd1940b` (roadmap/manifest)

Implementation: `c6d91981781cc8b8a8a44caa5eb1b4c59cc40ba4`

Stage 38A: explicit `gpuTextureFenceOnly:true` with `gpuTextureTransfer:true,materialized:true,bakeSet:true` generates only bindable material outputs, preserving their dependency closures and separate named textures. No per-output CPU pixel allocation, readBufferRangeAsync/readTextureAsync or CPU evaluator call on this route; the existing owner accepts a copied texture only after the matching nonzero frame fence and healthy matching device lease. `gpuTextureAcceptance=fence_only` is not numerical parity; default remains CPU-reference validation. Strict GPU mode, no fallback. Status reports zero readback bytes and `fileExportAvailable=false`; native PNG publish refuses, Studio disables it. Apply the session-only resident material or cancel before replacement. Plan-only uses the same filtered targets and zero diagnostic staging reservation; 64 MiB total logical cap, 32 MiB DAG cap remain. Cache retirement remains fence-gated, not a numerical certification. Integrated owner lifecycle, real GPU correctness, Studio build and native Mac gate NOT RUN.

## Changed paths

- engine/render/graph/TextureGraphGpuTextureBudget.h
- engine/render/graph/TextureGraphGpuTextureTransfer.h
- apps/editor/core/TextureGraphGpuPreview.h
- apps/editor/core/GraphOps.h
- apps/editor/EditorPlayOps.cpp
- apps/studio-web/src/graphs/TextureGraphPanel.tsx
- tests/render/texture_graph_gpu_texture_budget.cpp

## Actual evidence

Isolated test, two exact files reconstructed and verified with `git hash-object`: header `e8c88cd8f78274804823386e3d82a1f05fe3563a`, test `62790cf12a888841abef41ef9ec58d3741cfb418`. Tested on Linux, not a full checkout.

```
g++ -std=c++20 -Wall -Wextra -Werror -I. tests/render/texture_graph_gpu_texture_budget.cpp -o budget && ./budget
clang++ -std=c++20 -Wall -Wextra -Werror -I. tests/render/texture_graph_gpu_texture_budget.cpp -o budget-clang && ./budget-clang
```

GCC 14.2.0 and Clang 17.0.0: both exit 0, `PASS: texture request cap, all candidates, validation reserve, overflow and exact boundary`. Includes explicit zero staging, validated-mode rejection of zero, contradictory mode rejection and exact cap. No full checkout evidence report, so feature gate tests remain NOT RUN. Integrated owner/Studio/MCP, real GPU parity/mips, captures and merged-tree Mac quick gate NOT RUN. No speed claims.

## Next

Add fake-device owner fixtures for no readback calls, per-output fence gating, timeout/cancel/device loss and strict export refusal, then exercise actual Metal/Vulkan resident generation and live snapshot publication. Extract the host-independent runtime request API only after preserving the single owner and lifecycle. 3.1.0 note candidate: opt-in GPU material-only generation without CPU readback (experimental; no persisted format changes). Do not merge until integration gates pass.
