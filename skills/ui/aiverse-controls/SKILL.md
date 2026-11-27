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

### Shared project settings authoring

In native Studio open Project → Project settings → Load settings, edit the existing game.json object and Save settings. This is the general manifest editor for all project properties, including selectionShortcut, commandCard, portrait defaults and minimap settings. It delegates to Workspace game_info/set_manifest. Discard draft before reloading a conflicting edit. Save rejects a changed project or original manifest and preserves the draft on failure. Compatible edits use History → Project; changing module/rules source requires reopening, and topology/map-list changes are history barriers. Restart the game to apply runtime configuration.

For MCP, game_info returns manifest and originalManifest. Pass the exact originalManifest as set_manifest.expectedManifest. Use path="" with valueJson containing the complete JSON object, or a normal property path with value. valueJson is parsed by the native owner and preserves 64-bit JSON integers without a browser numeric conversion. Supply exactly one of value/valueJson. This is generic JSON editing; domain-specific semantic validation remains with each existing runtime owner. Native owner regression and actual Studio save/reopen, Project Undo/Redo, malformed/stale draft acceptance are retained under docs/evidence/issues-project-settings-ui-20261127. Final integration readiness is checked separately.
