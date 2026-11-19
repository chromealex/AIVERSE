# Stage 59 — riverbed carriers and NEREID-linked confluence sediment transfer

**Date:** 2026-11-19. **Branch:** `feature/terraweave` (Draft PR #24).

## Actual commits / changed files

- `28b23347e269407ebb029795cc1a626495fa756a` — `engine/world/TerraWeaveShorelineRaster.h`: `ShorelineRasterSettings::includeWetBedCells` (default false), `minWetBedDepthCm`, and bounded wet bed depth detection. Besides outer wet/dry shoreline, optional fully submerged riverbed/estuary terrain grid cells (up to 16,384 total candidates) are sampled from NEREID's existing `WaterQueryIndex`. Bank/bed entities retain the same stable grid ID, wet-side probe, authoritative ground height, soil class and saved water/sediment components. Enabled wet bed cells can receive transported suspended sediment, deposit it during quiet NEREID ticks via existing `erodeShore`, gradually rise and eventually become new shoreline/sandbar after the host rerasterizes.
- `ceedaebb9e7482da93bae49be9eb2100dcbab50f` — `libraries/include/aiverselib/water/NereidSedimentTransport.h`: optional directed `std::span<const SedimentWaterLink>` for crossing different NEREID water-body IDs. Same-water-body transfers are unchanged. Only specifically authored generation-safe inlet→outlet body pairs are allowed across a boundary, maintaining mass conservation and deterministic source ordering. Input links capped at 1024.
- `4acbc99a1e84410d5d562d5544b84c820dad43f1`, `3a7bf8577ecc42fb8cb0e10be526ece73a1c52d8` — `engine/world/TerraWeaveSedimentEcs.h`: after NEREID bank erosion, the new transport system collects existing `WaterConfluenceComponent` and `WaterCascadeConnectionsComponent`. It validates confluence mouth geometry and valid inlet/outlet `WaterBodyComponent` footprints before constructing directed links, checks cascade geometry and valid bodies, and passes only actual ECS-authoritative connections to pure advection. It does not invent connectivity by checking proximity alone. No extra water solver or double terrain write.
- `ba805421d569382bfe199d6c2e6ad9f3863a007e` — `examples/world/terraweave_native/Integration.h`: registers the optional NEREID confluence/cascade persistent schemas as well as the original polygon/current/erosion components. Water entities/junctions are still authored by the game's native module.
- `90701d6e2bc351115a920956f4d27b78aae969b7` — `examples/world/terraweave_native/README.md`: usage example enabling `scan.includeWetBedCells=true`, height range, optional biome/material callback and native erosion→transport→terrain-authority ordering. Rerasterize after water/soil topology changes; no per-frame full rescanning.

## Behavioral contract

Water enters a real wet-bed or wet/dry bank cell only when the existing NEREID water geometry/currents report a covering sample. Transfer is *integer-only*, deterministic, capped and mass preserving among participating water-carrier ECS cells. Each bank retains local `ShoreErosionCell` sediment and NEREID carries in its persisted ECS components. A river mouth may transfer into an outlet lake only when a valid upstream→outlet junction/cascade is explicitly linked by the existing ECS policy, and a neighbour cell exists at the target grid position. Low-flow lake/estuary sediment can then deposit on its own terrain cell; the canonical TerraWeave ECS resource receives its resulting heightfield edit, root support/fallen tree and grass disturbance through the existing commit stage.

**Future fidelity:** This is a bounded per-cell sediment-mass rule, NOT a full fluid-sediment simulation, particle tracking, differentiable hydrology, shoreline fluid-volume feedback or automatic river-route rerouting. Large confluence cascades spanning more than a single grid neighbour do not yet provide long-range sediment transfer. Water body connectivity/rerasterization remains a structural game action; source NEREID water world is read-only during discovery. Sediment type/composition is currently scalar equivalent ground centimetres, not multiple grain-size classes. Formed sandbars remain heightfield surfaces, not overhang meshes.

## Actual verification and next

All listed commits are source changes on the feature branch. GitHub source readback verified; **NO NEW CMake/Clang/GCC, CTest, NEREID ECS scenario, Metal/Box3D runtime, network parity, mass-budget benchmark, cross-platform test or actual river-delta screenshot has been run** for Stages 57–59. Prior TerraWeave 9/9 CPU and GPU router 1/1 results were older revisions only. PR #24 must remain Draft.

**Next:** Prefer a native compile and focused mass-conservation/replay scenario after feature development. Consider multiple sediment grain classes, slope collapse/landslides, auto rerasterization scheduling and true riverbed material layer later. When adding more mechanics, preserve single NEREID water authority, one TerraWeave soil writer, COW ECS isolation and per-stage handoff.


## Stage59 source review follow-ups (2026-11-19)

- `412ac81e2f0b8a9efcae7e78a55410da3bcf7fa0`: `shore_detail::cm(Fixed)` now range-checks the raw Fixed elevation before multiplying by 100, refusing hostile/out-of-range water-level data rather than overflowing signed `int64_t`.
- `2802f769f4c5072a5213bfceb97fa9690257d510`: wet-bed minimum depth consistency is enforced only when `includeWetBedCells=true`; shoreline-only maps retain independent shallow-water range configuration.

**State:** Changes committed to `feature/terraweave`; no build/CTest/run performed after these edits. Continue feature-first stage, then native compilation/QA prior to merge.
