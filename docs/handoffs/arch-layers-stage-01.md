# Architecture layers — stage 01 handoff (2026-11-21)

Branch `feature/aiv20261121_arch-layers` from main d84e958b1. Split of work with the no-genre branch:
`docs/architecture-layers.md` § Coordination.

## Done
- `tools/layers/check_layers.py` + `tools/layers/allowlist.json` (57 edges: G3 16, G4 3, L1 38), CI job `layers`
  in `.github/workflows/ci.yml` (python only, no build).
- `docs/architecture-layers.md`: layer order, debt ids, rules, coordination.

## Checked and dropped
- Stable ECS component ids: not needed now. Saves, replays and hashes use string ids
  (`engine/sim/PersistentEcsRegistry`); the numeric `componentTypeId` is a per-process pool index; the only
  loop over pools in id order (`World::destroy`) is order-independent; native modules are static OBJECT
  libraries (no DLL copies of the static counter). Revisit only if dynamic modules appear.

## Found for the no-genre work (not changed here)
- Native module build `aiverse_native_module` lives in `modules/rts/native/Build.cmake` — shared
  infrastructure inside a genre host.

## Checks
- `python3 tools/layers/check_layers.py` -> `layers: OK` (run on the user's machine VM).
- Engine build / CTest: NOT RUN (no C++ changed; no cmake in this environment).

## Next (L1, needs the Mac build)
engine/ -> libraries/ edges, by group; each group is its own stage, then `check_layers.py --update`:
1. `engine/world/{Nereid*,TerraWeave*,PhysisNereid*,WorldScene.h}` (31 edges) include `aiverselib/water/*`.
   Either move `aiverselib/water` core (geometry, systems, query index) down to `engine/world/nereid/` with
   forwarding headers in `aiverselib/water/`, or move these bridges up to `libraries/`. Coordinate with
   NEREID / TerraWeave owners (AGENTS.md cross-agent ownership).
2. `engine/view/View.h` -> MotionWeave / animation budget (3): same choice with the MotionWeave owner.
3. `engine/platform/*Input*` -> `aiverselib/gameplay/InputFlow.h` (4): move InputFlow core to `engine/platform`.
4. `engine/meta`, `engine/render/graph` -> `aiverselib/Graphs.h`, `Loot.h`; `engine/net` -> `SnapshotDelta.h` (4).
Then: split `engine/render` into CMake sub-targets; shrink AGENTS.md after no-genre is merged into main.

## Stage 02 (2026-11-21)
- 2e88a870e: InputFlow core -> `engine/platform/input/` (4 L1 edges).
- d2980fa3c: `Graphs.h` -> `engine/core/`, `Progression.h`/`Loot.h` -> `engine/core/progression/`,
  `SnapshotDelta.h` -> `engine/net/` (4 L1 edges); meta package pin hashes the moved files.
- Old `aiverselib/...` paths are forwarding headers; namespaces, CMake targets, catalogs unchanged.
- Checks: g++ -fsyntax-only of moved headers, forwarders and engine users: PASS (Linux VM).
  Mac build `./run-tests.sh --quick --build-dir /Volumes/KINGSTON/AIVERSE-builds/arch-layers`: NOT RUN yet
  (launcher `_tmp/build_arch_layers.command`, log `/Volumes/KINGSTON/AIVERSE-builds/arch-layers-tests.log`).
- Worktree note: a worktree created from the VM is registered with a VM path and gets removed by
  `git worktree prune` on the Mac. This worktree now uses a relative `.git` file and the Mac path in
  `.git/worktrees/arch-layers/gitdir`.

## Remaining L1 (30 edges) — needs owner decision, not changed
- 27: `engine/world/{Nereid*,TerraWeave*,PhysisNereid*,WorldScene.h}` -> `aiverselib/water/*`. NEREID core
  (23 headers, 3.2k lines) depends only on `engine/sim`/`engine/core`. Options: (a) NEREID core moves to
  `engine/world/nereid/` with forwarders (NEREID owner); (b) the bridges move up to `libraries/` (Terra owner).
- 3: `engine/view/View.h` -> MotionWeave / animation budget, which include `engine/render` and ECS. Option:
  move MotionWeave up into `engine/animation/` or move the View bridge into `libraries/` (MotionWeave owner).

## Stage 03 (2026-11-21, owner asked to finish L1)
- NEREID core (25 headers) -> `engine/world/nereid/` (27 edges); note in `docs/nereid/HANDOFF.md`, AGENTS ownership row.
- MotionWeave core (12 headers: MotionWeave, EcsAnimationSystems, schedulers, montage, playback, pose blend,
  inertialization, budget/work policies, source bridge) -> `engine/animation/` (3 edges);
  `tools/animation/check_motionweave_dependencies.py` also scans `engine/animation`: PASS.
- L1 is closed; allowlist = 19 genre-host edges (G3 16, G4 3) owned by the no-genre work.
- g++ -fsyntax-only: all moved headers and forwarders PASS except pre-existing failures that also fail on
  the parent commit (gcc size_t lambda in TerraWeaveForestEcology.h; box3d include path not given).

## Stage 04 (2026-11-21)
- AGENTS.md 5668 -> 1098 lines, area sections verbatim in `docs/agents/*.md` (8338847d5); no-genre AGENTS wording
  applied first (09c136ed6); worktree prune rule (b0b46a766).
- Merged origin/main (d7a92d581): main had changed 5 moved headers (NEREID Erosion/FloodRouting/FlowObstacles,
  MotionWeaveWorkers, EnhancedInput) and TerraWeaveSedimentEcs; their diffs were re-applied at the new paths.
- main build fixes found on the Mac: BwAI PS save (6a12c54fd, 367b5e39e), rts_authored_pose_test link (50f9edabb).
- Render finding, NOT changed: `Renderer3D*.cpp`, `GeometryScene.cpp`, `Prisma.cpp`, `Lights.cpp` are compiled
  twice because they are two variants: `aiverse_runtime` defines `AIVERSE_STRATA_STAGING_ENABLED=1`
  (CMakeLists.txt ~2911), `aiverse_tests` compiles them without it, so renderer tests do not cover the STRATA
  paths that ship in the client/editor. Needs an owner decision (tests on the STRATA variant, or two named
  render targets) before a shared render library.

## Stage 05 (2026-11-21) — tests and registry
- Mac `./run-tests.sh --quick --build-dir /Volumes/KINGSTON/AIVERSE-builds/arch-layers` on a3556ed01:
  configure PASS, build PASS, CTest 362/364 PASS (aiverse_tests 967 s). FAIL, both from main:
  `skill_audit` (missing `docs/handoff/studio-html5/NN-*.md`, same on main) and `rts_authored_pose_test`
  (e128dfd38 feature: "authored entities not spawned"; on main the test did not even build — fixed link here,
  logic left to its author).
- 99611fa7d: registry schema 2 (core/partial/planned, `tests`, `gameReady`), 5 genre library suites (+4 examples)
  moved into the root CTest, `genre_registry` check. Build/run of those new root targets: NOT RUN yet.

## Stage 06 (2026-11-21) — no compatibility headers, verified
- Owner rule (main e0713339c): the 43 forwarding headers removed, every user on the real path (ea578868d);
  `AIVERSE::libraries` exports the repository root because library headers include `engine/...` (9be615a03).
- Merged origin/main (3a0d3564c): BwAI fix taken from main; V1-V3 from the racing merge, V3 -> core; 2 new
  engine -> libraries edges from the racing merge recorded as L2 (racing owner).
- Mac `./run-tests.sh --quick --build-dir /Volumes/KINGSTON/AIVERSE-builds/arch-layers` on 9be615a03:
  configure PASS, build PASS, CTest **372/374 PASS** (aiverse_tests 950 s; the 10 new tests strategy_libraries,
  library_genre_puzzles(+example), library_cards_turns(+example), library_vehicles_sports(+example),
  library_genre_production_survival, library_production_survival_example, genre_registry all PASS).
  FAIL, both from main: skill_audit (missing docs/handoff/studio-html5/NN-*.md), rts_authored_pose_test
  (e128dfd38 feature logic: "authored entities not spawned").
- Branch is ready for MAIN merge. Launcher: `_tmp/run-arch2.command` (Finder double-click works; a file edited
  with `sed -i` from the VM gets an ACL and macOS refuses to run it — recreate with `cat >` + chmod 755).
