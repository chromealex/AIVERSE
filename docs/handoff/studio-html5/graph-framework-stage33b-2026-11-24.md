# Shared Graph Framework — stage 33B — 2026-11-24

Branch: feature/aiv20261123_feature_gate; draft PR #120. Previous stage 33A handoff indexed at cbf7b0ecb31ab4dddc4f1abb9799f3f1dae0fcb8.
Planning: 246709e81f9b627b7a3359e3ebc8d7e4eaa35de5.
Implementation HEAD: 891e93d65bdd11c97bba26a2f9c7deb308e1b775.

Added dependency-only planDagLifetime with topological order/edge/target validation, reachable closure, consumer-edge refcounts, output pinning, per-step release list and logical peak live array count. Existing CPU evaluateMany now releases float arrays after last consumer's complete pixel evaluation; pinned outputs remain through ordered RGBA8 packing. No math/format change; pattern nodes have no inputs and normal reads neighbors during its own step. General GPU scheduler not implemented: reuse this plan only with GPU-specific fences/barriers. No actual memory/performance measurement.

Paths: engine/render/graph/TextureGraphLiveness.h; engine/render/graph/TextureGraph.h; tests/render/texture_graph_liveness.cpp; tests/vfx_graph_ops_test.cpp; CMakeLists.txt; docs/agents/graph-framework.md; skills/workflow/aiverse-graph-authoring/SKILL.md; docs/devlog/systems.json; docs/features/graph-framework.json.

Verification PASS: standalone Linux GCC 14.2.0, c++ -std=c++20 -Wall -Wextra -Werror -I. tests/render/texture_graph_liveness.cpp -o liveness_test && ./liveness_test. Output: PASS: shared dependencies, duplicate edges, pinned outputs, pruning, chain lifetime, invalid plans. Local git hash-object and fetched implementation blob IDs match header 313d7640cb2c1fccc65745d5569ccf1d80dc1995, fixture a5b79548023e381e3d582318d337f4ac4878c6c4. 32-node single-output chain logical peak two arrays; no measured allocator or GPU memory claim. Added native multi-output duplicate/normal/gradient parity fixture but NOT RUN. Full checkout build, CTest, Studio, MCP audit, actual Metal/Vulkan scratch reuse/fences, GPU profiling and Mac merged-tree gate NOT RUN. No main merge or release.

Next: Stage 34 general multi-pass GPU execution planning, preserve existing fused compiler fast path and scratch owner, use explicit lifetimes plus fence-aware retirement, add deterministic scheduling tests and actual GPU acceptance. Stage 33A native scratch GPU verification and Stage 31B.2 stable host identity/runtime apply remain open.
