# PRISMA Stage 69 continuation — GL33 terrain sampler fallback implementation

Date: 2026-11-19
Branch: feature/prisma-postfx
Previous: docs/handoffs/prisma-stage-69-gl33-terrain-sampler-limit.md
Draft PR: https://github.com/chromealex/AIVERSE/pull/26
No merges to main or base.

## Incoming evidence
User Mac Studio Apple M3 Ultra: full CTest 115/115 PASS in 272.67 s.
GL33 HDR fixture diagnostic: terrain high (LDR) shader linker rejected
18 active fragment samplers against an implementation limit of 16.
This is not a Metal runtime failure.

## New source changes
1. Commit 5b49e1809e90153952bb12e5c37600fb645ee210:
   modules/rts/view/RtsTerrainRender.cpp.
   Existing production pipeline() reduced tier but retained TERRAIN_DETAIL=1;
   if all detail variants failed, it returned no terrain pipeline.
   After detailed tiers exhaust, it now explicitly logs the downgrade and
   retries pipeline(dev, tier) with TERRAIN_DETAIL=0. The existing shader
   compiler/pipeline cache, failed-variant markers and native Metal shader
   path are preserved. Non-detail fallback still attempts success/failure
   honestly, and no shader image-unit limit is overridden.
2. Commit 34283152c309ef3ca9a4d71b9f7adf231001bd7d:
   tests/render/HdrShaders.h.
   When "terrain high" fails shader creation in the standalone HDR
   fixture, try the matching TERRAIN_TIER=2, TERRAIN_DETAIL=0 GLSL/MSL
   variant. Explicitly log "non-detail fallback PASS" on successful
   secondary compilation; fail normally if both variants fail. Other
   shader names are unaffected. This matches production fallback intent
   and verifies LDR/HDR non-detail shader compilation on the actual device.

## Limitations / interpretation
- This preserves terrain visibility but sacrifices optional micro-normal
  and detail-splat presentation when fragment samplers are exhausted.
- The exact set of 18 active shaders slots must be profiled to recover
  full detail on GL3.3 without reducing other features.
- A successful fixture fallback is NOT evidence that detailed high variant
  linked, and not a Metal/Vulkan/GLES pixel-parity result.
- Shader variants, output pixel capture and fallback warning still require
  actual Mac compiler/GPU execution. Static source inspection and GitHub
  update acknowledgments are not native test passes.

## Test state
- CTest 115/115 PASS before these source changes, user verified.
- After these two source changes: CMake build, CTest and HDR fixture NOT RUN
  by this agent; no Mac GPU device accessible here.
- Last observed HDR GL33 error is a real failure/blocked validation until rerun.

## Next step (existing Mac worktree)
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
Read whole log, exit code and capture. Re-run CTest after source modifications
in the consolidated final cycle when GPU blockers are resolved. Preserve
separate handoff after any further bounded fixes.
