# PHYSIS Stage 42 — fail-closed positive remote receipts when journal writes fail (2026-11-19)

Repository: chromealex/AIVERSE
Branch: feature/physis-nature-simulation
PR: https://github.com/chromealex/AIVERSE/pull/25 (DRAFT)
Previous handoff: docs/agent-handoffs/physis-stage41-ci-validation-2026-11-19.md

## Scope and actual changes

- `cbf6f95f778cf7ac251eb51d5577ae1fec5ee434`
  - `engine/physis/PhysisRemoteServer.h`
  - In `ServerGateway::poll`, after handling a packet, positive
    `Result::Queued`/`Result::Applied` reply states are masked as
    `Result::Unknown` with `scheduledTick=0` and an explicit
    "receipt durability unconfirmed" diagnostic whenever
    `journalHealthy_==false`.
  - This applies to receipt Query, same-ID retries, and immediately
    attempted submissions when the Queued or Applied journal update
    fails. Do not mistake committed in-memory ECS state for durable
    confirmation of its receipt. Existing ACL Denied/Rejected behavior
    is unchanged.

- `174a17c95aacfedfa4fd4f12044f11195e91a3da`
  - `tests/physis_admin_journal_test.cpp`
  - Add `journalWriteFailureNeverAcknowledgesDurableSuccess` to the
    existing `physis_admin_journal` CTest. It injects writer failure
    at the second write (Queued) and third write (post-tick Applied).
    Both scenarios assert no false positive ACK once unhealthy, ECS
    revision=1 after authoritative steps, same-ID reply Unknown,
    Query reply Unknown, and fresh Submit rejected without mutation.

- `95b2a6d2e0148e1a93a45fb96ffc020f7710dc9f`
  - `engine/physis/PhysisRemoteServer.h`
  - An unknown request-ID Query when the journal is unavailable now
    reports suspended retries rather than incorrectly claiming the
    request can safely be retried.

- `039c624896a9dcb45df13a1c721e38af3a007d70`
  - `tests/physis_admin_journal_test.cpp`
  - Assert that unknown IDs during unhealthy journal state never
    return a "safe retry" diagnostic.

- `da51184e9924169f59e743dab217fd21999cf599`
  - `engine/physis/AGENTS.md`: make this fail-closed contract
    explicit for downstream agents.
- `d8deb7ca923b6cb20e2a668920eab3f792a7b08d`
  - `skills/world/aiverse-physis/SKILL.md`: agent workflow guidance.

## Actual verification

- **PASS (connector writes / source inspection only):** GitHub
  update_file returned these commit SHAs. Inspected the current
  `PhysisRemoteServer.h` and existing FileJournal tests to locate
  the failure paths and add test coverage.
- Checked GitHub Actions on this branch. After the latest code changes,
  workflow runs were still pending/queued or explicitly cancelled by
  the branch concurrency policy; **no completed successful native
  build or PHYSIS CTest result was available**. Do not call this PASS.
- **NOT RUN:** full repository checkout, CMake configuration, native
  C++ compile, `physis_admin_journal` CTest, complete PHYSIS suite,
  macOS/Windows editor, OS disk-failure injection or security fuzzing.
  Local checkout remains blocked by DNS for github.com in this
  execution environment.

## Behavioral contract

- The canonical authenticated admin path remains
  `FieldRuntimeState` + `installEditableFromMap` +
  `applyFieldEditCommand`; never install parallel tracked
  `RuntimeFieldRegistry/FieldCommandHost` with it.
- If persistence fails after an authoritative World mutation, the
  in-memory FieldRuntimeState is still actual current ECS state.
  But disk receipt durability is uncertain; outward status Unknown
  is intentional until a safe restart and reconciliation.
- No new ECS component, network message version, save schema, genre
  module or renderer dependency. Water owned by NEREID; terrain and
  foliage by TerraWeave; physics by Box3D.
- Journal + Simulation checkpoint are still separate files. This
  change does NOT offer cross-file atomic exactly-once guarantees.

## Next agent: concrete actions

1. Inspect https://github.com/chromealex/AIVERSE/actions/workflows/physis.yml
   for the latest completed Ubuntu and macOS run; collect actual
   `physis-build.log`, `LastTest.log`, CTest summary and error stack.
2. In a native checkout:
   ```sh
   cmake -S . -B build/physis42 -DCMAKE_BUILD_TYPE=Debug \
       -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_ENABLE_PHYSICS=ON \
       -DAIVERSE_BUILD_EDITOR=OFF -DAIVERSE_BUILD_TOOLS=OFF \
       -DAIVERSE_BUILD_CLIENT=OFF -DAIVERSE_BUILD_HEADLESS=OFF \
       -DAIVERSE_BUILD_SERVER=OFF -DAIVERSE_BUILD_PHYSICS_DEMO=OFF
   cmake --build build/physis42 --parallel 4 --target \
       aiverse_physis_admin_journal_test \
       aiverse_physis_remote_edit_test \
       aiverse_physis_field_runtime_test
   ctest --test-dir build/physis42 --output-on-failure \
       -R '^physis_(admin_journal|remote_edit|field_runtime)$'
   ```
3. If focused tests pass, run all `^physis_` CTests
   including Box3D and editor-off contracts, and repair any actual
   failures. In particular, inspect loopback timing and new
   writer-failure fixtures.
4. Add real OS disk failure injection to the FileJournal path,
   and test admin UI interpretation of Unknown while reconnecting.
5. Log tested results, exact branch SHA, limitations and the next
   step in a new stage handoff. Keep PR #25 DRAFT until native and
   cross-platform validation is genuinely complete.
