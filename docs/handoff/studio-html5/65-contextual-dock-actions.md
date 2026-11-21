# AIVERSE HTML5 Studio — Stage 65: usable tab actions instead of technical position dropdowns

Date: 2026-11-20
Branch: feature/studio-html5
Parent commit: 03472b98a37cb4098401acfee4c1234c39bcb060
Stage commit: see branch history.

## User-reported issue
Every dock tabbar showed a cryptic technical drop-down `left / center / right / bottom` that duplicated dragging and obscured the elegant first approved concept.

## Implemented
- Removed always-visible `nested-move` native HTML select from DockTabs.
- The active tab exposes a small accessible `...` actions button, with aria-label and aria-expanded.
- Clicking it shows an **action strip in the top dock toolbar** (above the native Metal Scene surface), not an overlay potentially hidden by the sibling CAMetalLayer. Commands: Move to named populated dock, Split left/right/top/bottom when more than one tab, Close panel, Done. Escape closes the strip.
- Preserved drag-and-drop, Panels chooser, full docking tree persistence and split keyboard resize.
- Styles use semantic Studio UI v1 tokens and first-concept theme, no new hardcoded palette in panel CSS.
- Updated agent documentation with Stage 64-65 dock semantics.

## Changed
apps/studio-web/src/components/NestedDockWorkspace.tsx
apps/studio-web/src/components/NestedDockWorkspace.css
apps/studio-web/src/ui/editor-shell.css
docs/studio-ui-agent-guide.md
docs/handoff/studio-html5/65-contextual-dock-actions.md

## Validation
Source committed. Full npm TypeScript/Vite build and macOS WKWebView manual menu/drag test **NOT RUN** in this tool runtime. `npm run test:dock` models Stage 64 pruning but visual action toolbar behavior still requires Safari QA.

## Next
On Mac: npm run test:dock, npm run typecheck, npm run build; verify moving an active tab through menu, splitter resize, restoring pruned dock from Panels and native Metal viewport rect after a move. Continue first approved concept UI migrations, handoff each stage. Do not modify main.
