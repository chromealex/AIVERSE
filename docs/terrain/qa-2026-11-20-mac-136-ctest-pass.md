# TerraWeave QA — full native CTest 136/136 PASS (Mac Studio)

Date recorded: 2026-11-20.
Repository: `chromealex/AIVERSE`.
Source branch: `feature/terraweave`; PR #24 remains Draft, not merged.

## Evidence: full CTest user report

After repairing the native optional-Box3D compile boundary,
`skill_audit` path references, Stage70 micro-nutrient tests,
and PHYSIS Stage100/101 Place replica hot-path backport, the user
executed a full native test run and provided the exact CTest summary:

```text
100% tests passed, 0 tests failed out of 136

Label Time Summary:

SpectraFX    =   1.47 sec*proc (3 tests)
```

**Confirmed result: 136/136 CTest registrations passed, zero failed.**
The previous full-suite `place` performance failure at 8.256ms
with a strict 5ms budget has therefore been resolved in the tested
full CTest configuration. The earlier *separate* focused Mac Studio
place command directly showed 0.766ms avg, 1.729ms worst,
523 bodies / 16 simulated 4G clients + late join, with 11 checks,
0 failures. Do **not** claim those exact 0.766ms / 1.729ms numbers
were remeasured inside this final 136-test full suite; the CTest
summary includes no per-test timings.

The same prior focused run separately reported
`aiverse_replica_tests` PASS 1/1.
The earlier Box3D-on/off native smoke + TerraWeave model run
reported PASS 3/3.

## Proven scope and caveats

The latest user message provides the final full CTest result, but
does **not** include the shell output of:
- `git rev-parse HEAD` from the QA worktree,
- `grep CMAKE_BUILD_TYPE .../CMakeCache.txt`,
- `grep AIVERSE_PLACE_FAST_DEBUG .../CMakeCache.txt`,
- an entire `build-all.log` or per-CTest log,
- explicit editor/Metal/GLES/Vulkan visual screenshot or live
  NEREID+PHYSIS same-world acceptance report.

Thus we can honestly conclude that the 136 registered tests
on the user's native Mac Studio build all passed.
We CANNOT attribute the tested source to a particular SHA
without a worktree command, and cannot claim every runtime/visual
subsystem has independent GPU/real-world integration coverage.
The provided final CTest line indicates a successful summary,
not a source diff or an independent benchmark trace.

Earlier QA runbook configured a separate build:
`build/terraweave-place-perf` with
`CMAKE_BUILD_TYPE=Debug`,
`AIVERSE_ENABLE_PHYSICS=ON`,
`AIVERSE_PLACE_FAST_DEBUG=ON`, and
`CMAKE_BUILD_TESTS` / `AIVERSE_BUILD_TESTS=ON`.
These are the documented intended flags from the runbook;
the truncated 136/136 excerpt does NOT display the effective
local `CMakeCache.txt`. Verify before asserting the final run
used exactly these flags.

## Exact verification commands for archived acceptance

In the same Mac QA worktree, without changing files:

```bash
cd ~/Projects/AIVERSE-terraweave-qa-fix
git rev-parse HEAD
git status --short
grep -E '^(CMAKE_BUILD_TYPE|AIVERSE_BUILD_TESTS|AIVERSE_ENABLE_PHYSICS|AIVERSE_PLACE_FAST_DEBUG):' \
  build/terraweave-place-perf/CMakeCache.txt
ctest --test-dir build/terraweave-place-perf -N | tail -n 5
```

If the user's final full run used a different directory,
read the cache in that directory instead. Do not infer a
build directory from the final two lines alone.

Source branch GitHub head *before* documenting this result:
`04d3044ad23ddcbb75c0a0dffdd16c09896b21ce`.
The GitHub branch has diverged from `main`; last observed
comparison **968 ahead / 467 behind**, Draft PR #24
reported `mergeable=false`. This is a cross-branch integration
blocker, **not** a CTest failure. Do not merge or force-rebase
concurrent agents' graphics/ECS/NEREID/PHYSIS changes blindly.

## Source implementation and regression checkpoints

See adjacent exact per-stage handoffs:
- `qa-2026-11-19-physis-place-performance-backport.md`:
  `Server::setFields`, `Server::setViewer`,
  opt-in optimized Debug and strict 5ms Place CTest.
- `qa-2026-11-19-place-perf-native-green.md`:
  Mac 0.766ms/1.729ms and replica parity PASS.
- `qa-2026-11-19-box3d-smoke-green-and-warning.md`:
  Box3D on/off smoke 3/3 passing.
- `stage-70-sparse-micro-nutrients-handoff.md`:
  versioned v3 organic pool migration, NEREID 4m water
  and Stage70 CPU regression.

No gameplay or engine code changes are required merely to
record this 136-test PASS; this QA handoff is documentation only.

## Next integration agent acceptance, without weakening tests

1. Verify exact Mac tested source SHA and actual cache settings.
2. Evaluate merge conflicts against newly advanced `main`
   in a separate temporary worktree or dedicated integration
   branch, preserving the TerraWeave feature branch and all
   concurrent agent commits. PR #24 currently nonmergeable.
3. Bring compatible PHYSIS and NEREID updates into that
   integration worktree, preserving single ownership of weather,
   water and Earthworks, then re-run full build/CTest.
4. Run a standalone native world end-to-end case: dig/fill,
   active river erosion, bank slump/root exposure, falling
   tree Box3D and NEREID current obstruction, NavMesh dirty
   rebuild, ecology regrowth, 4m nutrient replay, save/load
   and rollback. Check no duplicate water/ECS authority.
5. Visually validate terrain/forest Horizon/Flora and water
   on Metal, then the supported Vulkan and GLES build/render
   paths where applicable. Record actual images, errors,
   frame/memory metrics; do not claim GPU acceptance based
   solely on CPU CTest.

Keep PR #24 Draft until merge conflict resolution, integrated
runtime and GPU acceptance are truly verified. Record next
agent handoff with actual commit, build commands and results.
