# Feature Readiness — Studio and MCP stages 24–36

Branch: feature/aiv20261123_feature_gate. Implementation and documentation checkpoint: e9d8e21e84152332a046e206c45b1787a4961dc4.

Stage 24: manual native report regeneration and opt-in periodic refresh using the exclusive Workspace background command runner; no arbitrary command or project mutation. Stage 25: paginated readiness query, job status and refresh operations. Stage 33: read-only feature dependency projection using the existing GraphCanvas and selected-node prerequisite/blocker details. Agent assignment proposals appear in the panel. Report writes are atomic. Source SHA and snapshot warning remain visible.

Paths: apps/editor/core/FeatureGateOps.h, Workspace.cpp; apps/studio-native/StudioTools.h, StudioSession.cpp; apps/studio-web/src/panels/FeatureReadinessPanel.tsx; tools/agents/feature_policy.py; tools/mcp/audit_registry.py; docs/features/feature-gate.json.

Limitations: report generation needs Python, Git and origin/main; existing background runner must be idle. Auto refresh is opt-in while the panel is open; no background agent execution. Native snapshot freshness remains unverified by the reader. Graph displays at most 200 nodes; full data available through MCP query. Python, native build, MCP tools/list/call, TypeScript build, UI screenshots and Mac merged-tree quick suite NOT RUN per user request. Next: validate all report operations and UI lifecycle, including busy/error/disconnect paths and narrow docks. No main merge.
