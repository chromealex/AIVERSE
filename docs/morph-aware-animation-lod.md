# MotionWeave — morph-aware visual LOD authoring (Stage 172)

This feature is genre-neutral. The source Model has ONE animator/AnimationGraph owner. Visual LOD meshes are only render representations and **never evaluate another animation graph**.

## glTF/GLB source authoring

A visual parent node references its most detailed mesh (LOD0). Define authored LOD meshes using the preferred `mesh.extras.aiverseLods` field on that original mesh, ordered from high to low screen-size thresholds:

```json
{
  "meshes": [
    {
      "name": "Face_High",
      "primitives": [{"attributes": {}, "targets": [{}, {}, {}]}],
      "extras": {
        "targetNames": ["Smile", "Blink", "JawOpen"],
        "aiverseLods": [
          {"mesh": 1, "maxPixels": 120},
          {"mesh": 2, "maxPixels": 40}
        ]
      }
    },
    {
      "name": "Face_Medium",
      "primitives": [{"attributes": {}, "targets": [{}, {}]}],
      "extras": {"targetNames": ["Blink", "Smile"]}
    },
    {
      "name": "Face_Low",
      "primitives": [{"attributes": {}, "targets": [{}]}],
      "extras": {"targetNames": ["Smile"]}
    }
  ]
}
```

This is a **schema illustration only**, not a complete loadable glTF file: real primitives must contain valid geometry accessors, indices, attributes, and morph delta accessors. The example demonstrates that LOD1 can reorder channels, while LOD2 can omit expressions. The actual source graph still provides the original high-resolution pose.

The older `extras.rtsLods` key remains accepted as a compatibility fallback when `aiverseLods` is absent. When both exist, `aiverseLods` takes precedence.

## Publication and rendering rules

- Each morph-bearing mesh in an authored LOD chain must supply genuine, nonempty, unique `extras.targetNames`. The target name list must identify its primitive morph target order. Names are exporter-authored metadata, not part of the mandatory core glTF specification.
- Each LOD target may have fewer morph channels and a different order, but every target channel name must exist in the LOD0 source table. A missing/ambiguous name is a hard import error; no index guessing.
- Morph-bearing LOD sources and targets must both contain morph deformation. If one level omits all channels, import fails instead of silently dropping the face pose. For a very cheap LOD, author at least one permitted expression or use an independently managed whole-model LOD policy.
- Existing skin palette rules still apply: source and target primitive skin binding must match. Nested LOD chains and non-decreasing `maxPixels` remain unsupported.
- At import, `ModelLod::morphSourceForTarget` stores target-slot -> LOD0-slot mappings. It belongs to the source/LOD asset publication. On disk reimport the Model and map are regenerated.
- During camera/shadow LOD selection and crossfade, the *same original* animated pose is sampled once; Renderer3D fetches weights through that mapping and emits morph texture indices for the actual target mesh. No new AnimationGraph player is created.
- CPU node weights and source clipping allow up to 64 facial channels. Draw submission packs nonzero channels and indexes them in GLSL (GLES/Vulkan) or MSL (Metal). No top-N pruning.
- Conservative morph geometry bounding is calculated during `Model::finalize` for weights in [0,1]. If a current expression uses a negative or >1 value, animated mesh LOD falls back to LOD0 instead of using a bounding model that is not proven conservative. This does not itself solve all out-of-bounds culling for unbounded authoring expressions.
- Automatic mesh simplification still skips morph/deformable meshes. Morph LOD must be authored, not generated from static AutoLOD.

## Memory and backend restrictions

Every morph channel stores two RGBA32F texels per vertex (position and normal). Large faces with 64 channels can use substantial VRAM. The existing texture address path stores integer texel offsets in float uniforms, so the global morph atlas is restricted to at most 2^24 texels (256 MiB RGBA32F). A more restrictive per-model budget can be specified before running the app:

```sh
AIVERSE_MORPH_TEXTURE_MIB=64 ./aiverse_modelview ...
```

The value must be an integer from 1 to 256 MiB. A model over budget is rejected rather than truncated. The graphics backend may impose an even smaller texture dimension or storage budget and reject resource creation. Per-device capability discovery and texture paging are **not** implemented.

## Verification status (2026-11-20)

This is SOURCE IMPLEMENTATION ONLY. No native C++ compile, CMake/CTest, real glTF/FBX fixture import, Metal/Vulkan/GLES shader or pixel test, sanitizer, GPU fault injection or crowd benchmark has been performed in this chat. Do not claim visual LOD parity until the final native Mac acceptance verifies remapping, crossfade, skinning, shadows, large rigs and memory budgets.

See `docs/agent-handoffs/animation-stage172-morph-aware-lod-2026-11-20.md` for exact code commits, limitations and the next integration step.
