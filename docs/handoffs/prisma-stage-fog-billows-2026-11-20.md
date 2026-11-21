# PRISMA — volumetric fog billows and sun self-shadowing (2026-11-20)

Branch: `codex/structured-volumetric-fog`.
Code: `19d2d8f1a2f2357b235dc9ef6dd08da70d243cb9`.
Previous handoff: `prisma-stage-structured-fog-2026-11-20.md`.
User rejected the flat-looking preview and requested completion. During work,
user explicitly required **all builds on KINGSTON**; see storage contract below.

## What changed

- `LocalFog.h/.cpp`: appended validated `FogStructure::heightVariation`, `warp`,
  `edgeErosion`. Height profile crests vary across XZ; domain warp distorts all
  three noise coordinates; edge erosion raises the density threshold near the
  soft sphere/OBB boundary. CPU `densityAt` uses the same definition as the GPU.
  Zero defaults preserve previous uniform/structured semantics. Layout changes
  require native consumers to rebuild; no persisted map/network format changes.
- `LocalFogShaders.h`: real bounded optical-depth integration toward the sun.
  Every sample includes all active fog densities and max-union reveal holes.
  Beer-Lambert transmission attenuates direct sun scattering, multiplied by the
  existing geometry shadow map. Quadratic light-step spacing, 4 steps Medium /
  6 High, configurable distance .1..1000 world units. New `selfShadow` (0..1)
  defaults OFF; ambient fill is unchanged. No 2D fake shadow overlay, new texture
  ownership, compute, temporal history or gameplay data was introduced.
- UBO 2112 -> 2256 bytes; GLSL/MSL share source. 8+8 region cap, HDR gating,
  depth clipping and 48/96 structured view-ray steps remain unchanged.
- `tests/render/FogLandscape.h`: authored overlapping sphere banks with eroded
  edges plus a weak OBB haze, rather than one deep box filling the entire view.
  Added above/unshadowed cases. Explicit fixture parameters now show upper
  surfaces, open sightlines and internal shading. Geometry remains synthetic.
- `tests/render/local_fog.cpp`: new-field rejection, warped/variable-height
  periodic advection, erosion reducing edge density without escaping bounds,
  2256-byte finite UBO and prior reveal/GPU lifetime contracts.
- Docs/AGENTS: public controls, native example, limitations and mandatory external
  build storage. PHYSIS/gameplay visibility/editor UI/save formats untouched.

## Visual iterations and constraints

Initial height/warp-only captures were still too uniformly filled. Separate
bounded banks exposed their depth but first looked like smooth white spheres.
Added boundary density erosion, removed the uniform density floor and tuned
light/preset scale. Final fixture adds very low-density connecting haze; a
forest camera which landed behind a tree was restored to its unobstructed yaw.
Final low and elevated cameras were rendered on Metal. These demonstrate actual
volume geometry and lighting, not parity with the user's finished scene assets.

A local patch/build attempt failed with `no space left on device` before the
patch was applied. Retried after free space returned. No code build/test failure
remained. User then required KINGSTON; all subsequent build/cache/compiler-temp
and shader-output operations were moved there. Do not resume local builds.

## Build storage (persistent user requirement)

Source worktree stays:
`/Users/aleksandrfeer/Projects/AIVERSE/.worktrees/physical-sky-clouds`.
Build root: `/Volumes/KINGSTON/AIVERSE-builds/physical-sky-clouds/sky`.
Set `TMPDIR=/Volumes/KINGSTON/AIVERSE-builds/physical-sky-clouds/tmp` and
`CLANG_MODULE_CACHE_PATH=/Volumes/KINGSTON/AIVERSE-builds/physical-sky-clouds/modules`.
Use the same modules directory for `xcrun metal -fmodules-cache-path=...`.

