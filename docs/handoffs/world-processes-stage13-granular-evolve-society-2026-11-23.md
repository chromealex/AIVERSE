# AIVERSE World Processes — Stage 13: GRANULAR + EVOLVE + SOCIETY (2026-11-23)

Branch: `feature/world-processes-experimental`, PR #112 DRAFT, main unchanged. User requested **features first, tests later**.

## GRANULAR
`libraries/include/aiverselib/world/GranularEcs.h`, SHA `c9018dde2ad265740e4c9b16ad20c3a6e0f1ec6c`.
ECS `granular::Field` is authoritative small 2D granular mass storage (width,height,capacity,solid obstacle mask, per-cell mass, last tick); `granular::Source` emits or excavates finite mass into a location. A deterministic bottom-up gravity pass moves solids one row or diagonally around obstacles, with parity-based left/right preference; mass is conserved except deliberate sources. Atomic stage->commit and strict caps (16 fields, 128×128 each, 4096 sources, <=1e6 mass/cell). `granular::install` registers `granular.field/source` v1. This is not CFD/SPH, a 3D granular particle solver or TerraWeave terrain replacement. Terrain/physics and visible mesh conversion remain downstream presentation or world-authoring owners.

## EVOLVE
`libraries/include/aiverselib/world/EvolveEcs.h`, SHA `101d3c7b4e7434fdeeb951bdf71f662b89487c0a`.
ECS `evolve::Region`: long-term durability, fertility, ecological succession, pollution and sampled tick. `evolve::Pressure`: external decay/recovery, nutrient gain/loss, contamination/purification, regrowth/regression on an Entity target. Stable ordering and bounded tick catch-up (<=10000) maintain deterministic region evolution; uses Stage01 age policy for durability. No direct weather or resource authority: PHYSIS/environment and a game's economy author Pressure; TerraWeave/renderer consume evolution. Schemas `evolve.region/pressure` v1, caps 8192/16384.

## SOCIETY
`libraries/include/aiverselib/world/SocietyEcs.h`, SHA `7752d90fe1f46e63c674c632903a67ff365ec9e5`.
ECS `society::Person`: 64-bit knowledge-topic mask, credibility and acceptance threshold; `society::Contact`: directed generation-safe source->target relationship with trust; `society::Message`: declared source knowledge broadcast. A one-tick, snapshot-isolated propagation pass (no same-tick cascade) shares knowledge only when directed trust*credibility meets the recipient's threshold. Sorted by packed Entity ID, generation-aware, caps 8192 people, 32768 contacts and 8192 messages. Schemas `society.person/contact/message` v1. Does NOT replace dialogue, quest logic, relationship narratives or NPC AI; those are host consumers.

## Status and handoff
All 7 remaining systems (MORPH, CROWD, IMPRINT, CIRCUIT, GRANULAR, EVOLVE, SOCIETY) now have authored deterministic ECS components, explicit `install(Simulation&)`, portable `io(Archive&, T&)`, v1 persistence IDs, bounded runtime `advance(World&,tick)` and genre-neutral behavior. Prior ECHO/FRACTURE/AETHER remain separate.

**Tests deferred by user.** Source files exist on GitHub branch; **no compilation, CTest, CI result, sanitizer, native cross-platform or visual Studio verification of these 7 new headers has been performed**. Do not report production ready. Need integrate all seven into optional GameModule/project native setup, map/Studio authoring and downstream gameplay adapters first; then run regression tests and full Mac quick suite when user approves.

Next Stage14: add a single opt-in registration/installation facade and extend native `world_processes` module to map-author these seven safely without changing engine ownership; update Studio type schema through existing Add Component workflow, supply handoff. Then review hard limits/invariants and document remaining presentation boundaries.
