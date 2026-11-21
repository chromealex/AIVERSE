---
name: aiverse-wind-masks
description: Hierarchical wind masks for any model — how strongly each part (trunk, branch, twig, leaf, flag, cable) sways; shared mask library, per-class profiles, <model>.wind.json sidecars, TerraWeave Arbor rigs, one GPU vertex graph.
---

# AIVERSE hierarchical wind masks

Use this instead of writing a per-object wind shader or painting a single
"sway weight". Code: `engine/render/WindRig.h` (CPU, reference math),
`WindRigShader.h` (GPU graph), `WindRigModel.h` (models, sidecars),
`TerraWeaveArborWind.h` (procedural trees). Docs: `docs/wind-masks.md`.

## Concepts
- **Mask** — 1D curve 0..1 along a part (0 = attachment, 1 = tip), 32 samples.
  Library `wind/masks.json` adds to built-ins `rigid, linear, trunk, branch,
  twig, leaf, tip, uniform`. Identical curves are merged; ONE atlas per game.
- **Profile** — defaults per hierarchy level (mask, stiffness 0..1, flex 0..2).
  `wind/profiles.json`; one profile per species/class.
- **Rig** — parts with pivot, tip, parent. A child moves rigidly with its
  parent at its attachment and adds its own masked bend. 4 levels evaluated.
- Cost: 16 B per part, 4 B per vertex (vertex colour), shared shader program.

## Author an object
`<model file>.wind.json` next to the model (applied by `render::loadModel`):
```json
{ "profile": "oak",
  "rules": [ {"node": "Branch*", "mask": "branch"},
             {"node": "Leaf*", "mask": "leaf", "flutter": 1} ],
  "flutterMaterials": ["Leaves"] }
```
Without `skeleton` the model's node tree is the hierarchy (glTF trunk node ->
branch child nodes). Node transforms are baked into model space. For a single
mesh, give `"skeleton": [{"name","parent","pivot":[x,y,z],"tip":[x,y,z],"mask"}]`
and vertices bind to the nearest segment. Rules: `node` exact or `prefix*`.

## Code
```cpp
using namespace aiverse::render::wind;
WindMaskLibrary lib; lib.addCurve("flag", {{0,0},{0.2f,0.6f},{1,1}});
auto atlas = WindMaskAtlas::fromLibrary(lib);          // share it
WindRig rig = buildWindRig(nodes, profile, lib, seed);  // or rigFromModelNodes
bindWindRig(model, rig, *atlas, binding);               // before GPU upload
// per draw (same I.anim convention as canopy wind):
draw.useVertexParameters = true;
draw.vertexParameters = windRigVertexParameters({windX, windZ, phase, gust});
draw.maxVertexOffsetMetres = windRigMaxOffsetMetres({...});
```
Arbor: `makeArborBranchLodModel(profile, seed, {}, &owners)` then
`applyArborWind(model, generateTree(profile, seed, TreeLOD::High), owners, ...)`.

## Rules
- Rigged materials ignore vertex colours (`vertexColors=false`); bind before upload.
- A material with its own fragment shader graph is never replaced (throws).
- Not for skinned/animated node models. PHYSIS wind is the input; this is
  presentation only.
- Test: `wind_rig` CTest. Keep `PackedWindRig::offset` and the shader in sync.
