# PHYSIS Stage 44 — sticky journal uncertainty and manual same-ID recovery (2026-11-19)

Repository: chromealex/AIVERSE
Branch: feature/physis-nature-simulation
PR: https://github.com/chromealex/AIVERSE/pull/25 — DRAFT
Previous handoff: docs/agent-handoffs/physis-stage43-client-journal-backoff-2026-11-19.md
Implementation/docs HEAD before handoff: e429da3fe85b12429b2a8eb603daa66cc6d9c590

## Why this stage

Stage 43 suspended automatic remote editor Submit when the server returned
Result::Unknown with a journal-unavailable diagnostic. Two gaps remained:
1. RemoteClient::disconnected() converted Uncertain into Offline, so a
   reconnect could silently re-arm automatic Query/Submit.
2. A delayed pre-failure positive Queued/Applied receipt could arrive after
   the journal-unavailable Unknown and inadvertently re-arm client traffic.
The user also needed a verified source fixture for explicit recovery and
healthy Unknown automatic retransmission.

## Changes and commits

1. `8e8a443d6f66e35cbe2c387d15395f285d0fa013`
   - engine/physis/PhysisRemoteClient.h
   - disconnected() now preserves ClientStatus::Uncertain. A journal failure
     cannot be converted to an ordinary connection timeout.

2. `233c3188010176284925b475c6ee886cc9af04b8`
   - tests/physis_remote_edit_test.cpp
   - `manualRetryAfterJournalRecovery`: injected failure on third journal
     write (post-tick Applied receipt); retain prior durable Queued record,
     reconcile a new ServerGateway against actual committed ECS state, ensure
     client remains Uncertain after reconnect and sends nothing until explicit
     retry(), then Query same request ID and receive Applied without a second
     ECS revision.
   - `ordinaryUnknownStillRetriesOriginal`: restore an unsent request,
     Query unknown ID against healthy ServerGateway, automatically resubmit
     the SAME ID, and assert a single applied revision / request cache entry.

3. `1e9cae0bd5aa859ecdfad5caa04283523bf93d51`
   - engine/physis/PhysisRemoteClient.h
   - Add `suspendedForJournal_` latch. While set, delayed Receipt packets
     cannot clear Uncertain or rearm a Submit. Explicit user retry() clears
     the latch; begin/restore resets it for the new active request.
   - No changes to kVersion, binary packet shape, save manifest or ECS.

4. `accbb3a8c9f48d4c9fb27806126f9be66b1a8514`
   - tests/physis_remote_edit_test.cpp
   - Inject delayed pre-failure Queued receipt and verify it is ignored
     while Uncertain, with no automatic application message emitted.

5. `b2c2c4eabd3dd784d9554b9091397a1fec3fef54`
   - tests/physis_remote_edit_test.cpp
   - Explicit <map> include for durable journal fixture.

6. `67d91b68685644dd671f84590d579670ba64d410`
   - engine/physis/AGENTS.md — agent ownership/recovery contract.

7. `e429da3fe85b12429b2a8eb603daa66cc6d9c590`
   - skills/world/aiverse-physis/SKILL.md — AI skill guidance.

## Architecture and API

- Canonical editing is still FieldRuntimeState + installEditableFromMap +
  authenticated ServerGateway over net::Conn. Legacy Tracked/FieldCommandHost
  is a separate opt-in and must not run simultaneously.
- ClientStatus::Uncertain is NOT proof that an ECS edit was rejected or applied.
  It expresses an unresolved durable receipt. The client must retain the
  exact original instance/request ID and submission envelope.
- `retry()` is a deliberate operator action, NOT an automatic fallback.
  It queries the original request before Submit; a healthy server with the
  prior journal + matching ECS checkpoint can return Applied directly.
- Regular Unknown responses without journal-unavailable diagnostics retain
  the existing same-ID automatic resubmission flow.
- This mechanism does NOT make FileJournal and Simulation checkpoint into
  a crash-atomic transaction. No TLS, genre or rendering coupling added.
  PHYSIS owns natural simulation; NEREID water; TerraWeave terrain/foliage;
  Box3D physics.

## Tests ACTUALLY performed

- PASS: GitHub connector updated files successfully and returned all commit
  SHAs; source changes and test registration were inspected.
- Local container `git ls-remote https://github.com/chromealex/AIVERSE.git HEAD`
  FAILED with `Could not resolve host: github.com`, so full checkout
  and compilation were unavailable.
- GitHub Actions query on PHYSIS branch after Stage 44 code commit:
  latest run `44480756450` for `b2c2c4ea` was QUEUED; earlier runs were
  CANCELLED under branch concurrency. No successful completed PHYSIS CTest
  or compiler report was available.
- AUTHORED but **NOT RUN**: new manual retry, ordinary Unknown, delayed
  Queued fixtures; native CMake, CTest, sanitizer, editor, macOS/Windows
  platform QA. Do not claim PASS for these integration tests.

## Next agent — exact actions

1. Inspect newest completed GitHub Actions jobs for BOTH Ubuntu and macOS,
   including uploaded physis-build.log and Testing/Temporary/LastTest.log.
   Do not infer a pass from queued/cancelled status.
2. On a checkout, configure/build and run the focused fixtures:
   ```sh
   cmake -S . -B build/physis44 -DCMAKE_BUILD_TYPE=Debug \
      -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_ENABLE_PHYSICS=ON \
      -DAIVERSE_BUILD_EDITOR=OFF -DAIVERSE_BUILD_TOOLS=OFF \
      -DAIVERSE_BUILD_CLIENT=OFF -DAIVERSE_BUILD_HEADLESS=OFF \
      -DAIVERSE_BUILD_SERVER=OFF -DAIVERSE_BUILD_PHYSICS_DEMO=OFF
   cmake --build build/physis44 --parallel 4 --target \
      aiverse_physis_remote_edit_test aiverse_physis_admin_journal_test \
      aiverse_physis_field_runtime_test
   ctest --test-dir build/physis44 --output-on-failure \
      -R '^physis_(remote_edit|admin_journal|field_runtime)$'
   ```
3. If green, run all `^physis_` CTests with optional Box3D, then
   actual macOS editor and security/rollback smoke. Fix real failures.
4. Specifically verify the test writer's Queued checkpoint matches the
   restored World; inspect manual retry and delayed receipt ordering.
   Audit application UI handling of Uncertain across disconnect and
   explicit user Retry, plus the restart/recovery policy on native servers.
5. Update mandatory next-stage handoff with tested logs, changed files,
   exact branch SHA and concrete next step. Keep PR #25 DRAFT until
   genuine native validation.
