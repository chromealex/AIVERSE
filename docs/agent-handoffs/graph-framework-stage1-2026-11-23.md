# Shared Graph Framework — stage 1 — 2026-11-23

Branch: feature/aiv20261123_feature_gate (draft PR #120; no main or Studio migration branch write).
Implementation head: c43b2f856c193d513e7aa6e1c08331d96e99f008

Roadmap and separate graph-framework manifest were committed before code. Added shared TS graph projection types, domain registry, structural topology diagnostics with explicit cycle policy, bounded immutable draft history, reusable SVG GraphCanvas with pointer capture and inverse screen transform, semantic token styles, and keyboard selection/movement. Existing Shader Graph panel now uses shared canvas and the registered material/postfx/particle movement adapters; native bridge, compiler, persisted schema and runtime unchanged. Shader panel source blob matched main before editing. Existing layout helper still owns its bounded shader-specific geometry. Topology diagnostics and history utility are not yet integrated into native save or user-facing Undo/Redo.

Files: apps/studio-web/src/graphs/{graphFramework.ts,GraphCanvas.tsx,GraphCanvas.css,shaderAdapter.ts}, apps/studio-web/src/panels/ShaderGraphPanel.tsx, apps/studio-web/tests/graphFramework.test.mjs, apps/studio-web/package.json, docs/features/graph-framework.json, docs/devlog/systems.json, docs/agents/{graph-framework,feature-contract}.md, skills/workflow/aiverse-graph-authoring/SKILL.md, tools/skills/catalog.py, skills/catalog.json.

Tests: NOT RUN (connector-only editing). Unit tests added to npm test:graphs. Catalog entry derived from generator, not executed. No screenshots, native Studio QA, CI or Mac quick gate claim. No successful public devlog entry yet. No new native MCP operation. No complete graph editor claim.

Next: coordinate with Studio owner, run npm run test:graphs, typecheck, build and actual Mac UI capture; verify skill catalog and root runner integration. Add shared typed node catalog and port wiring, connect draft history and inspector, then migrate SpectraFX/MotionWeave/modeling via their existing native adapters. Keep domain validation, native revision and save semantics, and per-domain graph execution independent. No merge until existing gate passes.
