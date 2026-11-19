# PHYSIS Stage 26 hardening — restart-safe queued receipt reconciliation (2026-11-19)

Repository: chromealex/AIVERSE
Branch: feature/physis-nature-simulation
PR: #25 DRAFT
Main baseline: 9ee6ff6fbf390bd45363bc92c838f492dfe92eee
Previous full Stage 26 handoff: docs/agent-handoffs/physis-admin-durable-journal-2026-11-19.md
Ownership: PHYSIS natural phenomena; NEREID water; TerraWeave terrain/foliage; Box3D physics. No genre-specific dependency.

## Branch coordination
At the start of this stage, branch HEAD was
a5ddc151f48860677d0ab7b58b07950bf0344ab4.
An independent agent had already committed the Stage 26 admin-only
loopback WebSocket endpoint, challenge-response handshake, and durable
checksummed file receipt journal. The changes below extend those
existing APIs; no second incompatible journal or listener was created.

## Changes implemented

1. fb3f40fe94cad3e55b76ea8a73724b2f6300fef2
   File: engine/physis/PhysisRemoteServer.h
   Corrects ServerGateway::restoreJournal so persisted Result::Queued
   is restored as Result::Unknown with cleared scheduledTick and
   a diagnostic explaining that the in-memory Simulation command queue
   may have been lost on process restart. A previously queued receipt
   is NOT evidence that the command will still execute.

   After restore, the host must first restore the matching ECS World
   checkpoint. ServerGateway::observe handles Unknown:
   - if FieldRuntimeState revision equals expectedRevision+1 AND
     authored fingerprints match, mark Applied;
   - if ECS revision is greater but state mismatches, mark Conflict;
   - if revision remains expected, keep Unknown until the ORIGINAL
     authenticated Submit arrives. The existing ServerGateway::poll
     then re-admits the same original packet only after ACL and
     expected-revision checks and writes its Queued receipt to journal.
   An old Queued record is therefore not prematurely marked Rejected
   just because its pre-crash scheduled tick passed.

2. 7ffe37898d7dc853fe8014e2e501b3335225d2da
   File: tests/physis_admin_journal_test.cpp
   Added queuedReceiptBeforeCrash:
   - journal flushes Queued while Simulation has not stepped;
   - simulated server process/command queue is destroyed;
   - restored journal exposes Unknown, Query returns Unknown;
   - retransmitting same original ID/payload safely schedules one
     new command, eventually yielding Applied and revision 1.
   Added queuedReceiptAlreadyAppliedBeforeCrash:
   - the ECS commit occurs, but server dies before saving Applied;
   - restored save/World revision and fingerprints prove Applied;
   - journal initially restores Unknown, then observe reports Applied.
   Both tests use the existing Test Host, FileJournal, net::Conn loopback
   and typed PHYSIS field command path, not test-only mock state.

3. This handoff/docs commit: inspect git log -1 -- this path.

## Architecture/API
No new externally visible API or default save format. The existing
ServerGateway::restoreJournal, ServerGateway::observe and
ServerGateway::poll still compose with FileJournal::attach and
LoopbackAdminEndpoint::open. The rollback-owned FieldRuntimeState
keeps authoritative revision and field fingerprints. The sidecar
journal has durable intent+receipt records, but is NOT transactionally
coupled to the Simulation save.

The host MUST:
- restore authoritative Simulation World and pending commands for its
  checkpoint BEFORE allowing a new admin connection;
- attach the checksummed FileJournal to the correct server-owned path
  and ensure the restored queued request is not admitted from a
  different authenticated principal or under a changed ACL;
- call observe after each committed tick; allow only dedicated admin
  transport, not normal player InputFrame;
- treat Unknown as unresolved. Never report definite success without
  matching authoritative ECS revision/fingerprint state.

## Tests actually performed
- PASS (source/GitHub): GitHub connector wrote both commits using a
  fast-forward expected-HEAD ref update. Exact modified files
  re-fetched; source patches inspected. Previous Stage 26 CTest
  target physis_admin_journal and workflow registration remain intact.
- AUTHORED, NOT RUN: two new native fixtures described above.
- NOT RUN: CMake/clang native compile, CTest, CI PASS, sanitizer,
  macOS GUI, real loopback socket, TLS/WSS external tunnel, real
  server checkpoint + journal crash testing. GitHub Actions
  may be queued; do not infer success.

## Limits
- This fix does not provide cross-file crash-atomicity between
  FileJournal and Simulation save. An edit applied in memory but
  not included in the restored ECS checkpoint may need re-execution.
  The rollback/revision protocol prevents duplicate effects on the
  restored state but cannot reconstruct every lost crash-era result.
- The server journal is bounded (256 entries), currently not a
  distributed transaction log. Only one outstanding edit is
  admitted at a time. No cross-server leadership/fencing.
- The generic PHYSIS admin endpoint is loopback-only ws:// and
  requires authentication and ACL. Off-machine access needs a
  separately authenticated encrypted tunnel; native WSS support
  is not claimed.
- A second independent RuntimeFieldRegistry/TrackedFromMap path
  exists on this branch. Do not install it simultaneously with
  the canonical FieldRuntimeState/installEditableFromMap pipeline.
- Full-field command payload remains capped at 32760 bytes.

## Next agent exact steps
1. Obtain a native AIVERSE checkout, build:
   cmake -S . -B build/physis26 -DAIVERSE_BUILD_TESTS=ON
   cmake --build build/physis26 --target aiverse_physis_admin_journal_test aiverse_physis_remote_edit_test -j4
   ctest --test-dir build/physis26 --output-on-failure -R '^physis_(admin_journal|remote_edit|field_runtime)$'
   Repair actual errors; do not claim PASS based only on source.
2. Run a real loopback-only websocket service with the existing
   authenticated connector; verify credentials, deny paths, crash
   between journal fsync and Simulation step, crash after step
   before journal Applied flush, and network reconnect. Verify
   both restored queued states described above.
3. For strict crash-durable exactly-once receipts, couple the
   request outcome journal and World checkpoint in one durable
   transaction (or WAL replay with authoritative state and
   fencing). Implement safe per-process exclusive journal ownership.
4. Reconcile the duplicate opt-in tracked/editable field
   registries before releasing to game projects. Retain NEREID,
   TerraWeave, Box3D and genre boundaries.
5. Keep PR #25 draft; after each stage record changed files,
   tested evidence, current SHA and next step in a handoff.
