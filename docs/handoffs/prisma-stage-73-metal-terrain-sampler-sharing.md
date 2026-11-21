# PRISMA Stage 73 — Metal terrain sampler sharing

Date: 2026-11-19
Repo: chromealex/AIVERSE
Branch: feature/prisma-postfx
Draft PR: https://github.com/chromealex/AIVERSE/pull/26
Previous: docs/handoffs/prisma-stage-72-metal-hdr-taa-pass.md
Do not merge main/base without explicit permission.

## Observed GPU baseline (user's Mac Studio Apple M3 Ultra)
- User completed 115/115 CTest, 0 failures, earlier consolidated run.
- Native Metal HDR: 24 fixture variants and 54/54 GPU probe comparisons PASS.
- Native Metal TAA: stationary centroid x=0.1678, y=0.2029 pixels,
  edge transition 2, PASS.
- Metal high+detail terrain shader failed compiling before fallback:
  `fragment function 'fs_main' uses too many samplers (17)`.
- Accepted Metal fallback `tier=2 detail=0 PASS` for HDR and LDR.
- The 17-sampler error is NOT equivalent to a full shader/pixel PASS for the
  original preferred high+detail variant.

## New source change and intent
Commit a1f2e3d9a2e2dc73b78c042ebd35b63a9649068d:
`modules/rts/view/RtsTerrainShaders.h`.
- Metal `tex14` (cliff normal) now uses `s13` (cliff albedo)
  rather than a distinct fragment sampler `s14`. Both textures are
  tiling cliff layers with matching UVs and shared filter/address intent.
- Metal `tex17` (detail splat) now uses the same constexpr sampler `s16`
  as `tex16` (detail normal); the two previous constexpr samplers used
  identical linear/mip/clamp descriptor settings.
- Removed now-unused sampler declarations/uses; keep all texture bindings,
  UV/math and normal/detail/height/splat material calculations intact.
- GLSL remains untouched; GL33 high+detail still exceeds image-unit
  limits and uses the existing documented tier=1 detail=0 fallback.
  A separate atlas/array/packed-texture design with verified resource
  bindings is required to recover GL33 full detail.

## Verification actually done
- GitHub write and readback confirmed changed Metal shader text.
- Static checks: no references to `s14` or `s17` sampling remain;
  `tex14.sample(s13,` and `tex17.sample(s16,` present;
  GLSL tex14 uniform untouched.
- NOT RUN by assistant: native C++ compilation, MSL compiler, HDR fixture,
  GPU performance, visual/pixel parity, CTest after change.
- A native source compile error or linked sampler count change is possible;
  do not mark the new Metal high+detail variant PASS before actual rerun.

## Exact next check on user's existing Mac worktree
```bash
cd ~/Projects/AIVERSE-prisma-validation-20261119-222509
git status --short
git fetch origin
git switch --detach origin/feature/prisma-postfx
cmake --build build/prisma-final --parallel 4
set -o pipefail
mkdir -p build/prisma-final/prisma-gpu
AIVERSE_HDR_VERIFY=1 \
 AIVERSE_HDR_CAPTURE="$PWD/build/prisma-final/prisma-gpu/metal-hdr-stage73.ppm" \
 build/prisma-final/aiverse_hdr_scene \
 2>&1 | tee build/prisma-final/prisma-gpu/metal-hdr-stage73.log
```
Inspect whether the *preferred* terrain high variant links without any
fallback, not merely the existing 24 variants PASS; record pixel probes and
actual exit code. If Metal still exceeds 16, inspect the compiler's exact
sampler count and resource binding; do not suppress real failures.

After Metal shader resource closure: plan shared GL33 texture packing,
native multi-view history/fence retirement and Vulkan/GLES platform builds.
Each stage needs its own handoff with files, API, executed tests and next step.
