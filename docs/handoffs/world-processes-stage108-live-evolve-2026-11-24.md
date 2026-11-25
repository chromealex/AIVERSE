# Stage 108 — live planetary EVOLVE regions
Date: 2026-11-24. Branch: feature/world-processes-stage31-bidirectional-handles.
Base: 34b33c3f29774be5270233c04da8e111835a2685. Plan: 920176cf1915b738c4c363b47959fe188e442f68. Implementation: 10c81830adbcf62b5a2e9de474138b7e50fe23a3.

## Delivered
- libraries/include/aiverselib/world/EvolveSurfaceRegions.h: shared bounded inverse chart.
- apps/studio-native/StudioPlanetProcessOverlays.h: authored geometry reuses canonical inverse.
- apps/studio-native/StudioPlanetLiveSnapshot.h: samplePlanetEvolve, actual completed-tick Region + SurfaceArea, bounded body-local outline and state.
- apps/studio-native/StudioGameSurface.cpp: local Play snapshot integration.
- apps/studio-native/StudioScene.h/.cpp, StudioHost.cpp: existing layer selector, native Scene renderer and map/project/revision guards.
- apps/studio-web/src/types.ts, panels/SceneBrushBar.tsx: typed state and bounded regional summary.
- tests/world_processes_gamecore_native_test.cpp: source fixtures.

## Feature gate
Implementation/architecture: code committed; existing canonical Surface/Region/renderer reused. Actual native compilation NOT RUN.
Native API: shared pure inverse in existing public header; diagnostic snapshot is Studio-only.
Skill/documentation: existing EVOLVE and World Processes skills and compatibility guide updated; no new catalog ID.
Studio/Inspector/MCP: existing read-only studio.evolveState and existing layer RPC extended without a new authoring mutation; existing inspector unchanged. Typecheck, RPC and interactive acceptance NOT RUN.
Persistence/graphs: no new authoritative data or schema, no new graph; headless regression NOT RUN.
Performance/platforms: explicit region/edge budget; timings, actual GPU/backend and all platform checks NOT RUN.
Dependencies: existing targets and geometry dependencies only; compile NOT RUN.
Examples/tests: native geometry fixture extended, no test command executed.
Publishing: actual screenshots/demo/release acceptance NOT RUN.
Integration: no main sync/merge; shared Studio paths require integration review with studio-html5 owner.

## Stage 108: live planetary EVOLVE regions

With local Play current map, a saved unchanged document, Live 3D overlay and gizmos, the Scene draws lifted read-only outlines of the actual ECS Region + SurfaceArea on the selected body. The existing EVOLVE layer selector chooses combined fertility/pollution, fertility, pollution, succession or durability colors. Authored outlines and authoring handles remain separate. No browser-side ecology or planar projection of spherical regions.

The shared `evolve::surfaceRegionDirection` in EvolveSurfaceRegions.h is the inverse of the existing azimuthal chart on its open domain, solving the nonorthogonal face-axis Gram matrix and rejecting the antipodal cut. Existing authored preview and handles use the same inverse via their forwarding function. The live path projects each sampled direction onto actual canonical Surface terrain, subtracts the body anchor and lifts the outline. Stable packed Entity ordering; maximum 64 regions globally, 4 edges × 16 segments per region; invalid, stale, disabled or ambiguous owner states are skipped; invalid chart points break the polyline, never bridge the cut. This is a sampled boundary, not exact filled global coverage or a planetary climate/vegetation simulation. Snapshot reports actual ecological values, completed tick, capped/rejected/clipped counts, body ID and region ID. HTML5 panel summarizes the first 12 sampled regions and the total; existing layer and tick controls are reused.

Existing physical/ecology state, save schemas and planar authoring are unchanged. Source fixtures cover inverse roundtrip, moon body identity, values, stale sample, disabled area, cap and antipodal clipping. Build, native tests, browser typecheck, interactive UI, screenshots and GPU performance NOT RUN. Next: PHYSIS effective live gravity visualization from the authoritative gravity sampler; TerraWeave planetary ecology lifecycle and terrain mutation remain owner dependencies.
