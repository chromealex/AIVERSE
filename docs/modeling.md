# Model and animation authoring

AIVERSE has a host-only authoring library (`tools/modeling`), shared Workspace/MCP
operations, and **Графика → Моделирование / Создание анимации** windows. Game builds
consume ordinary GLB models and the existing `view.animationGraph`. Authoring meshes,
history, caches and background jobs never become simulation state.

## Start through MCP

```json
{"name":"robot","preset":"robot"}
```

Call `modeling_create` with these arguments, then:

1. `modeling_preview {name:"robot",expectedRevision:1,clip:"Walk",time:0.25}`.
   Through the running editor/MCP link this returns a PNG and the rendered revision.
2. `modeling_get {name:"robot"}` to inspect the complete source.
3. `modeling_edit {name:"robot",expectedRevision:1,commands:[
   {op:"parameters",values:{legLength:1.0}}]}`. The response has revision 2.
4. `animation_preview {name:"robot",expectedRevision:2,clip:"Death",time:0.85}`.
5. `modeling_apply {name:"robot",expectedRevision:2,type:"footman",replaceView:true}`.

`replaceView:true` deliberately replaces the target's view block. Without it, apply
rejects existing model-dependent animation/attachment/LOD bindings rather than
silently leaving references to the previous model's bones and clips. To retain
specific presentation settings, edit the resulting view with `rules_merge`.
Use existing `material_set`, `texture_generate`, `material_apply` for game-level
material libraries/textures. Authoring materials themselves currently export PBR
factors, emissive colour and double-sided/alpha settings, not image textures.

The robot is original procedural content: 34 named nodes, six articulated legs,
a turret/muzzle, three materials and Idle/Walk/Attack/Death clips. It is a mechanical
animation example, not a physically balanced locomotion controller.

## Source and revisions

`models/source/<name>.model.json` contains `{revision,document,undo,redo}`. Names use
letters, digits, `_`, `-` and are confined to the opened project. Export writes
`models/<name>.glb`. The packager excludes `models/source/*` from game content. Every edit validates a complete candidate before replacing the
source file. Failed validation keeps the previous file and revision. Undo/redo also
increase the revision. Mutation calls require `expectedRevision`; reacquire the
source after a conflict. Do not treat revision as a replay/simulation tick.

One file holds source and bounded history (32 edits, 32 MiB target); temporary files
are renamed after successful writes. A per-asset directory lock excludes concurrent
writers, including separate processes. An interrupted process may leave a `.lock`
or `.writing` path: inspect and remove it only after confirming that no writer is
running. Asset history does not replace the map's existing undo history. Applying
an exported file and updating rules are separate file operations, not a multi-file
filesystem transaction; an export can remain if applying rules fails.

### Document version 1

```json
{
  "version": 1,
  "parameters": {"length": 1.5},
  "materials": [{"id":"steel","color":[0.25,0.3,0.35,1],"metallic":0.8,"roughness":0.3}],
  "nodes": [
    {"id":"root"},
    {"id":"body","parent":"root","material":"steel",
     "translation":[0,1,0],"rotation":[0,0,0,1],"scale":[1,1,1],
     "geometry":{"kind":"bevelBox","size":[1,0.5,{"param":"length"}],"bevel":0.08}}
  ],
  "skins": [],
  "clips": [],
  "graph": null
}
```

Coordinates are Y up, +Z forward, metres. Node rotations are normalized xyzw
quaternions. Positive node scales only; use a mirror geometry modifier for reflected
geometry. Any numeric parameter expression is `{param,scale?:1,offset?:0}`. It is
resolved before geometry, transforms and graph compilation. IDs survive changes of
parameters and geometry. A meshless node serves as a bone, pivot or attachment socket.

Edit commands:

- `put {collection,id,value}` replaces/adds an entire node/material/skin/clip.
- `remove {collection,id}` deletes it; dangling references reject the transaction.
- `parameters {values}` updates named scalar parameters.
- `graph {value}` changes the existing AnimationGraph definition.
- `replace {document}` validates and replaces the complete source.

All commands use an `op` field and are passed in one `commands` array (max 256).
`modeling_duplicate {node,prefix,translation?}` clones a rigid subtree, preserving
internal parent links with prefixed IDs. Skinned copies require explicit skin remapping.

## Geometry

| kind | Parameters |
|---|---|
| `box` | `size:[x,y,z]` |
| `bevelBox` | `size`, `bevel`, `steps` (2–16) |
| `sphere` | `radius`, `segments`, `rings` |
| `cylinder`, `cone` | `radius`, `height`, `segments`, cylinder `topRadius` |
| `lathe` | `profile:[[radius,y],...]`, `segments` |
| `extrude` | simple `profile:[[x,y],...]`, `height` along Z |
| `sweep` | simple `profile:[[x,y],...]`, `path:[[x,y,z],...]` |
| `mesh` | `vertices:[[x,y,z],...]`, triangle `indices:[...]` |
| `boolean` | `a`, `b` preceding node IDs; `operation:union|subtract|intersect` |

