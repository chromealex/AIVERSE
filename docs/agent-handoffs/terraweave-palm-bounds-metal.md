# Two-card palm impostors

Tested commit `8a64baa22f4226b9c1fa83c21f67ce9e12be0ebf` on `codex/forest-impostor-contour`. Planning commit 36ca815b7. Build tree on KINGSTON, x86_64 Release, Apple M3 Ultra. The test constructs a reproducible palm with a tapered 9 m trunk and fourteen arching fronds with individual leaflets. Both variants bake the same near mesh via the normal forest adapter.

The baker accumulates the union of opaque texels across all azimuths while resolving each layer. In eligible baked prototypes the bark spans at most one quarter of tile width and at least two thirds of height. The crown spans at least half the width and ends in the upper half of the tile. An eligible prototype gets one padded rectangle per layer. The legacy and ordinary broad-canopy paths keep full cards by default. Explicit native settings still override the selection. Geometry stays at four triangles; the CPU test covers every opaque texel in all eight azimuths. The oak fixture also verifies its original UVs.

## Metal comparison

288 instances, one 1024 × 640 target, eight camera azimuths at each orbit radius, static lighting, no wind. The two modes alternate order within each view. Each of three runs records 64 post-warmup command-buffer GPU samples per mode per radius. Table values are medians of the three run medians. The last column measures diagnostic shader coverage before alpha and depth rejection in an independent pass. It is not a production pipeline statistic.

| Orbit radius (m) | Original (ms) | Automatic (ms) | GPU time saved | Fragment coverage saved |
|---:|---:|---:|---:|---:|
| 80 | 0.146209 | 0.100584 | 31.2% | 74.6% |
| 140 | 0.108021 | 0.081250 | 24.8% | 75.6% |
| 220 | 0.086208 | 0.075000 | 13.0% | 75.8% |

The Metal check compares draw counts, RGB and silhouette masks against an empty render at every camera position. It allows at most 0.1% of reference foreground pixels (minimum four). Each mask uses a 16-level RGB difference from the background. The log gives per-distance maxima. Results apply to the procedural test palm on this GPU. Wind, mobile GPUs, authored game assets and the full merged-tree quick gate remain unverified. Keep broader rollout on the integration branch pending those checks.

Rerun the command recorded in `docs/evidence/terraweave-palm-bounds-metal.json` with the two test targets built from the tested checkout. `AIVERSE_COMPARE_PALM_BOUNDS=1` selects the orbit fixture. The checked-in capture is `docs/agent-handoffs/media/terraweave-palm-bounds.png`. The normal manifest-driven forest scene also passed: `ctest -R ^terraweave_forest_scene$` (1/1). Source changes are enumerated in the feature manifest.
