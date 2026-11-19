# Stage 57 — automatic NEREID bank discovery and auto-owned ECS shoreline

**Date:** 2026-11-19. **Branch:** `feature/terraweave`, PR #24 Draft.

## Implemented and committed

- `291dad245fe98fcecc2ba2561f54b79d5a406530`: NEW `engine/world/TerraWeaveShorelineRaster.h`.
- `b849d68a88ef1e8accf3d5f97e14623712486a91`: ECS COW fix, prepares writable `WaterBankSoilComponent` and `WaterProbeComponent` pools before mutation to avoid changing shared Simulation rollback snapshots.

`discoverNereidShoreline(const World&,const EarthworksLayer&,ShorelineRasterSettings,ShorelineSoilAt={},ShorelineRootsAt={})` constructs one `lib::nereid::WaterQueryIndex` from the completed ECS water world (which already respects `WaterBodyComponent`, `WaterGeometryComponent` polygon/river spline, body priorities, NEREID current/confluence/cascade flow) and scans a bounded rectangle of up to **16,384** terrain grid vertices. For each vertex, read its canonical Earthworks height and neighbouring samples. Only the wet/dry transition of real NEREID footprint and elevation qualifies, including a bounded wave-splash height above the surface and submergence depth. Each resulting `AutoShorelineBank` contains unique terrain vertex, wet-side `WaterProbeComponent` position, water-contact permille, bank slope, material, bedrock, optional root reinforcement and wave energy. Stable grid and cardinal direction order independent of ECS pool insertion order. `biomeShoreSubstrate(Biome)` offers a simple fallback; a real painted-soil callback is preferable.

`registerAutoShorelineBankPersistence(Simulation&)` registers `AutoShorelineBankComponent` under `terraweave.auto_bank` v1. `refreshAutoShorelineBanks(World&,vector<AutoShorelineBank>)` runs ONLY at a native ECS structural-write barrier (not inside water queries) and creates/upserts stable generated bank entities with existing `WaterProbeComponent`, `WaterContactComponent`, `WaterBankSoilComponent`, `WaterBankErosionOutputComponent` and `BankEarthVertexComponent`. It preserves suspended sediment and fractional erosion/deposition carries on existing cells; newly dry auto banks become dormant rather than silently deleting their sediment. Manually authored bank ECS entities are never touched. Existing soil heights must agree with the source Earthworks revision or refresh fails instead of hiding stale state. Caller must re-discover after water geometry/terrain topology changes; high-frequency per-frame rasterization is intentionally not automatic.

### Constraints and next stage

NEREID still uniquely owns water/current/sampling, TerraWeave owns soil/vegetation effects; no duplicate water system is introduced. Auto marker registration must happen in GameModule::setup before persistent registry sealing. `refreshAutoShorelineBanks` is a helper, not an automatically installed native game code path. Its structural ECS publication is not a full transaction across all entity allocations; a native host must restrict it to validated bounded candidates and safe structural-write barriers.

**Actual QA:** GitHub source commits and readback only; no compilation/CTest/GPU/real ECS simulation executed for this stage. Earlier 9/9 TerraWeave CPU and 1/1 GPU timing router results apply to prior code. PR stays Draft.

**Next:** Stage 58 conservative per-cell downstream sediment transport and ECS integration between NEREID bank erosion and TerraWeave terrain commit. Use pure integer advection with bounded per-tick flux, preserved sediment mass, no double soil write and deterministic simultaneous transfer. Preserve optional manual bank configuration and save/rollback state.
