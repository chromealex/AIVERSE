---
name: aiverse-look
description: Set the look of an AIVERSE game through MCP - lighting and sky, point lights, post effects with phone cost, materials and textures, model view blocks, fog of war style, checked on screenshots.
---

# Look of the game

Always look at the result: `view_focus` + `view_screenshot` after every step.

**PRISMA ownership:** For any postprocessing changes in C++ or a new screen effect, use `aiverse-prisma` and `docs/prisma.md`. Existing `postfx_*` controls below configure PRISMA; they do not belong to SpectraFX. Scene VFX particles belong to `aiverse-spectrafx`.

1. Lighting: `lighting_set` (sun, sky, day/night, presets); local lights `light_add` / `light_update` / `light_list`. `view_lighting` to preview.
2. Post effects: `postfx_catalog` -> `postfx_preset` (cinematic, vivid, mobile, noir, retro, miniature, sunset, winter, nightVision, clean; `scope` game or map) or `postfx_add` / `postfx_set`. Answers carry `warnings` with ms per frame for heavy effects (bloom, fxaa, sunShafts) - pass them to the user. `postfx_estimate` for the budget, `view_postfx {quality: low}` + screenshot for weak phones. Neutral LUT: `postfx_lut_neutral`.
3. Materials: `texture_patterns` -> `texture_generate` (bricks, panels, wood, camo, scales, normal maps, team masks) -> `material_set` (shared `materials.json`) -> `material_apply {type, material, target|node}`.
   For automatic material selection, inspect the asset using `material_techniques_get {model}`. Use `material_techniques_set {model, rules, expected}` with the previous rules as `expected`. The same operations are available in Studio's Models panel. See `docs/models.md` for the ordered rule format.
4. Models: `asset_import`, then the type's `view` block via `rules_merge` (model, material overrides, team colour, clips; `docs/models.md`).
   Set `view.impactHeight` on the receiving unit type for flat-map straight projectiles. Preview from a side angle with the weapon trail enabled to check the endpoint on the body.
5. Terrain surface: `terrain_material`, `terrain_paint_material` (`docs/terrain-textures.md`).
6. Fog of war look: `rules_merge {path: "fogOfWar"}` (mode, colour, style, softness, lineOfSight).
7. Phone check: `device_set` + `view_screenshot {window: game}` on a low-end device profile.

8. HDR pipeline: `postfx.hdr` = `auto` / `on` / `off` in game.json or the map (`AIVERSE_HDR` overrides);
   costs and limits in `docs/hdr.md`. Temporal stability and flicker checks on heavy scenes: `docs/taa-stability.md`
   (`tools/renderbench/temporal_noise.py`). These are rendering, not output to HDR10/EDR displays.

Report: what changed, screenshots compared, per-frame costs from the warnings.

Runtime library: none. Workflow over the engine MCP view tools.

## Offline model preview in a host application

Use the Python, C# or Java renderer bindings in `sdk/render/README.md` for a bind-pose thumbnail. Load the installed native library, open the model once, render top-down RGBA8 at the required dimensions and camera angles, then release its handle. The CPU backend runs headlessly.

## Additive model materials

Use the existing shared material owner, for example
`material_set {"name":"glow","material":{"blend":"additive"}}`, then
`material_apply` on the intended type. `additive` adds source RGB multiplied by
source alpha; `additive-unweighted` adds source RGB without that factor. Read
back with `material_get`. Both modes depth-test, do not write depth, and do not
cast ordinary shadows. They remain scene-linear in HDR; this is independent of
SpectraFX particle `blendSpace`. Studio Materials exposes the same material.
For an FX model, author `model.materials[].blend` through `fx_set`; omitted blend
inherits the imported material. See `docs/models.md` and the three-scene
`demos/material-effects` example. Native Metal and WebGL example captures are
retained in `docs/evidence/issues-material-effects-{metal,web}-20261127`;
other backend parity is not established by those captures.
