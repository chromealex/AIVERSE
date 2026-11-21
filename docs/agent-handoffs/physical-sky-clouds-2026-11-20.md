# Physical sky / volumetric clouds / god rays — stage 1

Date: 2026-11-20 (Europe/Moscow). Status: implemented opt-in presentation,
focused native verification complete; broader release validation remains.

## Revision and ownership

- Branch: `feature/physical-sky-clouds`.
- Base: `2f9c1b01ca07140dfecba2cf7d994881f1b5b5cf` (`feature/studio-html5`).
- Implementation: `64bb182bab3d485ddc3988d0fa7eca6ad53dbbf7`.
- Worktree: `/Users/aleksandrfeer/Projects/AIVERSE/.worktrees/physical-sky-clouds`.
- Source checkout's untracked Studio node_modules/lock/build-info preserved.
- No push, PR, CI run or merge performed. CI URL/status: none / NOT RUN.

User requested physical sky/clouds, then explicitly volumetric clouds + god rays.
PHYSIS remains authoritative weather; no terrain, water, ECS or genre gameplay
state was changed. Shared renderer batching/ICB and its experimental flags remain
unchanged. No editor UI migration or new ImGui panel.

## Changed files and behavior

- `engine/render/PhysicalSky.h`: bounded/sanitized settings, layered optional
  JSON parsing, quality budgets and RHI pass owner.
- `engine/render/PhysicalSky.cpp`: lazy shader/pipeline creation, safe separate
  read/write textures, linear HDR composition, wind presentation clock,
  backend depth conventions, sun shadow fallback and resource shutdown.
- `engine/render/PhysicalSkyShaders.h`: common GLSL/MSL spherical exponential
  Rayleigh/Mie single scattering, solar transmittance/sun disk, procedural 3D
  density, ray-marched cloud extinction and self-shadow, ambient sky illumination,
  phase function and cloud/geometry-shadowed volumetric god rays. Fixed spatial
  jitter reduces view-march banding without introducing frame-history ownership.
- `engine/render/PostFx.{h,cpp}`: optional native setters and
  `postfx.physicalSky`, pass before bloom/grade/underwater/tone mapping, target
  role 50, project/map configure reset. Existing settings APIs unchanged.
- `CMakeLists.txt`: source in both existing PostFx runtime source lists;
  default-built `aiverse_physical_sky_tests`, CTest `physical_sky`.
- `tests/render/physical_sky.cpp`: JSON/default/override/type checks,
  nonfinite/bounded settings, budgets, RHI lifetime, no texture feedback,
  failure latching and retry after shutdown. Always-on checks in Release too.
- `tests/render/HdrScene.h`: opt-in fixed-time fixture scenarios.
- `examples/render/physical-sky.json`, `docs/physical-sky.md`: activation/API,
  parameters, units, pipeline/quality limits and reproduction.
- `AGENTS.md`: area owner and continuation contract.
- `README.md`, `README.ru.md`, `README.de.md`, `README.es.md`, `README.fr.md`,
  `README.ja.md`, `README.ko.md`, `README.pt-BR.md`, `README.zh-CN.md`:
  matching rendering capability line.

Public additions: `PhysicalSkySettings`, `sanitizePhysicalSky`,
`parsePhysicalSky`, `physicalSkyBudget`, `PostFx::setPhysicalSky`,
`physicalSky`, `setPhysicalSkyTime`. Existing projects remain default OFF;
no binary save, authoritative component, network or asset format migration.
Native clients without PostProcess must opt in explicitly. HDR is required;
Medium needs explicit `hdr:on`. Low/Off or unavailable HDR keeps the old scene.

## Executed verification

All commands ran from the worktree above; builds are inside that isolated
worktree, never the shared checkout's build directory.

1. PASS native configure:
   `cmake -S . -B build/sky -DCMAKE_BUILD_TYPE=Debug -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_EDITOR=OFF -DAIVERSE_BUILD_TOOLS=OFF -DAIVERSE_BUILD_HEADLESS=OFF -DAIVERSE_BUILD_SERVER=OFF -DAIVERSE_BUILD_CLIENT=OFF -DAIVERSE_BUILD_HDR_TESTS=ON`.
