# World Processes planet/moon example — Stage 97

Open this ordinary project in AIVERSE Studio or build it with the existing native project pipeline. The map deliberately places all ten World Processes on eight actual map entities, with explicit links, two bodies and separate authored PHYSIS radial fields. No new simulation or renderer is installed. The example native module composes the canonical PHYSIS map loader and World Processes native adapter. The zero baseline is explicit; centers use Surface::anchor (8192, 12288 metres), not render orbits. The 1x1 PHYSIS climate grid is a bootstrap only, not a spherical climate simulation. Field strength fades with radius.

The world-process graph/field state can be inspected through existing native and Studio diagnostics. The planet CROWD preference lists the moon slot first to exercise same-body filtering; the local slot remains the eligible destination. GRANULAR fields on each body settle via actual gravity; MORPH opts into absolute loading. AETHER uses the native stratification installer, but same-altitude ducts are not a vertical buoyancy demonstration.

The registered world_processes_gamecore_native_test reads this exact map and rules, composes the same example module, checks runtime effects, peer parity, save/load and rollback. Run from the repository root: `./run-tests.sh --filter '^world_processes_gamecore_native_test$'`.

## Reproduce an ARBOR fall in Studio

Use a copy of this project with a physics-enabled Studio build linked to its `native` directory. In `rules.json`, set `terrain.model` to `heightfield` and add these two entries to the existing World Processes native module's `config` object.

```json
{
  "planetForestPhysics": true,
  "planetForest": {
    "growthInterval": 1,
    "decayInterval": 2000,
    "naturalFallPermille": 1000,
    "species": [
      {"id": 1, "matureTicks": 1, "lifespanTicks": 12, "naturalFallBody": {"fromArbor": true}}
    ],
    "plants": [{"entity": "local_slot", "species": 1, "key": 7}]
  }
}
```

In `maps/planet.json`, set `world.terraweaveForest` to the following catalog. The plant and the collision recipe resolve the same species. The short lifespan makes natural mortality observable during the first seconds of local Play.

```json
{
  "species": [
    {"id": 1, "shape": "columnar", "minHeightCm": 1000, "maxHeightCm": 1000, "trunkRadiusCm": 40, "prototypeSeed": 7}
  ]
}
```

Start **Play** on the saved map. Open **Rules → Planet forest**, select **Show live forest in Scene**, then return to **Scene**. In the live inspector, plant key 7 on Terra becomes dead, fallen and physical. Its capsule reports the centre, axis, radius and half-height from the completed physics tick. Pause to inspect the branches in that pose. The authenticated Studio operation `studio_planet_forest_live` exposes the same body-local coordinates for framing the tree.
