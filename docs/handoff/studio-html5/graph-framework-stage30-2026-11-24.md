# Shared Graph Framework — stage 30 — 2026-11-24

Branch: feature/aiv20261123_feature_gate; draft PR #120.
Planning commit: 017124b4dbe5a70df3e681070b9ee6a32876d6f2.
Implementation HEAD: eb0a079ef7b81e2a03cfa76d199b5a5225cc14da.

Native texture_graph_gpu_preview adds request-scoped cancel, optional timeoutMs (1000–300000, default 60000), owner-tick monotonic request deadline and draining status. Cancellation invalidates unpublished pixels, set outputs, graph snapshot and program; no publication of cancelled/timed-out work. The existing callback/no-read fence retirement path retains submitted GPU handles and blocks new work until safe retirement. Timeout is terminal even if requireGpu is false; no forced reset or blocking wait. Studio cancel bypasses draft JSON parsing and uses exact request identity; existing read and publish guards remain. No persistence format change, new owner, genre split or direct runtime binding.

Changed paths: apps/editor/core/TextureGraphGpuPreview.h; apps/editor/core/GraphOps.h; apps/editor/EditorPlayOps.cpp; apps/studio-web/src/graphs/TextureGraphPanel.tsx; tests/vfx_graph_ops_test.cpp; docs/agents/graph-framework.md; skills/workflow/aiverse-graph-authoring/SKILL.md; docs/devlog/systems.json; docs/features/graph-framework.json.

Verification: GitHub tree/commit/ref updates accepted. Authored headless cancellation refusal fixture. NOT RUN: native compile, Studio typecheck/build, MCP audit, CTest, real GPU cancellation/timeout or captures, feature gate and Mac merged-tree quick suite. No Mac/KINGSTON checkout available through this connector. Main comparison at starting HEAD: diverged, branch ahead 497 and behind 1. No main sync/merge attempted because merged-tree integration cannot be verified here; reconcile main before integration. No release or publishing claim.

Limitations: deadline checked on owner tick, not an interrupt of synchronous driver calls or CPU parity evaluation. Healthy backend missing a callback can remain draining until teardown; cannot start another job during drain. Studio read still requires a matching draft and valid JSON; cancel itself does not. No runtime ownership/texture binding or expanded cache. 

Next: add deterministic native lifecycle fixtures (queued cancellation, stale ID, completed unpublished cancellation, timeout/readback drain and project switch); make Studio polling independent of draft validity while gating preview/publish by provenance; coordinate with render resource owner for fence-safe runtime GPU texture binding. Test Metal/Vulkan on actual hardware before claiming ready.
