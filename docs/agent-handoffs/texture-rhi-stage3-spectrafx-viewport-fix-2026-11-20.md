# Texture RHI Stage 3 — SpectraFX viewport API compatibility

Date: 2026-11-20
Branch: integration/texture-rhi-main-stage3-20261120
Merged main snapshot: f9aed9a705982e2c6b77b29869e309995ce8b163

## Problem
SpectraFX editor preview windows called Viewport::ensure with
three arguments after Editor introduced a mandatory
ViewportGpuRetirement owner.

## Implementation
Updated SpectraFxGpuAssetsWindow.cpp and SpectraFxGraphWindow.cpp
to pass Editor::viewportRetirement() and descriptive viewport names.

## Architecture
Viewport textures are registered with the Editor-owned retirement
queue, allowing safe deferred destruction after GPU completion.
No change to the public Viewport API or backend implementation.

## Validation
Previous compilation: failed at two ensure() calls.
After patch: native compilation and CTest NOT YET RUN.

## Next step
Build aiverse_editor and full engine.
Run complete CTest and Metal GPU acceptance.
Record actual test results and final merge SHA before push.
