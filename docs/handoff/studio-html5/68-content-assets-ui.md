# AIVERSE HTML5 Studio — Stage 68: Assets window rooted in project/content

Date: 2026-11-20
Branch: feature/studio-html5
Parent commit: 1cffe6b9e712f79dbdc45a1847fdb09b93542cd8
Stage commit: see Git history.

## Implemented
- Updated `StudioAssetList` DTO to expose optional `root: 'content'`, matching the Stage 67 native reply; browser mock returns an empty catalog with the same root.
- Assets UI displays lower-case **content/** as root breadcrumb, tree node, status footer, tooltip prefixes and empty-state guidance. Folder traversal and global search work on paths RELATIVE to content, with no duplicate visible `content/content` nesting.
- The browser explicitly explains where users should place resources when the new directory is empty.
- Updated pure `assetBreadcrumbs` and added tests for the content root and content-relative imported resources.
- Added content convention and read-only native-bridge safety rules to agent docs and UI guide.

## Paths changed
apps/studio-web/src/types.ts
apps/studio-web/src/mock.ts
apps/studio-web/src/assetTree.ts
apps/studio-web/tests/assetTree.test.mjs
apps/studio-web/src/components/AssetsPanel.tsx
apps/studio-web/tests/mock.test.mjs (if required by current mock contract)
apps/studio-web/AGENTS.md
docs/studio-ui-agent-guide.md
docs/handoff/studio-html5/68-content-assets-ui.md

## API contract
`invoke('studio.assets', {})` returns `{root:'content', files:[{path:'Models/Ship.glb', bytes:...}]}`. The frontend does not allow filesystem paths as parameters, writes or arbitrary directory traversal.
Legacy files outside `content/` are intentionally hidden, NOT deleted or moved.

## Tests
- New frontend assetTree tests authored. Full npm test:assets/typecheck/build on Mac NOT RUN here.
- Native Stage 67 content-directory CTest authored and added to macOS job, not yet run on Mac.
- Existing HTML5 UI/Renderer3D unaffected by UI-only path label changes. Visual QA still needed.

## Next
Pull the branch and run `cd apps/studio-web && npm run test:assets && npm run typecheck && npm run build`, then `bash tools/studio/verify_mac.sh`. Open a real project, verify `content/` exists alongside game.json, put sample files under `content/Models` and `content/Textures`, refresh Assets and check only content resources are displayed. If C++ compile/test errors occur, fix them in the next stage handoff. Do not merge to main yet.
