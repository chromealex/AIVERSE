# Stage 106 — live planetary IMPRINT snapshot
Date: 2026-11-24. Branch: feature/world-processes-stage31-bidirectional-handles.
Base: d7872f2fee93db067c03a59dd2954bbb153f3b7c. Plan: 4ecf9b9f7dcfe97c3522d0fbbfeb9bdc59b7a941. Implementation: 6285399e1cb617c89ec9dbe4c74d7d072ade3d26.

## Delivered
- apps/studio-native/StudioPlanetLiveSnapshot.h: bounded read-only snapshot using canonical PlanetProcessGeometry; stable ordering, completed tick, body ID, values, body-local vertices.
- apps/studio-native/StudioGameSurface.cpp: local Play planet routing, no planar fallback.
- apps/studio-native/StudioHost.h/.cpp: existing Live toggle and refresh, map/project/revision binding, stop/restart/restore invalidation.
- apps/studio-native/StudioScene.h/.cpp: render-thread snapshot ownership and selected-body line rendering.
- tests/world_processes_gamecore_native_test.cpp: source assertions for body identity, coordinates, stale tick and cap.

## Feature gate
- Implementation/architecture/dependencies: code committed, static readback only; existing owner and Scene path reused, no new targets. Native build NOT RUN.
- Native API: existing PlanetProcessGeometry reused; Studio-specific snapshot helper, no new public runtime feature.
- Skill/documentation: existing world-process skill and compatibility guide updated; no new skill ID.
- Studio/MCP/inspector: existing Live toggle and studio.evolveState response extended; no new authoring mutation, registry or inspector fields needed. Actual Studio/RPC acceptance NOT RUN.
- Persistence/graphs/headless: no new authoritative state or graph; existing owner contracts unchanged; headless regression NOT RUN.
- Performance/platforms: capped scan at owner limit and 1024 quads; actual CPU/GPU measurement and all platform runs NOT RUN.
- Examples/tests: existing planet demo and native geometry fixture reused; no test command executed.
- Publishing: no real capture/demo; NOT RUN, do not claim release readiness.
- Integration: no main sync or merge; review shared Studio paths with studio-html5 owner. 

## Next
## Stage 106: live planetary IMPRINT snapshot (first slice)

For a saved planetary map launched with Play current map, enable the existing Live toggle and gizmos in Scene. The native local-match host samples the existing IMPRINT Cell ECS components at the last completed simulation tick, using the canonical Surface and `planetImprintQuad`. Only nonzero enabled traces are considered, with the owner cell limit and a maximum 1024 returned quads sorted by packed Entity ID. Snapshot reports sample tick, rejected and truncated state, body ID and exact owner values; vertices are converted from simulation anchors to body-local coordinates before publication. The existing Scene renderer draws outlines on the selected body, colored by the dominant footprint (amber), scent (cyan) or residue (violet). This is not an intensity heatmap or collision mesh.

The Live toggle, local Play ownership, project/map path, launch revision, clean document and selected body guard display. Stop/restart/checkpoint restore clear the old snapshot; normal refresh is the existing 250 ms Live cadence while Game is visible. Planet snapshots deliberately do not emit the old planar ecology rows. No persistence, simulation writes, second geometry owner or new RPC. Planar path unchanged.

Source fixtures cover moon body identity, anchor subtraction, stale completed tick and cap. Build, native tests, browser interaction and GPU capture NOT RUN. GRANULAR rendered geometry requires an explicit owner-provided placement/scale contract; do not invent one. Remaining live diagnostics for the other World Processes, robust match/map identity across menu transitions, live EVOLVE/PHYSIS visualization, TerraWeave physical owner dependencies and Studio integration are open.
