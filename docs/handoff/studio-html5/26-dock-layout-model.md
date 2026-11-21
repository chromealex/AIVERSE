# AIVERSE HTML5 Studio — Stage 26: pure docking state model

Date: 2026-11-20
Branch: feature/studio-html5
Parent: b8d4f8cf64d24bad85e02e561e402168f755dc5b
Commit: see branch HEAD.

## Implemented
- Introduced \`apps/studio-web/src/dockLayout.ts\`: genre-neutral, JSON-serializable 4-area docking layout with 8 panels.
- Safe normalization from localStorage: deduplicates panels, filters unknown IDs, checks active tab references and clamps panel sizes. Layout changes never mutate the old state.
- Commands: moveDockPanel (including tab reordering), setActiveDockPanel, openDockPanel, closeDockPanel, dockAreaOf.
- v1 preference migration for sidebar widths, open console and selected tabs.
- Versioned v2 persistence. Only UI preferences are stored; project state remains native C++.
- Added Node 22 test suite in \`apps/studio-web/tests/dock-layout.mjs\` and \`npm run test:dock\`, integrated in web CI.

## Files
apps/studio-web/src/dockLayout.ts
apps/studio-web/tests/dock-layout.mjs
apps/studio-web/package.json
.github/workflows/studio-html5-web.yml
docs/handoff/studio-html5/26-dock-layout-model.md

## Checks
Model implementation and tests committed. Native macOS build and full npm/Vite build not executed here.
Node 22 test command available: \`cd apps/studio-web && npm run test:dock\`.
Run state model tests before building React components, then confirm CI result.

## Limitations
- This is the docking **state engine**, not yet the visual drag/drop or nested/floating windows.
- Current App still uses the fixed CSS grid. Next stage must integrate this model in a reusable DockWorkspace React UI.
- Hidden panels do not render; reopening is controlled by a future Panels menu.
- Native viewport uses ResizeObserver and should follow its mounted panel after repositioning; verify via Mac test.

## Next
Implement accessible dock tab bars, HTML drag/drop across left/center/right/bottom groups, resize handles, open/close controls and persistence. Integrate all panels without creating a new renderer, then validate TypeScript and tests. Keep legacy ImGui until feature parity.
