---
name: aiverse-graph-authoring
description: Reuse the shared Studio graph canvas, typed graph projection and domain adapters without duplicating native graph execution.
---
# Shared graph authoring
Read docs/agents/graph-framework.md, docs/agents/feature-contract.md and the owning subsystem rules. Reuse GraphRegistry, GraphAdapter, validateTopology and GraphCanvas in apps/studio-web/src/graphs. Define domain-specific typed ports, cycle policy and native validation; do not introduce a second runtime or change persisted formats merely to match UI. Use typed Studio bridge and existing MCP operations. The current shader adapter covers three shader domains. DraftHistory is local only; preserve domain commit semantics. This workflow has no runtime target. Run npm run test:graphs, typecheck, build and real native QA. Record handoff and exact evidence. For new authoring domains update graph inventory, feature manifest, catalog, and the owner roadmap.
