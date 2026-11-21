# PRISMA Stage 71 — macOS GL33 HDR GPU probes verified

Date: 2026-11-19
Repository: chromealex/AIVERSE
Branch: feature/prisma-postfx
Draft PR: https://github.com/chromealex/AIVERSE/pull/26
Previous: docs/handoffs/prisma-stage-70-gl33-progressive-terrain-fallback.md
Main and PR base unchanged.

## Genuine user-run results

Host: Mac Studio Apple M3 Ultra.
Earlier full consolidated native CTest: 115/115 PASS, 0 failures, 272.67 s.
HDR fixture now reports:
- Device: GL33 Apple M3 Ultra / 4.1 Metal - 89.4; RGBA16F=1.
- Terrain high LDR preferred shader unavailable: OpenGL linker reports 18
  active fragment samplers, exceeding hardware limit 16.
- LDR fallback: tier=1 detail=0 PASS.
- HDR preferred shader similarly unavailable at 18 samplers;
  fallback: tier=1 detail=0 PASS.
- Scene shaders: 24 GLSL/MSL variants PASS (fixture accepted fallback
  for high-detail terrain, not full high-detail shader success).
- HDR GPU probes indexed 0 through 53: all 54 individually PASS against
  explicit expected values.
- Repeated "Static lights: max 1 per cell, 2048 x N atlas" lines are
  status diagnostics, not reported failures.
- User pasted terminal prompt returning after the output, but did not
  include an explicit process exit code or saved capture inspection.
  Do not claim exit code 0 or assert PPM file contents without evidence.

## Code and test ownership

No additional source changes in this stage. The previous Stage-69/70
code and test updates are now checked on the user's real GL backend.
Production TerrainRenderer::pipeline attempts tier degradation and
non-detail fallback, and HDR fixture tests the same downgrade.
This is a compatibility fallback; full high-detail terrain at 16 fragment
samplers remains unsupported unless texture count is reduced/packed.

## Validation matrix

- Native CTest: PASS, 115/115 (user Mac; before Stage-69/70 changes).
- Apple OpenGL shader variants: PASS, 24/24 (fixture uses a validated
  Medium/no-detail downgrade for terrain high).
- Apple OpenGL HDR pixel probes: PASS, 54/54.
- OpenGL full-high-detail shader: FAIL to link (18 > 16), documented
  fallback PASS instead.
- GPU process exit code and image capture existence: not explicitly confirmed.
- True Metal backend PRISMA HDR/temporal/multiview GPU test: NOT RUN or
  NOT REPORTED in this message.
- Vulkan, GLES, WebGL native shader/pixel behavior: NOT RUN/NOT REPORTED.
- GPU-fence stress, device loss/recovery, perf: NOT RUN/NOT REPORTED.

## Next action

Identify existing Metal renderer and PRISMA GPU fixture targets in
CMake/tests and execute focused real-Metal tests on the Mac Studio.
Require device/backend identified as Metal, successful shader compile and
readback, temporal and multi-view history, fences and resource retirement.
Avoid re-running entire 115 CTest suite until source changes warrant it.
If the user can inspect captured PPM or process status, record those exact
facts separately. Do not merge PR #26 without explicit approval.
