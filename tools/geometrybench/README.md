# Bistro geometry experiments

External asset: Amazon Lumberyard Bistro v5.2, NVIDIA ORCA, CC BY 4.0.
Source: https://developer.nvidia.com/orca/amazon-lumberyard-bistro
Credit: Amazon Lumberyard, 2017. Retain the downloaded LICENSE.txt and README.txt.
Assets are not committed or included in engine builds.

Build `aiverse_geometry_bench` (desktop + tests). Run sequentially, without other benchmarks:

```
aiverse_geometry_bench /path/BistroExterior.fbx /path/NEW-cache /path/results/bistro batch 0 32 keep-masked
python3 tools/geometrybench/analyze.py /path/results
```

Create the results directory first. `batch` runs 3 paired repeats across 3 cameras,
60 warm-up + 120 measured frames per phase. Pair order reverses in repeat 2. All phases
share one process and preloaded GPU resources. Individual `off|on` mode also accepts a
camera index 0..2. Following parameters: maximum projected size, `keep-masked`, optional
profile, warmup count and sample count (see the performance controls linked below).
The 96-pixel setting without keep-masked is an intentionally aggressive diagnostic.
Reports refuse overwrite. Use a new cache when source, material adapter or bake code changes.
Archive source/binary hashes alongside reports: the tool itself does not validate cache freshness.

Source FBX is loaded with the production loader. Both variants use the SAME decomposition
into source nodes, transforms, frustum culling, material settings, medium shadows and
1280x720 framebuffer. The default `surface` profile does not enable automatic HLOD, cluster
simplification or meshoptimizer LODs. It isolates baked-surface substitution.
The scene remains resident: no world streaming, simulation, GI, TAA or crowds are measured.

Per Bistro's README, `_Specular` maps are connected as ORM and DirectX normal-map green
channels are inverted. Foliage materials use alpha mask. Source sunlight/cameras and extra
UV sets are ignored by the importer and preserved in diagnostics; the benchmark supplies
its own cameras and default AIVERSE sun/ambient, disables fog, and does not reproduce
Falcor's HDR environment lighting. Do not claim cross-engine visual/workload parity.

Nodes with 512..100000 triangles are candidates for 64-pixel-per-face box baking. Unsupported
materials are retained and listed in `rejected`. keep-masked excludes source alpha-mask
parts (not the generated proxies, which all use sampled coverage). Larger/smaller meshes
remain untouched. The test borrows source GPU buffers/materials for per-node submissions;
both modes use the same source partition. It is not an original monolithic-vs-split comparison.

GPU times come from asynchronous completed Metal command-buffer timestamps; unsupported
backends report failure, not invented values. The optional RHI timing API was reused from
codex/sponza-full. `cpuEncodeMs` measures renderer submission/encoding, `frameWallMs` includes
platform/queue/presentation pacing. Their reciprocals are NOT uncapped FPS. Capture occurs
after all measured samples, with no screenshot readback inside a sample. All requested GPU
samples and a completed screenshot are required before writing each report.

`visibleInputTriangles` is a CPU estimate of main-view input triangles after per-part frustum
selection, not a hardware counter or total across shadow passes. `drawCalls` is the renderer's
actual total draw counter. Proxy resources remain resident in both modes, so these runs do
not measure memory savings. Check all paired screenshots before describing a speed win;
box surfaces can change silhouettes, parallax, masks and shadows. Static captures do not
validate transition popping or moving-camera temporal stability.

The analyzer accepts an optional repeat count: `analyze.py /path/combined 6`. To combine
two batches, copy the second batch reports as runs 4..6 while retaining originals separately.
Inspect individual run medians and p95, not just the aggregate median: the recorded Bistro
experiment reproduced late-session regressions in both fresh processes.

Additional prepared-scene A/B profiles and renderer counters are documented in
[geometry performance controls](../../docs/geometry-performance.md). Use a native target build
(e.g. `-DCMAKE_OSX_ARCHITECTURES=arm64`) and record architecture alongside results.
