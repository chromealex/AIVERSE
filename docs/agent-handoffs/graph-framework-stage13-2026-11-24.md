# Shared Graph Framework — stage 13 — 2026-11-24

Branch: feature/aiv20261123_feature_gate; draft PR #120.
Implementation HEAD: b714c122dfa6337b51a4fc859fa16f8c8d1cd2de

Added graph_property_schema and graph_asset_dependencies to native Workspace/MCP and Studio read-only allowlist. Property schema covers a bounded typed subset of shader, MotionWeave and Modeling node fields; setProperty edits preserve IDs and delegate final validation to native compilers. SpectraFX uses its native catalog defaults and existing property edit. Dependency inspection checks literal texture/model/mesh file references confined to the project, local model materials, embedded FX function names and embedded animation subgraph references/recursion. No external transitive asset closure, no importer verification, no runtime telemetry, no file writes by dependency inspection.

Changed paths: apps/editor/core/{GraphPropertySchema.h,GraphAssetDependencies.h,GraphDraftEdits.h,GraphOps.h}, apps/studio-native/StudioTools.h, tools/mcp/audit_registry.py, tests/vfx_graph_ops_test.cpp, docs/features/graph-framework.json, docs/agents/graph-framework.md, skills/workflow/aiverse-graph-authoring/SKILL.md.

Verification: NOT RUN. GitHub source writes confirmed. Regression fixtures added for typed property schema, immutable identity, recursive embedded graph and unsafe paths. No C++ compile, CTest, built MCP audit, Studio QA, CI or Mac merged-tree test. No publication/main merge. Main policy PR #122 still needs reconciliation.

Next: extend literal reference resolution only against actual owner import schemas, add owner-provided runtime telemetry with bounded snapshots and explicit unavailable state, wire visual inspector diagnostics, and verify all domains in a real checkout. Do not claim external dependency closure or runtime profiling from this stage.
