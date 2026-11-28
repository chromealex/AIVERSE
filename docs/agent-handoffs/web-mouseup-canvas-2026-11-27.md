# Web: mouse-up coordinates relative to the canvas — handoff (2026-11-27)

Branch `feature/aiv1127_web_mouseup_canvas` (from `main` 53330a9f8). Area: `engine/platform/web`.

Problem: `PlatformWeb` listens to mouse-up on the document (a drag released outside the canvas still ends) but
used `targetX/targetY`, which for the document are page coordinates. When the page does not put the canvas at
the top-left corner (a game page that centres a portrait 9:16 column in a wide desktop window) a press went
down on a button and came up hundreds of pixels to the left, so RmlUi buttons never clicked.

Fix: mouse-up subtracts the canvas `getBoundingClientRect()` from `clientX/clientY`, like down / move.

Verified: web build of a game project with a centred 9:16 canvas in desktop Chrome (1400x800): main-menu
button clicks and starts a level (it did not before). NOT RUN: native tests (web-only code), Safari / Firefox,
touch (touch events are on the canvas and were already canvas-relative).

3.1.0 notes: "Web: mouse clicks work when the page does not place the canvas at the top-left corner".
