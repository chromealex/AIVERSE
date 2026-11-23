# AIVERSE Open World — W1.18: native acceptance gate and dirty archive repair

Date: 2026-11-23
Repository: `chromealex/AIVERSE`
Branch: `feature/aiv20261123_open_world_audit`
Draft PR: https://github.com/chromealex/AIVERSE/pull/116
Area: W1 / OW-01; **partial**, no merge or production acceptance
Previous: `docs/agent-handoffs/open-world-stage17-motion-audit-and-dirty-batch-2026-11-23.md`

## Intent

Prior stages W1.1–17 accumulated many authored C++ integration fixtures
without an actual native compile or execution on the owner's macOS
`/Volumes/KINGSTON` volume. W1.18 primarily makes verification
executable and source-identifies one concrete persistence bug instead
of adding another unvalidated gameplay subsystem.

## What was actually inspected

1. Current branch ancestry, against `main` — at stage start W1 branch
   was 197 commits ahead and 41 behind.
2. Main CMake definitions for 11 existing W1 CTest targets, including
   both source-only world ledger and real GameCore/ActorHost integration
   fixtures.
3. Existing `.github/workflows/ci.yml`,
   `strata-focused.yml` and `aiverse-postmerge-full-validation.yml`
   to adopt official macOS runner practice without claiming local
   KINGSTON evidence.
4. `tests/world/region_gamecore_integration_test.cpp` truly loads
   the external `mushrooms/rules.json` and `maps/duel.json`
   from `AIVERSE_TEMPLATES_DIR` — do not run the fixture without
   fetching the real template package first.
5. `engine/core/Archive.h::count(size_t,minBytesEach)`, which
   rejects a serialized count when the remaining payload cannot
   contain the declared minimum stride. Verified that the dirty queue
   encodes `uint64_t id` (8 bytes) plus `uint8_t flags` (1 byte).

## Code changes and commits

1. **`.github/workflows/open-world-w1.yml`** new GitHub Actions CI
   workflow:
   - `macos-15` hosted runner; `pull_request` paths and explicit
     `workflow_dispatch` for manual execution.
   - Fetches external `mushrooms` template and confirms its files.
   - CMake Debug with `AIVERSE_BUILD_TESTS=ON` and editor/tools off.
     Disables duplicate automatic template fetching after the explicit step.
   - Compiles all **11** previously registered W1 targets:
     `aiverse_region_entity_ledger_test`,
     `aiverse_region_ecs_bridge_test`,
     `aiverse_region_cold_store_test`,
     `aiverse_region_gamecore_integration_test`,
     `aiverse_vehicle_region_group_test`,
     `aiverse_actor_vehicle_region_group_test`,
     `aiverse_actor_region_position_test`,
     `aiverse_actor_region_lifecycle_test`,
     `aiverse_actor_region_admission_test`,
     `aiverse_race_region_respawn_gate_test`,
     `aiverse_region_dirty_queue_test`.
   - Runs matching named CTests with no-tests-error and
     output-on-failure; uploads configure/build/CTest/LastTest logs on
     failure or success.
   - Commits `175ae812de55a116d3233d7fe71f5979734339e5`,
     `029159d5cd8cd075087219116af18ee5a2f46e9a`,
     `90b85970557421a5f5066721e3854c11cb9ca314`.
2. **`engine/world/RegionDirtyQueue.h`**:
   - `Archive::count(...,17)` corrected to `...,9`. The old value
     was too conservative and could reject a *valid* native ECS v10
     queue with multiple pending notifications as malformed.
   - This is a **read-time validation fix only**; no schema bump,
     wire byte or persistent data representation change.
   - Commit `32fda8cb109d6faed090001359d81a94e5093c24`.
3. **`tests/world/region_dirty_queue_test.cpp`**:
   - Adds 1024-entry archive roundtrip, exact byte-length assertion
     `4+8+4+1024*9`, and truncated payload transactional refusal.
   - This was source-authored, not executed. Commit
     `55778478e5f3362295d34c61204323bd52797ca3`.
