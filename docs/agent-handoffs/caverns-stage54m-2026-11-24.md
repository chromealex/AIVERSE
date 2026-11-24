# Caverns Stage54m — planetary surface cut and native renderer leaf replacement

Date: 2026-11-24. Continue in integration/caverns-studio-stage54c-20261124. Main and source feature branches unchanged.

## Source commits

- abb502ce: generic exact rim extraction for arbitrary triangle sources.
- 926f14d57d4fcd0764df0656d11d369fc5034428: planetary cut plan with actual body-local leaf slots, projected mouth coverage, Fill/overlap rejection and chart/body manifold/exterior boundary certification.
- d26da39c / 1c4dd546: existing PlanetRenderer leaf capture and native staged cut buffer replacement, same terrain material and fog/detail atlas interpolation, original ocean geometry, forced fine LOD ancestry and no morph at cut leaves.
- 686871e9: preserve actual body-space surface vertices in native collision payload and verify transformed rim edge incidence.
- ee86d8f0 / 6c9f86e5: bounded leaf selection over cube faces in chart footprint, stale cut epoch watermark.
- 875a949d: constrained chart-radial shaft trim/collar payload and transformed native rim check.
- be48c2f7 / 2b9f595b: skills.

## Contract

Caller chooses bounded chart and padded footprint; capture selected existing leaf slots via PlanetRenderer, preserving actual detail grid/triangle source (no skirts). Use caveSurfaceRevision and canonical authority revision when constructing CavePlanetCutPlan. Complete mouth projected area, unchanged exterior boundaries and matching internal incidences are required; conservative clipping may reject valid cases with edge subdivision. Body-space original vertices are preserved rather than inverse-round-tripped through Fixed basis. Prepare each surface slot collision from plan.bodyTriangles using prepareCavePlanetCutSlotPayload and a globally reserved native owner cell/IDs. prepareCavePlanetRimPayload uses the exact shared chart rim and constrained existing shaft collar; verify actual transformed boundary. It needs an explicitly isolated owned volume source and does not automatically partition multiple volume chunks. Axis is radial at chart anchor, not a curved radial path at arbitrary surface positions.

PlanetRenderer::installCaveSurfaceCut is an opt-in owner-boundary operation. Stage leaf VB/IB using existing terrain PV layout and source-face attribute interpolation, validate unchanged source triangles/revision, require GPU idle and real exclusion, and require actual nativeOwnersReady proof before replacing opaque terrain indices. Original geometry remains available for ocean and later restoration. Cut ancestors force leaf traversal, cut leaf morph disabled, no skirts on replacement geometry; neighboring uncut LOD/morph and skirts, float/Fixed seams and lighting must be tested. A tiny distant planet cannot switch to plain sphere while cut is active. Explicitly retire coordinated native volume/collision and clear renderer cuts before world/height/detail/flag edits or renderer shutdown. Epoch watermark prevents stale reinstalls. No concrete GameModule or Studio caller, no built-in spherical physics owner replacement, no global transactional rollback; native callbacks must not be dummy success. Existing flat-world behavior remains opt-in unchanged by no cut. Native compilation, CTest, TS build, GPU/physics/runtime acceptance NOT RUN.

## Next Stage54n

Implement planet-scoped native volume/surface publication with real ownership and reader exclusion (shared GeometryScene, existing PHYSIS/Box3D, SpatialWorld and native nav), per-body/region slot identity and retirement, and explicitly connect PlanetRenderer cut lifecycle to actual caller. Generalize neighboring LOD and chart seam transitions, then planetary Studio authoring, arbitrary-up navigation and mandatory KINGSTON QA. Feature incomplete.
