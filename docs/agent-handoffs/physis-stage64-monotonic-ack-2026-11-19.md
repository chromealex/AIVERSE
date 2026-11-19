# PHYSIS Stage 64 — monotonic receipt state and Query-only Applied recovery

Date: 2026-11-19
Repository: chromealex/AIVERSE
Working branch: `feature/physis-nature-simulation`
PR: https://github.com/chromealex/AIVERSE/pull/25 (**DRAFT**)
Last source HEAD before documentation: `1e5d9e282f0b0720d7ba570ba2866efba1afd76e`
Implementation commits:
- `79890dc1c74e34b97325d90b7a09d55c17c14497`
- `1e5d9e282f0b0720d7ba570ba2866efba1afd76e`
Handoff/docs commit: the commit introducing this file (inspect `git log -1 -- docs/agent-handoffs/physis-stage64-monotonic-ack-2026-11-19.md`).
Prior: `docs/agent-handoffs/physis-stage63-revision-probe-durability-2026-11-19.md`.

## Motivation

After receiving an authoritative Applied receipt, the existing RemoteClient
could still process delayed Queued or Unknown receipts from the SAME
`(clientInstance,requestId)` and overwrite terminal Applied, leading
to unexpected automatic retransmit, a blocked next edit or an unverified
rollback of UI state. Reconnect verification of a previous Applied receipt
was vulnerable to a stale Queued arriving before a new verification Query.
After a Query timeout, explicit `retry()` could not re-check the Applied
request because it refused all terminal receipt types.

## Code and API changes

1. `engine/physis/PhysisRemoteClient.h`
   - Ignore any active request receipt after a terminal status unless in
     an explicit post-reconnect Applied verification.
   - Applied verification ignores stale responses received BEFORE its Query
     was sent, and any Queued receipt (which is not authoritative proof
     against the prior Applied). Only a fresh status response can resolve
     the check.
   - A missing prior Applied receipt (Unknown) becomes Uncertain with a
     sticky `suspendedForReconcile_` guard. Subsequent delayed frames,
     including Queued, cannot auto-rearm sending. User must retry.
   - `retry()` after an Applied verification timeout re-arms a **Query-only**
     check of the original request ID; never re-Sends the old Submit on
     that path. For ordinary unresolved requests manual retry retains
     its existing same-ID behavior.
   - `begin()` rejects a new edit in Uncertain, during Applied
     verification or while an Applied edit is disconnected.
     Preserve original request and host-side admission authority.
   - No public wire contract or storage format change.
2. `tests/physis_remote_edit_test.cpp` — existing native target, no
   extra CMake registration needed:
   - `delayedReceiptCannotUndoApplied`: inject late Queued, Unknown,
     Rejected, Conflict, Denied on the actual loopback net::Conn after
     Applied; verify it remains Applied, new edit may complete.
   - `appliedRecheckNeedsFreshQueryAndManualReconcile`: old Queued
     around reconnect; restarted gateway missing prior receipt;
     expect Uncertain and no automatic retransmit; manual same-ID
     retry with authoritative revision check prevents second ECS effect.
   - `appliedRecheckTimeoutCanRetryOnlyQuery`: no server responds to
     Applied verification until bounded retry exhaustion; block new
     edit; manual retry emits only Query and accepts reverified Applied.
3. Documentation: `engine/physis/AGENTS.md`,
   `skills/world/aiverse-physis/SKILL.md`,
   `docs/physis.md`, and this dedicated stage handoff.

## Verification — precise status

- **Source verified:** used the connected GitHub repository to read
  prior Stage 63 handoff/client/fixtures, then atomically committed the
  two source changes with branch-head lease checks. The branch was
  **0 commits behind main** when checked; main was NOT modified.
- **Static check:** reviewed the new state branches and verified the
  expected named test fixture registrations in the repository source.
  This is not equivalent to compiling or running them.
- **No actual PHYSIS native CTest performed in this stage.** The
  container's `git ls-remote https://github.com/chromealex/AIVERSE.git HEAD`
  failed: `Could not resolve host: github.com`, so there is no complete
  local checkout for the high-level tests.
- The GitHub connector returned **0 pull-request workflow runs** and
  **0 combined statuses** for implementation commit
  `1e5d9e282f0b0720d7ba570ba2866efba1afd76e`. This is NOT PASS.
  Previous Stage 63 standalone POSIX journal 3/3 is unrelated and was
  **not rerun** here. macOS editor, Linux full AIVERSE, Box3D, native
  security integration and CTest are all **NOT RUN**. Keep PR DRAFT.

## Known limitations / security

- AIVERSE protocol V1 has no independent Query nonce or server epoch.
  Receiving an old Applied frame AFTER the new Query is sent remains
  indistinguishable from a new Applied reply on the same request ID.
  Stage 64 improves local state monotonicity, not cryptographic
  anti-replay or crash-atomic exactly-once semantics. A protocol V2
  Query correlation and server generation would close this gap.
- The journal and the authoritative World checkpoint remain separate
  durability domains. Unknown/Conflict is honest when reconciliation
  cannot prove Applied. Do not grant editor permissions from packet IDs.
- Server authentication/ACL, loopback-only WS, NEREID water authority,
  TerraWeave terrain/foliage, Box3D and shared genre-independent ECS
  remain unchanged. Stage 23 FieldRuntimeState is still canonical;
  do not enable alternate RuntimeFieldRegistry and command hosts together.

## Next agent: concrete next step

1. On the REAL AIVERSE checkout (macOS/Linux) at the PHYSIS branch:
   `bash tools/physis-validate-native.sh --integration --no-physics --jobs 4`
   Preserve configure/build/ctest/summary logs. Focus first on
   `physis_remote_edit` and all three Stage 64 fixtures.
2. Fix **actual** compile/CTest failures (including Conn receive timing)
   before claiming PASS. Then validate `physis_admin_journal`,
   `physis_field_runtime`, and full PHYSIS/Box3D/authoring targets.
3. For further reliability, consider protocol V2 Query correlation
   nonce/epoch with a strict downgrade/compatibility plan; do NOT
   silently alter V1 journal or packet decoding.
4. Recheck current main ancestry; pull ONLY main into the PHYSIS
   working branch if it is truly behind, never modify main.
   Publish a new stand-alone handoff after the next stage.
