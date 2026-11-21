# AIVERSE HTML5 Studio — Stage 27: reusable dock-tab UI

Date: 2026-11-20
Branch: feature/studio-html5
Parent: 45c7358b263118300881f544368d7383053849ba
Stage commit: see branch HEAD.

## Implemented
- Reusable DockWorkspace.tsx with four dock areas (left, center, right, bottom) and their own tab bars.
- HTML drag-and-drop tab reordering and moving between dock groups, with allowlisted panel IDs.
- Keyboard-accessible location select on the active tab as an alternative to drag-and-drop.
- Per-tab close, Panels chooser for reopening panels, Reset layout.
- Splitter handles for left/right widths and bottom height, plus empty drop targets.
- The component is generic: receives a renderPanel(panelId) callback and a controlled DockLayout; it knows nothing about C++, games or ImGui.

## Files
apps/studio-web/src/components/DockWorkspace.tsx
apps/studio-web/src/components/DockWorkspace.css
docs/handoff/studio-html5/27-dock-workspace-ui.md

## Checks
Changes committed, no browser/macos visual run or TS build yet.
The standalone docking state model (Stage 26) tests are authored and pending execution.

## Limitations
- This component is not yet wired to App.tsx; next stage must render the editor's real Hierarchy, Assets, Scene, Inspector and Console through it.
- Only four fixed dock areas; no floating windows, nested split trees, drag-to-detach or multi-window support.
- Drag-and-drop in WKWebView/Safari needs manual verification.
- Native 3D surface must follow the Scene pane via ResizeObserver; visibility/hit-testing require Mac QA.
- Existing production ImGui editor remains untouched.

## Next
Replace fixed editor grid with DockWorkspace, unify UI layout persistence and remove old sidebar state, test drag/drop + layout restoration, and record a separate handoff.
