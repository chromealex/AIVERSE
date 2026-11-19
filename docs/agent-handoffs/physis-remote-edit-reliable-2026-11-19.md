# PHYSIS Stage 25 — Reliable remote editor transport (2026-11-19)

Repository: chromealex/AIVERSE
Branch: feature/physis-nature-simulation
Pull request: #25 DRAFT.
Main baseline: 9ee6ff6fbf390bd45363bc92c838f492dfe92eee (already ancestor).
Previous: docs/agent-handoffs/physis-editor-live-edit-2026-11-18.md

## Actual implementation commits and files

- ff3f56bf: engine/physis/PhysisRemoteProtocol.h; versioned Submit, Query and Receipt envelopes using existing Simulation Command encoding, packet bounds and ACK/NACK states.
- fade5bf0: engine/physis/PhysisRemoteServer.h; authenticated principal/ACL gateway, bounded deduplication and authoritative revision/fingerprint post-tick verification.
- fc61cc8b: C++ typed-width protocol header parsing correction.
- 8cb695b2: engine/physis/PhysisRemoteClient.h; retries, reconnect Query-before-Submit, original ID preservation and pending export/restore.
- 07a8cb7c: tests/physis_remote_edit_test.cpp; protocol, network and reconnect fixture source.
- 4a9f76ab: removed incomplete unused test fixture.
- ed62c285: CMake target aiverse_physis_remote_edit_test, linking aiverse_net, and PHYSIS + Studio workflow entries.
- 340e7b60: client initial revision discovery via authenticated unknown-ID Query.
- 2337cdfd: apps/editor/PhysisRemoteEdit.h, opt-in bridge for already-authenticated net::Conn.
- a1dbe8cf: apps/editor/windows/PhysisWindow.cpp remote editing panel for revision/status/ACK/retry controls.
- d14c0566: end-to-end editor bridge over net::Conn loopback test source.
- 8f8ba13f: server detects rollback invalidating an Applied receipt; client rechecks Applied on reconnect.
- 62d09c05: rollback-after-ACK and initial version Query tests.
- 94763b39: preserve unresolved identity through timeout/reconnect.
- 5945267a: serialize edit admissions to one pending request, preventing false double Applied ACKs.
- 8e089061: concurrent admission rejection test.
- 4b982670: bounded revision query retries and explicit denied feedback.
- 2685cb8d: delayed/jittered net::Conn loopback fixture.
- Stage documentation and handoff are committed separately; find latest via git log.

## Protocol

The separate PHYSIS remote admin protocol runs on net::Conn (reliable
UDP, WebSocket or in-memory loopback). It is NOT the standard player
InputFrame and grants no permission by itself. Envelope version 1,
clientInstance (nonzero random token assigned by the host),
requestId (monotonic), message kind Submit / Query / Receipt,
Result Unknown / Queued / Applied / Rejected / Conflict / Denied.
Packet maximum 40 KiB; nested Stage 23 field batch remains bounded
by 32760 bytes (4096 Command.args including length). Any unknown
version, oversized frame, truncated command or wrong nested packet
is rejected before allocation/application.

## Server public API

Include engine/physis/PhysisRemoteServer.h.
ServerGateway::poll(authoritativeSimulation, authenticatedConn,
trustedPrincipal, canReadAcl, canWriteAcl, monotonicMs);
ServerGateway::observe(authoritativeSimulation) AFTER each committed step.

The principal comes from the SERVER AUTHENTICATED CONNECTION, not from
Command.player, packet clientInstance or packet requestId. The project
server must supply the ACL and a dedicated authenticated editor endpoint.
Gateway internally calls Stage 24 admitAuthorizedFieldEdit, which
constructs a new trusted system/neutral Command for Simulation::submit.
The project GameModule still dispatches applyFieldEditCommand from
applyCommand at the scheduled tick. Gateway never invokes Simulation::step
or directly writes ECS fields.

The dedupe key is (trustedPrincipal,clientInstance,requestId), with the
exact original packet bytes stored in a bounded cache. Retransmission
of the same bytes only returns a previous receipt; reuse of ID with
different packet is rejected. Maximum 256 cached requests; only
terminal entries inactive for >=10 minutes can be evicted. Pending
entries cannot be evicted to accept new work. The gateway serializes
new admissions while one PHYSIS edit is pending, so matching desired
content cannot produce a false double ACK.

Queued means accepted for submission only. After the scheduled tick,
the gateway checks FieldRuntimeState revision and all authored binary
fingerprints. Applied is emitted only for a matching actual ECS state.
Wrong revision or content, dropped handler, rollback or invalid request
receives a definite NACK. An already Applied cached receipt is rechecked
against rollback to avoid telling a reconnecting editor that rolled-back
changes still exist.

