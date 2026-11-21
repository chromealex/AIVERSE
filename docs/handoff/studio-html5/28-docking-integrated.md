# AIVERSE HTML5 Studio — Stage 28: DockWorkspace integrated in real editor UI

Date: 2026-11-20
Branch: feature/studio-html5
Parent commit: fb29a2e955eee5746008e58a882bdb42b015939e
Stage commit: see branch HEAD.

## Implemented
- Replaced the fixed .editor-grid/sidebar tabs with DockWorkspace driven by a single DockLayout state.
- Real Hierarchy, Assets, Inspector, Console, and Scene panels are now rendered by panel ID inside any dock.
- Game, Materials and Animation are explicit unavailable placeholders; do not claim those editors migrated.
- Panel tabs can be moved/reordered and closed/reopened via a Panels menu; keyboard alternative is an area select.
- Four zones and their splitter sizes persist across restarts in localStorage v2 (migrating v1 preferences).
- When the Scene panel unmounts or changes geometry, its existing ResizeObserver sends native viewport visibility/rect updates; no image streaming or renderer changes.
- Existing Save/Undo/Redo/project picker and C++ Workspace bridge kept intact.

## Files
apps/studio-web/src/App.tsx
apps/studio-web/src/styles.css
docs/handoff/studio-html5/28-docking-integrated.md

## Verification
Committed GitHub branch. TypeScript compilation, Vite build, actual WKWebView drag/drop and Metal layout correctness have NOT been tested locally. CI remains queued as last checked. Run \`npm run test:dock\`, \`npm run typecheck\`, \`npm run build\` and the native macOS scripts.

## Limitations
- Four fixed dock regions with movable tab strips; NOT full Unreal/VS-style nested split/floating multi-window docking.
- Native GPU view may cover HTML popovers in default composition mode; test the opt-in --html-composite mode on Mac.
- React state can remount a panel on move; currently designed to refresh state from C++ not keep pending unsaved text drafts.
- No long-running editor builds/tests have passed; do not merge or delete old ImGui.
- Engine/game client ImGui paths are unrelated to editor migration.
- Browser drag support and accessibility must be verified on Safari WKWebView.

## Next
Run headless dock model unit tests now, use CI for TS build, then native Mac compile and screenshots. Fix issues as they arise. Extend to nested docking and real other editor windows after baseline is green.
