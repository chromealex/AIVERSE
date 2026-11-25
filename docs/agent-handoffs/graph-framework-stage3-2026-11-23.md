# Shared Graph Framework — stage 3 — 2026-11-23

Branch: feature/aiv20261123_feature_gate, draft PR #120.
Implementation HEAD: 3b1c2fc769f39c4c886a4a0d1265050201cefec4

Added reusable rectangle selection, modifier selection and rigid group movement; shared canvas handles marquee selection, group drag with a single commit and middle/Alt drag scroll panning. Shader panel has local Copy/Paste, group duplicate and dependency-based auto layout. Clipboard remaps selected internal links and leaves external inputs disconnected; same-domain only, max 128 nodes. Layout refuses cycles and layouts exceeding existing position bounds. Native validation/save and formats remain unchanged.

Changed paths: apps/studio-web/src/graphs/{graphSelection.ts,GraphCanvas.tsx}, apps/studio-web/src/panels/{ShaderGraphPanel.tsx,shaderGraphLayout.ts}, apps/studio-web/tests/{graphFramework,shaderGraphEdit}.test.mjs, docs/features/graph-framework.json, docs/agents/graph-framework.md, skills/workflow/aiverse-graph-authoring/SKILL.md.

Verification: NOT RUN. GitHub writes confirmed; unit tests added to existing test:graphs suite, no TypeScript build, native UI or Mac merged-tree checks. Clipboard is panel-local, not OS clipboard; wires remain at committed positions during drag preview. General port identities, context menu, live wire preview, graph templates, external revisions, graph debugger/profiler and other native adapters remain pending. No publication or merge.

Next: finalize generic port IDs and live edge geometry, contextual commands and clipboard shortcuts; verify npm run test:graphs/typecheck/build and real Studio interaction; then migrate SpectraFX and MotionWeave using native domain adapters. Coordinate with Studio owner.
