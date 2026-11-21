# Lighting acceptance — 2026-11-20

Branch: integration/lighting-main-stage1-20261120.
Final rendering code: 7f0bbd633228d4dc27248f4da82d670044a05c42.
Runner/catalog integration: 6ce5b19c061ec16a450f60bc475e0e9f6e6674fe.

## Delivered scope

- Shared CPU and bounded GPU probe GI, Metal/OpenGL PBR/terrain/Shader Graph
  receivers, detach/update/removal, actual Metal ICB dynamic-GI bindings.
- Native Luma Metal HDR receiver composition, authored source removal, shadow
  and material tests; explicit unsupported/capacity fallback.
- Shared posed rigid/skin/morph geometry snapshots and native animated host.
- Studio renderLighting v1 controls, validation, native Save/Undo/Redo,
  actual CPU/GPU/Luma viewport status; FPS negative-coordinate bounds/camera
  and material-preview transitions. Existing terrain/document proxy geometry.
- Release arm64 Studio and game client builds; Debug legacy editor build.

## Verification actually executed

All build/dependency/temp/capture/report output is on KINGSTON:
`/Volumes/KINGSTON/AIVERSE/Builds/.temp/lighting-completion-20261120`.

Full quick run report `build/test-reports/20261120-155447-18337`:
313/316 passed, three skill metadata tests failed before the integration fix.
After that fix, CTest skill_catalog/routing/audit: 3/3 passed. No unresolved
failure remains from that run. This is a full baseline plus focused retests,
not a claim of a single clean full-suite run. Main engine test: 840.79 seconds;
full CTest: 998.66 seconds. Stage30 final code: four targeted Debug Studio
checks and actual Release Metal Studio check passed.

Release arm64 ten lighting GPU/host/animated/ICB tests passed. Stage25 web
57 tests, typecheck/build and Python UI contracts 19/19 passed; Stage26/27
web build/typecheck/UI contracts also passed. Exact commands and failed build
attempts are in Stage25–30 handoffs. Stage29 full Debug build initially found
two SpectraFX Viewport retirement call incompatibilities; fixed and rebuilt.
No CI was dispatched and no remote branch was pushed.

## Release timing evidence — exploratory, not isolated acceptance

Actual Apple M3 Ultra, Release arm64. Binary reports configure-time revision
`v2.0.0-7009-gb247d4228`; benchmark/renderer sources did not change in the later
catalog/Studio stages. Fixed camera/pose, HDR, 30 warmup +120 measured frames
per run, OFF/ON/OFF, real GPU timestamps. A separate lens-completion build was
observed during sampling and was not stopped. Therefore CPU/GPU numbers below
are exploratory and do not establish production performance or a speedup.
Submitted-frame timings exclude asset loading and convergence-only updates;
window pacing is not FPS. No experimental ICB flag was enabled for these runs.

### Sponza 640x360

| Run | CPU encode mean / P50 / P95 ms | GPU mean / P50 / P95 ms | Load/upload ms | GI convergence ms |
|---|---|---|---|---|
| A1-off | 0.483 / 0.416 / 1.072 | 2.026 / 2.018 / 2.111 | 29750.090 | 0.000 |
| B-on | 1.445 / 1.337 / 2.029 | 2.116 / 2.115 / 2.252 | 30042.651 | 256.140 |
| A2-off | 0.476 / 0.422 / 0.943 | 2.198 / 2.019 / 2.228 | 29441.502 | 0.000 |

GPU P50 baseline drift: 0.012%. Raw recipes, reports, logs and captures: `/Volumes/KINGSTON/AIVERSE/Builds/.temp/lighting-completion-20261120/release-sponza-aba`.

### Luma 128 sources, 1280x720

| Run | CPU encode mean / P50 / P95 ms | GPU mean / P50 / P95 ms | Load/upload ms | GI convergence ms |
|---|---|---|---|---|
| A1-off | 0.146 / 0.147 / 0.167 | 0.445 / 0.408 / 0.679 | 0.127 | 0.000 |
| B-on | 0.166 / 0.166 / 0.191 | 0.855 / 0.853 / 0.902 | 764.661 | 0.000 |
| A2-off | 0.108 / 0.109 / 0.124 | 0.467 / 0.397 / 0.719 | 0.119 | 0.000 |

GPU P50 baseline drift: -2.723%. Raw recipes, reports, logs and captures: `/Volumes/KINGSTON/AIVERSE/Builds/.temp/lighting-completion-20261120/release-luma-aba`.

Sponza retained 3,738,878 opaque GI triangles (3,747,018 total scene triangles)
and correctly selected CPU because the full geometry exceeds bounded GPU
capacity. No arbitrary caster dropping. Initial GI convergence: 256.140 ms.
Actual B-on.png was visually inspected: complete courtyard, textures/shadows,
no blank frame. Metal reported zero pipeline failures. Luma uses a four-triangle
receiver fixture, 128 sources, GPU backend; legacy and Luma differ in lighting
quality, so their timing difference is not an equal-quality speed comparison.

## Remaining acceptance and scope limits

- Android/Vulkan hardware rendering: NOT RUN. User explicitly confirmed no
  available test stand. Portable contracts/SPIR-V checks are not hardware parity.
- Interactive native/WKWebView lighting smoke: PASS after user unlocked Mac
  (Stage32). Actual 1440x928 screenshots inspected, native terrain and light
  gizmo visible. Auto/GPU → CPU → Off and Undo restored CPU; disabled GI fields,
  Add light, Undo/Redo and native Save verified. Disk JSON contains CPU/Luma and
  the authored source. Screenshots are in the CUA conversation evidence, not
  exported files. This is the RTS lighting smoke, not full editor UI parity or
  an exhaustive browser/240px/error-state matrix.
- Isolated production scene CPU/GPU performance: NOT ACCEPTED; repeat the
  provided runner without other compilation/render workloads.
- Studio currently lights terrain/document proxies, not full game asset preview
  parity. Native FPS/RTS games can compose the shared runtime; this work does
  not claim automatic full gameplay-host adoption or real-time animation cost.
- Animated benchmark holds a selected pose until GI converges. Production hosts
  must choose/update their own cadence. Geometry is opaque-triangle based;
  alpha masks, transparency and Shader Graph displacement are not GI casters.
- Low-resolution Luma shadow denoising has documented softness limits. GPU
  capacity/fallback remains explicit. Experimental ICB stays OFF by default.

These external checks and production integration limits must not be described
as complete merely because local tests pass. Next work when resources are
available: run Android/Vulkan device acceptance and measure representative
gameplay workloads without background load before enabling production defaults.
