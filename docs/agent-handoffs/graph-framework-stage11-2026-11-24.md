# Shared Graph Framework — stage 11 — 2026-11-24

Branch: feature/aiv20261123_feature_gate, draft PR #120.
Implementation HEAD: 4acc76810d2c38d2db392a3b156aeaad0c5e173f

Added header-only native GraphOps facade registered by Workspace: graph_catalog, graph_list, graph_get, graph_node_catalog, graph_validate, graph_edit, graph_save, graph_diagnostics, graph_dependencies. Fixed four owner families: shader (material/postfx/particle), SpectraFX, MotionWeave and Modeling. Actual operation presence checked against Workspace registration; no arbitrary dispatch. Generic responses wrap native result and omit generated shader source. Dependencies are explicit authored references, not a resolved asset closure. In-memory edit currently delegates only SpectraFX. Save validates candidate then uses native owner; MotionWeave expected document, Modeling expectedRevision locked replace; shader/FX explicit allowUnversionedWrite without concurrency guarantees. Read-only facade exposed in Studio animation graph Native operations; dedicated editors keep their bridges. No auto bake/apply/delete, runtime evaluator or telemetry.

Changed paths: apps/editor/core/{GraphOps.h,Workspace.cpp}, apps/studio-native/StudioTools.h, tools/mcp/audit_registry.py, tests/vfx_graph_ops_test.cpp, docs/features/graph-framework.json, docs/agents/graph-framework.md, skills/workflow/aiverse-graph-authoring/SKILL.md.

Verification: NOT RUN. Connector writes confirmed, actual native operation schemas and registration source inspected. Added regression assertions to existing VFX graph ops test for discovery, invalid domain, validation, dependencies, explicit write guard, save/readback and native catalog. Built MCP tools/list audit updated but not executed. No C++ compile, TypeScript compile, CTest, MCP binary test, Studio QA or Mac merged-tree quick gate. Main rules PR #122 still require reconciliation. No publication/main merge.

Next: verify C++ and MCP tools/list + native calls in a real checkout; add typed generic draft edit adapters for shader, MotionWeave and Modeling only where domain ownership and revision checks are preserved. Add dependency closure and actual runtime telemetry separately using owner APIs. Coordinate Building/Caverns and texture/STRATA owners before registering new families.
