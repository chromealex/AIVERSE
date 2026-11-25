---
name: aiverse-shader-graph
description: Create custom AIVERSE material, post-effect and particle shaders with Shader Graph and bake lighting (lightmaps, light probes, probe GI) - with cost checks for phones.
---

# Shader Graph and baked lighting

## Shader Graph

Read `docs/shader-graph.md`, the agent guide `docs/shader-graph-agents.md` and the node catalog `docs/shader-graph-nodes.md` (103 operations).
1. Pick the kind: `material`, `postfx` or `particle`. One graph produces GLSL ES 3.00, desktop GLSL 330 and Metal MSL; project data never runs shader source directly.
2. Build from the closest applied example in the agent guide; only catalog nodes and declared parameters.
3. Bind the graph to a material / post effect / particle and check `view_screenshot` on desktop and `view_postfx {quality: low}`.
4. Metal paths that could not be compiled are listed as unverified in the report and commit message.

## Baked lighting

Read `docs/lightbake.md`; probe GI `docs/probe-gi.md` (target mid-range Android at 30 FPS - a target, not a measurement).
1. Mark lights: `light_add` / `light_update` with `bake: mixed` (stays realtime, bounce baked) or `baked` (lightmap/probes only).
2. Bake: `lightbake_set {quality: preview|medium|high, ...}` -> `lightbake_bake` -> poll until done.
3. Compare before/after screenshots; moving units must pick up probe light.
4. Re-bake after static geometry or light changes; never claim mobile frame times you did not measure.

Runtime library: none. Workflow over the engine graphics tools.

For UV-free imported or Modeling/CSG meshes, generate a world-space graph with `material_triplanar` (colour map, optional +Y tangent normal map and repeat length in metres). Save its returned material in the library with `material_set` and assign it to the exported or imported model with `material_apply`. See the triplanar section of `docs/shader-graph.md`.
