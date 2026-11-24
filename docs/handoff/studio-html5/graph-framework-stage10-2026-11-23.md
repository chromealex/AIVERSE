# Shared Graph Framework — stage 10 — 2026-11-23

Branch: feature/aiv20261123_feature_gate, draft PR #120.
Implementation HEAD: 0f16c7e47830a4967c361c7f48a99504c6dccbfb

Added StrataAnalysisPanel to Models and Modeling using existing read-only strata_capabilities and strata_model_analyze, added to Studio allowlist. Select a real content model GUID or existing exported project-relative path; no automatic export, no unsaved-model analysis. Strict response parser and common canvas projection display native eligible/skipped primitives, cluster nodes, meshlets, auxiliary CPU index bytes, source/primitive seam certification, rejected parents and shared boundary segments. Editor request round-trip timing is not GPU or isolated CPU execution timing. No new STRATA builder, cook, renderer changes or GPU parity claims. Suggested models/<name>.glb path is not proof of source revision.

Shared GraphDiagnostics adds opt-in advisory topology inspection, counts, bounded issue report and editor-only timing. Animation state loops allowed, pose/shader/model dependencies reject cycles, SpectraFX exec/data cycle checks are separate. Native validation remains authoritative. Runtime debugger/profiler not implemented.

Changed paths: apps/studio-web/src/graphs/{strataAnalysis.ts,StrataAnalysisPanel.tsx,graphDiagnostics.ts,GraphDiagnostics.tsx,ModelGraphPanel.tsx,NativeGraphPanel.tsx}, apps/studio-web/src/panels/{ToolPanel.tsx,ShaderGraphPanel.tsx}, apps/studio-native/StudioTools.h, apps/studio-web/tests/nativeGraphAdapters.test.mjs, docs/features/graph-framework.json, docs/agents/graph-framework.md, skills/workflow/aiverse-graph-authoring/SKILL.md.

Verification: NOT RUN. Existing native GeometryOps contract inspected, connector writes confirmed and fixtures authored for report parsing, split cycle checks and geometry projection. No actual model report generated in this environment, no TypeScript compile, npm tests, native build, real GPU/Studio acceptance, CI or Mac merged-tree quick gate. Main policy PR #122 still requires reconciliation. No publication/main merge.

Next: implement a shared adapter metadata/operation registry and generic Graph MCP discovery/read/validate routing with explicit domain contracts, preserve native authoring operations and avoid inventing mutations. Then runtime diagnostics only from actual owner telemetry, test all graph domains and verify integration. Coordinate owner branches before connecting Building/Caverns or claiming texture/STRATA runtime parity.
