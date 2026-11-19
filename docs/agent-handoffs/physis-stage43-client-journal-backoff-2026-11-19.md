# PHYSIS Stage 43 — journal-unavailable client retry suppression (2026-11-19)

Repository: chromealex/AIVERSE
Branch: feature/physis-nature-simulation
PR: https://github.com/chromealex/AIVERSE/pull/25 (DRAFT)
Previous handoff: docs/agent-handoffs/physis-stage42-durable-receipts-2026-11-19.md

## Motivation / actual defect

Stage 42 made the authoritative ServerGateway mask unpersisted Queued /
Applied receipts as `Result::Unknown` when its journal is unhealthy.
But the existing RemoteClient handled **every** Unknown as proof the
server lost the request and immediately re-sent Submit with the same ID.
When journal dedupe is unavailable, this is the wrong behavior:
it causes needless traffic and may lead to ambiguous retries.
The client must retain a pending request and require explicit recovery.

## Implementation and commits

1. `70174150df442d086358a5c08249762e84c4c82f`
   `engine/physis/PhysisRemoteProtocol.h`
   - Add stable V1 diagnostic constants
     `kJournalDurabilityUnavailable` and `kJournalRetrySuspended`,
     and pure `journalRetrySuspended(detail)`.
   - The existing Result enum, kVersion=1, binary Receipt layout,
     journal schema IDs and codec are unchanged.

2. `6ebe144a2f4302a6eb6ed4c7c982629df9190c82`
   `engine/physis/PhysisRemoteServer.h`
   - Use shared constants in the two existing journal failure paths.
     The positive state masking and new-edits fail closed policy from
     Stage 42 remain intact.

3. `66b1ca1ef23933f19362e141fe7fd9cbc3ff0fe1`
   `engine/physis/PhysisRemoteClient.h`
   - On Unknown + journalRetrySuspended(detail), enter
     `ClientStatus::Uncertain` and suppress automatic sending.
   - The original active request is retained for manual same-ID retry.
   - Ordinary Unknown still resubmits the same ID as before. No new
     server authority, transport, genre dependency or ECS schema.

4. `2944a9776ae0cfbea3d80f637b67c6b107093ac6`
   `tests/physis_remote_edit_test.cpp`
   - New `unhealthyJournalStopsAutomaticResubmit` fixture, invoked
     by existing `physis_remote_edit` CTest.
   - Inject writer failure on second (Queued) and third (Applied)
     durability write, drive real net::Conn loopback and canonical
     FieldRuntimeState ECS ticks. After a journal-unavailable Unknown,
     assert client Uncertain, retained original outstanding request
     and no further application Submit/Query frames on automatic polls.

5. `b8e3a157eafbde4f7954622d0533bdda41635722`
   `engine/physis/AGENTS.md`: per-agent protocol contract.
6. `00c2515daeb1db346a7b0e274e55bd2d2ea9a07d`
   `skills/world/aiverse-physis/SKILL.md`: AI implementation guidance.

## Actual verification and limitations

- GitHub connector wrote the code/test/docs commits and returned concrete
  SHAs; protocol/client/server changes were inspected in the branch.
- Queried PR #25 and PHYSIS Actions: the latest code run
  `44479953591` was **queued**, while earlier source runs were
  `cancelled` by the concurrency policy. No compiler/CTest success
  or failure log was available for this feature HEAD.
- A local `git ls-remote` against github.com failed DNS resolution.
  Therefore **NOT RUN**: AIVERSE CMake, native C++ compile, PHYSIS
  CTest, real editor UI, macOS/Windows platform QA.
- The V1 retry-suspension signal uses two **exact** stable receipt
  detail values rather than a new binary enum. Do not silently alter
  those strings in older servers; if protocol evolves, replace this
  compatibility convention with an explicit versioned typed reason.
- A journal-unavailable server's in-memory ECS state may differ from
  its last durable World checkpoint. Unknown/Uncertain is never
  proof of Applied or Rejected. Cross-file crash atomicity remains
  unresolved. Client retry() is an explicit user action only.
- Canonical FieldRuntimeState/Editable admin path must not be
  installed alongside tracked RuntimeFieldRegistry/FieldCommandHost.
  NEREID owns water, TerraWeave terrain/foliage, Box3D physics.

## Next agent exact steps

1. Inspect https://github.com/chromealex/AIVERSE/actions/workflows/physis.yml
   for the latest **completed** Ubuntu/macOS run, including uploaded
   `physis-build.log` and `Testing/Temporary/LastTest.log`. Do not
   claim PASS from queued or cancelled runs.
2. On a local checkout, run:
   ```sh
   cmake -S . -B build/physis43 -DCMAKE_BUILD_TYPE=Debug \
     -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_ENABLE_PHYSICS=ON \
     -DAIVERSE_BUILD_EDITOR=OFF -DAIVERSE_BUILD_TOOLS=OFF \
     -DAIVERSE_BUILD_CLIENT=OFF -DAIVERSE_BUILD_HEADLESS=OFF \
     -DAIVERSE_BUILD_SERVER=OFF -DAIVERSE_BUILD_PHYSICS_DEMO=OFF
   cmake --build build/physis43 --parallel 4 --target \
     aiverse_physis_admin_journal_test aiverse_physis_remote_edit_test \
     aiverse_physis_field_runtime_test
   ctest --test-dir build/physis43 --output-on-failure \
     -R '^physis_(admin_journal|remote_edit|field_runtime)$'
   ```
3. Repair any actual compile/CTest errors. Specifically verify the
   new client test's polling intervals and net::Conn loopback queue.
   Extend with a manual explicit retry after recovery and test that a
   missing-ID non-journal Unknown still automatically recovers.
4. Run full `^physis_` suite and real editor + Box3D smoke once
   focused tests pass; log actual evidence and branch SHA.
5. Continue mandatory per-stage handoff. Keep PR #25 DRAFT until
   native CI and all relevant integration tests pass.
