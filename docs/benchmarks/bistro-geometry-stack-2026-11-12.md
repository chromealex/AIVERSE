# Bistro: native geometry optimization stack, 2026-11-12

All five implementation directions are available: native profiling, combined geometry A/B, static submission caches, shadow-specific selection, and larger Metal instance batches. They remain selective controls, not a promise that enabling every option accelerates every scene.

The measured CPU benefit of binding reuse is 5–8%; isolated cluster traversal costs 25–26%
on these overview cameras. The combined mode reduces draws by 27–41%, but does not demonstrate
a consistent GPU or frame-time improvement. These regressions are retained, not omitted.
The selective `static` combination reduces CPU submission/encoding time by 25–34%; frame
wall time remains approximately 10 ms, so no FPS improvement is established.

Asset and material adaptation match [the surface-proxy experiment](bistro-surface-proxies-2026-11-12.md): Amazon Lumberyard Bistro Exterior v5.2, CC BY 4.0, 2,832,120 triangles, 1296 source parts, medium shadows, 1280x720. Assets remain outside Git. This is a resident outdoor city scene, not a world-streaming/simulation benchmark.

Machine: Apple M3 Ultra, native ARM64 Release, Metal. Base commit `1607f3a9`; candidate binary SHA-256 `fee56df8a089e04f9771238aaec91b711e7d2c8086b317e246303e2c02ffa063`. Raw reports/captures are retained locally under `/private/tmp/aiverse-bistro/arm-final-throughput`; the adjacent JSON preserves run medians, p95 and representative pass counters.

## Method and limits

Final profiles each run in a fresh process: three paired repeats at three cameras, 180 warmup + 600 measured frames per phase, pair order reversed in repeat 2. The table uses median of run medians. GPU values are completed Metal command-buffer timestamps; CPU values measure submission/encoding. Captures occur after measurement. No concurrent compiler was observed at launch or spot checks; the shared desktop is not an isolated performance lab.

`AIVERSE_BENCH_UNCAPPED=1` disables presentation sync and requests a 1 ms timer. macOS scheduling, acquisition and presentation still affect wall intervals; these are not a perfectly uncapped loop. Normal application pacing is unchanged. Do not derive an FPS promise from these CPU/GPU medians.

Each off/on pair uses the same prepared data and source-node partition. All-mode off disables optional runtime selections; on enables LOD, HLOD, surfaces, clusters, shadow clusters, occlusion, static cache, 256-instance capacity and Metal binding cache. Always-on implementation improvements (such as rigid shadow-frustum tests) exist on both sides. This is not a direct old-engine/new-engine total-speedup measurement. Memory for both alternatives remains resident.

## Final paired results

Negative percentages mean less time. Cameras: 0 overview, 1 reverse overview, 2 far overview.

| Profile | Camera | GPU off → on, ms | GPU change | CPU off → on, ms | CPU change | Draws off → on |
|---|---:|---:|---:|---:|---:|---:|
| bindings | 0 | 1.981 → 2.001 | +1.0% | 3.074 → 2.921 | -5.0% | 3179 → 3179 |
| bindings | 1 | 1.728 → 1.741 | +0.7% | 3.089 → 2.904 | -6.0% | 3182 → 3182 |
| bindings | 2 | 1.530 → 1.535 | +0.3% | 3.179 → 2.912 | -8.4% | 3182 → 3182 |
| clusters | 0 | 1.761 → 1.779 | +1.0% | 3.080 → 3.890 | +26.3% | 3179 → 3179 |
| clusters | 1 | 1.724 → 1.767 | +2.5% | 3.117 → 3.935 | +26.2% | 3182 → 3182 |
| clusters | 2 | 1.595 → 1.614 | +1.2% | 3.118 → 3.902 | +25.1% | 3182 → 3182 |
| shadows | 0 | 2.069 → 2.021 | -2.3% | 3.141 → 3.771 | +20.1% | 3179 → 3179 |
| shadows | 1 | 1.657 → 1.652 | -0.3% | 3.102 → 3.862 | +24.5% | 3182 → 3182 |
| shadows | 2 | 1.556 → 1.546 | -0.6% | 3.124 → 3.779 | +21.0% | 3182 → 3182 |
| all | 0 | 6.385 → 6.401 | +0.3% | 2.838 → 2.882 | +1.5% | 3179 → 2313 |
| all | 1 | 6.425 → 5.984 | -6.9% | 2.658 → 2.794 | +5.1% | 3182 → 2074 |
| all | 2 | 6.340 → 6.381 | +0.6% | 2.801 → 2.413 | -13.9% | 3182 → 1874 |

All-mode tails and actual wall intervals:

| Camera | GPU pooled p95 off → on, ms | CPU pooled p95 off → on, ms | Wall median off → on, ms |
|---|---:|---:|---:|
| 0 | 10.398 → 7.061 | 3.310 → 3.498 | 9.988 → 9.992 |
| 1 | 7.036 → 6.754 | 3.348 → 3.231 | 9.996 → 9.994 |
| 2 | 7.086 → 7.013 | 3.405 → 2.858 | 9.996 → 9.991 |

## Baseline and exploratory evidence

