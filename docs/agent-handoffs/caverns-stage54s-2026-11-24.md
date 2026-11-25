# Caverns Stage54s — multi-region planetary authoring

Date: 2026-11-24. Branch `integration/caverns-studio-stage54c-20261124`. Code/test/skill HEAD `3d60b179b41b0185b27561d061bb20ff511937b9`. Main unchanged, compare before implementation reported zero commits behind main. Roadmap e3d22495.

## Source commits and contract
- ee649df6: `apps/editor/core/TerraWeavePlanetCavernsDoc.h`: v2 map document stores regions, activeRegionId and nextRegionId as decimal IDs. V1 reads as ID 1 and migrates on successful mutation; empty set preserves next ID. Up to 32 regions; separate immutable chart anchors and snapshots. Conservative same-body bounding-sphere overlap rejection at creation across face boundaries. Undo may restore/reuse IDs, so retain map revision fences. Does not yet revalidate all pairwise overlaps when loading externally malformed v2 data.
- 10bb1a4c: `TerraWeaveCavernsOps.h`: existing Workspace/MCP operation adds create/select/delete, readback list and active ID, scoped volume mutation and expectedRegionId check for v2. Selection is persisted in MapDoc and undoable; selected region volume revision is not changed just by selection. Brush coordinates remain local.
- f091c288 / 8622b217: Studio visual and camera select chart by active ID and anchor, one selected chart owner at a time with existing shared 64-slot preview budget, other regions remain persisted. Switching closes old owner through existing GPU lifecycle.
- 288b418d / 5616fd4f: native Scene brush stores active region ID at arm time and checks it during pick/edit.
- a0e3f0b6: HTML5 Caverns panel region list, select/create/delete with confirmation and revision fence; region actions blocked while brush armed.
- 6d0d0e92: existing StudioTerrainGeometryTests extended with v1 migration, independent chart selection, overlap refusal and v2 roundtrip fixture.
- 3d60b17: existing Caverns skill updated.

## Evidence and constraints
Source committed only. Implementation/architecture/dependencies: static inspection, no compile. Native API: existing shared editor document helpers and Workspace operation extended; no runtime authority changes. Skill updated. MCP/inspector/Studio/persistence: source wired, built mutation tests and actual roundtrip/undo NOT RUN. Graphs not applicable. Headless gameplay unchanged. Examples: regression fixture, NOT RUN. Performance, platforms, tests, publishing: NOT RUN, no native compiler/CTest/TS build/Metal screenshot in this session. GitHub workflow lookup at exact code/test HEAD returned no runs. Handoff and roadmap committed. Mandatory KINGSTON merged-tree quick gate outstanding.

## Next
Stage54t: actual globe view/picking and chart selection from surface hit; avoid treating chart coordinates as world space. Review the selected-chart hot path for unnecessary decode/allocations, tighten v2 external document overlap validation and add Workspace undo/stale-region regression. Simultaneous native multi-chart same-body publication is NOT supported yet: PlanetRenderer still has one cut plan per body; aggregate leaf ownership and a shared per-body budget before gameplay load (54u), then PHYSIS gravity navigation (54v). Do not imply all saved regions are concurrently rendered or collision-enabled. Feature incomplete.
