# TerraWeave + latest main — isolated two-parent merge and new native QA gate

**Date:** 2026-11-20. **Repo:** `chromealex/AIVERSE`.
**Working/staging branch:** `integration/terraweave-main-20261120`.
**Do NOT change:** `main` or `feature/terraweave`.
**PR #24:** original TerraWeave Draft, unchanged and not mergeable before integrated QA.
**PR #52:** staging-only `main` → `integration/terraweave-main-20261120`,
now GitHub **merged** to the integration branch, never to main.

## Baselines and exact merge record

- Original TerraWeave tested-source branch:
  `d9de76dcd8aa4e7b4e01a42e183a0b9c98c4854b`
  (previous user's Mac Studio summary: full **136/136 CTest pass**, exact
  local tested SHA was not printed; one earlier focused optimized Debug
  Place test had mean 0.766ms, max 1.729ms, 11 checks 0 failed).
- Latest **main used for this merge**:
  `d7d9b3f4180dcba98b9238fd56eedc7ccb420bb1`.
- Three-way merge base:
  `d71c6d47e532b25fb816a8bb1f96c41e5275a165`.
- Integration branch created from TerraWeave; true TWO-PARENT merge:
  `ad982326f6b52e728a689f0c53c43d50f5ad9e02`,
  first parent TerraWeave, second parent main.
- Materialized merged Git tree:
  `d194bef1ca0a8c3c175837d5e33737ff5746136a`.
  Based on **main's tree** plus every one of the **189** files changed
  on TerraWeave relative to their common base. The same comparison
  had **94** paths changed in main and **11** overlapping paths.
  **Zero** files deleted by TerraWeave in that three-way base.
- GitHub verification after merging: integration was
  **971 commits ahead / 0 behind main**, and
  **468 ahead / 0 behind feature/terraweave**.
  That proves both histories are ancestors of this staging head.
