# PHYSIS Stage 6 — cross-agent Water / Terra ownership and reuse contract (2026-11-18)

**Branch:** `feature/physis-nature-simulation` (draft PR #25, base `feature/universal-entity-inspector-fps`).  
**Type:** documentation / coordination stage. **NO PHYSIS C++ runtime, renderer, Terra or NEREID code changed.**  
**Implementation/documentation commits (on this branch):**
- `d3ce749b676764a4454ce4d0c209e57f96a5dbbd` — new integration contract.
- `d7e295874f2f0886600300ad6e1d4f8f3c57d0e1` — root `AGENTS.md` ownership matrix and rules.
- `fd51f13624b7e52f25d8aabd2fa514f5d70ddef2` — `engine/physis/AGENTS.md` subsystem agent rules.
- `dd80d4437aeb2d5b978ca947cfa735cd678ed713` — `docs/physis.md` discoverability link.
- `85a2ea37880f46856b5919730dd4d98f251b3c5a` — PHYSIS root responsibility clarification.

## Why

The user confirmed another agent is implementing Water. Work must not be duplicated. Existing parallel work:
- **NEREID** draft PR #23 / `feature/nereid-water-core`: `libraries/include/aiverselib/water/Nereid*.h`, `docs/nereid/ARCHITECTURE.md`, `docs/nereid/HANDOFF.md`.
- **TerraWeave** draft PR #24 / `feature/terraweave`: `engine/world/TerraWeave*.h`, `docs/terrain/TERRAWEAVE.md`, `docs/terrain/AGENT_HANDOFF.md`.
- **PHYSIS** draft PR #25 / `feature/physis-nature-simulation`: `engine/physis/`, `docs/physis.md`.

Roles are subsystem assignments, not human identity assertions. Do not edit other agents' active branches or assume their untested PRs have been merged.

## Public integration API (existing, not newly implemented)

- PHYSIS: `physis::environmentAt(World const&, x, z, EnvironmentalResponse&)`, `physis::forEachPresentationCell(World const&, Sink)` and generic `physis::install(Simulation&, InstallOptions)`.
- NEREID (separate branch): `aiverse::lib::nereid::WaterBodyComponent`, `WaterSample`, `sampleWater(World const&, Vec3)`, `WaterContactComponent`, `registerNativeWaterPipeline` (check actual Water agent's current header/API when integrating).
- TerraWeave (separate branch): `terraweave::heightAtCm`, `generateChunk`, `scatter`, `TerraWeaveForestScene`, host height/exclusion provider callbacks (check current Terra agent's API before integrating).

Full contract: `docs/physis-nereid-terraweave-integration.md`. It defines data authority, X/Z horizontal, Y up, Terra signed cm vs PHYSIS/NEREID Fixed metres, strict negative-coordinate floor division, tick ordering and presentation-only snapshots.

## Known collision and required resolution

PHYSIS `HazardCell::waterDepth` and `advanceDisasters` currently perform optional standalone **shallow flooding**. NEREID is to own water bodies/surfaces/flows in integrated worlds. Two authoritative water states over the same region are forbidden. Before enabling both in one world, implement/test an explicit flood policy (disable PHYSIS flood or use one-way calibrated handover to NEREID) **without changing NEREID source or rewriting legacy RTS water**. PHYSIS `Atmosphere::rain` is normalized 0..1 and cannot directly change NEREID `surfaceY` in metres. Water agent should own a calibrated conversion if requested.

## Actual verification

- GitHub connector confirmed PR #23, PR #24, PR #25 exist and their branches contain documented module APIs.
- GitHub connector created/updated documentation on **PHYSIS feature branch**.
- **NOT RUN:** CMake, C++ compilation, CTest, CI, cross-branch integration, editor visual smoke, rollback or network replay. This documentation stage changes no compiled code. No test success claimed.

## Next agent steps (bounded)

1. Read root `AGENTS.md`, `engine/physis/AGENTS.md`, `docs/physis-nereid-terraweave-integration.md`; inspect latest commit heads of PR #23 / #24 / #25 and their agent handoffs.
2. First check/repair PHYSIS Stage 1–5 actual native build and tests (`./run-tests.sh --filter '^physis_'`) on a local checkout; record exact failures and fixes in a new stage handoff.
3. Coordinate with Water agent on the **single authoritative water owner** before any PHYSIS flood-to-NEREID coupling. Prefer an opt-in host-owned integration adapter after branch merge with no foundational module includes of one another.
4. Add a **CPU-only Terra preview** consuming immutable PHYSIS environment and NEREID water (water masks/waterline; wetness/snow/ice/wind) using explicit checked units; do not modify TerraWeave/NEREID on PHYSIS branch.
5. Verify native standalone targets and cross-system negative-coordinates, multiple-grid-scale, checksum/save/rollback, shoreline, absence of double-flood, legacy water/terrain tests. Use `./run-tests.sh --filter '^(physis_|ecs_nereid_|terraweave)'` plus legacy suites.
6. For each next implementation stage: commit code, then separately commit handoff with exact implementation SHA, paths, public API, actual tests, failures, next actions. Only then consider making the draft PR ready for review.