2. PASS final native build:
   `cmake --build build/sky --target aiverse_hdr_scene aiverse_hdr_gl aiverse_physical_sky_tests -j 8`.
   Final log has no compiler errors/warnings. Earlier direct `c++ -std=c++20
   -Wall -Wextra -Werror -fsyntax-only -I. engine/render/PhysicalSky.cpp
   engine/render/PostFx.cpp` also passed before the final shader-body tuning.
3. PASS on implementation SHA above:
   `./run-tests.sh --no-build --build-dir build/sky --filter '^physical_sky$'`.
   CTest 1/1. Report `build/sky/test-reports/20261120-125426-69023/summary.txt`
   and `ctest.xml`. Earlier dirty-tree run also passed; this report has the
   committed revision. The entire repository suite was NOT RUN.
4. PASS exact shader extraction and compilers:
   `./build/sky/aiverse_physical_sky_tests /tmp/aiverse-sky-shader`;
   `glslangValidator /tmp/aiverse-sky-shader.vert /tmp/aiverse-sky-shader.frag`;
   `xcrun -sdk macosx metal -fmodules-cache-path=/tmp/aiverse-sky-metal-cache -c /tmp/aiverse-sky-shader.metal -o /tmp/aiverse-sky-shader.air`.
   Metal emits one unused-function warning for shared `sunDepth` from
   `AIVERSE_MSL_SUN_FILTER`; no shader errors. Initial default Metal module
   cache location was sandbox-denied; explicit /tmp cache solved it.
5. PASS actual Apple M3 Ultra Metal render/capture, 960x640:
   `AIVERSE_PHYSICAL_SKY_TEST=CASE AIVERSE_HDR_CAPTURE=/tmp/aiverse-sky-CASE.ppm AIVERSE_AUDIO=off ./build/sky/aiverse_hdr_scene`.
   Cases: clouds, clear, no-rays, sunset, inside, above, medium, ortho, night.
   All reported HDR, 3 post passes, zero Metal pipeline failures, successful
   capture and exit. Clouds/clear/sunset/inside/above/ortho/night images inspected.
   First sandbox GUI launch stalled on macOS XPC; terminated that exact process
   and ran authorized GUI tests outside sandbox. No engine workaround added.
6. PASS real macOS GL 4.1 / GL33 fixture:
   `AIVERSE_PHYSICAL_SKY_TEST=clouds AIVERSE_HDR_CAPTURE=/tmp/aiverse-sky-gl.ppm AIVERSE_AUDIO=off ./build/sky/aiverse_hdr_gl`.
   Image inspected. Metal vs GL mean absolute 8-bit channel difference =
   0.379733 over the complete fixture (includes scene/reflection differences).
   This is approximate image agreement, not exact pixel parity or Vulkan proof.
7. God-rays ON vs OFF at identical time/camera: 1,516,961 changed channels,
   mean absolute channel difference 33.124701. This confirms visible contribution,
   not a physical calibration or performance measurement.
8. PASS `python3 tools/readme/check.py`: 9 files, 0 problems.
   PASS `git diff --cached --check` before implementation commit.

Copied build/GPU logs: `build/sky/verification/`.
PNG captures: `build/sky/captures/{clouds,sunset,inside,above,medium,ortho,night,gl}.png`.
These are local validation artifacts, intentionally not committed.

Discovery issue: `python3 tools/skills/catalog.py search 'sky clouds atmosphere'`
failed because this base revision's catalog references unregistered/missing
`skills/editor/studio-ui/SKILL.md`. No unrelated catalog repair performed.

## Limits and next actionable work

- Single-scattering Earth sky, flat cloud slab, 50 km view cap; no planetary
  cloud shell/multiple-scattering LUT or moon/stars. Night is dark by design.