- `eea7d65e680f3addad9171473b5034875f525f02`:
  NEW `.github/workflows/terraweave-main-integration-qa.yml`;
  GitHub Actions [run #44619590001](https://github.com/chromealex/AIVERSE/actions/runs/44619590001)
  was **QUEUED** at last inspection.
  It compiles editor+native headless+physics on Ubuntu and macOS
  in Debug with `AIVERSE_PLACE_FAST_DEBUG=ON`,
  then executes the ENTIRE registered CTest set with
  `place` RUN_SERIAL and uploads real configure/build/CTest logs.

## The 11 overlapping paths and actual resolution

1. `AGENTS.md`: retained both sets of handoff/agent policies;
   appended TerraWeave instructions after their original base anchor,
   without removing main's later ECS/static scene instructions.
2. `CMakeLists.txt`: retained main's ECS static scene and collision
   namespace test registration, inserted TerraWeave Horizon offline
   atlas baker after the FPS GameModule, applied TerraWeave native
   tests, optimized Debug Place option, and `RUN_SERIAL` without
   removing newly introduced main targets.
3. `apps/editor/Editor.cpp`: all six TerraWeave source hunks
   applied on main's editor implementation.
4. `apps/editor/windows/SceneWindow.cpp`: retained main's new
   editor changes plus TerraWeave GPU timing router changes; all
   three TerraWeave hunks applied against main.
5. `engine/gfx/Gfx.h`: three additive TerraWeave GPU timing
   interfaces applied over main's newer gfx backend interfaces.
6. `engine/render/GlobalGeometryPool.cpp`: main had replaced
   old `canPool` with explicit
   `geometryPoolAdmission` / `GeometryPoolPolicy`. Rather than
   reinstating the obsolete old function, the merged path treats
   TerraWeave `!model.batching.allowGeometryPool` as
   `GeometryPoolAdmissionReason::DedicatedAsset`, preserving
   main's specialized admission policy and allowing dynamic
   TerraWeave vertex patches to own dedicated buffers.
7. `engine/render/Model.h`: appended
   `Model::Batching::allowGeometryPool` for TerraWeave's dynamic
   geometry opt-out without removing main's new
   `GeometryPoolPolicy` field.
8. `engine/render/Renderer3D.h`: appended trailing
   `ModelDraw` `primitiveMaterialFilter`,
   `useVertexParameters`, `vertexParameters`, and
   `maxVertexOffsetMetres` without changing main's preceding
   aggregate layout or new static-packet owner fields.
9. `engine/render/Renderer3DModels.cpp`: preserved main's modern
   static-model cache/packet replay and material owner policies,
   and manually adapted TerraWeave's partial-primitive/vertex-graph
   functionality: static capture rejects filtered draws;
   special filtered/vertex-parameter draws bypass immutable
   whole-model cache and packet replay; bounded graph displacement
   enlarges frustum bounds, allows safe node culling/LOD;
   unknown displacement stays uncullable; original TerraWeave
   nonconflicting 4 hunks add persistent GPU bounds, node material
   filtering and U0 Inst.anim vertex params.
   **Requires REAL GPU parity and C++ build:** source resolution
   is not equivalent to testing the merged renderer.
10. `skills/catalog.json`: joined skills by ID, preserving
    **221 unique entries**, including TerraWeave and ECS
    Shared/Static added from main.
11. `tools/skills/catalog.py`: preserved main's ECS
    Shared/Static routing registration and added TerraWeave
    world skill tuple + search aliases.

No `<<<<<<<` conflict markers were retained by the textual
resolution. Merged blobs are normal Git blobs in a real merge tree;
the merge was not a fake two-parent commit that overwrote main with
the TerraWeave tree. Future changes to main after the stated SHA
require another explicit integration update.

## Independent Git-tree content retention verification

GitHub recursive trees for the common ancestor, main, TerraWeave and
the synthesized merge were compared by path, blob SHA and file mode.
The 189 TerraWeave-changed paths break down into **178 TerraWeave-only
paths plus 11 changed on both sides**; main had **83 main-only**
changed paths plus those same 11 overlap paths.

The merged tree preserves the EXACT TerraWeave blob/mode for
**all 178 TerraWeave-only paths** and EXACT main blob/mode for
**all 83 main-only paths**. The 11 overlaps have intentional merged
blobs; none is missing. **Zero unexpected SHA/mode mismatches**
were found across these nonoverlapping change sets.
Merged tree has **4,786 file entries**; main had 4,633,
TerraWeave had 4,726. This is source-tree validation,
NOT C++ build, logical behavior verification or GPU acceptance.

## Verification achieved vs not yet achieved

**Actually done:** GitHub repo/head/merge-base comparison, 189/
94/11 change counts, source-level conflict resolution, Git blob/tree
construction, true two-parent merge commit, branch readback,
both-parent ahead/behind verification, staging PR #52 merge,
workflow creation and actual GitHub queued run.

**NOT YET DONE:** CMake configure/build on this new merged commit,
any of its CTest executions, a macOS Metal screenshot or Vulkan/
GLES runtime capture, PHYSIS branch owner integration,
TerraWeave/NEREID/Box3D live world erosion/falling-tree/navmesh
acceptance, latency/replica load on merged code, or GPU performance.
The prior user's **136/136 PASS applies to a pre-merge tree**,
not to the new main integration.

### Independent Mac Studio acceptance (a CLEAN new worktree)

```bash
cd ~/Projects/AIVERSE
git fetch origin main feature/terraweave integration/terraweave-main-20261120
git worktree add --detach ~/Projects/AIVERSE-terraweave-main-qa \
  origin/integration/terraweave-main-20261120

cd ~/Projects/AIVERSE-terraweave-main-qa
git rev-parse HEAD
set -euo pipefail
cmake -S . -B build/integrated-qa \
  -DCMAKE_BUILD_TYPE=Debug \
  -DAIVERSE_BUILD_TESTS=ON \
  -DAIVERSE_BUILD_EDITOR=ON \
  -DAIVERSE_BUILD_CLIENT=OFF \
  -DAIVERSE_BUILD_HEADLESS=ON \
  -DAIVERSE_BUILD_TOOLS=ON \
  -DAIVERSE_ENABLE_PHYSICS=ON \
  -DAIVERSE_BUILD_PHYSICS_DEMO=OFF \
  -DAIVERSE_PLACE_FAST_DEBUG=ON

cmake --build build/integrated-qa --parallel 4 \
  2>&1 | tee build/integrated-qa/build.log

ctest --test-dir build/integrated-qa \
  --output-on-failure --parallel 2 \
  2>&1 | tee build/integrated-qa/ctest.log
```

The **number of CTest registrations can be greater than 136**
because current main added more ECS Scene / Shared/Static tests.
Do not change `place` performance threshold or any native
feature test. Do not run CTest until CMake build succeeds.
Do not overwrite another agent's worktree.

## One-command native Mac acceptance utility

- `c5c6a6fd6b36d747b554223175e56fa9b1ca2432` —
  NEW `tools/terraweave-main-integration-acceptance.sh`. Use from a
  clean isolated integration worktree via:

  ```bash
  bash tools/terraweave-main-integration-acceptance.sh
  ```

  It checks both actual merge ancestors, records the tested Git SHA,
  effective CMake flags and git status, configures Debug with optimized
  Place hot paths and editor/physics enabled, performs ONE complete
  build, lists registered tests and then runs ONE unfiltered CTest.
  All logs and CTest JUnit output are saved in
  `build/terraweave-main-acceptance/acceptance-reports/`.
  `set -euo pipefail` prevents CTest starting after a failed build.
  It does **not** claim GPU visual or live ECS integration acceptance.
  Source creation is confirmed; this script has not been run on
  the new integrated tree by the assistant or user.

## Next QA owner and integration checklist

1. Inspect [run #44619590001](https://github.com/chromealex/AIVERSE/actions/runs/44619590001)
   jobs and build errors. Fix the **first concrete** compiler/link
   diagnostics on `integration/terraweave-main-20261120`
   and append a new isolated handoff with changed files/results.
2. Prove new renderer API actually compiles and preserves:
   main static packets, CPU multi-draw batching, geometry-pool
   policies; TerraWeave material-filtered Horizon close/far
   transitions, rigid vertex parameters and bounded
   displacement culling.
3. Run full Mac merged CTest. Compare complete test inventory
   rather than assuming exactly 136.
4. Exercise one authoritative multi-owner world:
   excavate/erode bank with NEREID, slip/roots exposed,
   Box3D fall, flow obstruction, NavMesh invalidation,
   ecological regrowth, body-tagged 4m nutrient conservation,
   save/restore and rollback. PHYSIS is still owned by
   its separate branch; combine only with compatible public
   native API and no second weather/water simulation.
5. Capture/verify actual Metal scene and Vulkan/GLES backend
   builds/render parity and frame/memory budgets on available
   hardware. Report supported vs deferred platforms honestly.
6. Do not merge Draft PR #24 to main until all new
   integration/source checks are green. Keep `main` and
   `feature/terraweave` unchanged. This staging branch is
   disposable/rebaseable after its QA results are preserved.

Each further QA stage MUST create a separate handoff recording
exact SHAs, changed files, real tests, limits and the next step.
