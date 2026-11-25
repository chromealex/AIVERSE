# S6g.2 — direct asset dependency edges (2026-11-24)

Owner Studio; branch feature/studio-html5. Base 0d3270dce4aef22bd66a636b6a40aa12290199d6; roadmap 7b95aeec9c8bd5f9391deeb78c61bf76edad6e55; implementation b600149bdf78e743422b1fba87442b08abffe9f2.

## Implementation
- apps/studio-native/StudioAssetReferences.h: typed glTF buffers/images URI discovery; deterministic project content scan, no symlink traversal, bounded entries/files/bytes/results, relative lexical containment and limited percent decoding, no target reads. Unresolved local paths retain an empty GUID. Direct incoming/outgoing selection including folder descendants. Existing move preview and shared map/rules/history/delete fence refuse known glTF resource endpoint changes conservatively; no automatic URI rewrite.
- apps/studio-native/StudioSession.cpp: existing studio.assetReferences optional indexed asset path; unchanged full array contract without path.
- apps/studio-web/src/types.ts, components/AssetsPanel.tsx and AssetsPanel.css: Find References shows Used by, Depends on, missing GUIDs, exact document/field, separately labeled text mentions, graph errors and project-scoped results.
- apps/studio-native/tests/StudioAssetTransferTests.cpp: fixture for buffer/image URI, parent-relative resolution, percent-encoded spaces, embedded/remote/escaping URI exclusion, missing GUID, folder and exact edge filters, move impact and refusal.
- docs/devlog/systems.json, docs/mcp-agent-link.md, skills/editor/studio-ui/SKILL.md: scope and usage updated.

## Delivery evidence
Source changes committed and read back from GitHub. No new library, runtime ECS state or persistence schema. Native API, existing Studio and linked move preview integration implemented; runtime verification NOT RUN. Skill/docs updated. New fixture in existing native suite; NOT RUN. Explicit scan budgets, no measured performance. Desktop platform and visual checks NOT RUN. Inspector, simulation/headless, new graph authoring format and runtime serialization are N/A for this editor read-only graph query. Roadmap remains in progress. No local C++/CTest, frontend typecheck/build, MCP registry audit, Mac/Windows native UI, screenshots or KINGSTON quick gate executed in this session. No main merge or full parity claim.

## Known limits / next
Only direct typed .gltf buffers/images and previous material/rules/map edges; no GLB parser, script/extension/remote/unsupported URI resolution or transitive closure. Known glTF edges block either endpoint, including otherwise safe same-folder or coordinated relocations. Malformed glTF and exceeded budgets fail the graph/fence rather than silently approving operations; unrelated malformed documents can prevent moves. Read-only direct graph query is not a standalone linked MCP command. Legacy text mentions remain heuristic. Next S6g.3: transitive traversal with cycle handling, navigation and pagination, explicit coverage diagnostics and safe relative-URI relocation policy; then S6h storage quotas and S6i MCP expansion. Final QA required.
