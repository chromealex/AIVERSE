---
name: aiverse-controls
description: Configure AIVERSE game client controls in game.json - gamepads (cursor, snapping, bindings, chords, game actions) and region swipe controls for Mushroom-Wars style province games - without game code.
---

# Client controls: gamepad and region swipe

Use for the shipped client's input schemes. Native input routing for your own host is the library skill
`skills/ui/aiverse-input`; camera behaviour is `skills/ui/aiverse-camera`.

1. Gamepad (`docs/gamepad.md`, `engine/platform/Gamepad.h`): game.json `"gamepad"` - `deadZone`, cursor and pan
   speeds, `snap` (`structures` / `units` / `all` / `none`, `flick`), `buttons` (`click`, `rightClick`,
   `key:<Key>`, `pan`, or a game action) and `chords`. The pad drives a mouse-like cursor, so every screen works.
   UI Lua: `game.sendPercent`, `game.useAbility`. Tests: `aiverse_tests gamepad_`.
2. Region swipe (`docs/region-controls.md`): game.json `"controls": {"mode": "regionSwipe", "anchorTag",
   "unitTag", "dispatchAbilities", "cancelAbility", ...}`; regions come from the map, dispatch is implemented by
   the named point abilities in project Lua. The controller only emits standard ability commands.
   Tests: `aiverse_tests region_swipe`.
3. Check on a device profile in the editor (`device_set`) and with the real client.

Not verified (from the docs): web presentation of region swipe; backends other than the developer's platform.

Workflow recipe over existing engine code; no runtime library.

## Selection shortcuts and command cards

Use `set_manifest` to edit game.json `selectionShortcut`, including its optional
`hotkey` (for example F6). A repeated activation focuses the selected subject.
Use ordinary `commandCard.buttons` cell overrides for Move and Sell instead of
adding consumer-specific input code. Armed immobile subjects expose Attack and
Stop; passive ability cells show information without issuing active commands.

Re-read game.json and inspect the actual HUD. The controls demo at
`demos/client-command-controls` exercises F6, Move with M, script selection with
F7, portrait changes, passive ability and Sell placement. Also verify input
focus: hotkeys must not fire while a text input is being edited. Escape opens
pause; its Quit action requests the platform host's normal termination. Web
host behavior differs from closing a native application.