- Full-resolution procedural sampling is expensive; no timing speed claim,
  temporal accumulation, half-resolution depth-aware upscale or weather-map asset.
  Some light-march banding/noise remains, particularly at grazing sunset angles.
- Air/cloud composition is a bounded approximation; exact transparent-surface
  ordering is unavailable where scene depth does not represent transparency.
- Cloud shadows affect volume illumination/god rays, not surface materials.
  Environment IBL, reflection captures, fog/surface ambient and sun lighting
  remain host-owned. Avoid double haze with existing VolumetricLight/sunShafts.
- PHYSIS snapshots may feed public settings; automatic adapter is not installed.
- Dedicated Studio authoring panel, actual project/editor interaction tests,
  Vulkan/GLES/WebGL/Android validation and full repository tests: NOT RUN.

Next: validate this opt-in in an actual project's Scene/Game views using the
example JSON and HDR Medium/High, then run Vulkan/GLES fixtures. Before quality
promotion, profile full-resolution clouds and implement a separately verified
half-resolution depth-aware compositor/temporal resolve (keep histories per view,
reset on weather/time/camera changes). Add cloud transmittance to surface lighting
only as a separate stage with dedicated GLSL/MSL shadow/IBL parity tests. Preserve
this handoff and append a separate code-SHA entry after each further stage.

## Stage 2 — PHYSIS weather binding (2026-11-20)

Implementation: `50480303c9428e92f8f0b1f1346763d4b3d5bef1`, same branch
`feature/physical-sky-clouds`, parent `a4443b5b59f441d87d5208ebfc574874b72488c8`.
Stage 1 was subsequently pushed to origin at the user's request. This stage
continues the user's approved PHYSIS integration; no main merge or PR creation.

### API, ownership and changes

- Added `engine/world/PhysisSkyBridge.h`: `PhysisSkyPolicy`,
  `PhysisSkyFrame`, `physisSkySettings`, `PhysisSkyBinding::update/reset`,
  `applyPhysisSkyFrame`. Host-side bridge samples `const World` through existing
  `physis::sampleNatureField`; no changes or render dependency inside PHYSIS.
- Coverage maps humidity plus max rain/snow/storm precipitation; cloud extinction
  and haze are configurable visual multipliers. PHYSIS has no canonical cloud
  coverage, so this is an artistic transfer, not atmospheric calibration or a
  new climate solver. No time stepping, world/resource/component writes.
- Added `PostFx::setPhysicalSkyAdvection/clearPhysicalSkyAdvection` in
  `engine/render/PostFx.h`, configure/shutdown reset and forwarding in
  `engine/render/PostFx.cpp`. `PhysicalSky.{h,cpp}` accepts optional metre
  displacement with positive X/Z world motion; absence preserves prior manual
  time*wind behavior. Shader source/layout unchanged.
- Binding integrates previous committed wind by elapsed PHYSIS ticks. Repeated
  same-tick reads freeze advection. Wind changes preserve accumulated phase.
  Source token, decreasing tick, changed tick duration/cloud scale or >60-second
  gap resets cosmetic phase; caller must change source token for same-tick
  restores. One instance per source/view. Missing/outside-grid PHYSIS or invalid
  clock/token returns authored settings and clears advection on publish.
- `tests/render/physis_sky.cpp`: normalized precipitation mapping, invalid
  policy, real negative-coordinate World sample, wind/pause/change behavior,
  copy isolation, rollback, missing atmosphere, source change and two bindings.
  `tests/render/physical_sky.cpp`: actual uploaded displacement signs and
  manual-clock compatibility. `CMakeLists.txt`: default-built CPU bridge target
  `aiverse_physis_sky_tests`, CTest `physis_sky` in unified runner.
- `tests/render/HdrScene.h`: real World -> bridge -> PostFx dry/storm fixtures.
- `docs/physical-sky.md`: compiled API usage, units, lifetime and boundaries;
  root `AGENTS.md`: continuation contract.

