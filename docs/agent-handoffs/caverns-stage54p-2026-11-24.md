# Caverns Stage54p — exact planetary chart cut preflight

Date: 2026-11-24. Branch: `integration/caverns-studio-stage54c-20261124`. Code SHA: `3f74df500b8fa019e5721c73015cd2e8693bcffd`. Main unchanged. Main sync merge 11e5ee80 incorporates the two site-only changes from 2be780ab. Roadmap/acceptance scope recorded in 863aae68.

## Delivered scope

- `engine/world/TerraWeaveCavernsPlanetSurface.h`: retain original outward body triangles and source-cell sequence alongside the projected coarse triangles.
- `apps/editor/core/TerraWeavePlanetCavernsDoc.h`: `planLocalSurfaceCut` validates source shape, chart anchor, cell bounds and exact re-projection; groups by face and delegates to the existing planetary cut planner. This is an editor-only coarse group, not a native renderer leaf lease.
- `apps/studio-native/StudioTerrainGeometry.h`: report certified candidates versus refused/outside coverage while preserving the original solid terrain, including preflight exceptions. No candidate cut is drawn before a matching collar.
- `apps/studio-native/tests/StudioTerrainGeometryTests.cpp`: regression source for captured triangles, plan identity, original count and refused mismatched projection.
- Existing Caverns skill updated; no new skill registration, schema, MCP command, simulation owner or dependency target.

## Delivery evidence

Implementation/architecture/dependencies: source committed; static inspection only, NOT RUN for build/runtime validation. Native API: existing shared planner reused; new adapter is editor-only. Skill: source updated. MCP/inspector/persistence: no new authoring action or schema; existing path unchanged, regression NOT RUN. Studio: diagnostic source wired; native build and visual acceptance NOT RUN. Graphs: not applicable (no graph). Headless: no simulation change. Examples: regression fixture added, NOT RUN. Performance/platforms: no measurements; NOT RUN. Tests: existing Studio target regression extended, NOT RUN. Documentation/roadmap/handoff: recorded. Publishing: real screenshot/demo pending successful native acceptance.

No local repository compiler, CTest, TS build or GPU/physics acceptance executed in this session. GitHub PR-triggered workflow lookup on exact code SHA returned no runs. Mandatory Mac KINGSTON quick gate remains outstanding. Do not claim feature readiness.

## Next actionable stage

Build the shared immutable chart-space rim plan from the cut result, refine its boundary on the exact volume lattice (Q16 representability required), and pass the SAME refined plan to the existing visual owner and the Studio surface mesh. Preserve explicit chart demand on recovery and match runtime-normalized revisions after undo/redo. Stage both meshes before publication; show only the solid baseline until the collar and replacement surface are both ready. Do not use face groups as gameplay leaf leases. Then improve full coverage/LOD, multi-region, globe picking, runtime loading and PHYSIS-oriented navigation. Feature incomplete.
