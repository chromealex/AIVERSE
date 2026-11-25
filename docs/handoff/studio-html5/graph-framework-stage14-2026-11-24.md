# Shared Graph Framework — stage 14 — 2026-11-24

Branch: feature/aiv20261123_feature_gate; draft PR #120.
Implementation HEAD: d21a4569ca20ea788d64b542aa985bea747bd373

Added graph_runtime_capabilities and graph_runtime_probe. Isolated native CPU previews only, bounded to 60 fixed 1/60-second steps. MotionWeave uses existing loadAnimationModel, AnimationGraph::compile and AnimationGraphInstance update/evaluate; reports observed state changes, state time, active compiled pose program, bounded notify samples, health/error and measured CPU timings. No same-state restart tracking or per-node execution instrumentation. Optional declared scalar overrides. SpectraFX uses native compileGraph and a separate low-quality headless ParticleSystem, reports real native particle/instance/dropped/culled/emitting counts and update timing when run. No external effect/file loader, world environment or GPU. Shader/Modeling unavailable as runtime probes; STRATA remains existing CPU analysis. Studio NativeGraphPanel has explicit probe and frame selector, stale-result detection. No project writes or live gameplay attachment.

Changed paths: apps/editor/core/{GraphRuntimeProbe.h,GraphOps.h}, apps/studio-native/StudioTools.h, apps/studio-web/src/graphs/{GraphRuntimeProbePanel.tsx,NativeGraphPanel.tsx}, tools/mcp/audit_registry.py, tests/vfx_graph_ops_test.cpp, docs/features/graph-framework.json, docs/agents/graph-framework.md, skills/workflow/aiverse-graph-authoring/SKILL.md.

Verification: NOT RUN. Native owner APIs inspected and GitHub writes confirmed. Added headless FX probe regression with bounded steps, actual particle expectation, and explicit non-GPU/non-live scope. No C++ or TS build, CTest, real MCP invocation, MotionWeave probe execution, Studio/GPU QA, CI or Mac merged-tree gate. No measured results from this development environment. No publication/main merge. Main policy PR #122 still needs reconciliation.

Next: connect live owner telemetry via explicit lifetime-safe snapshots from native scene/game hosts; inspect actual instance identity and ownership before adding attach, pause, breakpoints or counters. Expose actual active-state visualization and bounded frame history, never infer per-node timing from aggregate CPU data. Verify both probes and full graph feature in a real checkout.
