# Stage 104 — spherical EVOLVE scene editing
Date 2026-11-24. Branch feature/world-processes-stage31-bidirectional-handles.
Base 11273929; plan 325780f6; implementation 5e990629; snapshot corrections and fixtures 86c83d57c4460c1aa024fb333a9c73af1b820314.

Delivered:
- StudioPlanetRegionEditing.h: canonical azimuthal chart projection/inverse, four edge handles, center handle, integer bounds and snapping, antipodal limit, fixed-opposite and symmetric resize.
- StudioScene.h/.cpp: terrain-occluded handle picking, transient candidate outline, cancel, revision/body guards; fixed previous raw MapDoc pointer identity mismatch with StudioHost snapshotting.
- StudioHost.cpp: existing mouse phase routing, Shift/Ctrl/Cmd modifiers, Escape cancellation; existing request and document.changed notification.
- StudioSession.cpp: additive bounds key on studio.worldProcessAreaSet, validates complete integer tuple, lock/hidden, optional expectedRevision and bounded Area before one MapDoc edit. Numeric edits and plane behavior unchanged.
- StudioTerrainGeometryTests.cpp and StudioSessionTests.cpp: chart/resize/snap/undo/stale source fixtures. Committed source readback only. Compilation, CTest, UI, GPU/Metal and screenshot evidence NOT RUN.

Remaining: spherical PHYSIS center/radius handles through physis_set, rotation/scale gizmos, live GRANULAR/IMPRINT/other process visualization, actual terrain and physical owner dependencies. No main merge/sync. Cross-branch Studio integration required. Next stage 105: PHYSIS graphical editing and remaining transform tools, then live diagnostics. No feature-ready claim.
