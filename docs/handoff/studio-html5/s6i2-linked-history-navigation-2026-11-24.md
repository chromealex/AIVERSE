# S6i.2 — linked history navigation and registry availability (2026-11-24)

Owner Studio; branch feature/studio-html5. Base 597ec221ae806b886eef954fbee3a84bf0f6ddb3; roadmap 7b11a2e86987861ec286a895e315c4061d72a491; implementation ed62dbc1246fe30111b7692942f70e96c236b275.

## Implementation
- apps/editor/core/Workspace.h/.cpp: explicit studioLinkOnly operation metadata, distinct from guiOnly, emitted in mcp_registry and described in tools/list. Existing linked asset/history adapters registered through explicit helper. Four new linked commands: studio_agent_history_list, studio_agent_history_step, studio_agent_batch_preview and studio_agent_batch_apply. Local fallback refuses all linked-only tools.
- apps/studio-native/StudioSession.cpp: project-bound paginated retained conversation links; exact scope/history ID/direction/version and explicit confirmation for step; batch direction/token/confirmation reuse existing dispatch and owner coordinator. Hosted agent must be stopped. Linked navigation retains original edit rows rather than fabricating new edits; failed navigation with changed owners fences batch. Capability inventory includes studioLinkOnly.
- apps/studio-native/StudioAgentHistoryPolicy.h: observational list/preview read-only; navigation has explicit special handling, not a new edit.
- apps/studio-native/tests/StudioSessionTests.cpp: authenticated line-client fixture for unconfirmed refusal, exact redo, stale retry refusal, unchanged edit count and exact undo.
- tools/mcp/audit_registry.py: registration baseline expanded, link-only metadata and actual standalone refusal asserted for each listed Studio command.
- docs/devlog/systems.json, docs/mcp-agent-link.md, skills/editor/studio-ui/SKILL.md updated.

## Evidence
Source committed and exact diff/HEAD read back. C++ compilation, registered CTest, built MCP audit, authenticated fixture execution, web build, visual QA, platform/performance checks and Mac KINGSTON merged-tree quick gate NOT RUN. No main merge, full parity or production-readiness claim. Existing HTML5 history controls reused; no new ECS runtime, schema or dependencies. Runtime headless, gameplay persistence and editable graph format N/A for linked editor navigation. Roadmap remains in progress.

## Next
S6 implementation baseline is present; enter QA, not release closure. Sync latest main in isolated branch/worktree, compile and run native/web/MCP contracts with KINGSTON build output on Mac, fix failures and warnings, and test saved/reopened history, dependency/relocation safety, quota/cleanup failure paths and linked batch undo/redo. Remaining limitations from S6g/h/i handoffs remain: conservative dependency coverage, not globally atomic saved map/asset history, quota is logical retained bytes not physical peak, native-only external picker and permanent purge, no claim of arbitrary dependent relocation chains. Review hosted-agent stop boundary and partial failure fences during integration. Record actual test evidence before marking done.