Profiles support concavity and are triangulated with ear clipping. Holes and
self-intersections are not supported. Lathe profiles are open radius/height paths;
include points on the axis to close the ends. Sweep uses transported local frames;
it does not automatically resolve self-intersection at sharp bends.

Boolean operands use their **local geometry**, including its modifiers. Node
transforms are not applied to boolean operands; use a geometry `transform` for
positioning them. `construction:true` keeps an operand editable without drawing it.
CSG uses bounded BSP clipping, requires closed consistently wound operands, and has
an absolute plane tolerance of `1e-5` metres. It is not an exact-arithmetic CAD kernel.
Input is limited to 10,000 triangles per operand, tree depth 256, 2 million splits.
Empty results and exhausted budgets are explicit errors.

Modifiers are applied in order in `geometry.modifiers`:

| kind | Behavior |
|---|---|
| `transform` | `translation`, positive `scale`, Euler XYZ `rotation` in radians |
| `mirror` | adds reflected copy about `axis:x|y|z`, reverses winding |
| `array` | `count` 1–64, repeated `offset:[x,y,z]` |
| `bevel` | chamfers all noncoplanar edges of a convex closed mesh by `amount` |
| `subdivide` | linear triangle subdivision, `levels` 1–4 |
| `smoothSubdivision` | Loop subdivision, `levels` 1–4, positional welding, new box UVs |
| `simplify` | vertex clustering with `cell` of at least 0.00001; inspect shape/topology afterwards |
| `normals` | area-weighted vertex normals (welding is not implicit) |
| `uv` | `mode:box|planar|cylindrical`, `scale` |
| `uvAtlas` | one nonoverlapping chart per triangle, `padding` as tile fraction |

`uvAtlas` trades many seams and imperfect area utilization for predictable layout;
it is not a seam-minimizing production unwrap. General concave edge bevel, sculpting,
retopology and texture painting are not supplied by these operations. `bevel` rejects
unsupported topology rather than presenting a different operation as a bevel.

The CPU geometry cache reuses unchanged geometry definitions, including resolved
parameters, across builds. It is bounded to 500,000 cached vertices; models remain
independent of cache lifetime. Each document is limited to 512 nodes, 500,000 visible
vertices, 1.5 million indices and 1 million generated construction vertices. Reports
include geometry counts, estimated draws, bind bounds, local surface area and
number of degenerate triangles. They do not certify absence of self-intersections.

`modeling_lod {name,expectedRevision,target,cell}` creates a separate editable source
and GLB with the same node/clip names. It rejects explicit vertex weights, which must
be regenerated after topology changes. Add resulting paths to the existing `view.lod`
array with desired distances. Vertex clustering is a basic reduction method; verify
silhouette, thin features, UV seams and skinning before shipping a LOD.

## Rig and animation

Bones use ordinary nodes. A skin is `{id,joints:[nodeId,...]}` (1–80 joints). A mesh
node assigns `skin` and optionally `bindJoint` for rigid weighting. Explicit `weights`
has one entry per generated vertex, each `[[jointIndex,weight],...]` with 1–4 influences.
Weights are normalized and quantized to the runtime format; inverse bind matrices
are generated from the node hierarchy. `rig_auto_weights {node,skin,...}` produces
editable nearest-bone-segment weights (up to four influences, 100,000 vertices max).
This is an initial binding heuristic, not anatomical automatic rigging.

A clip is `{id,channels:[...]}`. Each channel has `node`, `path:translation|rotation|scale`,
`times`, `values`, and `interpolation:step|linear|cubic`. Times must strictly increase.
Cubic values follow glTF in-tangent/value/out-tangent triples. Quaternions are xyzw;
translation and scale use three components. Editing topology invalidates explicit
weights by design instead of silently reassigning them.

`animation_generate` produces editable clips from tracks:

```json
{
  "name":"robot","expectedRevision":2,"clip":"TurretScan","duration":2,"fps":30,
  "tracks":[{"node":"turret","path":"rotation","axis":[0,1,0],
             "wave":"sine","amplitude":0.8,"frequency":0.5,"phase":0,"offset":0}]
}
```

Waves: `sine` (gait/breathing), `spin` (wheels), `fold` (smoothstep transition),
`recoil` (short pulse). Per-leg phase offsets produce alternating mechanical gaits.
Channels are relative to the model's bind transform. Stateful aiming and IK reuse
AnimationRig/AnimationGraph, not a second animation runtime.

The existing rig adds `angularLimit {bone,min:[x,y,z],max:[x,y,z]}`: local Euler XYZ
rotation limits in radians, matching transform's Rz*Ry*Rx convention. Like other Euler
limits it is unsuitable for unrestricted rotations through the pitch singularity;
this is not a physical joint solver. Existing `limit` still clamps local translation.

- `modeling_graph_get/set/validate` reads/saves/compiles the existing graph format.
- `rig_get/edit/validate` and `animation_get/edit` use the same source transaction.
- `animation_sample {clip?,graph?,time,parameters?}` returns local TRS and world joint positions.
- `animation_bake {clip,duration,fps,graph?,parameters?,expectedRevision}` samples the
  graph sequentially into editable keys, then validates and commits it.
