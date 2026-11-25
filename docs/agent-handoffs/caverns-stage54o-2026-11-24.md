# Caverns Stage54o — planetary local chart authoring

Date: 2026-11-24. Continue in integration/caverns-studio-stage54c-20261124. Main and source feature branches unchanged.

## Commits

- 1d823f72 / e67cf186: shared MapDoc planetary chart recipe and authoring Workspace operations. Planet block is separate from the flat-world volume.
- bf4f6f18: HTML5 body, face, chart extent and local brush/shaft settings.
- cf718ffb / c8bdc98d: existing visual streaming owner explicit bounded chart demand and local lattice.
- 2878a3b9 / ecd5e176 / ab17cf22 / eb314d30 / 526897a0 / fccc0d9e: real coarse projected spherical triangles in Studio chart view, camera and local brush bounds, diagnostics and existing sparse-owner wiring.
- 469b8d6c / 90ccf689: pick nearest actual coarse surface triangle in common Workspace operation.
- 4ffbdb3f / 98eb701c / a0a2c27e / 6598e69d: one shaft per scene stroke, native operation and UI.
- 22430db2 / bff57b8e: skill updates.

## Contract

`MapData.extra.terraweavePlanetCaverns` is a version-1 object containing immutable `anchor` and canonical `volume`. One chart per map at this stage. Body ID, cube face, fixed-point a/b and extent are authored explicitly. Chart extent <= radius/8; edits validate bounds and stable revision. Do not reinterpret this key as absolute flat coordinates. `terraweave_caverns_paint` adds planet_region and planet_entrance (surface height re-sampled at quantized local X/Z, then authored depth); ordinary stamp/recipe/entrance operations use local centimetres. `terraweave_caverns_pick` clips SDF rays to chart; planetSurface selects nearest actual projected coarse triangle. Existing undo/history merge and Studio project/map revision fence retained. The local chart view is not a globe viewport. Editor-only selected-body Surface has only tables needed by chart queries, not a fully built simulation Surface. The visual owner uses a bounded interior subset of the chart (64 slots, resolution 8, spacing max(1, cap/24) cm), with no planetary transform in this local inspector. Original coarse surface remains intact; isolate preview is needed to inspect the underground volume. Do not advertise planetary detail/LOD parity, actual holes in this Studio view or gameplay collision publication. Shaft placement is one per stroke. No automatic game authority load of the new map block yet.

## Next Stage54p

Connect planetary chart preview to the exact paired cut/collar owner (actual planet mesh source, no duplicate surface); add full chart coverage/LOD and multi-region support, explicit native game loading and gravity-oriented navigation. Review existing source/build integration before native and frontend QA. No compiler, CTest, TS build, screenshots or GPU/physics acceptance performed. Mandatory KINGSTON native validation before main. Feature incomplete.