No gameplay/network/save/schema changes; no TerraWeave dependency, automatic
PHYSIS install or automatic existing editor/RTS/FPS attachment. Native hosts
must explicitly call the bridge at their committed read barrier. Always retain
the original authored settings, not the previous mapped settings, as baseline.
The transient cloud offset intentionally resets on rollback; it is NOT an exact
persisted visual replay history. One sample controls the entire cloud slab;
spatial weather maps and temporal parameter smoothing remain future work.

### Actual validation and failures corrected

- Initial build command for the newly added target failed `No rule to make
  target aiverse_physis_sky_tests` before regeneration. Ran
  `cmake -S . -B build/sky` (PASS) to update the existing isolated Debug cache.
- First compile exposed four fixture `.tick` accesses on the pointer returned
  by `World::resource<T>()`. Corrected to `->tick`; no production change.
- PASS build: `cmake --build build/sky --target aiverse_physis_sky_tests
  aiverse_physical_sky_tests aiverse_physis_presentation_test aiverse_hdr_scene
  -j 8`. Final incremental build also passed after test additions.
- PASS strict syntax: `c++ -std=c++20 -Wall -Wextra -Werror -fsyntax-only -I.
  tests/render/physis_sky.cpp`.
- PASS final committed-source unified run:
  `./run-tests.sh --no-build --build-dir build/sky --filter
  '^(physical_sky|physis_sky|physis_presentation)$'`: 3/3, 0 failures.
  Exact SHA `50480303c9428e92f8f0b1f1346763d4b3d5bef1`, report
  `build/sky/test-reports/20261120-131057-81808/summary.txt` and `ctest.xml`.
- PASS Metal Apple M3 Ultra: `AIVERSE_PHYSICAL_SKY_TEST=physis-dry` and
  `physis-storm`, `AIVERSE_HDR_CAPTURE=/tmp/aiverse-sky-CASE.ppm`,
  `AIVERSE_AUDIO=off ./build/sky/aiverse_hdr_scene`. Both exit successfully,
  HDR/3 passes, zero pipeline failures. Both resulting PNGs visually inspected:
  dry almost-clear sky; high-precipitation dense overcast, suppressed sunlight.
  Captures in `build/sky/captures/physis-{dry,storm}.png` and logs in
  `build/sky/verification/`. These are synthetic host snapshots, not proof an
  existing game automatically installs/steps PHYSIS.
- PASS staged diff whitespace check. Full repository/CI, new-stage GL/Vulkan/
  mobile and actual game/editor attachment tests NOT RUN. No CI URL/run exists.

Next: explicitly attach one binding in a selected native host which already
installs PHYSIS, identify its source generation and real climate tick interval,
then exercise live pause/load/rollback and map transitions. Do not introduce
renderer dependencies into PHYSIS core or force climate on legacy games. For
spatial clouds, design a bounded read-only weather texture separately; preserve
this stage's deterministic simulation and transient-render ownership boundary.

## Stage 3 — local sphere/OBB volumetric fog and runtime reveal (2026-11-20)

Implementation commit: `ed9d9a6b0a7d6fc4458a41e3bae01400851f36cf`.
Branch: `feature/physical-sky-clouds`; isolated worktree
`/Users/aleksandrfeer/Projects/AIVERSE/.worktrees/physical-sky-clouds`.
User requested volumetric fog authored by radius or OBB and revealed at runtime.

### Changes and contracts

- `engine/render/LocalFog.{h,cpp}`: bounded per-view cosmetic scene, 8 fog + 8
  reveal regions; nonzero IDs, transactional validated upsert and removal.
  Spheres, normalized quaternion OBBs, inward feather, linear albedo and
  extinction in inverse world units. Overlaps add density; reveal uses maximum
  weighted strength (duplicate regions do not compound). CPU `densityAt` query
  shares shape/feather semantics. Moving/removing reveal restores local fog.
- `engine/render/LocalFogShaders.h`: common GLSL/MSL full-resolution HDR
  raymarch. Analytic sphere/OBB ray intersections, sorted/merged disjoint
  intervals, opaque-depth clipping, Beer-Lambert attenuation, ambient and
  anisotropic sun scattering using the existing sun shadow map. Medium/High
  use 32/64 samples per interval. No sampled/writable depth texture aliasing.