4. **`docs/region-entity-ledger.md`**:
   - Stage18 CI gate, serialization defect, and actual evidence status.
     Commit `e2771294644c4e07907ad9bc4f4a0cad56805597`.
5. **`skills/world/aiverse-region-lifecycle/SKILL.md`**:
   - Shows agent the workflow dispatch path and 9-byte archive contract,
     with GitHub-hosted runner versus KINGSTON distinction.
     Commit `c62569e0820adb95499a5211b8de6ac67529f622`.
6. This handoff.

## Tests actually executed / limitations

- **Actually performed**:
  - Inspected real GitHub source and CMake target registration.
  - Queried GitHub Actions workflow runs for the feature branch.
  - Checked the current container's network and filesystem. The
    container lacks `/Volumes/KINGSTON` and `git ls-remote`
    could not resolve `github.com`, so a local clone/full build
    cannot run here.
  - Created the GitHub-hosted macOS workflow and verified its source.
- **NOT performed**:
  - No W1 GitHub Actions workflow run was observed for this new
    workflow when checked. A committed YAML file does not establish a
    runner invocation, test result, or success.
  - No CMake/native compiler execution against the real AIVERSE
    checkout, no CTest PASS, no `./run-tests.sh --quick`, no user
    KINGSTON build, no merged main regression, no GPU/native visual
    capture and no performance statistics.
  - The 1024-entry archive regression is a test **source** to run,
    not a passed test.
- The new workflow may require the user or integrator to trigger
  `workflow_dispatch` manually, depending on GitHub Actions policy
  for newly introduced workflows and repository permissions.
- GitHub-hosted macOS tests, even when green, are not proof of
  acceptance on the user's Mac/KINGSTON or the complete merged tree.

## Run instructions for next agent / user

On GitHub, open **Actions → Open world W1 native acceptance** and
select the feature branch under `Run workflow` if available.
For a workflow introduced only on a feature branch, GitHub may require
a PR event or workflow availability permission before manual dispatch.
Inspect the actual run and download `w1-native-<run_id>` artifacts.
Do not claim the tests passed merely because a run was queued.

On the owner's Mac with writable KINGSTON and actual latest main
integrated safely into an isolated W1 worktree:

```bash
python3 tools/pkg/fetch_templates.py --out /Volumes/KINGSTON/AIVERSE-builds/w1-templates --only mushrooms
cmake -S . -B /Volumes/KINGSTON/AIVERSE-builds/w1-acceptance \
  -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_EDITOR=OFF \
  -DAIVERSE_BUILD_TOOLS=OFF \
  -DAIVERSE_FETCH_TEMPLATES=OFF \
  -DAIVERSE_TEMPLATES_DIR=/Volumes/KINGSTON/AIVERSE-builds/w1-templates
cmake --build /Volumes/KINGSTON/AIVERSE-builds/w1-acceptance --parallel 4
ctest --test-dir /Volumes/KINGSTON/AIVERSE-builds/w1-acceptance \
  -L World --output-on-failure --no-tests=error
./run-tests.sh --quick --build-dir /Volumes/KINGSTON/AIVERSE-builds/w1-acceptance
```

Record complete compile/CTest logs, revision, actual pass/fail count
and environmental prerequisites. If the macOS runner detects
compile/API failures in prior unverified stages, repair those first.

## Remaining stages

- First obtain real compilation and tests and reconcile all 41+
  incoming main commits without overwriting the work of other agents.
- Audit and fix mismatched HostSystems, COW and native ECS save
  contracts after actual build output.
- Complete notification coverage and prove 100+ real vehicle and
  NPC crossings, rollback and two-peer parity; then remove full scans.
- Build a **lossless** disk region WAL with atomic checksums,
  checkpoints, restart recovery and bounded memory. The current
  coalesced dirty queue/latest-state cursor is not that WAL.
- Make character/AI/VehicleDrive gameplay truly dormant-aware before
  parking core components; test offscreen progression and streaming.
- Create another stage handoff with **actual** test results.

Status remains **W1 partial, PR #116 Draft, no main merge**.
