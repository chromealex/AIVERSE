# PRISMA stage: DoF foreground bleed (2026-11-21)

Branch `feature/aiv20261121_lens_dof_bleed`, from `main` `eed939121`.

## Change

`engine/render/PrismaShaders.h`, metric DoF branch (`focusInfo.w = 1`, used by LENS
per-camera lens overrides and `metricFocus`). The previous gather sampled 8 taps at the
pixel's own blur radius and rejected every tap across a depth step in both directions.
An out-of-focus foreground object therefore could not spread over the background, and
its outline stayed a hard cutout.

Now a scatter-as-gather over the full blur radius (3 rings x 8 taps, rings turned by
15 degrees): a tap counts when its own blur circle reaches the pixel. Taps nearer than the
pixel (by more than `occlusionToleranceMeters`) use their own circle, so the blurred
foreground bleeds over the background and the sharp zone. Taps at the same depth or behind
are limited by the pixel's own circle, so the background still never bleeds onto a sharper
foreground. Depth 1 (sky) is treated as the far plane instead of being skipped.
In the near field (pixel in front of the sharp interval) taps behind use the pixel's own
circle, so the background shows through the blurred foreground edge instead of the edge
ending on a sharp line. New helpers `prismaDofCoc`, `prismaDofReach`. The legacy (non-metric) branch is unchanged.
Cost: 24 taps + depth reads per pixel instead of up to 8, for metric DoF only.

## Verified

macOS (Metal), `aiverse_lens_showcase`, Sponza, 1920x1080, 35 mm f/1.4 focus 4 m:
the shader compiles (`pipelineFailures=0`), and the near blue curtain and ivy now soften
over the column and the background instead of ending on a sharp line (before/after frames in
`/Volumes/KINGSTON/AIVERSE-builds/lens-p2/shots/h1-old.ppm`, `h1-new.ppm`).

## Not verified

GL / GLES / WebGL / Vulkan compilation of the new loop (same shared source, int division and
`cos`/`sin` in a loop), mobile cost, CTest (no shader test covers this pass).

## Tried and dropped

A thin-lens CoC r(d) = r_inf*|d-S|/d for LENS overrides: physically right, but with the
showcase lens (r_inf = 5 px) it left the near curtains and the mid-ground almost sharp, so
the frame lost its depth of field. The linear ramp stays; revisit together with a larger
blur budget (LENS clamps r_inf to 16 px) and more taps.

## Next

Half-resolution near-field pass if the 24-tap cost matters on mobile tiers.