## Client and editor

Include engine/physis/PhysisRemoteClient.h.
RemoteClient(clientInstance) exposes requestRevision, begin,
poll(net::Conn,nowMs), disconnected, retry, exportOutstanding and
restoreOutstanding. On reconnect, first Query original request ID,
then resend only if Unknown, with identical bytes and ID; never blindly
create a second edit. Retry timer is separate from ECS and bounded.
A previously Applied receipt is queried again after reconnect; if
server history was lost, report uncertainty rather than invent success.

apps/editor/PhysisRemoteEdit.h provides EditorLink. A project host must
call EditorLink::attachAuthenticated with an authenticated connection,
client instance, assigned slot and reserved PHYSIS command type.
The editor never opens an anonymous admin socket or guesses credentials.
PHYSIS window now has Remote Editing status, confirmed server revision,
request ID, refresh, send and retry-same-ID UI. Stage 24 local editing
panel remains independent.

## Source test coverage

New CTest: physis_remote_edit. Actual net::Conn loopback route is used,
not direct ECS function calls. Source fixtures include protocol version/
corruption, submit/query/receipts, ACL denial, duplicate replay, altered
payload under existing ID, one-at-a-time admissions, revision discovery,
latent/jittered network delivery, reconnect after lost ACK, pending
export/restore after client restart, server cache loss and stale revision,
rollback after prior Applied receipt and editor bridge end-to-end.

## Verification status (do NOT invent PASS)

PASS: Github connector accepted source commits and CMake/CI test registration.
Source inspection of net::Conn, Simulation, native ECS mapping, and
basic code syntax/brace balances was performed.

NOT RUN: clang/C++ compile, CMake build, CTest, macOS PHYSIS editor
visual QA, real UDP and WebSocket listener/authentication integration,
server crash durability, security fuzzing, sanitizer and CI PASS.
PHYSIS GitHub Actions jobs were previously observed queued, not passed.

## Known limitations and coordination

1. ServerGateway is VOLATILE during one server lifetime. After SERVER
   RESTART without a durable journal, the old request may be already
   applied; the Stage 23 expectedRevision guard prevents reapplication,
   but the server can no longer produce a definite old Applied receipt.
   Client reports uncertain/conflict. This is not crash-durable
   exactly-once delivery; add a rollback-owned or durable admin journal
   before production.
2. Native host must actually authenticate connections (prefer WSS/
   tickets), expose its own privileged endpoint, bind its ACL to that
   principal, and reserve/dispatch GameModule PHYSIS command type. This
   stage does not modify the normal AIVERSE battle player-input relay,
   nor ship an open admin listener. No automatic FPS/RTS GameModule opt-in.
3. Caller supplies a collision-resistant client instance. Pending
   envelope can be exported/restored; the host must safely persist
   request-ID high-water mark or rotate client instance for independent
   new sessions.
4. Large full-map edits exceed the Stage 23 Command.args limit; no
   atomic multipart/typed delta protocol yet.
5. Another agent added PhysisRuntimeFields.h plus
   installTrackedFromMap; that is a SEPARATE opt-in registry from the
   Stage 23 FieldRuntimeState / installEditableFromMap used here.
   Do not enable both. Unify before shipping one canonical authority.
6. PHYSIS remains genre-neutral. NEREID owns water, TerraWeave owns
   terrain/foliage and Box3D owns rigid-body dynamics.

## Next agent exact steps

1. From native checkout:
   ./run-tests.sh --filter '^physis_'
   cmake -S . -B build/physis25 -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_EDITOR=ON
   cmake --build build/physis25 --target aiverse_physis_remote_edit_test aiverse_editor -j4
   ctest --test-dir build/physis25 --output-on-failure -R '^physis_(remote_edit|live_edit|field_runtime)$'
2. Fix actual compiler errors and warnings. Run native macOS editor UI and
   verify status, disconnection, retry, changing IDs and save/undo.
3. Add an authenticated real server admin Conn listener/route (not normal
   player InputFrame) with WSS/ticket authentication and actual principal ACL.
   Test permissions, reconnect, duplicate and malformed packets.
4. Add durable server-side receipt/request journal for restart-safe
   exactly-once RESULTS and reliable ACK/NACK after restart/rollback.
5. Reconcile competing Stage 23/24 tracked/editable registry models,
   implement bounded atomic batch deltas if large-map support is needed.
6. Keep PR #25 draft until native test and security QA actually PASS.
   Write a standalone handoff after every subsequent code stage.
