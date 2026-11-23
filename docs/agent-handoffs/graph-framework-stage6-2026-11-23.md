# Shared Graph Framework — stage 6 — 2026-11-23

Branch: feature/aiv20261123_feature_gate, draft PR #120.
Implementation HEAD: 0733bd3204104e5712aa55fc01736ae50fd2473d

Added ModelGraphPanel to the existing Modeling panel, retaining Native operations. Projects the real model source hierarchy and embedded animation pose/state graphs. Parent rewiring rejects cycles and preserves local transform values. Entry JSON edits retain stable IDs. Model source validation uses existing modeling_validate (added to Studio allowlist); commits use native modeling_edit replace with expectedRevision, lock and full model build. Layout remains local. MotionWeave state port wiring creates explicit unconditional transition drafts, allows cycles/self-transitions, preserves transition priority, supports initial state, remove transition and fade/after editing. Conditions remain JSON edits.

Changed paths: apps/studio-web/src/graphs/{nativeGraphAdapters.ts,NativeGraphPanel.tsx,ModelGraphPanel.tsx}, apps/studio-web/src/panels/ToolPanel.tsx, apps/studio-native/StudioTools.h, apps/studio-web/tests/nativeGraphAdapters.test.mjs, docs/features/graph-framework.json, docs/agents/graph-framework.md, skills/workflow/aiverse-graph-authoring/SKILL.md.

Verification: NOT RUN. GitHub writes confirmed; existing native operation schemas and transaction semantics inspected. Added regression fixtures for state ordering, loops, timing and hierarchy cycles, geometry preservation and revision response. No TypeScript compile, native build, Studio/GPU QA, CI or Mac merged-tree quick gate. No claim of geometry operand graph, visual condition editor, live preview, persisted layout, or complete modeling parity. No new native solver or format. No publication/main merge. Main agent rules landed separately in PR #122; draft requires reconciliation before integration.

Next: implement typed condition/parameter inspector and explicit model geometry dependency graph (boolean operand edges distinct from parent edges), then inspect actual building/cave/terrain generator schemas and available authoring APIs. Coordinate owners; run test:graphs, typecheck, build and native acceptance before merging. Do not invent graph APIs for generators without them.
