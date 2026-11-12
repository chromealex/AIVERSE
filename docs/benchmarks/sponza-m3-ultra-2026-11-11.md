# Intel Sponza: AIVERSE baseline on Apple M3 Ultra

Date: 2026-11-11. Renderer source: `1e4b30f4` (builds on the linear HDR commit
`a2011562`). Release build, 1280×720, three authored cameras.
Architecture audit during the full-scene work confirmed this build tree produces
x86_64 binaries: these runs used Rosetta on Apple Silicon. Do not mix their CPU
measurements with the later native arm64 series. Asset fingerprint:
`df6130778c6dcc33089fb2df6813bac1b6d32ba9eccd933cdd02eede7a505550`.

Baseline: three independent launches per backend; each camera uses 60 warm-up frames
and 120 measured frames (1080 measured frames per backend). HDR/Low supplementary
runs use one launch each, also three cameras and 120 samples/camera. Load/upload,
shader warm-up, screenshots and GPU-result draining are excluded from samples.

Raw JSON, logs and screenshots are local generated artifacts under
`build/bench-results/{metal-baseline,gl-baseline,metal-hdr,metal-low}`.
Reproduce with `tools/renderbench/run.py --warmup 60 --samples 120`; see
[the protocol](../render-benchmarks.md) for build/fetch/run commands.

GPU values are elapsed GPU timeline intervals, not occupied GPU cycles or application
FPS. In particular, GL_TIME_ELAPSED on macOS can include idle time between CPU-issued
commands; the Metal interval covers an already-submitted command buffer. CPU encode
is listed alongside it to expose this distinction. Neither these values nor their
ratios are an Unreal/Unity performance comparison.

## Fidelity and limitations

- Geometry/texture bytes are verified against the pinned SHA-256 manifest.
- Imported cameras and zero-intensity source lights are replaced by the pinned camera
  and lighting setup. TEXCOORD_1 is unused by all source materials; ignored-channel
  notices are audited and retained as notes. No other import warnings remain.
- The atrium GL/Metal captures have mean absolute channel difference 0.35/255.
- Hemisphere ambient is used; there is no indirect light transport, Lumen, hardware
  ray tracing, temporal AA/upscaling or virtual geometry in this workload.
- Low reduces rendering quality; it does not reduce the 3.75-million-triangle source
  mesh or 4K texture set, and is not a claim that the whole asset fits a weak phone.
- Windows, Android/GLES, WebGL and iOS have not been validated. Unreal/Unity have not
  been run. macOS OpenGL results must not be generalized to Windows/Linux GL drivers.

## Measurements

Times in milliseconds. For baseline rows, CPU/GPU median is the median of the
three run medians; p95 is the largest run p95. HDR/Low rows are single-run values.
Peak is the largest observed sample. Each baseline row contains 360 samples;
each supplementary row contains 120. p95 uses inclusive linear interpolation.

| Backend / profile | Camera | CPU encode median | GPU interval median | GPU p95 | GPU peak |
|---|---|---:|---:|---:|---:|
| metal-baseline | PhysCamera001 | 1.107 | 1.733 | 2.187 | 2.865 |
| metal-baseline | PhysCamera002 | 0.424 | 2.156 | 2.678 | 3.620 |
| metal-baseline | PhysCamera004 | 0.845 | 1.817 | 2.429 | 3.261 |
| gl-baseline | PhysCamera001 | 65.584 | 65.240 | 67.926 | 90.705 |
| gl-baseline | PhysCamera002 | 66.030 | 65.735 | 71.251 | 91.527 |
| gl-baseline | PhysCamera004 | 65.513 | 65.108 | 69.733 | 92.319 |
| metal-hdr | PhysCamera001 | 1.982 | 1.829 | 1.976 | 2.173 |
| metal-hdr | PhysCamera002 | 2.065 | 2.211 | 2.349 | 2.549 |
| metal-hdr | PhysCamera004 | 1.947 | 1.893 | 2.061 | 2.191 |
| metal-low | PhysCamera001 | 1.408 | 1.399 | 1.510 | 1.568 |
| metal-low | PhysCamera002 | 1.362 | 1.581 | 1.774 | 1.862 |
| metal-low | PhysCamera004 | 1.455 | 1.463 | 1.606 | 1.756 |

## Checks

Both benchmark targets built without compiler warnings on macOS. Seven tooling
unit tests passed. The HDR regression fixture passed all seven GPU pixel probes and
22 shader variants on each backend after adding the timing API. All eight benchmark
runs completed, with 2880 total measured frames, no missing GPU results and no
material/geometry import warnings. Raw source notices remain in `importNotes`.

The comparison tool accepted matching repetitions and GL/Metal scene manifests;
unit checks reject mismatched scenes, incomplete runs and insufficient sample sets.
These checks validate the harness, not equivalence to an unmeasured external engine.

All three authored camera views were inspected. The additional PhysCamera002 run
with `MTL_DEBUG_LAYER=1` completed without Metal API validation errors; its 30 timing
samples are diagnostic only and excluded from the performance table. HDR and Low
captures were also inspected. Baseline GL/Metal atrium and balcony views match visually.
