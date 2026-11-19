# PHYSIS Stage 38 — queued remote receipt rollback regression (2026-11-19)

Repository: chromealex/AIVERSE
Branch: feature/physis-nature-simulation
PR #25 DRAFT
Implementation commit: 4e7b773bd5a266bec5f090aa4ca3f2198985da11

## Changes
- tests/physis_remote_edit_test.cpp: adds queuedReceiptInvalidatedByRollback() to existing physis_remote_edit CTest.
- Uses real net::makeLoopbackPair, RemoteClient, ServerGateway and canonical FieldRuntimeState GameModule.
- Submits an authenticated remote edit without stepping Simulation; asserts no Applied and revision 0, disconnects, restores initial SimSnapshot, calls ServerGateway::observe, reconnects and polls without stepping. Asserts no false Applied and unchanged revision.
- Complements existing appliedReceiptAfterRollback test; no production API or schema changed.

## Actual verification
- PASS: GitHub update_file returned code commit.
- AUTHORED, NOT RUN: native C++ compilation, CTest, macOS/Linux CI, real WebSocket, TLS and server crash. No runtime PASS claims.

## Important limitations
- This test does not assert an exact terminal status after rollback because the client/server may legitimately report Unknown, Conflict or pending while awaiting authoritative reconciliation. It strictly forbids false Applied.
- This is in-process loopback and snapshot rollback, not durable FileJournal process restart.
- Tests added across Stages 32–38 remain unexecuted; failures must be fixed before merge.
- The separate FieldCommandHost journal still requires architecture reconciliation; no silent conversion of legacy save manifests.

## Next agent
1. Configure/build physis_remote_edit and physis_field_runtime; run focused CTest and capture logs.
2. Add durable FileJournal crash/restart rollback fixture with real authenticated admin endpoint and confirm receipt uncertainty semantics.
3. Audit stale queued receipt handling and command queue restoration; repair any false success or resubmission.
4. Preserve PHYSIS genre independence and NEREID/TerraWeave/Box3D boundaries; keep PR draft.
