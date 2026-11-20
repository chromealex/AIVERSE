# AIVERSE TerraWeave Stage70 — merge updated main / SpectraFX + live PHYSIS-NEREID nutrient QA

**Date:** 2026-11-20.
**Branch:** `integration/terraweave-physis-stage70-20261120`.
**Draft PR:** [#53](https://github.com/chromealex/AIVERSE/pull/53).
**Actual merge SHA:** `40ed8606e834fb7c75a1c16901280300191980ec`.
**Merge first parent:** latest main at merge time
`b80601eb3fdcafddedbe9a9387f78a2fea0e499f`
(PR #60 SpectraFX / generic VFX GPU pipelines).
**Merge second parent:** TerraWeave staging HEAD
`7bfed528acc3ae9c1da704ca5c00080584910728`.
**Common ancestor:** `e4129c5062a6ab47e7c70f3fdd483392fde1f679`.

## What was actually done

1. Inspected all three full GitHub recursive file trees:
   baseline 5,972 entries, new main 6,055, TerraWeave 6,008.
   None was truncated. Compared every tracked blob against the
   common ancestor; newer main had 93 changed files and TerraWeave
   had 48. **Exactly one nonidentical overlapping path:
   `CMakeLists.txt`.** The changed lines in this CMake file were
   disjoint inserts, not edits of the same registration.
2. Built the merged Git tree from **current main as the base**,
   copied 47 non-conflicting TerraWeave file changes without
   overwriting a main-only edit and composed the CMake test block:
   - Kept new main's SpectraFX architecture and contract test targets.
   - Kept TerraWeave Stage70 4m nutrient, NEREID native micro compile
     and optional Box3D native smoke test registration.
   - Kept the FBX=OFF test runner compile flag, needed by the
     Mac full CTest (see prior native QA handoffs).
   - Verified removing Terra's exact two inserted segments
     reconstructed the new main's CMake byte-for-byte; no unrelated
     new-main lines were removed.
3. Created a real **two-parent** Git commit and fast-forwarded ONLY
   `integration/terraweave-physis-stage70-20261120`.
   Did NOT modify `main`, the original Terra feature branch, any
   local worktree, or another agent's branch.
4. Postmerge GitHub compare:
   - `base=main; head=integration/terraweave-physis-stage70-20261120`
   - `status=ahead`, `ahead_by=133`, `behind_by=0`,
     merge base is `b80601eb3fdcafddedbe9a9387f78a2fea0e499f`.
   - Recursive merged tree was returned untruncated with 6,091
     total entries (main plus Terra-only additions).
   - Read back merged CMake. Both the new SpectraFX tests and
     TerraWeave Stage70 / FBX-off targets were present.

## The new cross-owner regression, unverified

Already committed on the Terra parent before this merge:

- `tests/physis_terraweave_fall_bridge_test.cpp` was extended in
  code commits `2a7c5b660211accac1370f7ad02eef767fe6a8fc`
  and `5ccba305e8a097ad0edc38388c02bcd0b1078ee3`.
- Existing authentic PHYSIS wind -> rooted TerraWeave tree fall ->
  Box3D -> native Simulation save/replay/repair remains untouched.
- Added 4m per-body nutrient test uses a real PHYSIS World climate
  tick, NEREID `WaterBodyComponent` + `WaterQueryIndex`, real
  Earthworks/forest resource, PHYSIS weather adapter, conserved
  Stage70 nutrient step, v3 Archive roundtrip, normal/COW World
  rollback and deterministic replay.
- See [independent live-nutrient handoff](qa-2026-11-20-stage70-live-physis-nereid-nutrients-rollback-handoff.md).

## Real test evidence vs current unverified source

- **PASS, user-reported:** Mac native 181/181 CTests,
  `0` failed, `886.46 s`, exact local code SHA
  `f55d8d4e8622d297c4c27266f9289f82cc59baed`,
  from `acceptance-reports/provenance.txt`.
  That predates the new cross-owner test and the newest main merge.
- **PASS, static GitHub verification only:** ancestry, one CMake
  conflict manually reconciled by additive merge, presence of both
  sets of CMake registrations, untruncated tree.
- **NOT RUN for `40ed8606`:** CMake, C++ compilation,
  `physis_terraweave_fall_bridge` expanded test, full CTest,
  sanitizers, GUI editor startup, Metal/Vulkan/GLES runtime
  parity, actual long-running NEREID erosion/PHYSIS/Box3D co-sim,
  and Play/Place throughput benchmarks. Do NOT mark merged QA PASS.

## Exact Mac commands for next agent

First check the isolated worktree is clean. Detached HEAD is expected,
and no other agent's worktree should be reset or modified:

```bash
cd ~/Projects/AIVERSE-terraweave-stage70-local-qa
git status --short
git fetch origin main integration/terraweave-physis-stage70-20261120
git switch --detach origin/integration/terraweave-physis-stage70-20261120
git rev-parse HEAD
git merge-base --is-ancestor origin/main HEAD &&
  echo "PASS: newest main included"

# Reconfigure because merged main adds SpectraFX test targets.
cmake -S . -B build/terraweave-main-acceptance \
  -DCMAKE_BUILD_TYPE=Debug -DAIVERSE_BUILD_TESTS=ON \
  -DAIVERSE_BUILD_EDITOR=ON -DAIVERSE_BUILD_CLIENT=OFF \
  -DAIVERSE_BUILD_HEADLESS=ON -DAIVERSE_BUILD_TOOLS=ON \
  -DAIVERSE_ENABLE_PHYSICS=ON -DAIVERSE_BUILD_PHYSICS_DEMO=OFF \
  -DAIVERSE_PLACE_FAST_DEBUG=ON -DAIVERSE_FBX=OFF

cmake --build build/terraweave-main-acceptance \
  --target aiverse_physis_terraweave_fall_bridge_test \
  --parallel 4 &&
ctest --test-dir build/terraweave-main-acceptance \
  -R '^physis_terraweave_fall_bridge$' --output-on-failure
```

On focused PASS run one complete all-target and all-CTest suite,
using unchanged strict Place budget:

```bash
AIVERSE_INTEGRATION_JOBS=4 AIVERSE_INTEGRATION_CTEST_JOBS=2 \
  bash tools/terraweave-main-integration-acceptance.sh
```

Use the new `acceptance-reports/provenance.txt`,
`configure.log`, `build.log`, `inventory.log`,
`ctest.log` and `ctest.xml`. Newer main introduces
more tests than the old 181; do not demand a hard-coded count.

## Remaining production gates

- Run the actual integrated gameplay scenario (completed PHYSIS
  weather, NEREID flow/erosion, TerraWeave plant/deadwood/soil
  changes and derived Box3D bodies) over multiple simulation ticks,
  with authoritative snapshot rollback and replay; the added
  test covers one completed nutrient barrier + existing full
  Box3D fall fixture, not a long-running ecology campaign.
- Verify Metal via real GPU readback/screenshots and separately
  Vulkan/GLES on supported hardware or build environments.
- Compare final `main` again before deciding to merge PR #53;
  new agents are concurrently updating the repository.
- PR must remain **Draft** until required acceptance and user
  approval; write an exact-SHA handoff after each new phase.
