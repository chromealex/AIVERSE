# STRATA Stage 19.1 — integration worktree preparation (2026-11-20)

## State / evidence
- Feature source: `feature/strata-virtual-geometry` at
  `609c311e91f498f3b632789d4c81a27325947019`.
- Main inspected at `b80601eb3fdcafddedbe9a9387f78a2fea0e499f`.
- Common merge base: `58e29ab052458fffcdebebffe27022b53adb9367`.
- GitHub compare main...Strata: **535 ahead / 2017 behind** (high churn).
- New safety branch `integration/strata-main-stage19` created FROM
  Strata head. It does **NOT yet contain current main**.
- Native merge attempt in this agent's isolated container could not
  fetch github.com (DNS unavailable). GitHub connector can create
  branches/files but offers no general conflict-aware git merge write.
- **No actual merge, C++ build, CTest, Metal/Vulkan/GLES device acceptance
  or end-to-end performance benchmark was completed.**
- Feature branch and main were not changed by this stage.

## Inspected high-risk intersections
1. `engine/render/GeometryScene.{h,cpp}`:
   main retains its existing 171-line implementation while Strata
   changes its lifetime, draw preflight, staging and residency.
   Preserve original HLOD, cache, render packet paths and all new
   Strata opt-ins; conflict resolution must be semantic.
2. `engine/render/Renderer3DModels.cpp`, `Renderer3D.h`,
   `Renderer3D.cpp`, `Renderer3DReflect.cpp`, `Lights.*`:
   keep one authoritative material/draw submitter; avoid duplicate
   main, sun, spot and reflection draws. Review batching changes
   introduced after Strata's pinned GlobalGeometryPool integration.
3. `engine/gfx/Gfx.h`, `metal/GfxMetal.mm`, Vulkan files:
   Strata extends readback/compute; main independently extended
   diagnostic GPU timing. **Main now exposes `GpuTimingRouter`,
   `timeGpuFrame`, `takeGpuTimings`, and independent subscribers.**
   Reuse `device.gpuTimingRouter()` for future timing; never create
   a second destructive `takeGpuTimings` consumer. Stage18.3 Strata
   counters are RHI dispatch counts, NOT GPU timestamps.
4. `CMakeLists.txt` and `AGENTS.md`: substantial independent edits.
   Combine both target sets and ownership entries. Do not wholesale
   replace either side.
5. Backend acceptance remains Metal/Vulkan/GLES. Existing
   STRATA GPU production parity switches stay OFF.

## Safe local sequence (owner Mac)
```bash
set -e
cd ~/Projects/AIVERSE
git fetch origin main feature/strata-virtual-geometry integration/strata-main-stage19
git worktree add ../AIVERSE-strata-stage19 origin/integration/strata-main-stage19
cd ../AIVERSE-strata-stage19
git switch -c local/strata-stage19
git status --short
git merge --no-ff --no-commit origin/main || true
git status --short
git diff --name-only --diff-filter=U
```
Do **not** continue into build before resolving every conflict and
checking the semantic interfaces. Avoid `git merge -X ours` or
`-X theirs` for core rendering files. If clean, inspect and commit
merge explicitly. Push only after validation (to the existing isolated
integration branch using an explicit fast-forward, not force-push).

## Focused validation after merge resolution
```bash
cmake -S . -B build/strata-stage19 \
  -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_STRATA=ON
cmake --build build/strata-stage19 \
  --target aiverse_strata_builder_tests \
           aiverse_strata_model_adapter_tests \
           aiverse_strata_view_tests \
           aiverse_strata_gpu_scene_tests \
           aiverse_strata_gpu_culling_tests \
           aiverse_strata_portable_tests \
           aiverse_strata_lod_benchmark --parallel 4
ctest --test-dir build/strata-stage19 -R '^strata_' --output-on-failure
python3 tools/strata-stage18-image-audit.py --self-test
```
After this focused run, perform the **complete project build + CTest**
and manually enabled GPU tests with genuine Metal on the Mac.
Vulkan/GLES require appropriately configured target environments;
do not claim these backends passed just because the Metal target
compiled. Use the Stage18.5 capture manifest for actual image parity.

## Next agent action
1. Review any newer main commit after the pinned audit.
2. Resolve the real local merge conflict list, especially
   `GeometryScene`, `Renderer3DModels`, RHI and CMake, in the
   isolated worktree; do not modify `main`.
3. Record concrete resolved files, exact commit SHA, actual build and
   test outcomes in the next handoff. If any test failed, include logs.
4. Only propose merging PR #35 into main after full device evidence,
   feature parity, and benchmarks. Never assert PASS for tests not run.
