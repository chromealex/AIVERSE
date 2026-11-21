# PRISMA — physical sky/clouds/fog integration into main (2026-11-20)

User explicitly requested merging the completed feature into main.
Integration branch: `codex/physical-sky-main`.
Base: `f9aed9a705982e2c6b77b29869e309995ce8b163`.
Code commit: `1397dfb52f163bb234b10692411bfe6d3311c8aa`.

## Scope and adaptation

The published `feature/physical-sky-clouds` branched from Studio, whose unrelated
migration is not in main. Integrated only its six feature/handoff commits from
`64bb182ba` through `aa4230ba2` (diff from `2f9c1b01c`), preserving all existing
main changes. Original feature branch remains unchanged. No Studio files added.

A direct cherry-pick conflicted with main's PostFx-to-PRISMA migration, CMake and
AGENTS; aborted it before making commits. Applied the feature-only delta, kept
main's Prisma fixture includes, added the PHYSIS bridge, and ported integration
into canonical `engine/render/Prisma.{h,cpp}`. Legacy PostFx shims are untouched.
The imported stage history is preserved in
`docs/agent-handoffs/physical-sky-clouds-2026-11-20.md`.

- Atmosphere/fog outputs now feed exposure metering BEFORE motion/temporal,
  bloom and tonemap. Targets 200/201 avoid main's existing DoF role 50.
- Sample original scene depth, attach PRISMA's separate scratch depth. Target
  lifetime/size accounting remains in PRISMA's existing GPU-fenced caches;
  shader shutdown occurs after its GPU-idle barrier and device-generation check.
- PHYSIS bridge uses `Prisma&`; `PostFx` alias is still source-compatible.
- Added PhysicalSky/LocalFog sources to both native CMake lists, tests and the
  opt-in GPU fixtures; docs/examples/README translations describe the capability.
- Native sky/fog settings are owner-wide, not per-view profile interpolation.
  A multi-camera host must supply view-specific data or separate owners. All
  features default OFF. No simulation, FoW, save/network or existing JSON key
  migration; local fog remains native API only, as documented.
- Fixed a pre-existing main Metal compile failure: missing closing brace after
  `MetalDevice::completedFrameSerial()` in `engine/gfx/metal/GfxMetal.mm:103`.
  Confirmed the same defect in origin/main; one brace only, no RHI policy change.

## Actual verification

- PASS `cmake -S . -B build/sky` (isolated Debug cache).
- Initial build: sky/fog/PHYSIS tests + `aiverse_hdr_policy` compiled; runtime
  failed on the above baseline Metal syntax defect (20 cascading diagnostics).
  `/tmp/sky-main-build.log` records the failure.
- After brace fix, PASS `cmake --build build/sky --target aiverse_hdr_scene
  aiverse_hdr_gl aiverse_prisma_graph_tests aiverse_prisma_volume_tests -j 8`.
  `/tmp/sky-main-build-retry.log` records the successful complete runtime link.
- PASS committed-source `./run-tests.sh --quick --no-build --build-dir build/sky
  --filter '^(local_fog|physical_sky|physis_sky|physis_presentation|hdr_policy|prisma_graph|prisma_volume)$'`:
  **7/7**, report `build/sky/test-reports/20261120-134019-3729/` at code SHA above.
- PASS Metal native fixtures with `AIVERSE_AUDIO=off`:
  `AIVERSE_LOCAL_FOG_TEST=off|sphere|reveal|move|obb|obb-reveal|behind|combined`
  (each separate invocation), captures `/tmp/sky-main-fog-CASE.ppm` via
  `AIVERSE_HDR_CAPTURE`, binary `./build/sky/aiverse_hdr_scene`.
  Exact raw P6 RGB equality: reveal/off, obb-reveal/off, move/sphere, behind/off.
- PASS `AIVERSE_PHYSICAL_SKY_TEST=clouds|physis-storm`, same Metal binary;
  captures `/tmp/sky-main-CASE.ppm`. Visually inspected converted clouds and OBB
  images in `build/sky/captures/main-{clouds,fog-obb}.png`.
- PASS `AIVERSE_HDR_VERIFY=1 AIVERSE_AUDIO=off ./build/sky/aiverse_hdr_scene`:
  all **54 HDR GPU probes** pass, `/tmp/sky-main-hdr-verify.log`.
  An ad-hoc count of every ` PASS` initially asserted 54 but counted 55 because
  another diagnostic also passed. Corrected the parser to `^HDR GPU probe`;
  all 54 numbered probes pass, zero failures. No renderer change needed.
- PASS OpenGL: `AIVERSE_LOCAL_FOG_TEST=obb
  AIVERSE_HDR_CAPTURE=/tmp/sky-main-fog-gl.ppm AIVERSE_AUDIO=off
  ./build/sky/aiverse_hdr_gl`, GL33/Apple M3 Ultra, HDR 3 passes, exit 0.
- PASS README checker (9 files) and staged whitespace check.

Full repository CTest, editor/client targets, Vulkan/mobile, performance and CI
NOT RUN in this integration. No CI URL/result claimed. The original shader/media
limitations remain documented. This is a verified targeted main integration,
not automatic installation into an existing game or Studio authoring support.

Next: preserve opt-in defaults; select a native host and authored map owner for
fog persistence/runtime reveal, and validate camera-volume/temporal motion with
animated fog before claiming full temporal quality. Continue separate handoffs.
