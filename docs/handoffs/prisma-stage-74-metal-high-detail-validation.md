# PRISMA Stage 74 — Metal high-detail shader verified on M3 Ultra

Date: 2026-11-19
Repository: chromealex/AIVERSE
Branch: feature/prisma-postfx
Draft PR: https://github.com/chromealex/AIVERSE/pull/26
Previous: docs/handoffs/prisma-stage-73-metal-terrain-sampler-sharing.md
Do not merge PR into base/main without explicit approval.

## User-executed real native results

On Apple M3 Ultra, following the Stage-73 MSL sampler-sharing change:
- Native build completed, final lines:
  [98%] Built target aiverse_hdr_gl
  [100%] Built target aiverse_tests
- Metal device identified: `GPU: Metal Apple M3 Ultra; RGBA16F=1`.
- `[metal-assets] libraries=0 sourceMisses=53 pipelines=351 pipelineFailures=0 archive=0 strict=0`.
- `Scene shaders: 24 GLSL/MSL variants PASS`.
- All 54 indexed HDR GPU probes 0..53 PASS against expected values.
- There is NO `terrain high ... preferred shader unavailable`, sampler
  limit error, or `fallback tier=2 detail=0 PASS` in supplied run.
  Since the fixture only enters fallback when original shader creation
  fails, source + native log support preferred Metal High+Detail compile PASS.
- No explicit shell exit code or saved PPM visual inspection was shown;
  do not invent either.
- GPU probe scene is not a per-pixel visual parity test for the high-detail
  terrain's four material normal maps and detail splat assets.

Earlier successful user-run results:
- 115/115 native CTest, 0 failures, 272.67 seconds (earlier source revision).
- Native Metal TAA static centroid drift x=0.1678 y=0.2029 px,
  edge transition=2 PASS (earlier shader revision).
- GL33 HDR 24 shader fixture variants and 54 HDR probes PASS, but
  GL33 high-detail shader needs fallback tier=1 detail=0 (18 > 16 samplers).

## Source ownership and code in this stage
No new runtime edits in Stage 74: verification/documentation only.
Metal source change preceding it:
`modules/rts/view/RtsTerrainShaders.h` at commit
`a1f2e3d9a2e2dc73b78c042ebd35b63a9649068d`
shares MSL `tex13/s13` sampler with `tex14` and constant sampler
for `tex16` with `tex17`, without changing texture bindings or
GLSL normal/detail material math.

## Remaining work
1. Restore high+detail on GL33 (16 active image units) with genuine
   fewer texture fetch resource identities (e.g., packing/array and
   binding schema), not just Metal sampler state aliases.
2. Verify multi-camera isolated histories, resize/camera-cut,
   GPU-fenced retirement, device loss/recovery, memory/performance.
3. Validate Shader Graph/DoF/AE/MotionBlur/reactive boundaries on
   real GPU, and available Vulkan/GLES builds/runtimes.
4. When changes settle, rerun one consolidated CTest.
Keep a separate handoff after each bounded implementation stage.
