# Hierarchical wind masks

Status 2026-11-21: CPU data model, packing, reference evaluation, model/sidecar
binding, Arbor rigs and the GPU graph are implemented. GLSL is validated with
glslang (330 core, ES 300, 450); MSL and on-screen behaviour are NOT verified
on Metal yet. Handoff: `docs/agent-handoffs/wind-masks-2026-11-21.md`.

## Model

```
mask library (wind/masks.json, one atlas per game)      32 B / mask
   ▲ index
profile (wind/profiles.json, per species/class)          level -> mask, stiffness, flex
   ▲ defaults
rig (per prototype mesh, shared by all instances)        16 B / part
   part: pivot, axis, length, parent, mask, stiffness, phase, flex
   ▲ node index
vertex colour (per vertex)                               R,G node | B flutter | A legacy weight
```

Per vertex the shader walks up to 4 parts, deepest first. For part *k* the
position along the part `t = dot(q − pivot, axis) / length` is looked up in
the part's mask; the bend is the wind component perpendicular to the axis ×
mask × flex × a phase-shifted wave, minus an along-axis term so parts swing
on an arc. Then `q = pivot`: the parent is evaluated at the child's
attachment, so a branch moves rigidly with the trunk where it is attached and
adds its own bend. Leaf flutter is a high-frequency term along the normal,
weighted by the vertex's B channel.

## Files

| File | Role |
|---|---|
| `engine/render/WindRig.h` | `WindMaskLibrary`, `WindProfile`, `buildWindRig`, `packWindRig`, `encodeWindVertex`, `PackedWindRig` (reference) |
| `engine/render/WindRigShader.h` | `windRigGraph()` (shared vertex graph), `windRigVertexParameters`, `windRigMaxOffsetMetres` |
| `engine/render/WindRigModel.h` | `WindMaskAtlas`, `bindWindRig`, `rigFromModelNodes`, `bindNearest`, sidecars |
| `engine/render/TerraWeaveArborWind.h` | `arborWindRig`, `applyArborWind` |
| `engine/render/Renderer3DModels.cpp` | `SG_TEXEL<i>(x,y)` exact vertex texel fetch for graph textures |
| `engine/render/Model.cpp` | `loadModel` applies `<model>.wind.json` |
| `engine/render/Renderer3D.h` | `setSceneWind`, `setWindRigDebugView` (Parts/Depth/Weight) |
| `engine/render/TerraWeaveForestAdapter.h` | `enableHierarchicalWind`, `setSpeciesWindProfile` |
| `apps/studio-native/StudioWindPreview.h` | Studio «Ветер» panel state (`studio.wind`, `studio.windSet`) |

## Project data

`wind/masks.json`
```json
{"masks": {"palm_frond": [[0,0],[0.3,0.4],[1,1]], "flag": [[0,0],[0.2,0.6],[1,1]]}}
```
`wind/profiles.json`
```json
{"profiles": {"oak": {"levels": [
   {"mask": "trunk",  "stiffness": 0.85, "flex": 0.35},
   {"mask": "branch", "stiffness": 0.55, "flex": 0.75},
   {"mask": "twig",   "stiffness": 0.35, "flex": 1.1},
   {"mask": "leaf",   "stiffness": 0.2,  "flex": 1.3}]}}}
```
`<model>.wind.json`: `profile` (name or inline), `rules` (`node` exact or
`prefix*`, `mask`, `stiffness`, `flex`, `flutter`), `flutterMaterials`,
`materials`, optional explicit `skeleton`.

## Seeing it

Studio: panel «Ветер» (right dock) — preview wind, debug view
(части / уровни иерархии / сила качания / скелет), mask curves of the
project. ImGui editor: PHYSIS «Живая природа» drives the same scene wind.
Games: call `Renderer3D::setSceneWind` with PHYSIS or gameplay wind.

## Limits

- 4 levels evaluated per vertex (deeper parts inherit their 4-level chain).
- Up to 65534 parts per rig, 256 masks per library.
- Rigged materials ignore vertex colours; not for skinned/animated nodes.
- Shadow pass uses the same graph; verify on Metal that shadows sway too.
- GL ES: graph samplers default to the driver's vertex sampler precision.

## Editor showcase on a TerraWeave landscape

The editor's TerraWeave forest overlay uses hierarchical wind masks by default and the botanical catalog (oak, birch, maple, spruce, pine) with the PBR maps of `engine/assets/materials/vegetation/textures`. Agent tool `view_wind` switches the forest, PHYSIS nature-preview grass and wind (`windSpeed`, `windDirection`), the forest LOD distance (`lodScale`) and the debug view (`debug: off|parts|depth|weight|skeleton`). `tools/editor/terra_wind_shots_mac.command` builds the editor, generates a 192x192 TerraWeave map from `templates/wildfire` and writes calm, wind and debug shots to `.wind-qa/terra/`. Known gap: debug views ignore leaf alpha, so leaf cards show as full quads.
