# 98 — Scene: Hand / Move / Rotate / Scale tools, Global/Local, snapping, gizmos toggle

Date: 2026-11-21. Design board 1 (tool strip + caption).

## Done
- **Native** (`StudioMetalScene`): the Move tool is generalised into transform tools
  (`TransformTool` View/Move/Rotate/Scale). Move: arrows; Rotate: three rings (back half
  dimmed), angle unwrapped around the screen pivot; Scale: box handles + centre = uniform,
  always on the object's own axes. Global/Local for Move/Rotate. Snap (move 1 u, rotate
  15°, scale 0.1 by default; Ctrl/⌘ while dragging inverts). Live preview by a world-space
  premultiplier (`dragPreview`) for proxies, primitives and models; `transformCaption()`.
  Gizmos toggle hides handles, selection box and the orientation gizmo. Lights only Move.
- **Host** (`Main.mm`): Q/W/E/R (physical key codes, not while RMB-flying) pick the tool and
  emit `viewport.tool`; Hand tool LMB-drag pans; drag captions go out as
  `viewport.action`; a finished drag writes ONE `studio.setProperties` (x/y|height/z,
  rotationX/Y/Z wrapped to ±180, scaleX/Y/Z clamped 0.001..10000). `viewportInput
  {kind:'tool', tool, local, snap, snapMove, snapRotate, snapScale, gizmos}` works without
  composite mode.
- **Web**: `components/SceneToolbar.tsx` in the Scene header (tool radio group with key
  hints, Global/Local, Snap + steps, Gizmos), persisted in `localStorage`, synced from
  native `viewport.tool`; Q/W/E/R in the HTML Scene too. Status bar shows the live caption.
- **Fix**: the main merge (768aeacb7) had left a second `proxies`/`syncDocumentModels` in
  `StudioMetalScene::render` (redefinition — the Mac build would fail). Removed; contract
  test guards it.

## Limits
- Rotation commits add the dragged angle to the matching Euler component (Unity-style
  inspector values). Exact for Y and for single-axis rotations; combined X/Z tilts on an
  already rotated object are approximate (no quaternion → Euler decomposition yet).
- No rect (marquee) selection yet — stage 99 with multi-selection.

## Checks
- clang++ 18 `-fsyntax-only` (cloud container, real repo headers): StudioMetalScene.cpp,
  StudioSession.cpp, StudioSessionTests.cpp. Main.mm (ObjC++) reviewed by hand — needs
  the Mac build.
- tsc clean; `npm run test:web` pass; Python contracts 44/44 (new `test_scene_tools.py`).
