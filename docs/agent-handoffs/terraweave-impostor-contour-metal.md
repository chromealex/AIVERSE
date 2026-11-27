# Arbor far tree contour experiment

Tested commit `d050984baf6d47c6d143a00585cff659c72aeb55` on branch `codex/forest-impostor-contour` (Apple M3 Ultra, arm64 Release, Metal). Planning commit 42fddb5012b6ededad1f78f1d0e02c2e504ce329.

`ArborForestAdapter::setImpostorContourBands(3 or 8)` applies to subsequently added prototypes. Zero selects the original two cards. At bake time, each horizontal band encloses nonzero alpha for all azimuths, with a two-texel border. Bark and foliage keep independent primitives. Layers retaining 80% or more of full-card area use the original rectangle. No scene data migration or per-instance allocation.

## Results

One seed and species, 288 trees, one 1024 × 640 target, static camera, no wind, identical atlas and render settings. Each repeat interleaves three modes, drops 12 warmup frames and takes 32 timed samples per mode. GPU command-buffer times cover the offscreen forest frame. Fragment counts come from a separate counting shader replaying projected triangles with alpha and depth rejection disabled. They measure coverage prior to rejection. Metal exposes only its timestamp counter set on this device.

| Geometry | Draw calls | Diagnostic fragments | GPU medians across runs (ms) | Median of medians (ms) |
|---|---:|---:|---|---:|
| cards | 6 | 2,374,772 | 0.124604, 0.122958, 0.126146 | 0.124604 |
| three | 6 | 1,839,665 | 0.126501, 0.128000, 0.130083 | 0.128000 |
| eight | 6 | 1,097,941 | 0.163104, 0.165271, 0.167229 | 0.165271 |

CPU coverage checks inspect opaque texel centres for both layers across eight azimuths. GPU checks compare captured color, draw counts and fragment coverage. The test log records differences exceeding 16 RGB levels. The permitted difference is 0.1% of pixels. The grove uses the acceptance fixture's tree profile. Palm-specific foliage, moving wind, winter, other devices and backends have not been benchmarked.

Keep the experiment opt-in. Promotion requires a reproducible GPU-time benefit on the target assets and devices and the main integration gate. No full merged-tree gate or site publication in this experiment.

## Reproduction

Build CMake targets `aiverse_terraweave_metal_forest_test` and `aiverse_terraweave_model_test` on KINGSTON. The complete invocation and build path are recorded in `docs/evidence/terraweave-contour-m3-ultra.json`. Set `AIVERSE_COMPARE_FOREST_CONTOURS=1` to run the comparison and `AIVERSE_TERRAWEAVE_FOREST_CAPTURE` to a writable capture prefix. Stage paths are listed in the feature manifest.
