# Caverns Stage54t — native globe overview and chart selection

Date: 2026-11-24. Branch `integration/caverns-studio-stage54c-20261124`. Code/test/skill HEAD `a54394d47dc49ff6fe3ca953e6b763f54ddc31d0`. Main unchanged; compare before work reported zero commits behind main. Roadmap 98ba8496.

## Source commits
- 6011f3fc / 534b930b: `apps/studio-native/StudioPlanetGlobe.h`: selected body overview from shared Surface and CubeSphere, normalized display scale, up to 64 subdivisions per face (49,152 triangles); same CPU triangles for GPU mesh and nearest ray intersection. Canonical CubeSphere faceFromDir returns Q16 anchor coordinates. Region footprint lookup and visual markers from saved chart frames. For larger bodies the overview resamples actual authored heights; it is not full-resolution terrain or native CDLOD.
- 9432271e / 3238b1d4 / f65c50e0: `StudioScene.h/.cpp` separate session-only globe mode in existing Renderer3D camera and terrain buffer retirement owner. Retire old active chart host on entering; project/map changes exit; only actual overview draw enables picking. No flat scene or local chart draw while overview active. Existing document terrain cache invalidated on return.
- e035d741: `StudioHost.cpp`: existing typed cavernsPreview route accepts globeBody (empty returns to chart); validates context and disarmed brush. Globe mouse mode consumes clicks and drags, orbits and emits caverns.globePicked with map/project/revision. Never feeds globe coordinates to local brushes or flat object manipulation.
- 0ceb53fe / 1a3d424d / 00affee1: `apps/studio-web/src/types.ts` and `panels/generated/CavernsPanel.tsx`: body selection, open/return globe, accept fenced hit event, populate anchor fields or open existing region, create/select through existing validated operation. Chart edits hide while overview active; region mutation closes globe. Readback includes chart LOD stats.
- 1647d2fd: existing StudioTerrainGeometryTests adds nearest-hit/region/miss/invalid-ray regression source.
- ce0c7d29: existing Caverns skill updated.
- 268fd3f9 / 42fab237 / a54394d4: selectedRegionRecord/readAnchor and Scene/preview use them to avoid decoding and serializing whole region volumes on every frame.

## Contract and limitations
Overview is one body at a time; display positions are normalized and never persisted. Clicking is read-only until user commits a create/select via existing Workspace/MCP path. Overlapping chart creation remains refused. V2 format unchanged. No new gameplay simulation or physics/renderer authority. Local chart editor retains exact rim and LOD path. External malformed v2 pairwise overlap validation remains outstanding; do not imply it is verified. No actual live acceptance of globe picking, markers, resize/input, GPU lifecycle, chart switching or TS bridge.

## Delivery evidence
Source committed, static review only. Native compiler, CTest, frontend build, performance, platforms, Metal captures and publishing: NOT RUN. Existing Studio regression fixture extended but not executed. GitHub workflow lookup at exact code/test HEAD returned no runs. Existing validated mutation operation reused; no new MCP editing API or persistence version. Mandatory KINGSTON merged-tree quick gate outstanding.

## Next Stage54u
Load authored regions into the shared gameplay runtime with stable chart identity and separate ECS authority resources. Extend PlanetRenderer from one cut plan per body to shared disjoint leaf ownership and aggregate body budget, matching physical collision and nav publication/rollback. Never attach multiple independent owners to the same baseline leaves. Then gravity-aware navigation via PHYSIS (54v), followed by full native/Studio QA. Feature incomplete.
