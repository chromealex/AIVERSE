# Shared Graph Framework — stage 9 — 2026-11-23

Branch: feature/aiv20261123_feature_gate, draft PR #120.
Implementation HEAD: 66ecac7c941ced02640c9eb38bfaa3f1e1b8b0ba

Added materialGraph and MaterialGraphPanel to existing Materials editor. Projects six actual texture channels, PBR factors, inheritance and embedded shader snapshot. Reuses existing PropertyField/GUID picker and material draft; no competing shader or texture evaluator. Native texture_patterns supplies catalog; texture_generate creates a PNG under content/textures; explicit assignment resolves actual Studio asset GUID and maps color/normal/mask to texture/normalMap/teamMask respectively. Material apply uses existing studio.materialPatch. Added opt-in overwrite=false to native texture_generate (legacy default true), existence check is not atomic across independent writers. Generation is an immediate file creation, not undone by material discard. Material reload no longer silently retargets a pending draft when its selected material disappears; concurrent same-material edits are not revision-protected.

Changed paths: apps/studio-web/src/graphs/{materialGraph.ts,MaterialGraphPanel.tsx,GraphCanvas.tsx}, apps/studio-web/src/panels/MaterialsPanel.tsx, apps/editor/core/MaterialOps.cpp, apps/studio-web/tests/nativeGraphAdapters.test.mjs, docs/features/graph-framework.json, docs/agents/graph-framework.md, skills/workflow/aiverse-graph-authoring/SKILL.md.

Verification: NOT RUN. GitHub writes confirmed. Added fixture for distinct material channels and bounded texture recipe. No TypeScript compile, native build, PNG generation/read-back, real Studio/GPU QA, CI or Mac merged-tree quick gate. No publication or main merge. Main policy PR #122 still needs reconciliation. Full procedural Texture Graph, texture recipes as assets, arbitrary channel processing, texture GPU preview, STRATA processing, generic MCP, debugger and profiler are pending.

Next: inspect existing native STRATA cook/analysis operation contracts; expose actual read-only build results, then implement common diagnostics and domain adapter metadata. Coordinate Building/Caverns/texture owners before integrating their live panels. Run graph tests, native material/texture regression and real Studio acceptance before claiming functionality verified.
