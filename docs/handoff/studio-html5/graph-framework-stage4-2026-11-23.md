# Shared Graph Framework — stage 4 — 2026-11-23

Branch: feature/aiv20261123_feature_gate, draft PR #120.
Implementation HEAD: 58ccf55008353c8c0342e9e243a1727dc28071f1

GraphCanvas now uses domain-neutral GraphView, named typed PortRef identities and independently placed multiple input/output ports. graphGeometry derives live wire endpoints from temporary drag positions and renders an unfinished connection preview. Shader adapter projects catalog-defined arity and retains native single-output semantics. Added canvas-scoped contextual Actions strip and Ctrl/Cmd C/V/D/A/Z/Shift-Z shortcuts, Escape cancellation, a native-catalog-validated tint node-group template, and reassignment of existing shader output keys. No new native API, evaluator or persistent schema; native compiler/save remains authoritative.

Changed paths: apps/studio-web/src/graphs/{graphGeometry.ts,GraphCanvas.tsx,GraphCanvas.css,shaderAdapter.ts}, apps/studio-web/src/panels/{ShaderGraphPanel.tsx,shaderGraphLayout.ts}, apps/studio-web/tests/{graphFramework,shaderGraphEdit}.test.mjs, docs/features/graph-framework.json, docs/agents/graph-framework.md, skills/workflow/aiverse-graph-authoring/SKILL.md.

Verification: NOT RUN. GitHub writes confirmed; tests authored for multiple output geometry, preview movement, template applicability and output assignments. No TypeScript compilation, npm tests, native UI/GPU capture, CI, or Mac merged-tree verification. Context actions are a toolbar strip, not a floating menu. Clipboard remains panel-local. Shader domain has one native output per node despite general canvas multi-output capability. Native revision conflict protection, full domain templates and graph debugger/profiler remain pending.

Next: integrate existing SpectraFX and MotionWeave authoring adapters against their actual schemas and existing typed Workspace/Studio operations; preserve their semantics, especially animation loops and native validation. Coordinate with Studio owner; run npm graph tests, typecheck/build and actual native acceptance before merging. No publication or main merge.
