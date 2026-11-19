# PHYSIS Stage 27 — optional Simulation command host (2026-11-19)

Repository chromealex/AIVERSE, branch feature/physis-nature-simulation, PR #25 DRAFT.
Code commit: 2d866e2fd2f724da7aba3bd8312f1e1f6cd29cbd.

## Implemented
Added engine/physis/PhysisFieldCommandHost.h. FieldCommandCheckpoint is a persistent World resource containing the last applied authoritative sequence. FieldCommandHost owns a transient queue and immutable-history journal. submit stages both journal and queue before publication. apply is called by a Simulation system inserted at index zero and processes scheduled commands for sim.tick(). A state-restored hook reconstructs the pending queue from the journal and World checkpoint. installFieldCommandHost(sim) registers the checkpoint schema and callbacks and returns a shared host handle.

## Native integration
During GameModule::setup:
  physis::installTrackedFromMap(sim, map);
  auto host = physis::installFieldCommandHost(sim);
Keep the returned host alive/available to the authoritative command producer. Call host->submit with fully validated complete field snapshots (parseFieldAssets) and authoritative tick/sequence. Never call from an ImGui draw callback or concurrent thread. All operations must execute on the simulation-owner thread.

## Critical limits
- Journal remains external/in-memory; it is NOT persisted in native ECS saves and must be retained/reloaded by host after file deserialize. A new host without the journal cannot replay future commands.
- Snapshot rollback can reconstruct pending commands only if journal history is available and the checkpoint matches the restored World.
- installFieldCommandHost must run exactly once after tracked installation and before persistence registration is sealed. No duplicate-install guard exists.
- No editor Apply UI, networking transport, command validation of arbitrary manually constructed field payloads, or test coverage.
- The persistent checkpoint adds another schema entry; save compatibility requires explicit migration policy.
- The new system is inserted at index zero, ahead of existing systems, but Simulation::step applies its generic commands before all systems. Host must define ordering between gameplay commands and PHYSIS commands.

## Verification
GitHub source write succeeded. Native compilation NOT RUN, CTest NOT RUN, CI NOT VERIFIED, GUI NOT RUN. Do not merge PR #25 until actual validation.

## Next
Add dedicated CTest for Simulation setup, tick ordering, same-tick atomicity, snapshot rollback, journal reconstruction and persistence; fix any compile errors. Then integrate editor Apply via a host-owned submission interface with explicit authoritative validation, not direct World edits. Coordinate external NEREID, TerraWeave, Box3D adapters separately.
