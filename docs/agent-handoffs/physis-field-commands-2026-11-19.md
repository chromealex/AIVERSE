# PHYSIS Stage 25 — deterministic tick-boundary authored-field command queue

Repository chromealex/AIVERSE; branch feature/physis-nature-simulation; PR #25 DRAFT.
Code commits 70a8b16af4c287a530b35e3af89a7d39fad767a7 and a2dfd0eed901aab340cef24b7a19c914cced2f53.

## Implemented
engine/physis/PhysisFieldCommands.h adds transient FieldCommandQueue and FieldCommand. enqueue() takes an authoritative monotonically increasing sequence, nondecreasing scheduled tick and a complete desired authored-field set. It rejects duplicate IDs, empty/overlong IDs, oversized sets and commands preceding applied history. applyAtTick(World&, tick) calls reconcileTrackedRuntimeFields at the host's simulation barrier; late commands are rejected. On successful application the command is removed; failed reconciliation leaves the command pending. reset() clears transient queue state.

## API contract
Host installs PHYSIS via installTrackedFromMap first, calls enqueue only with a validated full field set (parseFieldAssets), and calls applyAtTick before advancing PHYSIS systems at the scheduled authoritative tick. Queue itself does not invoke Simulation::addSystem or subscribe to editor events. Commands are full replacement snapshots, NOT patches. The host is responsible for immutable journal storage, multiplayer ordering, rollback replay and reconciling field state after load. The queue is deliberately not an ECS persistent resource. Multiple commands at one tick are applied sequentially, not as one atomic batch.

## Known limitations
Only shallow ID validation occurs at enqueue; caller MUST validate typed components through parseFieldAssets before enqueuing. No command canonical serialization or network transport. applyAtTick may partially commit earlier commands if a later command for the same tick fails; host should validate all inputs before submission and/or add a batch staging transaction. If a host calls applyAtTick at a later tick without processing earlier pending commands, it throws. No editor wiring. C++ compile and CTest not run.

## Verification
GitHub writes succeeded. Native compile NOT RUN; CTest NOT RUN; editor GUI NOT RUN; save/rollback/network test NOT RUN. Keep PR Draft.

## Next
1. Build and add dedicated CTest covering monotonic ticks/sequence, duplicate IDs, late commands, same-tick ordering, rollback reconstruction, and invalid command atomicity.
2. Add host-owned immutable command journal and atomic per-tick batch apply; restore queue from authoritative journal after rollback.
3. Connect MapDoc editor Apply to command submission, not direct World mutation; add runtime save/reload and network tests.
4. Integrate external NEREID/TerraWeave/Box3D adapters without duplicating physics or water ownership.
