# TerraWeave Stage 54 — compact authoritative soil/root/grass delta replication

Date: 2026-11-19; working branch `feature/terraweave`; Draft PR #24.

## Source commits
- `306830179601a6cd2295bb1a4a76f5a135a88cb0`: `RootSupportRegistry::applyAuthoritativeSupportChanges(span<PlantSupportChange>)` validates every stable plant identity/root habit/support value and all duplicates BEFORE mutating saved root/fallen states. Clients do not recalculate support from their render terrain.
- `c8b7f5826e726c5bfaaf9d155fc9cfbf09a4fb42`: `GrassCoverRegistry::applyAuthoritativeCoverageChanges(span<GrassCoverChange>,soilRevision)` validates original coverage, new coverage and exact soil revision before replacing sparse patch coverage. Zero ECS entity per grass blade.
- `360bed45d13038836a9b38e6bdb5e3092940357d`: NEW `engine/world/TerraWeaveReplication.h`, versioned little-endian `Archive` codec for `TerraWeaveStateDelta` with completed tick, before/after soil revision, sorted sparse terrain vertex edits, root support updates, grass cover changes and deterministic `PlantFallEvent` triggers. Stable sort, duplicate rejection, strict count and 8 MiB packet limit, FNV64 accidental-corruption footer. `makeTerraWeaveStateDelta`, `encodeTerraWeaveStateDelta`, `decodeTerraWeaveStateDelta`, `applyTerraWeaveStateDelta`, `TerraWeaveDeltaReceiver` (monotonic tick watermark).

## Wire/ownership

Only an AUTHORITATIVE native server/ECS module computes erosion, excavation and root fall decisions. After its completed tick, the host encodes sparse outcomes once and sends bytes through its own networking transport. Clients and replay players reject changed recipe/world until a compatible base snapshot exists, then decode and apply the authoritative edits to staged copies; if any height, root identity, grass cover or revision is incompatible, the entire delta is rejected with no partial terrain mutation. The receiver also rejects duplicate/out-of-order ticks EVEN if no ground vertex changed. The host then calls `TerrainMutationCoordinator::notifyExternalEarthwork(applied.earth)` for dirty geometry, NavMesh and near/far forest publication and sends fall events to VFX/predicted presentation. It must not recompute NEREID erosion locally or spawn authoritative Box3D physics on clients.

The delta payload does NOT include transient GPU resources, native Box3D body handles or floating-point trajectories. Those come from the server's existing replicated actor/physics stream. For newly joining clients, first send the full Stage53 `saveTerraWeaveState` snapshot with the precise immutable procedural recipe identity, then ordered deltas strictly after its tick watermark. Save the watermark alongside the game's ECS current tick; full snapshots MUST be tied to the same authority tick to avoid gaps.

## Limits, verification and remaining work

No transport/socket/packet sequencing, ACKs, bandwidth compression, interpolation or general FPS/RTS gameplay install was claimed here; the host supplies those. The code is header-only native C++ and can be used by all game genres, not a replacement NEREID or NavMesh.

**Actual verification:** GitHub commits created and source inspected; **NOT RUN** Clang/GCC, CMake, CTest, network replay, Box3D, FPS/RTS execution, Metal/Vulkan/GL screenshots or performance. Prior TerraWeave 9/9 and GPU router 1/1 passes were on older heads. PR stays Draft. Next Stage55: opt-in native GameModule integration facade, explicit save/restore binding and per-frame publication contract, and then comprehensive tests after functional completion.
