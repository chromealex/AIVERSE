# Building/Caverns integration — Stage 10 (2026-11-24)

## Code commit

`1e2edcbd6` changes:

- `engine/world/TerraWeaveCavernsMesh.h`: omit exact zero-area facets after integer-centimetre interpolation, before the shared visual/collision mesh is published.
- `engine/render/TerraWeaveCavernsGeometryScene.h`, `engine/render/TerraWeaveCavernsBatchGeometry.h`, `engine/world/TerraWeaveCavernsPlanetNativeOwner.h`: supply `GeometryScene` manifest version 1 and stable per-instance IDs.

## Actual Mac evidence

- PHYSICS=ON TerraWeave native micro compile target built and its CTest passed (1/1) after Stage 9 link fix.
- Native Caverns demo target built from the integration tree on KINGSTON.
- First six-scenario run after Stage 10 changes: `chamber`, `swim`, `moving_restore`, `swim_restore` passed (4/6). `portal` and `portal_restore` failed before their acceptance markers with `Caverns planetary quantized fragment invalid` in `TerraWeaveCavernsPlanetCut.h`. Report root: `/Volumes/KINGSTON/AIVERSE-builds/caverns-stage59/0f4394d81d798f28bfdf6b5e43991057a9e7636b/caverns-demo-reports/`.
- A diagnostic portal run found a sub-centimetre projected fragment. A tentative small-fragment filter did not resolve the scenario and was removed. It is not part of this commit.
- Building Mac gate remains 8/9 with `studio_building_preview` showing only its clear color. A diagnostic primitive also failed to appear in that offscreen pass; temporary instrumentation was removed.

## Next step

1. Correct planetary portal clipping/quantization while retaining its manifold and border certificates; rerun all six native scenarios.
2. Correct the Building offscreen Metal render path and rerun merged-tree Building gate.
3. Run the full exact-SHA Caverns and Building gates, complete delivery evidence, then squash-integrate into `main`.
