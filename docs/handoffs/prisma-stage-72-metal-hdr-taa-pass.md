# PRISMA Stage 72 — Native Metal HDR and Temporal AA validation

Date: 2026-11-19
Repository: chromealex/AIVERSE
Branch: feature/prisma-postfx
Draft PR: https://github.com/chromealex/AIVERSE/pull/26
Previous: docs/handoffs/prisma-stage-71-gl33-hdr-probes-pass.md
Do not merge into main or PR base without explicit user approval.

## Actual user-provided Mac Studio GPU evidence

User executed on Apple M3 Ultra:
```sh
AIVERSE_HDR_VERIFY=1 \
 AIVERSE_HDR_CAPTURE="$PWD/build/prisma-final/prisma-gpu/metal-hdr.ppm" \
 build/prisma-final/aiverse_hdr_scene \
 2>&1 | tee build/prisma-final/prisma-gpu/metal-hdr.log

AIVERSE_TAA_VERIFY=1 build/prisma-final/aiverse_hdr_scene \
 2>&1 | tee build/prisma-final/prisma-gpu/metal-taa.log
```
Both report: `GPU: Metal Apple M3 Ultra; RGBA16F=1`.
- Metal HDR: `Scene shaders: 24 GLSL/MSL variants PASS`.
- Metal HDR: all indexed GPU probes 0..53 (54/54) are PASS.
- Metal TAA: `TAA stationary centroid drift: x=0.1678 y=0.2029 pixels, edge transition=2: PASS`.
- First run: `[metal-assets] libraries=0 sourceMisses=55 pipelines=354 pipelineFailures=0 archive=0 strict=0`.
- TAA run: `[metal-assets] libraries=0 sourceMisses=46 pipelines=24 pipelineFailures=0 archive=0 strict=0`.
- Both runs show that the preferred detailed terrain-high MSL shader fails:
  `fragment function 'fs_main' uses too many samplers (17)`.
  Both LDR and HDR compile `fallback tier=2 detail=0 PASS`.
- Nonfatal unused-function MSL warnings appear in the failed first shader variant.
- Repeated static-light atlas status lines are not failures.
- Terminal returned to prompt; no explicit shell exit code or inspection of
  metal-hdr.ppm was supplied. Do not assert these facts without evidence.

## Summary of completed independent checks

- Consolidated Mac CTest: 115/115 PASS, 272.67 s (from prior user log).
- GL33 Apple HDR: all 24 fixture variants and 54 indexed GPU probes PASS,
  with terrain downgraded to tier=1 detail=0 (previous Stage 71).
- Native Metal Apple HDR: 24 variants accepted and 54 indexed GPU probes
  PASS, with terrain tier=2 detail=0.
- Native Metal TAA stationary temporal sample: PASS with quantified drift
  and edge transition above.

The 24 variants accepted by the fixture include a terrain fallback; this
does NOT mean high+detail terrain compiles. The HDR probe and TAA tests
are real GPU checks but do not establish all 19 effects' visual correctness.

## Source changes this stage

None. Documentation-only checkpoint. Earlier fallback implementation remains
in tests/render/HdrShaders.h and modules/rts/view/RtsTerrainRender.cpp.
No main/base/other branch mutation.

## Open GPU acceptance requirements

- Metal terrain high+detail uses 17 samplers and fails; requires reducing
  sampler pressure or a justified, documented capability split preserving
  authoring behavior, not a silent fixture exclusion.
- Native multi-camera viewport history isolation, resize/cut invalidation,
  GPU fence/retirement, device loss/recovery and memory/performance stress.
- Comprehensive effects/pixel comparison for HDR LUT/Graph/Bloom/DoF,
  exposure, reactive motion and alpha cutouts.
- Vulkan/GLES/WebGL platform compilation/runtime remains unverified.
- Capture file existence and command exit status remain not explicitly reported.

## Next agent

Inspect existing CMake registered renderer/Prisma GPU tests and perform focused
Metal camera-history/GPU-fence lifecycle checks, then resolve remaining issues
with a new code and handoff stage. Do not unnecessarily rerun full 115 CTest
before the next batch of code changes. Use an existing build cache and preserve
parallel worktrees. Record actual commands, logs, device and pass/fail.
