# PHYSIS Stage 55 — interrupted temporary journal recovery (2026-11-19)

Repository: chromealex/AIVERSE
Branch: feature/physis-nature-simulation
PR #25 DRAFT: https://github.com/chromealex/AIVERSE/pull/25
Code + agent docs commit: eac7bfc6c6da57bc6370dd186dd6314020ef293f
Previous handoff: docs/agent-handoffs/physis-stage54-locked-io-test-2026-11-19.md

## Why this stage

The canonical privileged editor journal uses a checksummed V1 receipt
file and writes it atomically through a temporary `receipts.next-*`
file followed by `renameat` and directory `fsync`. If the process
crashes before rename, a temporary file may remain. Such an orphan is
not committed journal history and must not be mistaken for an applied
or queued request. Existing `FileJournal::loadPinned` reads the
canonical journal path only.

The low-level POSIX helper also previously permitted an empty
`bytes` argument. Even though the canonical `FileJournal` encoder
normally never produces empty bytes, a direct caller could replace a
valid journal with an invalid zero-byte file.

## Implementation and changed paths

Single atomic GitHub tree/commit update (not one CI run per file):
- `engine/physis/PhysisJournalPosixIO.h`: reject empty
  `posix_journal::write(..., bytes,...)` payload before temp
  creation, preserving previous canonical contents.
  Git blob SHA: `aebe122d3c6318d7ac0d05eb6623cef214742cc6`.
- `tests/physis_journal_posix_io_test.cpp`: new scenarios
  create a malformed abandoned `receipts.next-abandoned` temp file,
  verify `read` treats the still-absent canonical journal as missing,
  reject an empty write without any temp file, publish a valid journal
  through a different suffix, and prove the abandoned temp does not
  disturb correct reads/writes. The fixture deliberately preserves
  the orphan until the test explicitly cleans it up.
  Git blob SHA: `04722f9262c7f939f6bc51628098bc789c3c38aa`.
- `engine/physis/AGENTS.md` and
  `skills/world/aiverse-physis/SKILL.md`: document strict
  committed journal semantics, source tests and limitations.

No changes to V1 receipt serialization, server authority, ECS
resource schemas, networking protocol, CMake target names, or
genre/module ownership. The existing `physis_journal_posix_io`
CTest and `physis_journal_locked_io` CTest are unchanged as
registered build targets. NEREID water, TerraWeave terrain/foliage
and Box3D physics remain separately owned.

## Verification ACTUALLY performed

Working directory: `/mnt/data/physis54_smoke` in the isolated Linux
container. It contains exact copies of the committed production
headers and the two standalone POSIX tests, not the entire engine.
The two edited sources were checked by `git hash-object`; their
SHA values match those returned by GitHub `create_blob` before
constructing the atomic commit tree.

**PASS — isolated CMake/CTest with GCC 14.2**
```sh
cmake --build /mnt/data/physis54_smoke/build -j2
ctest --test-dir /mnt/data/physis54_smoke/build --output-on-failure
```
Actual output: `2/2 Passed, 0 Failed`:
`physis_journal_locked_io` and `physis_journal_posix_io`.

**PASS — isolated strict compiler and sanitizers**
- GCC 14.2 C++20 `-O2 -Wall -Wextra -Werror -pedantic`;
  program output: `PHYSIS POSIX journal I/O: PASS`.
- Clang 17 C++20 same strict flags; output PASS.
- GCC C++20 AddressSanitizer + UndefinedBehaviorSanitizer
  (`-fsanitize=address,undefined -fno-omit-frame-pointer`,
  leak detection on, UBSAN halt); output PASS and exit code 0.

**NOT RUN / NOT VERIFIED**
- Full AIVERSE root CMake configuration and native compilation.
- `aiverse_physis_admin_journal_test`,
  `aiverse_physis_remote_edit_test`,
  `aiverse_physis_field_runtime_test`, full PHYSIS suite,
  actual editor and WebSocket transport, macOS/Windows.
- GitHub Actions for the PHYSIS branch: latest inspected was
  queued, older runs cancelled; no completed native build/test log.
  The container cannot resolve github.com for `git clone` or
  `git ls-remote`.
- GitHub compare `main..feature/physis-nature-simulation`
  before implementation reported `behind_by=0`; no main merge
  or main mutations performed.

## Known limitations

- An abandoned temp file is intentionally NOT automatically removed
  at startup. Safe deletion would need an explicit trusted cleanup
  policy under the process lock, filesystem inspection and tests.
- If the crash happens after `renameat` but before directory
  `fsync`, the current journal may be visible without proven
  crash durability. `ServerGateway` must preserve
  Unknown/Uncertain fail-closed behavior.
- There is no joint crash-atomic transaction between the journal
  sidecar and the authoritative Simulation World checkpoint.
  Do not promise exactly-once effect/acknowledgment guarantees.
- Full Windows owner ACL/reparse identity validation remains
  separate, as does coexistence prevention between canonical
  Editable `FieldRuntimeState` and the legacy Tracked registry.

## Exact next step

1. Obtain a completed GitHub Actions PHYSIS Ubuntu/macOS run or a
   full local checkout; collect native compiler logs, not queued
   status. Build the full `aiverse_physis_admin_journal_test`
   target and execute its pinned startup and crash-recovery fixtures.
2. Repair the first actual native integration failure, then run
   `physis_remote_edit`, `physis_field_runtime`,
   the two standalone journal tests and all `^physis_` CTests.
3. If needed, add a deterministic fault-injection fixture in the
   canonical endpoint between intent persistence and `renameat`,
   checking that restart never fabricates an Applied receipt from an
   abandoned temp file.
4. Keep PR #25 Draft until the full cross-platform checks pass.
   Every stage needs a new handoff recording exact branch/commit,
   files, APIs, tests ACTUALLY run and their results.
