# Stage 93 — Unity Scene fixes: 3D Transform, Move tool, selection, layout-independent keys, window chrome (2026-11-21)

Branch `feature/studio-html5`. User feedback after stage 92.

## Transform is fully 3D for every map entity
- `apps/editor/AuthoringComponents.h`: `components.Transform` now accepts
  `rotation:[x,y,z]` degrees (Euler Z→X→Y, right-handed) and `scale: n | [x,y,z]`;
  legacy `yaw` / uniform `scale` remain valid and are written back when possible.
  `authoredRotationDegrees`, `authoredScale3`, `authoredTransformKey`, extended
  `editAuthoredPlacement` (keys y, yaw, scale, rotationX/Y/Z, scaleX/Y/Z; NEREID stays
  translation-only). Quaternion composed in fixed point.
- Inspector Transform: Position X/Y/Z, Rotation X/Y/Z, Scale X/Y/Z — all editable.
  A legacy map unit is promoted on its first Y/rotation/scale edit
  (`components.Transform` with its current ground height and facing). Rotation Y is
  kept in sync with the runtime `facing` (clockwise) so the game's initial rotation
  matches. NOTE: the RTS runtime still ignores Transform Y / X-Z rotation / scale for
  units (presentation is Studio + NEREID only) — runtime support is a separate task.
- Scene proxies of authored entities use the full Transform matrix.

## Scene tools
- Orbit / free look direction fixed to Unity (drag right → view right, drag up → up).
- Move tool: X/Y/Z handles drawn on top at the selected object's pivot; drag along an
  axis previews live and commits ONE undoable setProperty on release (x/y/z, or light
  `height`), via the same `studio.inspect`/`studio.setProperty` API. Default host:
  NSView mouse events; composite host: `viewportInput {kind:'handle',phase}`.
- Selection: editor silhouette enabled (`setSceneSelectionOutline`), selected proxy
  drawn with `drawSelectionMesh`, plus an always-visible orange box overlay for the
  selected object (proxies and models).

## Input / chrome
- Shortcuts use physical keys: web `src/keyboard.ts shortcutKey` (event.code), native
  NSView `keyCode` — WASD/QE/F/⌘S/⌘Z/⌘D work with the Russian layout.
- No text selection in the editor chrome (inputs excepted); no WebView default context
  menu (text fields keep theirs).
- Window drag from empty title-bar areas (`studio.windowDrag` → `performWindowDragWithEvent`
  with the last WebView press), double-click zooms.

## Checks
- tsc OK; web 98/98 (new keyboard tests); Python 41/41; clang syntax OK.
- macOS build + `aiverse_studio_session_tests` PASS (full Transform promotion, facing
  sync, per-axis rotation/scale, invalid scale rejected, undo restores legacy unit).
- Interactive on Mac (qa-lua-lab, not saved, undone): Inspector X edit moves the cube;
  Move-tool drag on X moved townhall 9 → 9.8 with Undo; orange selection box and handles
  visible; real-mouse drag of the title bar moves the window; right-click shows the
  Studio menu only; Focus frames the selection.
