# PHYSIS Stage 98 — combined full native acceptance 165/167; Terra skill docs fixed; Place performance unresolved

Date: 2026-11-19
Repository: `chromealex/AIVERSE`
Branch: `integration/physis-terraweave-complete-20261119`
PR: https://github.com/chromealex/AIVERSE/pull/40 (Draft)
Prior handoff: `docs/agent-handoffs/physis-stage97-native-enospc-2026-11-19.md`

## Actual Mac Studio full-build / CTest evidence

User ran the one-pass full native combined acceptance from
`~/Projects/AIVERSE-physis-terra-final` and pasted the final CTest
report from `build/physis-terraweave-onepass/onepass-reports/20261119-184750-57321`:

- **165/167 CTests passed; two failed**: `place` (#99) and
  `skill_audit` (#105).
- All **31 PHYSIS-labeled tests passed**, with combined PHYSIS/Terra/Box3D
  integration registered and run; PHYSIS total label 21.68 sec*proc.
- `aiverse_tests` passed after 883.35 sec.
- `place` failed after 314.34 sec; first scenario simulated
  523 physical bodies and 16 clients on virtual 4G plus a late joiner.
  Reported `tick avg 8.199 ms` (worst 18.967 ms), while
  `tests/place/place_tests.cpp:139` asserts **`tickAvg < 5.0`**.
  All other Place scenarios were reported as passing, with
  52 checks, 1 failed.
- End-to-end scene convergence of resting bodies was 0.0069 m worst;
  no missing/extra objects; other functional Place checks passed.
- Full CTest wall time: **1207.40 seconds** (~20m 7s).
- Disk: 926Gi total volume, 20Gi available, ~98% utilization,
  combined CMake build directory 6.8 GiB.
- The unified acceptance exits **8** (CTest failure), not PASS.
  There is no evidence yet that any remaining failure is due
  to PHYSIS/Box3D authoritative fall logic.

## Exact skill_audit failure supplied in subsequent user turn

The strict Python auditor `tools/skills/audit.py --strict` reported
three *missing path* errors in
`skills/world/aiverse-terraweave/SKILL.md`:

```text
missing path apps/client/GameClient.h/.cpp
missing path docs/nereid/TERRAIN_EROSION.md\
missing path examples/world/terraweave_native/Integration.h::configureFloodRiverPorts
3 error(s), 0 warning(s)
```

Root cause: the skill used literal backtick-path forms that are
not actual repository file paths. The audit deliberately checks each
backticked path with Python's Path.exists, and does not understand
`h/.cpp` filename shorthand, escaped terminal-style backticks or
`file.h::method` as an existing filesystem item.

## Committed exact source change

Code/documentation commit:
`8a41676f97bd60b0ed33d08c5ddb24aaf38f21bf`.

Modified **only** `skills/world/aiverse-terraweave/SKILL.md`:

1. `apps/client/GameClient.h/.cpp` -> separate, valid
   `apps/client/GameClient.h` and
   `apps/client/GameClient.cpp` links.
2. Literal escaped backticks `\\`docs/nereid/TERRAIN_EROSION.md\\``
   -> genuine `docs/nereid/TERRAIN_EROSION.md` path.
3. `examples/world/terraweave_native/Integration.h::configureFloodRiverPorts`
   -> file path `examples/world/terraweave_native/Integration.h`,
   followed by symbol `configureFloodRiverPorts` as a separate token.

All four distinct referenced repo files exist on this combined branch;
`Integration.h` contains the named function. The three exact old
offending literals no longer appear in the modified skill. No changes to
the actual gameplay API, CMake, PHYSIS, NEREID or Box3D.

Native `skill_audit` **NOT YET re-run** on this corrected commit.

## Place load-test status and next investigation

The Place test is deliberately `RUN_SERIAL TRUE` in `CMakeLists.txt`.
Its `<5.0 ms` budget is NOT relaxed. The test measures
`PlaceServer::update` via real `std::chrono::steady_clock`, including
poll, character step, Box3D step, replication and broadcast.
The full suite was configured as CMake **Debug**, which may materially
change the performance budget versus an optimized shipping server.
`RUN_SERIAL` excludes other CTest processes, but not other agents/host
processes consuming machine CPU.

There is no sample profile, Mac thermals/process load information or
isolated optimized reference result in the pasted report. It is
premature to claim a specific implementation performance regression or
to change the 5ms threshold. Targeted follow-up should:

- Run strict `skill_audit` on the new source (short).
- Re-run the unchanged `place` CTest in an otherwise idle Mac Studio,
  the same Debug build and fixture, to measure repeatability.
- If the 5ms failure persists, profile `PlaceServer::update` phases
  (server poll, characters, Box3D `Scene::step`, sync and replica
  tick/broadcast), fix a measured hotspot, and re-run at the same
  5ms threshold. Optionally compare an independently optimized
  Release/RelWithDebInfo benchmark **without** removing the existing
  Debug full-CTest failure or weakening acceptance.
- Finally rerun complete unfiltered one-pass acceptance after all
  fixes, preserving `aiverse_tests` and other passed tests.

No Place runtime change was committed at this stage. This handoff
does not claim `place` PASS, a full 167/167 or release readiness.

## Native targeted checks after updating clean worktree

```bash
cd ~/Projects/AIVERSE-physis-terra-final
git status --short
git fetch origin
git switch --detach origin/integration/physis-terraweave-complete-20261119

# First run a short strict skill audit.
python3 tools/skills/audit.py --strict

# Run the exact previous Place performance test in isolation,
# with no simultaneous agents/workloads on the Mac, *not* a relaxed test.
ctest --test-dir build/physis-terraweave-onepass --config Debug \
  -R '^place$' --output-on-failure --parallel 1
```

Once both targeted failures are resolved, run
`bash tools/physis-terraweave-final-validation.sh` for the complete
unfiltered CTest acceptance and record the final result.
Never merge a failed full suite into `main`.
