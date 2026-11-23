# AIVERSE World Processes — Stage 12: IMPRINT + CIRCUIT (2026-11-23)

Branch `feature/world-processes-experimental`, draft PR #112; no main merge.

## IMPRINT
Commit `53b03939ec049aa117de5fa2f40bb47a04d61217`.
`libraries/include/aiverselib/world/ImprintEcs.h`: actual persistent ECS components `imprint::Cell` (footprint/scent/residue concentrations + fading and sample tick), `imprint::Link` (generation-safe adjacent cells + exchange rate) and `imprint::Deposit` (producer target and per-tick deposition).
`imprint::advance(World&,tick)` stages stable sorted data, uses Stage01 `worldprocess::erode` for decay, conservatively moves three trace species between linked cells, applies bounded deposits, then commits authoritative ECS. `imprint::install(Simulation&)` registers v1 `imprint.cell/link/deposit` and deterministic tick. Limits 8192 cells/32768 links/8192 sources and 1e6 concentration. No engine terrain, weather or VFX copy. Rendering footprints, scents/tracking AI, producer collision footprints are host consumers/writers, not yet wired.

## CIRCUIT
Commit `76d0c3442fb9708f2309f2a38ed0749fcbf2c854`.
`libraries/include/aiverselib/world/CircuitEcs.h`: persistent `circuit::Node` (generated supply, consumed load, external drive and internal powered/signal outputs), `circuit::Wire` (directed or bidirectional, full-generation Entity links), `circuit::Gate` (AND, OR, XOR, NOT, optional 2-input; output writer).
`circuit::advance(World&,tick)` computes per-generator directed BFS and deterministic bounded load allocation, evaluates Boolean gates from prior-tick signals (cycles intentionally delayed one tick), commits after staged validation. Installs schemas `circuit.node/wire/gate` v1, limit 4096/16384/8192, supplies/demands <=1e6. This is a logic/power routing mechanism for opted-in games, NOT a replacement for the main economy, grids or electrical hardware physics. Validation refuses multiple gates owning the same output.

## Shared constraints, tests and next stage
Both modules use existing `Simulation::registerPersistentComponent`, `addSystem`, only World-owned gameplay state, stable sorted Entity keys, fail-closed bounds and no genre-specific ownership. Read/write/rollback behavior has been authored but **NO tests, compilation, sanitizers or native Studio QA have been executed** per user's explicit instruction (tests later). Do not conflate older Stage05 green builds with this source.

Next implement GRANULAR, EVOLVE and SOCIETY, each with a separate authored native ECS header, persistence schemas and tick API, then stage13 handoff. After feature implementation, wire all seven into an opt-in native integration/Studio authoring API and only later enter the consolidated test phase. No claim of production-ready physics or AI without real gameplay host QA.
