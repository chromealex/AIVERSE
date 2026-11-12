# Geometry performance controls

The renderer keeps the portable 4 KB uniform path (32 instances). Metal advertises a
32 KB block and uses frame-owned, 256-byte-aligned buffer pages for larger uploads,
allowing 256 rigid/baked instances per draw. `Renderer3D::setInstanceLimit(32)` forces
the old batching limit for A/B checks; zero selects the backend limit. Shader variants
use the same instance count in GLSL and MSL. Changing the limit invalidates pipelines.
This does not merge different meshes or materials, and CPU-skinned/morphed parts retain
their individual draws. More instances per draw are not automatically faster on every GPU.

`StaticModelCache::prepare(model, world)` caches rigid node transforms, primitive offsets
and conservative world bounds. Assign it to `ModelDraw::staticCache`. The model and cache
must outlive submission; rebuild after changing model geometry/materials/bind transforms.
A different world matrix or animation/material override bypasses the cache. GeometryScene
owns caches for its immutable instances and group proxies. Camera-dependent visibility and
transparency depth are still evaluated every frame. No mutable gameplay state is cached.

`GeometryScene::loadPrepared` accepts owned, nonempty rigid CPU models for procedural/import
pipelines. Geometry GPU resources belong to the scene. Images may carry `borrowedTexture`:
the texture owner must outlive the scene, and releaseModel does not destroy those handles.
HLOD can merge equal materials referring to identical live texture handles; matching filenames
alone are not proof of identical resources. Existing vertex budgets and transparency exclusions
remain in force. `setRuntimeOptions` is an A/B switch, not permission to modify static assets.

Geometry policy adds optional `shadowClusters` (default false) and `shadowErrorPixels`
(default 1, range 0.1..16). Rigid shadow casters are tested against the current light frustum.
Opt-in cluster selection uses that shadow map's matrix/resolution, including the spot pass;
it never uses main-camera occlusion. Masked casters retain fine cluster geometry. This is an
approximate geometric error policy; inspect silhouettes and moving light/camera transitions.
Offscreen objects may still cast visible shadows and are retained when the light sees them.

Offline mesh LOD tools are reused from commit 837b0211 (Sponza work), with meshoptimizer v1.3,
MIT, pinned source/hash manifest in third_party/meshoptimizer/UPSTREAM.json. Build with
`-DAIVERSE_BUILD_AUTOLOD=ON`, target `aiverse_lod_simplify`, then run
`tools/models/autolod.py source.gltf destination.gltf --worker /path/aiverse_lod_simplify`.
The existing Python generator accepts unpacked external-buffer glTF; it preserves source
resources. With `--skinned`, four-influence skins use canonical per-joint weight attributes (weight 8) in the simplification metric; joint IDs are never treated as continuous values. Without the flag skins are skipped. Morph targets remain unsupported. Retained vertices keep their original position, UVs, joints and weights byte-for-byte. Up to 32 total simplification attribute components are supported; larger joint regions require splitting. This is an attribute-aware error estimate, not a guarantee for arbitrary unbounded bone transformations; inspect extreme animation poses. Include referenced resources when distributing output.
`meshes[].extras.rtsLods` persists descending pixel thresholds and target meshes. Nested/cyclic chains, morph targets and mismatched skin targets are rejected. Skinned target meshes inherit the source skin even though they have no scene node. The renderer chooses using both main-camera and
sun-shadow projected extent; reflection/spot views currently reuse that mesh choice. Do not
claim per-pass AutoLOD for those auxiliary views. `setModelLodEnabled(false)` forces LOD0.

`Renderer3D::setProfiling(true)` enables CPU timers. `profile()` resets at beginScene and reports
submission, occlusion construction, per-pass selection/sorting/encoding, submitted triangles,
draws, instances, selected LOD nodes and cached models. Phases: 0 shadows (sun + spot), 1 opaque
(including reflections), 2 blended, 3 outlines, 4 transparent depth. CPU timings do not measure
GPU work; GPU command-buffer timing remains asynchronous. Frame wall intervals include the
macOS platform's 120 Hz NSTimer and presentation scheduling; they are not uncapped throughput.

Benchmark: tools/geometrybench accepts a final positional profile after keep-masked:
`surface` (default), `cache`, `clusters`, `occlusion`, `shadows`, `lod`, `hlod`, `all`, `instances`.
`static` combines LOD, HLOD, surface proxies, static/binding caches and larger batches,
without cluster or software-occlusion traversal. It is a comparison profile, not a universal preset.
Prepared-scene profiles use identical source-node decomposition in both A/B phases and toggle
runtime options; CPU mesh preparation is outside timed frames. `all` combines applicable
features; explicit surfaces still prevent parent HLOD from bypassing their threshold. Legacy
visibleInputTriangles is -1 for grouped/LOD profiles; use per-pass triangles0..4 counters.
The instances profile on Bistro is not a repeated-mesh stress test; use aiverse_instance_preview
for 1025 identical meshes, forced 32/default batches, GPU timing and screenshot comparison.

Prepared C++ scenes retain a 2-million source-vertex default. A caller may explicitly request
up to 16 million; this is a memory budget, not a performance promise. Bistro's per-node split
uses an explicit 8-million budget. JSON-authored scene limits are unchanged, and HLOD proxy
vertices retain their independent 2-million budget.

Metal also avoids rebinding the identical texture/sampler or vertex buffer within one encoder.
The cache compares actual Metal objects (not recyclable handle IDs), resets for each pass, and
can be disabled through `gfx::Device::setBindingCacheEnabled(false)` for A/B diagnostics.
Other backends retain their existing behavior. The additional benchmark profile is `bindings`.

CPU cluster selection skips primitives below 2048 triangles and rejects fragmented selections
above four draws, or when the removed indices do not pay a heuristic 4096-triangle cost per
extra draw. This prevents fewer triangles from creating hundreds of extra CPU draw calls.
The heuristic is a bounded fallback to exact geometry, not a universal hardware cost model.

For a macOS throughput diagnostic only, `AIVERSE_BENCH_UNCAPPED=1` disables CAMetalLayer
presentation sync, uses a 1 ms app timer and holds a user-initiated latency activity until
termination. Normal applications keep their existing pacing. This is not a perfectly uncapped
busy loop: report actual frame intervals and CPU/GPU times, and do not compare results against
paced runs as if only geometry changed. The benchmark JSON records this flag.
The benchmark also accepts warmup and sample counts after the profile (defaults 60/120).
Throughput confirmation uses 180/600 to avoid shortening the sample window when pacing is removed.

Measured benefits and regressions are retained in
[the native Bistro report](benchmarks/bistro-geometry-stack-2026-11-12.md).

### Skinned AutoLOD runtime

Palette-skinned draws with a current pose and node-world matrices select embedded AutoLODs, retaining animation graphs, per-instance shoulder edits and sockets. Selection bounds conservatively enclose transformed source primitive boxes for every influencing joint; main-camera and sun-shadow size both constrain detail. Original LOD0 remains available. Baked-only draws without current pose bounds retain LOD0 (no claim of skeletal AutoLOD in that mode). Material overrides stay on the shared material indices. Morph LODs remain unsupported.

`aiverse_modelview model.glb@Run --time 0.16 --distance 20 --lod-report` prints actual selected LOD-node count and main/shadow submitted triangles. `--no-lod` supplies a LOD0 A/B reference.
