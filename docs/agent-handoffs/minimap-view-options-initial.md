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
