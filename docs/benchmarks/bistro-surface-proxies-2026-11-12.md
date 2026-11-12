# Bistro: baked surface proxies, 2026-11-12

Result: opt-in surface replacement lowers median GPU time in this static city scene,
but repeated late-session regressions prevent a claim of stable acceleration or frame-rate gain.

Asset: [Amazon Lumberyard Bistro Exterior v5.2](https://developer.nvidia.com/orca/amazon-lumberyard-bistro),
Amazon Lumberyard / ORCA, CC BY 4.0. Loaded **2,832,120 triangles**, 1296 source parts.
This is an outdoor city block, not terrain/world-streaming or simulation coverage.
Assets and their original license remain outside Git.

Machine/backend: Apple M3 Ultra, Metal, 1280x720; **x86_64 Release executable on Apple Silicon**.
CPU numbers include execution through Rosetta and do not represent a native ARM64 build.
Source and proxy use identical cameras, source-node decomposition, materials and medium shadows;
fog is disabled. No GI, TAA, world streaming, automatic HLOD, cluster selection or the separate
Sponza branch's meshoptimizer AutoLOD. This isolates the new proxy substitution; it does not
measure the gain on top of the entire optimization stack.

Final policy: box proxies, 64 texels per face, switch below **32 projected pixels**;
retain alpha-masked source geometry (including foliage). Bake candidates have 512..100000
triangles. Unsupported emissive materials remain original. 332 eligible parts were prepared;
only 141 / 173 / 211 were selected in the three views.

Six paired repeats per camera in two fresh processes (three repeats each), 60 warm-up +
120 measured frames per phase; reversed pair order in repeat 2 of each batch. Each batch
shares process/resources, with no concurrent benchmark. The confirmation batch had no concurrent builds. GPU timestamps are
asynchronous completed Metal command-buffer intervals, not inferred from FPS. Captures happen
after timing. Medians below are the median of the six run medians.

| Camera | GPU source → proxy, ms | GPU change | CPU encode source → proxy, ms | Visible input triangles | Draw calls |
|---|---:|---:|---:|---:|---:|
| Overview | 1.724 → 1.590 | -7.7% | 4.252 → 4.208 | 2,827,889 → 2,419,748 | 3179 → 3089 |
| Reverse overview | 1.622 → 1.449 | -10.7% | 4.285 → 4.223 | 2,832,120 → 2,288,530 | 3182 → 3018 |
| Far overview | 1.457 → 1.286 | -11.8% | 4.297 → 4.156 | 2,832,120 → 2,107,774 | 3182 → 2912 |

Visible triangles are CPU-counted main-view inputs after per-part frustum selection, not a GPU
hardware count or shadow-pass total. Draw calls are the renderer counter including shadows.
Aggregate frame wall medians are **9.7–10.0 ms**, but late runs reached 17–24 ms.
The median GPU reduction is only 0.13–0.17 ms;
CPU submission and platform/presentation pacing remain material. No uncapped FPS improvement
is claimed. GPU resources for both alternatives stayed resident; memory savings were not tested.

Both processes slowed late in the third repeat, including the source-only mode. The cause is
unresolved. Proxy GPU run medians then regressed by 14–31% in the reverse/far views, rather
than improving. Pooled GPU p95 source → proxy: overview 1.862 → 1.810 ms, reverse
1.786 → 2.370 ms, far 1.607 → 2.381 ms. These samples are retained, not discarded as outliers.
The median benefit therefore does **not** demonstrate stable acceleration during a long session.

Visual review: paired views were inspected. The conservative setting retains vegetation
and the overall shape, with localized changes on small ornaments, ledges and shadows. Images
are not identical. RGB MAE is 0.028 / 0.081 / 0.089 on a 0..255 scale; pixels with any channel
error >8 are 0.065% / 0.175% / 0.194% of the frame. Background dilutes full-frame metrics;
these numbers do not certify perceptual equivalence or moving-camera transition stability.

An earlier aggressive diagnostic (96 pixels, foliage eligible) reduced GPU time by 29–41%,
but visibly distorted small shrubs/decorations. It is **not** accepted as an equivalent-quality
speed win. Trial low cameras that faced exterior backs/walls were replaced with overview cameras;
only the final fixed overview set is tabulated here. Earlier raw reports are retained separately.

Bistro-specific material adaptation follows its README: Specular textures are ORM (131 materials),
132 DirectX normal maps have green inverted, and foliage uses alpha mask. Both modes use the
same adaptation. The importer reports one ignored source-camera/light notice and 1296 extra-UV
notices; only UV0 is used. This is the AIVERSE-imported workload, not parity with Falcor lighting
or all source FBX features. The original HDR environment is not rendered.

Reproduction and definitions: [geometrybench](../../tools/geometrybench/README.md).
Raw reports: `/private/tmp/aiverse-bistro/final/` and `/private/tmp/aiverse-bistro/confirm/`;
combined six-run reports: `/private/tmp/aiverse-bistro/combined/`. Captures, source/binary hashes
and image metrics are in `final/`.
Source FBX and archive: `/private/tmp/aiverse-bistro/assets/` and `Bistro.zip`.
The accompanying summary JSON retains all six per-run medians and pooled p95 values.
Windows, OpenGL, mobile and WebGL were not measured. Keep surface proxies opt-in and inspect
actual content; these results do not justify a Nanite implementation by themselves.
