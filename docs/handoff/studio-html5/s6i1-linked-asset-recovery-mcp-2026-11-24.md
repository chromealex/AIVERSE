# S6i.1 — linked asset dependency and recovery MCP (2026-11-24)

Owner Studio; branch feature/studio-html5. Base 5a89e4358ea73a0f88fbd7c28cd14a9869bb587f; roadmap 8cb603de1c9c8a72d5fd94abb438d5938511e6c5; implementation aa9f33a7cf913b66c4b046b63f95150b11f16059.

## Implementation
- apps/editor/core/Workspace.cpp: eight explicit registered schemas, authenticated linked Studio only; standalone fallback refuses. studio_asset_dependencies, studio_asset_recovery_storage, studio_asset_trash_list, studio_asset_trash_restore_review, studio_asset_trash_restore, studio_asset_delete, studio_asset_recovery_quota_set, studio_asset_trash_cleanup_review.
- apps/studio-native/StudioSession.cpp: project-bound dispatch allowlist, existing GUID lookup and integer/page/depth validation, direct/multi-hop graph readback, native recovery readback, confirmed reversible deletion with current owner version, reviewed restore with token/version/confirmation, quota compare-and-set and cleanup review. Reuses existing native owner and publication wrapper. No arbitrary browser dispatch, picker or permanent purge adapter.
- apps/studio-native/StudioAgentHistoryPolicy.h: read policy for observational adapters; delete tracked as assets, restore and quota changes remain unverified barriers rather than falsely claiming undo support.
- apps/studio-native/tests/StudioSessionTests.cpp: extends real authenticated line-client fixture with readback, invalid GUID refusal, unconfirmed deletion/restore refusal, actual delete/restore, quota CAS/stale refusal and unknown purge refusal. tools/mcp/audit_registry.py includes Studio asset family registration baseline.
- docs/devlog/systems.json, docs/mcp-agent-link.md, skills/editor/studio-ui/SKILL.md: workflow, confirmation boundaries and limitations.

## Evidence
Source committed and read back from GitHub. No new runtime dependency or persisted schema; existing native/HTML5 owner reused. C++ compile, registered CTest, built MCP tools/list and registry audit, authenticated fixture execution, frontend build, Mac/Windows UI, performance, screenshots and KINGSTON quick gate NOT RUN. No main merge or production readiness claim. Inspector, gameplay headless and editable graph format N/A for these editor adapters. Roadmap in progress.

## Remaining
External import/replacement remains native-picker-only. Permanent purge stays UI-only; cleanup review does not authorize agent purge. Restore invalidates history; quota is not an Undo/Redo operation. No standalone linked history navigation beyond existing owner-bound Agent Panel controls. Dependency result pagination caps returned rows but still computes native graph per query, no stable snapshot token for external mutations. Revisit outstanding S6g/S6h limits and crash/large-input hardening during QA. Next S6i.2: audit registration/transport, capability labels and linked history navigation boundaries; then build and run full native/web/MCP regression gates on Mac KINGSTON and correct failures before any merge.
