# Stage 63c — finite NEREID river supply/outflow ports and global local-reservoir mass conservation

**Date:** 2026-11-19. **Branch:** `feature/terraweave`. **PR:** #24 Draft. This supplements the Stage63a/b water-column, terrain-bottom and WorldScene visual work.

## Implementation and commits

- `c97d61c6a31b09483985c3bedfda055f6ddeeeae` — `libraries/include/aiverselib/water/NereidFloodRouting.h`: persisted `WaterFloodPort` entries (at most 64), `WaterFloodPortKind::Inflow/Outflow`, and **external finite water ledger** `WaterFloodResource::externalVolumeCm` (bounded to 1,000,000,000 equal-area column centimetres). A source transfers a configured bounded volume from this counted reserve to the cell, and an outlet returns cell water to the SAME reserve. Ports are tied to signed NEREID grid X/Z cells, and inflow references a real generation-safe NEREID WaterBody Entity. No uncontrolled infinite water injection, no new water-body simulation and no double-counting of cell volume. `WaterFloodTick` records supplied/drained volume alongside interior transferred volume and overtopping.
- `4b42e01e2b81e0fad6e6379f5e3e5c4a542dc1d6` — pressure/volume check applies to *entire finite reservoir ledger + every internal cell*, and wet/dry topology revisions derive from FINAL old-vs-new cell wetness after all interior transfers and ports (rather than incrementing for transient dry/refill within a tick). The native ECS flood system rejects invalid inflow WaterBody references before stepping.
- `7524449e0e735707c01aeae3cc8242eafd98d1db` — `engine/world/TerraWeaveNereidFloodBridge.h::configureNereidFloodBoundaryPorts(World&,ports,finiteReserveCm)` validates every inlet WaterBody is a real NEREID ECS water component, verifies all port cells exist in the previously seeded grid, rejects unsupported/multiple ports and disallows configuring a running reservoir after the first flood tick. It publishes all port configuration as one named ECS resource update; the backend continues to own core water flow.
- `f1b4a6070906a1dec4f42f5127e96184e97c47fa` — `examples/world/terraweave_native/Integration.h::configureFloodRiverPorts` wraps initial port configuration for a native game, with NO Box3D dependency.
- `71d996be24cbec00e20906ea3ce1e860824def0d` — `examples/world/terraweave_native/README.md` includes a finite 1m-gridded river example with one 6cm/tick inlet, one 6cm/tick outlet, and an explicitly accounted 100,000cm external reserve.

## Why this matters

Stage63a conserved water within a closed, seeded local grid. A flat NEREID `WaterBodyComponent` (static level for a river) has no head gradient and no subsequent water inflow, so its water would NOT automatically pile up in front of a blockage. This stage provides the missing explicit finite inlet/outlet condition. A real NEREID obstruction reduces permeability through the same `NereidFloodRouting` edge callback; NEREID floodwater can accumulate upstream as the inlet transfers its **existing counted reserve**, then spill over neighbouring dry terrain through higher head. The downstream outlet returns water to that reserve for continued recirculation, and a depleted source stops. Water body current sampling, visual foaming and water geometry stay under NEREID ownership; TerraWeave continues to publish terrain elevation only.

**Scope of conservation:** Equal-area water-column volumes on the explicit local grid PLUS the counted external store. Global ocean/river base body mass and static WaterBody surface are NOT incorporated into that ledger; do not claim entire world fluid conservation or perfect hydraulic pressure. Reservoirs are intentionally bounded and configured once before the first ECS flood tick. Native GameModule save/rollback includes ports and reserve via the named persistent `nereid.flood` resource. A multiplayer transport must replicate that ECS state rather than recreating arbitrary source water on each client.

## API and deterministic tick order

1. Native GameModule registers NEREID water and `enableFloodRouting` before setup seals persistence.
2. At the structural map setup barrier, spawn legitimate WaterBody Entity, seed a bounded grid once via `examples::terraweave_native::seedFloodReservoir`, then call `configureFloodRiverPorts` with a finite reserve.
3. Each NEREID flood tick performs deterministic adjacent-cell routing from previous heights, evaluates current obstruction permeability at grid edges, then applies explicit inflow/outflow ports from/to the counted reserve. It verifies `sum(before cell volumes) + externalBefore == sum(after cell volumes) + externalAfter` before committing the new ECS resource.
4. Reference and indexed NEREID water samples can include newly flooded cells outside authored geometry; TerraWeave moves its ground in the usual soil/erosion/slump transaction, synchronizes grid bottom without modifying volume; auto-bank reraster only when final wetness or soil revision changes; WorldScene overlays extra water via bounded one-model batch.

## Verification and next work

**Actual verification:** Source changes committed through GitHub and reviewed, but NO native C++ build, CTest, ECS flood 1000-tick mass/audit run, Box3D obstruction scenario, real-world water refraction/rendering or GPU performance benchmark executed. Prior 9/9 TerraWeave CPU and 1/1 GPU timing router were from older branch heads. PR #24 remains Draft.

**Next:** Compile the combined new header graph (especially reference-index/adaptive water, WorldScene, native example, ECS persistent flood resource); run bounded 1000-tick mass/overflow/repeated tick/rollback fixtures with 0%, 50%, 100% obstruction permeability; validate cloned COW World snapshots; then test renderer on Metal and profile 2,048/16,384 flood cells. The stage remains a local depth/volume heuristic, not a fluid/energy-conserving shallow-water solver.


## Stage63c follow-up source review
- `0dfc5af85eb80e2bec1084edcf924683c8148c55`: `NereidFloodRouting.h` rejects a dry cell retaining a stale WaterBody Entity; a newly inundated cell explicitly inherits its validated transfer source rather than reusing a previous, potentially recycled body handle.
- The flood port and local sediment mass records are separate. `externalVolumeCm` is a counted **water** reserve, not ground/sediment or NEREID's static ocean volume.
- Code was reviewed after this fix; **native compilation and runtime QA remain NOT RUN**.
