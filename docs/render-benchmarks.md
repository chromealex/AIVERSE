# Sponza rendering benchmark

The reference scene is **Intel Sponza 2022, base package, glTF 003** (not Crytek's
older Sponza or Intel's optional curtains/ivy/candles). It has 3,747,018 triangles,
115 meshes, 28 source materials and 72 source textures. Geometry and textures are
loaded unmodified, at the source scale. Three author-authored cameras are pinned in
`tools/renderbench/scene.json`, with eye, target and vertical field of view.

Source: [Intel GPU Research Samples](https://www.intel.com/content/www/us/en/developer/topic-technology/graphics-research/samples.html).
Credit: Frank Meinl and Anton Kaplanyan, Intel Sample Library, 2022. The downloaded
`main_sponza/credits_license.txt` is retained verbatim: it includes CC BY 4.0 and
introductory usage terms. Assets are external to Git and are not bundled in engine
releases. This is an internal rendering test scene, not game content.

## Fetch, build, run

The source ZIP contains about 4 GB of multiple authoring formats. The fetch tool uses
HTTP byte ranges to read only glTF dependencies (about 2.17 GB), verifies ZIP CRCs,
and saves SHA-256 hashes. Keep 2.4 GB free for first download. Re-running reuses only
matching files and refuses modified assets. It does not delete existing user content.

```sh
python3 tools/renderbench/fetch_sponza.py build/bench-assets/sponza
cmake -S . -B build/.temp/hdr-mac -DAIVERSE_BUILD_RENDERBENCH=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build/.temp/hdr-mac --target aiverse_renderbench aiverse_renderbench_gl
python3 tools/renderbench/run.py build/.temp/hdr-mac/aiverse_renderbench \
  --scene build/bench-assets/sponza/scene.json --out build/bench-results/metal-baseline \
  --profile baseline --capture
```

`aiverse_renderbench` uses the native platform backend. The macOS-only
`aiverse_renderbench_gl` runs the production desktop GL backend offscreen. Other
platform builds use their existing platform runner. The native app presents normally;
CPU frame wall time can include drawable/refresh pacing. Never compare its reciprocal
as uncapped renderer FPS. GPU timeline durations are collected asynchronously. They are not GPU busy-time:
GL_TIME_ELAPSED can include GPU idle gaps while the CPU submits commands; Metal
command-buffer timestamps start after submission. Inspect CPU encode time alongside
GPU time, and do not call their ratio a shader-speed ratio.

This branch builds on the HDR branch, before the main working tree's AIVERSE rename:
existing `rts_runtime`/`rtsMain` internal symbols are retained until integration. New tool
names use AIVERSE. The base's unrelated `<cmath>`/`extern C` build issue and workaround
are documented in [hdr.md](hdr.md).

## Workloads

| Profile | Scene target | Materials | Shadows | Post |
|---|---|---|---|---|
| baseline | linear RGBA16F | High PBR | 2048, 16 PCF taps | ACES + sRGB; no AA/bloom |
| hdr | linear RGBA16F | High PBR | 2048, 16 PCF taps | bloom threshold 1.5/intensity .25; FXAA after ACES |
| low | legacy RGBA8 | Medium | 1024, 4 PCF taps | legacy per-material tone mapping |

All profiles: 1280×720 actual pixels, fixed sun and hemisphere ambient, no fog,
no GI, ray tracing, SSR, dynamic local lights, temporal upscaling, dynamic resolution,
LOD substitution, simulation, GUI, or adaptive governor. These are explicit renderer
workloads, not a claim of feature equivalence to Unreal. Imported cameras/lights are
replaced with the pinned benchmark settings; their importer notices remain in reports.
Unused TEXCOORD_1 attributes are audited against material references and recorded as
notes only when no material uses them. Other importer warnings block cross-engine comparison.

Default: **90 warm-up frames + 240 measured frames per camera**, 3 independent runs.
The warm-up is repeated at every camera, so lazy shader compilation and first-use
allocations are excluded. Screenshot/readback happens after measured frames. No GPU
wait/finish is inserted into measured frames. GPU results are drained afterwards. At most three timed frames are outstanding;
queue pacing occurs outside the measured CPU encode interval.

The runner verifies every asset hash before each batch, clears quality/capture/debug
layer environment overrides, and refuses to overwrite previous reports. Every run
saves revision/build, device, OS/architecture, scene settings, mesh statistics, load
time, camera/sample IDs, renderer draw calls (post passes separately), CPU encode time, CPU frame wall time and GPU ms.
GL timestamps use `GL_TIME_ELAPSED`; Metal uses completed command-buffer GPU times.
GPU timing is currently implemented for Metal and desktop GL on macOS/Windows;
GLES/WebGL return unsupported rather than invented timing numbers.

```sh
python3 tools/renderbench/analyze.py build/bench-results/metal-baseline/baseline-1.json
python3 tools/renderbench/analyze.py first.json second.json --output comparison.json
python3 tools/renderbench/test_analyze.py
```

The analyzer uses Python's standard statistics library (median, arithmetic mean,
inclusive/interpolated p95, peak), reports sample counts, and never treats missing GPU
data as zero. Compare all independent runs; do not select just the fastest. A pairwise
comparison refuses different assets, cameras, profiles, effective settings or GPU
models/machines. CPU wall time is deliberately not used for cross-backend ranking.

## Comparing with Unreal, Unity or another engine

1. Use the exact glTF and texture files identified by `sponza-lock.json`; import at
   source scale without mesh simplification or texture resizing. Record engine version,
   importer settings, build mode, shader compilation completion and asset changes.
2. Reproduce the cameras, framebuffer size, lights and profile. Source coordinates:
   metres, right-handed, Y up. Unity conversion: `(x,y,-z)`; Unreal centimetres:
   `(-100*z,100*x,100*y)`. Transform both eye and target; use vertical FOV (convert for
   engines whose camera setting is horizontal FOV). Do not include viewport chrome.
3. For the comparable raster baseline disable Nanite, Lumen/GI, hardware RT, screen
   reflections, automatic exposure, dynamic resolution, TAA/TSR/DLSS/FSR and bloom.
   Use the same shadow-map budget and document remaining BRDF/filter differences.
   A separate “engine maximum quality” run is useful but must have a different profile
   and must not be ranked as an equal-workload speed test.
4. Measure a standalone Release/packaged player, not the editor. Warm up every camera,
   collect per-frame GPU ms and CPU ms, use the same machine/power mode/resolution and
   at least three independent runs. Do not benchmark two engines concurrently.
5. Inspect corresponding screenshots first. Missing geometry, materials or features
   disqualifies a speed win. Export raw samples using this JSON schema with the actual
   engine/version/device; do not relabel AIVERSE numbers as another engine's results.

Only AIVERSE measurements have been produced by this task. Unreal/Unity adapters and
measurements require their actual installed engines; no comparison ranking is implied.

Полная сцена с тканями, растительностью, свечами, анимированным рыцарем и профилем
`full` (SSR/GI/TAA): [рецепт, ограничения и запуск](sponza-full.md). Базовые locks и
замеры этой страницы сохраняются; нагрузки нельзя смешивать при сравнении.
