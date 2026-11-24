# Shared Graph Framework — stage 8 — 2026-11-23

Branch: feature/aiv20261123_feature_gate, draft PR #120.
Implementation HEAD: 2ec6f898abe770a6f614ac5692a12f1569ba2af8

Modeling: added draft modifier presets (documented native schemas), removal and model-local material assignment by existing ID. Native revision-checked modeling_edit and full build remain the authority. No automatic project-material/texture embedding. TerraWeave: connected a read-only recipe dependency diagram in the existing generator panel; preview/generate/forest mutation operations remain unchanged. Added preparatory read-only building and Caverns projections of actual recipe and snapshot formats, bounded presentation only. Not connected to live owner panels, not a node generator or alternate evaluator.

Owner investigation: Building Grammar feature/aiv20261123_building-grammar at 8c645f336fd761ef05ffe3e8a27999a2d77a0f2f (draft PR #114) contains StructureGraph and typed buildingInspect/buildingGenerate, style material slots and texture dependency persistence. Caverns feature/terraweave-caverns-core-20261123 at fad0b1f5b08cd47ef016afa383359577e9b117eb contains terraweave_caverns_paint mode=get returning versioned snapshot, MapDoc mutation ownership and STRATA/physics publication boundaries. Both were inspected, not merged or modified. The owner's Stage33 smoke report is not verification of this UI. TerraWeave existing terrain/forest API and current Modeling texture_patterns/texture_generate were inspected. Do not conflate model-local materials with building PBR library integration.

Changed paths: apps/studio-web/src/graphs/{nativeGraphAdapters.ts,ModelGraphPanel.tsx,generatorProjections.ts}, apps/studio-web/src/panels/generated/TerraweaveGeneratorPanel.tsx, apps/studio-web/tests/nativeGraphAdapters.test.mjs, docs/features/graph-framework.json, docs/agents/graph-framework.md, skills/workflow/aiverse-graph-authoring/SKILL.md.

Verification: NOT RUN. GitHub writes confirmed; fixtures cover modifier/material edits and bounded recipe projections. No TypeScript compile, npm tests, native build, actual Studio/GPU acceptance, CI or Mac merged-tree quick gate. No publication or main merge. Main policy from PR #122 still requires reconciliation.

Next: coordinate Building and Caverns owners to connect the projections in their existing panels after compatible integration; add native recipe editing only through their APIs. Extend material/texture authoring using native-supported schemas, expose real STRATA build results and then verify all graph domains. No invented executable graph or duplicated solver.
