# PHYSIS Stage 7 — external water authority switch (2026-11-18)

**Branch:** `feature/physis-nature-simulation` (draft PR #25), based on `feature/universal-entity-inspector-fps`.  
**Code and test commits:** `509a622ccb3cd3aeb4839611f6b3327d2adc05bd`, `fc4099ad8dde64cf863c79982ef30c25e70fa003`, `4c5292022e1dc276322239c9ea96f09d6ba684fc`, `456121681135878575e44c291ffafbad69e1d511`, `2ec64b1d18b60069ffe3fe63c8f75b0bbf0ca809`, `b5a3e00ec44614ba214eb6f3e0133a1517bd7b75`, `cde7a3cd397b3710ded94c296f08322dddd3707c`.  
**Documentation commits:** `db51be68c5f90d963b2ea641ee07582b73682c65`, `bc7ec59a0ca5d6f8c4b6372d2ae3db0b9c28167c`, `f3200da09c76975b21eaa8374c9b6ff3219120f3`, `6dd71c4112b29975359332f5c3cef9f48d047ffb`.  
**Stage 6 ownership handoff:** `docs/agent-handoffs/physis-ownership-terra-water-2026-11-18.md`.  
**Verification:** source edits and tests written through GitHub connector. **NO C++ compilation, CTest, CI, editor or runtime executed.**

## Implementation

- `engine/physis/PhysisDisasters.h`: new small API enum `physis::FloodMode { LocalShallow, ExternalAuthority }`; `advanceDisasters(World&, FloodMode)` and `advanceEnvironmentWithDisasters(World&, FloodMode)` accept mode (default `LocalShallow` preserves prior behavior).
- `FloodMode::ExternalAuthority` skips PHYSIS's independent shallow-flood gradient/transfer, precipitation inflow and evaporation updates; clears `HazardCell::waterDepth` to zero on each step. Fire spread, fuel consumption, rain-based fire suppression and quake events **continue** as before.
- Invalid policy is rejected **before** committing a climate tick; direct `advanceDisasters` also validates. Mode is per-host policy, not serialized as a new ECS field.
- `engine/physis/PhysisIntegration.h`: `InstallOptions::floodMode` defaults to `LocalShallow`, passed into the generic `Simulation::addSystem` capture. Reject invalid install policy before initializing a World.
- `tests/physis_disasters_test.cpp`: external-mode zero-local-water, fire/quake continuation, replay from World checkpoint, invalid-mode tick unchanged; the existing local-flood conservation and deterministic fire fixtures remain unchanged.
- `tests/physis_integration_test.cpp`: two identical generic native hosts configured with external water authority, save/load and continuation checksum assertions.
- `AGENTS.md`, `engine/physis/AGENTS.md`, `docs/physis.md`, `docs/physis-nereid-terraweave-integration.md`: state the new host option and single-authority rule.

## How to configure a future NEREID-powered host

```cpp
aiverse::physis::InstallOptions physis;
physis.enableDisasters = true; // fire / quake still enabled
physis.floodMode = aiverse::physis::FloodMode::ExternalAuthority;
aiverse::physis::install(simulation, physis);
// Separately install NEREID water persistence/sampling in the host. It owns
// water bodies, level, flow and contacts; PHYSIS does not create NEREID water.
```

If the world uses PHYSIS standalone shallow flooding, keep the default `LocalShallow`. The mode should be a **stable deterministic host/project configuration** and match the config used to load/replay saves. Switching modes mid-save is not an implemented migration; the ExternalAuthority mode intentionally erases the PHYSIS local flood depth (it does NOT transfer water to NEREID).

This policy is **not** an integration adapter. There is no NEREID include/link dependency in PHYSIS and no TerraWeave dependency, no rain-volume conversion, no mesh/waterline export. It only prevents double water authority while allowing PHYSIS hazards to remain active.

## Required verification (currently NOT RUN)

```sh
./run-tests.sh --filter '^physis_'
```

This runner should include `physis_disasters` and `physis_integration`, including the newly authored assertions. Then run actual NEREID/Terra cross-integration builds and full regression/snapshot/rollback parity when branches are coordinated. No PASS claims until logs are available. Review that `Fixed` values, `FloodMode` capture, `Simulation` persistence, and COW match the implementation.

## Compatibility and limits

- No changes to `DisasterConfig`, `DisasterState` or `HazardCell` persistent wire/schema layout or version; existing LocalShallow mode remains default.
- Policy is **not embedded into persistent ECS schema**. A saved game loaded with a different host option may diverge; track it in project config/version and reject incompatible loads in a future integration stage.
- No dynamic NEREID coupling. PHYSIS `Atmosphere::rain` remains normalized, not a physically calibrated water volume.
- Existing waterAgent NEREID PR #23 and TerraWeave PR #24 branches were **not modified**. Both remain separate draft PRs.
- The PHYSIS code is currently serial, not production-grade large world job simulation. No backend visual/physics adapters are implemented.

## Next bounded stage

1. Verify full PHYSIS build, tests and save/restore locally; correct all actual errors before changing more APIs.
2. Water agent retains NEREID water dynamics/query/surface code; Terra agent retains ground heights and foliage. Do not edit those branches from PHYSIS.
3. On a **dedicated integration PR after reviewing/combining relevant heads**, create an opt-in host adapter: TerraWeave cm heights/biomes -> PHYSIS checked Fixed cell elevations/material; PHYSIS environment and NEREID water contact/waterline -> read-only Terra presentation/placement exclusions. No hard includes in three core modules.
4. Cross-test negative coordinates, metre↔cm conversion, cells of differing sizes, rain/snow vs NEREID water levels, no double water authority, save/rollback/replay, and legacy water/terrain parity.
5. Commit implementation/test work and a separate, exact-SHA handoff after **every** stage; keep PRs draft until actual build/test evidence.
