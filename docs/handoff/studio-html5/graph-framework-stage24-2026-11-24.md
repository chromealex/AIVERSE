# Shared Graph Framework — stage 24 — 2026-11-24

Branch: feature/aiv20261123_feature_gate; draft PR #120.
Implementation HEAD: 7b51dc05d599b676ec6772dd7f8fffe8294f409a

Added declared graph parameters with default and typed node property bindings, native resolution of defaults and instance overrides without mutating source, validation and preview/bake integration. Added per-Workspace owner-thread bounded in-memory LRU result cache with exact resolved program/resolution/targets key (16 entries / 32 MiB accounted key+result bytes). Layout ignored, no persistent disk cache or intermediate cache across edits. Bake set includes texture-set.json with source graph, overrides, output paths and resolution. Material variant command validates manifest and files and uses existing material_set base inheritance with new name; no runtime regeneration, importer settings remain separate, no external-writer CAS. Studio exposes declaration from selected property, JSON overrides, cache status/clear and baked variant after importer settings readback. No GPU evaluator or runtime procedural material instances.

Changed paths: engine/render/graph/{TextureGraph.h,TextureGraphCache.h}, apps/editor/core/{MaterialOps.cpp,GraphOps.h}, apps/studio-native/StudioTools.h, apps/studio-web/src/graphs/TextureGraphPanel.tsx, tools/mcp/audit_registry.py, tests/vfx_graph_ops_test.cpp, docs/features/graph-framework.json, docs/agents/graph-framework.md, skills/workflow/aiverse-graph-authoring/SKILL.md.

Verification: NOT RUN. GitHub source writes confirmed. Authored fixtures for override values, cache hit/invalidation, out-of-range rejection and manifest/clear. No C++/TS compile, CTest, actual texture generation, importer QA, variant material runtime verification, GPU, CI or Mac merged-tree gate. No main merge. Main rules PR #122 reconciliation and texture owner coordination remain required.

Next: inspect existing GPU compute texture capabilities and implement only supported native GPU operations with explicit CPU fallback, or proceed with Building/Caverns owner graph integration if GPU owner is concurrent. Audit compilation/interface mismatches before feature completion and coordinate final tests.
