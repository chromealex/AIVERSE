# Shared Graph Framework — stage 25 — 2026-11-24

Branch: feature/aiv20261123_feature_gate; draft PR #120.
Implementation HEAD: 5da1a83f83ad265640a84a0320f6866b7ec50657

Added native Metal pointwise texture DAG compute compiler and one asynchronous native editor preview job. Supports 18/21 current operations; normal, pattern and patternMask closures fall back or fail if requireGpu. Native readback of 8..32 RGBA8 pixels, validated against CPU reference within one unit. Reports Metal only on completed accepted readback, not dispatch. Storage/pipeline resources remain held until callback/fence or existing shutdown idle proof; lost device handles abandoned. No synchronous preview GPU wait, full-resolution bake, runtime texture binding, Vulkan/GLES implementation, performance claims, or separate HTML5 owner transport. Studio queue/read controls validate request identity and graph/override source. CPU bake unchanged.

Changed paths: engine/render/graph/TextureGraphGpu.h, apps/editor/core/{TextureGraphGpuPreview.h,GraphOps.h}, apps/editor/{Editor.h,Editor.cpp,EditorPlayOps.cpp}, apps/studio-native/StudioTools.h, apps/studio-web/src/graphs/TextureGraphPanel.tsx, tools/mcp/audit_registry.py, tests/vfx_graph_ops_test.cpp, docs/features/graph-framework.json, docs/agents/graph-framework.md, skills/workflow/aiverse-graph-authoring/SKILL.md.

Verification: NOT RUN. GitHub writes confirmed; actual RHI Metal compute/readback API inspected. Authored fixtures for generated kernel source, unsupported normal and headless availability. No C++/TS compiler, MSL compile, GPU execution, readback/CPU parity run, CTest, CI or Mac merged-tree gate. No main merge. Coordinate texture/RHI owners and reconcile main rules PR #122.

Next: implement multi-pass GPU neighborhood nodes and bounded full-resolution output ownership only with real RHI acceptance; alternatively connect Building/Caverns graph authoring to their existing owner branches. Review source/compile issues in current branch before final integration.