Copied prior artifacts/reports to KINGSTON, excluded the old CMakeCache, then
configured fresh with source above and Debug/tests/HDR ON, editor/tools/headless/
server/client OFF. CMake confirms AIVERSE_BINARY_DIR/CMAKE_CACHEFILE_DIR on KINGSTON.
After successful external compilation, removed the duplicate internal build and
replaced worktree `build/sky` with a symlink to the external build; previous
capture links remain usable. Do not delete unrelated build directories or silently
fall back to the internal disk if KINGSTON is absent.

## Executed verification

All final paths below are relative to
`/Volumes/KINGSTON/AIVERSE-builds/physical-sky-clouds/` unless absolute.

- PASS external CMake configure (`configure.log`). PASS `cmake --build .../sky
  --target aiverse_hdr_scene aiverse_hdr_gl aiverse_local_fog_tests
  aiverse_physical_sky_tests aiverse_physis_sky_tests
  aiverse_physis_presentation_test aiverse_hdr_policy aiverse_prisma_graph_tests
  aiverse_prisma_volume_tests -j 8` (`build.log`). Final fixture rebuild is in
  `final-build.log`, final CPU-test rebuild in `final-tests-build.log`.
- PASS strict `c++ -std=c++20 -Wall -Wextra -Werror -fsyntax-only -I.
  engine/render/LocalFog.cpp tests/render/local_fog.cpp`, external TMPDIR/cache.
- PASS exact committed-source `./run-tests.sh --quick --no-build --build-dir
  /Volumes/KINGSTON/AIVERSE-builds/physical-sky-clouds/sky --filter
  '^(local_fog|physical_sky|physis_sky|physis_presentation|hdr_policy|prisma_graph|prisma_volume)$'`:
  **7/7**, `sky/test-reports/20261120-142116-62323/`, code SHA above.
- PASS shader extraction via `sky/aiverse_local_fog_tests .../local-fog`,
  glslangValidator on `local-fog.vert/.frag`, and xcrun Metal compilation of
  `local-fog.metal` to `local-fog.air`, all on KINGSTON. Only shared helper
  unused-sunDepth warning; no shader errors.
- PASS Metal Apple M3 Ultra: `AIVERSE_LOCAL_FOG_TEST=CASE AIVERSE_AUDIO=off
  AIVERSE_HDR_CAPTURE=.../sky/captures/final-CASE.ppm .../sky/aiverse_hdr_scene`.
  Cases: forest, valley, forest-above, valley-above, forest-unshadowed,
  valley-unshadowed, forest-off, forest-reveal, valley-off, valley-reveal,
  valley-wind. Re-ran forest variants after final yaw correction.
- PASS OpenGL GL33/Apple M3 Ultra: valley with `sky/aiverse_hdr_gl`, capture
  `sky/captures/final-valley-gl.ppm`. Logs `sky/verification/final-*.log`.
  PNG low/elevated captures inspected with view_image during the iteration.
- Raw P6 RGB comparison: forest-reveal/off and valley-reveal/off EXACT equality.
  Self-shadowed minus unshadowed full-image mean channel change (0..255):
  forest -0.238024, valley -1.860125; valley max absolute change 131. Thus the
  light integration measurably darkens intervening-density regions, not opacity.
  Valley wind-at-8s mean absolute change 4.37850, max181. GL/Metal valley mean
  absolute difference .33255, max19; not bit parity.
- PASS README checker (9 files), whitespace checks.

Full suite, editor/client, Vulkan/mobile, GPU performance and animated temporal
quality NOT RUN. No CI result claimed. Self-shadowing is finite-distance,
low-sample single scattering: distant blockers can be omitted, thin structures
can undersample. Ambient occlusion/multiple scattering/fog shadows on surfaces
and transparent-depth integration are NOT implemented. Height is still world Y,
not automatic terrain following. Default parameters remain compatible and opt-in.

Next: review the public bank settings in the real host scene, profile the extra
light marching on target hardware, and verify temporal/moving-camera quality
before raising default budgets. Continue per-stage handoff; this branch is not
an additional automatic main merge.
