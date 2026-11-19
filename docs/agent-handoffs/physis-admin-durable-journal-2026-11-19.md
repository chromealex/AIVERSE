# PHYSIS Stage 26 — Durable admin receipts and authenticated local server endpoint

Date: 2026-11-19
Repository: chromealex/AIVERSE
Branch: feature/physis-nature-simulation
PR: #25 DRAFT
Main baseline: 9ee6ff6fbf390bd45363bc92c838f492dfe92eee (ancestor)
Previous stage: docs/agent-handoffs/physis-remote-edit-reliable-2026-11-19.md

## Implemented commits, in order

- 03285866c4e765de5b90a757ba5a92fa00be4077 — engine/physis/PhysisRemoteServer.h adds optional journal writer, persisted intent before enqueue, atomic status persistence and failure-closed admission.
- 9747174f6310398f4b830449636d1d51ccb2b8eb — new engine/physis/PhysisRemoteJournal.h: bounded journal codec, FNV hash checksum, atomic disk replacement, recovery, writer attachment.
- 54d0e44be239647c52bddf1362af97f4500b16c0 — recover Unknown durable intents from authoritative ECS or safely retry exact original request on authenticated resubmission.
- 7460b9475a1729bba1cb261a71fcc142769ac35b — new engine/physis/PhysisAdminAuth.h: secret-preserving challenge/response using OS CSPRNG and keyed BLAKE2b, principal bound to proof.
- 8eb231252a5e4d5769d0bcf0004b5936f8c48314 — new engine/physis/PhysisAdminEndpoint.h: native loopback-only WebSocket listener and editor connector.
- 44012d5d9e378351fe1cb388737a88750d23707a — tests/physis_admin_journal_test.cpp: journal corruption/reload, fail-closed, recovered intent, auth proof/wrong key and actual localhost WS fixture.
- d3b0c9db282ae9d1c8a41792b22bdf96a4876d01 — apps/editor/PhysisRemoteEdit.h integrates optional local admin connector, external host credentials and authenticated session handoff to EditorLink.
- 994ccc507d2425e5dc0dea38f602b80d772f23ed — CMake target aiverse_physis_admin_journal_test, CTest physis_admin_journal, PHYSIS and Studio CI build registrations.
- 278cef6248eb77c4994819861208c346fc0943f3 — PHYSIS window polls ongoing handshake before editor session becomes attached.
- 9e6b7440c2ee489c39c49787f2824a0564461e47 — admin listener now requires a reserved game-specific field command type and enforces it even with broad ACL.
- edfcc1f2fb99a361ec48de89ccb41b41f1e64fb8 — handshake deadline, closed-connection handling, secret wiping, and visible editor errors.
- c31f8bfd5eeffc93462829162d1a5022064f5116 — CSPRNG-named exclusive temporary journal files, flush/fsync and atomically replace the existing journal on POSIX/Windows.
- 356102204de440c3b6d922a0a6a2dffdf686368f — journal restoration is restricted to the reserved PHYSIS command type, previously Applied receipts are reconciled with current ECS field contents.
- Current docs/AGENTS/skill change: inspect git log -1 -- docs/agent-handoffs/physis-admin-durable-journal-2026-11-19.md for the exact final SHA.

CONCURRENT WORK: other PHYSIS agents inserted Stage 27/28 commits between Stage 26 commits (PhysisFieldCommandHost.h and docs/agent-handoffs/physis-live-authority-audit-2026-11-19.md). No Stage 27/28 files were overwritten. The Stage 28 audit identifies canonical live editor authority as Stage 23/24 FieldRuntimeState + installEditableFromMap + GameModule::applyCommand; the parallel RuntimeFieldRegistry/FieldCommandHost API must NOT be installed simultaneously.

## APIs and data flow

Stage 25 ServerGateway is extended with setJournalWriter(callback), restoreJournal(saved), persistJournal(), journalHealthy(), and setReservedCommandType(gameType). It persists an Unknown/intention receipt BEFORE submitting a command; if the disk writer fails, it deletes that transient entry and rejects admission. After successful submission it persists Queued, then persistently updates to Applied/Rejected/Conflict once authoritative Simulation has passed the scheduled tick. No editor/renderer mutation directly touches World.

FileJournal(path).attach(gateway,error) MUST run BEFORE opening the listener and BEFORE processing any request. It validates the entire existing file and refuses corrupted/incompatible data; absent file is a new journal. Records include authenticated principal, client instance/request IDs, byte-identical original Submit frame, desired field kind/fingerprint set, scheduled tick, expected revision and receipt status. Schema version 1; bounded 16 MiB, max 256 entries. File is written to unpredictable O_EXCL 0600 temporary file (POSIX) or CREATE_NEW (Windows), flushed/fsynced, atomically replaces previous and syncs its parent dir on POSIX. Admin server must own the journal directory exclusively and must not open two writer processes for one path. Reading a corrupted file is fail-closed (no silent loss of deduplication history).

