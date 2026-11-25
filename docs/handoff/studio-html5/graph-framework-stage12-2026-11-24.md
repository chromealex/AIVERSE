# Shared Graph Framework — stage 12 — 2026-11-24

Branch: feature/aiv20261123_feature_gate, draft PR #120.
Implementation HEAD: 198c0fc74769a17d1b3e2deb3222c187e6a861f5

Extended graph_edit to shader, MotionWeave, Modeling and SpectraFX with single edit or ordered batch of 1..64 edits on a private candidate. Shader supports add/replace/delete/move, input slot rewiring/disconnect and output binding. MotionWeave reuses native AnimationDocument helpers, adds state and ordered transition operations and final pose topological sort. Modeling delegates to existing modeling::edit transaction, SpectraFX to existing fx_graph_edit. Result distinguishes applied from native valid and saved:false; failed edit returns no partially changed candidate. Invalid candidates may be repaired, except incomplete/cyclic MotionWeave pose topology can fail during sorting. Save retains native revision/explicit-unversioned guard and revalidates independently. No runtime or disk edit by graph_edit. Size limits and compact validation responses added. Catalog advertises per-family actions. Existing dedicated Studio editors and native owner APIs remain unchanged.

Changed paths: apps/editor/core/{GraphDraftEdits.h,GraphOps.h}, tests/vfx_graph_ops_test.cpp, docs/features/graph-framework.json, docs/agents/graph-framework.md, skills/workflow/aiverse-graph-authoring/SKILL.md.

Verification: NOT RUN. Existing owner API signatures and schemas inspected; GitHub writes confirmed. Added regression fixtures for shader batch, duplicate ID failure, model-less MotionWeave transition draft, native Modeling and FX batches, and no file writes. No C++/TS build, CTest, live MCP call, Studio or GPU acceptance, CI or Mac merged-tree gate. Main rule changes in PR #122 still need reconciliation. No publication or main merge.

Next: add typed property mutations and explicit asset reference/dependency closure from owner schemas, then real owner telemetry adapters. Verify full checkout and MCP registry, native graph transactions and Studio before merge. Do not infer successful runtime execution from draft editing or add unsupported graph domains.
