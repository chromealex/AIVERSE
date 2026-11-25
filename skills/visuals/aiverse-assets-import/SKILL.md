---
name: aiverse-assets-import
description: Import models, textures, sounds and animations into an AIVERSE game - asset_import, type view blocks, materials, Spine and Unity animation, size limits, checked on screenshots.
---

# Import assets

References: `docs/models.md`, `docs/spine.md`, `docs/unity-animation.md`, `docs/unity-materials.md`, `docs/animation-graphs.md`.

1. `asset_list` - what the game already has; reuse before importing.
2. `asset_import` models (glTF preferred), textures, sounds into the game folder - never into the engine repo. Keep single files under 10 MB; large sources stay outside git.
3. Hook models to types: the type's `view` block via `rules_merge` (model, scale, material overrides by material name, team colour, clips). `model_info` lists nodes, clips and materials - use the real names.
4. Materials: imported PBR or `skills/visuals/aiverse-look` step 3; Unity materials per `docs/unity-materials.md`.
5. Animation: clips mapped in `view`; Spine 4.x skeleton JSON + atlas (`docs/spine.md`, unsupported parts warn on load); Unity animation import `docs/unity-animation.md`; graphs `docs/animation-graphs.md`.
6. Icons: `icons_preview` for unit and building icons.
7. Check: `view_focus` + `view_screenshot` close and at gameplay zoom, idle and in combat (clips play, team colour visible, no T-pose or missing textures); `build_estimate` for size impact.

Runtime library: none. Workflow over the engine MCP asset tools.

For Quake III IBSP v46, keep the BSP under maps/ and its named diffuse images under the sibling textures/ directory. Import scripts/shaderlist.txt and its static .shader definitions when materials use image aliases. model_info resolves the same assets as the renderer. Face lightmaps are embedded in the BSP. See docs/models.md for the OpenArena example and the limits of static material translation.
