# PHYSIS Stage 24 — persistent authored-field ECS registry (2026-11-19)

Repository: chromealex/AIVERSE; branch: feature/physis-nature-simulation; PR #25 DRAFT.
Commits: ae3aec57c2b8f28b31533417f2bb7e85d9bce718, 4c54e4ce8250d4ad1b6d7f5bec9cd1a9595e34e5.

## Implemented
- engine/physis/PhysisRuntimeFields.h: Archive io for RuntimeFieldBinding and RuntimeFieldRegistry, plus reconcileTrackedRuntimeFields(World&, vector<AuthoredField>). Registry is a rollback-owned World resource, updated transactionally with staged ECS components. Existing Stage 23 API retained.
- engine/physis/PhysisProjectConfig.h: opt-in installTrackedFromMap(Simulation&, Json). Parses all assets before installation, registers persistent resource schema physis.authored_field_registry v1 and initializes ECS bindings with deterministic IDs. Legacy installFromMap remains unchanged and does not silently alter its persistent save schema.

## Contracts and limitations
Use installTrackedFromMap from GameModule::setup only if stable authored field identities and a new save manifest are acceptable. Never call both installation variants. Host invokes reconcileTrackedRuntimeFields only at authoritative simulation tick boundaries, after parseFieldAssets validates complete new map. Live editor event wiring, command queue, multiplayer replication, save migrations and external water/terrain adapters remain NOT IMPLEMENTED. Host must preserve authoritative tick ordering; deterministic changes must be replayed after rollback. Existing manually spawned untracked field entities are not reconciled.

## Verification
GitHub connector source commits succeeded. No native compiler/CTest/CI/editor UI execution performed. C++ compile and save/rollback parity NOT VERIFIED. Keep PR #25 Draft.

## Next
1. Build native PHYSIS targets and add CTest for tracked install, same-ID update identity, type replacement, deletion, save/deserialize and snapshot restore. Confirm Archive IO ADL and persistence registry manifest. Fix compiler errors before merging.
2. Add tick-boundary host command queue and canonical asset revision tracking for replay. Avoid direct ImGui -> live World mutation.
3. Integrate editor authoring only after authoritative command processing exists. Follow with TerraWeave/NEREID/Box3D read-only adapters.
4. Update AGENTS/skill and write another handoff after each implementation stage.
