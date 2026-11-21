# 94 — Application menu, dock ⋮ menu, Assets context menu, built-in primitives

Date: 2026-11-21. Branch: current studio-html5 branch (based on 27b6786c3).

## Done
- **Application menu** (`components/StudioMenuBar.tsx/.css`, wired in `App.tsx`):
  File (Open Project… ⌘O, New/Open Map…, Save ⌘S, Save As… ⇧⌘S),
  Edit (Undo, Redo, Duplicate, Delete, Frame Selected, Preferences…),
  Window (toggle each `STUDIO_PANELS` entry with a check, Reset Layout,
  then the legacy editor windows that are not migrated yet, disabled —
  list in `legacyWindows.ts`). The title-bar Open Project icon and the
  Panels `<details>` were removed.
- **Dock tab strip** (`NestedDockWorkspace`): the per-tab "…" is gone.
  One ⋮ button sits at the right of each tab strip; right-clicking a tab
  opens the same menu (Close, Split Right/Left/Down/Up, Move to …).
- **Assets background context menu**: Create Folder, Create Material…
  (inline form: name + matte/metal/glass → `studio.materialCreate`),
  Import Assets…, Copy Folder Path, Reload.
- **Built-in primitives**: MeshRenderer asset accepts `builtin:cube`
  (default; clears the asset) and `builtin:sphere`. The picker lists both.
  The Metal scene draws the primitive with the assigned `materials["*"]`
  (owner colour when unset). Covered in `StudioSessionTests`.
- **Native overlay hiding** (`nativeOverlay.ts`): in the default host the
  Metal view is above the WebView, so the viewport/game native views are
  hidden while a dialog, menu or popover overlaps them (asset picker fix).
- `StudioContextMenu` supports `checked`, `caption`, scrollable height.

## Checks
- tsc clean; `npm run test:web` 98/0; `tools/studio` python contracts OK;
  g++ `-fsyntax-only` for StudioSession.cpp and StudioSessionTests.cpp.
- Mac rebuild/visual check: see the commit note / next handoff if pending.

## Not done / limits
- ~38 legacy editor windows are still not ported (shown disabled in Window).
- RTS runtime ignores Transform Y, X/Z rotation and scale (Studio + NEREID only).
- Material preview uses factors only (no textures).
- RMB fly / Alt orbit / MMB pan need a manual check (automation cannot hold buttons).
