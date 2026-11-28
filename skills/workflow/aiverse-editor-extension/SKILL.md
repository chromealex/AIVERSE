---
name: aiverse-editor-extension
description: Extend HTML5 Studio with dockable panels, project Lua widgets and world-tree grouping. Connect the UI to shared native operations and verify it in the desktop host.
---

# HTML5 Studio windows and the world tree

1. For project-specific widgets, inspect the Extensions panel and the project's `editor/extensions.lua`. Its `window(ctx)` supplies widgets, action handlers issue commands, and `scene_gizmos(ctx)` describes Scene shapes.
2. For a built-in window, add the panel ID and title in `apps/studio-web/src/nestedDock.ts`, assign its Window menu group in `windowGroups.ts` beside it, and render it in `panels/StudioPanelRenderer.tsx`. Use the shared Studio controls and dock layout. Render asset previews inside their owning panel.
3. Keep document and engine edits in their native owners. Use the typed Studio bridge in `apps/studio-web/src/types.ts` and reuse the shared editor operations for MCP. For world-tree grouping, inspect `panels/HierarchyPanel.tsx`, the scene groupings in the Studio snapshot and `docs/editor-world-tree.md`.
4. Run `npm run build` and `npm run test:dock` in `apps/studio-web`. Open a project in the desktop HTML Studio, select the panel from the Window menu and verify its actions in docked and detached layouts. Capture the working window; verify persistence for document edits.

Game UI uses `skills/ui/aiverse-game-screens`. This recipe uses the existing Studio host and adds no runtime library.