- `engine/render/PostFx.{h,cpp}`: native `setLocalFog`/`localFog` API, pass after
  sky and before bloom/tonemap, independent of sky enable. Configure/shutdown
  clear settings and regions; disabled by default, HDR Medium/High only.
- `tests/render/local_fog.cpp`: always-on checks for sphere/rotated OBB edges,
  partial/full/duplicate reveal, runtime move/remove, capacity/rejected updates,
  NaN/zero quaternion input, pipeline reuse/failure/reset and shader extraction.
  `tests/render/HdrScene.h`: opt-in real Metal/OpenGL fixtures, including actual
  reveal movement on frame 3 and capture on frame 5, hidden volume and inside view.
- `CMakeLists.txt`: both runtime source lists and default-built CTest target
  `aiverse_local_fog_tests` / `local_fog`. `docs/local-volumetric-fog.md`: API,
  usage, units, bounds and limitations. Updated sky docs, all 9 README capability
  lines and root AGENTS contract.

Public change is additive native rendering API. No map/game JSON schema, save,
network, PHYSIS simulation, TerraWeave, gameplay visibility/FoW or deterministic
ECS changes. Hosts must author/recreate transient regions after configure/load.
No Studio authoring window or automatic game/editor installation. Existing
physical sky APIs/shaders and PHYSIS bridge remain unchanged.

### Executed validation

- PASS `cmake -S . -B build/sky`, existing isolated Debug cache.
- PASS `cmake --build build/sky --target aiverse_local_fog_tests
  aiverse_hdr_scene aiverse_hdr_gl -j 8`, including final fixture changes.
  Log `/tmp/aiverse-fog-build-final.log`, copied into `build/sky/verification/`.
- PASS strict compile: `c++ -std=c++20 -Wall -Wextra -Werror -fsyntax-only -I.
  engine/render/LocalFog.cpp tests/render/local_fog.cpp`.
- PASS committed-source `./run-tests.sh --no-build --build-dir build/sky
  --filter '^(local_fog|physical_sky|physis_sky|physis_presentation)$'`: 4/4.
  Exact code SHA above; report `build/sky/test-reports/20261120-132204-92297/`.
- PASS `./build/sky/aiverse_local_fog_tests /tmp/aiverse-local-fog`,
  `/opt/homebrew/bin/glslangValidator /tmp/aiverse-local-fog.vert
  /tmp/aiverse-local-fog.frag`, and `xcrun -sdk macosx metal
  -fmodules-cache-path=/tmp/aiverse-sky-metal-cache -c
  /tmp/aiverse-local-fog.metal -o /tmp/aiverse-local-fog.air`.
  Metal emits only the existing shared sun-filter unused `sunDepth` warning.
- PASS real Metal Apple M3 Ultra: `AIVERSE_LOCAL_FOG_TEST=CASE
  AIVERSE_HDR_CAPTURE=/tmp/aiverse-fog-CASE.ppm AIVERSE_AUDIO=off
  ./build/sky/aiverse_hdr_scene` for `off`, `sphere`, `reveal`, `move`, `obb`,
  `obb-reveal`, `medium`, `ortho`, `combined`, `behind`, `inside`.
- PASS real OpenGL 4.1/GL33 Apple M3 Ultra, same command with `CASE=obb`,
  binary `aiverse_hdr_gl`, capture `/tmp/aiverse-fog-gl.ppm`.
  All runs exited successfully. Logs under `build/sky/verification/fog-*.log`;
  `sips` converted captures to `build/sky/captures/fog-*.png`. Inspected sphere,
  OBB, reveal, combined sky/fog, orthographic and camera-inside PNGs visually.
