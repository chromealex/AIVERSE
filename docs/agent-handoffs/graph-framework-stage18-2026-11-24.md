# Shared Graph Framework — stage 18 — 2026-11-24

Branch: feature/aiv20261123_feature_gate; draft PR #120.
Implementation HEAD: 2f16b639083c7aae509a8fb9bf95927f7fe80cf9

Added GraphWatch.h opt-in identity-pinned subscription with sequence deduplication, bounded 120 sample history and stop on replacement/reset. Native SpectraFX ParticleSystem now exposes per-instance CPU counters, emitting/time and monotonic owner-local instance/update serials. Existing native Editor captures one selected common MotionWeave source or actual CPU FX preview instance during native play after window drawing, only when game viewport rendered. No hidden frame capture; no authoritative ECS mutation. FX serial is lifetime identity, not asset hash. graph_live_watch start/status/stop routes to optional native editor owner, returns unavailable elsewhere. Studio displays timeline via explicit refresh, start/stop. This is not generic FX ECS scheduling, live HTML5 host transport, per-node execution tracing or GPU profiling.

Changed paths: engine/render/graph/GraphWatch.h, engine/fx/{FxInternal.h,ParticleSystem.h,ParticleSystem.cpp}, apps/editor/{Editor.h,Editor.cpp,EditorPlayOps.cpp}, apps/editor/core/GraphOps.h, apps/studio-native/StudioTools.h, apps/studio-web/src/graphs/GraphLiveSnapshotPanel.tsx, tools/mcp/audit_registry.py, tests/vfx_graph_ops_test.cpp, docs/features/graph-framework.json, docs/agents/graph-framework.md, skills/workflow/aiverse-graph-authoring/SKILL.md.

Verification: NOT RUN. GitHub writes confirmed; native owner and handle generation paths inspected. Added regression for duplicate samples, pinned identity replacement, clear and unavailable headless owner. No native/TS build, CTest, actual editor capture, per-instance runtime verification, GPU QA, CI or Mac merged-tree gate. No publication/main merge. Main policy PR #122 needs reconciliation; coordinate animation/FX owners.

Next: review true committed owner publication and generic native Studio transport; add opt-in bounded per-node timing and events at actual execution sites with separate instrumentation overhead and no second evaluator. Verify all graph domains in actual checkout before merge.