When reloaded against a persisted Simulation, an Unknown intent with expected+1 revision and matching full set of authored fingerprints can be recovered as Applied. If the original request did not commit (revision is unchanged), re-submitting the EXACT original packet under the SAME authenticated principal, ID and ACL can queue it again. A newer/conflicting ECS revision makes it Conflict. Cached Applied is revalidated after rollback; content mismatch revokes that receipt. This recovery depends on the host restoring the correct Simulation save/pending commands BEFORE accepting admin traffic. A World checkpoint and the external journal are separate files, NOT a single atomic database transaction; if a crash interrupts both at incompatible points, the result may be Unknown/Conflict. Do not claim crash-proof exactly-once ACK when such a transaction is not implemented.

Admin authentication uses net::secureRandom challenge (32 bytes) plus net::keyedHash/BLAKE2b proof bound to the ASCII principal. Neither client nor server transmits its 32-byte secret; net::equalBytes performs constant-time comparison. Key lookup and account/permission lifecycle belong to the host. The handshake is not TLS by itself. Native AIVERSE connectWebSocket DOES NOT SUPPORT wss:// at present. Consequently the concrete LoopbackAdminEndpoint uses WsListener::open(port,true), never a public ws:// binding. External remote use requires a separate authenticated encrypted tunnel/TLS-terminating proxy or host-owned secure net::Conn. Plain remote ws:// is NOT secure; refuse it.

Native opt-in server example:
    LoopbackAdminEndpoint service(privateJournalPath);
    service.open(localPort, reservedPhysisCommandType,
                 verifiedKeyLookup, canReadAcl, canEditAcl, &error);
    // after restoring Simulation snapshot and command queue:
    service.poll(authoritativeSim, net::nowMs());
    authoritativeSim.step();         // native host controls timestep
    service.afterTick(authoritativeSim);

Editor host integration:
    editor::physis_remote::link().connectLocalAdmin(
        "ws://127.0.0.1:<private-port>",
        serverPrincipal, secretFromSecureStorage,
        cryptographicallyRandomPersistedClientInstance,
        serverAssignedPlayerSlot,
        reservedPhysisCommandType);
    // PHYSIS window polls connector and shows receipt/status.

The server GameModule must explicitly call installEditableFromMap and dispatch applyFieldEditCommand from GameModule::applyCommand. It must not install the separate tracked FieldCommandHost simultaneously. The admin server ACL MUST be bound to ServerHandshake's authenticated principal, not to Command.player, editor field ID, clientInstance or query parameters. The reserved type MUST be a nonzero game-owned PHYSIS command ID.

## Tests and verification, truthful

PASSED: GitHub connector created commits and updated branch refs. Source, native net::WsListener, tcp loopback binding, net::Crypto and Simulation/FieldRuntimeState APIs were inspected. Basic brace and patch anchor checks were carried out.

TEST SOURCE CREATED, NOT RUN: physis_admin_journal verifies bounded journal encode/decode and checksum tamper rejection, disk save/reload and restored Applied receipt, fail-closed absent parent dir, Unknown intent retry, correct/wrong secret proof over net::Conn, and a native localhost WebSocket handshake + remote edit (no public socket). Existing physis_remote_edit and physis_live_edit continue to be registered. CMake, PHYSIS Linux/macOS and Studio CI were updated to include aiverse_physis_admin_journal_test. No compiler output or CTest PASS is available. GitHub Actions workflows were observed QUEUED, not PASS.

NOT VERIFIED: clang/CMake compile, CTest results, macOS editor GUI, IPv6 binding, live TLS proxy, real server credentials/ACL integration, Windows file rename behavior, power-loss replay, concurrent server writers, full rollback checkpoint/journal atomicity, sanitizer and malicious network fuzzing.

Local container lacks an AIVERSE checkout; github.com DNS was unreachable when a read-only git ls-remote was attempted. Do not claim native compilation succeeded.

## Next agent: concrete actions

1. Update native branch checkout. First compile AIVERSE targets and fix any actual C++ compiler errors:
       cmake -S . -B build/physis26 -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_EDITOR=ON
       cmake --build build/physis26 --target aiverse_physis_admin_journal_test aiverse_physis_remote_edit_test aiverse_editor -j4
       ctest --test-dir build/physis26 --output-on-failure -R '^physis_(admin_journal|remote_edit|live_edit|field_runtime)$'
   Run clang ASan/UBSan and Windows/macOS when available. Record actual logs PASS/FAIL; source code alone is NOT verification.
2. Run the loopback admin listener against a local PHYSIS GameModule explicitly using installEditableFromMap; configure a randomly generated 32-byte key from an OS secret store and a private journal directory. Check denied/wrong-key, no-public-bind, reconnect, same-ID replay, rollback and corrupted journal fail-close. Keep auth keys out of logs and committed files.
3. Persist or atomically checkpoint Simulation state/command stream and request journal as ONE coordinated transaction (or integrate request IDs into a versioned rollback-owned ECS component) before promising power-loss exactly-once ACK across arbitrary save/restart boundaries. Ensure recovery of Queued intents after restart without replaying unverified commands.
4. For off-machine editing, provide real native TLS/wss support or a documented encrypted authenticated local tunnel. Never publish plain ws:// on a public address. Add endpoint auth rate limits and account revocation.
5. Unify/deprecate parallel tracked vs canonical editable Runtime registries after call-site audit. Keep PHYSIS ECS genre-neutral and keep NEREID/TerraWeave/Box3D ownership separated.
6. Continue with a separate handoff for every code stage; keep PR #25 in DRAFT until actual test and security evidence is available.