The native ARM64 baseline at 1607f3a9 did not reproduce the earlier Rosetta late-session collapse in its three-repeat sequence. CPU medians were 3.43–3.62 ms and paced wall intervals about 10 ms. Surface-only GPU changes were -6.6%, -11.3%, -17.6%, with CPU changes +4.8%, +4.3%, +1.9%. This does not establish Rosetta as the cause of the prior collapse or prove long-session stability.

The later final `all` process measured approximately 6.4 ms GPU even in its off phases,
versus roughly 1.5–2.1 ms in earlier profiles. The prepared resident workload differs between
profiles, and host/GPU scheduling was not controlled sufficiently to identify the cause.
Do not compare absolute timings across profiles or claim that the original slowdown is solved.
The within-process paired data and its tails are provided above.

The first complete seven-profile sweep (60/120, normal pacing) overlapped other agents' compilation. Its CPU timings are retained in JSON but are not clean evidence of benefit. It exposed cluster draw-call growth from about 3180 to about 5040; the final implementation bounds splits and falls back to exact geometry. AutoLOD reduced main-pass geometry; HLOD reduced submissions; overview occlusion removed zero objects. Fewer triangles/submissions alone are not a speed result.

## Practical policy

Use the Metal binding cache and larger batches for compatible repeated geometry. Keep per-scene LOD/HLOD/surface choices subject to image review. Cluster and software-occlusion traversal have a CPU cost: do not enable them globally on these open overview views. A rejected fragmented cluster selection still costs CPU to evaluate. Shadow clusters remain opt-in and use the light matrix/resolution; masked casters retain fine geometry.

The portable backend remains at 32 instances. The larger Metal uniform path uses frame-owned buffer pages; unsupported backends retain their original path. AutoLOD preparation reuses the Sponza branch's pinned MIT meshoptimizer code. Reflection and spot views currently reuse the selected mesh LOD; they are not independent per-pass LODs.

Reproduction and API contracts: [geometry performance controls](../geometry-performance.md), [benchmark tool](../../tools/geometrybench/README.md).

## Selective static profile

A fresh process with the final binary (SHA-256 `33d944950d8d6f40a91763d72a2efbe6b14490e57a81dbb895ab10b72d0eaad6`) repeats the same 180/600 protocol. This profile combines LOD, HLOD, surface proxies, static/binding caches and larger instance capacity, without cluster or occlusion traversal. Off and on retain the same prepared resources.

| Camera | CPU off → on, ms | CPU change | GPU off → on, ms | Draws off → on |
|---|---:|---:|---:|---:|
| 0 | 2.601 → 1.958 | -24.7% | 8.288 → 8.295 | 3179 → 2313 |
| 1 | 2.574 → 1.859 | -27.8% | 8.231 → 8.172 | 3182 → 2074 |
| 2 | 2.585 → 1.699 | -34.3% | 8.107 → 8.180 | 3182 → 1874 |

CPU submission/encoding improves 25–34% in this paired test. GPU command-buffer intervals remain about 8.1–8.3 ms in both modes and wall medians remain about 10 ms. This confirms a CPU benefit, not a total-frame/FPS gain or resolution of the GPU/presentation anomaly. Full per-repeat medians and pooled p95 remain in the JSON.

## Validation

- Native ARM64 macOS editor, runtime and tools built; changed code compiled without warnings in the final build. Windows editor/client cross-compiled; earlier dependency rebuilds emitted existing warnings outside this change.
- 8885 engine checks, 618 modeling checks, 34 surface-proxy checks and seven offline AutoLOD tests passed.
- Catalog: 78 entries validated; ten library tests and relocated native consumers passed.
- Headless skirmish: Blue, 405 game-seconds, checksum `ae6a5687d79765c2`; mushrooms: Red, 92 game-seconds, checksum `a06caad4f1d88f10`.
- Actual Metal rendering/timing and paired captures exercised. Windows GL runtime, Linux, iOS, Android and WebGL were not exercised. Static screenshots do not validate moving-camera/light LOD transitions.

## Image and instance checks

Paired `all` and `static` captures were inspected at all three cameras. Buildings and foliage
remain present; localized differences affect small ornaments, distant silhouettes and shadows.
Static-profile RGB MAE (0..255) is 0.033 / 0.086 / 0.097; pixels with any channel difference
above 10 are 0.071% / 0.175% / 0.197%. These full-frame metrics include the empty background
and are not a substitute for close-range or moving-view review. Binding-cache captures are
byte-identical across all three cameras. Cluster-only and shadow-only difference metrics
are retained in JSON; their visual differences are small, and their measured CPU cost remains.

The separate repeated-mesh validation draws 1025 copies with ten primitives each. Increasing
the instance capacity from 32 to 256 changes 330 draws to 50. Both final Metal captures have
SHA-256 `9b69e54f357e3391e283974b9d614c1ef641b511820d70e084df29b7730c93bf` and were visually checked.
Both collected 120 GPU samples with no model-shader error. This single validation pair measured
GPU 0.607 → 0.598 ms and CPU 2.601 → 2.699 ms; it proves batching/correctness, not a stable speedup.
