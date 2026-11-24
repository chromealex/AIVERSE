# Caverns Stage55d — mixed LOD compile and stamp equality

Date: 2026-11-24. Branch `integration/caverns-studio-stage54c-20261124`. Starting HEAD `c6abe2b59a6bd22aa7ea564e512d8702fab07b2b`.

Mac log (test-reports/20261124-132829-3576/build.log): all visible project diagnostics reduce to LodTransition.h:81 (three invalid coordinate-minus-SeamPoint expressions and cascading sort template errors) and VolumeSnapshot.h:20 (defaulted equality deleted because PointCm lacked equality). Correct per-axis subtraction; define exact coordinate equality on PointCm without changing layout or serialization. Add compile-time checks for point/stamp equality in existing snapshot worker fixture. No suppression flags, no changes to seam topology algorithm or disabled tests.

Evidence: actual Mac build failed before CTest; source changes committed. Native full build and tests NOT RUN in this session. Compiler error limit obscures possible further failures. Retry same KINGSTON build. Main unchanged; native GPU/GI/playable gate pending.
