# Stage110 — sky/fog main integration

Date: 2026-11-20. Working branch: codex/physis-qa-fixes.
User requested feature/physical-sky-clouds and codex/structured-volumetric-fog in main.
Main base: 552bd0751a0bafb571d1b791d809eec2cdd16712.
Merge: 2195c10b5bc5a0be853c9241d90e0ec311d5274f.
Merged fog tip: fa9ac9339253f456f706ee632679bedc0bb327d0.

## Scope and history

The original sky branch tip aa4230ba2129eadada1ff6661e8da8c7ba56b6d8
contains unrelated HTML5 Studio ancestry. Its sky/cloud/god-ray/local-fog
implementation was already integrated into main by
1397dfb52f163bb234b10692411bfe6d3311c8aa. Compared PhysicalSky.h/.cpp,
PhysicalSkyShaders.h, PhysisSkyBridge.h, tests/render/physical_sky.cpp and
physis_sky.cpp against the old branch: all identical after normalizing the
intentional PostFx→Prisma aliases. Did not merge unrelated Studio history or
pretend the old branch tip is now an ancestor. Its requested rendering content
is present; structured-fog branch is merged with real ancestry.

Four new fog commits (1fd380b6f,81b28fadb,19d2d8f1a,fa9ac9339) add procedural
world-space structure, ground height falloff, wind advection, billows/erosion
and optional sun self-shadowing. See the imported structured-fog and billows
handoffs for API details. Old defaults, 8+8 caps and presentation ownership
preserved; native struct/UBO changes require rebuild, no persisted format change.
Source files merged automatically. Only AGENTS.md conflicted at appended notes;
kept BOTH main LENS/STRATA/PHYSIS instructions and incoming fog/storage notes.
No other worktree or user project modified.

## Actual post-merge validation

macOS Apple Silicon, Debug x86_64/Rosetta, AppleClang17; Metal GPU test.
All build/compiler temp/module cache/shader/capture outputs under
/Volumes/KINGSTON/AIVERSE-builds/physis-system-qa.

- CMake configure with AIVERSE_BUILD_HDR_TESTS=ON: PASS (sky-merge-configure.log).
- Built aiverse_local_fog_tests, aiverse_physical_sky_tests,
  aiverse_physis_sky_tests, aiverse_physis_presentation_test, aiverse_hdr_policy,
  aiverse_prisma_graph_tests, aiverse_prisma_volume_tests and aiverse_hdr_scene
  with --parallel4: PASS (sky-merge-build.log).
- `./run-tests.sh --quick --no-build --build-dir /Volumes/KINGSTON/AIVERSE-builds/physis-system-qa/build --filter '^(local_fog|physical_sky|physis_sky|physis_presentation|hdr_policy|prisma_graph|prisma_volume)$'`:
  7/7 PASS,1.92s. sky-merge-tests.log and
  build/test-reports/20261120-184003-79252.
- Extracted GLSL/MSL with aiverse_local_fog_tests merged-fog output prefix:
  PASS. glslangValidator merged-fog.vert/.frag: PASS. xcrun -sdk macosx metal
  with external modules cache: PASS; one existing unused sunDepth warning.
- Actual aiverse_hdr_scene Metal cases via AIVERSE_LOCAL_FOG_TEST:
  valley, valley-off, valley-reveal: all exit0. Captures/logs merged-CASE.ppm/.log.
  Byte comparison: reveal and off EXACT equality; fog-on differs from off.
  This verifies rendering/reveal smoke, not reference-art parity or GPU cost.
- Staged whitespace/conflict-marker checks: PASS.

NOT RUN: full repo regression, Vulkan/mobile, OpenGL revalidation, production
scene visual acceptance, performance and moving-camera temporal quality.
Remote CI NOT CHECKED; no remote success claimed. Earlier branch GPU results
are documented separately, not substituted for this combined-source validation.

Next: publish this merge and separate handoff normally (no force-push), then
review/tune the new bank parameters in the intended real scene if requested.
