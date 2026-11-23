# Shared Graph Framework — stage 7 — 2026-11-23

Branch: feature/aiv20261123_feature_gate, draft PR #120.
Implementation HEAD: 34f71eebe47a481dd402e5c7af2006afe03826d5

Added separate geometry dependency view in Modeling: actual native CSG a/b operand ports, preceding-mesh constraint, union/subtract/intersect choice and ordered modifier display/reordering. Transform hierarchy remains distinct. Entry JSON and full native revision-checked modeling_edit transaction remain unchanged. No new mesh generator, native file format, geometry evaluator or runtime. Added shared AnimationConditions and AnimationParameters for standalone MotionWeave and embedded model animation: typed existing finite scalar/boolean defaults, AND condition operators/thresholds, add/remove with 32-condition limit; vector defaults remain JSON. Native compiler remains authoritative.

Changed paths: apps/studio-web/src/graphs/{nativeGraphAdapters.ts,AnimationConditions.tsx,NativeGraphPanel.tsx,ModelGraphPanel.tsx}, apps/studio-web/tests/nativeGraphAdapters.test.mjs, docs/features/graph-framework.json, docs/agents/graph-framework.md, skills/workflow/aiverse-graph-authoring/SKILL.md.

Generator investigation: inspected engine/systems/BuildingGen.h (generateBuilding), apps/editor/core/GenOps.cpp (gen_presets/map_generate), docs/terrain.md; probed docs/cave-generator.md and docs/building-generator.md returned 404 on this branch. This is not a determination that the systems are absent in current owner branches. Do not invent graph APIs.

Verification: NOT RUN. Source files written via GitHub connector; added fixtures for geometry operands, modifier ordering, conditions and invalid values. No TypeScript compile, npm tests, native build, Studio/GPU QA, CI, or Mac merged-tree gate. No release or main merge. Draft needs reconciliation with agent rules already in main.

Next: locate active building/cave/TerraWeave owner branches and their actual graph schemas/authoring APIs; add adapters only after confirming ownership and persistence contracts. Extend geometry catalog with native-supported modifier creation and typed material/texture inputs, then run test:graphs, typecheck/build and native acceptance. Preserve distinct domain execution and existing formats.
