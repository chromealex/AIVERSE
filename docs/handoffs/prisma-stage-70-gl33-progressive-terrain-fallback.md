# PRISMA Stage 70 — GL33 progressive terrain shader fallback

Date: 2026-11-19
Repository: chromealex/AIVERSE
Branch: feature/prisma-postfx
Previous: docs/handoffs/prisma-stage-69-gl33-terrain-fallback-implementation.md
Draft PR: https://github.com/chromealex/AIVERSE/pull/26
Main and PR base unchanged.

## Real user evidence
- CTest: 115/115 PASS, 0 failed, total 272.67 sec (before Stage 69/70 source updates).
- User Mac Studio M3 Ultra incremental build: "[100%] Built target aiverse_tests".
- HDR GL33 shader diagnostic after first fallback revision:
  "terrain high (LDR): link: ERROR: Implementation limit of 16 active fragment shader samplers ... fragment shader uses 17 samplers".
- GPU reports GL33 Apple M3 Ultra / 4.1 Metal - 89.4 and RGBA16F=1.
- No complete log, pixel capture result or process exit code was provided.

## Diagnosis
The previous test-only fallback on failure of terrain high/detail=1
retried only TERRAIN_TIER=2, TERRAIN_DETAIL=0. That may still exceed GL33's
16 active fragment samplers. Production TerrainRenderer::pipeline already
iterates tiers and now retries the non-detail family after detailed family
failure (Stage 69). Fixture should match production instead of falsely
requiring high tier on a sampler-limited backend.

## Actual change
Commit 909c3326b9f4b2a50f8b63b0246fce9e0f7cc7ea:
- tests/render/HdrShaders.h: if "terrain high" preferred shader fails,
  attempt non-detail tier=2, then tier=1, then tier=0, stopping on first
  successful GLSL/MSL shader creation; log accepted tier and first error.
- Preserve failure if none of the alternatives link.
- Do not change runtime shaders, catalog, RHI limits, camera API, Metal
  behavior, or make an unsupported high variant appear to have compiled.
- The fallback reduces terrain visual quality if needed; preserving full
  high detail on GL33 requires future texture packing/array consolidation
  and actual shader resource measurements.

## Validation performed vs pending
- PASS: GitHub file update and source read-back of the exact generated
  C++ fallback loop and correctly escaped source strings.
- NOT RUN by assistant: C++ compiler, GL33 shader linking, Metal GPU,
  screenshot/pixel validation, Vulkan/GLES, and native CTest after change.
- Mac GPU tests remain pending. Incoming error was real and is not a pass.

## Next action on Mac
```bash
cd ~/Projects/AIVERSE-prisma-validation-20261119-222509
git status --short
git fetch origin
git switch --detach origin/feature/prisma-postfx
cmake --build build/prisma-final --parallel 4
set -o pipefail
mkdir -p build/prisma-final/prisma-gpu
AIVERSE_HDR_VERIFY=1 \
AIVERSE_HDR_CAPTURE="$PWD/build/prisma-final/prisma-gpu/hdr.ppm" \
 build/prisma-final/aiverse_hdr_gl \
 2>&1 | tee build/prisma-final/prisma-gpu/hdr-gl.log
```
Collect full output (including possible planet or other shader errors),
real executable exit code and capture file. A successful fallback means a
lower-quality terrain shader linked; it does not establish high-detail GL33
or Metal full-path correctness. Document the next bounded fix and outcome
with a fresh handoff, and rerun consolidated CTest after GPU fixes settle.
