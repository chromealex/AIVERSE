---
name: aiverse-touch-accessibility
description: Touch controls (fixed and floating sticks, hold/toggle buttons, tap, double tap, long press, swipe, pinch, rotate, pan) and accessibility settings (colour vision, scales, subtitles, reduced motion, remapping with conflicts).
---

# Touch controls and accessibility

Header: `<aiverselib/gameplay/TouchAccessibility.h>`. Library catalog id `touch-accessibility`, CMake target `AIVERSE::lib_touch_accessibility`.
Capability area U1 in `docs/capability-status.json`. Tests: `tests/libraries/policy_mechanics_tests.cpp`.

Deterministic policy: Fixed/integer math, ascending ids, strictly increasing ticks; repeated or invalid input returns nullopt and leaves state untouched. Works in any project: the owner reports facts and applies outputs.

## Use

1. Touch: define a `Layout`; each frame `updateTouch(layout, state, touches, dt)` gives stick vectors, button states and gestures; map them to InputFlow actions.
2. Accessibility: keep a `Settings` per player; `validate` names the first bad field; `bindingConflicts` and `rebind` for remapping.
3. `compensate(colorVision, rgb)` for UI/palette colours, `shakeScale` / `flashScale` for camera shake and flashes.

## Limits

No on-screen widgets (draw them with the UI layer); colour compensation is per colour, not a post-process pass.
