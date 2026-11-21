# 96 — Workspaces, command palette, centred play controls, status bar

Date: 2026-11-21. Design: claude.ai artifact "AIVERSE Studio — редизайн редактора" (boards 1, 3, 4).

## Done
- `src/workspaces.ts`: 6 workspaces (Scene, World, Look, Animate, Logic, Publish) with a
  default dock (left | center | right over bottom); each keeps its own layout in
  `localStorage` (`aiverse.studio.dock.v3.<id>`; Scene inherits the old v3 layout once);
  active workspace in `aiverse.studio.workspace`. All 38 legacy windows are mapped to one
  workspace (`legacyWorkspace`). Title-bar tabs + ⌘1…⌘6 + Window menu entries;
  "Reset <Workspace> Layout".
- `src/commandSearch.ts` + `components/CommandPalette.tsx`: ⌘K palette over commands
  (menu items, refresh, console), workspaces, panels, legacy windows (disabled, flagged
  "legacy" with their workspace), scene entities (select + frame) and assets (reveal in
  Assets via `studio:reveal-asset`). Fuzzy subsequence ranking, `>`/`@`/`#` filters,
  recent items first. It is a `role=dialog` so the native viewport hides under it.
- Title bar: menu · project (dirty dot) · Play/Pause/Step centred · workspace tabs ·
  search · settings. ⌘P play/stop, ⇧⌘P pause, ⌥⌘P step (`studio:play-control`).
  Undo/redo/save icons removed from the bar (menu + shortcuts remain).
- `components/StudioStatusBar.tsx`: error/warning counts (click opens Console), map
  name + unsaved, selection, play state + tick, host.
- `tsconfig.json`: `allowImportingTsExtensions` so node tests can import modules that
  import other `.ts` modules.

## Checks
- tsc clean; `npm run test:web` all pass (new `tests/workspaces.test.mjs`: layouts,
  legacy mapping, per-workspace storage + migration, fuzzy ranking/filters).
- Python contracts 41/41.
- Not yet seen on the Mac (computer busy): header widths at 1280 px, palette over the
  native viewport, ⌘P while the Game view has focus.

## Not done
- FPS / frame time in the status bar (no native metric exposed yet).
