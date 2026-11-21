# AIVERSE Studio HTML5 — Stage 22: read-only Assets panel

Date: 2026-11-20
Branch: feature/studio-html5
Parent commit: 01176fbfc132c8c8a38983b3a7cee7d45bc60898
Stage commit: see Git history.

## Implemented
- Added strict \`studio.assets\` native bridge method. Delegates to the EXISTING Workspace \`asset_list\` operation, always with an EMPTY parameters object. Browser-provided \`dir\` is ignored, preventing arbitrary directory traversal through this new API.
- A new Assets tab beside Hierarchy shows project-local files, human-readable sizes, icon by file extension, search and reload.
- Catalog UI is read-only. No file imports, deletion, arbitrary path reading, drag/drop or side effects are exposed to untrusted JavaScript.
- Loading/empty/error states and mock DEMO catalog (empty, no invented files).
- Tab refreshes on project path or workspace.changed notifications.
- Added negative native session assertion: studio.assets without an open project must fail even if an attacker sends dir "../../".

## Changed files
apps/studio-native/StudioSession.cpp
apps/studio-native/tests/StudioSessionTests.cpp
apps/studio-web/src/types.ts
apps/studio-web/src/mock.ts
apps/studio-web/src/App.tsx
apps/studio-web/src/styles.css
apps/studio-web/src/components/AssetsPanel.tsx
docs/handoff/studio-html5/22-assets-browser.md

## Actual verification
Source committed in GitHub. TypeScript/Vite build, macOS CMake/CTest, and visual/interactive WebView tests NOT EXECUTED.
GitHub Actions was queued previously; inspect current run for results.

## Risks and limits
- Workspace asset_list uses a recursive filesystem walk and caps around 2000 items; currently synchronous on main thread. Replace with async indexed assets for large repositories.
- Default empty directory is enforced in the new Studio method, but arbitrary paths can still be requested through older MCP operations; this stage does not audit unrelated MCP endpoints.
- Asset list is not yet a searchable persistent, async resource database with thumbnails/import/export/drag-drop.
- Studio still has incomplete feature parity with old ImGui editor; keep fallback.

## Next step
Run npm typecheck/build and studio_session CTest on macOS; fix any failures, especially in new React AssetPanel and build target. Continue extracting generic renderable scene data and porting remaining windows.
