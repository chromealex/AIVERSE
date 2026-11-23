# Shared Graph Framework — stage 26 — 2026-11-24

Branch: feature/aiv20261123_feature_gate; draft PR #120.
Implementation HEAD: 0a8bba8cccd8ae22e4445fc341a0d0db0401303b

Extended native TextureGraphGpu compiler with Vulkan GLSL 450 body using existing TextureResources storage buffer binding 12; shared validated numeric program emits Metal MSL and Vulkan GLSL. Terminal height-to-normal samples the upstream scalar pointwise closure at four wrapped neighbors in float precision. This is fused neighborhood sampling, not a generic multi-pass executor. 19 of 21 authored operation kinds have GPU source support; pattern and patternMask closures still reject/fallback. Native owner supports Metal checked dispatch/range readback and Vulkan checked dispatch/readBufferAsync, optional resolution 8..128 clamped to graph size, bounded to existing 64 KiB readback cap. CPU reference compared before GPU label. Studio requests 32/64/128 and verifies request/source/overrides/output/resolution identity. Existing CPU bake unchanged.

Changed paths: engine/render/graph/TextureGraphGpu.h, apps/editor/core/{TextureGraphGpuPreview.h,GraphOps.h}, apps/editor/EditorPlayOps.cpp, apps/studio-web/src/graphs/TextureGraphPanel.tsx, tests/vfx_graph_ops_test.cpp, docs/agents/graph-framework.md, skills/workflow/aiverse-graph-authoring/SKILL.md. Existing feature manifest already includes modified paths and MCP operation.

Verification: NOT RUN. GitHub writes confirmed; inspected Vulkan compute binding layout, checked dispatch and asynchronous readback APIs. Authored source-generation fixtures for normal, Vulkan binding and unsupported pattern. No C++/TS compilation, MSL/GLSL compile, real GPU execution/readback parity, CTest, CI or Mac merged-tree gate. No main merge. No GLES compute, full-resolution GPU bake, runtime GPU texture binding, persistent GPU cache or multi-pass claim. Coordinate texture/RHI owners and reconcile main rules PR #122.

Next: integrate GPU pattern source generation and explicit multi-pass resource plan with bounded allocations, or advance independent Building/Caverns graph authoring after owner branch review. Before publishing, run source/build interface audit and native acceptance on actual hardware.
