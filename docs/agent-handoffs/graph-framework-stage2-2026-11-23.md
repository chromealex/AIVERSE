# Shared Graph Framework — stage 2 — 2026-11-23

Branch: feature/aiv20261123_feature_gate, draft PR #120.
Implementation HEAD: 3b9b85e02371acdf981d0f6d590e2f31db4e725e

Added native shader catalog search/domain filtering, click-to-connect typed shader input/output ports, cycle prevention, disconnection, finite float4 editing for constant/parameter nodes, node duplication, local Undo/Redo, zoom 50–200%, scrolling, keyboard port interaction and one history commit per completed pointer drag. Existing shader compiler/save bridge remains authoritative. No new native API, runtime solver, persisted format or MCP command. Workspace changes refresh catalog without intentionally discarding the loaded draft; external concurrent asset edits are not revision-protected by this stage.

Changed paths: apps/studio-web/src/graphs/{GraphCanvas.tsx,GraphCanvas.css,graphFramework.ts}, apps/studio-web/src/panels/{ShaderGraphPanel.tsx,shaderGraphLayout.ts}, apps/studio-web/tests/shaderGraphEdit.test.mjs, docs/features/graph-framework.json, docs/agents/graph-framework.md. Tests added for domain search, input slots, cycles, value validation and duplication; existing npm test:graphs covers them.

Verification: NOT RUN. GitHub edits confirmed, no TypeScript compilation, browser or native UI capture, CI or Mac quick gate. Local draft history is not project-wide Undo. Graph canvas currently supports one output per node in the shader workflow; shared multi-port semantic adaptation, general inspector, multiple selection, clipboard, drag-to-pan, output assignment, external-writer conflict handling and remaining adapters are pending. No publication or main merge.

Next: implement multiselect/clipboard/panning and generic port identities; run npm test:graphs, typecheck, build and native visual checks before migration to SpectraFX and MotionWeave. Coordinate with Studio owner; retain native domain semantics.
