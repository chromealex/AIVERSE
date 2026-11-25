# Stage 109 — effective live planetary PHYSIS gravity
Date: 2026-11-24. Branch: feature/world-processes-stage31-bidirectional-handles.
Base: 8fa021b1d7b4595b1836c95faf365ee303c17893. Plan: a2b15a5a6229d3500dab62b8f36570897e77016b. Implementation: 2afaf91b1c5422d8f893de0ca64dab4647f2d9fa.

## Delivered
- apps/studio-native/StudioPlanetLiveSnapshot.h: samplePlanetGravity; canonical PHYSIS sampling, bounded surface sites, vector and body summary.
- apps/studio-native/StudioGameSurface.h/.cpp: opt-in local completed-tick snapshot.
- apps/studio-native/StudioHost.h/.cpp: validated transient Live gravity setting and existing native bridge.
- apps/studio-native/StudioScene.cpp: effective arrows/zero markers, selected-body rendering, separation from authored contributions.
- apps/studio-web/src/types.ts, mock.ts, panels/SceneBrushBar.tsx: typed response, opt-in checkbox and per-body summary.
- tests/world_processes_gamecore_native_test.cpp: source fixtures.

## Feature gate
Implementation/architecture: source committed; existing Surface and gravityAt owner reused, no new simulation. Native compilation NOT RUN.
Native API: existing public gravity policy unchanged; Studio-only diagnostic adapter.
Skill/documentation: PHYSIS and World Processes skills and planetary compatibility guide updated; no new catalog ID.
Studio/Inspector/MCP: transient validated setting on existing studio.evolveLayer and snapshot on studio.evolveState; no authored data mutation, new inspector component or MCP authoring tool. Typecheck, RPC and interactive acceptance NOT RUN.
Persistence/graphs: no new authority, save schema or graph; headless regression NOT RUN.
Performance/platforms: cap of 1024 fields and 1536 sites, no partial gravity sum; actual timing, GPU and platform runs NOT RUN.
Dependencies: existing native targets only; build NOT RUN.
Examples/tests: source assertions added to existing native geometry fixture; no test command executed.
Publishing: real captures/demo/release acceptance NOT RUN.
Integration: no main sync/merge; shared Studio files require integration review with studio-html5 owner.

## Stage 109: effective live planetary PHYSIS gravity

In Studio, enable Live 3D overlay and the separate Effective PHYSIS gravity (planet) checkbox during local Play current map. The Scene uses the saved map/project/revision and selected-body guards. The existing EVOLVE layer can be off while gravity is shown. Effective arrows replace authored per-field arrows only while the live gravity snapshot is available; authoring handles and field data remain independent.

The diagnostic samples actual canonical Surface positions through `physis::gravityAt` after a completed local simulation tick. This retains the owner's baseline, additive overlap, override priority/tie rule and component clamp, including default settings when the resource is absent (explicitly reported); no implicit radial baseline or separate field solver. Four by four sites per cube face, 96 per surface body and at most 16 bodies. If more than 1024 gravity components exist the diagnostic is unavailable instead of silently dropping fields. No stars without surfaces are sampled. Snapshot records body ID, face coordinates, body-local position, radial up, acceleration vector, magnitude, radial component and per-body min/max and classification counts. Fixed-point values are rounded for JSON presentation only.

Cyan inward, orange outward, violet near-tangent, grey cross for magnitude <= approximately 0.01 m/s²; radial classification uses the same fixed threshold. Arrow lengths are clamped indicative display lengths; actual numeric values are returned by the snapshot. Browser panel reports sampled per-body magnitude range and counts, not global extrema or all-space gravity. No physics force application, persistence, gameplay change, trajectory prediction, volumetric field, or new RPC. Existing planar ecology remains unchanged.

Source fixtures exercise two bodies, zero field, radial attraction, additive overlap, direct canonical-sampler comparison, reversal and fail-closed field budget. Native build, test execution, TypeScript typecheck, interactive UI, actual GPU screenshots and performance measurements NOT RUN. Next: AETHER gas cell and duct flux visualization from completed ECS state; remaining World Processes, physical owner dependencies and cross-branch integration are still open.
