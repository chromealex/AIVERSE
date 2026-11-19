# PHYSIS Stage 53 — verify the published inode after journal rename (2026-11-19)

Repository: chromealex/AIVERSE
Branch: feature/physis-nature-simulation
PR: https://github.com/chromealex/AIVERSE/pull/25 (DRAFT)
Previous handoff: docs/agent-handoffs/physis-stage52-startup-revalidation-fsync-2026-11-19.md
Most recent implementation/docs commit before this handoff: 3ebc4151e2fc5420de805805c605c965c456f801

## Goal and architectural context

The canonical genre-independent PHYSIS remote admin endpoint owns a
`JournalProcessLock` and a pinned POSIX FileJournal sidecar. The
authoritative `FieldRuntimeState` World and sidecar are separate
persistence domains (NOT a crash-atomic transaction).

Stages 48–52 added anchored `openat/renameat`, checksum validation,
pre/post-lock identity checks, fail-closed journal health and fsync
error semantics. One gap remained: after a successful `renameat`,
`posix_journal::write` did not explicitly confirm that the visible
journal pathname still resolved to the SAME inode that had been
written and fsynced. Another same-UID process with directory write
access could replace the published name during that window. This
stage adds one more verification boundary; it does not claim to
eliminate all same-user filesystem races.

## Changes, exact files and code commits

1. `206667d1198b4cc124062700730c9388fb4f39de`
   - `engine/physis/PhysisJournalPosixIO.h`
   - The staged temporary fd is `fstat`-verified after writing:
     regular file, single link, current effective UID, no group/
     other permissions and exact size. Then it is fsynced/closed.
   - After `renameat`, `fstatat(directory,name,AT_SYMLINK_NOFOLLOW)`
     verifies the published basename still resolves to the same
     staged device/inode with expected size, ownership and mode.
     Check directory fsync and private ownership before returning
     true. Mismatch returns false, leaving outcome Unknown under
     the existing `ServerGateway` sticky journal health policy.
   - V1 journal bytes, ECS schemas, networking and Windows behavior
     are unchanged. Git blob SHA:
     `2dd114731b498c4156da305915201f97c4848647`.

2. `e8910b199f1467ab18b052da50e9449a674a3760`
   - `tests/physis_journal_posix_io_test.cpp`
   - Standalone POSIX CTest checks a successful atomic replacement
     publishes a distinct inode on the same device; secure regular
     file, single link, current owner and mode; no leaked .next
     temporary file after success.
   - Existing cases still cover missing/valid journal, checksum-
     independent file I/O, embedded NUL names, symlinks, FIFO,
     hardlinks, O_EXCL collision, parent chmod, parent rename and
     Linux O_PATH post-rename directory-fsync failure.
   - Git blob SHA:
     `7c6cd0ef9e330be5b61bf7935bb8d8b0f025a907`.

3. `fc7f9e0b2afb1555d64d5634ea394074b124f88b`
   - `engine/physis/AGENTS.md`: API, safety constraints, actual
     test results and downstream agent instructions.

4. `3ebc4151e2fc5420de805805c605c965c456f801`
   - `skills/world/aiverse-physis/SKILL.md`: Stage 53 skill rules.

## Tests actually executed — precise evidence

The local isolated working directory from Stages 49–52,
`/mnt/data/physis49_smoke`, already had both previous
source-identical files. Applied the EXACT replacements committed
above, then independently checked:
```sh
git hash-object engine/physis/PhysisJournalPosixIO.h \
  tests/physis_journal_posix_io_test.cpp
# 2dd114731b498c4156da305915201f97c4848647
# 7c6cd0ef9e330be5b61bf7935bb8d8b0f025a907

cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
# 1/1 Test #1: physis_journal_posix_io ... Passed
# 100% tests passed, 0 tests failed out of 1
```

Also compiled and ran this exact standalone source:
- Debian GCC 14.2 C++20 `-O2 -Wall -Wextra -Werror -pedantic`
  → `PHYSIS POSIX journal I/O: PASS`.
- Clang 17 C++20 same strict warning flags → PASS.
- Debian GCC 14.2 with `-fsanitize=address,undefined`,
  `-fno-omit-frame-pointer`,
  `ASAN_OPTIONS=detect_leaks=1:abort_on_error=1`,
  `UBSAN_OPTIONS=halt_on_error=1` → PASS.

These are **real native, isolated POSIX filesystem checks**, not
proof that the AIVERSE engine or `physis_admin_journal` compiled.

## Full engine/CI status

- Container still cannot clone GitHub:
  `git ls-remote https://github.com/chromealex/AIVERSE.git ...`
  returned `Could not resolve host: github.com`.
- Checked GitHub Actions for the feature branch: latest inspected
  run `44489842269` was queued; previous runs cancelled by
  workflow concurrency. No completed successful PHYSIS Ubuntu/macOS
  full-build or CTest evidence was available.
- **NOT RUN**: full AIVERSE CMake configuration, real
  `aiverse_physis_admin_journal_test`,
  `aiverse_physis_remote_edit_test`,
  `aiverse_physis_field_runtime_test`, complete `^physis_`
  suite, real editor, macOS/Windows security and networking QA.
- GitHub main comparison: `behind_by=0`, PHYSIS already contains
  current main. No redundant merge commit or main mutation.

## Known limitations and important constraints

- Post-rename inode revalidation narrows the verification window;
  a malicious SAME-UID process may still replace a pathname after
  this final check. The parent journal directory must remain
  private to the authorized server. Advisory flock is not a
  distributed lease/fence.
- A failed post-rename check can mean the new journal bytes are
  visible, but the caller must treat durability as uncertain,
  suspend new edits, and avoid false positive Queued/Applied ACKs.
- FileJournal and authoritative World snapshot remain separate
  non-atomic persistence domains.
- The direct path-based `FileJournal::load/save/attach` legacy APIs
  bypass canonical admin lock ownership. Privileged servers must
  use `LoopbackAdminEndpoint` and `loadPinned/savePinned`.
- On Windows the endpoint uses path-based FileJournal and
  `JournalProcessLock::valid()` only tests held HANDLE; Windows
  ACL/reparse/file-identity parity is still unverified.
- PHYSIS remains genre-independent. NEREID owns water,
  TerraWeave terrain/foliage, Box3D physics. Do not install
  editable FieldRuntimeState with legacy tracked FieldCommandHost.

## Exact next step for next agent

1. Fetch newest **completed** Ubuntu/macOS PHYSIS workflow run from
   https://github.com/chromealex/AIVERSE/actions/workflows/physis.yml
   and inspect `physis-build.log`, CMake config and CTest logs.
   Queued/cancelled statuses are NOT PASS.
2. On an actual full checkout of the feature branch, build and run
   `physis_admin_journal`, `physis_remote_edit`,
   `physis_field_runtime` and `physis_journal_posix_io`, then
   the complete `ctest -R '^physis_'` suite. Fix first actual
   compiler/test failure; record logs.
3. Add deterministic integrated tests for a mismatched receipt
   inode during startup and for a post-rename write failure, with
   gateway remaining Unknown/Uncertain. Do not add flaky sleeps.
4. Complete Windows handle-based journal identity/reparse/ACL
   validation with Windows native CI, then test the editor.
5. Continue mandatory per-stage handoff recording branch/commit,
   changed files, actual tests, limitations and next action.
   Keep PR #25 DRAFT until full native validation is complete.
