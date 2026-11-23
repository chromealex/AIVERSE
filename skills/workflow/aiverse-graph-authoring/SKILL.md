---
name: aiverse-graph-authoring
description: Reuse the shared Studio graph canvas, typed graph projection and domain adapters without duplicating native graph execution.
---
# Shared graph authoring
Read docs/agents/graph-framework.md, docs/agents/feature-contract.md and the owning subsystem rules. Reuse GraphRegistry, GraphAdapter, validateTopology and GraphCanvas in apps/studio-web/src/graphs. Define domain-specific typed ports, cycle policy and native validation; do not introduce a second runtime or change persisted formats merely to match UI. Use typed Studio bridge and existing MCP operations. The current shader adapter covers three shader domains. DraftHistory is local only; preserve domain commit semantics. This workflow has no runtime target. Run npm run test:graphs, typecheck, build and real native QA. Record handoff and exact evidence. For new authoring domains update graph inventory, feature manifest, catalog, and the owner roadmap.

Use graphSelection.ts for marquee selection and rigid grouped movement. Preserve stable IDs, remap only internal clipboard links, validate domain compatibility, and commit each group edit to local draft history once. Never paste native file paths or bypass domain validation.

GraphCanvas consumes GraphView and PortRef ({node,port}) from graphGeometry.ts. Provide named typed ports, including multiple outputs if the native domain supports them. Use graphGeometry for transient wire positions and domain mutations for real edits; never persist preview geometry or apply another domain's cycle rules. Supply scoped commands and catalog-validated templates as appropriate.

For SpectraFX and MotionWeave, see nativeGraphAdapters.ts and NativeGraphPanel.tsx. Use the allowlisted studio.op bridge with each domain's existing operations. Keep execution/data ports distinct, preserve pose DAG ordering and state transition loops, and use the native validator before any save. MotionWeave supports expected document concurrency checks; SpectraFX published save currently has only a non-atomic read-back preflight. Do not advertise concurrent writer safety for SpectraFX.
