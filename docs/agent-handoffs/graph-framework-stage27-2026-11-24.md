# Shared Graph Framework — stage 27 — 2026-11-24

Branch: feature/aiv20261123_feature_gate; draft PR #120.
Implementation HEAD: 29208468862713736d9cccf8edc5cdce29a5827b

Added GPU shader mirrors for all 14 default TextureGen patterns, using canonical native default palettes and explicit RGBA8 intermediate quantization to match CPU graph semantics. All 21 authored graph op kinds have source generation. Terminal normal plan now uses two actual compute dispatches: scalar closure writes float bits into scratch; normal pass reads wrapped neighbors through slot 1/binding 13 and writes packed RGBA8. Pointwise plan stays one pass. Native owner retains both pipelines and buffers until existing readback/fence/idle proof, abandons on device loss. Reports encoded pass count and planned scratch bytes (max 64 KiB scratch at 128x128). CPU reference acceptance remains mandatory before GPU result label. No general arbitrary multi-pass scheduler, cross-request resource pool, full-resolution GPU bake, runtime GPU texture binding or GPU timing evidence.

Changed paths: engine/render/{TextureGen.h,TextureGen.cpp,graph/TextureGraphGpu.h,graph/TextureGraphGpuPatterns.h}, apps/editor/core/TextureGraphGpuPreview.h, apps/studio-web/src/graphs/TextureGraphPanel.tsx, tests/vfx_graph_ops_test.cpp, docs/features/graph-framework.json, docs/agents/graph-framework.md, skills/workflow/aiverse-graph-authoring/SKILL.md.

Verification: NOT RUN. GitHub source writes confirmed; existing TextureGen formulas, Vulkan/Metal dispatch ordering and readback contracts inspected. Added source-generation fixtures for all 14 pattern kinds, two-pass scratch budget and binding ABI. No C++/TS or MSL/GLSL compilation, actual GPU execution, CPU parity measurement, CTest, CI or Mac merged-tree gate. No main merge. Coordinate texture/RHI owners and reconcile main rules PR #122. Pattern code mirrors CPU formulas and must be checked on hardware before production use.

Next: consider tiled full-resolution GPU readback through actual RHI supported ranges, or integrate Building/Caverns graph domains. Add per-device resource cache only with proven lifetime ownership and hard budget. Complete actual build and QA before publishing.
