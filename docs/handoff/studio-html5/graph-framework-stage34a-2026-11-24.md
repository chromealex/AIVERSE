# Shared Graph Framework — stage 34A — 2026-11-24

Branch: feature/aiv20261123_feature_gate; draft PR #120.
Previous: 31992132bef4b72e0d3c15dc7a7272bd02fe05bd.
Planning: 67d9c4aba192a459fb5a3ca32c3cd1f017d75f68.
Implementation: ddb04bae5a5c74a80b5346e45a051141096397ef.

Added generic bounded ordered GPU pass policy: 32 passes/slots, explicit resource initialization and external-ready slots, no in-place read/write, aggregate slot byte cap, written result validation and fail-fast partial encode accounting. Existing native fused, two-pass and shared scalar scratch dispatch paths now run through it with an 8 MiB logical buffer budget; accepted request-local scratch can skip its producer. Existing backend barriers, frame submission fences, readback and CPU parity remain authoritative. No added GPU owner or format change. Status and Studio show plannedPasses/plannedSlotBytes. This is an integrated scheduling foundation, not arbitrary DAG-to-GPU shader lowering or independent GPU fence validation. Existing adapter binds at most one input per pass; generic policy allows multiple inputs but those are not yet lowered or bound by this adapter.

Paths: engine/render/graph/TextureGraphPassSchedule.h; tests/render/texture_graph_pass_schedule.cpp; apps/editor/core/TextureGraphGpuPreview.h; apps/studio-web/src/graphs/TextureGraphPanel.tsx; CMakeLists.txt; docs/agents/graph-framework.md; skills/workflow/aiverse-graph-authoring/SKILL.md; docs/devlog/systems.json; docs/features/graph-framework.json.

Verification PASS: Linux GCC 14.2.0 isolated exact-source test: c++ -std=c++20 -Wall -Wextra -Werror -I. tests/render/texture_graph_pass_schedule.cpp -o schedule_test && ./schedule_test. Output: PASS: ordered passes, slot reuse, external readiness, bounds, dependency rejection, partial encode. git hash-object matched implementation blobs c298267e63b69c59f805f9e7ecfa49dc45e33097 (header) and 64e5cba4fe4843ef6bcbd666840ceac4c99d5a78 (fixture), verified by GitHub readback. Full native build, CTest, Studio typecheck/build, MCP audit, Metal/Vulkan barriers/readback, real GPU memory/timing and Mac merged-tree gate NOT RUN. No main merge/release.

Next: 34B lower bounded shared DAG subgraphs to typed materialized buffers and multiple-input kernels, with explicit resource slot assignment from liveness, backend capability/binding validation and hardware acceptance. Keep fused fast path. Stage 31B.2 actual runtime host wiring and direct GPU texture transfer remain open.
