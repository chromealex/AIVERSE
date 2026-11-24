# Stage 107 — live GRANULAR diagnostic section
Date: 2026-11-24. Branch: feature/world-processes-stage31-bidirectional-handles.
Base: 0a82aff3f2994694ece305a1e3981f8b041654fd. Plan: 56e1d59a95d588526afe7ab861684335d840ed8d. Implementation: 947b3a49a7312d385f170fa64f7b0a296c6a3967.

## Delivered
- apps/studio-native/StudioPlanetLiveSnapshot.h: samplePlanetGranular, actual ECS completed tick, bounded occupied tiles, owner geometry, body-local output.
- apps/studio-native/StudioGameSurface.h/.cpp: opt-in sampling with explicit diagnostic cell size.
- apps/studio-native/StudioHost.h/.cpp: existing typed Live settings, validation before mutation, snapshot reuse and map-change clearing.
- apps/studio-native/StudioScene.cpp: selected-body GRANULAR wireframe top outlines, fill color and sample status.
- apps/studio-web/src/types.ts, mock.ts, panels/SceneBrushBar.tsx: typed bridge, scale selector, planetary snapshot summary; mock never claims live geometry.
- tests/world_processes_gamecore_native_test.cpp: source fixtures.

## Feature gate
Implementation and architecture: committed source, reuse canonical Field/Surface/PlanetProcessGeometry and Scene renderer; native compilation NOT RUN.
Native API: existing public geometry reused, diagnostic helper is Studio-only.
Skill/docs: updated existing skills and planetary guide, no new catalog ID.
Studio/Inspector/MCP: transient typed UI/RPC setting, not authored data or a gameplay mutation; no new MCP authoring operation or Inspector component required. Typecheck, actual UI and RPC acceptance NOT RUN.
Persistence/graphs: no new authoritative data, save schema, or graph. Existing owner contracts unchanged; headless regression NOT RUN.
Performance/platforms/dependencies: bounded fields/tiles/quads; no new targets or external dependencies; timings and platform acceptance NOT RUN.
Examples/tests: existing native geometry fixture extended; actual test command NOT RUN.
Publishing: real captures and release evidence NOT RUN.
Integration: no main sync/merge; reconcile shared Studio files with studio-html5 owner.

## Stage 107: live planetary GRANULAR diagnostic section

Enable the existing Live 3D overlay and Scene gizmos on a saved local Play current-map session. Select a nonzero GRANULAR planet diagnostic cell/depth (cm); zero disables it. This is an explicitly selected transient display scale, NOT a physical placement, terrain mutation, mass calibration, or collider setting. Existing Field schema has no metre scale; physical owner adapters still supply their own explicit placement. The diagnostic uses the live Transform and canonical Surface, square cell/depth, zero bottom clearance relative to ground, and canonical planetGranularTops at the completed tick. Wireframe tops are colored by actual fill fraction; no full volumetric mesh.

Stable packed-ID field order and row-major occupied 16x16 tile traversal; at most 16 fields, 64 occupied tiles, 512 output quads globally. Empty/solid cells do not emit geometry. Stale/invalid geometry is rejected and cap/rejection reported. Body-local coordinates and body IDs prevent cross-planet rendering. Native Scene guards and existing Live toggle apply. Browser polling reuses the planet snapshot while Live is enabled; the panel reports sampled counts and basic local tick controls instead of plotting spherical coordinates on a planar SVG. Flat ecology remains unchanged. No new ECS state, persistence, graph, collider or terrain owner.

Source fixtures extend the existing native geometry test for moon, fill mass, row, stale tick, disabled scale and output cap. No build, CTest, TypeScript typecheck, browser interaction, GPU captures or performance measurements run. Physical render/collider publication, actual planetary terrain transfer, per-field authored physical scale, full all-mechanic visualization and integration remain open. Next: live EVOLVE canonical SurfaceArea boundaries and state, then effective PHYSIS gravity arrows.
