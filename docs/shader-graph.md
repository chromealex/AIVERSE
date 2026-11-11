# Shader Graph

Shader Graph v1 edits bounded fragment graphs in **Графика → Shader Graph**.
One graph produces GLSL ES 3.00 / desktop GLSL 330 and Metal MSL. No shader source
from project data is executed directly. Existing games and built-in shaders remain valid.

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

Nodes: constant, uv, input, texture, add, subtract, multiply, divide, mix, sin,
fract, clamp (0..1), oneMinus, dot, length, step, red, green, blue, alpha.
Material texture samples decode sRGB RGB using the built-in model conversion and preserve alpha.
Texture takes a UV input and samples the bound base texture (material/particle) or
scene texture (postfx). No independent texture assets or sampler nodes in v1.
Division clamps denominator magnitude to 0.00001 and preserves its sign.
Material outputs: required color; optional roughness, metallic, emission. The graph
runs before alpha testing and lighting; alpha-mask shadow passes evaluate the same
colour graph. Existing skinning, instancing, normal maps and quality tiers remain.
Postfx and particle domains expose color only.

Limits: 128 nodes, eight texture sample nodes, 64-character IDs, 256 KiB assets,
finite constants within +/-1,000,000. Missing connections, cycles, duplicate IDs,
unknown operations and incompatible output domains are errors. Cycles are checked
using the existing library topological ordering implementation. No compute, vertex
displacement, custom source nodes, time/particle-age inputs or Shader Graph imports.

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
