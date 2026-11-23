# Handoff: AIVERSE constructive building envelope — stage 5 (2026-11-23)

## Provenance

- Branch: `feature/aiv20261123_building-grammar` (stacked on `feature/aiv20261122_building_styles`).
- PR: https://github.com/chromealex/AIVERSE/pull/114 (Draft).
- Roadmap first commit: `63eee1a4a1bb3cf3687180dd46a976b61390db32`.
- Implementation: `5a7eea81c88d45527d4edebb4292ab8a79eb2950`.
- Raw-geometry regression: `9253c1632ae958a046ecdd87d16c58f90eba6476`.

## What changed and API contract

1. `libraries/include/aiverselib/systems/BuildingStructure.h`: split raw
   `dressStructureGeometry(..., StructureSize body={})` from the preserved
   public `dressStructure(..., StructureSize desired={})`.
2. Explicit requests are now fitted by bounded regeneration (max 32
   iterations) in body W/H/D. The fitter translates **rigidly only** and
   never multiplies vertex positions, normals, UVs, colliders or gate anchors
   by final-envelope scale factors.
3. Nominal gate leaf sizes originate from the authored style's kind size,
   independent of the requested envelope, with an explicit minimum
   width 1.6 m and height 2.1 m. Physically infeasible sizes return empty.
4. `StyledStructure::constructionSize` exposes actual raw W/H/D to
   support debugging and regression tests without guessing solver values.
5. `tests/libraries/building_style_tests.cpp` compares sized models
   with separately generated raw geometry, requiring identical vertex
   normals, UVs and counts, and position differences matching one rigid
   translation. Existing size/yaw/seed/GLB checks remain enabled.
6. Documentation and agent skill updated. No HTML5 Studio code modified.

## Tests actually performed

- Stage-4 focused Linux C++ tests + four GLB export probes **PASSED** in
  GitHub Actions run `45210066318` at parent commit `97313625`.
- New stage-5 focused GitHub workflows were queued at time of implementation.
  Do **not** claim the stage-5 solver passed until actual run results are
  observed and failures fixed.
- Full cross-platform CI, Metal and image/silhouette quality not verified;
  no public devlog screenshots created.

## Known limitations, risks, and next action

- Solver uses damped fixed-point steps, not a full constrained CAD solver;
  large discontinuities in randomized rooftop prop placement can make
  exact dimensions unreachable. Returning empty is intentional, not
  permission to stretch the mesh. Test extreme sizes and low buildings.
- Exact body dimensions are exposed; no per-part semantic constraints beyond
  nominal gate widths/height. Future detailed architectural rules should
  preserve explicit physical thickness for stairs, walls, handrails and doors.
- Closed sci-fi hull colliders remain non-enterable, even when a decorative
  arch or visual gate is present; no new gameplay ECS ownership.
- The next stage should add structured failure reasons, style/setting/
  function catalogs, and the HTML5 Studio generation UI from the approved
  design kit; create Assets GUID/meta via native editor bridge rather than JS.
- Use current `Building Style Focused` GitHub Actions to compile and run
  tests and inspect its full logs; review screenshot from real
  `aiverse_modelview`/Metal before merging.
- Parent branch remains behind main; do not merge this PR directly to main.

Suggested 3.1.0 release note **once verified and merged**: architectural
structure fitting preserves unscaled details while accommodating requested
model dimensions. Legacy unsized structures remain source-compatible.
