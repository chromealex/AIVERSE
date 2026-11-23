# Shared Graph Framework — stage 5 — 2026-11-23

Branch: feature/aiv20261123_feature_gate, draft PR #120.
Implementation HEAD: ad1f34ac97cba67ee436b646a43762c0d8a365ec
Main agent rules landed separately at 71aa5c93afa5993c4e45ba39f1bf5454343389ea (PR #122); branch still needs integration/reconciliation of the manual rule contract with the automated gate before main merge. Do not overwrite the new main policy or merge this draft automatically.

Added nativeGraphAdapters and NativeGraphPanel in existing fx_graph and anim_graph ToolPanel entries. Kept Native operations tab. SpectraFX projects v1/v2 exec and data links, native catalog, add/delete/connect/disconnect/property edit through fx_graph_edit, validation and published save/read-back. The published native set lacks CAS; frontend preflight is non-atomic and cannot prevent all concurrent overwrites. MotionWeave projects pose DAG and state transitions separately; pose rewiring enforces earlier-node dependencies, selected existing entries editable as JSON, state loops preserved; validate then save with expected original document. Animation layout remains local, no native format or evaluator changes. No full state visual wiring, node creation, GPU preview, profiler/debugger or generic graph persistence claim.

Files: apps/studio-web/src/graphs/{nativeGraphAdapters.ts,NativeGraphPanel.tsx,GraphCanvas.tsx}, apps/studio-web/src/panels/ToolPanel.tsx, apps/studio-web/tests/nativeGraphAdapters.test.mjs, apps/studio-web/package.json, docs/features/graph-framework.json, docs/agents/graph-framework.md, skills/workflow/aiverse-graph-authoring/SKILL.md.

Verification: NOT RUN. Repository writes confirmed and native schemas/allowlist read; three new unit fixtures registered in test:graphs. No TypeScript compile, browser/native Studio, GPU or Mac merged-tree test evidence. Native operations remain the authority. No publication or main merge.

Next: reconcile main and draft policy changes, run graph unit tests/typecheck/build and native acceptance, review read-before-write limitation with SpectraFX owner; add native CAS if approved. Implement richer MotionWeave state/transition inspector and modeling adapter before expanding other graph families. Keep native semantics and use shared authoring components.