- Graph units, states, subgraphs, masks, traits, Motion Matching and IK keep the
  semantics and limitations documented in [animation-graphs.md](animation-graphs.md).

Baking permits at most 60 seconds, 120 FPS and 100,000 keys; sampling permits 600
seconds and reconstructs graph state in fixed 1/60-second steps. Clocks are local to
preview, not simulation. Runtime graph definitions must outlive their instances;
rebuilding a preview resets the graph before releasing its model.

## Editor and MCP

**Моделирование** provides named parts/joints, parameter editing, node definitions,
orbit preview, clip playback, wireframe, skeleton, undo/redo and GLB export. The node
JSON property editor exposes every geometry/modifier/skin option; there is no
vertex-selection gizmo or texture painting UI. **Создание анимации** provides channel
and key selection, a timeline, key insertion/removal and value editing. The graph tab publishes/opens the asset in the existing visual graph and rig editors,
which own graph-node dragging, connections, states and rig-unit editing. The source
window retains complete graph JSON editing and clip baking. Existing
`animation_graph_*`, `animation_editor_open` and `rig_editor_open` operations keep
their original animations/ asset semantics; model-source graph commands are named
`modeling_graph_get/set/validate` to avoid shadowing that API.

`modeling_graph_publish {name,expectedRevision,graphAsset?}` exports the current GLB
and saves a graph snapshot with its model path into `animations/<graphAsset>.json`.
An existing graph asset requires its exact `expectedGraph` before replacement.
Open the published asset with `animation_editor_open` or `rig_editor_open`. After
saving there, `modeling_graph_import {name,expectedRevision,graphAsset?}` validates
and imports it into model source. This explicit snapshot round trip prevents one
editor from silently overwriting another editor's draft.

`modeling_preview`/`animation_preview` accept `name`, optional `expectedRevision`,
`clip`, `time`, `yaw`, `pitch`, `wireframe`, `skeleton`, `useGraph`. Supplying
a clip previews that clip; omitting it defaults to the document graph when present. The MCP editor link returns
PNG content plus revision. A preview pins its snapshot until a new preview or a
manual open/edit, so automatic file polling cannot replace it before capture.
A simultaneous screenshot request is rejected with a retry message. Preview without
an editor returns a readable error; creation, validation, edits, sampling, export,
graph compilation and discovery work without GPU. UI-internal preview calls return
metadata; screenshot delivery belongs to the editor link.

`modeling_build {name,async:true}` returns `jobId` and source revision. Poll
`modeling_job_status {jobId}`; a completed result is consumed once. Cancel with
`modeling_job_cancel`. Cancellation is cooperative between geometry operations;
a bounded CSG/bevel operation already in progress finishes before it is observed.
At most eight queued/uncollected jobs. Progress is running/finished, not a fabricated
percentage. Async jobs never publish or overwrite a newer source revision.

The standalone `aiverse_modeling <project-dir> [engine-dir]` tool reads JSON lines
`{op,arguments}` on stdin and returns the same operation result on stdout. It uses
Workspace directly, without a GUI or Python dependency.

## Validation

```sh
cmake --build <build> --target aiverse_modeling_tests aiverse_animation_tests aiverse_editor aiversemcp
<build>/aiverse_modeling_tests
<build>/aiverse_animation_tests
python3 tests/modeling/mcp_preview.py --build <build> --output /tmp/modeling-preview
```

The first test covers geometry volume/winding, concave triangulation, boolean volumes,
bevel/subdivision/UV bounds, GLB roundtrip through the real loader, skins/clips,
parameters, angular limits, generated motions, weights, history, failed transactions,
async results, LOD and MCP discovery. The second script launches an isolated project
in the real editor and stdio MCP, tests revision conflicts, applies a model, samples
a graph, and saves actual preview/timeline images. It needs a desktop display and
permission to open localhost sockets. Existing engine and animation tests remain
required. Platform checks and visual results are recorded in the implementation report;
passing CPU tests is not proof of mobile or web rendering.

## Verified implementation (2026-11-11)

On macOS arm64, the primary workspace editor, MCP server and CPU authoring tool
built successfully. The authoring suite passed 618 checks and the animation suite
passed 101 checks. The full engine suite in the integration worktree passed
6,136 checks with localhost access enabled. The real editor/MCP integration script passed creation, edits,
revision conflicts, apply, graph publishing/sampling and five screenshot captures
using Metal. A packaging estimate included the exported robot GLB and excluded
`models/source/`. Skirmish and mushrooms headless smoke matches both ended with a
winner. Windows, Linux, mobile and Web rendering were not run for this change.

For distant surface replacements with baked textures instead of vertex clustering, see
[offline surface proxies](geometry.md#offline-baked-surface-proxies). `geometry_proxy_bake`
works on exported/imported model files and produces a self-contained GLB for existing LODs.
The GLB exporter now embeds CPU images and base/normal/metallic-roughness/occlusion/emissive
texture bindings and alpha-mask settings; the authoring document format is unchanged.
