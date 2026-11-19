# PHYSIS Stage 56 — bounded standalone POSIX receipt journal I/O (2026-11-19)

Repository: chromealex/AIVERSE
Branch: feature/physis-nature-simulation
PR #25 (Draft): https://github.com/chromealex/AIVERSE/pull/25
Previous handoff: docs/agent-handoffs/physis-stage55-interrupted-temp-recovery-2026-11-19.md
Code commits: 892d9695ca90c34bc8428a00ba35bec08c95a8ea and db487cc5ee9e1f382eca5ff72f807dba9525ef54
Documentation: this handoff and matching AGENTS.md/Skill instructions in the subsequent documentation commit.

## Goal and issue

The serialized FileJournal V1 sidecar is bounded to 16 MiB. Stages
49–55 extracted raw POSIX I/O to a standalone header to allow native
testing without ECS, but raw io::read accepted arbitrarily large
maxBytes and io::write accepted buffers larger than FileJournal's
bound. A direct noncanonical caller could bypass the serialized
journal limit and write an oversized file, potentially preventing
a later safe restart.

## Implementation / API

- engine/physis/PhysisJournalPosixIO.h:
  new constexpr kMaxJournalBytes = 16*1024*1024. read() now fails
  closed before opening a file if minBytes>maxBytes or maxBytes
  exceeds the journal ceiling. write() refuses empty or oversized
  input BEFORE creating the .next-* file. The previously implemented
  owner/mode/inode checks, O_EXCL, fsync and renameat remain.
  Exact GitHub blob: ab24c98afebc3dbe45a05244b1a94e6eec977ccf.
- engine/physis/PhysisRemoteJournal.h:
  compile-time static_assert on POSIX that FileJournal::kMaxDiskBytes
  equals posix_journal::kMaxJournalBytes, preventing future drift.
  Source blob after commit: 9d1bb78e4f0afb4062fd7c3184f1ba643973dc2e.
- tests/physis_journal_posix_io_test.cpp:
  create a 16 MiB + 1 vector, assert write rejected, no temp file
  created; invalid read max and inverted min/max fail, output
  remains untouched. Existing orphan temp, symlink, FIFO, inode,
  chmod, collision and O_PATH failure cases remain.
  Exact GitHub blob: d3ef56264ab8da3324375e7b8d76bf1a1ea64851.
- engine/physis/AGENTS.md and skills/world/aiverse-physis/SKILL.md:
  instructions and actual verification status.
- No change to PHYSIS ECS schema, receipt wire protocol, V1 on-disk
  layout, NEREID water, TerraWeave terrain, Box3D physics,
  FPS/RTS logic or editor binding. CMake test targets unchanged.

## Actual verification

The attached local Linux workspace /mnt/data/physis54_smoke contained
the exact source changes. git hash-object printed:
- ab24c98afebc3dbe45a05244b1a94e6eec977ccf
- d3ef56264ab8da3324375e7b8d76bf1a1ea64851

These match the GitHub blob IDs from the committed source.

Ran:
```sh
cmake --build /mnt/data/physis54_smoke/build --parallel 2
ctest --test-dir /mnt/data/physis54_smoke/build --output-on-failure
```

Actual output:
```text
1/2 Test #1: physis_journal_locked_io ... Passed
2/2 Test #2: physis_journal_posix_io ......... Passed
100% tests passed, 0 tests failed out of 2
```

Additional native Linux verification:
- GCC 14.2 C++20 compiled the standalone CTest targets with
  -Wall -Wextra -Werror -pedantic.
- Clang 17.0 C++20 strict warnings compiled and ran the updated
  standalone POSIX I/O test: PHYSIS POSIX journal I/O: PASS.
- GCC 14.2 with AddressSanitizer + UndefinedBehaviorSanitizer
  (leak detection and halt on UB enabled) ran and printed PASS.

**NOT RUN / NOT VERIFIED:**
- Full AIVERSE root CMake configure and build, including the new
  compile-time assertion in PhysisRemoteJournal.h.
- physis_admin_journal, physis_remote_edit, physis_field_runtime
  and the complete PHYSIS test suite.
- macOS, Windows, editor/WebSocket, real Box3D.
- GitHub Actions: newest run was pending/queued, both Ubuntu and
  macOS jobs queued and older runs cancelled. No full-engine PASS.
- A complete local Git checkout failed because github.com DNS is
  unavailable to this container.

## Limitations

- Standalone I/O helpers operate on a borrowed pinned directory fd;
  canonical privileged writers MUST retain JournalProcessLock and
  check valid() before and after writes. Limits do not replace
  authentication or process-exclusive ownership.
- Journal + authoritative World checkpoint remain separate,
  non-atomic persistence domains; an ambiguous crash outcome must
  remain Unknown/Uncertain, not falsely Applied.
- Windows journal I/O remains path based without POSIX security
  equivalence. No distributed fencing provided.
- PHYSIS is genre independent; NEREID owns water, TerraWeave owns
  terrain/foliage and Box3D owns physics.

## Concrete next step

1. Obtain the first completed GitHub Actions ubuntu-latest/macos-latest
   jobs and inspect actual CMake/CTest logs, or obtain a full checkout.
2. Build aiverse_physis_admin_journal_test,
   aiverse_physis_remote_edit_test and
   aiverse_physis_field_runtime_test; verify the new static_assert
   in the full engine context.
3. Run ctest --output-on-failure -R '^physis_' including both pinned
   journal tests and optional Box3D.
4. Fix actual native compiler/test failures, record logs and commit
   SHA in the next per-stage handoff. Keep PR #25 DRAFT until then.
