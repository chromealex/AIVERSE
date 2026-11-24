# Caverns Stage55a — acceptance inventory and CI

Date: 2026-11-24. Branch `integration/caverns-studio-stage54c-20261124`. Starting HEAD d7623b11. Roadmap 1bab44f5. Code/config HEAD `fbfb741121c53ff2d4f7d2f943f3493abf7e3ff4`.

## Changes
- `tests/caverns/CMakeLists.txt`: register 12 additional existing CPU/worker fixtures alongside five prior smoke tests; keep assertions active and use Threads.
- `tests/caverns/IntegrationTests.cmake` and root CMake: register 18 existing native fixtures against their engine/render/physics/editor dependency groups. Physics and editor fixtures remain conditional on owning modules. Together with the two prior planetary fixtures, all 37 current Caverns test source files have a CTest registration in source. Actual configured inventory and link compatibility have not yet been established.
- `.github/workflows/caverns-stage55.yml`: branch-specific Linux native QA (explicit Box3D ON, editor ON, templates OFF, unified runner filtered to Caverns) and separate Node22 Studio typecheck/test:web/build. Artifacts preserve native inventory and logs. No change to main or shared runner logic.
- `docs/testing.md`: document optional physics and Mac acceptance.

## Actual evidence
GitHub tree verified after commit: 37 Caverns source files; static registration lists cover all 37; existing Threads target declaration precedes smoke directory. GitHub Actions run [45376794047](https://github.com/chromealex/AIVERSE/actions/runs/45376794047) at code SHA `fbfb741121c53ff2d4f7d2f943f3493abf7e3ff4` reported QUEUED, conclusion null. A later handoff-only commit can supersede the run. Native configure/build/CTest, Studio web, real GPU, GI, playable traversal and performance: NOT RUN in this session. Mac KINGSTON not mounted in the Linux environment; no silent fallback or claimed Mac build. No new editable schema or runtime implementation. Existing MCP/Studio authoring fixture is now registered where editor core is built; actual test execution pending.

## Local Mac gate
Run in a separate clean checkout of this branch. Keep all build/output on KINGSTON:

```bash
B=/Volumes/KINGSTON/AIVERSE-builds/caverns-stage55/debug
test -d /Volumes/KINGSTON && test -w /Volumes/KINGSTON || exit 1
mkdir -p "$B/tmp"
export TMPDIR="$B/tmp"
cmake -S . -B "$B" -DCMAKE_BUILD_TYPE=Debug \
  -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_ENABLE_PHYSICS=ON \
  -DAIVERSE_BUILD_EDITOR=ON -DAIVERSE_BUILD_STUDIO_HTML5=ON \
  -DAIVERSE_FETCH_TEMPLATES=OFF &&
./run-tests.sh --quick --build-dir "$B"
```

This is the merged-tree quick gate only after a real integration merge; current branch tests are preliminary. Do not use a green focused CPU run as proof of live native GPU/GI commit. Stage55b: inspect CI failures, repair actual compile/test failures, run full Mac native/Metal and Studio/MCP QA, collect real screenshots and prove native body cohort publication with existing GI and motor. Default GameCore integration remains separate and must be reported as such. Do not merge before acceptance.
