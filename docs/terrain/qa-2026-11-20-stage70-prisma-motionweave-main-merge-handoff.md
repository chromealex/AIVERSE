# TerraWeave Stage70 — latest Prisma/MotionWeave main merge: source-integration handoff

**Date:** 2026-11-20.
**Repository:** `chromealex/AIVERSE`.
**Branch:** `integration/terraweave-physis-stage70-20261120`.
**Draft PR:** [#53](https://github.com/chromealex/AIVERSE/pull/53).
**Real two-parent merge SHA:** `c27085af0bbfa24f13523e566a9d39ddaa4b9fb6`.
**Main first parent:** `fc42156b41129f15b48c8b9e8626579eb2c70db0`.
**TerraWeave second parent:** `e8656adde4dd30f9a55e68999676d6c0e269fa9e`.
**Three-way merge-base:** `b80601eb3fdcafddedbe9a9387f78a2fea0e499f`.

## Purpose and baseline

A previous Mac Studio native full-suite run **191/191 PASS, 945.76 s**
was proven by user-provided `acceptance-reports/provenance.txt` to have
tested exact source `346dc66aeb0a89e1dc69ad889134fe7079ecb169`.
The later commits before this merge changed only documentation.

Since that PASS, main gained a broad Prisma + universal MotionWeave /
animation + camera/editor/RHI integration. The user asked to pull
upstream into TerraWeave, but **never to overwrite main**.

## Performed Git checks and actual code merge

Fetched complete recursive Git trees for base, main and TerraWeave,
all with `truncated=false`, respectively **5,067 / 5,441 / 5,106
tracked blob files**. Relative to base, main changed **517** paths,
TerraWeave **51**, with **exactly four** differing intersections.

Resolved each intersection after reading baseline/main/Terra content:

| File | Resolution |
| --- | --- |
| `CMakeLists.txt` | Started from current main so Prisma/PrismaData, MotionWeave, SpectraFX and all new targets stayed intact. Inserted Terra Stage70 native micro-water/nutrient and physics smoke tests and FBX=OFF `AIVERSE_NO_FBX` test flag at their original semantic anchors. |
| `engine/render/Renderer3DModels.cpp` | Started from current main, retained pending/failed GPU color/pipeline warm lifecycle and `warmModelPipelinesChecked`. Applied Terra `specialDraw` filtering of immutable static cache/packet reuse for partial primitive and per-instance vertex-parameter draws, and preserved the bounded displacement envelope. |
| `tests/ecs_wave_replay_test.cpp` | Chose current main's `tests/AlwaysCheck.h` and `AIVERSE_TEST_CHECK` throughout, preserving always-on assertions with `NDEBUG` and `-Werror`. Removed Terra's older duplicate local `CHECK` macro. |
| `tests/tests.cpp` | Kept main's `waitForResourceIdle()` RHI test-device API and opt-in `AIVERSE_TEST_TRACE` instrumentation. Replaced only the identical preexisting FBX fixture region with Terra's conditional FBX-on original regression / FBX-off actionable-diagnostic regression. |

Applied TerraWeave's non-overlapping changed blobs verbatim to a
tree based on **current main**, then created a genuine two-parent
merge commit. Fast-forwarded only the TerraWeave integration branch.
The `main` branch and other agents' worktrees were not modified.

## Post-merge read-back verification — not native tests

- GitHub compare `main...TerraWeave`: **ahead=142, behind=0**,
  merge-base `fc42156b41129f15b48c8b9e8626579eb2c70db0`.
- Merged tree had **5,480** blobs, was not truncated.
- Every file currently in main is present in the merged tree.
  Terra's older `modules/rts/view/RtsFog.cpp` was intentionally
  not resurrected: latest main removed that obsolete genre-bound
  view implementation as part of a universal View migration.
- Static source read-back confirmed present and retained:
  `aiverse_motionweave`, `engine/render/Prisma.cpp`,
  `aiverse_spectrafx_contract_tests`,
  `aiverse_vfx_graph_runtime_tests`,
  `aiverse_prisma_graph_tests`,
  `aiverse_terraweave_micro_nutrient_test`,
  `aiverse_terraweave_native_micro_physics_compile_test`,
  `aiverse_physis_terraweave_fall_bridge_test`,
  `AIVERSE_NO_FBX` for disabled FBX,
  `warmModelPipelinesChecked`, `pendingColorWarms_`,
  `specialDraw` cache/packet guards, shared ECS AlwaysCheck,
  `models_fbx_disabled_gives_actionable_error`,
  `AIVERSE_TEST_TRACE`, and test device
  `waitForResourceIdle()`.
- No conflict markers were introduced in manually composed
  files; source tree written as Git blobs and read back through
  GitHub. These checks do not prove native compiling.

## Test evidence (different commits!)

- **User-reported PASS, old code `346dc66a`:**
  191/191 full Mac native/editor/Box3D CTest.
- **Not yet tested for merged source `c27085af0bbfa24f13523e566a9d39ddaa4b9fb6`:**
  CMake configuration, Clang/GCC, CTest, PHYSIS+NEREID+Terra
  combined regression, Prisma+MotionWeave+View renderer/Editor
  cross-owner integration, Metal/Vulkan/GLES backend parity,
  live erosion/fall world simulation, sanitizer or perf budgets.
  Do not mark this new merge as 191/191 PASS.
- Some test names and registration numbers may have changed with
  newer main. Always report actual `ctest -N` inventory.

## Required local Mac QA (isolated worktree only)

Check `git status --short` before switching detached HEAD.
If dirty, preserve the changes first; never reset someone else's
working tree or delete their output.
The old `build/terraweave-main-acceptance` is reusable.

```bash
cd ~/Projects/AIVERSE-terraweave-stage70-local-qa
git status --short
git fetch origin main integration/terraweave-physis-stage70-20261120
git switch --detach origin/integration/terraweave-physis-stage70-20261120
git rev-parse HEAD
git merge-base --is-ancestor origin/main HEAD && echo 'PASS: latest main included'
df -h . "${TMPDIR:-/tmp}"

# Configure again for new Prisma/MotionWeave targets.
cmake -S . -B build/terraweave-main-acceptance \
  -DCMAKE_BUILD_TYPE=Debug \
  -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_EDITOR=ON \
  -DAIVERSE_BUILD_CLIENT=OFF -DAIVERSE_BUILD_HEADLESS=ON \
  -DAIVERSE_BUILD_TOOLS=ON -DAIVERSE_ENABLE_PHYSICS=ON \
  -DAIVERSE_BUILD_PHYSICS_DEMO=OFF \
  -DAIVERSE_PLACE_FAST_DEBUG=ON -DAIVERSE_FBX=OFF

cmake --build build/terraweave-main-acceptance \
  --target aiverse_physis_terraweave_fall_bridge_test \
  --parallel 4 &&
ctest --test-dir build/terraweave-main-acceptance \
  -R '^physis_terraweave_fall_bridge$' --output-on-failure
```

On focused PASS, execute the full one-pass native acceptance:

```bash
AIVERSE_INTEGRATION_JOBS=4 AIVERSE_INTEGRATION_CTEST_JOBS=2 \
  bash tools/terraweave-main-integration-acceptance.sh
```

The script keeps strict Place 5ms acceptance, `FBX=OFF` semantics,
and writes `build/terraweave-main-acceptance/acceptance-reports/`
with `provenance.txt`, flags, full CMake/build logs, inventory
and JUnit `ctest.xml`. Check free disk before running;
a previous 188/191 failure was caused by `No space left on device`.
Do not clear that build/acceptance directory or another agent's files.

Then verify real Metal GPU readback / TerraWeave renderer parity
and separate Vulkan/GLES compile/backend runtime support on supported
platforms, plus sustained PHYSIS/NEREID/TerraWeave/Box3D
erosion/fall/rollback gameplay acceptance.

**Merge to main NOT authorized. PR #53 remains Draft.**
A separate per-stage handoff must be authored after real Mac QA.
