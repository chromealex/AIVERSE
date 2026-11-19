# PHYSIS Stage 40 — cross-process journal ownership and crash recovery (2026-11-19)

Repository: chromealex/AIVERSE
Branch: feature/physis-nature-simulation
PR: #25 (DRAFT)
Previous handoff: docs/agent-handoffs/physis-stage39-journal-lock-test-2026-11-19.md
Last known stage code/doc commit before this handoff: 73422cc49a90c1980c729124f12bda10bb7740a7

## Architecture / changes

1. d8840871064b9f5093e3801d1be6e34736232cf1
   - engine/physis/PhysisAdminEndpoint.h
   - Declare JournalProcessLock before listener, journal, gateway and peer objects.
     C++ reverse-order member destruction now retains exclusive file ownership
     until the endpoint's active transport resources are torn down.
   - Still advisory, single-host locking; do not delete/replace an active
     <journal>.lock inode or bypass the lock using direct uncoordinated writers.

2. 7016ff75c49c39d89cb98049341f08d903000113
   - tests/physis_admin_journal_test.cpp
   - killedProcessReleasesJournalLock: POSIX fork, readiness pipe, second OS
     process denies competing flock, forced SIGKILL without RAII destruction,
     waitpid and successful reacquire.
   - endpointJournalOwnershipLifecycle: real LoopbackAdminEndpoint instances
     deny concurrent opens of a single journal and allow open after destruction.
     This fixture runs on native Windows too, but NOT on Emscripten.

3. bf547fb1b3b82bd5ff26bca1e0a951e506f29cd3
   - tests/physis_admin_journal_test.cpp
   - realLoopbackWebSocket test now destroys the endpoint before deleting its
     temporary journal + lock directory. It previously unlinked a live lock
     inode and could invalidate the protection.

4. 638bd4e58d341f23c681f1db0b120d9718527e52
   - tests/physis_admin_journal_test.cpp
   - killedEndpointRecoversQueuedJournal: persist valid Queued request record,
     start a separate child LoopbackAdminEndpoint, ensure it normalizes Queued
     to Unknown, reject second endpoint while child owns lock, SIGKILL child,
     open new endpoint, retransmit the exact original request under a trusted
     server-side principal + ACL through a loopback net::Conn, advance native
     Simulation ticks, persist Applied, and reopen with matching restored World
     to confirm the Applied receipt.
   - POSIX-only crash fixture, no change to default network or save schema.

5. a70a037dd3a29c8db98a0b8aa53b94df40ecb579
   - engine/physis/AGENTS.md: document lock lifetime and recovery rules.
6. 73422cc49a90c1980c729124f12bda10bb7740a7
   - skills/world/aiverse-physis/SKILL.md: update agent instructions.

## Tests actually performed

- **PASS (isolated Linux smoke only):** Manually copied the exact current
  PhysisJournalProcessLock.h into an isolated local workspace and built a tiny
  C++20 two-process harness with Debian GCC 14.2:
  `c++ -std=c++20 -Wall -Wextra -Werror -pedantic lock_smoke.cpp -o lock_smoke`
  Output: `PASS: process lock conflict + SIGKILL release + reacquire`.
  This proves the standalone POSIX lock header behavior in the isolated
  environment, **NOT** any full repository/CTest integration.
- **PASS:** GitHub connector returned the implementation and docs commit SHAs,
  and updated test source was re-fetched to inspect preprocessor guards and
  main() registration.
- **FAILED precondition:** `git ls-remote https://github.com/chromealex/AIVERSE.git HEAD`
  from the local container: could not resolve github.com DNS.
- **NOT RUN:** CMake configure, AIVERSE native compilation, the new
  physis_admin_journal CTest, macOS/Windows build, actual full server
  process checkpoint crash fuzz, editor QA, sanitizers, external secure TLS.
  Tests in the repository remain AUTHORED, not verified passed.

## Build/test follow-up

On a checkout of this exact branch:

```sh
cmake -S . -B build/physis40 -DAIVERSE_BUILD_TESTS=ON
cmake --build build/physis40 --target \
  aiverse_physis_admin_journal_test \
  aiverse_physis_remote_edit_test \
  aiverse_physis_field_runtime_test -j4
ctest --test-dir build/physis40 --output-on-failure \
  -R '^physis_(admin_journal|remote_edit|field_runtime)$'
```

Then run all `^physis_` CTests and platform-specific configurations.
On POSIX, verify both fork/SIGKILL fixtures; on Windows add a native
independent CreateProcess/TerminateProcess test rather than treating the
single-process endpoint fixture as a substitute.

## Known limitations / next step

- `JournalProcessLock` is advisory and only works if all processes use the
  same persistent lock inode on a local filesystem. Lock paths must live in
  a trusted directory; the current implementation does not supply distributed
  leader fencing or a coordinated journal + ECS World transaction.
- The test uses a synthesized valid Queued sidecar before crashing the child
  endpoint, then a new authoritative Simulation host. It does not simulate
  a simultaneous power loss of both World checkpoint and sidecar.
- Canonical FieldRuntimeState/editable admin route remains separate from the
  legacy RuntimeFieldRegistry/FieldCommandHost journal; never run both.
- Next agent should compile and fix real errors, run the entire focused suite,
  implement Windows independent-process and filesystem failure-injection
  coverage, and reconcile dual command hosts without silent save migration.
- Keep PR #25 **DRAFT** until compiler/CTest and manual runtime evidence exist.
- Per-stage handoff is mandatory and must record actual tests, not assumed pass.

Ownership: PHYSIS nature; NEREID water; TerraWeave terrain/foliage;
Box3D physics. No genre or renderer coupling introduced.
