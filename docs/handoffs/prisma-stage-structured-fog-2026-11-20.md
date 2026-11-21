# PRISMA — structured fog from visual references (2026-11-20)

Branch: `codex/structured-volumetric-fog`, base `5aba82c35` (published main).
Code: `1fd380b6fe0ad1325162bc3c0a32e5c3eb1dab53`.
User supplied two references: dense broken valley banks, then thin forest ground
mist. Continued existing fog feature; no unrelated rendering/gameplay migration.

## Implementation

- `LocalFog.h/.cpp`: appended `FogVolume::structure` (amount, anisotropic cell
  scale, coverage, softness, world-Y base/falloff, wind). All inputs validated
  transactionally at upsert. Defaults preserve uniform fog. CPU `densityAt(p,t)`
  includes height/noise/reveals; nonfinite/outside queries never enter lattice
  integer conversions. Bounded periodic advection supports long session clocks.
- `LocalFogShaders.h`: identical GLSL/MSL periodic uint lattice hash and three
  octaves of smooth 3D value noise. Noise/height shape density before reveals;
  positive wind moves density along positive world axes. World-anchored noise
  is independent of boundary rotation/movement. No smoke cards or new textures.
- Uniform block grows from 1728 to 2112 bytes (three 8-entry vec4 arrays).
  Structured/height-shaped scenes use 48/96 steps per merged interval and a
  static per-pixel sample offset; uniform scenes retain midpoint 32/64 steps.
- `Prisma.h/.cpp`: independent steady fog clock and `setLocalFogTime` override;
  configure/shutdown reset clock. Existing pass order/lifetime is preserved.
- `tests/render/local_fog.cpp`: bounded variation, world-space wind translation,
  256-cell periodic time, huge finite time, rejected scale/wind, exponential
  height profile and reveal/restore, plus updated UBO/lifetime checks.
- `tests/render/FogLandscape.h` and `HdrScene.h`: opt-in asset-free forest and
  valley previews with -off/-reveal/-wind variants. Simple terrain/cones/rocks
  intentionally demonstrate density and occlusion, not reference asset parity.
- Documentation: local fog parameters and sample, PRISMA continuation, AGENTS.

Old aggregate initializers/call expressions remain source-compatible via appended
fields/default arguments; C++ consumers must rebuild (struct/UBO layout changed).
No save/network/map JSON schema, editor controls, physics/FoW or PHYSIS state
changes. This remains a rendering feature under PRISMA, disabled unless opted in.

## Actual tests and visual iteration

- PASS native build `cmake --build build/sky --target aiverse_local_fog_tests
  aiverse_hdr_scene aiverse_hdr_gl -j 8`; final log
  `/tmp/fog-structure-build-final.log`.
- PASS rebuilt regressions: `aiverse_physical_sky_tests aiverse_physis_sky_tests
  aiverse_physis_presentation_test aiverse_hdr_policy aiverse_prisma_graph_tests
  aiverse_prisma_volume_tests` with the same CMake build command.
- PASS strict `c++ -std=c++20 -Wall -Wextra -Werror -fsyntax-only -I.
  engine/render/LocalFog.cpp tests/render/local_fog.cpp`.
- PASS `./build/sky/aiverse_local_fog_tests /tmp/fog-structure`,
  `/opt/homebrew/bin/glslangValidator /tmp/fog-structure.vert
  /tmp/fog-structure.frag`, `xcrun -sdk macosx metal
  -fmodules-cache-path=/tmp/aiverse-sky-metal-cache -c /tmp/fog-structure.metal
  -o /tmp/fog-structure.air`. Only shared helper unused-sunDepth warning.
- PASS committed-source unified run `./run-tests.sh --quick --no-build
  --build-dir build/sky --filter
  '^(local_fog|physical_sky|physis_sky|physis_presentation|hdr_policy|prisma_graph|prisma_volume)$'`:
  7/7; `build/sky/test-reports/20261120-135048-10003/`, code SHA above.
- Actual Metal M3 Ultra GPU runs: `AIVERSE_LOCAL_FOG_TEST=CASE
  AIVERSE_HDR_CAPTURE=/tmp/structured-CASE.ppm AIVERSE_AUDIO=off
  ./build/sky/aiverse_hdr_scene` for forest, forest-off, forest-reveal,
  forest-wind, valley, valley-off, valley-reveal, valley-wind and sphere.
  OpenGL: same env with CASE=forest and `aiverse_hdr_gl`, output suffix forest-gl.
  All final runs exit 0. Logs copied to `build/sky/verification/structured-*`.
- Initial synthetic captures were visually too opaque/white; reduced densities,
  changed coverage/ambient and made simple fixture geometry unlit for readable
  silhouettes. Added structured sampling budget/jitter. Rebuilt/re-ran all cases.
  Final `build/sky/captures/structured-{forest,valley}.png` visually inspected.
- Raw P6 pixel tests: forest-reveal vs forest-off and valley-reveal vs valley-off
  EXACT equality. Wind at 8s changes mean channel value by 4.48878 (forest) and
  11.18015 (valley), 0..255 scale. GL/Metal forest mean difference .31048, max13.
- Legacy uniform sphere comparison initially used exact-byte equality and failed
  on only 3 channels differing by 1/255 (mean .0000016276). This is not exact
  backend bit parity. Verified bounded rounding tolerance max<=1, mean<.00001;
  no geometric/appearance regression was observed. Do not report this as exact.
- PASS README checker (9 files) and staged whitespace checks.

## Limits / continuation

Full suite, editor/client, Vulkan/mobile, performance, dynamic multi-camera fog
and temporal animation quality NOT RUN; no CI result claimed. Noise is bounded
procedural value noise, not fluid simulation. Height is world Y, not terrain
sampling; choose regions per elevation for hills. Wind parameter changes at a
fixed absolute time may change phase; no continuous variable-wind integrator.
No fog self-shadowing, multi-scattering or transparent-depth integration added.
Fine noise/thin layers can still undersample and incur significant ALU cost.

Feature is in a separate branch, not a new automatic main merge. Next: review
both profiles in the intended real scene, tune density/world scale and lighting,
then measure GPU cost and test animated fog with canonical temporal resolve.
Do not present synthetic trees/rocks as matching the user's finished-scene assets.
