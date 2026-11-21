# AIVERSE Studio HTML5 — Stage 66: real project Asset Browser in approved first-concept layout

Date: 2026-11-20
Branch: feature/studio-html5
Parent commit: 6862add4160cecacb96b48e885bd8f0e8a749d29
Stage commit: see branch history.

## Implemented
- Replaced text-only Assets list with a dock-safe file browser: project-root breadcrumb, searchable real file catalog, actual project folder tree left, grid/list switch and selected file path/size.
- Pure `src/assetTree.ts` derives folders exclusively from the file paths returned by `Workspace.asset_list`, with filtering of absolute/traversal/control-character paths. Search is explicitly global; default grid shows the immediate folder contents only.
- Resource cards use Lucide icons differentiated by file extension (model, image, audio, code, document). No fabricated files or thumbnails, no direct browser file I/O.
- Styled the browser through semantic `--au-*` Studio UI tokens, matching the richer first-concept bottom Assets area. Keeps the C++ bridge and native Metal sibling Scene unchanged.
- Added Node 22 tests for folder tree, folder navigation/global search, path handling, file kind and breadcrumb generation; registered `npm run test:assets` in the existing frontend test:web workflow.

## Files
apps/studio-web/src/assetTree.ts
apps/studio-web/tests/assetTree.test.mjs
apps/studio-web/src/components/AssetsPanel.tsx
apps/studio-web/src/components/AssetsPanel.css
apps/studio-web/package.json
docs/handoff/studio-html5/66-assets-browser-first-concept.md

## Validation
Source changes committed to feature branch; new tests authored. npm test:web, full Vite/React build, WKWebView visual QA NOT RUN in the connector-only environment.
Actual native Asset list is read-only, capped by the existing Workspace operation. No image thumbnail decoding pipeline is implemented.

## Limits and next
- Cards deliberately show *file-type icons*, not real image/model thumbnails. Adding thumbnail provider requires a controlled native resource/preview API, not fake generated pictures.
- Current project asset navigation uses a flat, bounded folder list; very large projects will need indexed virtualized trees.
- Stage 67: run full Node tests and macOS Vite build, inspect selected first-concept layout and fix any issue. Continue richer Inspector once ECS WorldInspect can provide real component data.
- Preserve main and legacy ImGui; one handoff per stage.
