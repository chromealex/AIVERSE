# PHYSIS Stage 51 — fail-closed pinned admin startup and NUL-safe POSIX journal names (2026-11-19)

Repository: chromealex/AIVERSE
Branch: feature/physis-nature-simulation
PR #25: https://github.com/chromealex/AIVERSE/pull/25 (DRAFT)
Previous handoff: docs/agent-handoffs/physis-stage50-post-read-integrity-2026-11-19.md
Branch HEAD immediately before this handoff: 9b1594428f434b2eaae750cdd03febe98ff5f22c

## Status at start

- Verified PR #25 head was `b03337bcead0e3d9346098d1135b025de9677cdb`,
  and the PHYSIS branch remained **0 commits behind main**.
- Checked latest GitHub Actions PHYSIS runs for this feature ref:
  statuses were queued or superseded/cancelled, with no completed
  successful full-engine native compiler/CTest evidence.
- Attempted local `git ls-remote https://github.com/chromealex/AIVERSE.git HEAD`;
  it failed `Could not resolve host: github.com`. There is no full
  AIVERSE checkout in this container, so the integration test could
  be authored and reviewed but NOT compiled/run as an engine executable.

## Implementation

1. `48217081ed601b9a1a4600a0f262aad5572e2b46`
   - `tests/physis_admin_journal_test.cpp`
   - New `pinnedEndpointRejectsUnsafeStartupJournal()`, registered
     under existing POSIX-only `physis_admin_journal` CTest.
   - Uses real `FileJournal` and `LoopbackAdminEndpoint::open` to
     cover valid empty V1 receipt journal startup; intentionally
     corrupted magic/checksum refusal; lock release after failed
     startup (another `JournalProcessLock` can acquire the path);
     rejection of a symlink and a FIFO journal file; successful
     endpoint startup on the repaired same path.
   - Ensures the listener is never intentionally opened after a
     failed pinned journal load. No production admin protocol
     changes are required for these contracts.
   - GitHub source blob SHA at creation:
     `8d11e3835ebf9fb7f57f01ae2629911ac0c94016`.
   - This integrated test is **AUTHORED, NOT EXECUTED**.

2. `2a7fbedd00cb09ef3b80d5e4c61e4191d4643372`,
   `3d0b5c0ba923b31f4a118332026a43fe708b70a5`
   - `engine/physis/PhysisJournalPosixIO.h`
   - Reject embedded NUL in POSIX journal basenames and
     unpredictable temp suffixes. Otherwise `name.c_str()` could be
     truncated by the POSIX filesystem APIs.
   - No binary journal schema change, ECS registration change,
     genre-specific dependency or Windows behavior change.
   - Final source blob SHA:
     `3e00b8ec1508c438f401c240eaa3aa38ab90bbd1`.

3. `cb2cb3554944763286c2e127042d68c68da89752`
   - `tests/physis_journal_posix_io_test.cpp`
   - Source fixture for invalid journal name and random suffix with
     embedded NUL, preserving existing positive read and write
     tests.
   - Final test source blob SHA:
     `a2393a8d09a75429d9e291a537682c72dff09e3f`.

4. `f4965210a817ac866c4850bcccef6155ce8b8bd8`
   - `engine/physis/AGENTS.md`: documented API, test and status.
5. `9b1594428f434b2eaae750cdd03febe98ff5f22c`
   - `skills/world/aiverse-physis/SKILL.md`: AI agent follow-up.

## Tests ACTUALLY EXECUTED

**PASS — source-identical isolated POSIX CMake/CTest**

- Updated the existing local isolated workspace
  `/mnt/data/physis49_smoke` with exactly the new two modified
  low-level sources. Executed `git hash-object` for the header and
  test; both matched the GitHub blob SHAs above.
- Ran:
  ```sh
  cmake --build /mnt/data/physis49_smoke/build -j2
  ctest --test-dir /mnt/data/physis49_smoke/build --output-on-failure
  ```
  Observed: `1/1 Test #1: physis_journal_posix_io ... Passed`,
  `100% tests passed, 0 tests failed out of 1`.
  This uses a separate one-target CMake harness and is NOT
  the AIVERSE root CMake project.
- Compiled/ran the exact files with
  `-std=c++20 -O2 -Wall -Wextra -Werror -pedantic` using GCC,
  then Clang: both printed `PHYSIS POSIX journal I/O: PASS`.
- Compiled/ran with GCC `-fsanitize=address,undefined`
  (`ASAN_OPTIONS=detect_leaks=1:abort_on_error=1`,
  `UBSAN_OPTIONS=halt_on_error=1`): printed PASS and returned 0.

**NOT RUN / unresolved**

- Full `aiverse_physis_admin_journal_test` integration scenario
  including `pinnedEndpointRejectsUnsafeStartupJournal`.
- Full engine CMake configure/build and CTest, full net/Simulation
  integration, Ubuntu/macOS GitHub Actions completion, Windows
  path/ACL/reparse validation, real editor and production server.
- A successful isolated low-level module does NOT establish
  success of the high-level endpoint integration.

## Architecture and limits

- Canonical privileged admin must acquire
  `JournalProcessLock`, read `FileJournal::loadPinned` before
  listener binding, and use the protected writer
  `FileJournal::savePinned` during request/tick processing.
  Invalid or unreadable journal fails closed, with no fresh editor
  admission. A corrected file requires a fresh safe endpoint.
- Legacy direct `FileJournal::load/save/attach` remains a separate
  uncoordinated path; never bypass canonical endpoint locking for
  privileged clients.
- Client `RemoteClient` must continue to report Uncertain, never
  falsely Applied after journal loss.
- Journal and authoritative World checkpoints are not a single
  crash-atomic transaction; distributed fencing is not provided.
- PHYSIS remains genre-independent. NEREID owns water, TerraWeave
  terrain/foliage, Box3D physics. Canonical FieldRuntimeState and
  legacy RuntimeFieldRegistry/FieldCommandHost are mutually exclusive.

## Next agent concrete step

1. Obtain a native AIVERSE checkout or completed PHYSIS CI job logs.
   Inspect actual compiler or CTest failures before deciding next fix.
2. Build `aiverse_physis_admin_journal_test` and run
   `ctest --test-dir <build> --output-on-failure -R '^physis_admin_journal$'`.
   Verify this new fixture with real `LoopbackAdminEndpoint`
   and POSIX lock+FileJournal. Run `physis_remote_edit`,
   `physis_field_runtime`, and all other PHYSIS tests afterward.
3. Investigate any native runtime failure, in particular the
   post-failure journal lock release, file permission errors,
   symlink/FIFO refusal and restart path.
4. Add a native integration test for a changed receipt inode during
   startup/restore and deterministic fault injection after
   `renameat`/directory fsync. Extend Windows handle-based
   journal security separately.
5. Record actual build/test evidence, touched files, resulting
   branch SHA and next step in the next dedicated handoff.
   Keep PR #25 DRAFT until all relevant native tests pass.