- Pixel comparisons on raw P6 RGB payloads (Python bytes split after third
  newline, absolute per-channel differences): `reveal` vs `off`, `obb-reveal`
  vs `off`, `move` vs `sphere`, `behind` vs `off` all **exactly identical**
  (mean/max delta 0). Sphere vs off mean delta 66.59576/255 confirms visible
  fog. GL vs Metal OBB mean delta 0.32459/255, max 25 (not exact backend parity).
- PASS `python3 tools/readme/check.py`: 9 files, zero problems.
- PASS `git diff --cached --check`. No failed builds/tests in this stage;
  expected Git LF/CRLF notices and Metal helper warning only.

CI/full repository suite, Vulkan/mobile, real game/editor attachment and GPU
performance measurements **NOT RUN**; no CI URL/status to report. Existing
transparent geometry without depth writes is composited at opaque depth, not
individually integrated. No fog self-shadowing, multiple scattering, local light
integration, reflection/IBL fog or temporal upsampling. Overlapping scales/thin
edges may undersample; sky/cloud/fog ordering is compositing, not unified media.
Full-resolution worst-case 8 disjoint ranges is bounded but potentially costly.

Next actionable step: select a native host/map asset owner, persist region
configuration there and recreate IDs after its configure/load barrier; feed
read-only presentation positions into reveal updates. Add Studio controls only
under the shared Studio UI contract on its required branch. Before raising
limits or shipping Low/mobile, profile real target scenes and verify Vulkan
shader/depth parity. Do not make fog a gameplay authority or force PHYSIS on.

## Main integration — 2026-11-20

User authorized main merge. Code `1397dfb52f163bb234b10692411bfe6d3311c8aa`
on `codex/physical-sky-main`, based on main `f9aed9a705982e2c6b77b29869e309995ce8b163`.
Imported only sky/PHYSIS/fog changes, excluding unrelated Studio ancestry;
canonical owner is now `Prisma`, legacy `PostFx` remains its alias. Targets 200/201,
atmosphere before exposure/temporal, native APIs remain compatible. Fixed main's
missing Metal method brace. PASS 7 targeted CTests, 54 Metal HDR probes, Metal
sky/fog/reveal and GL OBB smoke. Full commands, failed initial merge/build,
limitations and next step are in
`docs/handoffs/prisma-stage-physical-sky-main-2026-11-20.md`. Full suite, editor/
client, Vulkan/mobile and CI NOT RUN. Feature branch history remains unchanged.

## Structured fog continuation — 2026-11-20

Code `1fd380b6fe0ad1325162bc3c0a32e5c3eb1dab53`, branch
`codex/structured-volumetric-fog`: opt-in 3D density noise, world-Y falloff,
wind and explicit fog presentation clock, keeping sphere/OBB reveal semantics.
Two user-reference directions have native synthetic forest/valley fixtures.
PASS 7/7 targeted tests, GLSL/MSL compilers, Metal/GL rendering and exact full
reveal/off comparisons. Uniform legacy image differs by only 3 one-level channel
roundings; not bit-identical. See `docs/handoffs/prisma-stage-structured-fog-2026-11-20.md`
for commands, limitations and visual tuning. Not merged to main in this stage.

## Fog billows/self-shadowing — 2026-11-20

Code `19d2d8f1a2f2357b235dc9ef6dd08da70d243cb9`, same structured-fog branch.
User rejected flat appearance: added world-space height crests, 3D domain warp,
edge erosion and bounded 4/6-sample optical sun self-shadowing through all fog/
reveal volumes. Zero defaults preserve compatibility. Native fixtures now expose
bank surfaces at low/elevated angles and A/B self-shadowing. PASS 7/7 CTests,
GLSL/MSL, Metal/GL, full reveal exact-pixel and shadow/wind difference checks.
See `docs/handoffs/prisma-stage-fog-billows-2026-11-20.md` for evidence/limits.
**All future builds/caches/compiler temporary files on KINGSTON**, per user:
`/Volumes/KINGSTON/AIVERSE-builds/physical-sky-clouds/sky`; local build/sky is only
an alias. Internal duplicate removed after external compilation. No main merge.
