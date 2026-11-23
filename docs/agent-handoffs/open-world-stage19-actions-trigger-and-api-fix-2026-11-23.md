# AIVERSE Open World — W1.19: native CI triggered and public SpatialWorld API repair

Date: 2026-11-23
Repo: `chromealex/AIVERSE`
Branch: `feature/aiv20261123_open_world_audit`
Draft PR: https://github.com/chromealex/AIVERSE/pull/116
System W1 / OW-01 status: **partial, unmerged, no native PASS**
Previous: `docs/agent-handoffs/open-world-stage18-native-acceptance-gate-2026-11-23.md`

## Scope / facts established

Stage18 added `.github/workflows/open-world-w1.yml` to build and
run all 11 native W1 test targets under GitHub-hosted `macos-15`,
but the branch had no observed run. Stage19 checked branch status
and the GitHub Actions run list, and inspected the public
`engine/world/SpatialWorld.h` method visibility against the
real `engine/actor/ActorRegionAdmission.cpp` source.

Observed branch divergence at stage start:
- 205 commits ahead of `main`.
- 41 commits behind `main`.
- PR #116 is Draft.

## Changes made

1. `.github/workflows/open-world-w1.yml`:
   - Added `push` trigger restricted to
     `feature/aiv20261123_open_world_audit` and the W1
     source/CMake/test/workflow paths.
   - Retained `pull_request` and `workflow_dispatch`, and
     the same hosted macOS test target list.
   - This change **actually caused** a GitHub Actions run to
     be created. Commit `f977005e3d0830ee38c5917df764cb79947fa997`.
2. `engine/actor/ActorRegionAdmission.cpp`:
   - Removed an invalid private-member call to
     `world::SpatialWorld(32).cells(Bounds)`, which was a
     definite C++ access error. `SpatialWorld::cells` is
     private in the actual header.
   - The pre-existing authoritative `worldLimit` validation
     is retained; public `SpatialWorld::cell` is used for
     owning-cell lookups by the existing region synchronizer.
   - Commits `17f36414d6574db395d016761ddea5dff4011929`,
     `61b08b6b6b0a1e14b490d7423a31d169842234bb`.
3. `tests/world/actor_region_admission_test.cpp`:
   - Added `testOutOfBoundsRegionMoveRejected`: on a real
     GameCore/ActorHost W1 opt-in fixture, a target beyond
     `config_.worldLimit` fails before changing checksum,
     original CharacterBody position or persisted move request.
   - Commit `251e40e2cdb916bdcd193daedb2e92c096aa90e1`.
4. `docs/region-entity-ledger.md`: Stage19 API and observed
   run evidence, explicit queued vs completed distinction.
5. This handoff.

## GitHub Actions evidence as actually observed

- First newly triggered run:
  https://github.com/chromealex/AIVERSE/actions/runs/45255049441
  on SHA `f977005e...`. It was **queued** on initial check,
  later reported **cancelled** due to newer commits and
  `cancel-in-progress: true`.
- Subsequent run 45255372515 on SHA `17f36414...`: cancelled.
- Run 45255411604 on SHA `61b08b6b...`: cancelled.
- Latest source-test run:
  https://github.com/chromealex/AIVERSE/actions/runs/45255698623
  on SHA `251e40e2cdb916bdcd193daedb2e92c096aa90e1`.
  It was **queued** when checked. No job compiler output or
  CTest test result had been delivered at handoff time.
- GitHub-hosted runner is **not** the user's Mac
  `/Volumes/KINGSTON` environment, so even a later successful
  hosted run will not prove full merged Mac acceptance.

**Actually executed:** GitHub API run/job status queries and
source-level visibility/API inspections; GitHub file commits.
The existence of a queued run is real, but it is not a
completed build.

**NOT executed:** CMake build, native CTest, complete
`./run-tests.sh --quick`, merged-tree Mac/KINGSTON regression,
GPU capture or performance benchmark. Do not report passing tests.

## Practical next actions for the next agent

1. **Do not push more source changes merely to poll status.**
   The W1 workflow cancels earlier runs on new source pushes.
   Read the most recent run status first. If run
   45255698623 starts, fetch its jobs, steps and logs via
   GitHub and use the actual compiler output to fix failures.
2. If it completes, record its exact `head_sha`, conclusion,
   number of tests, failures, and archived logs in the next
   stage handoff. If it remains queued or cancelled, state that,
   rather than fabricating a PASS.
3. Resolve the 41+ incoming `main` commits in a separate
   integration worktree, without overwriting concurrent agents.
   Run all W1 tests and full quick suite on the owner's Mac,
   with build artifacts on `/Volumes/KINGSTON/AIVERSE-builds/...`.
4. Fix any other compilation incompatibilities in the W1.1–18
   sources exposed by actual macOS compiler results.
5. Only after reliable tests, finish dormant gameplay owner
   support, a lossless per-region WAL/checkpoint/recovery,
   event-driven ECS identity and performance validation.
6. Preserve stage-by-stage docs and keep PR #116 Draft until
   tested and conflict-free, then involve the integrator
   before any merge to main.

No W1 production completion, no native PASS and no main merge.
