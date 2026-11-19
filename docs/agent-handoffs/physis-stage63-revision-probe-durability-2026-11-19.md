# PHYSIS Stage 63 — fail-closed remote revision probe when journal unhealthy

Date: 2026-11-19
Repo: chromealex/AIVERSE
Branch: feature/physis-nature-simulation
PR #25 (DRAFT): https://github.com/chromealex/AIVERSE/pull/25
Implementation commits:
- 67cc79cf73dcd79e46720eaf2be695d5a7abc4fc
- 607202a833a8af95e27668ebe27ba0cd3fddf9c1
- 369d24bd379556007cbe2e7234cb20ab85cb3496
- a0f078d738a1636fa41309911edc30a6b81bf67c
Latest branch commit at beginning of handoff: a0f078d738a1636fa41309911edc30a6b81bf67c
Previous: docs/agent-handoffs/physis-stage62-targeted-integration-runner-2026-11-19.md

## Finding

RemoteClient's fresh request ID revision probe had a special
`Result::Unknown` handler that unconditionally set
`knownRemoteRevision_=true` from `message.receipt.revision`.
When ServerGateway's journal had already become unhealthy, it
reported Unknown with diagnostic `kJournalRetrySuspended`, and
still included the current authoritative World revision. The
client mistakenly interpreted that revision as proof of an
operable durable editing route, potentially unblocking editor
submissions despite the server's fail-closed journal state.

Prior Stage43/44 safety logic already suspended resend on
unhealthy-journal Unknown for OUTSTANDING edits; revision probes
bypassed that guard.

## Changes and API behavior

- engine/physis/PhysisRemoteClient.h
  - In `RemoteClient::poll` when receiving the outstanding
    revision probe reply, distinguish ordinary `Result::Unknown`
    from the two stable journal-unavailable diagnostic strings,
    using existing `journalRetrySuspended(detail)`.
  - For journal-unavailable Unknown: `knownRemoteRevision_=false`,
    preserve the receipt, set `ClientStatus::Uncertain`, finish
    the probe. Do not advertise a usable editing baseline and do
    not automatically retransmit or submit.
  - After an explicitly initiated probe on a fresh healthy
    gateway, a normal Unknown again sets the authoritative
    revision and `knownRemoteRevision_=true`. If the client has
    no active edit and was Uncertain, change to Idle and clear
    the previous journal failure diagnostic via `Receipt{}`.
  - Current Git blob: 872e61c380fee748cf219341caa6178d35fd5de1.
- tests/physis_remote_edit_test.cpp
  - New `revisionProbeStopsOnUnhealthyJournal()` fixture:
    `ServerGateway::suspendJournal()` with real simulated
    `Simulation`, `RemoteClient` and `net::makeLoopbackPair()`;
    asserts probe terminates without a known remote revision,
    client status Uncertain and journal suspension diagnostic.
  - Simulates a restarted, healthy ServerGateway and requests a
    NEW revision probe explicitly, verifies known revision 0,
    client status Idle, diagnostic cleared, and no commands
    admitted or World revision incremented.
  - Registered under existing native `physis_remote_edit` CTest.
  - Current Git blob: 0c9eaaa6c95f2e37a15ee7180cacacbd283cfc9f.
- engine/physis/AGENTS.md and
  skills/world/aiverse-physis/SKILL.md:
  per-stage agent instructions and honest test status.
- No changes to remote wire format or receipt enum, V1 FileJournal
  schema, ECS component layout, Box3D, TerraWeave or NEREID.
  PHYSIS remains game-genre-independent.

## ACTUAL verification vs outstanding tests

- Re-fetched current GitHub feature branch source: verified the
  new probe diagnostic guard, healthy recovery logic and
  fixture's registration in test main. Checked exact blob SHAs.
- Rebuilt and ran local standalone native Linux POSIX journal
  suite in `/mnt/data/physis54_smoke/build-staged`:
  `cmake --build ... --parallel 3`
  `ctest --test-dir ... --output-on-failure`
  Actual results:
  `physis_journal_posix_io` Passed,
  `physis_journal_locked_io` Passed,
  `physis_journal_crash_recovery` Passed,
  100% tests passed, 0 failed out of 3.
- **NOT RUN:** full native `physis_remote_edit` CTest, the new
  `revisionProbeStopsOnUnhealthyJournal()` high-level fixture,
  `physis_admin_journal`, `physis_field_runtime`,
  full root AIVERSE configure/build, editor, macOS/Windows.
- Native CI runs for PHYSIS remained queued or cancelled, without
  successful Ubuntu/macOS compiler logs. Local GitHub DNS
  `git ls-remote` failed; full repository checkout unavailable.

## Safety constraints and remaining limitations

- The EditorLink's submit() checks hasRemoteRevision(); with
  this fix journal-unavailable revision probes no longer
  incorrectly open that gate. A true server authorization and
  healthy durable journal remain mandatory before any edit.
- When an existing remote edit becomes Uncertain, retain original
  instance/request identity and require manual reconciliation;
  no automatic new ID or duplicate edit.
- The durable sidecar and authoritative World checkpoint are
  not one crash-atomic transaction; an uncertain outcome remains
  Unknown until safely recovered.
- PR #25 is 0 commits behind main but is still Draft/dirty
  against its separately diverged feature branch. Do not merge
  that unrelated base without explicit instruction.

## Next step

1. Run on full native macOS/Linux AIVERSE checkout:
   `bash tools/physis-validate-native.sh --integration --no-physics --jobs 4`.
   This builds the real `physis_remote_edit` and native admin/
   runtime tests, with configure/build/ctest logs saved.
2. Check new revision probe fixture first, then all
   `physis_admin_journal` and `physis_field_runtime` cases.
   Fix first ACTUAL compiler/test error, not hypothetical logs.
3. Independently collect completed GitHub Actions Ubuntu/macOS
   build/CTest logs; queued/cancelled jobs are not PASS.
4. Run `--full` and editor-enabled PHYSIS authoring tests when
   integration is green. Publish Stage64 handoff with exact SHA,
   touched files, tests actually run and next step.
