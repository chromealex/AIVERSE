# PRISMA stage 96 — per-view physical-lens defocus override (2026-11-21)

Branch `feature/aiv20261121_lens_p2` (LENS P2 work; PRISMA change is additive).
Code commit: `7c6d4903` (with the LENS physical-lens stage).

## Change

- `engine/render/Prisma.h`: `struct DepthOfFieldOverride {enabled, focusDistance,
  focusRangeMeters, maxBlur}`, `setViewDepthOfField`, `clearViewDepthOfField`,
  `viewDepthOfField` (read back for diagnostics/tests); map `viewDepthOfField_`.
- `engine/render/Prisma.cpp`: setter clamps to the `depthOfField` catalog bounds
  (0.05..2000, 0.01..500, 0..16; non-finite -> catalog default). `retireView` erases.
  The DoF pass: an override forces the metric branch and supplies focus distance,
  sharp interval and max blur; `enabled=false` skips the pass for that view.
  Authored `intensity` and `occlusionToleranceMeters` are kept. No new shader,
  uniform layout or GPU resource.
- Consumer: LENS `CameraPostFxPool` (one PRISMA instance per camera entity).

## Compatibility

Additive API; views without an override render exactly as before. No JSON/catalog
change, no `postfx` key change, PostFx aliases untouched.

## Verification actually run (KINGSTON, Linux aarch64 VM, GCC 11.4, Debug)

- `engine/render/Prisma.cpp` compiled with the project flags + `-Wall -Wextra`:
  0 diagnostics.
- Core test `lens_physical_lens_drives_prisma_exposure_and_dof` (setter clamping,
  per-view isolation, retireView, explicit clear, pool application on a recording
  RHI): see the LENS P2 handoff for the run result.
- NOT RUN: GPU pixels on Metal/GL/Vulkan, shader compilation on device.

## Next

Pixel check on Metal: same camera, lens f/1.4 vs f/16, compare background blur.
