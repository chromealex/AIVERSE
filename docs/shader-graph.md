# Shader Graph

Shader Graph edits bounded fragment and material vertex graphs in **Графика → Shader Graph**.
One graph produces GLSL ES 3.00 / desktop GLSL 330 and Metal MSL. No shader source
from project data is executed directly. Existing games and built-in shaders remain valid.

Agent workflow and three applied examples: [Shader Graph agent guide](shader-graph-agents.md).

Expanded catalog (103 operations), textures, parameters and vertex outputs:
[Node reference](shader-graph-nodes.md).

## Editing

Open a project, choose `material`, `postfx` or `particle`, add nodes and connect
an output pin to an input pin by clicking each. Drag headers to move nodes;
middle-drag pans the canvas. The properties panel also exposes connections and outputs.
Escape cancels a pending connection. Delete removes a node; dangling connections
are reported and must be repaired. Undo/redo includes values, connections and layout.
Invalid graphs remain editable but cannot be saved/applied. Save or discard edits
before loading another graph. Preview runs the actual renderer on the current GPU:
a lit sphere, a postprocessed sphere image, or a looping particle emitter.

Assets are stored in `<project>/shaders/<name>.json`. **Save and apply** embeds a
validated snapshot into the destination. Reapply after editing the asset:

- `material`: target is a library material name in `materials.json`; existing keys
  are merged. Assign it to objects through the Materials window / `material_apply`.
- `postfx`: adds the `shaderGraph` effect to game.json, at medium quality by default.
  MCP supports `scope: "map"`. One graph effect, applied at the end of the composite;
  texture nodes sample the scene input, while Input is the current composited colour.
- `particle`: target is an existing FX name, emitter is a zero-based index.
  Applies to billboards, mesh particles and sprite frame batches. Trails keep their
  existing shader. Emitter `minQuality` controls availability on weaker devices.

The embedded snapshots travel in ordinary content packs without runtime file watchers.
Reapplying uses existing material/FX/postfx operations and their refresh behaviour.
The graph window retains unsaved per-project drafts in memory, not across editor exits.

Example assets: `examples/shader_graph/{material,postfx,particle}_tint.json`.
Copy one into the project shaders folder and load it by name.

## Format and operations

```json
{
  "version": 1,
  "domain": "material",
  "nodes": [
    {"id": "surface", "op": "input", "position": [40, 60]},
    {"id": "tint", "op": "constant", "value": [0.2, 0.6, 1, 1], "position": [40, 180]},
    {"id": "result", "op": "multiply", "inputs": ["surface", "tint"], "position": [260, 90]}
  ],
  "outputs": {"color": "result"}
}
```

All ports are float4 RGBA vectors. Scalar constants splat to all channels; material
roughness/metallic use the red channel and emission uses RGB. There are no implicit
connections by numeric index: input entries reference stable node IDs. Material colours
are linear, particle colours follow the existing particle display-space path, postfx
colours follow the existing LDR composite. UV nodes produce `[u,v,0,1]`.

The [node reference](shader-graph-nodes.md) describes all 103 operations and their
input order, domain and stage constraints. `shader_graph_catalog` reports arity,
domains and vertex eligibility. All previous operations keep their meaning.
Material texture samples decode sRGB RGB using the built-in model conversion and preserve alpha.
`texture` reads the existing base texture or scene; `sampleTexture` adds a project image,
and `sampleTextureLod` samples an independent image in a material fragment or vertex stage.
Up to four independent images are bound. `parameter` uses a named default or graph.parameters override.
Material outputs: required color; optional roughness, metallic, emission, world-space normal,
local-space vertexOffset and vertexNormal after skin/morph. Fragment graphs run before alpha
testing and lighting; vertex displacement runs in colour, outline and shadow passes.
Postfx and particle domains expose color only. See the node reference for clocks, particle
age/lifetime/random, numerical guards, texture colour spaces and quality settings.

Limits: 128 nodes, eight texture sample nodes, 64-character IDs, 256 KiB assets,
finite constants within +/-1,000,000. Missing connections, cycles, duplicate IDs,
unknown operations and incompatible output domains are errors. Cycles are checked
using the existing library topological ordering implementation. No compute, custom source nodes or Shader Graph imports.

## MCP

- `shader_graph_catalog`: supported operations and arities.
- `shader_graph_list`, `shader_graph_get {name}`: assets.
- `shader_graph_validate {graph}`: validation, generated GLSL/MSL statements and cost counts.
- `shader_graph_set {name,graph}`: validated atomic save.
- `shader_graph_apply {name,target,emitter?,scope?}`: embed the saved snapshot.
- `shader_graph_preview {name}` or `{graph}`: open in the graphical editor.

GPU compilation uses the normal Device error path. Model and postfx variants are
cached by graph source/content and variant state; particle batches include graph
identity. Invalid GPU variants report errors; postfx/particles retain the built-in
fallback. Project authors should check the preview on each target backend.

