# TerraWeave Stage 53 — authoritative save/restore and existing NEREID ECS bank outputs
Date: 2026-11-19. Branch: `feature/terraweave`. PR #24 remains Draft.

## Actual commits and files
- `f652be6598dd5a5ee0ae27517337d9556826b6bc` — `engine/world/TerraWeaveEarthworks.h`: `EarthworksSnapshot` with deterministic sparse ordered heights, grid spacing, bedrock and exact soil revision; `snapshot()`, `setSnapshot()`, `exchangeSnapshotState()`.
- `1605ebdc12bf3a98eb6f6a95546bfd23b142da48` — `engine/world/TerraWeaveRootEvents.h`: no-allocation swap of saved root/plant state.
- `924e4dbc16aa9e6385fce73b61fc04630a3bb9c9` — `engine/world/TerraWeaveGrassCover.h`: paired patch/index swap for restore.
- `592e5d1e6c8a4f29692a4d41888d1fc298de0ebb` — `engine/world/TerraWeaveNereidErosionBridge.h`: saved soil-bank sediment/carry/tick state swap, retaining its original `EarthworksLayer&` reference.
- `2b486a9e5c02fc1f1e343b6b92eeb415465b17de`, `605c33cb0c636c85fb1681340ef86a010df92a13` — NEW `engine/world/TerraWeavePersistence.h`: versioned little-endian `Archive` save/restore with immutable recipe ID, source budgets, footer noncryptographic FNV64 corruption check, exact uint64 IDs, sparse soil, root support/environment/fallen state, grass patch coverage/age and local NEREID soil sediment. `restoreTerraWeaveState` validates all four staged sources before committing through map exchanges, including bank ground-height agreement. Avoid redefining the already existing `ShoreErosionCell::io` overload from `NereidShorelineSystems.h`.
- `1cf1eacbe8bb485e2e0fcd89bdcd32aae35e253c` — NEW `engine/world/TerraWeaveEcsBankBridge.h`: opt-in persistent `BankEarthVertexComponent` binds an NEREID ECS soil-bank entity to an Earthworks grid vertex. `commitNereidBankTerrain(World&,tick,earth,roots,grass)` consumes **already computed** `WaterBankErosionOutputComponent` after NEREID sampling/erosion systems. Validates tick, before/after heights, sediment quantities, unique terrain vertex ownership; applies grouped soil edits and derived root/grass changes on staged copies, then swaps state. `addTerraWeaveEcsBankCommitSystem` can be registered AFTER `registerNativeWaterPipeline` (with bank erosion enabled). It calls a host-supplied callback for new dirty regions/fall events. NEREID water flow is never calculated twice.

## Native integration sketch

```cpp
lib::nereid::NativeWaterPipelineConfig cfg;
cfg.enableBankErosion = true;
lib::nereid::registerNativeWaterPipeline(sim, "nereid", cfg);
terraweave::registerTerraWeaveBankVertexPersistence(sim);
terraweave::addTerraWeaveEcsBankCommitSystem(
    sim, "terraweave.bank_commit", ground, &roots, &grass,
    [&pending](const terraweave::EcsBankTerrainCommit& c) {
        pending.push_back(c); // app publishes at owner-thread barrier
    });
// At save barrier:
const std::string bytes=terraweave::saveTerraWeaveState(
    ground, roots, grass, standaloneWaterBridge, immutableRecipeHash);
// At load barrier (this path restores standalone bridge state):
terraweave::restoreTerraWeaveState(
    bytes, immutableRecipeHash, ground, roots, grass, standaloneWaterBridge);
```

Use **either** the standalone `NereidEarthworksBridge::step` water erosion path OR the native `WaterBankErosionOutputComponent` path as the authoritative writer for each bank vertex, never both in one tick. When using the ECS source, `registerNereidBankErosionPersistence` already handles ECS bank soil/carry state via the native ECS save, while TerraWeave archives the soil/root/grass authority; the standalone bridge may have no bank entries. A native game must assign unique saved `BankEarthVertexComponent` per ECS soil-bank entity.

## IMPORTANT ownership / limits
- The saved recipe ID MUST identify the exact compatible procedural baseline/asset (e.g. generator version+seed+height input content digest) and be independently verified by the game. The footer is NOT cryptographic verification of untrusted network input.
- Serialize/restore only at an authoritative tick boundary. Restore invalidates RHI/SpatialWorld/NavMesh/Horizon residency; the game must republish dynamic terrain and plant instances after loading. Do not write an async worker callback that reads mutable `EarthworksLayer`.
- Physics::Scene Box3D handles, trajectories and GPU models are NOT included in the portable archive. Saved root `fallen` flags alone cannot reconstruct an identical Box3D trajectory; the server must restore its separate trusted Box3D state or respawn an approximate fallen body and replicate positions to clients. There is no new transport packet/reconciliation implementation in this stage.
- Source snapshots can be up to 64 MiB; do not send entire saves every tick. The next stage should implement compact revision/tick-guarded delta replication.

## Verification
Source files committed in GitHub. No new CMake build, CTest, macOS Metal/Box3D runtime, FPS/RTS gameplay, network replay, fuzz or cross-platform QA has been run; prior 9/9 TerraWeave CPU and 1/1 GPU timing tests are older revisions. Continue **features first** per user; leave PR Draft.

## Next
Stage 54: revision-guarded per-tick terrain + root + grass delta capture/application for network/replays; explicit save/load system binding at native application lifecycle and rebind near/far forest. Later final build/tests including corrupt save, duplicate bank cell, replay, Box3D transient body and client synchronization.
