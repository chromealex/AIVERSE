# minimap-view-options — initial handoff

Owner: gameplay
Branch/SHA: pending
Architecture/API: pending
Changed paths: pending
Tests: NOT RUN
Risks: pending
Next: confirm owner, update roadmap and implement.

## 2026-11-27 shared planar projection

Commit: d78af5d22ebdac54241af032ba18cf95e48da992
Added engine/view/MapProjection.h, neutral rectangle projection for world/UV conversion, clipping and rectangular camera footprint. Tests added for roundtrip, clipping, rejected reversed bounds and trapezoid envelope. Tests/build NOT RUN; diff check passed. Next integrate rendering, fog/background sampling, overlays and minimapPointer through the same bounds; add per-type visibility and data settings. screen.bounds is [left,right,top,bottom] allowed center displacement, not absolute map rectangle: camera coverage is center ± half size plus displacement. Keep default full-grid behavior and inspect MinimapPlanet separately. No #213 completion claim.

## 2026-11-27 client projection integration

Commit: 85fb1b38abe6cebdc2e961be32f28799769b9ada
Paths: apps/client/{GameClient.h,Minimap.cpp,MinimapPlanet.cpp,ClientUi.cpp}, engine/view/MapProjection.h, tests/tests.cpp, docs/game-format.md, docs/features/minimap-view-options.json.
All flat minimap layers and pointer actions now share cropped bounds. Per-axis pixel scales account for integer texture rounding; background samples the crop from the full-map image. Authored camera coverage or explicit bounds, rectangular footprint option, per-type visibility (flat and globe), and finite extent validation added. Explicit visibility overrides legacy small-node suppression.
Evidence: git diff --check passed. Tests and builds NOT RUN, per requested final-build workflow. Regression cases added for nonfinite/overflow extents. Runtime screenshots, data editing surface/schema acceptance, final Mac gate and feature delivery validation remain pending. No issue closed, no merge/push in this stage. Next: audit #212 portrait model/background, #219 additive model materials and #221 particle blending, then final integration checks.