## Verification

`aiverse_shader_graph_tests` covers graph validation, ordering and domain round trips.
With an editor listening on a separate port and the graph window visible, run
`python3 tests/shader_graph/editor_smoke.py --port PORT --screenshots /tmp/graph-shots`.
It checks GPU diagnostics after every node/domain combination, not only CPU generation.
`shader_graph_preview_status` reports readiness and the current GPU diagnostic.

## Starter material library

Nine editable PBR presets and a safe project installer are documented in
[the material library](../examples/shader_graph/material_library/README.md).
The HDR test scene accepts `AIVERSE_MATERIAL_LIBRARY` to render their actual
material graphs as a gallery on GL and Metal.


## Virtual texture sampling in material graphs

Material-domain Shader Graph now provides a
`sampleVirtualTexture` fragment node with **one UV input**.
It samples the active `Renderer3D::ModelVirtualTextureBinding`
using the existing physical VT atlas and page table, rather than
creating a second virtual-texture implementation. If the material
has no valid VT binding, the node safely samples the ordinary
`tex1` source texture (the existing coarse/full-resolution
fallback). `srgb:true` on the node requests the compiler's
standard sRGB-to-linear conversion after sampling.

Minimal graph:
```json
{
  "version": 1,
  "domain": "material",
  "nodes": [
    {"id":"uv0","op":"uv","inputs":[]},
    {"id":"baseVT","op":"sampleVirtualTexture",
     "inputs":["uv0"],"srgb":true}
  ],
  "outputs":{"color":"baseVT"}
}
```

The compiler sets `Program::usesVirtualTexture` when the node
appears. The model renderer assigns physical atlas and page table
to the next two free texture slots after the graph's own texture
resources, which start at slot 13. The supported layouts on the
current GL33/GLES3/Metal RHI are:

| Independent graph textures | VT physical atlas | VT page table |
| --- | --- | --- |
| 0 | tex13 | tex14 |
| 1 | tex14 | tex15 |

A graph with `sampleVirtualTexture` and more than one
**independent** graph texture fails graph compilation with
an explicit slot-budget error; it is never silently rendered
with a broken binding. The node is fragment-only and material-
only: vertex graphs, postprocessing and particle graphs cannot
use it. It is subject to the existing eight-sample graph budget.

If the application enables this path via
`renderer.setModelVirtualTextureResolver(...)`, the compiler
creates a VT material shader permutation; `SG_SAMPLE_VIRTUAL`
uses the canonical `VirtualTextureSampling.h` GLSL/MSL
lookup and U6 layer/gutter parameters. If the resolver returns
no binding, the same graph runs with `SG_SAMPLE` on tex1.
Shader Graph material GPU equivalence and hashing now include
whether it declares the VT node. GPU Metal ICB batches remain
unavailable for graph-backed materials; the ordinary model
render path still handles them.

**Scope:** this virtualizes the material's explicit VT sample
(and the built-in base sample in the same VT shader
permutation). Other `sampleTexture` graph nodes continue to
use their normal project texture bindings, and the current
system does not expose arbitrary N-domain bindless textures
inside one graph. Alpha-mask R8 reduction is still excluded
for graph-backed assets. The new node and shaders have NOT
been native-compiled or GPU-tested yet.


## Material ambient occlusion output (2026-11-21)

Material graphs may also provide `outputs.occlusion`. The red channel replaces
the ordinary PBR texture AO and is clamped to0..1. It affects the same ambient/
indirect lighting paths as existing AO, without multiplying the albedo or direct
light a second time. Old graphs keep their ordinary texture AO. Postfx/particle
domains still expose color only. Native Renderer3D GLSL/MSL inject this after
standard material sampling. The legacy ImGui output selector has not been
extended; agent-authored graph snapshots can use the output through existing
material assignment APIs. Do not claim editor GUI parity from compiler support.


## World-space height normals

Material fragment node `normalFromHeight(Position, Normal, Height)` returns a
unit world-space normal (W0). Position and scalar Height.r must use the SAME
world units (normally metres); multiply normalized height maps by relief metres
first. Normal is the unperturbed world-space direction. The node adds zero
texture samples and uses screen derivatives; vertex/postfx/particle use is
rejected. It changes shading only, not silhouette, depth, collision or picking.

Example: worldPosition + normal + (height texture.r *0.01) -> normalFromHeight
-> outputs.normal. Blend/layer heights before this node. Rescaling before final
normalization preserves macro detail; degenerate derivatives return the input
normal and a zero normal input falls back to up. Derivatives at hard projection
seams/discontinuous masks still need care; this is not parallax/displacement.

Math reference: Morten S. Mikkelsen, [Bump Mapping Unparametrized Surfaces on
the GPU](https://mmikk.github.io/papers3d/mm_sfgrad_bump.pdf), surface-gradient
formulation. Engine code uses the same GLSL/MSL semantics and numerical guards.
