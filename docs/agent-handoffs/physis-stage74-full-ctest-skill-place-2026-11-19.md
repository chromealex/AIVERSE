# PHYSIS Stage 74 — broad AIVERSE CTest: fix skill source path; isolate Place performance

Date: 2026-11-19
Repository: `chromealex/AIVERSE`
Branch: `feature/physis-nature-simulation`
PR #25: https://github.com/chromealex/AIVERSE/pull/25 (DRAFT)
Pre-fix HEAD: `31759ab5782f4f1e73d4f8b915ef0b9cba67a4ed`
Source fix commit: `a61923df653994a9a94c1916f4077db7fb7e9848`
Documentation commit: inspect `git log -1 -- docs/agent-handoffs/physis-stage74-full-ctest-skill-place-2026-11-19.md`
Previous: `docs/agent-handoffs/physis-stage73-postmerge-40-green-2026-11-19.md`

## User-reported native macOS broad suite failures

On the merged PHYSIS + current main/NEREID working branch:
- **Full CTest 150 tests: 148 Passed, 2 Failed**.
- `skill_audit` (#105) fails with exactly one PHYSIS skill path:
  `skills/world/aiverse-physis/SKILL.md: missing path
  tests/physis_remote_edit_test.cpp::revisionProbeStopsOnUnhealthyJournal`.
- `place` (#99) takes ~313 sec and fails at
  `tests/place/place_tests.cpp:139`,
  `CHECK(tickAvg < 5.0)`. The 16-client 4G test measured a server
  tick average of **8.204ms**, worst **19.381ms** (523 bodies,
  31,954 B/s/client, correct late join convergence), and the test
  printed **52 checks, 1 failed**. Its other physics, prediction,
  spawn, network and security scenarios passed.
- `skill_library_isolation` (#109) ran 256.65sec in the same
  parallel CTest. Competing workload is a **plausible confound** for
  wall-clock timing, not yet proven as the sole cause of 8.204ms.

Previous Stage73 focused native combined CTest PASS (40/40)
remains valid for its filtered scope. This broad run is new evidence.

## Root cause / implementation

1. PHYSIS documentation audit:
   - `tools/skills/audit.py` extracts backticked tokens and checks
     leading source-directory paths literally, removing only
     trailing `:lineNumber`. It does not parse `file.cpp::function`.
   - Corrected in
     `skills/world/aiverse-physis/SKILL.md` to separate
     `tests/physis_remote_edit_test.cpp` from backticked
     `revisionProbeStopsOnUnhealthyJournal`.
     The function itself was already present in that test source.
   - This is a documentation/tool-input correction, not a change
     to the PHYSIS implementation.
2. Place benchmark scheduling:
   - `PlaceServer::update()` measures server tick wall time using
     `std::chrono::steady_clock` and computes `tickMsAvg()`.
     The scenario uses 16 simulated clients and ~500 dynamic bodies;
     `CHECK(tickAvg < 5.0)` is a strict performance budget.
   - The complete repository suite was run with parallel tests;
     scheduling unrelated heavy jobs concurrently can corrupt
     wall-clock benchmark comparability.
   - In `CMakeLists.txt`, after
     `add_test(NAME place COMMAND aiverse_place_tests ...)`, set:
     `set_tests_properties(place PROPERTIES RUN_SERIAL TRUE)`.
     This asks CTest not to run other tests simultaneously.
   - **The <5ms threshold remains unchanged**, and no gameplay,
     Place server network/physics or Box3D solver changes were made.

## Actual verification vs unverified status

- The two source changes are present in the GitHub feature branch,
  committed to `a61923df653994a9a94c1916f4077db7fb7e9848`.
- Locally tested CMake scheduling syntax against a MINIMAL independent
  CMake 3.31.6 fixture with `enable_testing()`, one test,
  `set_tests_properties(place PROPERTIES RUN_SERIAL TRUE)`.
  `ctest --show-only=json-v1` returned the property
  `RUN_SERIAL: true` (actual test plumbing smoke).
  This was NOT a native AIVERSE compile/run.
- **NOT RUN after changes:** native `skill_audit`, isolated Place
  benchmark, broad 150-test macOS CTest, editor rendering, complete
  cross-platform CI, GPU/Metal water visual and host integration.
- The Place benchmark might STILL fail if the native build is
  unoptimized, CPU is throttled or the server has an actual speed
  regression; do not relax performance gates without isolated data.
- PR #25 remains DRAFT.

## Next agent / user command: focused confirm, then broad suite

In the same macOS AIVERSE checkout:

```sh
cd ~/Projects/AIVERSE && \
git fetch origin && \
git switch feature/physis-nature-simulation && \
git pull --ff-only origin feature/physis-nature-simulation && \
cmake -S . -B build/physis-nereid-validation \
  -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_EDITOR=ON \
  -DAIVERSE_ENABLE_PHYSICS=ON && \
cmake --build build/physis-nereid-validation --target aiverse_place_tests --parallel 4 && \
ctest --test-dir build/physis-nereid-validation \
  --output-on-failure -R '^(skill_audit|place)$'
```

Run the focused Place test **without concurrent workload**.
If it still reports `tickAvg >= 5.0`, preserve measured tickAvg,
actual `CMAKE_BUILD_TYPE`, compiler optimization flags, CPU
architecture/Rosetta and system load. Diagnose the baseline and
profile `PlaceServer::update` (network poll, physics step,
replica tick/broadcast); do not silently raise `5.0`.
If both focused tests pass, run the full CTest again with
`--parallel 4`; `place` is now RUN_SERIAL even under -j4.
Save build and CTest logs and publish Stage75 handoff.

PHYSIS/NEREID authority boundaries remain unchanged:
PHYSIS environmental ECS, NEREID canonical water,
`FloodMode::ExternalAuthority` when both are installed,
TerraWeave terrain/foliage. `main` is NOT modified.
