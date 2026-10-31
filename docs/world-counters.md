# Script-driven world counters

Optional `game.json` `worldCounters` entries provide read-only numeric labels shared by the client and editor Game view. Example:

```json
{"worldCounters":[{"enabledVar":"labels_on","valuePrefix":"count_","xPrefix":"x_","zPrefix":"z_","visiblePrefix":"visible_","height":0.2}]}
```

Each matching value variable suffix identifies one label. Coordinates are absolute world x/z Fixed values; height is an absolute presentation y. Missing coordinates suppress the label. Optional enabledVar and per-suffix visiblePrefix require a positive value. Zero counts remain visible; negative counts clamp to zero. Missing configuration has no effect. The reader never modifies simulation or caches state across restores. Labels outside the viewport or behind the camera are skipped.

The `game` RmlUi model supplies `worldCounters[]` with `text` and screen-space `style`. Game HUD documents opt in with a `data-for="c : worldCounters"` element, `data-attr-style="c.style"` and `{{c.text}}`. Use absolute positioning, centred text and `pointer-events: none`; this is decorative presentation and must not capture commands. Hosts project at logical viewport size and scale positions/width to UI framebuffer pixels consistently with entity actions. The game owns font and styling. No embedded text requires localization because labels contain numbers only.

Tests: `world_counter_script_bindings` covers value, coordinates, visibility, zero, missing data, disabled/absent configuration and read-only behavior. Shader code is unchanged. Android/Web and device touch remain unverified.
