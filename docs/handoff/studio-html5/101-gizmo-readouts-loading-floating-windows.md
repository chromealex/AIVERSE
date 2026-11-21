# 101 — Gizmo readouts, project loading progress, floating panel windows

Branch `feature/studio-html5`. Covers the work after handoff 100 (commits b5bc44bf7..this one).

## Done
- **No full-window repaint on selection** (b5bc44bf7): the viewport rect is sent (web) and applied (native
  `placeMetalView`) only when it really changes; `studio.select` no longer sets the global busy flag.
- **Plane handles on every tool** (11f9fc1c2): move, rotate (plane square = the ring of its normal axis)
  and scale (plane square = uniform scale in that plane).
- **Readouts**: rotate draws a sector from the start angle, the angle in degrees and, with snap on, tick
  marks every snap step (major every 90°); move and scale show the delta / factor (`x1.2`) next to the
  handle and in the status bar (`viewport.action`).
- **Project loading** (11816d430): the project opens on a background queue (`projectLoading_` guards every
  session access); `project.loading` events drive a Unity/Unreal-style progress dialog, then models stream
  into the scene with a 30 ms per-frame budget and a strip in the header/status bar.
- **Assets folder tree is resizable** (splitter, width in `aiverse.studio.assets.treeWidth`).
- **Floating windows** (two commits): any dock tab can live in its own OS window, e.g. on a second
  monitor, and each floating window is a small dock of its own (several tabs, splits).
  - Out: drag a tab out of any Studio window and drop it outside all of them, or ⋮ / tab right-click →
    *Open in New Window*, or ⌘K → *Open … in New Window*.
  - Between windows: drag a tab onto another Studio window's dock (tab strip, centre or edge zones) — it moves
    there; this is also how a tab goes back into the main window. ⋮ → *Move to Main Window* does the same.
    Closing a floating window docks all its tabs back; Window → *Dock All Floating Windows*.
  - Native: `studio.windowOpen {window,panels,title,ifOutside,x,y,width,height}` creates an NSWindow + WKWebView
    loading `index.html?window=<key>&panels=a,b`; `studio.windowClose {window}`; `studio.windowTitle {title}`
    (a float names itself after its active tabs); `studio.windowBroadcast {event:"ui.*"}`. All WebViews share
    one bridge and one `StudioSession`; events go to every WebView, synchronous replies to the sender,
    asynchronous replies to all (each window uses its own request-id range, `bridge.ts`). `ifOutside` opens
    only when the mouse is outside every Studio window.
  - Cross-window protocol (`ui.*`): `panelTaken {panel,by}` (every other window drops the tab),
    `floatPanels {window,panels}` (float → main), `showPanel`/`dockPanel` (float → main), `activatePanel`
    (main → float). Tabs carry `application/x-aiverse-studio-tab` and `text/plain aiverse-panel:<id>`.
  - Scene and Game work floating: the Metal view moves into the window whose WebView reports its rect
    (`adoptNativeView`); closing that window moves it back to the main window.
  - Persistence: main keeps `[{key,panels,frame}]` in `aiverse.studio.floating.v2` and reopens them on launch;
    each float keeps its own arrangement in `aiverse.studio.float.<key>`. A panel shown by a float is never
    also in the main dock of any workspace. Closing the main window closes the floats but keeps the list.
  - Selection made in a floating window refreshes the others (`selection.changed` when floats exist).
- Web: `src/floating.ts` (+ `tests/floating.test.mjs`), `FloatingShell.css`, App floating mode,
  `NestedDockWorkspace` `onFloat`/`onAdopt`/`onDockBack`/`compact`; mock returns `opened:false`
  (the browser preview stays single-window).

## Verified
- tsc, `npm run test:web` (all suites, incl. floating), `tools/studio/test_scene_tools.py`.
- Mac rebuild: WEB 0, BUILD 0, session tests PASS, RTS pose test PASS; app launches and loads the web UI.
- Earlier visually checked: Y edit, rotate rings, scale handles, plane drag with snap, tool persistence.

## Not verified yet (no screen access to the new instance)
- Tearing a tab out to a second monitor, Scene/Game floating, restore on relaunch.
- Rotation sector/angle/ticks, move/scale readouts, loading dialog/strip, tree splitter on screen.

## Next
- On-screen pass of everything above on a two-monitor setup.
