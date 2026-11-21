# PRISMA Stage 69 — HDR GL33 validation blocker: terrain sampler budget

Date: 2026-11-19
Branch: feature/prisma-postfx
Previous: docs/handoffs/prisma-stage-68-ctest-pass-gpu-next.md
No merges to main/base.

## Reported actual Mac results
The user reported consolidated CTest:
- 115/115 PASS, 0 failed; wall 272.67 seconds.
- SpectraFX label: 6 tests, 0.25 sec*proc.

The user then launched the HDR GPU fixture on Apple M3 Ultra. Output excerpt:
```
terrain high (LDR): link: ERROR: Implementation limit of 16 active fragment shader samplers
(e.g., maximum number of supported image units) exceeded, fragment shader uses 18 samplers
GPU: GL33 Apple M3 Ultra / 4.1 Metal - 89.4; RGBA16F=1
```
The excerpt does not include process exit status or remainder of the log. Do
not record the GPU fixture PASS. GL33 is OpenGL, not Metal rendering.

## Source-grounded diagnosis
- tests/render/HdrScene.h invokes checkHdrShaders(device) when
  AIVERSE_HDR_VERIFY is set, and aborts the scene verification if false.
- tests/render/HdrShaders.h explicitly compiles "terrain high" with
  TERRAIN_TIER=2 and TERRAIN_DETAIL=1, in both HDR and LDR variants.
  The LDR variant failed at GL shader link with 18 > 16 active fragment samplers.
- modules/rts/view/RtsTerrainShaders.h declares sampler2D tex4..tex17 and
  uses macros for TRACE (tex18), baked lightmap/shadowmask (tex19/tex20)
  and shared lighting/shadows. Real linked sampler count depends on
  active compile options.
- modules/rts/view/RtsTerrainRender.cpp independently creates runtime
  variants for quality tier and detail mode. This is terrain/GL backend
  capacity, not Prisma profile numeric parsing or HDR tone mapping.

## Resolution requirements for next agent
Fix the backend-specific shader resource budget without raising reported
GL33 limits or disabling valid Metal quality by accident. Prefer a
capability-driven resource plan (or intentionally reduced GL terrain tier/
detail fallback) that also affects production TerrainRenderer, not only the
HDR test fixture. Validate exact shader texture bindings and resource
indices, and preserve baked lighting and traced shadow behavior where
supported. Compile both LDR and HDR variants and recheck the PPM capture.
Do not merely skip the failing test or relabel terrain high as passing.

## Current validation
- User native CTest: PASS 115/115.
- HDR GL33: encountered real shader-link ERROR; overall exit unknown from
  excerpt (treat as blocked).
- Actual Metal GPU HDR, Vulkan, GLES, pixel parity: NOT VERIFIED.
- No runtime/shader source was changed in this handoff.

## Precise next local diagnostic
```bash
cd ~/Projects/AIVERSE-prisma-validation-20261119-222509
set -o pipefail
mkdir -p build/prisma-final/prisma-gpu
AIVERSE_HDR_VERIFY=1 \
AIVERSE_HDR_CAPTURE="$PWD/build/prisma-final/prisma-gpu/hdr.ppm" \
  build/prisma-final/aiverse_hdr_gl \
  2>&1 | tee build/prisma-final/prisma-gpu/hdr-gl.log
echo "GPU fixture exit: ${PIPESTATUS[0]}"
```
Note: PIPESTATUS is Bash-specific and valid when bash is the shell.
On the user's zsh terminal, use `echo "GPU fixture exit: ${pipestatus[1]}"`
immediately after the pipeline, or store status before other commands.

Required next step: inspect full log and implement and build shader resource
fallback, then repeat the GPU fixture; document actual outcome.
