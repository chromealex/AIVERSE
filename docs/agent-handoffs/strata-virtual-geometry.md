# Strata virtual geometry — agent handoff (stage 1, 2026-11-19)

## Resume here
Branch: `feature/strata-virtual-geometry` (based on `main` at branch creation).
Implementation commit: `a87d4fd4fa8b3609243530ab44bde261ccee2391`.
This document is the first Strata handoff. Read `AGENTS.md`, `docs/metal-batching-agent-handoff.md`, `docs/geometry-performance.md`, `docs/geometry.md`, `docs/models.md` and inspect current branch HEAD before edits. Preserve other agents' changes.

## Existing architecture found on main
- `engine/render/Geometry.h`: `GeometryPolicy`, `GeometryCluster`, `GeometryData`, CPU geometry preparation and occlusion.
- `engine/render/GeometryScene.h`: static instances, HLOD groups, cache, proxies.
- `engine/render/AutoLodRuntime.cpp`: meshoptimizer-based runtime LOD for eligible imported opaque rigid models.
- `engine/gfx/Gfx.h`: BufferType::Storage/Indirect, DrawIndexedIndirectCommand (20 bytes), Metal/GLES/GL/Vulkan. Existing indirect commands are generated on the CPU (see `docs/models.md`).
- `docs/geometry-performance.md`: restrictions of current CPU clustering, shadow selection, cache and Metal batching.
Do NOT duplicate GeometryScene's ownership, issue independent material state/draw submits, or replace existing batching without A/B evidence.

## Stage 1 implementation (completed)
Added `engine/render/StrataGeometry.h`: independent CPU reference types `ClusterNode`, `SelectionInput`, `SelectionOutput`, hierarchy `validate` and `select`. Flat ordered DAG-free tree with roots, per-node material/index references and residency; child refinement is atomic (fallback to parent if any child is not resident); projected per-node pixel error supplied by caller; bounded selection and explicit budget/missing-residency diagnostics.
No existing runtime pathways have been modified. This is a preparatory contract, not a functioning virtual geometry backend.

## API / compatibility
Namespace `aiverse::render::strata`. Header-only API; no new CMake targets, binary formats, ECS components, shader resources, backend capabilities or save formats. No breaking runtime changes.
`select` has no occlusion/frustum handling; current renderer remains the visibility authority. Root clusters must be resident. Input errors must be computed conservatively for the actual pass. Keep material groups and opacity compatibility in asset pipeline.

## Actual validation
GitHub commit successfully created. Local compilation, CTest, GPU tests and CI: **NOT RUN** (GitHub connector does not execute native build commands). DO NOT claim this compiles or renders on hardware. First next action: compile isolated header and add reference-selector fixtures.

## Next actionable stage
1. Add `tests/strata_geometry_test.cpp`, CMake test target, verify invalid graphs, resident fallback, refinement, budget, NaN/Inf and material partition cases. Fix selector issues before enabling runtime path.
2. Implement offline/imported cluster building with existing pinned meshoptimizer; use group hierarchies and conservative error/bounds, preserve original source mesh/LOD for fallback.
3. Add a single shared GeometryScene→Strata adapter, opted in by capability/asset policy. Do not divert dynamic/skinned/morphed meshes by default.
4. Introduce GPU Scene instance storage owned by the render presentation layer, not simulation ECS, then GPU frustum/Hi-Z selection, indirect command compaction, residency/streaming with feature-tested Metal/Vulkan backend paths. GLES/WebGL keep CPU fallback.
5. Add per-pass diagnostics, GPU timestamps and A/B benchmarks on instancing-heavy scenes plus terrain/foliage; do not report hypothetical speedups as measured.
6. Integrate TerraWeave immutable chunks first, then dirty-region rebuild and PHYSIS event notifications with versioned handles; only after residency and synchronization safety are proven.

## Coordination
- Strata owns mesh cluster hierarchy, selection, geometry residency and presentation-side GPU visibility.
- Existing batching owns compatible material/draw grouping; do not develop a second command allocator in parallel.
- TerraWeave owns terrain generation and changed-chunk events; Strata consumes render meshes/versions, does not mutate terrain.
- PHYSIS supplies world events and simulation outcomes; no genre dependencies.
- Prisma is postprocessing, SpectraFX is VFX, neither owns Strata geometry.

## Handoff discipline
Every stage: first commit code, then append a dated section here with exact code SHA, modified paths, actual tests and results, blockers, API compatibility and one next action; commit handoff separately. Never write PASS for unrun tests. Rebase/merge main safely before substantial integration; avoid force push.


---

## Stage 2 — deterministic meshlet/cluster asset builder (2026-11-19)

### Branch and exact implementation commits
- Branch: `feature/strata-virtual-geometry`; base originally created from `main`.
- `12cff0e026db581869f352b5302f4c20be2c126f`: new `engine/render/StrataBuilder.h` import-time public types and API.
- `81377a619cbbbbecf4ff521a71ec1314d0bbec13`: new `engine/render/StrataBuilder.cpp` meshlet packer, parent simplification and BFS tree layout.
- `86d64e2417916aa3ecb04a9bb1a9da189986114f`: new `tests/render/strata_builder.cpp` scenarios.
- `82e84e8475f7589d60f7dc439c0d09526f810a15`: CMake option `AIVERSE_BUILD_STRATA=ON`, standalone `aiverse_strata_builder` and `strata_builder` CTest target.
- `3527d0f7994e6ac72da86f93bfd3bf5f272bf598`: active asserts under Release tests.
- `a260e9155e0c7e38093cf909a4b10b6f93d22f3b`: API/architecture document `docs/strata.md`.
- `3a813fece0aad4c8454939da12bd3240e3d4d0fa`: ownership and handoff pointer in root `AGENTS.md`.
- `f00ee0c0ae372a89c3995d0bb900cb3aa1c7128f`: explicit `<iterator>` include.

### Implemented
- Transactional, CPU-only import-time `build(input, options, asset, error)` for a **single material partition**. Input is borrowed, bounds and indices are validated; failure does not overwrite an existing asset.
- Deterministic greedy meshlets in source triangle order, with per-leaf local uint8 vertex indexing, global vertex mapping and original-triangle indexed fallback. Defaults are 64 vertices / 124 triangles.
- Group parent generation via already vendored meshoptimizer v1.3 `meshopt_simplify` (no new vendored dependency): border lock and absolute position error, cumulative parent error, per-node AABB, opt-out of ineffective parent groups (less than 10% reduction).
- Tree flattening to the existing `StrataGeometry.h` top-down contiguous-child contract. Multiple roots allowed. All newly generated data CPU-resident until a future residency manager exists.
- Isolated `aiverse_strata_builder` static target and focused CTest; existing `aiverse_models`, `GeometryScene`, RHI and active draw paths remain unchanged.
- `docs/strata.md` is the public engineering contract; AGENTS now points to it and this handoff.

### API and compatibility
- New headers are `engine/render/StrataBuilder.h` and `StrataGeometry.h` in `aiverse::render::strata`.
- New CMake option `AIVERSE_BUILD_STRATA` defaults ON. This adds the builder to default build and keeps it unlinked to production renderer; it shares the existing meshoptimizer library, including when runtime AutoLOD is OFF.
- No changes to existing model/game/save formats, existing runtime draw behavior, frame scheduling, shaders, ECS state or network semantics.
- Parents represent indexed-draw *cluster groups*, not guaranteed hardware meshlets. Leaves fit configured compact meshlet limits. All simplification uses only vertex positions; normals, UV, tangent and color seam behavior still needs attribute-aware handling.
- Source LOD0 and existing model buffers remain intact; no model importer is calling this builder yet. No Vulkan/Metal/GL GPU functionality is claimed.

### Testing and CI (actual status at handoff)
- Compilation: **NOT RUN** (working environment lacks the project checkout and can only use GitHub connector; not a local build runner).
- CTest `strata_builder`: **NOT RUN**. CMake target and fixture were added, but there is no execution result.
- GPU / visual parity / benchmark: **NOT RUN**.
- GitHub workflow runs for `3a813fe` were observed **queued/pending**, no successful conclusion was available at check time. Do not interpret queued as PASS, and inspect the current head's CI independently.

### Specific next step (mandatory before enabling rendering)
1. Pull the exact current feature branch and newest `main` safely (no force push); read `docs/strata.md`, this handoff, root `AGENTS.md` and batching handoff.
2. Configure and compile `aiverse_strata_builder_tests`:
   `cmake -S . -B build/strata -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_STRATA=ON`
   `cmake --build build/strata --target aiverse_strata_builder_tests`
   `ctest --test-dir build/strata -R '^strata_builder$' --output-on-failure`
   Fix compiler errors, warnings or failing assertions with committed implementation and a fresh appended handoff.
3. Expand importer-side attribute preservation: `meshopt_simplifyWithAttributes` and explicit material/UV/normal/vertex-color seam tests, plus error/bounds monotonicity. Evaluate simplifier adjacency and crack-free LOD transitions; conservative error and border lock are not sufficient to claim universal crack-free results.
4. Only then add an **opt-in** single `GeometryScene` / `Model` adapter maintaining a single instance/material grouping owner; use existing indexing fallback until GPU culling and GPU residency are verified.

### Blockers / handoff warning
Current asset builder is not a GPU-driven renderer and has not been compiled in this session. Avoid merging PR #35 into `main` as production-ready until focused native tests and full renderer/asset compatibility checks pass. Do not let another agent duplicate Strata meshlet storage in batching, TerraWeave or PHYSIS.


---

## Stage 3 — weighted simplification and read-only Model adapter (2026-11-19)

### Branch / code commits
Branch `feature/strata-virtual-geometry`; no merge into main. Existing draft PR is #35.
- `11ec604a4fcf3a9e044b991ea8685f9d9896ed51`: `engine/render/StrataBuilder.h`, optional borrowed float attributes/stride/count/weights in `BuildInput`.
- `4773baf2b4d4c625a240d58e2ea83f81db92ee0a`: `engine/render/StrataBuilder.cpp`, validate all attribute records and finite positive weights; use pinned `meshopt_simplifyWithAttributes` when provided, preserving the old position-only path.
- `dedddc9c4c5368e411514357c343517a7692d95f`: `engine/render/StrataModelAdapter.h`, new opt-in, presentation-only API `buildModelAsset(const Model&, const BuildOptions&, ModelAsset&, std::string*)`.
- `9485c02f90187a6faf32224df07a557e9d1b70ef`: `engine/render/StrataModelAdapter.cpp`, construct per-primitive, per-material assets from existing CPU `Model` without mutating it; 15 weighted vertex attributes (normal, UV, tangent, RGBA, packed lightmap UV); skip unsupported materials/animation/deformation/existing authored LODs; transactional failures.
- `1f2d48f84ee1197ffc04ea227f202994a5419bb8`: `tests/render/strata_model_adapter.cpp` read-only, skip-policy and transactionality assertions.
- `bb070029f04f5b5a77e87edd344083a753374cab`: `CMakeLists.txt`, new static `aiverse_strata_model_adapter` target and `strata_model_adapter` CTest.
- `21fa0b2b9bab41d0988485fccf53d89e0287a35e`: `docs/strata.md`, public API updated.
- `60619eb0560e03f9904f985ab91d8c49283b51c0`: `tests/render/strata_builder.cpp`, exact source LOD0 triangle reconstruction, monotonic parent errors and invalid attribute layouts.
- `b18ddf84746644fe2cd921080d6e1687852aa6ab`: `AGENTS.md`, ownership/status update.

### Implemented and constraints
New Model adapter does **not** hook into active `GeometryScene` draw path. Material partitions retain their exact indices; all source Model vertices, indices, nodes, transforms and LOD0 remain unchanged. Skinned/morphed/mutable, authored-LOD, uploaded GPU, alpha masked/blended and shader graph surfaces are explicitly skipped. Output is wholly owned CPU data with diagnostic skip reasons. Any failure building an eligible primitive keeps the previously returned `ModelAsset` untouched.

The adapter uses `ModelVertex` layout (60 bytes, xyz at offset zero), supplies 15 components per vertex to meshoptimizer. Parent errors are cumulative; retaining locked borders alone does not prove watertight cross-LOD transitions. No runtime GPU path, no new shader, no new scene JSON or persistence format, no ECS changes.

### Tests and CI observed (NOT a pass)
- Local compiler / CMake / CTest: **NOT RUN**. Connected GitHub cannot execute a native build, and this assistant container cannot clone github.com.
- New CTest targets: `aiverse_strata_builder_tests` and `aiverse_strata_model_adapter_tests`. Tests **added, not executed**.
- Historical CI for commit `507ab6d0` was observed with the main CI workflow **cancelled** (other validation workflows queued). CI for `bb070029` and `60619eb0` was observed **queued**, no concluded success. Check newer head before claiming an outcome.
- No GPU timing, image comparison, streaming pressure or memory regression evidence exists.

### Immediate next tasks
1. On an AIVERSE build-capable machine, fetch and compile the branch:
   `git fetch origin feature/strata-virtual-geometry`
   `git worktree add --detach ../AIVERSE-strata origin/feature/strata-virtual-geometry`
   `cmake -S ../AIVERSE-strata -B ../AIVERSE-strata/build/strata -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_STRATA=ON`
   `cmake --build ../AIVERSE-strata/build/strata --target aiverse_strata_builder_tests aiverse_strata_model_adapter_tests -j 4`
   `ctest --test-dir ../AIVERSE-strata/build/strata -R '^strata_(builder|model_adapter)$' --output-on-failure`
2. Fix any compile/test errors and commit implementation; append a new section to this handoff as a **separate commit**.
3. Add material-safe and watertight transition fixtures: normals, UV seams, texture parity, geomorph/skirts/stitching and nearby clusters at different LOD depths. Verify error budgets with per-pass projection and non-uniform transforms.
4. Integrate **opt-in staging only** into `GeometryScene` after native checks. Keep current geometry owner/batching/draw submission untouched until profiling/visual parity demonstrates safety. Never enable both draw paths for one primitive.
5. Then implement GPU scene buffers/culling behind capability gates; retain CPU indexed fallback on all backends.

### Important integration ownership
Strata's `ModelAsset` is an auxiliary render-asset representation. `GeometryScene` remains the static instance owner, `Renderer3D` remains the material/draw grouping authority, and TerraWeave/PHYSIS keep simulation and terrain ownership. Avoid modifications to batching agent's concurrent GPU indirect code without coordinating current main and its handoff.


---

## Stage 4 — opt-in GeometryScene CPU staging (2026-11-19)

### Resume point / scope
**Branch:** `feature/strata-virtual-geometry`. Current design remains staging-only: default-OFF, no replacement of active `Renderer3D`, no new GPU submission, no ECS or scene format changes. PR #35 stays draft until native and GPU parity verification.

### Exact implementation commits (ordered)
- `0a47de5ed76ae833cbb1b2584e5e0c7dca06e000`: `engine/render/StrataModelAdapter.h` — `StagingStats`, `SceneStaging` APIs, per-source bounds, diagnostics and cache.
- `cb11bf5a4e5a16d46850498d2db12358ff49035c`: `engine/render/StrataModelAdapter.cpp` — deduplicate by source key, preserve old cache on failures, skip oversized source, collect skip/failed/staged source statistics, `find()`, `clear()`.
- `def009b11c3edd2df2fc86459dc85eb21649dfc1`: `engine/render/GeometryScene.h` — staging opt-in setter, support query, source lookup, counters and staging ownership.
- `16e93c91de762a309a269e0f67f42fd1a7a4e2f5`: `engine/render/GeometryScene.cpp` — stage source models once after geometry preparation and before `uploadModel()`, exclude proxy-only assets, release on `shutdown()`, preserve existing draw path.
- `fc9f49a6c2643de1a70c691ba500f692fe1e1e4c`: `CMakeLists.txt` — link adapter into runtime under `AIVERSE_BUILD_STRATA`, define `AIVERSE_STRATA_STAGING_ENABLED` only on runtime target.
- `39fda592f3b8188a75cb6c042abbb0a5702f1b1a`: `tests/render/strata_model_adapter.cpp` — duplicate source keys, error isolation, storage ownership, staging limits and teardown.
- `ec5e88a94b69f8ab8919f01a3fa2b90b955161ea` and `87fe730a5b298ee7ebe0666e17c1854117ccc00f`: separate skipped-source and skipped-primitive diagnostics in `GeometryScene`.
- `febe96105f1e1c211f249943e639dbb6b60d3a39`: `docs/strata.md` — native API, lifecycle, CPU/GPU boundaries and memory counters.
- `1fbce8c3d45faa5b527cbd998799919ca96fdf48`: `AGENTS.md` — explicit Strata ownership and default-OFF state.

### What is working in code (not yet tested on hardware)
- `GeometryScene::setStrataStagingEnabled(true)` **must be called before** `load()` / `loadPrepared()`. Disabled by default; scene JSON has no Strata field.
- A scene creates exactly one `strata::ModelAsset` per *participating original source model* (keyed by the scene model file). Duplicate scene instances use that same CPU asset. Source-only surface proxies and generated HLOD proxies are excluded.
- Builds happen before `uploadModel` can release raw CPU vertices, and after legacy `prepareGeometry`; no changes to source model materials, indices or node transforms. `Renderer3D::drawModel`, material grouping, shadows, GI and HLOD remain authoritative.
- `GeometryScene::strataAsset(file)` returns a pointer to the staged CPU asset or null. **Never retain this pointer after shutdown/reload**. `strataStagingSupported()` reports whether Strata was compiled into the runtime.
- Per-source staging defaults to max 250,000 vertices / 750,000 indices; oversize/invalid sources do not abort the existing legacy scene upload.
- `GeometryScene::stats()` adds `strataSources`, `strataPrimitives`, `strataSkippedSources`, `strataSkippedPrimitives`, `strataFailures`, `strataNodes`, `strataMeshlets`, `strataIndexBytes`. The latter covers auxiliary flat index data only, *not* total heap or GPU residency. Scene shutdown clears the staging memory and statistics.

### API/format compatibility
Public native C++ API is additive. Existing `GeometryScene` declarations, `setRuntimeOptions`, save/scene JSON format, model assets, ECS and RHI are unchanged. `AIVERSE_BUILD_STRATA=OFF` compiles a no-op capability path with no staging objects; other renderers keep working. This stage does **not** implement a Strata draw path or GPU Scene, culling, GPU residency, streaming, dynamic terrain nor skinned mesh support.

### Tests and CI — factual, NOT a passing claim
- Code-level tests added to `tests/render/strata_model_adapter.cpp` for source deduplication, limits, input validation/failure isolation and `clear()`. Existing `tests/render/strata_builder.cpp` continues to exercise meshlets and hierarchy.
- **Native C++ compile: NOT RUN.** This assistant's container has no AIVERSE checkout and GitHub cloning from that container failed due DNS resolution. GitHub connector can edit files but not execute a native build.
- **CTest: NOT RUN.** No measured GPU performance, screenshots or render parity tests.
- GitHub CI for preceding head `c7098130` was seen **cancelled**. CI for staging commit `fc9f49a6` was also observed **cancelled**; other validation workflows were queued. No completed passing run was observed. Do not claim green CI until checking actual latest head's jobs/logs.

### Immediate next actionable step
First validate the native build on Apple Silicon / Linux, before any GPU draw integration:

```sh
cd ~/Projects/AIVERSE
git fetch origin main feature/strata-virtual-geometry
git worktree add --detach ../AIVERSE-strata origin/feature/strata-virtual-geometry
cmake -S ../AIVERSE-strata -B ../AIVERSE-strata/build/strata -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_STRATA=ON
cmake --build ../AIVERSE-strata/build/strata --target aiverse_strata_builder_tests aiverse_strata_model_adapter_tests -j 4
ctest --test-dir ../AIVERSE-strata/build/strata -R '^strata_(builder|model_adapter)$' --output-on-failure
```

Then add and run a real `GeometryScene` staging fixture with a recording gfx::Device and compare the exact legacy draw calls/counters at staging OFF vs ON; verify two instances of one model create one CPU asset, failed/oversized source continues to draw, unload/reload frees assets, HLOD/shadows unchanged. Respect `docs/metal-batching-agent-handoff.md` and the latest main changes. Only after proof of compile and draw parity, implement a capability-gated Strata selected-range adapter with one material/draw submission owner. Keep it OFF by default. Record concrete test commands/logs in the next separate handoff commit.

### Risks to track
- Staging consumes auxiliary CPU memory: enforce project-wide and per-scene budgets before production, not just per-source input caps.
- Import simplification does not yet prove crack-free transitions, topology stability or attribute/texture parity under all content and pass views.
- No source asset handle/version tracking for dynamic/streamed TerraWeave meshes.
- GPU culling and indirect draw path are unimplemented, so the presence of staging counters must never be presented as a performance improvement.
- Concurrent batching changes exist in other branches; do not merge renderer internals without resolving interface ownership.


---

## Stage 5 — merge incremental static model cache into Strata (2026-11-19)

**Merge commit**: `0913b82088b63913e1313557de16d85e7c79b89f` in `feature/strata-virtual-geometry`. Two parents:
1. Pre-merge Strata `4c4a5587b26f3c81e9d33b770c962837db56548f`
2. Pinned batching `1043a9de747fe89cb42de899c15c6d2f4db8e0e1` from `perf/incremental-static-model-cache`

The merge was constructed on GitHub using the pinned batching tree as base, overlaying exactly the 12 non-overlapping Strata files and manually combining the **Strata ownership section** into the batching branch's latest `AGENTS.md`. Thus all GPU batching, renderer, RHI, geometry pool and its agent documentation remain intact; all Strata files and CPU staging are preserved. GitHub compare confirmed the merged head is descended from both parents. Against pinned batching, the only changed paths are Strata's 13 paths including combined `AGENTS.md`. No changes were sent to the batching feature branch.

**Subsequent engineering documentation commit**: `6c48a4bf9e7b71e021d9b403e1485e8ba06766c2` updates `docs/strata.md` with shared ownership/API policy.

### Integration decisions
- Reuse `GlobalGeometryPool` and its geometry revision/eligibility contracts. Do not create a second GPU pool for source models.
- Reuse existing `StaticModelCache`, `StaticModelCachePolicy`, model geometry/material revision paths and existing `EcsStaticModelRender` adapter.
- `Renderer3D` owns CPU MDI/GPU ICB/material grouping and emits draw commands. Future Strata culling must supply visible cluster ranges through one exclusive rendering path; never double-submit the original source.
- `GeometryScene` still owns source instances, HLOD and Strata's *opt-in CPU staging*. The existing legacy render route remains authoritative. All experimental render flags retain defaults.
- Treat the Strata PR as stacked atop batching PR #34 while the latter is draft, to prevent duplicated PR diffs and accidental merge into main before its dependency.

### Evidence and verification
- Branch and commit ancestry: **verified through GitHub's compare and Git Commit API**.
- Source integration conflict resolution: only `AGENTS.md` overlapped. Both sections retained in merged tree; all previously unique paths preserved.
- **Native compile, CTest, Metal screenshots, full Vulkan/GLES checks: NOT RUN in this session**. Assistant container has no mounted AIVERSE checkout; connected GitHub tools cannot execute native builds.
- Batching PR #34 is still draft and under active development. Its owner's actual 2026-11-19 small synthetic Metal run (handoff Stage 69) passed 33 CPU checks and pixel parity with Direct fallback on that fixture, but *does not validate the Strata merge* or bigger CPU/GPU performance.
- The batching agent pushed newer feature branch commits while merge planning. Integration deliberately pins `1043a9de`; check `git fetch origin` for further batching updates before depending on newer APIs.
- There is still no Strata GPU cluster-selection or residency implementation, and no measured frame-time reduction.

### Next exact actions
1. Fetch both branches and compile the merged Strata feature branch on a Mac Studio:
   `git fetch origin main perf/incremental-static-model-cache feature/strata-virtual-geometry`
   `git worktree add --detach ../AIVERSE-strata-merged origin/feature/strata-virtual-geometry`
   `cmake -S ../AIVERSE-strata-merged -B ../AIVERSE-strata-merged/build/strata -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_STRATA=ON`
   `cmake --build ../AIVERSE-strata-merged/build/strata --target aiverse_strata_builder_tests aiverse_strata_model_adapter_tests aiverse_material_batches -j 8`
   `ctest --test-dir ../AIVERSE-strata-merged/build/strata -R '^strata_(builder|model_adapter)$|^model_batching_policy$' --output-on-failure`
   If `aiverse_material_batches` is not enabled under this configuration, inspect its CMake guard, enable required option explicitly and record it.
2. Run explicit staging OFF/ON parity on `GeometryScene`, including repeated models, failures, unload/reload, shader/material combinations and moving shadows. Use real Metal validation before enabling experimental ICB.
3. Add an *exclusive* shared render-command adapter in existing batching; only after step 1+2 pass introduce Strata selection output into the runtime. Gate by backend capabilities and maintain legacy direct rendering.
4. Before any future batching merge, compare current `perf/incremental-static-model-cache` head against pinned `1043a9de`, inspect its handoff, and merge only after checking concurrent renderer ownership and test status.
5. Record code and verification in a new handoff appendix and commit separately. Do not mark checks that only queued/cancelled as passing.

### Risks
- Both branches are experimental and under heavy active development; a clean semantic merge does not prove compiler or GPU compatibility.
- Batching branch uses large shared renderer and GPU pool; future Strata-specific GPU logic must not fork the pool/command scheduler.
- Stack PR #35 on PR #34 until batching lands; keep draft and do not merge into main prematurely.


---

## Stage 6 — per-pass CPU cluster selection oracle (2026-11-19)

### Branch, upstream sync and exact implementation commits
**Branch:** `feature/strata-virtual-geometry`, draft PR #35, stacked on `perf/incremental-static-model-cache` PR #34. Main unchanged.

- `d9c4dd4f55b8b15ce31d7921f4995f79ac530b17` — two-parent merge of fresh batching `29f6cb3c7cd8965b5123bfbf17f09d2164e45dbb` into Strata. The batching difference since pinned `1043a9de` was its handoff doc (not a renderer implementation change). The merge tree initially had that doc's earlier blob.
- `38abceaa6cf30f028fb9df5e9d3074ca602349e5` — **corrective update** copying exactly the new batching handoff blob into Strata. Do not drop this fix or falsely describe the diff as a material_batches fixture change.
- `799686566cef85411e15d9ab88cb0ef161aa3ac4` — `engine/render/StrataView.h`, read-only `ViewConfig`, `SelectedRange`, `ViewPlan`, `planView` public API.
- `288c15c90170fe87cb37ca7ad71e4d03e167b620` — `engine/render/StrataView.cpp`: validating source topology and node bounds/indices, projecting errors via existing `geometryErrorPixels`, using residency-aware hierarchy selection, conservative frustum rejection, transactional output.
- `76ce342609a42877a1be9f4d0c9ca78c1da5e688` — `tests/render/strata_view.cpp`: main vs scaled pass, parent/child detail choice, root fallback on missing residency, budget restriction, frustum exclusion and malformed-asset rejection.
- `a07c002f4e4684897331c451c4a870df2300a564` — `CMakeLists.txt`: `aiverse_strata_view` library and `aiverse_strata_view_tests`/`strata_view` CTest registration.
- `596c5292c6603a5b3c4afdb9173002c3666ca4d2` — `engine/render/GeometryScene.h`: read-only `planStrataView(sourceKey, mesh, primitive, config, out, error)`.
- `226c240e9214ca7c104db79fe73ce348b77211e6` — `engine/render/GeometryScene.cpp`: delegates source-local selection to staged `ModelPrimitiveAsset`; reports absent/ineligible source without changing scene or output. Compiles to no-op if `AIVERSE_BUILD_STRATA=OFF`.
- `7eb22c2737df70d488de8dadcace1704ca7985e8` — `CMakeLists.txt` links runtime to `aiverse_strata_view` instead of only the Model adapter.
- `95cb5c4c62927edc35f3afbd9bebecbeee402b12` — `docs/strata.md`: Stage 6 planner API and caveats; repairs malformed/duplicated 'Known shortcomings' section inherited from previous doc edits.
- `bd3ecb7a9101826a781a6512cf71b6d2ea1f6f19` — `engine/render/StrataView.cpp` adds explicit `<utility>` header.
- `fb35f10072bfaf270e6e8e4c4f3bd1becc6fe9a1` — `AGENTS.md` update with source-local index-offset safety.

### What Stage 6 actually implements
- `strata::planView` takes **one already-built primitive asset** and a pass-specific object-to-clip transform, target dimensions, depth convention, pixel threshold and max node budget. It returns a list of selected node index ranges in **that Strata asset's own** index vector, plus selection/visibility diagnostic stats. It never creates gfx buffers or submits draws.
- The implementation reuses `strata::select` and existing `render::geometryErrorPixels` / `render::geometryBoxVisible`. CPU plan results for main, sun shadows, reflections and portals are intentionally independent. Caller supplies objectToClip including the correct bound node transform. Non-uniform model transforms are part of that matrix.
- Frustum testing occurs AFTER choosing resident hierarchical detail. Near-plane conservative error can prevent refinement but does not cause holes; missing child residency keeps the parent. No GPU Hi-Z, occlusion history or runtime geometry compression has been added.
- `GeometryScene` now exposes `planStrataView`, which uses its existing staging cache, picks a source material partition by mesh/primitive ID, and delegates to the same planner. Must enable staging before scene load to retrieve any asset. No draw list or instance owner is duplicated.

### Ownership, compatibility and blocker
- **Critical:** `SelectedRange::firstIndex` refers to `strata::Asset::indices`, never `Model::indices` or a pooled `Model::ib`. The caller cannot submit this range until the auxiliary Strata index stream is uploaded and offset correctly into a shared-pool page. Do not insert these offsets directly into the existing MDI command path. A dedicated versioned residency/allocation contract with `GlobalGeometryPool` is the next design/implementation gate.
- Public APIs are additive, opt-in and read-only. Existing scene JSON, saves, ECS, material groups, shadows and `Renderer3D::drawModel` remain unchanged. No D3D/Metal/Vulkan shader or index-buffer format change.
- The planner CPU implementation is a reference oracle, not an optimized production GPU path; it visits all nodes and reruns hierarchy validation. It is useful for correctness tests but should not be run unconditionally each frame.
- CPU view selection currently uses conservative frustum planes and projected geometric errors; cross-LOD cracks, material seams, dynamic deformation, GPU timestamps and real frustum test parity remain unverified.

### Real verification evidence
- GitHub connector successfully created and updated code and documentation commits; two-parent branch ancestry verified in previous stages.
- **Native compilation: NOT RUN. CTest: NOT RUN. GPU/Metal/Vulkan visual parity: NOT RUN.** This assistant does not have the AIVERSE source mounted in a build-capable environment.
- Checked GitHub Actions: CI for `38abceaa` and `a07c002f` was observed **cancelled**; at `95cb5c4c` it was **pending** and other workflows **queued**. No passing status has been observed. A queued/absent status is not a pass.

### Exact next steps, in order
1. On native Mac Studio:
   ```sh
   cd ~/Projects/AIVERSE
   git fetch origin main perf/incremental-static-model-cache feature/strata-virtual-geometry
   git worktree add --detach ../AIVERSE-strata-stage6 origin/feature/strata-virtual-geometry
   cmake -S ../AIVERSE-strata-stage6 -B ../AIVERSE-strata-stage6/build/strata -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_STRATA=ON
   cmake --build ../AIVERSE-strata-stage6/build/strata --target aiverse_strata_builder_tests aiverse_strata_model_adapter_tests aiverse_strata_view_tests -j 8
   ctest --test-dir ../AIVERSE-strata-stage6/build/strata -R '^strata_(builder|model_adapter|view)$' --output-on-failure
   ```
2. Fix compiler/test issues and record native results, including exact logs and platform, with implementation commit and separate handoff commit.
3. Add `GeometryScene` recording-Device OFF/ON draw parity tests, including source-node transforms and shadow-only visibility; ensure root-fallback coverage never draws a source primitive twice.
4. Design and implement a versioned auxiliary index/cluster metadata allocation managed by existing `GlobalGeometryPool` and RHI lifetime fences. Keep batch material/indirect command ownership in `Renderer3D`, with an exclusive Strata/legacy decision per eligible primitive.
5. Add GPU frustum/LOD/Hi-Z compute path with CPU `strata::planView` parity oracle, platform capability gates and CPU fallback. Only then performance benchmarks for >=1k/10k/100k instances.

### Development coordination
Batching branch ownership is separate. Its live head can advance after this Stage 6 snapshot; verify newest PR #34/head before another merge. Keep feature flags default OFF and PR #35 Draft; do not merge either experimental code path into main until native and cross-platform validation is complete.


---

## Stage 7A — GPU cluster metadata anchored to GlobalGeometryPool (2026-11-19)

### Branch and exact implementation commits
**Branch:** `feature/strata-virtual-geometry` (Draft PR #35 stacked on active batching PR #34).
- `cffb601ee04cf9c05bb1213c88e958302276ad1a` — new `engine/render/StrataGpuScene.h`: versioned GPU metadata storage layouts and per-scene staging interface.
- `7a2baf58249b7d67e2edd4a8f6139b219c691816` — new `engine/render/StrataGpuScene.cpp`: pure transactional metadata packer, existing-shared-pool admission, metadata buffer allocation, geometry revision/handle validation, explicit teardown.
- `46e5dc2d0a50171e1e11841327c4e091561fafec` — fix validation order before accessing a possibly empty cluster tree.
- `2cc8720847dc999cc4076653f2e5966707ef7b54` — new `tests/render/strata_gpu_scene.cpp`: binary layout/offset, material/root/child IDs, missing residency flags and malformed source cases.
- `662e082ecd7be89554bcb1af42b83a3581be3dd3` — `CMakeLists.txt`: new `aiverse_strata_gpu_scene` and `aiverse_strata_gpu_scene_tests` / `strata_gpu_scene` target; runtime linked only if `AIVERSE_BUILD_STRATA=ON`.
- `8615c55f2d138956884149cecfff8229356002fc` — `engine/render/GeometryScene.h`: GPU metadata feature toggle, query, statistics.
- `03df91ed51724b36df9ff7ab7e56c766a4e6306a` — `engine/render/GeometryScene.cpp`: stage metadata after ordinary `uploadModel`; on shutdown destroy metadata before source models.
- `f91798d35472ecb5280045da0da0b9baafda11f7` — `docs/strata.md` updated with layout, lifecycle and feature gates.
- `bf15b90723d65da1c9d62b91d6f7fd4a2aed6ed2` — `AGENTS.md` requires same shared geometry pool and forbids using source-local indices as pooled GPU draw offsets.
- `668b37d3d888eb18bebc39159ad97ad65095e239` — `tests/render/strata_gpu_scene.cpp` recording RHI fixture: use real `uploadModelToGeometryPool`, reject nonpooled model, allocate metadata once, invalidate on `markGeometryChanged`, shutdown both metadata and shared original GPU page.

### Current functionality
`strata::packGpuMetadata(ModelAsset, vector<uint8_t>&, error)` builds one version-1 packed Storage buffer payload:
- 32 B `GpuMetadataHeader`; 32 B `GpuPrimitive` per asset partition; 64 B `GpuNode` per hierarchy node; 4 B root global indices; 16-byte total padding.
- Validates nonempty and DAG-free forest, material partitions, bounds and index ranges; 32-bit overflow and 64 MiB temporary payload bound. Pack remains transactional on invalid input.
- GPU node `sourceFirstIndex` is deliberately **ASSET-local**, not a ready GPU index buffer offset. `GpuNode::resident` is a CPU flag snapshot, **not** proof of actual GPU-resident triangles.

`strata::GpuMetadataScene` is experimental metadata **only**. It uploads via `gfx::Device::createBuffer(BufferType::Storage, Usage::Immutable)` after checking an existing `Model::geometryPoolPage`, shared valid VB/IB, nonempty pool allocations, Metal or Vulkan backend and compute capability. It never allocates source geometry or calls `drawIndexed`; `Renderer3D` remains the single draw owner. Allocations are bounded to 1 MiB per source and 8 MiB per scene by default. Model pointer, `geometryRevision` and GPU VB/IB handles are rechecked for each `find`; stale records require explicit scene reload instead of in-place overwrite.

`GeometryScene::setStrataGpuMetadataEnabled(true)` enables this path **only** if `setStrataStagingEnabled(true)` also ran before scene loading. Both default OFF. `strataGpuMetadataBuffer(sourceKey)` returns the optional metadata handle or invalid. `stats()` adds metadata uploaded/declined/failed source counts and metadata bytes. `shutdown(dev)` frees metadata buffers before original source models and the device. Old scene JSON, ECS, save files, materials, draw calls, dynamic rendering and HLOD remain unchanged.

### Important blocker — geometry residency NOT implemented yet
The existing `GlobalGeometryPool` is shared by legacy models and handles full vertex/index pairs. It does not expose an API for adding *auxiliary Strata index-only spans* to a currently uploaded source without a versioned reservation, frame-complete retirement and overlap-safe writes. Stage 7A therefore uploads only metadata, and refuses dedicated/nonpooled sources. It does **not** publish GPU-resident Strata meshlets/indices, call compute culling or issue indirect draws. Never pass Strata's `sourceFirstIndex` to existing pooled `Model::ib`. A model with `Model::geometry` (older GeometryCluster path) is intentionally rejected from the current rigid shared pool; do not bypass this policy without a proven safe compatible representation. Metadata buffer availability alone must never activate a Strata render path.

### Verification actually done
- GitHub connector writes succeeded, file/API contracts were inspected. No changes were made to the active batching branch.
- **Native CMake compile: NOT RUN. Focused CTest: NOT RUN. Mock RHI fixture: NOT RUN. Metal/Vulkan hardware/raster test: NOT RUN.** This runtime does not have an AIVERSE checkout, and direct GitHub access from the local container fails DNS. The test code exists but has not been executed.
- GitHub Actions earlier Stage 6 head `db597cc7`: main CI and GPU batching checks were **cancelled**, not passing. Stage 7 code head `03df91ed`: main CI observed **pending**, other workflows queued. Re-check current head, not historical runs.

### Immediate next actions
1. On Mac Studio fetch feature branch, build and run all four focused tests:
   ```sh
   cd ~/Projects/AIVERSE
   git fetch origin main perf/incremental-static-model-cache feature/strata-virtual-geometry
   git worktree add --detach ../AIVERSE-strata-stage7 origin/feature/strata-virtual-geometry
   cmake -S ../AIVERSE-strata-stage7 -B ../AIVERSE-strata-stage7/build/strata -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_STRATA=ON
   cmake --build ../AIVERSE-strata-stage7/build/strata --target aiverse_strata_builder_tests aiverse_strata_model_adapter_tests aiverse_strata_view_tests aiverse_strata_gpu_scene_tests -j 8
   ctest --test-dir ../AIVERSE-strata-stage7/build/strata -R '^strata_(builder|model_adapter|view|gpu_scene)$' --output-on-failure
   ```
   Fix all compilation warnings/failures, report actual results in another implementation + handoff commit.
2. Add a real `GeometryScene` recording-device OFF/ON parity test, verify pooled vs dedicated model diagnosis and unload/reload with multiple worlds, material variants and shadow passes.
3. Coordinate with batching agent on **extending the existing GlobalGeometryPool** for bounded, versioned and frame-fenced *auxiliary index spans*, not a second VB/IB source pool. Preserve GPU allocation failure rollback, model revision and shared pool admission.
4. Only after sidecar indices are resident implement GPU scene instance records and compute frustum/LOD, with `strata::planView` as oracle and a **single mutually exclusive render submission owner**. Record real GPU timing and visual parity.
5. Check newest `perf/incremental-static-model-cache` head before syncing. In the previous stage it advanced while Strata was running; do not blindly overlay its files, handoff or CMake.

### Coordination and risks
PR #35 remains a Draft stacked on PR #34. Keep all GPU/ICB experimental flags OFF by default, do not merge into main until native CTest + platform validation. Metadata GPU buffers must be released while their owning gfx::Device is still alive; the ordinary backend is responsible for keeping in-flight references alive when destroyBuffer is called. No real GPU compute, index-stream residency or render acceleration has been demonstrated.


---

## Stage 7B — pooled auxiliary Strata index residency (2026-11-19)

### Resume point
Branch `feature/strata-virtual-geometry`, Draft PR #35 stacked atop the independently evolving `perf/incremental-static-model-cache` Draft PR #34. Do not merge either experimental branch into `main` without actual native and visual parity results. **Stage 7B is source-attached sidecar index residency, NOT GPU compute culling or Strata indirect rendering.** All experimental opt-ins stay disabled by default.

### Exact code commits, in order
- `781a624ee3d459503c0cc14a0439909190d1dfd7` — `engine/render/GlobalGeometryPool.h`: add move-only `GeometryPoolIndexSpan`, `uploadAuxiliaryGeometryIndices`, `auxiliaryGeometryIndicesValidFor`, explicit `releaseAuxiliaryGeometryIndices`, and separate sidecar counters.
- `7a87f98ca0020b5a81b6765e87faa89ed3e960a4` — `engine/render/GlobalGeometryPool.cpp`: allocate index-only sidecars in the *existing* model page, synchronized with `poolMutex`, check all source-relative indices, reuse released ranges only with verified GPU completion and recycling opt-in, retain page lifetime until last Model or sidecar owner releases; no second GPU page.
- `c54d33abe88f367695ae89725c2ace93b8168f4d` — `engine/render/StrataGpuScene.h`: `GpuPrimitive::gpuFirstIndex`, binary metadata v1/v2 mapping, per-source/scene index budgets, read-only `GpuMetadataScene::indexBinding`.
- `fa935f01ad062fffa88faa7c2f911b765550e836` — `engine/render/StrataGpuScene.cpp`: concatenate per-primitive Strata indices, upload into source shared page, version-2 GPU metadata patch with absolute per-primitive base indices, joint lifetime and allocation failure cleanup.
- `5f5dc552e27ab5269cba14b2389c530443946123` and `1dd5ecf968cb4e221d3d358f217af2cbd4e4d708` — `GeometryScene.h/.cpp`: `strataGpuIndexBinding(sourceKey, out)` and index residency stats; draw path unchanged.
- `3931b1e4b087de3b1acf872850694fdf8237ee9a` — `tests/render/strata_gpu_scene.cpp`: synthetic recording-RHI v2 payload, full actual pooled byte comparison, source revision invalidation, checked GPU-write rollback, lifetime across Model release.
- `268fcd5e06873ae73465927303964e44582c7f25` — same fixture: no additional GPU page on index allocation exhaustion; no corruption of a second ordinary pooled Model.
- `b108001f4abda9b003b03e693dc2fa38eb417df0` — `GlobalGeometryPool.cpp`: count actual model leases separately from sidecar leases, preserving meaningful `GeometryPoolStats::liveModels`.
- `83da15840341c1b2d02e78ec41afe32e5c8816aa` — test explicit independent source model and auxiliary span ownership counts.
- `71616a0b212c0aabcfbc376dcf1a83b71cc8e2a4` — Strata GPU header comment corrected to state v2 sidecar residency behavior.
- `4761639fd7f5acb08816b08556dd4665b607d439` — `docs/strata.md`: new shared-pool residency API, metadata v2, lifetime/budget/per-pass offsets, limitations and native checklist.
- `15548c847c33614b4d84cf22571794464b01103b` — `GlobalGeometryPool.cpp`: clarify last-owner teardown and completed-serial diagnostics.
- `1f2ecc55f7146df11e395cd1dbc93430a274d574` — `AGENTS.md`: required single-pool, v2 format and no-double-draw rules.
- `986d633056d293979ea145578cf4f8ab93c6f445` — `tests/render/strata_gpu_scene.cpp`: simulated frame serials confirm a released GPU index range is quarantined before completion and optionally reused only after the completion fence.

### Architecture and API contract
`GeometryPoolIndexSpan` is **move-only**, retains the existing `GeometryPoolPage` with shared ownership, stores the pooled GPU index-buffer handle, absolute uint32 `firstIndex` and `indexCount`, source Model pointer identity (not dereferenced), `geometryRevision`, GPU VB/IB and model base offsets. The source Model's original vertex buffer is used unchanged. `uploadAuxiliaryGeometryIndices` accepts only an already pooled, valid immutable model and source-relative indices, checks the page belongs to the same device, and writes into unused space between `vertexEnd` and `indexBegin`. It never allocates a new Strata page. Failed checked RHI uploads do not commit a sidecar lease or advance the page frontier. A full page rejects the sidecar and legacy rendering continues. Call `releaseAuxiliaryGeometryIndices(dev, span)` before Device shutdown; the sidecar may legitimately outlive its source Model lease, preventing premature destruction of the existing shared GPU page.

Only the pre-existing `AIVERSE_GEOMETRY_POOL_RECYCLE=1` opt-in permits future reuse of a retired index-only span; reuse additionally requires nonzero `completedFrameSerial >= retireFrame` from a healthy device. In absence of a trustworthy serial, released spans stay stranded until the GPU page itself is destroyed, intentionally favouring safety over memory reuse. Sidecar accounting is distinct from Model count; `GeometryPoolStats` has `auxiliaryIndexSpans`, `auxiliaryIndexBytes`, retired/reusable span counts and recycled auxiliary uploads.

`GpuMetadataScene` now stages a complete source-level concatenated Strata index stream on the original page (defaults: <=1 MiB per source, <=8 MiB per scene). If upload succeeds, v2 GPU metadata records each primitive's `gpuFirstIndex` as an **absolute pooled GPU index offset**. The node-level `sourceFirstIndex` remains local to that primitive; the future GPU command offset is **primitive.gpuFirstIndex + node.sourceFirstIndex**, with the original Model's `gpuBaseVertex` used for vertex addressing. The pure CPU `packGpuMetadata` still emits v1 (gpuFirstIndex invalid); this metadata cannot be submitted for drawing. The source page, sidecar and metadata have linked lifetime. `GpuMetadataScene::indexBinding` and `GeometryScene::strataGpuIndexBinding` return read-only handles/ranges only while Model identity, revision, shared allocation and GPU handles remain valid; errors preserve caller output.

`GeometryScene::setStrataStagingEnabled(true)` and `setStrataGpuMetadataEnabled(true)` must both be called before load (both default OFF). `GeometryScene` releases Strata GPU metadata and pooled index spans **before** releasing the owning legacy models. No shaders, per-instance GPU transforms, computation, occlusion/Hi-Z, draw commands, physics, ECS, save schema or genre-specific renderer code are changed. The existing `Renderer3D` and CPU/GPU batching paths remain the only draw authority.

### Actual validation (do NOT report PASS)
- Source changes and exact commits confirmed by connected GitHub writes. The batching agent's branch was **not** modified.
- **Native compile: NOT RUN. CTest (all Strata targets): NOT RUN. Recording fake RHI test: NOT RUN. Actual Metal/Vulkan execution, GPU fences, visual parity, memory/perf measurements: NOT RUN.**
- GitHub Actions for code head `1f2ecc55` were observed **queued**; not passing. Recheck current head after this handoff commit. Historical queued/cancelled validation is not proof of correctness.
- The recording-RHI test now includes a simulated monotonic `frameSerial` and `completedFrameSerial`. Its presence does not constitute execution.

### Immediate native verification
```sh
cd ~/Projects/AIVERSE
git fetch origin main perf/incremental-static-model-cache feature/strata-virtual-geometry
git worktree add --detach ../AIVERSE-strata-7b origin/feature/strata-virtual-geometry
cmake -S ../AIVERSE-strata-7b -B ../AIVERSE-strata-7b/build/strata \
  -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_STRATA=ON
cmake --build ../AIVERSE-strata-7b/build/strata \
  --target aiverse_strata_builder_tests aiverse_strata_model_adapter_tests \
  aiverse_strata_view_tests aiverse_strata_gpu_scene_tests -j 8
ctest --test-dir ../AIVERSE-strata-7b/build/strata \
  -R '^strata_(builder|model_adapter|view|gpu_scene)$' --output-on-failure
```

Then build actual runtime and exercise source sharing + OFF/ON pixel parity on Metal; perform Vulkan parity where supported, HLOD/shadow/reflection passes, memory churn, frame-serial fence/retire under real GPU load and multi-world unload before activating any Strata draws. Fix native compile/test issues and commit code + separate handoff on the **same branch**.

### Next bounded code step
After native builds/tests pass, add an **opt-in, mutually exclusive** Strata submission route in the existing `Renderer3D`, using selected cluster ranges and correct shared buffer + base-vertex mapping; never draw an original primitive twice. Implement pass-specific GPU scene instance records and compute frustum/LOD with CPU `planView` as oracle, then Hi-Z and async streaming only after measured parity and safe fences. Avoid changing existing GPU ICB defaults or duplicating `GlobalGeometryPool` / ECS transforms.

### Known risks
- The existing GPU pool still uses a bounded, page-local, first-fit free-gap allocator; a sidecar can fail even if other pages have available memory (no second page is allocated for this source). Retired index ranges may strand memory when recycling is OFF or serials unavailable, by design.
- The sidecar model lease is explicit and move-only, not a self-destroying GPU RAII object; callers MUST invoke release before Device teardown.
- Hierarchical LOD may still crack or alter attributes across cluster boundaries. No measured speedup, GPU mesh shading or GPU indirect draw has been proven.
- The active batching branch has advanced considerably during this task. Do not blindly overwrite its `GlobalGeometryPool.cpp` work; re-diff current batching head and reconcile it before a later merge. PR #35 remains stacked Draft on PR #34.


---

## Stage 8 — Metal diagnostic GPU cluster culling (2026-11-19)

### Resume state and precise commits
**Branch:** `feature/strata-virtual-geometry`. PR #35 Draft, stacked on `perf/incremental-static-model-cache` PR #34 (that separate branch was still evolving; do not overwrite its files). No merge to main. Main legacy renderer still owns ALL draws; Strata GPU selection is off by default.

- `8dfcf4f5479e4730e165ccad754c1e08f46a9007`: new `engine/render/StrataGpuCulling.h` with 80-byte `GpuSceneInstance`, 32-byte selected-range record, 16-byte atomic counters, 96-byte pass uniforms, per-frame submission descriptors, culling lifecycle.
- `8b6bf01418ed65f6d5f586cdd81eefad33125f2c`: new `engine/render/StrataGpuCulling.cpp` with experimental Metal compute shader, hierarchy traversal, parent-residency fallback, projected pixel error, frustum selection, atomic bounded output and no draw submission.
- `1b91431dcca08a5d44bf4f2c1be83920180d8465`: make GPU frustum near-plane clipping match the existing conservative `geometryBoxVisible` CPU reference (not a replacement for exact hardware clipping).
- `ab811a3de4c3a589904e563c4d1fa59cf42269e6` and `e987b734c12dd58a8ae7ec28cc5694ed81ed0764`: distinguish GPU `GpuPassConfig::viewProjection` from CPU `ViewConfig::objectToClip`; add `makeGpuSceneInstance` and `cpuReferenceView` utilities to prevent double-applying world transforms.
- `23fdb1fad2f9ecc5264004bc1bb2f28afd2dc3aa`: new `tests/render/strata_gpu_culling.cpp` with recording gfx::Device, matrix helper, viewport uniform, Metal-only gate, multi-dispatch same frame and no-draw assertions.
- `0d051e01b8e999857a9c375b46e05f7e7fa667db`: CMake static target `aiverse_strata_gpu_culling`, CTest `strata_gpu_culling`.
- `3cb86a52506336b59c949f46a943251447447d62`: conservative frame reuse; if previous 3-frame slot has no known completed GPU serial, GPU diagnostic selection declines rather than overwriting in-flight data.
- `2c5bc99a4478d7353ac28eed132d8756db3bf289`: new `.github/workflows/strata-focused.yml`, targeted Ubuntu/macOS compile and five Strata CPU/mock CTests.
- `b08a684f5e7ed99cc742aac7d8cfe1238474f73a`: `GeometryScene.h` diagnostics API: collect staged static instances and request opt-in GPU per-pass selection.
- `21730654aee6b9fb1433bf476c67a1178a2ae1b5`: `GeometryScene.h` stores borrowed owner gfx::Device pointer for dispatch identity check.
- `7e2e015254bb3a6e2dde11f45c728718ea31f801`: `GeometryScene.cpp`, collect source Model node/world transforms into GPU scene records; dispatch diagnostic compute if metadata v2 and all flags are opted in, no draw calls.
- `a6a9e109bdefb4a1ad28c263764ec1ddb5416816`: runtime links `aiverse_strata_gpu_culling` when `AIVERSE_BUILD_STRATA=ON`.
- `b137a9e229a16ca32f4aa16900db2659ef5b11ec`: fix `GeometryScene::shutdown` incomplete GPU class method calls for `AIVERSE_BUILD_STRATA=OFF` by guarding hardware-specific method references.
- `e7a08912d0b967f38b8154eb4baa9b8670640cae`: `docs/strata.md`, Stage 8 public contract, example, limitations, validation gates.
- `612acabb7e2c12245e48bd1db4c79dd9ecbf34e1`: `AGENTS.md` ownership, no-render rule, GPU matrix semantics, index base safety.

### Implemented and architecture
The GPU culling stage is **not yet production GPU-driven drawing**. `GpuClusterCulling::dispatch()` is a standalone Metal-only diagnostic compute call: it takes one `GpuMetadataScene` **version 2** storage buffer and an array of GPU scene instances for that source. It stages bounded instance matrices and a buffer of selected cluster ranges; the kernel traverses the encoded tree for each instance using a fixed 128-entry stack and chooses parent clusters when descendants are missing. The output is atomic-compacted, nondeterministically ordered, and reports overflow/invalid/traversal counters. Every range contains a valid-looking *candidate* absolute pooled index offset, but the `Renderer3D` batching/material/shadow pipeline is **never invoked by this module**. It never calls GPU indirect draw or reads GPU results back during normal rendering.

`GpuPassConfig::viewProjection` is pass-level ONLY; `GpuSceneInstance::objectToWorld` contains the source node transform (including instance and bind hierarchy). `cpuReferenceView(pass, instance)` composes these for the CPU `planView()` oracle. The GPU path is bounded to 4096 primitive instances, 65536 output clusters and 64 independent submissions per GPU frame slot. If a slot may still be used by the GPU and no completion serial proves retirement, dispatch returns false. Staging/culling are independent of game genres, ECS authoritative state, dynamic gameplay and other render passes.

`GeometryScene::setStrataGpuCullingEnabled(true)` is a **third explicit opt-in**, in addition to CPU staging and pooled GPU metadata/index staging. The scene can construct static instance+node transform records for a source with `collectStrataGpuInstances`, and trigger a compute pass with `dispatchStrataGpuView(device, sourceKey, pass, device.frameSerial(), result)`. This API is for use between beginFrame/endFrame and **outside** any render/compute pass. Its metadata must be owned by the same Device; the scene guards against a different device instance. All source models, materials, shadows, HLOD, GI, legacy batching, draw calls and GPU pool allocations remain managed by previous systems. No scene JSON or save format changes.

**Critical failure semantics:** a successful `dispatch()` means only that the RHI accepted compute encoding, not that visible ranges are correct or safe to submit. A nonzero GPU `GpuSelectionCounters::overflow` or `invalid` marks the entire result unusable for rendering. Atomic output order is unspecified; sort by `instanceToken/nodeIndex/primitiveIndex` for GPU vs CPU parity checks. Unlike CPU's global output budgeting, GPU overflow does not recover by selecting coarser parents: on overflow legacy rendering must remain the fallback. Never modify ordinary draw counts from this result.

### Actual validation status
- Connected GitHub API accepted each source and documentation commit and reported the feature HEAD.
- Native C++ compilation: **NOT RUN**. `strata_gpu_culling` recording RHI test: **NOT RUN**. Existing four Strata CTests: **NOT RUN** in assistant environment. Real Metal shader compilation and execution: **NOT RUN**. GPU async counters/readback parity with CPU: **NOT RUN**. Vulkan compute, Hi-Z, real A/B screenshots, frame-time benchmark: **NOT RUN**.
- GitHub Actions `Strata focused CPU and GPU-dispatch contracts` appeared **QUEUED** for code head `612acabb`; no PASS/FAIL result available. Other CI jobs also queued. Do not mark any unexecuted work green.
- Full GPU command submission remains purposefully unimplemented.

### Native validation required before further rendering
```sh
cd ~/Projects/AIVERSE
git fetch origin main perf/incremental-static-model-cache feature/strata-virtual-geometry
git worktree add --detach ../AIVERSE-strata-stage8 origin/feature/strata-virtual-geometry
cmake -S ../AIVERSE-strata-stage8 -B ../AIVERSE-strata-stage8/build/strata \
  -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_STRATA=ON
cmake --build ../AIVERSE-strata-stage8/build/strata \
  --target aiverse_strata_builder_tests aiverse_strata_model_adapter_tests \
    aiverse_strata_view_tests aiverse_strata_gpu_scene_tests \
    aiverse_strata_gpu_culling_tests -j 8
ctest --test-dir ../AIVERSE-strata-stage8/build/strata \
  -R '^strata_(builder|model_adapter|view|gpu_scene|gpu_culling)$' \
  --output-on-failure
```
Then explicitly test `AIVERSE_BUILD_STRATA=OFF` with the ordinary runtime target, compile the dynamic MSL kernel on actual Mac/Metal, dispatch it with real metadata v2 after a valid `beginFrame`, read back asynchronously when the GPU frame completes, check overflow and invalid counters, sort output and compare every actual selected range against `strata::planView` for varied camera projections, thresholds, LOD levels, node transforms, shadows and residency changes. Verify 4096-instance cap, 65536-output cap, same-frame multiple views, recycled frame slots, and that DrawCalls/draw-model frame images are unchanged with diagnostics OFF/ON.

### Next bounded step
If native tests and GPU shader parity are clean, implement a single mutually exclusive Strata/legacy per-primitive selection owner inside the existing renderer (not another batching/geometry pool), with explicit opt-in and direct fallback on any mismatch/error. Do not enable GPU compute-authored indirect draws or Hi-Z until the exclusive source submission path and pixel parity are verified. Streaming/dynamic TerraWeave and cross-platform Vulkan compute remain future stages.

### Live batching dependency
PR #34 has advanced independently. Its newer commits are **not automatically merged** into this Stage 8 snapshot. Fetch current batching HEAD, compare changed GPU renderer files and notes, resolve any conflicting semantics and preserve its handoff before syncing. Neither agent should force-push or merge to main without shared native regression results.


---

## Stage 8.1 — Mac Studio compilation fix in Strata GPU culling test (2026-11-19)

**Branch:** `feature/strata-virtual-geometry`; draft PR #35 stacked on batching PR #34.
**Implementation commit:** `d71aa7790ca74260b2280b0ddd440bc238acc3a8`.
**Changed file:** `tests/render/strata_gpu_culling.cpp` only. Replaced three invalid `compute.computeCalls` / `compute.dispatchCount` field accesses with `dev.computeCalls` / `dev.dispatchCount`. Counters belong to the `RecordingComputeDevice` fake RHI; `GpuClusterCulling` has no such public counters. No production GPU code, rendering logic, CMake, ABI or asset formats changed.

### Actual native evidence supplied by owner
The user compiled on a Mac Studio using AppleClang / MacOSX26.2 SDK from worktree `~/Projects/AIVERSE-strata-stage8`:
- `aiverse_strata_builder`, `aiverse_strata_model_adapter`, `aiverse_strata_gpu_scene`, `aiverse_strata_view` and `aiverse_strata_gpu_culling` library targets: **BUILT** according to owner-provided output.
- Test executables: `strata_builder` **PASS 0.83s**, `strata_model_adapter` **PASS 0.38s**, `strata_view` **PASS 0.39s**, `strata_gpu_scene` **PASS 0.46s** (owner-reported CTest log, previous source snapshot).
- `aiverse_strata_gpu_culling_tests` **failed to compile** from exactly three invalid field accesses on lines 129 and 151; `strata_gpu_culling` was **NOT RUN**, CTest reported 4/5 passed. This patch addresses those exact compiler diagnostics.
- **Post-fix native build / `strata_gpu_culling` CTest: NOT YET RUN.** No new Metal compute execution, shader compilation/parity or screenshot evidence. Do not convert owner-provided pre-fix passes into post-fix claims.

### Next exact verification action (existing detached worktree)
```sh
cd ~/Projects/AIVERSE-strata-stage8
git status --short
git fetch origin feature/strata-virtual-geometry
git switch --detach origin/feature/strata-virtual-geometry
cmake --build build/strata --target aiverse_strata_gpu_culling_tests -j 8
ctest --test-dir build/strata -R '^strata_(builder|model_adapter|view|gpu_scene|gpu_culling)$' --output-on-failure
```
Do not discard uncommitted work if `git status` is nonempty. On success, append the actual output and test commit/OS to a fresh handoff section; then address real Metal kernel compile and GPU-vs-CPU selection parity. Keep Strata diagnostics OFF by default and main unchanged.


---

## Stage 8.2 — owner 5/5 CPU CTests; diagnostic async GPU readback and real-Metal parity harness (2026-11-19)

### Branch, scope and test evidence
**Branch:** `feature/strata-virtual-geometry`, PR #35 Draft stacked on batching PR #34. Main unchanged.

The user ran Stage 8.1 **before** the new Stage 8.2 implementation on Mac Studio at `~/Projects/AIVERSE-strata-stage8`. The native build produced `aiverse_strata_gpu_culling_tests` and CTest reported **100% tests passed, 5/5, 0 failed, 1.19 s**:
- `strata_builder` PASS 0.17 s
- `strata_model_adapter` PASS 0.17 s
- `strata_view` PASS 0.16 s
- `strata_gpu_scene` PASS 0.20 s
- `strata_gpu_culling` PASS 0.48 s

This is **real user-reported Mac native compilation / CPU + recording-RHI tests** for the older code, not a Metal compute parity pass or execution of this new Stage 8.2 code.

### Implementation commits
- `36636cbb1c06ca6fcd446922766be308ad04d82f`: `engine/render/StrataGpuCulling.h` adds `GpuSelectionReadback`, explicit statuses `Valid/Overflow/Invalid/ReadbackFailed`, asynchronous readback callback API, sorted CPU-vs-GPU parity oracle.
- `cb7d0a40a77be93033b05da6a089bc68850c6d6a`: `engine/render/StrataGpuCulling.cpp` implements two independent RHI asynchronous reads (counters and ranges) with owner-thread completion, fail-closed invalid and overflow, 2048-record/65536-byte Metal limit and `compareGpuSelection` sorted exact CPU oracle.
- `9716fee0e64089210d0d5dee3a8d08d24d58b248`: `tests/render/strata_gpu_culling.cpp` extends fake RHI for async callback sequencing, synthetic GPU selected records, GPU/CPU comparison, rejected overflow and oversized readback.
- `cab54aa847d5819430610aa95da00252396d0b08`: `tests/render/strata_metal_parity.cpp` new real Metal/AppKit platform fixture. Uses pooled source Model + metadata v2, GPU compute dispatch and asynchronous readback; checks five scenarios: two instances/fine LOD, coarse parent LOD, frustum rejection, disabled instance, capacity overflow. Never submits renderer draws. Fixture fails on MSL compile errors or GPU/CPU mismatch.
- `e10d0b3a37ed4037ed7872d845a5c02813879dd8`: `CMakeLists.txt` optional macOS target `aiverse_strata_metal_parity` gated by `AIVERSE_STRATA_METAL_GPU_TESTS=ON` (default OFF; intentionally not an unattended CTest because it opens a native Metal window).
- `7221b7fa9dd59346b731e8636c93d2fa342be247`: `engine/render/GeometryScene.h` additive async readback API.
- `52d4941f0008b18b939008eb6ad3588bf35a4442`: `engine/render/GeometryScene.cpp` delegates readback to scene-owned culler with gfx::Device identity/lifetime and opt-in gating. Compile-time Strata-OFF path is no-op.
- `210de63271823291fd4fe23c1ec6beaaad09fc69`: real Metal fixture stores a validated pooled GPU sidecar index base during setup, avoiding late mutable lookups in callbacks.
- `77002ea0473c329d417e5a1b4c6a36f4591beeef`: `docs/strata.md` readback contract, real Metal testing instructions, validation distinctions.
- `fc28f78d772835882beaac8a6346e030eaedf036`: `AGENTS.md` stages and prior owner-confirmed pass.
- `355044339ff142188a67619627e28a29bfbb9b3b`: `tests/render/strata_gpu_culling.cpp` additionally rejects nonzero `GpuSelectionCounters::invalid` with a complete-looking GPU prefix.

### API and correctness
`GpuClusterCulling::requestReadback(dev, selection, cb)` must run **during the same GPU frame and outside Metal encoders**, after a successful diagnostic `dispatch()`. It issues two `gfx::Device::readBufferAsync` copies. Metal supports at most 65536 bytes per request, so `GpuSelectionSubmission.capacity > 2048` fails immediately and reports `ReadbackFailed` to callback. GPU callbacks are delivered at a later `Device::beginFrame` on the device owner's thread. The shared callback state waits for both reads; malformed/incomplete, overflow or invalid counters do not publish drawable ranges. A scheduling failure reports failure once. Diagnostic only, no renderer submission.

`compareGpuSelection(source, instances, pass, pooledFirstIndex, gpu, error)` independently invokes `planView` for each enabled instance with `cpuReferenceView`, computes absolute pooled offsets from source primitive concatenation, translates local node IDs to global metadata IDs, sorts both CPU and atomic-order GPU output by full record keys and requires exact match. Duplicate enabled instance tokens, malformed source, GPU invalid/overflow or CPU budget fallback are rejected. It is a correctness ORACLE, not a production draw backend.

Real Metal fixture exercises `platform::run` and `gfx::createMetalDevice` through the existing engine runtime, no second RHI implementation. It enables only the pre-existing **CPU MDI admission** environment switch `AIVERSE_METAL_CPU_MDI=1` so its source can enter the shared `GlobalGeometryPool`; GPU-authored MDI/ICB and Strata renderer submission are NOT enabled. The fixture receives actual GPU results asynchronously, verifies expected LOD/frustum counts and CPU parity, and exits with failure on any issue. `GeometryScene` has a public readback convenience method for other diagnostic hosts; they MUST manage callback lifetime across scene unload.

### Validation as of this handoff
- **Previous Stage 8 Mac native focused tests: PASS 5/5** (user-supplied logs above).
- Stage 8.2 native compilation and all new fake RHI checks: **NOT RUN**.
- Actual Metal MSL compile, GPU dispatch, async readback and CPU/GPU parity: **NOT RUN**.
- GPU raster screenshots, no-double-render proof, Vulkan and performance benchmarks: **NOT RUN**.
- GitHub Actions was observed **QUEUED/PENDING** on Stage 8.2 intermediate commits; queued is not a passing result.

### Exact next commands (existing worktree on Mac)
```sh
cd ~/Projects/AIVERSE-strata-stage8
git status --short
git fetch origin feature/strata-virtual-geometry
# Only if your worktree has no unsaved changes:
git switch --detach origin/feature/strata-virtual-geometry
cmake -S . -B build/strata -DAIVERSE_BUILD_TESTS=ON \
  -DAIVERSE_BUILD_STRATA=ON -DAIVERSE_STRATA_METAL_GPU_TESTS=ON
cmake --build build/strata --target aiverse_strata_gpu_culling_tests \
  aiverse_strata_metal_parity --parallel 8
ctest --test-dir build/strata \
  -R '^strata_(builder|model_adapter|view|gpu_scene|gpu_culling)$' \
  --output-on-failure
build/strata/aiverse_strata_metal_parity
```

On any compiler error, shader pipeline error or failed GPU/CPU comparison, save the exact output, update the implementation and append a **new handoff section with actual results**. Inspect Metal shader compiler diagnostics, frame serials and GPU counters first. The real fixture must print five PASS scenario lines and `cases=5/5 status=PASSED` before claiming stage 8.2 GPU parity. Those are future required results, NOT current achievements. Preserve diagnostic-only mode and the legacy Renderer3D draw owner. Next work after real parity: randomized passes/transforms and conservative CPU/GPU pixel parity before mutually exclusive submission and Hi-Z.


---

## Stage 9 — exclusive renderer source routing preflight (2026-11-19)

### Current branch and upstream integration
**Working branch:** `feature/strata-virtual-geometry`; PR #35 remains Draft. PR #34 for `perf/incremental-static-model-cache` was merged into `main` at commit `58e29ab052458fffcdebebffe27022b53adb9367`. This Strata branch includes the new `main` using a **two-parent merge** `a77cd830e8dee751658028786066111bc8e65f6a` while keeping all Strata files and latest batching changes (`GfxMetal.mm`, `Renderer3DModels.cpp`, `AutoCpuBatchPolicy.h`, material batching fixture and batching handoff/script). No changes were made to the batching branch or main by Strata. PR base should now be `main`, not the already merged batching feature branch.

### Exact implementation commits
- `a77cd830e8dee751658028786066111bc8e65f6a`: actual merge from current batching `main`.
- `2930878b938b4a30743f94cf8f1af5e203ce3491`: new `engine/render/StrataRenderRouting.h`, exclusive source route decision, per-pass geometry-only candidates and common indexed indirect ABI.
- `8cd26285c19863a0fd9c6a8695291d9b4e01a85b`: new `engine/render/StrataRenderRouting.cpp`, strict active-source preflight, CPU `planView()` selection and pooled sidecar index translation; no draw calls.
- `0c3ead9339db925877f7444d8b0e0f71742e9a0e` / `e70fa64ad77589aba7efaaef625e36c9ebb320bc`: tie a command candidate to the exact Model identity, geometry/material revisions and pooled buffer handles at encoding time.
- `63632145689003fdef185a7ff66dc49a8912b7d4`: decline existing material and shadow batching ownership, in addition to skinned/morphed, alpha, vertex displacement, variants, material overrides and authored LOD.
- `76fb03c2ee1859586392abc14be8b850e1affbfd`: new `tests/render/strata_render_routing.cpp` recording-RHI route preflight fixture.
- `c86fe0110c8ad835954813d4c1ec96c8006a500c`: new CMake library `aiverse_strata_render_routing`, test executable `aiverse_strata_render_routing_tests`, CTest `strata_render_routing`; runtime links the preflight library only when Strata is compiled.
- `94456211a4eac216c0594bb3a0047f19f1d9fade` / `cbc65002a1ae99f7c4ba19cf215b161acd2ffeef`: additive `GeometryScene::prepareStrataRenderCandidate` API; reads scene-owned source and staged metadata but does not alter `GeometryScene::draw`.
- `a8d0a3b1a969fec2797906e0005d9522314ca72e` / `82e7cea293b2c021ae98dd6fff1015c1a0a98cb7`: always check a **fresh live `GpuMetadataScene::indexBinding`** during command encoding, preventing reuse after GPU sidecar retirement. Re-check live material/geometry compatibility even when a caller forgot to mark a material revision.
- `df20b874f99c04780d2c8cf1b703dc6ebaa753eb`: extend recording RHI tests for live lease checks.
- `4a4462001ae4ee2e11f453caa0665dd186e973a7`: update `.github/workflows/strata-focused.yml` to compile/test all six focused Strata targets.
- `f0272ecdf1b0f05b31a265571880c60a3498d77e`: regression cases for missing sidecar, changing a material alpha without a revision increment, and existing shadow-batch ownership.
- `54fb33b73d601762bffdccc320abeb604a20d3f9`: `docs/strata.md` stage 9 API, gated route and no-live-draw policy.
- `ee9cd11e116c3540bf5df5008bf678e7783df739`: root `AGENTS.md` ownership notes, no-double-render guard and merged batching provenance.

### Architecture/API contract
`strata::prepareRenderCandidate(const Model&, const ModelAsset&, const GpuMetadataScene&, sourceKey, mesh, primitive, pass, ViewConfig, RenderCandidate&, error)` requires a **currently live** source model, matching per-source metadata v2 and existing `GlobalGeometryPool` auxiliary index lease, rigid nonanimated/nondeformable object, opaque compatible material, no material batching, variant, explicit authored LOD, or shadow batch that would duplicate submission. It calls per-pass CPU `planView` and rejects budget fallbacks not representable by the GPU output. The candidate includes actual `gfx::BufferHandle` identity and ranges with **absolute pooled GPU index offsets**, original pooled base vertex, source geometry and material revision. Zero ranges after conservative frustum culling is a legitimate Strata-only skip, not permission to also draw the legacy primitive. It never submits anything.

`strata::RenderRouteGate` has six explicit opt-in conditions: requested, source/material safety, source residency, CPU/GPU parity, raster parity and Renderer3D submission ownership. `chooseExclusiveRenderRoute` defaults to `Legacy` unless **all six** are true. There is no supported path for simultaneously drawing Strata and legacy for the same primitive/pass. In production, these bits MUST come from proven actual tests and renderer control flow; simply setting them to true is not validation.

`encodeRendererCommands(liveModel, liveGpuMetadataScene, candidate, rendererInstanceSlot, gate, output)` requires a real renderer-owned per-draw uniform slot. It rechecks live Model identity/revisions/vertex+index handles and **fresh** source sidecar binding before preparing shared 20-byte `gfx::DrawIndexedIndirectCommand` records. Every selected range must be inside the active GPU lease, triangle aligned and have the correct material/base vertex. It is transactional: error preserves caller output. This only *constructs* command bytes and never calls `drawIndexed` / `drawIndexedIndirect`, sets pipeline/uniforms, or suppresses legacy ModelDraw. Renderer3D still owns all renders, material grouping, shadows, GI and CPU/GPU batching. No GPU culling was made authoritative.

`GeometryScene::prepareStrataRenderCandidate` exposes the existing staged source to this helper behind explicit CPU/GPU Strata staging. The caller supplies the exact pass-specific object-to-clip matrix including the node transform. Do not use a shared main-camera selection for sun shadows, reflection, portal or stereo passes. The preflight is per primitive and per pass; use `Legacy` on unsupported backend/material/model or missing/off GPU residency.

### Actual test evidence (not inferred)
- **Owner-provided Mac Studio result from earlier Stage 8.1:** 5/5 focused CPU and recording-RHI CTests PASS in 1.19 s. This remains valid only for that prior code snapshot.
- New Stage 8.2 Metal async readback, actual real-Metal parity runner and **Stage 9 route preflight**: native build/CTest **NOT RUN in this session**. No actual GPU/CPU parity or screenshot validation has been furnished. Do not claim Stage 9 tests PASS because a fake RHI fixture was added.
- GitHub Actions for intermediate current-feature commits are queued/pending, not yet a verified PASS. The assistant has no AIVERSE checkout or macOS Metal GPU execution capability.

### Immediate native commands (existing Mac Studio worktree)
```sh
cd ~/Projects/AIVERSE-strata-stage8
git status --short
git fetch origin main feature/strata-virtual-geometry
# Only when status shows no uncommitted work:
git switch --detach origin/feature/strata-virtual-geometry
cmake -S . -B build/strata \
  -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_STRATA=ON \
  -DAIVERSE_STRATA_METAL_GPU_TESTS=ON
cmake --build build/strata --target \
  aiverse_strata_render_routing_tests aiverse_strata_gpu_culling_tests \
  aiverse_strata_metal_parity --parallel 8
ctest --test-dir build/strata \
  -R '^strata_(builder|model_adapter|view|gpu_scene|gpu_culling|render_routing)$' \
  --output-on-failure
build/strata/aiverse_strata_metal_parity
```
The real Metal fixture MUST actually print five scenario PASS lines and `cases=5/5 status=PASSED` before GPU selection parity is declared. Compile and test `AIVERSE_BUILD_STRATA=OFF` as a separate portability check; this branch is otherwise experimental and default-off for all runtime Stage 7+ flags.

### Next bounded step for a subsequent agent
1. Fix any native compiler/CTest errors reported for Stage 8.2/9 with a code commit, then append a separate handoff including the actual logs.
2. Run and validate the real Metal GPU `strata_metal_parity` fixture (not just the fake RHI tests), then expand to random camera/LOD thresholds, scene node transforms, materials, shadow pass and multiple instances. Record exact compared counters/ranges and GPU timeline if available.
3. Once *real GPU-vs-CPU* and raster parity are independently confirmed, add a single exclusive Strata route inside existing `Renderer3D::renderModels` / `drawGeometryBatch`, not another renderer. Per-primitive visibility, material state, instance uniforms and material/shadow batch roles must be owned by the existing pipeline. Direct fallback on any error or unsupported pass. Never submit both old and Strata indexed geometry in one pass.
4. Only afterward implement compute-to-ICB indirect submission, Hi-Z and streaming; preserve GLES/WebGL and non-Metal legacy behavior.

### Risk register
- Stage 9's command output is **preflight**, not rendering. Boolean route gates are declarations by a future renderer caller, not evidence from GPU.
- Metal GPU buffer/metadata v2 must stay alive through all GPU frames that reference any selected index span, including async diagnostic readback.
- Auxiliary Strata index ranges live on the existing model GPU page; never copy a stale candidate after device, source or material revision change.
- CPU LOD simplification may have visual seams/cracks under different depths; texture parity remains unverified.
- PR #35 remains Draft. The batching PR #34 was already merged; rebase the PR base onto `main` after this exact upstream integration and continue writing per-stage handoffs.

---

## Stage 10 — Renderer3D ModelDraw dry-run preview, legacy remains authoritative (2026-11-19)

### Branch and exact implementation commits
Branch `feature/strata-virtual-geometry`, draft PR #35; base `main`, current integrated batching implementation already merged. **NO Strata rendering enabled; NO main mutation in this stage.**

- `54be3a2f0d3f55fb89872fdbc1963938eb912b44` — `engine/render/Renderer3D.h`: additive `previewStrataModel` entry point and `StrataPreviewStats` counters.
- `ae151e01734e0bd6b9f25128cb6c87e572281c5c` — `engine/render/Renderer3D.cpp`: reset diagnostic counters at `beginScene`.
- `99a2d0a4f117c539dfcab2cf71b5d86e7e782bc0` — initial `engine/render/StrataRendererPreview.cpp` renderer-side main/shadow route evaluator.
- `f9f9331b3400bc63c65a9108f2bd125030194199` — `CMakeLists.txt`: compile preview source as part of `aiverse_strata_render_routing` library, no separate renderer or new GPU objects.
- `513f846d8c5ac900f770d08b5b0845149343fc75` — `engine/render/GeometryScene.h`: `setStrataRenderPreviewEnabled(bool)` (default false) and query.
- `f6a2d29ffd2fad060519ffcaea623764426b9891` — `engine/render/GeometryScene.cpp`: optional synchronous call to renderer preview for original source assets, ALWAYS followed by existing `r.drawModel(d)`; never preview proxy/HLOD model replacements.
- `b90bf789544917cb54d07065e56ef7c027e0ab90` — `engine/render/StrataRendererPreview.h`: stateless `previewModelDraw`, per-pass `PreviewContext` and diagnostic `PreviewCounts` API.
- `1fc6bc3e88cc7d1852180a645c037ae39b7b7cbf` — refactor preview implementation to call the stateless evaluator from `Renderer3D::previewStrataModel`, retaining only per-frame stats in renderer.
- `c8bdc0f3aa62fcb962379fa5fa645d951e9eb163` — `tests/render/strata_render_routing.cpp`: test main/shadow readiness and cluster counts, reject alpha/tint and assert recording RHI emitted ZERO draw calls.
- `8272caa80ce601ef4c156a29160d3bf7d8fc7d45` — `docs/strata.md`: explain preview API, source/proxy exclusion, no-double-render rule and counters.
- `aedc9eda39a853b65385edc1cf169f98a43b9835` and `c0f892f7dc71460c75184eccb607040d7c91e965` — root `AGENTS.md` ownership and default-off feature flag note.

### Functionality / ownership
`GeometryScene::setStrataRenderPreviewEnabled(true)` is an **additional native opt-in**. It only activates on the already enabled CPU staging and source-resident GPU metadata path. During `GeometryScene::drawInstance`, if the legacy model is still the unmodified source (not a substituted surface proxy or HLOD proxy), the scene looks up the staged `strata::ModelAsset` and pooled GPU index lease and synchronously invokes `Renderer3D::previewStrataModel` prior to the untouched `r.drawModel(d)`. The preview never stores a source key or pointer beyond the call and never creates new GPU resources. If no source is eligible, the legacy draw still executes exactly as before.

The renderer-side public `strata::previewModelDraw(draw, staged, residency, key, context)` uses the actual ModelDraw (model world matrix, bind nodes, runtime material/alpha/flags and absence of animation/overrides). It calculates separate per-pass `objectToClip` from main and shadow projection, checks each eligible mesh primitive using the existing `prepareRenderCandidate` Stage 9 validation, and returns purely diagnostic counts (attempted, ready, rejected, selectedClusters, fullyCulled, skippedUnsafeDraws). The sun pass is only previewed when renderer sun shadow quality is not Off. The renderer's counters reset every `beginScene` and have **no effect on GPU draw commands, material grouping, HLOD, shadows, occlusion or existing CPU/GPU batching**.

Critical no-double-submit rule: `GeometryScene::drawInstance` **always** invokes the original `Renderer3D::drawModel(d)` after preview. Stage 10 does not call `encodeRendererCommands`, set uniforms/pipelines or submit even one Strata indexed draw. A `ready` count only means CPU reference and residency accepted the candidate; it is not proof of Metal compute parity, crack-free geometry or correct raster output. Under AIVERSE_BUILD_STRATA=OFF all preview call sites remain behind the compile-time feature macro; the old runtime still works in principle but must be compiled to confirm.

### Tests and verification — actual status
- User-provided earlier Stage 8.1 Mac Studio: all five older focused CPU + recording-RHI CTests **PASS (5/5, 1.19 seconds)**.
- **Stage 8.2 newer real Metal parity, Stage 9 six-test suite, and Stage 10 preview changes: NATIVE BUILD / CTest NOT RUN in this chat.** New code was committed through GitHub API, which does not compile C++. No actual real Metal shader parity, screenshot parity or performance measurement exists here.
- GitHub focused workflow on the current Stage 10 commits was observed **QUEUED**, not green; other CI jobs similarly queued.
- The `strata_render_routing` recording-RHI regression has Stage 10 assertions for two independent view passes, no new draws and alpha/tint exclusion; presence of these assertions does not make them passing tests.

### Immediate commands on existing Mac Studio worktree
```sh
cd ~/Projects/AIVERSE-strata-stage8
git status --short
git fetch origin main feature/strata-virtual-geometry
# Switch only if status clean or after preserving local edits:
git switch --detach origin/feature/strata-virtual-geometry
cmake -S . -B build/strata \
  -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_STRATA=ON \
  -DAIVERSE_STRATA_METAL_GPU_TESTS=ON
cmake --build build/strata --target \
  aiverse_strata_render_routing_tests \
  aiverse_strata_gpu_culling_tests \
  aiverse_strata_metal_parity --parallel 8
ctest --test-dir build/strata \
  -R '^strata_(builder|model_adapter|view|gpu_scene|gpu_culling|render_routing)$' \
  --output-on-failure
build/strata/aiverse_strata_metal_parity
```

Also run `AIVERSE_BUILD_STRATA=OFF` compile of `aiverse_runtime` (or a real client/editor target) and the main scene drawing fixture with the preview toggle OFF/ON once GPU parity is confirmed. Check for high preview overhead on large source scene counts: preview is only opt-in and builds CPU reference plans per draw, not a production hot-loop option.

### Next step for a new agent
1. **Before implementing any GPU draw path**, get actual native build/test results for Stage 8.2, 9 and 10. Fix compiler or shader errors; commit code and append a separate handoff with real logs.
2. Run the real Metal `aiverse_strata_metal_parity` fixture; collect exact GPU counters/ranges and sort against CPU oracle, including per-node transforms, multiple material partitions, 0/1 clip convention, foliage/terrain excluded cases, and missing residency. Success from recording RHI does not establish actual GPU correctness.
3. Add a full `GeometryScene` fixture with recording gfx::Device and two source instances, inspect model source/proxy selections, verify legacy draw counts and command bytes remain identical with preview OFF and ON, and measure preview CPU cost. Ensure pointer lifetimes during unload/reload.
4. **Only after** real Metal and visual parity: implement a single mutually exclusive cluster/legacy primitive route under existing `Renderer3D::renderModels` / `drawGeometryBatch`, preserving material/pipeline/shadow/instance ownership. No double submission and no second geometry pool. Preserve non-Metal fallback; then GPU-authored indirect submission, Hi-Z and streaming.

### Known risks
- Diagnostic preview currently loops each eligible node/primitive and may allocate/validate CPU hierarchy frequently. It is intentionally default OFF and unsuitable for always-on production until caching/versioning is added.
- Actual GPU cluster selection output has nondeterministic atomic order; compare sorted records before allowing any renderer routing.
- Source models with old `Model::geometry` GeometryCluster preparation are rejected by shared-pool admission. Do not bypass this without designing a single owner for legacy cluster buffers and Strata sidecars.
- Source proxy/HLOD substitutions are not previewed; legacy source draws remain authoritative. Scene JSON/asset formats are unchanged.


---

## Stage 10 verification update — native focused CTest 6/6 PASS (2026-11-19)

**User-supplied Mac Studio output** from `~/Projects/AIVERSE-strata-validation/build/strata` reports successful native execution of all **six** focused Strata CTests, with **100% tests passed, 0 tests failed**:

- `strata_builder` — PASS, 0.36 s
- `strata_model_adapter` — PASS, 0.59 s
- `strata_view` — PASS, 0.65 s
- `strata_gpu_scene` — PASS, 0.38 s
- `strata_gpu_culling` — PASS, 0.17 s
- `strata_render_routing` — PASS, 0.20 s

These results supersede the preceding 'not run' statements **for these focused native tests**. The exact checkout HEAD used for the native commands is not printed in the log; the report was provided after the stage-10 branch commands. Do not falsely claim a verified specific commit SHA if that was not printed.

**Still NOT verified:** `aiverse_strata_metal_parity` real Metal runtime test, shader compilation/execution on the actual device, async GPU range readback, GPU-vs-CPU parity on hardware, pixel/shadow/reflection screenshot parity, production Strata draw submission, GPU performance and memory timings. The renderer remains legacy-authoritative, with diagnostic dry-run opt-in. No change to main.

### Immediate next verification
From the existing `~/Projects/AIVERSE-strata-validation` worktree, first confirm the real-Metal executable exists; if it does not, configure with `-DAIVERSE_STRATA_METAL_GPU_TESTS=ON` and build target `aiverse_strata_metal_parity`. Run it outside CTest because it opens a macOS window. Record all five scenario outcomes and the final `cases=5/5 status=PASSED` line only if observed. Preserve any failing Metal compiler or GPU error verbatim for the next implementation patch.


---

## Stage 10 verification update — real Mac Studio Metal GPU parity PASS (2026-11-19)

**Source:** user-supplied terminal output from
`~/Projects/AIVERSE-strata-validation/build/strata`.
On the then-current `feature/strata-virtual-geometry` worktree,
CMake configured and built `aiverse_strata_metal_parity` successfully
(including `aiverse_strata_gpu_culling`, `aiverse_strata_render_routing`,
and `aiverse_runtime`). The runner compiled a real Metal compute
pipeline, dispatched GPU work, asynchronously read GPU-selected cluster
ranges/counters and compared against CPU `planView()`. The **observed**
results are:

| Actual real-Metal GPU fixture | Result |
| --- | --- |
| `fine-LOD-two-instances` | **PASS**, selected=4, examined=6 |
| `coarse-LOD` | **PASS**, selected=2, examined=2 |
| `frustum-rejection` | **PASS**, selected=0, examined=6 |
| `disabled-instance` | **PASS**, selected=2, examined=3 |
| `bounded-output` | **PASS**, overflow correctly rejected |
| Final | **`cases=5/5 status=PASSED`** |

The runner also printed
`[metal-assets] libraries=0 sourceMisses=0 pipelines=0 pipelineFailures=0 archive=0 strict=0`;
this is not itself a measure of GPU performance or a renderer screenshot
comparison. In the same conversation, the owner separately supplied
all six focused native Strata CPU/recording-RHI CTests **PASS (6/6)**
on Mac Studio.

**Scope of proof:** Real Metal GPU compute creation, dispatch,
asynchronous readback, basic LOD/frustum/disabled-instance selection,
and overflow reporting match the CPU oracle for the five fixture
cases. This does **NOT** establish pixel/shadow/reflection/transparent
raster parity, arbitrary models, randomized transforms, mesh seams,
stable performance, streaming or an exclusive live renderer route.
Stage 10 `Renderer3D` is still only dry-run; default legacy drawing
and all opt-in safety boundaries remain unchanged.

**Separate unrelated test results:**
The user also supplied `AIVERSE-static-scene` aggregate CTest
results `125/127`, with `skill_catalog` and `skill_routing`
failing on:
`unregistered or missing skills:
['skills/gameplay/aiverse-ecs-shared-static/SKILL.md']`.
This failure occurred in a **different worktree/feature**; it is not a
Strata GPU failure. The correct owner of the static-scene/skills
manifest must reconcile its skill registration and handoff, not
silently modify Strata or attribute 127-test results to Strata.

**Next explicit gate:** Add a real Metal raster fixture to render
Strata-selected source ranges through the existing `Renderer3D`
material/shadow state under a mutually exclusive scoped feature flag.
Compare before/after screenshots bytewise or with documented
per-channel tolerance, test main view, sun shadows and reflections,
and verify the same source primitive is submitted **once per pass**.
Reject unsupported material/LOD/buffer conditions and route to legacy.
Only once this is proven should Strata be allowed to drive production
indexed draws or GPU-encoded indirect commands. Preserve one shared
GPU geometry pool and the original staging/ownership model.

The exact output did not print the `git rev-parse HEAD` of the tested
checkout. The user had previously been instructed to fetch and switch
to the branch, but **do not falsely claim an SHA-level GPU PASS**
without that SHA being observed in an owner-provided log.


---

## Stage 11 — real Metal Renderer3D raster Legacy/Strata pixel-parity fixture (2026-11-19)

### Current branch and exact commits
**Branch** `feature/strata-virtual-geometry`; PR #35 remains Draft with base `main`.
Main and unrelated agents' branches are untouched. This stage adds a
**manual graphical regression fixture only**. No new production
`Renderer3D` draw route or Hi-Z GPU occlusion path is enabled.

Stage 11 implementation commits:
- `67ce7c346030cb002f400a78bc386b72974f3775` — new
  `tests/render/strata_metal_raster_parity.cpp` with real Metal,
  a source model + a lossless parent-and-two-leaf Strata asset,
  shared GPU-pool source VB/IB, sidecar v2 indexed metadata and
  raster-only alias Models sharing exactly the same pooled GPU
  buffer. It renders empty, legacy, fine, coarse in **separate frames**
  through the existing `Renderer3D::drawModel` and includes an
  actual shadow-receiver floor and main/sun shadow pass counters.
- `f5b9bab91810cf815478aed24609c0a7add5dba5` —
  `CMakeLists.txt` adds opt-in macOS-only target
  `aiverse_strata_metal_raster_parity` under the existing
  `AIVERSE_STRATA_METAL_GPU_TESTS=ON` flag.
- `a6b72b498f9b34d88e60fc5d307471e098744796` and
  `b6979f9bb364bfbfc7b63d881c8dbc2b5fc9e995` —
  write four lossless RGB PPM captures with proper newline headers
  (using `AIVERSE_STRATA_RASTER_CAPTURE_PREFIX` for output location).
- `6a8a7ec2a325896d12ecba8bbb338d9ccd1c9ab8` —
  `.github/workflows/strata-focused.yml`: fixed a discovered
  malformed/broken six-test CTest regex and duplicated
  `--output-on-failure`; ensures the native macOS runner builds
  **both** real Metal fixture executables, but does not launch
  AppKit windows in headless CI.
- `8c3a3163029268c4e66a3957d1501c02321a97c4` —
  `docs/strata.md` records Stage 11 fixture design, capture paths,
  limitations, safety invariants and full native commands.
- `e10d67be543fade673a18ab7cc941bdb1fd6c737` —
  `AGENTS.md` Stage 11 contract and handoff requirement.
- `8d7a8f44b1d34f5625e784b5273b462520bd4114` —
  enable Renderer3D profiling in the fixture so actual
  main/shadow pass draw counts are inspected.

### Architecture / APIs / no double-rendering
The fixture uses `gfx::Device` Metal via the regular
`platform::run` window and the production
`Renderer3D::beginScene/drawModel/render` pipeline. The CPU Model is
uploaded **once** to the existing `GlobalGeometryPool`;
`GpuMetadataScene::stage` separately uploads only Strata's
auxiliary indices into that same source page and records absolute
GPU first-index offsets. Fine and coarse alias Models are never
uploaded or released: they only borrow `source.vb/ib`, same
`source.gpuBaseVertex`, and set `gpuFirstIndex` to the
source's **v2 Strata sidecar firstIndex**. The models differ only in
primitive firstIndex/indexCount and use identical original
source vertex attributes, normals, UVs, material and triangle winding.
The parent is intentionally lossless (both legacy triangles);
the two leaves partition exactly those same triangles. This is an
index-addressing, material/shader/renderer-routing pixel parity test,
**not** evidence of real mesh simplification visual quality.

The test validates selected range offsets via CPU
`strata::prepareRenderCandidate` for current main view before
rasterizing. Original Legacy and both Strata aliases each draw via
the SAME `Renderer3D::drawModel` method in **different captured
frames**, with one explicit ModelDraw per candidate frame; it does
not submit the legacy source and selected Strata alias together.
The floor is a constant ordinary `MeshBuilder` draw in every frame,
serving as a shadow receiver; high sun shadow quality is enabled.
Screenshot capture is `gfx::Device::captureNextFrame`. A blank
capture must differ from the Legacy capture, preventing a false
positive from uniformly blank output. Fine/coarse image comparisons
are **byte exact**, reporting changed-channel counts. A mismatch
fails the executable and writes images for inspection.

The fixture checks nonzero renderer shadow and opaque pass draw
counts for all three model cases, and that each candidate frame
received one ModelDraw. It does not assert the *raw shadow depth
texture bytes*, does not drive indirect/ICB submissions from
`GpuSelectedCluster`, and does not test water reflection/special
material passes. Existing production Strata preview and routing
gate remain default-OFF and **do not suppress any original
ModelDraw**.

### Actual validation status
- **Previously verified, owner-supplied Mac Studio**:
  six Strata CPU/recording-RHI CTests **PASS 6/6**;
  real Metal GPU selection/async readback oracle **PASS 5/5**.
- **Stage 11 new raster C++ compilation: NOT RUN in assistant
  environment. Real Metal Renderer3D pixel comparison: NOT RUN.**
  A committed test is not a passing test. CI for new Stage 11
  commits must be checked for an actual conclusion; a queued or
  pending workflow is not green.
- Real GPU performance and photorealistic/simplified-LOD,
  sun-shadow depth texture, reflection, mixed material and
  Vulkan/other-backend raster parity: **NOT RUN**.
- Unrelated `AIVERSE-static-scene` skill registration reruns were
  owner-confirmed PASS 2/2, separate from this Strata stage.

### Immediate Mac Studio test commands
Run in the owner's existing worktree, taking care not to destroy
local changes:
```sh
cd ~/Projects/AIVERSE-strata-validation
git status --short
git fetch origin feature/strata-virtual-geometry
# Only if no unsaved working-tree changes:
git switch --detach origin/feature/strata-virtual-geometry
cmake -S . -B build/strata \
  -DAIVERSE_BUILD_TESTS=ON \
  -DAIVERSE_BUILD_STRATA=ON \
  -DAIVERSE_STRATA_METAL_GPU_TESTS=ON
cmake --build build/strata --target \
  aiverse_strata_builder_tests \
  aiverse_strata_model_adapter_tests \
  aiverse_strata_view_tests \
  aiverse_strata_gpu_scene_tests \
  aiverse_strata_gpu_culling_tests \
  aiverse_strata_render_routing_tests \
  aiverse_strata_metal_parity \
  aiverse_strata_metal_raster_parity --parallel 8
ctest --test-dir build/strata \
  -R '^strata_(builder|model_adapter|view|gpu_scene|gpu_culling|render_routing)$' \
  --output-on-failure
./build/strata/aiverse_strata_metal_parity
./build/strata/aiverse_strata_metal_raster_parity
```
Expected **not yet observed** output:
`[strata-raster] Legacy/Fine changed_channels=0`,
`[strata-raster] Legacy/Coarse changed_channels=0`,
`[strata-raster] status=PASSED`.
The real pixel test creates
`strata-raster-empty.ppm`, `strata-raster-legacy.ppm`,
`strata-raster-strata-fine.ppm` and
`strata-raster-strata-coarse.ppm` in the current working
directory unless a custom prefix is set. On mismatch/compile
error, record the actual diagnostics and fix without relaxing the
pixel-parity oracle.

### Next bounded step
After the native fixture passes, extend the raster test to
multiple materials and independent shadow-map image comparison,
then reflection/multiview and randomized camera transforms. Verify
that preview mode OFF vs ON yields identical submitted draws and
pixels on an actual `GeometryScene` with multiple source instances.
Only after passing those real backend tests implement an
**exclusive**, scoped `Renderer3D` source-to-Strata draw switch,
with preserved legacy fallback for any unsupported material/
occlusion/pass/GPU residency. Do not enable GPU-selected indirect
commands or Hi-Z until independent actual GPU selection AND
raster/occlusion parity are established.


---

## Stage 11.1 — correct delayed Metal raster screenshot capture, do NOT relax A/B oracle (2026-11-19)

### Actual owner evidence
The owner compiled the new native `aiverse_strata_metal_raster_parity`
target on Mac Studio, ran it alongside `aiverse_strata_metal_parity`,
and reported:
- `[strata-metal] cases=5/5 status=PASSED`, including all real GPU
  compute cluster-selection scenarios and asynchronously readback ranges.
- `[strata-raster] FAIL: Legacy and empty screenshots identical; test drew no model`.
- `[strata-raster] status=FAILED frames=45`.
- `[metal-assets] libraries=0 sourceMisses=11 pipelines=16 pipelineFailures=0 archive=0 strict=0`.
Thus **real raster parity FAILED on the prior fixture version**; this
result must NOT be called PASS. It does NOT by itself establish a
cluster indexing bug, nor does it invalidate the independently
confirmed GPU selection pass.

### Diagnosed fixture bug
The real backend implementation
`engine/gfx/metal/GfxMetal.mm::captureNextFrame` sets
`layer_.framebufferOnly=NO` and uses `captureDelay_=1`
when the current drawable is already framebuffer-only; the
`endFrame` capture can be deferred for another 1–2 frames.
The original raster fixture requested its `Empty`, `Legacy`,
`Fine` and `Coarse` captures on frames **8, 19, 30, 41**,
the final frames of those modes. Because modes change on
**9, 20, 31, 42**, the saved image can come from the
*next mode*. This is sufficient to explain how two apparently
different labels could carry pixel-identical valid model
geometry and trip the independent nonblank baseline.
The hypothesis is source-grounded, but it requires a native
post-fix run to confirm no other visibility bugs exist.

### Code and documentation commits
- `bcc471e93c9a4a35f7fda457d4fa15ccbda13fe9`:
  `tests/render/strata_metal_raster_parity.cpp` enables
  `gfx::Device::setScreenReadable(true)` at initialization
  **before** the first test frame acquires the drawable,
  restores `false` during teardown, and requests screenshots
  on frames **4, 15, 26, 37** (safely inside each test mode,
  including if Metal defers by two frames). Captures now print
  mode, request frame, callback frame, main/shadow draw counts,
  and identical Empty/Legacy prints pass counters.
  **No production GPU rendering or shared pool changes.**
- `16d1c5a0cc1bb08b7f997f8a2894ea03a7bc0933`:
  `docs/strata.md` records actual failure, capture-deferral
  root cause, fix and test instructions.
- `746a56fb92209eb4f99963e7bed7c79ac6dda70b`:
  `AGENTS.md` documents Stage 11.1, no false PASS, and
  render-route still OFF.

### Validation status and next exact commands
- Stage 11.1 source fix has been committed through the connected
  GitHub API. **Native compile: NOT YET RUN. Corrected raster test:
  NOT YET RUN.** Earlier Stage 11 A/B fixture was a reported FAIL.
- Earlier six focused CTest contracts **PASS 6/6**, and
  real Metal compute/readback comparison **PASS 5/5**
  (owner-supplied native output), on earlier snapshots.
- The AIVERSE base renderer still uses the original
  `Renderer3D::drawModel` for all production draws. Strata only
  has preflight/preview and test-only alias models.

```sh
cd ~/Projects/AIVERSE-strata-validation
git status --short
git fetch origin feature/strata-virtual-geometry
# Only when the worktree is clean (or local changes saved):
git switch --detach origin/feature/strata-virtual-geometry
cmake --build build/strata \
  --target aiverse_strata_metal_raster_parity --parallel 8
./build/strata/aiverse_strata_metal_raster_parity
```

Corrected fixture must still prove `Empty != Legacy`,
`Legacy/Fine changed_channels=0`,
`Legacy/Coarse changed_channels=0`, nonzero main/shadow model
submission counters, and `status=PASSED`. It writes four PPMs
for inspection. If it still fails, use new capture timing logs
and examine PPM images to determine whether model genuinely
was culled/hidden, rather than relaxing pixel equality or
enabling unsanctioned Strata production draws.

### Next bounded work after actual PASS
Real shadow-map *depth texture* comparison, reflection/multiview,
material variants, independent CPU/GPU selected draw submission
and randomized LOD/camera transforms are still pending.
Do not enable production GPU-authored Strata indirect rendering
or integrate Hi-Z until those validation gates pass. Keep a
single `Renderer3D` draw authority and one
`GlobalGeometryPool` GPU source allocator.


---

## Stage 11.2 verification handoff — real Metal raster A/B PASSED (2026-11-19)

### Current status and branch
Branch: `feature/strata-virtual-geometry`; PR #35 Draft against
`main`. **No merge to main, no production Strata render switch.**
The Stage 11.1 capture-synchronization implementation commit
`bcc471e93c9a4a35f7fda457d4fa15ccbda13fe9` is now confirmed
to resolve the Mac Studio fixture failure.

### User-supplied native evidence (exact values)
The owner built `aiverse_strata_metal_raster_parity` and executed
the real Metal runner. All four screenshots returned 1,920,000 bytes
at the intended steady-mode capture points:

| Mode | Request frame | Callback frame | Shadow draws | Opaque draws |
| --- | ---: | ---: | ---: | ---: |
| Empty | 4 | 6 | 0 | 0 |
| Legacy | 15 | 17 | 1 | 1 |
| Strata Fine | 26 | 28 | 2 | 2 |
| Strata Coarse | 37 | 39 | 1 | 1 |

The observed diagnostic lines include:
```text
[strata-raster] Legacy/Fine changed_channels=0
[strata-raster] Legacy/Coarse changed_channels=0
[strata-raster] PASS: real Renderer3D legacy vs fine/coarse source-page pixel parity
[strata-raster] PASS: sun shadow and main passes submitted through same renderer
[strata-raster] PASS: exclusive one-ModelDraw-per-frame, no second GPU geometry pool
[strata-raster] status=PASSED frames=44
[metal-assets] libraries=0 sourceMisses=11 pipelines=16 pipelineFailures=0 archive=0 strict=0
```

**Validation claim supported by these logs:** lossless parent/leaf
cluster ranges in the shared pooled IB reproduce identical actual
main-camera RGBA pixels to the Legacy source (zero changed channels),
while the same production Renderer3D's main and sun-shadow phases
submit draws; only one ModelDraw is queued per candidate test frame.
The old blank-baseline failure does **not** persist in this owner run.

**Limitations:** The test checks *shadow-pass submission counters*,
not the actual depth texture or separate shadow-map screenshot
equivalence. It does not test reflection camera, multiple distinct
materials, UV seams, real lossy simplified geometry, dynamic skinning,
LOD transitions, GPU-selected indirect raster commands, Hi-Z,
streaming, or Vulkan. All opt-in Strata production drawing remains
disabled; its dry-run remains purely diagnostic. No performance
claim was established. The owner-supplied log does not contain an
explicit `git rev-parse HEAD`; this result is associated with the
post-capture-fix branch testing, not independently SHA-verified.

The prior owner supplied focused CTest result remains **6/6 PASS**,
and real Metal GPU culling + async readback parity **5/5 PASS**.
Those earlier validations are separate tests and were not
reexecuted in this specific raster terminal log.

### Files / commits / changes in this verification stage
- `docs/strata.md` updated to reflect actual PASS with strict
  evidence scope: `cab2c671b1c795153414e530e75fc8c68f8e6035`.
- `AGENTS.md` updated to mark Stage 11.1 as actually verified:
  `1297d44c96b0740ce1bec43d93e36c9c556f4dea`.
- This handoff is the separate stage record; no C++ code changes
  were necessary following the successful owner test.
- Underlying C++ screenshot fix `bcc471e9` remains unchanged.

### Next bounded stage for the next AI agent
Implement separate actual GPU **shadow depth-texture equivalence**
between Legacy and selected Strata, with verified nonconstant depth
rather than relying on counters; then add main/reflection multi-view
capture parity and at least two material/UV-boundary cases.
Preserve one shared `GlobalGeometryPool` source geometry owner and
exclusive single `Renderer3D` draw submission. Keep strict visual
oracles and explicit unsupported-material fallback. Only after
native real-Metal evidence for those gates should a scoped production
Strata/Legacy route be considered. Update this handoff after the
next implementation stage with exact commits and test results.


---

## Stage 12 — actual Metal shadow Depth32Float GPU readback and independent two-view parity (2026-11-19)

**Branch:** `feature/strata-virtual-geometry`, PR #35 (Draft,
base `main`). **No merge to main**. The already owner-verified
Stage 11 real-Metal raster fixture is preserved without modifications.

### What was implemented

1. `engine/gfx/Gfx.h` — new optional cross-backend
   `bool Device::readDepthTextureAsync(TextureHandle,DepthReadFn)`;
   default false on unsupported backends. The returned bytes are
   tightly packed native little-endian float32 depth with width
   and height, or empty on GPU error. Callback occurs on the device
   owner thread during later `beginFrame`. No blocking GPU wait.
2. `engine/gfx/metal/GfxMetal.mm` — Metal implementation,
   asynchronous private `MTLPixelFormatDepth32Float` 2D texture
   copy to a temporary shared GPU readback buffer, with
   256-byte-aligned rows repacked to `width*height*sizeof(float)`.
   Restricts sizes to <=4096 per axis and <=64MiB per copy,
   max eight pending readbacks, requires `cmd_` and no active
   graphics/compute encoder, rejects invalid formats/handles.
   Uses an async command-buffer completion handler to populate
   payload and owner-thread `deliverDepthReads()` after the
   normal RHI frame fence; errors return empty data. It does not
   create a second geometry pool, expose a synchronous read of
   private textures or change ordinary draw routing.
3. `engine/render/Renderer3D.h/.cpp` — small opt-in
   `requestSunShadowDepthReadback(gfx::Device::DepthReadFn)`
   delegated to `dev_->readDepthTextureAsync(shadowMap_, ...)`,
   requiring shadow quality ON, a valid map, active renderer.
   No raw `shadowMap_` ownership leak.
4. `tests/render/strata_metal_depth_multiview_parity.cpp`
   — separate real Metal CAMetalLayer fixture reusing the
   previously successful Stage 11 source+lossless cluster hierarchy
   and **same pooled source VB and sidecar IB**. It uses one
   production `Renderer3D`, submits exclusive Empty/Legacy/
   Fine/Coarse ModelDraw mode in isolated steady frames,
   and repeats for two independent camera positions:
   - view0 pitch 0.2 yaw 0 distance 5;
   - view1 pitch 0.55 yaw 0.7 distance 6;
   - output color 800×600 RGBA, shadow 512×512 Depth32Float.
   Capture requests occur **inside** steady 11-frame windows,
   not on the transition frame, and `setScreenReadable(true)`
   preserves the previously confirmed Metal capture fix.
   The test reads real depth **after** finishing main pass in
   the same frame as the color capture, not merely shadow draw
   counters. It writes one PPM and one raw `.f32` file per
   camera/mode (8 of each), with optional
   `AIVERSE_STRATA_DEPTH_CAPTURE_PREFIX` for paths.
   Strict gates: Empty != Legacy for color AND depth, both
   Strata fine/coarse equal Legacy byte-for-byte in both formats
   for both cameras, nonclear finite depth in [0,1], distinct
   color screenshots for view0/view1, main/shadow pass draw
   counters nonzero for model modes, and exactly one ModelDraw
   per comparison frame. The LOD hierarchy is **lossless** and
   this deliberately is NOT a simplified LOD visual quality test.
5. `CMakeLists.txt` — `aiverse_strata_metal_depth_multiview_parity`
   added to macOS-only opt-in
   `AIVERSE_STRATA_METAL_GPU_TESTS=ON`.
   `.github/workflows/strata-focused.yml` compiles the new
   target on macOS CI, **but does not run its GUI**.

### Exact commits in this stage (implementation followed by handoff)
- `c90437baaa1f3b52e69d839b6f205937dfb1bd98`: opt-in DepthReadFn virtual API
- `302e6be51768761ac7e7af3f8d7c341f353d6149`: real Metal async depth blit
- `2d7838a89b2d80d17bc29fc423e6d1c54729028e`: Renderer3D header
- `af17549a32037453b3e0eed5f8115afcd74a87ca`: Renderer3D shadow readback method
- `fe61cc57c6dbe170c56bbbf3b1d097662e341cab`: real Metal 2-view pixel/depth fixture
- `e03d17acc86f31bb5187f7c3b1f57fff39d672b8`: macOS CMake target
- `723878d45438cdd58d8b35b62a2cd26dbaa93430`: macOS CI compile target
- `315745c2923444ebc5b37e1b93b78b1f56dee087`: raw .f32 depth captures
- `cd77ccc959e072848e62e09c4c1cf843269a31f9`: `docs/strata.md` stage 12
- `f10d0c9f71f7a1e0f1d1d4b79b553f451796ca7c`: root `AGENTS.md` agent routing

### Actual test results vs outstanding claims
- **Previously owner-verified:** Stage 11 real Metal
  main-color raster parity exact 0 changed channels, PASS;
  Stage 8.2 real Metal GPU compute cluster selection/async
  readback against CPU oracle 5/5 PASS;
  focused six CPU/mock Strata CTests PASS. Those are
  **prior snapshots**, not a new Stage 12 run.
- **Stage 12 C++ compile: NOT RUN in the assistant's
  environment. Stage 12 real Metal raw depth readback: NOT RUN.
  Stage 12 two-view equality: NOT RUN.** Do NOT mark PASS
  from this commit; do not treat a queued CI workflow as green.
- **Not supported yet:** reflection target capture, mixed
  materials, watertight lossy LOD, GPU-selected cluster
  command rasterization, production exclusive route,
  streaming, Vulkan parity or performance improvement.

### Next Mac Studio validation
Use the existing worktree and do not discard uncommitted work:
```sh
cd ~/Projects/AIVERSE-strata-validation
git status --short
git fetch origin feature/strata-virtual-geometry
git switch --detach origin/feature/strata-virtual-geometry
cmake -S . -B build/strata \
  -DAIVERSE_BUILD_TESTS=ON \
  -DAIVERSE_BUILD_STRATA=ON \
  -DAIVERSE_STRATA_METAL_GPU_TESTS=ON
cmake --build build/strata --target \
  aiverse_strata_metal_depth_multiview_parity --parallel 8
./build/strata/aiverse_strata_metal_depth_multiview_parity
```

Expected if real hardware agrees (unverified):
```text
[strata-depth] view=0 color_fine=0 color_coarse=0 depth_fine_bytes=0 depth_coarse_bytes=0
[strata-depth] view=1 color_fine=0 color_coarse=0 depth_fine_bytes=0 depth_coarse_bytes=0
[strata-depth] PASS: two independent camera views
[strata-depth] PASS: exact sun-shadow Depth32Float Legacy/Fine/Coarse parity
[strata-depth] status=PASSED views=2 modes=4 frames=...
```
On C++/Objective-C++ compile or GPU validation errors:
collect the actual diagnostics, update the precise failed
assertion, and fix without loosening depth/color baseline
guards or adding a second GPU geometry allocator. In
particular, inspect Metal resource blit restrictions and the
saved `.f32` output on unexpected depth mismatches.
Record the observed tested checkout SHA when available.
Test output is required before advancing to production routing.

### Next bounded development stage
After actual native Stage 12 depth/color parity PASS, implement
a separate real Metal planar-reflection target image A/B
(opaque models mirrored by Renderer3D's existing water/reflection
pass), with both camera views and material variants; do not
pretend main-view captures prove reflection parity.
Then validate lossy meshlet transitions, cracks and GPU-selected
draw input under an **exclusive per-pass renderer gate**, with
conservative Legacy fallback. Update this agent handoff
after every bounded stage.


---

## Stage 12.1 verification handoff — owner-proven actual two-view Metal sun-shadow DEPTH parity PASS (2026-11-19)

### Branch and scope
Branch `feature/strata-virtual-geometry`, PR #35 Draft against `main`.
This verification step modifies documentation only. No production
`Renderer3D` submission or Strata geometry allocator changes.
Main remains untouched. This step supersedes the 'Stage 12
NOT RUN' prevalidation note above, rather than rewriting past
history.

### Actual Mac Studio output (not a prediction)

The owner posted the full result of three real Metal GUI fixtures
after building the Stage 12 target:

1. `aiverse_strata_metal_parity`: all five actual Metal
   compute cluster-selection/async-readback CPU-oracle fixtures
   `PASS`; final
   `[strata-metal] cases=5/5 status=PASSED`.
2. `aiverse_strata_metal_raster_parity`: four requested colour
   captures returned 1,920,000 bytes each; measured
   `Legacy/Fine changed_channels=0` and
   `Legacy/Coarse changed_channels=0`; main/sun-shadow renderer
   submission and one-ModelDraw/shared-pool invariants PASS;
   `[strata-raster] status=PASSED frames=44`.
3. **New Stage 12**
   `aiverse_strata_metal_depth_multiview_parity`:
   - Eight RGBA captures (2 cameras × Empty/Legacy/Fine/Coarse)
     returned 1,920,000 bytes each; eight real private
     Metal `Depth32Float` shadow maps were GPU-blitted and
     received (262,144 samples per map).
   - View 0: `color_fine=0 color_coarse=0
     depth_fine_bytes=0 depth_coarse_bytes=0`.
   - View 1: `color_fine=0 color_coarse=0
     depth_fine_bytes=0 depth_coarse_bytes=0`.
   - View 0 shadow non-clear samples: **73,179**.
     View 1 shadow non-clear samples: **34,765**.
     This independently checks that raw depth was rendered,
     not merely clear-only or equal by accident.
   - Actual pass messages:
     `PASS: two independent camera views`;
     `PASS: exact main RGBA Legacy/Fine/Coarse parity in each view`;
     `PASS: exact sun-shadow Depth32Float Legacy/Fine/Coarse parity`;
     `PASS: nonconstant shadow content and exclusive model submission`.
   - Final:
     `[strata-depth] status=PASSED views=2 modes=4 frames=89`.
     Metal:
     `[metal-assets] libraries=0 sourceMisses=11 pipelines=16
     pipelineFailures=0 archive=0 strict=0`.

**Actual validation interpretation:** this is a native real-GPU test,
not Mock-RHI, fake depth, or just a count of submitted draws.
The original Metal renderer handled both main camera views and
the sun shadow pass; real raw 32-bit floating-point depth samples
were compared, byte-for-byte, between source and Strata
test-only alias models on **lossless** cluster geometry.

**Known limitation:** no tested checkout SHA was printed
(`git rev-parse HEAD` absent), so do NOT assert the GPU
log was executed against a specific source SHA, only the
updated feature-branch snapshot supplied by the owner.

### Docs changed and exact commits

- `docs/strata.md`: `fb0f9bf53df0e37fdd1d345e990646f252afc549`;
  updated actual GPU Stage 12 parity status.
- `AGENTS.md`: `2ed169505fa7c94f553d73ec21afc0c0526ae2a4`;
  updated agent-facing contract and actual run evidence.
- This independent handoff is the last documentation-only
  commit of the verification stage; no code edits required
  following the successful hardware run.

### Verified vs not verified

**Verified by owner output:** focus native Strata CTest 6/6 on
prior run; real Metal GPU culling 5/5, real main-camera A/B 0
changed channels, two independent cameras and actual sun shadow
raw Depth32Float strict parity; one original source pool and
one ModelDraw per candidate test frame.

**Still NOT verified:** actual planar reflection RTT colour output,
different material groups and UV seam discontinuities,
deliberately lossy LOD pixel-quality tolerances and mesh cracks,
drawing from GPU-selected cluster command buffers as a
production exclusive path, Hi-Z occlusion, streaming residency,
Vulkan parity, sustained GPU speedups, frame-fence lifetime
under scene unload, and mixed-world lifecycle.
No default-on new renderer behavior and no merge to `main`.

### Concrete next step for another agent
Implement a dedicated **real Metal planar-reflection RTT**
fixture through the same production `Renderer3D` reflection pass,
with at least two cameras and multiple materials in the scene.
Capture the actual reflection texture (NOT just the main screen
or renderer counter), establish `Legacy != Empty` reflection
baseline, and strict `Legacy==Strata Fine==Strata Coarse`
when using the lossless stage hierarchy. Keep the original
RHI shader/sampler states and `GlobalGeometryPool`; avoid
adding a second Strata renderer or geometry pool.
Then separately test genuinely simplified mesh LODs for
cracks/edge error and GPU-selected cluster indirect submission.
After each bounded implementation stage add an updated
handoff with changed files, real test logs, commit SHA, PR
state, limitations, and next step; never claim unrun tests.


---

## Stage 13 — real Metal planar reflection RTT + two independent PBR materials / UV seam parity (2026-11-19)

**Branch:** \`feature/strata-virtual-geometry\`; PR #35 Draft;
base \`main\`. This stage has NOT been merged, and there is no
production Strata raster route or second GPU geometry allocator.
The previous Stage 11 and Stage 12 owner-confirmed Mac Studio
GPU fixtures are preserved unchanged.

### Implementation changes, architecture and exact commits

1. \`engine/gfx/Gfx.h\` —
   \`gfx::Device::readColorTextureAsync(TextureHandle,
   ColorReadFn)\`: opt-in diagnostic for actual offscreen RGBA8
   target, default false on unsupported backends.
   Commit \`6478fa3360a3c85996005acc24a58fab1050fa51\`.
2. \`engine/gfx/metal/GfxMetal.mm\` —
   bounded asynchronous GPU blit from actual
   \`MTLPixelFormatBGRA8Unorm\` offscreen reflection target to
   temporary shared \`MTLBuffer\`, 256-byte row-alignment,
   BGRA-to-RGBA conversion, GPU-error empty callback, owner-
   thread delivery, maximum 8 outstanding requests, size
   <=4096² and <=64 MiB. Requires no active pass or compute
   encoder. No GPU stall and no main-screen substitution.
   Commit \`b2adc01d180d1310596d06a72149172f33413d27\`.
3. \`engine/render/Lights.h/.cpp\` —
   \`LightSystem::requestPlanarReflectionColorReadback()\` gates
   on this frame's actual \`reflReady_\`, an allocated
   \`reflColor_\`, and **LDR RGBA8**. It only delegates to
   \`gfx::Device\`, preserving ownership of the reflection
   texture. Commits
   \`a9e4bf21991144c6f4b773f8ecb36430d0e83c0c\`,
   \`5cc80bd8f9608085b13df147736b956047de7399\`,
   \`43dffdbcd1652620e01bd1c0431797cfcd0cb2a4\`.
4. \`engine/render/Renderer3D.h/.cpp\` —
   \`requestPlanarReflectionColorReadback()\` requires the
   selected water reflection plane to be valid and delegates
   to the existing light system; no new renderer or exposure
   of the raw texture handle. Commits
   \`28cac455ddab2a48748ea73c1465d37b089a5aa9\`,
   \`bb53faf4bd5854b999535724a648c24965cbe1a2\`.
5. \`engine/render/StrataRenderRouting.h\` —
   new \`strata::RenderPass::PlanarReflection\` so the
   preflight can use the actual mirrored reflection VP
   independently of main-camera/sun-shadow selection.
   This is NOT production Strata draw permission.
   Commit \`c01bc2e26781771a01231a63869b4ce464b2e120\`.
6. \`tests/render/strata_metal_reflection_material_parity.cpp\`
   — new manual Metal fixture, derived from and extending the
   already owner-verified Stage 12 fixture; commits
   \`b3ea93f4a9809dc68ad83c9c4e3209cf6a527515\`
   (fixture),
   \`32b9dde39f6459f4a84e5a6a1cf52a136522d105\`
   (remove duplicate reflection capture declarations),
   \`a20e3e8227229ad9b18e954fd2b9a869d04e66ab\`
   (exact coincident UV/material seam).
7. \`CMakeLists.txt\` — opt-in macOS-only target
   \`aiverse_strata_metal_reflection_material_parity\`
   enabled by existing \`AIVERSE_STRATA_METAL_GPU_TESTS=ON\`:
   \`dc7208d82c837dbea1c420d03581c49c943af78d\`.
   \`.github/workflows/strata-focused.yml\` additionally
   COMPILES the target on macOS without opening an AppKit
   window: \`53aac249fc2cb238542058009461a0d43c2b5a91\`.
8. \`docs/strata.md\` updated with Stage 13 exact
   architecture, test oracle, limitations and native commands:
   \`07d341e6f7f0643d5bd602ff0a8e8154ce042300\`.
   Root \`AGENTS.md\` updated with new stage contract:
   \`dd044660e05b791c37af66398bbc17b28c87db3f\`.

### What the fixture actually checks

A single original \`Model\` contains two adjacent quads sharing
the x=0 geometric seam but with **independent vertex records at
that boundary**. Each quad has its own opaque PBR material,
base-color/roughness/metallic factors, distinct
\`Material::baseTex\`, distinct 4×4 GPU checker image, and
independent UV offset/scale. Each opaque primitive has its own
Strata asset of ONE lossless parent (both triangles) and TWO
resident child clusters (one triangle each). Both primitives
are staged in the same \`GpuMetadataScene\`, sharing the original
\`GlobalGeometryPool\` source VB/IB and pooled Strata auxiliary
index sidecar; Fine/Coarse models are CPU-only borrowed aliases.
These aliases are NEVER uploaded or released as GPU sources.

Production \`Renderer3D::renderReflection()\` is triggered by an
existing translucent \`kDrawWaterSurface\` mesh with
specular=0.98 and high \`LightSystem\` quality, causing actual
half-resolution planar reflection RTT rendering. No fake GPU
output and no second geometry renderer is used.
The standalone fixture renders Empty/Legacy/Fine/Coarse in
**mutually exclusive frames**, for two independently positioned
cameras. Selection preflight for every material uses the
reflection's mirrored view-projection and
\`RenderPass::PlanarReflection\`, with \`frustumCull=false\`
in the diagnostic selector to isolate material/index/LOD
mapping; the **actual Renderer3D reflection path still has
its normal visibility culling**.

For every view and mode the test obtains THREE independent
actual Metal GPU outputs:
- Main 800×600 RGBA8 screenshot.
- Sun shadow 512×512 Depth32Float readback.
- Reflection **offscreen RTT** 400×300 RGBA8 readback, NOT a
  screenshot of the main framebuffer.

It captures 24 GPU outputs total (2 cameras × 4 modes × 3
buffers), saves PPMs and raw .f32 shadow maps with default
prefix \`strata-reflect\` (override with
\`AIVERSE_STRATA_REFLECTION_CAPTURE_PREFIX\`), and requires:
- All 24 readback callbacks received with expected dimensions.
- This frame's true reflection render pass executed.
- In each camera **Legacy != Empty** in main, depth and
  **reflection RTT**; reject equal all-clear screenshots.
- Legacy/Fine and Legacy/Coarse bytewise **exact equality**
  in all three buffers for both cameras.
- Different Legacy screenshots across cameras, including
  the actual reflection RTT.
- At least 2 opaque/material and shadow source draws
  (4 fine-cluster draws), and precisely ONE \`ModelDraw\` per
  candidate frame, without double-drawing Legacy+Strata.

Strict equality is meaningful only because the fixture
uses **LOSSLESS** parent/leaf triangle partitions. It does
not measure lossy LOD silhouette/attribute/texture errors
or cracks; no reflection PNG can substitute for the raw
offscreen readback.

### Actual validation status — do not invent a PASS

- **New Stage 13 C++/ObjC++ compile: NOT RUN by the agent.**
  New \`aiverse_strata_metal_reflection_material_parity\`
  real Metal hardware run: **NOT RUN**. Reflection RTT,
  UV seam, two-material and two-camera parity are therefore
  **NOT YET VERIFIED**.
- Previous owner-provided Mac Studio evidence remains:
  Strata focused native CTests 6/6 PASS (earlier run);
  real GPU compute selected cluster readback 5/5 PASS;
  Stage 11 raster 0 changed channels PASS;
  Stage 12 two-view main RGBA and REAL sun-shadow raw
  depth bytes 0 changes PASS, nonclear shadow depths.
- GitHub CI may compile Stage 13, but CI does not execute
  graphical GPU windows. A queued/pending/green compilation
  must not be reported as true reflection parity.
- Test source SHA is only established if native user log
  includes a \`git rev-parse HEAD\` output; do not invent one.

### Owner Mac Studio steps (preserve local changes)

\`\`\`sh
cd ~/Projects/AIVERSE-strata-validation
git status --short
git fetch origin feature/strata-virtual-geometry
git switch --detach origin/feature/strata-virtual-geometry
git rev-parse HEAD
cmake -S . -B build/strata \
  -DAIVERSE_BUILD_TESTS=ON \
  -DAIVERSE_BUILD_STRATA=ON \
  -DAIVERSE_STRATA_METAL_GPU_TESTS=ON
cmake --build build/strata \
  --target aiverse_strata_metal_reflection_material_parity \
  --parallel 8
./build/strata/aiverse_strata_metal_reflection_material_parity
\`\`\`

If the worktree is dirty, preserve or commit changes before
switching to the remote feature HEAD. The desired (NOT YET
OBSERVED) final result is
\`[strata-reflect] status=PASSED views=2 modes=4 frames=89\`
and 0 differences for main RGBA, sun depth and real
reflection RTT, in both views. This is a test expectation,
not a claim of observed success.

### Next bounded stage after real native test

If Stage 13 fails, fix the precise Metal compiler/backend
blit or actual reflection/image mismatch, and update this
handoff with owner-supplied logs. Do NOT weaken the strict
empty baseline or fake offscreen RTT via screen captures.
Once Stage 13 passes on real Mac Studio, next address
genuinely lossy meshlet LOD error/attribute borders and
cross-cluster cracks, multi-world residency/recycling
stress, and an exclusive per-pass production
Renderer3D Strata/Legacy draw gate with conservative
fallback and GPU indirect command readback validation.
Keep \`main\` untouched until those final gates are met.


### Parallel-main divergence audit (after Stage 13 implementation)

At the time Stage 13 was completed, \`main\` had advanced to
\`d7d9b3f4180dcba98b9238fd56eedc7ccb420bb1\`. GitHub's compare
showed this Strata branch **186 commits ahead and 162 behind**.
The incoming main-only diff consisted mainly of independent
shared-static ECS, scene/world runtime and skill registration work.
\`CMakeLists.txt\` is also changed in both directions.

**No automatic merge to main or forced ref rewrite was attempted.**
The old shared build/test outputs must not be used to claim that
the NEW main+Strata combined tree has been validated.
The safe next integration after a Stage 13 GPU test is to fetch
\`origin/main\` into an isolated local Strata worktree, merge it
with \`--no-ff\`, reconcile the CMake target blocks and any
new overlaps, record the resulting two-parent commit, run the
focused native Strata CTests plus the latest real Metal
fixtures, and commit a separate merge validation handoff.
Never overwrite independent agents' CMake targets or silently
update \`main\` by merging this Draft PR.


---

## Stage 14 — portable Vulkan/GLES3 CPU-selected Strata raster route (implementation 2026-11-19)

**Branch** \`feature/strata-virtual-geometry\`, PR #35 Draft against main.
No change/merge to main. This stage implements **actual opt-in
Renderer3D raster drawing of selected Strata cluster ranges** across
Metal, Vulkan, GLES3/WebGL2 and GL33 through the EXISTING renderer.
Vulkan/GLES3 do not require Metal compute or GPU-authored ICB to work.
There is no second vertex pool, second renderer or global GPU scene.

### Implemented components

- \`engine/render/StrataPortableRender.h/.cpp\`:
  \`strata::PortableSource\` stages all eligible rigid opaque
  primitives transactionally, rejecting incomplete/skipped models
  or invalid material/vertex/index ranges. It owns a single
  immutable auxiliary index-only stream for the whole staged
  source. On MDI-capable sources in \`GlobalGeometryPool\` it first
  tries \`uploadAuxiliaryGeometryIndices()\` to put data into the
  **existing GPU page**, with scene/revision/lease checks.
  Otherwise it creates just one dedicated \`BufferType::Index\`
  RHI buffer, borrowing the **existing source vertex buffer**.
  Dedicated index fallback works on GLES3/WebGL2 and Vulkan with
  no compute/shader-storage feature.
- \`PortableSource::prepareModel(instanceWorld,passes,forceFinest,out)\`
  uses CPU \`planView\` and independent camera, sun-shadow and
  optionally conservative reflection requirements. For meshes
  referenced by multiple nodes and passes it merges **selected
  hierarchical frontiers at the finest demanded depth**, not a
  union of overlapping parent+child indices and never frustum-culls
  by the main camera alone. Reflection with unknown future camera
  uses the *finest available resident frontier* for all passes
  rather than risking omitted reflected geometry.
  A staged \`Model\` alias borrows the ORIGINAL VB plus the
  auxiliary index-only buffer, live materials, image texture
  handles, source bindWorld/node transforms, pooled base vertex,
  and source bounds. Material, mesh, original authored LOD and
  shadow/material batching are deliberately kept compatible with
  the standard per-primitive \`Renderer3D::drawModel\` path; alias
  built-in LOD/batches are cleared so there is no *second* LOD
  or double geometry. No raw \`gfx::drawIndexed\` code exists
  outside Renderer3D. An out-of-date geometry/material revision,
  material changed to alpha/shader graph, absent node coverage,
  invalid LOD, missing residency or GPU sidecar results in
  **transactional Legacy fallback**. Output untouched on failure.
  Never \`uploadModel\` or \`releaseModel\` a borrowed alias.
- \`engine/render/GeometryScene.h/.cpp\`: new default-OFF
  \`setStrataPortableRenderingEnabled(bool)\`, set **before**
  load/loadPrepared. Automatically requests CPU Strata staging
  even if the old diagnostic staging toggle is false.
  After ordinary source Model uploads it admits portable
  sources per source model, with no exception if a model is
  unsupported (Legacy remains valid).
  On \`GeometryScene::drawInstance\`, prepares an alias per
  instance, with main/sun pass requirements, and chooses
  **exactly one** \`Renderer3D::drawModel\` invocation:
  Strata if preparation succeeded; original Legacy on any
  failure. Existing HLOD/surface proxies remain Legacy.
  Successful aliases disable source StaticModelCache/packet
  reuse to avoid dangling original primitive pointers, and
  the per-instance alias Model lives through \`render()\`.
  Scene shutdown releases aliases, then portable sidecars,
  then existing GPU metadata and source Models; GPU page
  fences/recycling remain owned by existing geometry pool/RHI.
  New per-scene stats distinguish portable staged/declined
  sources, actual substituted ModelDraws, fallbacks and
  selected cluster counts.
- \`CMakeLists.txt\` links portable static library into runtime.
  \`tests/render/strata_portable.cpp\` and
  \`aiverse_strata_portable_tests\` record mock GLES3,
  Vulkan, Metal and desktop GL index-path behavior, one
  source VB with one optional sidecar IB, multi-pass
  refinement, forced-finest reflection safety and material
  revision fallback. The tests exist but **have NOT been run**,
  per owner's request to finish functions first.

### Exact implementation commits
- \`56e8c564130437edf5d3a4a2281da528151176b7\` header
- \`891b35ad00a02ebcbe459c13e3ab7743537a3253\` portable implementation
- \`89eb60a228f234b7522ad4227d2bd40a63963123\` remove unreachable guard
- \`b461597f30a1c3a80046e6ad6691b9ec408fabfb\` portable CMake library
- \`403989f1b7e557b10e29efd893b60ec100a04ee8\` GeometryScene API
- \`1949ebbb7bb1c25c62cf2fbf44f08c1c4606f2f1\` exclusive opt-in GeometryScene source/alias draw
- \`9bca6e0d20ee2cd81e1cf3f4ca39b8aba141b657\` recording RHI test
- \`b316bb55536f2c788b2cad4e299d4bf9178a33cc\` CMake test target

### Validation
**Stage 14 source code is committed but has NOT been compiled or
run on any backend**. Do not claim Vulkan or GLES3 hardware
parity, successful native builds, or completed CTest.
The owner previously confirmed Stage 13 real-Metal strict
lossless reflection/Main/sun-shadow parity PASS on the
older test-only aliases; that does not prove the new
GeometryScene production opt-in route or latest combined main.

### Actual backend contract and limitations

- Metal: CPU reference selected cluster raster via Renderer3D;
  optional existing diagnostic Metal GPU compute/readback remains
  separate, not an authority for portable draw.
- Vulkan: indexed raster via existing Vulkan RHI, shared index
  pool when supported, otherwise dedicated sidecar. Selection
  uses the same portable CPU implementation.
  Vulkan RHI has **no Vulkan compute/SSBO selection path yet**
  (\`supportsCompute()\` is not overridden); do not call
  this GPU-driven selection. Vulkan GPU compute optimization
  requires its own Vulkan RHI compute pipeline implementation.
- GLES3 / WebGL2: indexed raster through standard ES3
  \`glDrawElements\` and existing emulated base-vertex
  attribute pointers. CPU selection and separate immutable
  index-only fallback by design. GL ES3.0 does NOT require
  compute, SSBO, Vulkan descriptors, or Metal ICB.
- All: selected Strata geometry is rendered with the SAME
  PBR textures/shadow/reflection pipeline, and only one
  \`Renderer3D::drawModel\` request for a source instance;
  opt-in OFF remains exactly Legacy. Generic Strata
  \`PortablePassView\` has no RTS/FPS dependencies, and
  the actual renderer remains responsible for final
  pass scheduling, shadows and reflections.

### Deferred build-only phase after functional work

The user explicitly requested no intermediate CTests/hardware
tests. Once feature work finishes, do one native build
of the \`aiverse_strata_portable_tests\` target alongside
the existing Strata and real Metal parity targets, then
report compile diagnostics and only afterwards perform the
final Mac Studio runtime tests when the owner approves.

Continue **Stage 14.1** with builder/sidecar quality and
backend lifecycle hardening before the final build; record
every bounded change here and preserve concurrent main
work (branch was behind main during Stage 13).


---

## Stage 14.1 — portable Strata quality, fallback and GPU-sidecar lifetime hardening (2026-11-19)

**Repository/branch:** `chromealex/AIVERSE`, `feature/strata-virtual-geometry`.
This stage is committed directly to the feature branch, **not** to `main`.
Most recent Stage 14.1 *source/test* commit before this handoff:
`7f40eef3233dafa8a9fa2d2ae1669193297f0272`. No test or local
Mac build is claimed from this GitHub connector environment.

### What was actually implemented

1. **Spatial LOD parent grouping** — `engine/render/StrataBuilder.cpp`.
   At each hierarchy level, the frontier is stable-sorted by 30-bit 3D
   Morton keys of cluster AABB centres (10 quantized bits/axis). Bounds
   normalization uses double intermediates to avoid float overflow for
   large but finite positions. This removes arbitrary input-index-order
   grouping of distant source islands. It does **not** reorder LOD0
   triangles or meshlet vertex IDs, duplicate the source VB, or change
   the per-material partition. Existing
   `meshopt_SimplifyLockBorder` and attribute-aware simplification
   are retained. Cumulative error and hierarchy BFS validation are
   unchanged.
2. **Device-owned sidecar lifecycle** —
   `engine/render/StrataPortableRender.h/.cpp`.
   `PortableSource` remembers the `gfx::Device` used by `stage`.
   `validFor` refuses an unhealthy device. A `shutdown` called
   on a different device is a no-op, preventing accidental deletion
   of another RHI's same-numeric-ID buffer. The owner MUST still
   `shutdown` with the original live device before source unload;
   device lifetime is borrowed and no recovery allocation is attempted.
3. **Legacy prepared-cluster coexistence** —
   `StrataPortableRender.cpp`. A source with pre-existing
   `Model::geometry` (ordinary `prepareGeometry` clusters and
   occluders) can enter the portable path if all other rigid/material
   checks pass. The Strata alias *does not copy* that legacy geometry,
   clears its built-in mesh LOD and batch chains, and draws one selected
   Strata frontier through Renderer3D. The untouched source retains
   the original Legacy geometry for fallback. A source may use one
   additional immutable index buffer when the shared pool refuses
   prepared clustered models; **no extra vertex buffer** is created.
4. **Import/streamed-asset safety gate** — `PortableSource::stage`
   now rejects an asset whose sum of leaf-node index counts differs
   from the original root primitive's `indexCount`. It checks finite,
   ordered node AABBs, every parent AABB contains all child AABBs and
   parent geometric error is no smaller than its children. These checks
   run **before** sidecar GPU upload, so malformed hierarchies cannot
   silently disappear at finer LOD or cause nonconservative culling.
   This is count/bounds coverage, *not* an exact per-triangle proof once
   original CPU index data has been released.

### Tests authored (deferred, NOT executed)

- `tests/render/strata_builder.cpp`: alternate complete rows of
  two spatially distant grid islands; assert original leaf triangle
  order is preserved while first-level parent AABBs remain local.
- `tests/render/strata_portable.cpp`: recording-RHI Vulkan/GLES3
  test for pre-existing `GeometryData` source (legacy remains,
  selected alias has none); reject incomplete leaf frontiers or
  incorrect parent AABBs without allocating a sidecar; foreign-device
  shutdown cannot release handles; device loss rejects preparations
  transactionally. Existing mock Metal/GL33 cases remain.

**Executed builds/CTest in this stage:** NONE (owner requested
feature completion before one final native build/test pass).
No claim of Vulkan/GLES3 device-level rendering parity. Real
Mac Stage 13 reflection+shadow pixel parity PASS is prior,
narrower evidence; it does not certify Stage 14/14.1.

### Changed files and commits

- `engine/render/StrataBuilder.cpp`: `1a1d5d2292f10c559f35d3369248e3e20c9fa42b`
- `tests/render/strata_builder.cpp`: `1ccbbc9f77d9c84f9d99a7416c134a92d0f99a23`
- `engine/render/StrataPortableRender.h`: `92f68075f07418ee72a5b6050e5c5e89cf2938f3`
- `engine/render/StrataPortableRender.cpp`: `613ff7138b46e70c77b0ec5d3c35cc77106ea144`,
  `992ae6be73a777a73ff30e335f43a1583a3c54bf`,
  `f7efc6d46aeb4de95b238ccffebd227d60520f02`
- `tests/render/strata_portable.cpp`: `b4314b559a171d88af582f49070d908d43650701`,
  `e5eb4c01bad9d57a31e3b4be9cdb70ccf0479d45`,
  `7f40eef3233dafa8a9fa2d2ae1669193297f0272`
- This file, and `docs/strata.md`: updated as Stage 14.1 handoff/docs.

### Remaining risks and next concrete step

- **Validation required**: do a final isolated native build of
  `aiverse_strata_builder_tests` and
  `aiverse_strata_portable_tests` (confirm target names against
  `CMakeLists.txt`), and the Stage 13 Metal reflection parity
  fixture; run Vulkan/GLES3 real-device raster and fallbacks when
  supported by the owner's platforms. Only execute once feature work
  has been declared complete.
- Verify newly authored spatial-island parent assertion with the
  repository's pinned meshoptimizer (parent simplification is data-
  dependent), and reconcile exact Metal/Vulkan/GLES3 base-vertex
  addressing in a real renderer path.
- No GPU-driven Vulkan compute selector yet; GLES3/WebGL2 remains
  CPU-selected by design. No claim of true per-pass GPU-occlusion
  hierarchy, crack-free **lossy** LOD transitions, bounded
  multi-world streaming residency, or measured frame-time wins.
- `main` had already diverged by 162 commits in the earlier audit.
  Preserve concurrent agents' changes; perform a separate isolated
  non-force integration merge and native build. **Do not merge this
  unverified Draft Strata branch directly into main.**

**Handoff continuation:** Keep one authoritative Renderer3D draw per
source instance; concentrate on residency/streaming budgets and
real per-backend raster/LOD lossless+lossy parity, then one combined
native validation. After every subsequent stage, append to this
handoff with actual test status and fresh commit identifiers.


### Stage 14.1 follow-up — source GPU reupload identity (same stage)

After the above handoff, a further dedicated-sidecar stale-binding
hazard was removed. `PortableSource` now snapshots its source
`vb`, `ib` and `gpuFirstIndex` at successful `stage()`, and
`validFor()` checks those handles and the staged
`gpuBaseVertex` against the live Model before every preparation.
This complements the existing pooled `GeometryPoolIndexSpan` lease
checks. If a source is released/reuploaded without calling
`markGeometryChanged()`, an old index sidecar must not accidentally
draw with a replacement vertex allocation.

- `engine/render/StrataPortableRender.h` commit
  `569e4b64408c4596a055ce8556caa27910096d2b`
- `engine/render/StrataPortableRender.cpp` commit
  `81dfb0fe8b65f3acc9dda8bb58dfb7ab3fd0b2c2`
- `tests/render/strata_portable.cpp` commit
  `18878b0616e1288d7ba53e665cdd65dd55d2614e`

The recording-RHI regression mutates the source VB identity and
first-index base to confirm rejection and unchanged prepared output.
The source must still be shutdown via the same live RHI device.
**No build or test run** was executed for this follow-up either.
Latest code/test source snapshot before the documentation commits:
`18878b0616e1288d7ba53e665cdd65dd55d2614e`.


---

## Stage 15 — index-only GPU residency + scene-driven demand (2026-11-19)

**Branch:** `feature/strata-virtual-geometry`. Work is committed to the
feature branch only. **No local compilation or CTest was executed**;
the owner requested one final build/test batch after functionality.
This stage is a real, opt-in GPU index-buffer residency path, not a
CPU-only simulation. It is **not** Nanite-equivalent on-disk virtual
geometry or Vulkan GPU-driven compute.

### Architecture and implemented behavior

- `engine/render/StrataIndexResidency.h/.cpp` owns one budgeted
  **index-only** resident RHI buffer generation per source model.
  `initialize(device, asset, budgetBytes)` uploads only the pinned
  hierarchy roots. Failure if root bytes exceed budget is atomic and
  lets the existing model render through Legacy. It validates topology,
  finite AABBs, parent containment, monotone parent LOD error, index
  ranges and device health before uploading.
- `update(vector<IndexDemand>)` processes explicit per-node demands
  in stable descending priority order. It promotes complete sibling
  groups along a root-to-node path, never only one child whose siblings
  are missing. Under budget pressure it keeps drawable ancestors,
  records deferred demands and evicts unrequested descendants. A new
  densely packed immutable RHI `BufferType::Index` is **fully
  constructed before publication**, then the prior buffer is released
  through backend RHI deferred/in-flight lifetime handling. A failed
  createBuffer leaves the old resident generation intact; no partial
  GPU page writes or second vertex allocation are possible.
  Per-source `activeBytes` is bounded by `budgetBytes`; older
  in-flight generations can transiently consume *additional* memory
  until RHI reclamation, and total budget across independent sources
  is **not yet global**.
- `StrataView.h/.cpp` accepts an **optional physical-residency mask**
  without editing immutable source assets. A missing child blocks
  refinement at the resident parent; malformed masks are transactional.
  The legacy unmasked `planView` interface still behaves as before.
- `StrataPortableRender.h/.cpp` adds
  `stage(device, source, asset, error, indexBudgetBytes)`.
  `indexBudgetBytes==0` preserves the old eager fully resident Stage
  14 pooled/dedicated path. A positive value switches to
  `IndexResidency`; source VB/IB, images, materials and bind transforms
  remain borrowed. `collectResidencyDemands` computes intended LODs
  against the **complete CPU asset** for every supplied view or all
  leaves for an unknown planar-reflection camera.
  `updateResidency` publishes the new buffer between submission
  epochs. `prepareModel` limits selection to physically resident
  nodes and translates selected node IDs to the compact GPU offset,
  with an atomic source-to-Legacy fallback on any inconsistency.
  Streamed alias does not inherit a pooled `geometryPoolPage` because
  its dedicated IB is not part of the shared page.
- `GeometryScene.h/.cpp`: opt-in
  `setStrataStreamingIndexBudgetBytes(size_t)` **before load**.
  This only matters with `setStrataPortableRenderingEnabled(true)`.
  `updateStrataStreamingBeforeDraw` gathers all source instance,
  camera, sun-shadow and planar-reflection demands and updates each
  source **once before any Renderer3D::drawModel submission**.
  The main, shadow and mirror replays share one selected generation;
  Legacy source/HLOD/surface-proxy ownership remains unchanged.
  New stats: streamed source count, actual active index bytes, total
  evictions and accumulated preparation failures. This API and
  implementation are genre-independent (no RTS or FPS).
- `CMakeLists.txt`: adds `StrataIndexResidency.cpp` to the existing
  portable library and the `aiverse_strata_index_residency_tests`
  executable/CTest named `strata_index_residency`.

### Tests authored but NOT executed

- `tests/render/strata_index_residency.cpp`: recording RHI for
  Metal, Vulkan, GLES3 and GL33; root admission and rejected-too-small
  budgets; sorted priority demands; all-sibling group promotion;
  unchanged generation (no upload); eviction and root fallback;
  atomic failure on RHI allocation; malformed demands; device loss
  and wrong-device shutdown. Records one live compact IB only.
- `tests/render/strata_view.cpp`: masked root-only, single missing
  sibling, all resident, invalid mask preserves output.
- `tests/render/strata_portable.cpp`: Vulkan/GLES3 streamed source
  root-only initial alias, GPU upload failure, promotion to two leaves,
  eviction to parent and source VB ownership/parity of handles.
- No real Vulkan/GLES3/Metal shader, reflection, shadow, image parity
  or frame-time measurement was performed on this Stage 15 source.

### Concrete commit history for Stage 15

- `a6e5e10b17b715247005e1fb4c7f3694820f7ab9` header, 
  `c3769b371e5e171a0643caef77b7912c84f47d1e` implementation,
  `2d6b8f43890d888aa7a192b361d1560a0e3db5f0` parent validation
- `cf14b6d3790e0a8b6eae735f76a1a797a1e1fd0a` core recording test
- `ebdbe7a2f771ca7fd0cd7a80b8095d4b34fcf2d9` CMake
- `d9b2c739e8e60230f3638af54474eda705a7af10` View API,
  `9c9ed26fcda27aae81395eea02b4f4a1ef91a7f7` view implementation,
  `266651650d7eba01d4c1fa85a0b8580927d3c13f` test
- `a0223512b9902cb9bf5bbe82b5828767cc5a0a59`
  PortableSource header,
  `38a2d5d7a9c0707c6a72f1e1d75b67a919507bc9`
  portable GPU alias integration,
  `723ae6dcc2f070286e4249cd2a5903b2b6bfb63f`
  demand planning/updates,
  `48fbe13beffaf5554b93f804de1aefe89252ff9d`
  read-only residency stats
- `8e96e988bed41cb554e5bbebb2bf3922bb89df3f`
  GeometryScene API,
  `b810d0b0d870af003d551f849138eff3acc1ec19`
  all-instance prepass integration
- `737d1d847cc1878c7e8668a020a8433e99679c68`
  portable streamed-GLES3/Vulkan recording test

### Important boundaries, risks and next agent step

1. This is an **immutable repacking** strategy. It has **actual
   budgeted GPU index-buffer allocations and atomic swaps**, but
   *not* sparse GPU pages, async disk/SSD reads, compressed on-disk
   streaming, page tables, hardware GPU-driven Vulkan selection or
   multi-world aggregate budgets. CPU `ModelAsset` still stores
   the full source index hierarchy.
2. A budget is per source `ModelAsset`, not total scene memory.
   Expose a global residency arbiter in Stage 15.1, add a GPU upload
   bandwidth budget, persistence/hysteresis, and per-backend memory
   diagnostics. Vulkan compute and async sparse page upload should
   remain separate opt-in improvements after real raster validation.
3. Safety relies on updating **before** Renderer3D submits a new
   draw epoch, and on the existing RHI contract retaining in-flight
   objects after `destroyBuffer`. Aliases must not survive source
   shutdown or be used after the next buffer-generation swap.
4. Verify native compilation **in the final combined run**:
   `aiverse_strata_index_residency_tests`,
   `aiverse_strata_portable_tests`,
   `aiverse_strata_view_tests`, builder and all Stage 13 GPU parity
   executables. Build on the owner's Mac Studio; perform exact
   Vulkan/GLES3 image/LOD tests when their native platforms are
   available. Do not claim test PASS from the authored fixtures.
5. The Stage 14 branch is diverged from concurrent main work. Do
   not blindly merge/force; first create an isolated integration
   worktree, reconcile CMake targets and run the final suite.
   Never overwrite other agents' work.

**Next step:** Stage 15.1 global shared budget and asynchronous upload
pressure control, then backend Vulkan compute/GPU-driven selector as
a separate Stage 16. Maintain the one-Renderer3D-draw-per-source
contract and keep default-OFF Legacy compatibility.


---

## Stage 15.1 — scene-wide active GPU index cap (2026-11-19)

**Implemented, committed, NOT compiled/tested**. Build/test remains deferred
to the owner-requested final native pass.

### New API and semantics

`GeometryScene::setStrataStreamingSceneBudgetBytes(size_t)` is an
optional scene-wide aggregate **active index residency** cap.
`setStrataStreamingIndexBudgetBytes(size_t)` remains the per-source
maximum. If the scene cap is positive and the per-source cap is
zero, streaming is automatically enabled; each source cap becomes
the lesser of its full Strata index bytes and the hard 64 MiB/source
safety limit. If both caps are zero, the Stage 14 eager route remains
unchanged. Both settings are configured **before scene load**.

At load, the scene counts each source's complete pinned-root indices
and its possible maximum index bytes, then calls the pure, independent
`strata::apportionSceneIndexBudget` helper. The helper deterministically
sorts by root byte cost and stable source key, admits as many root sets
as possible under severe pressure, and distributes remaining memory
in proportion to admitted-source headroom with integer rounding
remainder. Invalid root or insufficient per-source cap causes
decline. Sources not admitted stay entirely **Legacy** (no partial
stripped surface and no wasted GPU Strata index buffer).

For admitted sources, the sum of their fixed active caps is <= the
scene cap; runtime `IndexResidency` updates cannot exceed these
individual limits. This bounds active Strata index allocations across
multiple models **inside one GeometryScene**, not across separately
constructed worlds, devices, or simultaneously in-flight retired RHI
buffer generations. The latter remain the backend's deferred
reclamation responsibility.

New scene metric: `Stats::strataStreamingBudgetDeclined` (sources
left Legacy because the mandatory root reservation was not granted).

### Changed files / commits

- `engine/render/GeometryScene.h`:
  `253090f99c65a83bb01f8ff41de513cadcc2dc32`
- `engine/render/GeometryScene.cpp` initial inline allocator:
  `c2fd2af484da7be76d57f4834ac8aca29407bf62`;
  factored into independently testable helper:
  `67a5c885444c191e00fac85c48bccb79fc988478`
- `engine/render/StrataIndexResidency.h`:
  `f7171e34004ffdbd616ff4ce7d5e9b38dc65591d`
- `engine/render/StrataIndexResidency.cpp`:
  `7916a9025378807aca62ec3f17e3975062cfbc58`
- `tests/render/strata_index_residency.cpp`:
  `6573c96d26f85e1744673e934a81bce9811f2410`

### Deferred tests and next work

The new **unexecuted** regression covers three competing source
budgets, fair admission/decline, deterministic tie order, scene-wide
cap preservation, tiny-budget Legacy outcomes, unlimited cap mode,
root > per-source max, and `UINT64_MAX` input overflow safety.
All Stage 15 tests remain planned for the combined native build.

**Next**: per-frame upload bandwidth throttling and dirty-page
incremental uploads (avoid complete immutable buffer repacks), a
scene/world-wide active+in-flight physical residency budget,
then actual Vulkan compute selection + final visual/performance parity.
The current manager does not claim async SSD streaming, hardware
sparse bindings or cross-GeometryScene global pooling.


---

## Stage 15.2 — incremental Metal/Vulkan GPU index pages (2026-11-20)

**Repository/branch:** `chromealex/AIVERSE`, `feature/strata-virtual-geometry`.
This is a source-committed functional stage. **NO build, CTest, native GPU
run, shader/raster parity or performance profiling was performed**. The
owner specifically requested feature completion before one final native
build-and-test batch. Do not report this stage as hardware-validated.

### Architecture and implemented API

Stage 15/15.1 used a compact immutable `BufferType::Index` generation
for every residency change. Stage 15.2 adds an **opt-in alternative**:
a single fixed-capacity `gfx::BufferType::Storage`, `Usage::Dynamic`
RHI buffer with **stable indexed node offsets**. Both Metal and Vulkan
expose a checked shared-storage upload and native INDEX-compatible
storage. Each cluster node owns one variable-size, 4-byte-aligned
index subrange ("page") in that buffer. This is a software index arena,
**not** Vulkan sparse binding, MTL sparse textures or on-disk pages.

- `IndexResidency::initialize(..., preferIncremental=true,
  uploadBudgetBytes)` only enables stable pages on **Metal or Vulkan**
  when `canUpdateSharedBuffers()` is true. The default remains
  `preferIncremental=false`, maintaining Stage 15 behavior.
  GLES3/GL33/WebGL2 continue to use the safe immutable-generation path
  even if the caller requests incremental mode. A fixed buffer sized
  to the admitted per-source budget is allocated ONCE, but initialization
  only uploads pinned root node indices. Its buffer is type Storage,
  usable as a regular 32-bit index buffer through existing Renderer3D.
  Source vertex buffer is never duplicated.
- `StrataIndexPages.cpp` implements the private `IncrementalStorage`
  allocator. Retained node addresses are unchanged across all updates.
  Newly demanded children are admitted and uploaded as **complete
  sibling groups**, using the existing descending-priority demand list.
  Entire group deferral occurs if the per-update upload bandwidth cap
  or available arena space is insufficient: no half-frontier, no hole
  in rendering. `0` upload cap is unthrottled; a cap smaller than
  one complete sibling group means that group remains deferred rather
  than silently violating the cap.
- Evicted pages are placed in **quarantine** with
  `Device::frameSerial()`. A retired slot is reused ONLY when
  `completedFrameSerial()!=0` and
  `completedFrameSerial() >= retirementFrame`. No live or in-flight
  page is overwritten. Newly freed slots are not reused within the
  same update, even if the completion counter advances concurrently.
  Unknown/zero retirement serial leaves old nodes drawable instead
  of leaking an un-reclaimable slot. Completed adjacent free ranges
  are coalesced for first-fit allocation, then new tail space is used.
- Every source updates its pages **before** any Renderer3D draw
  submissions for that epoch. All planned GPU uploads target fresh
  or fully fence-completed storage. The draft page table, used/free
  ranges, counters and pending evictions are atomically published
  only after every required `updateSharedBufferChecked` succeeds.
  On a partial upload failure, only previously unreferenced GPU bytes
  may have changed; the visible page table and existing resident
  parent remain unchanged. Renderer3D uses exactly one source ModelDraw
  with the original source VB and the same stable Storage/INDEX handle.
- `GeometryScene` exposes:
  `setStrataIncrementalIndexStreamingEnabled(bool)` and
  `setStrataStreamingUploadBudgetBytes(size_t)`.
  Set both before `load/loadPrepared`, alongside the existing
  `setStrataPortableRenderingEnabled(true)` and a positive
  per-source or scene index budget. Default incremental mode is OFF.
  The upload cap is **per source per prepass invocation**, not a global
  multi-world GPU bandwidth throttle.
- `IndexResidencyStats`: added
  `incremental`, `arenaBytes`, `uploadedBytes`, `uploadCalls`,
  `uploadBudgetDeferrals`, `fenceReuseDeferrals`.
  `activeBytes` remains the sum of *resident* node index bytes;
  `arenaBytes` is the **fixed capacity reserved on GPU** when the
  incremental path is active. These are not physical VRAM telemetry.
  `GeometryScene::Stats` exposes aggregate counters, plus the count
  of actually incremental sources; Vulkan may internally retain
  multiple buffer versions and CPU shadow data.

### Files and exact implementation commits

- `engine/render/StrataIndexResidency.h`:
  `5fd444052192a1e9181228de3b8242d47259e4aa`,
  `b16b07777cf92b766a6327dd069bb5d202920f60`,
  `164f8ccc51aea2c329eb3408ee0c09a6e2cfb38a`,
  `f9b9d7ad3d46d27388a7756e935b8c269960392d`.
- `engine/render/StrataIndexResidency.cpp`:
  `de9a99aca842c5b0a447a561e186627978455257`,
  `1574fd4786e0fa2b826117c6b36ab95040d48552`,
  `2db70a03816655fdf4badd890963edf8a5cc2d5b`.
- **New** `engine/render/StrataIndexPages.cpp`:
  `b70093d62190b658eea33ebeb39aab0f28cb5476`;
  fixes `bbaefa700c5d6cee711014d6e3e915ee717ed1e7`,
  `6146898e5d3d0353aa720713df3ce38003e759f9`,
  `bf1e60f221610de7757727e18ef6f5cd2ebae9ad`.
- `engine/render/StrataPortableRender.h/.cpp`:
  `f7a52e2e949766f2611e220d02e39fb2204b8351`,
  `63739fb676ad428658aec3d1ebce9000dc5ee8bd`.
- `engine/render/GeometryScene.h/.cpp`:
  `86e7e85f320a2e3c0307d993be55e0e2fea69b96`,
  `3b4276a4cc62052b6798cd533c90bf7dedd31a13`.
- `CMakeLists.txt`: `a9f09aa2ff32520aad90ccbb1c623b9eb0af637e`
  adds the new library source; `10667ba1bee20e1b8ea58fad1a9c17e6644ad6ae`
  registers `aiverse_strata_index_pages_tests` /
  `strata_index_pages`.
- **New** `tests/render/strata_index_pages.cpp`:
  `9c7e9353350b9d8f191509d650f130b997e00e1b`,
  `727e298a4a46fffd7612281ff70bad93010e2341`,
  `0b174fc8f522e41a462f472398ae078c63ce8769`.
- `tests/render/strata_portable.cpp`:
  `384a1ccea995fb2a61e5b4f622165c1a3c1fc108`.

### Authored tests; none executed

`strata_index_pages` recording RHI verifies (for Metal, Vulkan and
immutable GLES/GL33 fallback): root-only allocation, constant handle
and index offsets, no uploads on unchanged demand, complete sibling
promotion with limited bandwidth, atomic first/second child upload
failure, valid ancestor after deferral, retirement quarantine,
no reuse while completion fence is behind, successful reuse after
completion, unsupported GPU write epoch, and unknown frame serial.
`strata_portable` exercises the actual Model alias with incremental
metal/Vulkan staged index offsets and preexisting source VB identity.
The test target exists in CMake, but **zero pass/fail evidence** is
available yet. Existing Stage 13 actual Metal reflection parity is
a separate older test and does not verify this source change.

### Known limitations / continuation

1. Allocation is a **fixed-capacity per-source arena**; backing storage
   is fully allocated, even when only roots are active. "GPU memory
   saved" must refer to *active* node residency, not reserved arena
   bytes. The scene cap now bounds the sum of these reserved capacities;
   the Vulkan RHI may additionally retain internal versions/copies.
2. The checked Metal/Vulkan uploads transfer only newly requested node
   payloads through the RHI API, but Vulkan's dynamic-buffer version
   preservation may copy its entire CPU shadow to a new mapped GPU
   buffer when an earlier version is in flight. Do not claim a measured
   reduction in physical transfer bandwidth before profiling.
3. Retired fragmentation and delayed completion can stall fine-detail
   promotion despite nominal free *active* capacity. Source/parent
   fallback remains correct, but there is **no separate compaction or
   page-move job** yet. For now there is no GPU page table for
   mesh-shader indirect draws and no disk/IO loader.
4. Incremental mode cannot overwrite any live range. The update must
   occur before Renderer3D ModelDraw submission, and an alias must not
   outlive that draw epoch or source device. A manual call to
   `IndexResidency::update` from inside an active encoder is invalid.
5. The feature branch remains divergent from concurrent `main`;
   preserve other agents' CMake and render changes before integration.
   **Do not merge into main** until a clean combined-source build and
   focused + hardware CTests PASS.

**Next bounded stage:** 15.3 asynchronous content-addressed on-disk
index-page IO, page upload staging/visibility deadlines and a real
global per-frame bandwidth governor. Later Stage 16 Vulkan compute
selection; Stage 17 crack-free LOD; final Metal/Vulkan/GLES3 acceptance.
Append a fresh handoff with exact test results after each stage.


---

## Stage 15.3 — asynchronous disk-backed STRATA index pages (2026-11-20)

**Branch:** `feature/strata-virtual-geometry` (NOT merged to main).
**State:** source code, CMake targets, and recording-RHI test fixtures
committed. **No native compilation/CTest, Metal GPU run, Vulkan raster
or GLES hardware run has been performed on this Stage 15.3 code**.
Do not reinterpret previous Stage 13 Metal reflection PASS as a
Stage 15.3 acceptance result.

### Implemented architecture

- **Offline archive format:** `engine/render/StrataDiskPages.h/.cpp`
  adds `DiskIndexPages::writeArchive(path, const ModelAsset&)`.
  Binary `.strataidx` contains a little-endian fixed-size header
  (magic `STRIDX03`, version 1, page count, source fingerprint,
  directory/payload offsets), a fixed 32-byte entry per
  `(primitive ordinal, cluster node ID)` and all node-local
  `uint32_t` index payloads. An FNV-1a 64-bit fingerprint verifies
  the staged source geometry and topology at open, and an independent
  FNV-1a checksum verifies every page after read. This is *accidental
  corruption detection*, **NOT cryptographic signing/authenticity**
  of untrusted asset packages. Archive maximum: 256 MiB and 2M
  node entries; the writer publishes via a temporary file and
  filesystem rename, never a partially written final destination.
  On platforms where rename cannot replace an existing file, export
  returns a failure rather than deleting a valid archive.
- **Stable source filenames:** `DiskIndexPages::archiveFilename(key)`
  computes a deterministic hexadecimal key hash and returns a single
  filename `strata-<hash>.strataidx`; no model path fragments become
  filesystem traversal components. The header fingerprint prevents
  silent cross-source reuse of an archive even in a key-hash collision.
  `SceneStaging::writeDiskArchives(directory, failures)` prepares
  all staged source archives for editor/import-time use.
- **Asynchronous runtime I/O:** `DiskIndexPages::open` validates the
  directory/header, complete entry ranges and source signature,
  then creates one native C++ worker thread per archive. `request`,
  `ready`, `failed`, `consume` and `retainRequested` form a
  bounded nonblocking render-thread interface. A producer queue is
  bounded by request count (default 512) and ready CPU payload bytes
  (runtime `GeometryScene` uses the source index budget). A worker
  waits without busy-spinning if CPU ready-cache memory is exhausted.
  `retainRequested` prunes obsolete queued/ready pages when the
  camera or shadow requirements change; an already executing read
  is allowed to finish and its result is discarded when not needed.
  `close()` stops, wakes and joins the worker before source/scene
  shutdown. Disk I/O is disabled for Emscripten/WebGL2.
- **Pinned roots are synchronous only during scene initialization.**
  `readRoot` checks the checksum before a root is submitted to the
  GPU and returns failure/Legacy if a mandatory root is absent or
  damaged. All child pages use asynchronous requests; **no file I/O
  wait is introduced in `GeometryScene::draw`**.
- **GPU integration:** `IndexResidency::initialize` takes an
  optional shared `DiskIndexPages` only in the checked, incremental
  Metal/Vulkan mode. `StrataIndexPages.cpp` requests all siblings
  of a selected hierarchy parent as a complete set. It uses the
  verified *disk-read* index bytes for GPU upload, not a CPU copy
  of `ModelAsset::indices`. If one child is missing, queued, corrupt
  or GPU upload fails, that branch remains on its resident parent.
  The GPU-visible node page table is published only after every
  required checked RHI write succeeds, then the CPU ready cache is
  consumed. A camera move discards obsolete disk requests. The
  source Model VB, textures, shadow/reflection passes and single
  Renderer3D draw authority are unchanged.
- **Scene integration:** `GeometryScene::setStrataDiskPageDirectory(dir)`
  is opt-in, before `load/loadPrepared`. It applies only with
  portable STRATA, a positive source/scene budget, and Metal/Vulkan
  incremental index streaming enabled; GLES3/GL33 retain their
  existing CPU-selected immutable fallback. On Metal/Vulkan an
  absent/mismatched/corrupt archive leaves that particular source
  on full **Legacy** instead of crashing or silently rendering a
  partial geometry surface.
- **Aggregate per-RHI-frame transfer budget:**
  `GeometryScene::setStrataStreamingFrameUploadBudgetBytes(bytes)`
  adds an optional cap (0 = unlimited) across Metal/Vulkan
  incremental source updates. The order of source processing is
  stable-key + round-robin first-source rotation each draw prepass;
  the allowance persists while `Device::frameSerial()` is unchanged
  and resets for the next serial. If a device returns serial 0,
  the cap safely applies per draw prepass (not strictly per GPU frame).
  `IndexResidency::update(..., uploadAllowanceBytes)` applies
  the minimum of this allowance and the older per-source cap.
  The new `gpuTransferBytes` counter counts **successful checked
  RHI transfers even when a later child write in the same group
  fails**, preventing frame-budget overshoot across retries.
  This governor applies to runtime *incremental* uploads; synchronous
  pinned root uploads at scene load, disk I/O bytes and GLES3 immutable
  replacement buffers are not included.
- **Diagnostics:** `IndexResidencyStats` tracks verified disk reads,
  failed reads, queued pages, ready bytes and actual submitted GPU
  bytes. `GeometryScene::Stats` exposes disk-enabled/declined
  sources, disk bytes/errors/queued/ready, plus GPU transfer counts.

### Public APIs

```cpp
// Editor/offline CPU import:
strata::SceneStaging staging;
staging.stage(sourceKey, cpuModel);
std::vector<std::string> errors;
bool packed=staging.writeDiskArchives(assetDirectory, &errors);

// Runtime, before load:
scene.setStrataPortableRenderingEnabled(true);
scene.setStrataStreamingSceneBudgetBytes(8 * 1024 * 1024);
scene.setStrataIncrementalIndexStreamingEnabled(true);
scene.setStrataStreamingUploadBudgetBytes(128 * 1024); // per source
scene.setStrataStreamingFrameUploadBudgetBytes(256 * 1024); // scene total
scene.setStrataDiskPageDirectory(assetDirectory);
scene.load(device, json, fileReader);
```

### Changed files / code commits

- `engine/render/StrataDiskPages.h`:
  `42f452daec4490f0140874990a4be620feffb39e`,
  `670fa9e682e4857c373f80d0ca6151694d89caa3`.
- `engine/render/StrataDiskPages.cpp`:
  `cdf86786403be4533b265c61ea1a621f3c07b879`,
  `cec96a3c5ea568d0d82bbf4c0999a0be55a689e1`,
  `2064959c8e428763b91cac9d3f7a4ccf8da1a2f6`,
  `b8cba13dc398b7977122f221b7baae69e462e362`,
  `bacc469369ec40902c45a9582accf0eb9f127458`,
  `d53c797bb872d4ba9b28977437df05f8cc3e691b`,
  `50e42467a6b4f16ebfc1cb61fbc5161d70c9ac35`.
- `engine/render/StrataModelAdapter.h`:
  `c8f131788877e3a0325981aa75a04ed7eee63c91`.
- `engine/render/StrataIndexResidency.h/.cpp`:
  `6656257f2c8666ac1788a3c773c8ad3daafc2ba7`,
  `2f2501942f77e15550dc9e397434bf4d43a7891f`,
  `fdd3f76df2c590f5ad32f67ede53e20815fc94bf`,
  `8250f1947e62d16ba4737454fb0a4cea30af13d1`,
  `844ca4181562ef3fd351529e759f65d3a7fb29be`.
- `engine/render/StrataIndexPages.cpp`:
  `a74ffc2f098ef6183e008d0d97a713453bf81b55`,
  `1206a2680f508299545639fe2fc9f8a627cd6fc0`,
  `cc757d274125caf7f9f7a8065d026a321492ab36`,
  `00fdbe3c6643a71eb99a2f19a0f5797e259c2342`,
  `965c2db869df9b46bce920cce72f8353fbfd8ecf`.
- `engine/render/StrataPortableRender.h/.cpp`:
  `1564cdc4022dde204f1b07aeedf61d1c15bad3b1`,
  `cfa6b53941f55a46e17ef3fc2cae1ecf0c5b3d11`,
  `9e97c0b492ff6be76ff00dd3d723499d6074c095`,
  `46415cf65af6ad378f8c314fd01702d98a317e16`.
- `engine/render/GeometryScene.h/.cpp`:
  `a57910f98be351c43e71f4771e9e9c28b0f289c0`,
  `e922469a419267820addccbcb86ca8cb61d66b3b`,
  `3e51f64d5f508f5e8d9d0b34622308710dc2a7e4`,
  `b2471d03b1998d77baa77780f8da5054d208b698`,
  `89714f627a6b63bdefb80c4aee9e839424abff64`.
- `CMakeLists.txt`:
  `78504b6b84f7131fb2e5df16210e2689b411ba32`,
  `b54995619ae66f92dc2d06520453c364cb68fa84`.
- New tests `tests/render/strata_disk_pages.cpp`:
  `378898cc6f5623fcf4311490a453805ac1bdf050`.
  `tests/render/strata_index_pages.cpp`:
  `de3f3aa0eeb3662cd25e3fef7515f92497894832`.

### Tests authored (NOT executed)

`aiverse_strata_disk_pages_tests` (CTest `strata_disk_pages`)
covers archive roundtrip and safe key naming, metadata/root checksum,
bounded queue and duplicate demand suppression, camera-change queue
pruning, root sync loading, asynchronous leaf disk-to-GPU page
promotion, unchanged RHI handle, CPU cache consumption, stale source
fingerprint, leaf corruption and invalid header. Existing
`aiverse_strata_index_pages_tests` now covers the cross-source
upload allowance. All should be compiled with native C++ filesystem
and Threads::Threads on macOS; no tests were run here.

### Current completion table

| Workstream | Code status | Validation status |
| --- | --- | --- |
| Disk page archive writer/manifest | Implemented | NOT RUN |
| Bounded async reads/cancel/cache | Implemented | NOT RUN |
| Disk-index payload to GPU arena | Implemented | NOT RUN |
| Scene disk fallback and resource lifetime | Implemented | NOT RUN |
| Aggregate per-RHI-frame GPU upload budget | Implemented | NOT RUN |
| GLES3 existing CPU fallback | Retained | NOT RUN on new tree |
| Release of full CPU ModelAsset index arrays | NOT IMPLEMENTED | N/A |
| Shared cross-world/global physical VRAM residency | NOT IMPLEMENTED | N/A |
| Hardware sparse pages/Vulkan GPU compute | NOT IMPLEMENTED | N/A |
| Crack-free lossy LOD parity | NOT IMPLEMENTED | N/A |

### Limits and next agent steps

**The live CPU ModelAsset still contains the complete original index
array.** The disk archive provides real asynchronous *disk byte reads
feeding GPU uploads* but currently does **not** save that CPU memory.
`planView`, import validation and other diagnostic paths still inspect
the in-memory index range. Eliminate this duplication only after
introducing index-count-only immutable metadata with identical failure
semantics and rewriting all CPU planners that rely on `indices.size()`.

One native worker is created **per disk-backed source**; queue/ready
cache limits are per source, not a global I/O thread or memory pool.
Stage 15.4 should centralize worker scheduling and IO cache, expose
lifetime-safe world ownership and enforce global active+in-flight
VRAM budgets. Do not confuse FNV with authenticated asset signatures.

Validate the combined updated code once in a clean isolated worktree:
`aiverse_strata_disk_pages_tests`,
`aiverse_strata_index_pages_tests`,
`aiverse_strata_index_residency_tests`,
`aiverse_strata_portable_tests`,
`aiverse_strata_view_tests`, and the existing native Stage 13 Metal
image parity tests. Then verify real GLES3 fallback and Vulkan
raster/reflect/shadow on supported hardware; do not claim success
until a real device log identifies the tested SHA.
Stay on the feature branch: concurrent `main` render/CMake work is
still divergent and must be integrated without forced updates.


### Stage 15.3 post-review build isolation

The disk archive writer also needs to be callable by standalone
native editor/import tools without a full `Renderer3D` link.
`CMakeLists.txt` commit
`a3cc4297c26a2b44d8f3879628c64f6662131dd0`
therefore moves `StrataDiskPages.cpp` into a separate
`aiverse_strata_disk` STATIC target (PUBLIC dependency:
`aiverse_strata_model_adapter`, native `Threads::Threads`).
`aiverse_strata_portable` links the disk library PUBLIC, while
GLES/GL33 still use the established non-disk runtime fallback.
Offline importers should explicitly link `aiverse_strata_disk`.
**Neither target has been compiled in this stage.**


### Stage 15.3 final documentation linkage

The agent entrypoint was updated in `AGENTS.md` commit
`8df61ea66cfaed5d95c669196da891637733ccda`
to point future agents to this handoff and the exact incomplete
hardware-validation gates. The public system documentation was
updated in `docs/strata.md` commit
`b4d2ebb8f666576b666a328078aa4b150608ee0b`.
All source changes remain **uncompiled/unexecuted** at this stage.


---

## Stage 15.4 — multi-world GPU/CPU index budgets, shared disk IO and optional CPU index release (2026-11-20)

**Branch:** `feature/strata-virtual-geometry`; no merge to `main`.
**Evidence:** changes below are committed source and authored regression fixtures;
**NO native compile, CTest, GPU raster run or performance benchmark was
performed** in Stage 15.4. Do not claim full VRAM/in-flight global
budgeting or production readiness from the authored code.

### Architecture and implemented APIs

1. **Device-wide index allocation ledger**:
   `engine/render/StrataStreamingCoordinator.h/.cpp` exposes
   `configureDeviceIndexBudget(device,limitBytes)`,
   `reserveDeviceIndexBytes(device,bytes,token)`,
   `releaseDeviceIndexBytes(device,token)`,
   `deviceIndexBudgetStats(device)` and
   `clearIdleDeviceIndexBudget(device)`. Tokens are globally
   monotonically assigned, so a stale duplicate release cannot free a
   later lease after an earlier world's teardown. The ledger is keyed
   by a borrowed `gfx::Device*` and protected by a mutex. All
   GeometryScenes on the same Device count the **full admitted Strata
   index capacity** before allocating source GPU sidecars. A rejected
   lease or incompatible concurrently-active budget leaves that source
   on its original Legacy draw route. On scene shutdown, portable
   sources destroy index resources first; then the scene releases its
   tokens, and the last idle device state is discarded. Independent
   devices have separate caps. **Not included:** source Model VB/IB,
   transient backend copies, graphics-driver memory allocations or
   frames that still use retired buffers. This is a logical reserved
   index-arena cap, not a physical VRAM hard ceiling.
2. **Process-wide bounded shared async archive I/O**:
   the same `StrataStreamingCoordinator` owns a configurable
   `SharedDiskIo` executor, default **2 worker threads**, maximum
   8 and bounded 2,048 globally enqueued jobs. Default total
   verified ready-page CPU cache: **64 MiB**; configurable before the
   first archive dispatch with
   `configureSharedDiskIo(workers, readyBytes)`.
   `sharedDiskIoStats()` exposes live workers/queued/active jobs and
   reserved ready bytes. `StrataDiskPages.cpp` no longer creates
   a native thread per source. Each archive has **at most one scheduled
   shared job**: work is rotated through the global queue, while each
   archive retains its own priority/demand queue and bounded cache.
   Verified page storage acquires a shared ready-byte reservation;
   consume, view-change pruning or archive close returns the bytes.
   A source whose own CPU-ready cache is full stops scheduling until
   capacity returns instead of blocking all workers. A worker blocked
   on the *global* quota is woken by release or archive cancellation.
   Jobs own a `shared_ptr<State>`, not a raw DiskIndexPages or
   ModelAsset pointer; shutdown marks cancellation, discards ready
   pages, wakes waiting workers and immediately returns. A late job
   cannot publish into an unloaded source. Emscripten keeps its
   explicit no-native-I/O fallback.
   Global ready bytes are **resident verified payload storage only**;
   transient copies returned by `ready()` and internal file-read
   buffers are not counted in that shared cap.
3. **Device-wide per-frame checked GPU page transfers**:
   `configureDeviceFrameUploadBudget(device,bytes)`,
   `deviceFrameUploadAllowance(device,frameSerial)`, and
   `accountDeviceFrameUpload(device,frameSerial,checkedBytes)`.
   All GeometryScenes on the same GPU Device consume one allowance
   for each nonzero RHI frame serial. The second world opening with
   the same cap **must not reset** the in-progress frame. Conflicting
   nonzero budgets are rejected while active leases exist; if the
   device cannot report a serial (0), further incremental promotions
   are deferred conservatively. The pre-existing per-source and
   per-scene caps still apply as additional minima. Accounting
   includes successful checked writes even when later writes in
   the sibling group fail. Root uploads during scene initialization,
   immutable GLES fallback, implicit Vulkan version/shadow copies and
   filesystem bytes are NOT part of this GPU-write governor.
4. **Opt-in duplicate CPU index payload release**:
   `StrataBuilder.h` records `Asset::archivedIndexCount` and
   `logicalIndexCount()` and can release its `std::vector<uint32_t>`
   with `discardCpuIndexPayload()` while retaining immutable
   hierarchy/node ranges. `StrataView.cpp` now validates against
   logical length, so CPU LOD view planning remains usable with
   disk-resident indices. `SceneStaging::discardArchivedCpuIndices`
   is called by `GeometryScene` ONLY after a full original source
   fingerprint check and complete pinned GPU root upload, only when
   `setStrataDropArchivedCpuIndicesEnabled(true)` is configured before
   scene load, only for Metal/Vulkan disk-backed incremental sources,
   and only while all GPU metadata/diagnostic staging/culling/preview
   switches are off. Normal Legacy and source Model VB/IB/CPU data
   are untouched; GLES3/GL33 never activate this discard.
   `GeometryScene::Stats` reports the released staged STRATA CPU
   index payload bytes and sources. Since archive verification needs
   original indices at load, this is **steady-state** memory saving,
   not import-time peak memory saving. Other STRATA consumers that
   require raw `Asset::indices` must retain data and/or use the
   explicit diagnostic switches. Archive writer runs *before*
   the runtime staged payload is discarded.
5. **Genre-neutral GeometryScene configuration/telemetry**:
   new setters `setStrataGlobalIndexBudgetBytes(bytes)`,
   `setStrataGlobalFrameUploadBudgetBytes(bytes)`,
   `setStrataDropArchivedCpuIndicesEnabled(bool)`.
   Zero device cap inherits an active policy; all setters apply
   before load. Existing per-source/scene budgets, incremental
   streaming and disk directory must also be enabled for archived
   CPU payload release. Stats now include cross-world reserved bytes
   / active leases / declined sources, shared IO worker and ready-cache
   pressure, per-frame uploaded/remaining bytes and CPU index bytes
   freed. All existing STRATA/Legacy draw routing remains exclusively
   owned by Renderer3D, with no FPS/RTS dependencies.

### Exact changed paths / commit checkpoints

- `engine/render/StrataStreamingCoordinator.h`:
  `539ebd9f141a2fed3690710374b01c761a22b40b`,
  `a364d63cfed1b96758aa1dd743f1f9bad5a23b2c`,
  `a9516efc8b051e899aba5736fd26748c173e0e3b`.
- `engine/render/StrataStreamingCoordinator.cpp`:
  `651b37818386dd863a8801e105624bc59b7a931b`,
  `789d01e868f673b45da8f17fb76f93b63d145e96`,
  `8d97deefea6e2cc3104ba75645478db73d581eca`,
  `1397e0dcaca243338caad2faa9c7a67182a775e6`,
  `b0fc3705e0369f8f47d8614a845f1274e413833d`.
- `engine/render/StrataDiskPages.h`: `cc914cf14c005e4c61aa3490d1423b1dd1475635`.
- `engine/render/StrataDiskPages.cpp`:
  `e507f416de15bc1b3518b66093687e2104c1daa3`,
  `6376f3758f14c61c2828236cf81da0ac4e2352bd`,
  `0c7c2b7c00084acd17f67f094e195c10436be7b5`.
- `engine/render/GeometryScene.h`:
  `a750836f4a471290530f3793aa775b92f3702331`,
  `2e71d838bd7333b6c726d3906b346f0610910c24`,
  `a5d439e5e13f835efd2afa750d21d7089aea600b`,
  `b519deada418f6aed18768e8df55c9612f181bd2`.
- `engine/render/GeometryScene.cpp`:
  `87ab221adf5f65aa26d854d61bc27026012f5a04`,
  `8f0f7db507f2ad3467a4d3f81f6b2a3f0c96960e`,
  `437e2578e28c2f32c5c2c07f6cebb06cd7706268`,
  `63b50d0663a9f689da80b85687c6b45f4d406dd8`,
  `c5ec323f4fe77bfab2966b5dfa95811f7710402e`.
- `engine/render/StrataBuilder.h` `264125905be4ba1e80c774d592b4d7ada42f9b22`;
  `StrataView.cpp` `9c306240edc4694beb77017af7cd98e3376b2828`.
- `engine/render/StrataModelAdapter.h/.cpp`:
  `ce24a3bdbec2fcae51b78a2b817a3a43c80b3dd1`,
  `e69218ae55ab43db3aee4ea97fbbd0de205f0dd6`.
- `CMakeLists.txt`:
  `9b72c514e15c2ca7584fb7b33e0c203df82dd64b`,
  `a91b3357837090e24a7094238b0e5721de80da39`.
- New `tests/render/strata_streaming_coordinator.cpp`:
  `979ebf0415f896ff908e7994ea2dc751d9f81e1d`,
  `af85c31fa64de8898f574d0d7a81d1c9d78578ac`.
- Existing tests extended: `strata_view.cpp` `e977ee5533cb3fbc88420ea6edbe2f6549348a9e`;
  `strata_model_adapter.cpp` `e18b69cef160d74946bf86b5f510510255c20440`;
  `strata_disk_pages.cpp` `1cbbdbf447f3d93fd5d74ca70e4e5b3b0255c307`.

### Authored final-run verification (NONE executed)

`aiverse_strata_streaming_coordinator_tests` verifies two worlds
sharing one GPU budget, admission/decline/release/double-release
safety, independent Device budgets, conflict detection, per-frame
upload exhaustion/reset, and 24 simultaneous archive instances
served by just 2 global IO workers with released global CPU-ready
cache accounting. Extended `strata_view` and
`strata_model_adapter` regression cases check that the archived CPU
index vector can be released without invalidating node-range
planning. `strata_disk_pages` additionally verifies that a copied
runtime ModelAsset can drop its CPU index payload while actual
background page reads still feed GPU residency and view planning.
The existing Strata builder, portable, Metal reflection and
Vulkan/GLES3 fallback targets remain unrun on this combined branch.

### Stage 15.4 completion tracking

| Component | Code | Hardware/test evidence |
| --- | --- | --- |
| Shared bounded IO worker pool | Implemented | NOT COMPILED / NOT RUN |
| Shared CPU ready-cache budget | Implemented (excludes ephemeral read copies) | NOT RUN |
| GPU index logical reservation across worlds on one Device | Implemented | NOT RUN |
| Checked GPU upload cap across worlds per RHI frame | Implemented | NOT RUN |
| Optional archived staged CPU index payload release | Implemented | NOT RUN |
| Full original Model CPU/index data release | NOT DONE (Legacy retained) | N/A |
| Exact physical VRAM incl. in-flight backend buffers | NOT DONE | N/A |
| Cross-device / entire physical GPU budget | NOT DONE | N/A |
| Archive IO dedupe between models/worlds sharing identical content | NOT DONE | N/A |
| Vulkan compute selection / crack-free lossy LOD | NOT DONE | N/A |

**Next bounded step:** verify the new CMake targets and focused CPU tests
in the owner's *final* clean isolated build, then integrate main
without clobbering other agents and run real Metal/Vulkan/GLES3 parity.
Further functionality: physical backend allocation telemetry including
in-flight bytes, cross-world page cache deduplication by archive
fingerprint, and GPU-driven Vulkan Stage 16. Record exact source
SHA and real results; do not label any work as passed until then.


### Stage 15.4 final audit notes

After the initial handoff:
- Device-global upload frame limit was connected in `GeometryScene`
  commit `63b50d0663a9f689da80b85687c6b45f4d406dd8`.
- `clearIdleDeviceIndexBudget()` and its scene-shutdown hook were
  added in `a9516efc8b051e899aba5736fd26748c173e0e3b`,
  `b0fc3705e0369f8f47d8614a845f1274e413833d` and
  `c5ec323f4fe77bfab2966b5dfa95811f7710402e`.
- Exception handling for a failed worker read/allocation was recorded
  in `StrataDiskPages.cpp` commit
  `0c7c2b7c00084acd17f67f094e195c10436be7b5`.
- Public `docs/strata.md` records the new configuration at
  `bb602302c5c8042db416c56e69d5ddd915b2f64e`.
  Agent routing was updated in `AGENTS.md` commit
  `439dbbd5ae4f0b9eafe2427559e8d860536cd2b3`.

**Remaining correctness/performance caveat:** a global ready-cache cap
counts individually finished pages, not atomically reserved sibling
groups. If global cache pressure admits many partial sibling sets
from many worlds, LOD promotion can remain deferred until another
view changes or ready pages are consumed. Parents remain drawable,
but stage 16/streaming tuning needs grouped cache admission or
priority-aware eviction to guarantee forward progress under sustained
pressure. The aggregate GPU-index cap counts only reserved source
arena bytes and NOT real RHI in-flight memory copies. No native
acceptance was conducted here. Keep the PR Draft.


---

## Stage 16.1 — Vulkan GPU compute RHI and STRATA selection (2026-11-20)

**Branch:** `feature/strata-virtual-geometry` (not `main`).
**Validation:** this stage authored code and regression tests ONLY;
**no native CMake, CTest, Vulkan driver or GPU capture has been run**.
The owner requested one final combined test run when functionality is
complete. Do not report Vulkan hardware parity PASS.

### Completed implementation

- `engine/gfx/vulkan/VkApi.h`: loads Vulkan 1.0
  `vkCreateComputePipelines`, `vkCmdDispatch`, and
  `vkCmdPushConstants`.
- `VkShaderCompiler.h/.cpp`: standalone
  `compileComputeProgram(glsl450,spirv,error)` compiles full GLSL450
  compute into SPIR-V using the existing glslang library. Unlike
  vertex/fragment translation, this API preserves std430 bindings,
  explicit descriptor layouts, and push constants unchanged.
- **New `engine/gfx/vulkan/VkCompute.cpp`**: Vulkan
  `gfx::Device` compute overrides, explicit descriptor set storage
  slots 0..7 subject to real device limits, <=128-byte push constants
  via uniforms slot8, 64-wide X workgroups, checked dispatch return,
  TRANSFER->COMPUTE and COMPUTE->DRAW/TRANSFER barriers, fence-gated
  asynchronous `readBufferAsync` status diagnostics. Descriptor
  pools are reset only after their frame-slot fence completes; GPU
  readback callbacks occur on the device thread after completion,
  not synchronously during rendering.
- `VkDevice.h` and `GfxVulkan.cpp`: compute availability only for
  graphics queues which also support compute, with 64-wide workgroup
  and >=128 push constant limits. Compute resources are destroyed
  at Device teardown; completed-slot readbacks delivered in
  `prepareSlot()`. `VkResources.cpp` adds TRANSFER_SRC to buffer
  usages and STORAGE to GPU-authored indirect arguments **while
  preserving original Storage vertex+index pooled usage**.
  `Gfx.h`: new `dispatchChecked()` virtual helper, backwards
  compatible with other RHI implementations. `CMakeLists.txt`
  compiles `VkCompute.cpp` only under `AIVERSE_VULKAN`.
- **New `StrataVulkanCullingShaders.h`**: GLSL450 compute traversal
  reads exactly the existing version-2 `GpuMetadataHeader` (32
  bytes), `GpuPrimitive` (32), `GpuNode` (64), and instance
  (80). One 64-thread compute dispatch visits per-primitive roots,
  derives projected LOD error, frustum-tests nodes and emits the same
  32-byte `GpuSelectedCluster` records plus status counters as
  Metal. Bounded 128-entry local stack, capacity and invalid-metadata
  signals force fail-closed behavior.
- `StrataGpuCulling.cpp` picks MSL for Metal or GLSL for Vulkan,
  uses checked compute, and allows diagnostic async readback on
  both. `GpuMetadataScene`'s already existing Vulkan metadata/index
  staging can now be used when Vulkan compute exists. CPU
  `compareGpuSelection` remains authoritative for reference parity.
  GLES3/GL33 never opt into compute or GPU indirect.

### Stage 16.1 commits

- `VkApi.h` `b443ae614f5c1ed9b43b451e9351209c95822ee9`
- `VkShaderCompiler.h/.cpp` `9107cdc69b022d1b2430a38744553b8bc9406fa6`,
  `5254680da60439624a62edad9d3ec6b190d4e010`
- `Gfx.h` `6dff4bef156fec8bce0bf48172cf607517a97c3f`
- `VkDevice.h` `1b607a7ebd13c8b22a835d1c185d1a0002295d60`,
  `43ca0860cb1dca0a513128c6d4328b429532f9b9`,
  `e5479b5333a22f6dd8d58158f416c08e71f54ffc`,
  `8e2f601a993c401c2f355ffbe4ad99369163e38a`
- New `VkCompute.cpp` `3234b077497d3635a609a035d7756cf590595777`
- `VkResources.cpp` `b84581acd54a64c2080f943177ed45462eb8535e`,
  `f3d8b02180b10e64d4ea26c423aea1ac116ecad8`
- `GfxVulkan.cpp` `571ab15d2c61e211b541e6c9f19ae0707683d06c`
- `CMakeLists.txt` `b48b58d9d138665542e62ef934c084955149b810`
- GLSL selector `04f797dd4d118ac968e74733df7db87e3641abfe`,
  `d2a4ee23d7886767836725737123c02fec29ebf9`
- `StrataGpuCulling.cpp` `8f46b37a28557fbde3583b748668164fb7274d06`
- Recording mock `tests/render/strata_gpu_culling.cpp`:
  `e1956ffa2a38c2d0697848ae8301441bab3613c8`.
- New `tests/render/strata_vulkan_kernel.cpp`:
  `064999c0ee88f363b7507bb7fcdd734aa91d6380`,
  `50bb072411b6cfe184d0fccf2dee9408b5258eea`.
  `CMakeLists.txt` registers optional
  `aiverse_strata_vulkan_kernel_tests`/
  `strata_vulkan_kernel` under
  `AIVERSE_VULKAN=ON,AIVERSE_BUILD_TESTS=ON`
  via `83a3ee739f9a615c430987a0b6ca7bcf1f92d024`.

### Unrun tests / next stage

The headless Vulkan fixture first requires offline GLSL-to-SPIR-V
compilation (even on hosts without Vulkan driver), then if a Vulkan
compute-capable driver exists launches one real source/instance on
GPU, asynchronously reads candidate range/status, compares to the
CPU oracle, and identifies unsupported GPUs as SKIP rather than PASS.
No production Vulkan renderer is switched on in this stage.

| Component | Code | Test status |
| --- | --- | --- |
| Vulkan GLSL compute compilation | Implemented | NOT RUN |
| Vulkan storage descriptor pipeline | Implemented | NOT RUN |
| Vulkan RHI dispatch, barriers, async readback | Implemented | NOT RUN |
| Vulkan STRATA cluster GPU culling | Implemented (diagnostic) | NOT RUN |
| GLES3/Metal legacy selection behavior | Retained | NOT RUN on new tree |
| GPU indirect packet preparation | Stage 16.2 | NOT RUN |
| Renderer3D exclusive production GPU draw routing | NOT IMPLEMENTED | N/A |


---

## Stage 16.2 — GPU-authored Vulkan indexed indirect packets + strict eligibility (2026-11-20)

**Branch:** `feature/strata-virtual-geometry`. **Implemented
source/test code only; NOT COMPILED, NOT TESTED, NOT ENABLED FOR
PRODUCTION DRAW SUBMISSION.** Do not merge to main or claim GPU
raster parity until all required native visual fixtures pass.

### Architecture and implementation

- `StrataVulkanCullingShaders.h` now includes three small Vulkan
  GLSL450 compute kernels:
  **clear** the entire fixed-capacity indirect array to zero draws;
  **select** cluster nodes and GPU-author matched
  `gfx::DrawIndexedIndirectCommand` (20 bytes each; binding4) while
  also writing legacy `GpuSelectedCluster` readback records;
  **safety** reads GPU overflow/invalid counters and either zeros
  every indirect command or emits exactly one full ORIGINAL indexed
  primitive fallback command to slot0 when the caller has passed
  an explicit strict single-material RHI contract. All stages stay
  GPU-resident and do **not** require a synchronous CPU counter read.
  Over-capacity or corrupt metadata never publishes a partial
  production packet as a valid STRATA replacement.
- `StrataGpuCulling.h/.cpp` carries optional
  `GpuSelectionSubmission::indirect`, plus
  `gpuIndirectPrepared()`; dispatching Vulkan additionally allocates
  one `BufferType::Indirect` RHI buffer of up to 2048 slots per
  submission in a completed-serial-guarded frame pool, and executes
  clear/select/safety passes (respectively). Metal remains the
  original GPU-selection diagnostic path, with no indirect buffer.
  The Vulkan `GpuPassConfig` adds `baseVertex` and strictly gated
  fallback fields. Device`::dispatchChecked` rejection leaves the
  caller's original submission unchanged.
- `VkResources.cpp` marks `BufferType::Indirect` as both
  `VK_BUFFER_USAGE_STORAGE_BUFFER_BIT` and
  `VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT`, and leaves
  `BufferType::Storage` usable for vertex, index and GPU storage.
  `VkDraw.cpp` implements
  `drawIndexedGpuIndirect(arguments,offset)` as an explicit opt-in
  single-command invocation through existing indexed-indirect draw.
  No `Renderer3D` material/shader/transform state is replaced by
  that RHI method.
- New genre-independent
  `StrataVulkanIndirectContract.h/.cpp` exposes
  `prepareVulkanIndirectContract(...)` and
  `validateVulkanIndirectSubmission(...)`. The caller must prove
  exactly one static rigid opaque source primitive/material/node, no
  skin/morph/custom shader/other authored LOD, no active
  sun-shadow/mirror dependent rendering, pooled original VB/IB
  sharing the auxiliary STRATA index storage, nonzero frame serial,
  exact source geometry/material revisions and current GPU handles.
  It binds an ORIGINAL full-primitive indexed fallback (not a
  discard/blank frame) for GPU overflow/invalid. Unknown/multiple
  material or transform sources remain on the existing CPU/Legacy
  route. The contract itself is read-only: it does not submit draws.
- **Important routing boundary**: `Renderer3D::drawModel` still
  submits the original Legacy draw by default. This Stage 16.2
  produces a hardware-consumable GPU indirect *candidate packet*;
  it does **not** yet replace the actual Renderer3D raster pass
  because material state, per-command transforms, shadows,
  reflections and real pixel parity must be proven first. This is
  deliberately conservative, not a "finished Nanite" claim.

### Commits / modified files

- Vulkan GLSL: `StrataVulkanCullingShaders.h`
  `a060861f30f7dd54586bb806274bef4d158cc5ab`
  (indexed packet output), `62161fbaed5eca16817dfde834d901840f99c68a`
  (clear), `44c380c7955ff9ab9852686cd09fa395e2439dd6`
  (optional original-primitive fallback on GPU error).
- `StrataGpuCulling.h`
  `4b345f8e9472ea081626542d1f8b58ede2161a5a`,
  `ae6cdcdce1f4a233dc435d873b82280bd0e51d2c`,
  `e0422a9825456cb6e8063941e25477cd50660474`
- `StrataGpuCulling.cpp`
  `9419a4213c6c679f5901eff9287db191c1497d24`,
  `bedb285887405cd73687ca1010a606fc315b7825`
- `VkDevice.h` `8181506519477c0bd7df57c0f82029eb4bb5996a`;
  `VkDraw.cpp` `6de3aed9da988cecbb2afb07c90e582abc972905`
- New `StrataVulkanIndirectContract.h/.cpp`
  `2afa975a1703e2ea4ff52ca6d9955867382608a4`,
  `c2d9f0688c2ab27ee77723c66c695b763c245abb`
- `CMakeLists.txt` `ce324ea1fa60cbce2a389c4718faa6da2f889e06`
  adds eligibility source to `aiverse_strata_render_routing`.
- `tests/render/strata_gpu_culling.cpp`
  `ad6934523e582d26cbf9d83bbb84b39f64aa01a7`:
  3-pass checked Vulkan compute mock, unchanged Metal contract,
  output-buffer type/capacity, no CPU draw submission.
- `tests/render/strata_vulkan_kernel.cpp`
  `fce3a94b65242d3e1913e3208e2d60df43fd0ab2`,
  `da4ee80dec16f0d8ebdd074eb3007846c58d2730`:
  optional real Vulkan GPU indirect buffer readback, empty-slot
  verification, invalid/overflow all-zero safety when no fallback
  is permitted, and source-original command under explicit fallback.
- `tests/render/strata_render_routing.cpp`
  `3a495277b3e3f63055d628e87964feb547fd0d89`:
  reject unsafe alpha, other passes, multiple instances, stale
  revision/handles. It does not claim production raster parity.

### Required final test matrix (still NOT RUN)

- `aiverse_strata_vulkan_kernel_tests`: shader SPIR-V compiler;
  real offscreen Vulkan GPU selection/counters/parity when driver
  available, indirect byte parity and GPU fallback safety.
- `aiverse_strata_gpu_culling_tests`: recording Metal/Vulkan
  three-pass contract and missing-capability rejection.
- `aiverse_strata_render_routing_tests`: strict one-material
  contract, source revision and no-double-draw policy.
- Vulkan RHI regression `aiverse_vulkan_tests`, plus other
  STRATA stages 1–15 (Metal main/shadow/reflection strict parity,
  GLES3 compile/device when available). Run all once user requests
  the combined final verification.

### Next functional stage 16.3 / remaining work

Actual *exclusive* Renderer3D GPU draw authoring for a source requires
binding the same model pipeline, lighting/material uniforms, source
VB/IB and per-instance world transforms as the Legacy drawing branch,
without duplicating its shadow/reflection work. Do not bypass
Renderer3D with an independent raw RHI draw. Initially gate by
single rigid opaque material, one instance, one main view and explicit
parity; then widen to multiple materials/instances, shadows and
reflections. CPU/GPU selection comparison can be done in diagnostics
but not as a synchronous production readback. Until verified, keep
`GeometryScene`'s default-off STRATA GPU route, CPU portable LOD and
Legacy fallback as authoritative.


### Stage 16.2 completion table (source state, NOT test certification)

| Component | Source status | Tests | Remains |
| --- | --- | --- | --- |
| Vulkan GPU-authored indexed packet | Implemented | NOT RUN | Real driver parity |
| GPU clear/select/error safety | Implemented | NOT RUN | Buffer/overflow diagnostics |
| Optional original-mesh GPU fallback | Implemented, strict opt-in | NOT RUN | Real pixel parity |
| Vulkan one-command RHI indirect call | Implemented | NOT RUN | Renderer3D-owned invocation |
| Source/material/frame eligibility gate | Implemented | NOT RUN | Multi-instance and pass support |
| Auto exclusive Renderer3D GPU submission | NOT IMPLEMENTED | N/A | Stage 16.3 |
| Shadow/reflection/material GPU rendering parity | NOT IMPLEMENTED | N/A | Stage 16.3/17 |
| Vulkan/GLES3 final hardware acceptance | NOT RUN | N/A | Combined final test |



---

## Stage 16.3 — exclusive Renderer3D Vulkan GPU-indirect routing, one rigid material (2026-11-20)

**Repository/branch:** `chromealex/AIVERSE`, `feature/strata-virtual-geometry`.
**Status:** Stage 16.3 narrow production-path code and regression fixture
authored and COMMITTED. **No CMake, CTest, real Metal/Vulkan/GLES3
renderer run, screenshots or GPU performance tests were executed.**
Do NOT claim production pixel parity passed. Do NOT merge to `main`.

### What has been implemented

1. **Single existing draw owner, no double geometry**:
   `engine/render/Renderer3D.h` introduces POD
   `GpuIndexedSelectionPacket` appended to `ModelDraw` (preserves
   prior positional aggregate field order), with one GPU-author
   `BufferType::Indirect` handle, source VB/IB, selected instance
   world matrix, RHI frame serial, original model geometry/material
   revisions, source primitive index span/baseVertex and command
   capacity. It is COPIED BY VALUE into a `Renderer3D::ModelItem`
   only for an unbatched source primitive, never a pointer into
   `GeometryScene`. ModelRenderStats adds submitted/rejected/fallback
   counters.
2. **Actual main-view raster consumption**:
   `engine/render/Renderer3DModels.cpp` `renderModels(phase)`
   checks the packet only during the main camera's phase-1 opaque
   pass, with Vulkan + MDI support and healthy RHI, exact current
   frame/Model source VB/IB/revisions, same original primitive
   range and pooled base vertex, bitwise-identical source-to-world
   transform, rigid static opaque material, no morph/skin, no
   baked GI/material palette/author LOD/cutaway/outline/alpha,
   no auxiliary reflection pass, no sun-shadow rendering, and
   no competing GPU model command path. It first calls the
   **existing** `drawModelBatch(...,submit=false)` to bind the
   original model graphics pipeline, lighting/material textures
   and per-instance U0, then consumes all bounded GPU-authored
   command slots through the existing Vulkan
   `Device::drawIndexedIndirect(...)`. On successful RHI
   submission it increments STRATA GPU counters and SKIPS the
   original indexed draw. On any rejection or backend failure
   it retains the same model item in normal `renderModels`
   sorting/Legacy/CPU-MDI fallback. No independent graphics pass,
   shader, material state, VB or alternative draw list is created.
3. **Scene-level prepare once before Renderer3D submission**:
   `GeometryScene::tryStrataVulkanGpuDraw` builds one immutable
   GPU-visible STRATA selection for the camera, using
   `GpuMetadataScene`'s pooled source index lease and the exact
   original source Node 0 bind transform. Only one scene instance
   of the source is admitted. It runs the independent Stage 16.2
   `prepareVulkanIndirectContract` and
   `validateVulkanIndirectSubmission`, checks RHI frame serial,
   and annotates the **same** `ModelDraw`. If GPU packet is ready
   it does NOT also produce a portable CPU STRATA alias.
   If dispatch fails or view/contract is unsuitable, the existing
   portable/Legacy branch runs unchanged. Source remains the
   original Model; its RHI pooled index sidecar and compute buffer
   live until scene shutdown AFTER the draw.
4. **EXPLICIT verification gates, default OFF**:
   `GeometryScene::setStrataVulkanGpuIndirectEnabled(true)` opts in.
   The independent switches
   `setStrataVulkanGpuParityGates(selectionVerified,rasterVerified)`
   must *both* be true, in addition to
   `setStrataStagingEnabled(true)`,
   `setStrataGpuMetadataEnabled(true)`, and
   `setStrataGpuCullingEnabled(true)`, before any source can
   qualify. This uses the existing
   `chooseExclusiveRenderRoute(RenderRouteGate)`. The parity
   switches are MANUAL declarations and do NOT automatically
   validate that the executable/source revision actually passed
   native A/B tests. Until the owner's real pixel tests succeed,
   leave them false in production.
5. **Bounded initial scope and correct fallback**:
   one pooled source, one opaque material, one mesh/primitive/node,
   one instance/world transform and **main camera only**.
   `StrataVulkanIndirectContract.cpp` additionally rejects
   authored shadow-batched primitives, extra source meshes/materials
   and a node with a parent. With sun shadows or planar water
   reflections active, the GPU route does NOT engage — Legacy or
   portable selection draws the COMPLETE usual passes instead.
   GLES3/GL33/Metal are unchanged; Vulkan GPU path remains
   default OFF and never steals their existing render commands.
   Full multi-material/instance shadow/reflection GPU raster is
   still a distinct future step after verified parity.
6. **Statistics**:
   `GeometryScene::Stats` adds
   `strataVulkanGpuCandidates/Prepared/Declined`;
   `ModelRenderStats` separately counts attempted/submitted/
   rejected/fallback GPU RHI draw packets. A GPU-culling result
   contains an unknown triangle count until asynchronous readback;
   the renderer intentionally does not invent that value.

### Exact modified files and commits

- `engine/render/Renderer3D.h`:
  `92809abc569fa9317a2730adf60c428b66df35c5` (POD packet),
  `fba72cb0416c7ba414809cf5b4b405295472c15f`
  (append-only ModelDraw layout compatibility).
- `engine/render/Renderer3DModels.cpp`:
  `183ed1530542129330f5bf6a0919680300e1a047`
  (one draw owner and CPU fallback),
  `18a5ab52fc49c94c3ef14c09e1b8669806d854ae`
  (one primitive/positive base vertex final RHI gate).
- `engine/render/GeometryScene.h`:
  `33b261ee206547955c0c330eed1d736c3a49b92f`
  (default-off scene options and counters).
- `engine/render/GeometryScene.cpp`:
  `2ba2fdc0c1a97f7bd879c912549d316dd7cc7c00`
  (single source selection+safe draw ownership and fallback).
- `engine/render/StrataVulkanIndirectContract.cpp`:
  `fea3d6a453feab4a241bb67a1fb3e402e4829c75`
  (exactly one real source material/mesh and no shadow-batch path).
- `tests/render/strata_render_routing.cpp`:
  `cb5aab5827fd06e6dc5ac1d2a1f3fd15d437564b`
  (POD packet copy and invalid exclusive/handle contracts).

### Unexecuted final verification matrix

| Component | Stage 16.3 code | Actual validation |
| --- | --- | --- |
| Exclusive Vulkan Renderer3D main opaque indirect draw | Written | NOT RUN |
| Renderer3D original PBR material and U0 binding | Reuses existing function | NOT RUN |
| Stale RHI frame/source/mesh/transform fallback | Guards written | NOT RUN |
| Shader output buffer clear/select/safety | Stage 16.2 written | NOT RUN |
| Multi-view, shadows, reflection preservation | GPU route declined; Legacy owns passes | NOT RUN |
| Metal/GLES3 unchanged | No GPU route on these backends | NOT RUN |
| Full multi-material + multi-instance Vulkan GPU raster | NOT DONE | N/A |
| Vulkan source-vs-GPU pixel parity, shadow and reflection parity | NOT VERIFIED | N/A |
| Full CMake + CTest + native Vulkan RHI fixture | NOT EXECUTED | N/A |

Native acceptance needs an actual Vulkan-capable machine
(e.g. Linux Mesa lavapipe for compilation/logic and a Vulkan GPU
for real performance). The existing Stage 16.2
`strata_vulkan_kernel` fixture verifies compute selection/indirect
bytes but does **NOT** verify this Renderer3D raster path.
Add an actual `Renderer3D` legacy-vs-Vulkan-indirect main-camera
RGBA A/B fixture on one pooled opaque source; later widen pass
coverage after Stage 17 LOD seam guarantees. Report the exact tested
commit SHA and failures in this handoff before changing production
defaults.

### Stage 16.3 continuation

Finish a self-contained native Renderer3D pixel-parity runner and
extend the one-source metadata and GPU-indirect packet to multi-material
per-primitive shaders/instance uniforms, then coordinated
per-pass sun shadow/reflection GPU routing (without duplicate
ModelDraw). Do not infer GPU success from SPIR-V compile or
mock-RHI tests; keep parity gates OFF until real screenshots and
timings are reviewed. Finish remaining STRATA Stage 17 crack-free
LOD and Stage 18 performance optimization before the owner's
single final compilation/test pass.

### Stage 16.3 follow-up: validated local source index offset and docs

A follow-up source change avoids re-reading the input mesh arrays after
GPU validation: `GeometryScene.cpp` commit
`9e73ec424ebce3bb4f1c611427db0a55f30b46a7` derives
`ModelDraw::gpuIndexed.sourceFirstIndex` directly from the
already checked `VulkanIndirectContract::originalFirstIndex` and
`Model::gpuFirstIndex`. This preserves the absolute pooled vs
primitive-local index ABI and eliminates a second mutable mesh lookup.
The public documentation was updated in `docs/strata.md` commit
`286b315f8c7b91654fdb1b6f2efc10471da160d7`, and the next agent's
entry point in `AGENTS.md` commit
`4563bef79bb01eda2aab632246df00dcf3df0711`.

**No Stage 16.3 test was executed.** Work remains default OFF with
manual parity gates both false; an indirect argument packet is not a
hardware image parity result.


---

## Stage 16.4a — independent per-material/per-instance Vulkan GPU indirect packets (2026-11-20)

**Branch:** `feature/strata-virtual-geometry`. Source changes and
recording-test assertions authored/committed; **NO native compilation,
CTest, real Vulkan GPU render, framebuffer pixel parity or Metal/GLES
acceptance has occurred**. Stage 16.3 parity switches default to OFF.

### Implemented

- `StrataVulkanIndirectContract.h/.cpp` introduces
  `prepareVulkanIndirectPrimitiveContract(device,source,staged,binding,
  primitiveOrdinal,view,hasShadowOrReflection,out,guarded,error)`.
  Allows one immutable pooled mesh/node with **up to 16 original opaque
  primitives/materials**; verifies *all* source/staged primitive
  identities and original/staged index spans, validates the per-item
  material, and returns a separately guarded **full original primitive**
  fallback command. Unsafe alpha/shader-graph/morph/skin/authored LOD
  sources are rejected entirely. The old
  `prepareVulkanIndirectContract` remains intentionally strict for
  exactly one primitive (source compatibility).
- `Renderer3D.h` appends
  `ModelDraw::gpuIndexedPrimitives` as a vector of small POD packets
  without changing existing aggregate initializer positions. Each
  `GpuIndexedSelectionPacket` carries source mesh/primitive/material
  identity and a per-instance world transform in addition to its
  original Stage16.3 RHI handles/revisions.
- `Renderer3DModels.cpp` copies only the exact matching primitive
  packet by value into its original `ModelItem`; its opaque pass
  still invokes `drawModelBatch(...,submit=false)` for EACH original
  material and U0 world transform before consuming that primitive's
  GPU command array. Explicit CPU material/shadow proxy batching is
  suppressed for packet-bearing ModelDraws, preventing one combined
  palette/material ModelItem from silently replacing separate GPU
  materials. A rejected GPU command falls through to an original
  primitive draw with the same material, avoiding duplicated geometry.
  Source mesh/primitive pointer/material identity and revisions are
  rechecked immediately before the RHI draw.
- `GeometryScene.cpp::tryStrataVulkanGpuDraw` now preflights the
  complete source (all 1–16 primitives) before any GPU dispatch;
  dispatches **one independent selection per primitive and source
  instance** (up to 16 instances/source); and only commits the
  per-ModelDraw packet vector when every primitive GPU selection and
  submission contract succeeds. GPU Culling's existing 64
  submissions/frame bound remains authoritative; if a scene exceeds
  it, source instances automatically fall back to portable/Legacy.
  Each dispatch uses its own objectToWorld, stable instance token,
  material-specific original fallback and GPU command slot.
  No GPU atomic output ordering is used for material state ownership.
  This path is still main-camera opaque ONLY and behind BOTH explicit
  user-held parity switches (OFF by default).
- `tests/render/strata_render_routing.cpp` includes a new
  recording-RHI two-material, one-mesh Vulkan source with independently
  admitted primitive ordinal 0/1, separate GPU packet ownership,
  world transform isolation across instance ModelDraw copies, alpha
  rejection, original-index offsets, model material revision
  invalidation, and old single-primitive API compatibility.
  All assertions are **authored, not executed**.

### Changed files / commits

- `engine/render/StrataVulkanIndirectContract.h`:
  `5cf987f275b9be2c5b16f7132e7bba4d6615724c`.
- `engine/render/StrataVulkanIndirectContract.cpp`:
  `db23cfda1e0699bb4a8dfa3961563948bfb8a86c`.
- `engine/render/Renderer3D.h`:
  `4209f5e370c293bb70f83f2b6f97ddcd8d639a8d`.
- `engine/render/Renderer3DModels.cpp`:
  `e3b70012fa1909b36d981bd0c64d4cdab9924a17`.
- `engine/render/GeometryScene.cpp`:
  `6a92ac87e1fbd17a51f0c1b4d7d0eca18cec0da1`.
- `tests/render/strata_render_routing.cpp`:
  `46bb3fed59506887e78b73eee161960176e37b46`.

### Progress at end of substage 16.4a

| Function | Code | Test evidence |
| --- | --- | --- |
| Multiple independent opaque material GPU packets | Implemented | NOT RUN |
| Up to 16 source instance transforms | Implemented | NOT RUN |
| CPU material/shadow proxy suppression for GPU item | Implemented | NOT RUN |
| GPU command limit and partial source fallback | Implemented | NOT RUN |
| Renderer3D original PBR state per primitive | Reused, code connected | NOT RUN |
| Sun-shadow GPU pass | NOT YET | N/A |
| Planar reflection GPU pass | NOT YET | N/A |
| Full Vulkan pixel parity / performance | NOT VERIFIED | N/A |

**Next:** Stage 16.4b: per-pass GPU shadow selection with
camera-specific commands, then reflections. Must never use main
view's selection for a sun/spot/reflection camera. Preserve all
Metal/GLES3/Legacy ownership. Build/test only at the owner's
requested final acceptance.


---

## Stage 16.4b — sun-shadow camera GPU packets (2026-11-20)

**Branch:** `feature/strata-virtual-geometry`; changes below are source
code and authored regression assertions only, **NOT COMPILED / NOT RUN**.
Manual GPU-selection + raster parity gates are still OFF by default.
Vulkan source/Renderer3D lifetimes and original Legacy fallback remain
authoritative until actual native A/B tests pass.

### Implemented code

- `GpuIndexedSelectionPacket` now includes explicit
  `GpuIndexedPass` (MainOpaque, SunShadow, PlanarReflection),
  exact source view-projection matrix and render-target dimensions.
  `matchesView(pass,viewProjection,width,height)` rejects any
  cross-camera reuse even within the SAME RHI frame. ModelDraw
  appends `gpuShadowPrimitives`; renderer-owned ModelItem stores one
  `gpuShadowIndexed` alongside its independent main GPU command.
  This also applies to per-material and per-instance packets.
- `GeometryScene::tryStrataVulkanGpuDraw` now stages two distinct
  GPU selections for every staged source primitive when the sun
  shadow map is enabled: main `Renderer3D::viewProj()` and the
  current sun `Renderer3D::lightViewProj()` at the actual shadow map
  dimensions. The complete source only commits after all required
  main/sun packets have been GPU-encoded and validated. Dispatch
  quota overflows or any unsafe material/state cause atomic
  portable/Legacy source fallback. Reflection scenes still use
  their entire Legacy/portable route, not main-camera GPU commands.
- `Renderer3D::renderModels(0)` is now allowed to use a prepared
  **SunShadow** GPU indexed packet after verifying the current
  sun projection, shadow-map size, original Model VB/IB/material,
  source/world revisions, castShadow flags and non-auxiliary pass.
  It binds original shadow material/state and submits via the
  existing Vulkan indexed indirect RHI call, then *skips* the
  original shadow primitive on success. Failure falls through to
  the original model shadow draw, never duplicating it. Main opaque
  pass independently uses only its matching main-view packet; when
  shadows are active main GPU admission requires a valid sun
  packet for the same ModelItem.
- Spot shadow's `renderModels(0)` has
  `auxiliaryModelPass_==true`, so it refuses the sun packet and
  renders its correct original view through Legacy. A planar water
  reflection has a different mirrored view and likewise refuses
  main and sun packets. This is explicitly a conservative
  single-sun-shadow production experiment, NOT a claim that all
  shadow/reflection passes are GPU-driven.

### Files and commits

- `engine/render/Renderer3D.h`:
  `f339b22eb6b6bfc3a0050e59f3617d382a644eb1`,
  `31024abeda789c55d339061706dbe65ba3d2dcac`
- `engine/render/Renderer3DModels.cpp`:
  `f4dd3a49069bca997bd7e7d75263cd662521018a`,
  `c93260997bbd01b7402ad0af4acc9437c5349f1a`
- `engine/render/GeometryScene.cpp`:
  `1c98bc8c3abbda2462bb9ce11f444da52178d0d8`
- `tests/render/strata_render_routing.cpp`:
  `1bc942497f9586d33e9cd2ad06d545efd6a8683a`,
  `dc03d6b6e11c6765aa9a2fde72aa88e7c4b322fb`

### Tracking table (no tests were executed)

| Function | Source status | Verification |
| --- | --- | --- |
| Separate sun camera compute selection | Implemented | NOT RUN |
| Per-primitive/per-material sun GPU argument slots | Implemented | NOT RUN |
| Renderer3D exclusive sun shadow indirect | Implemented | NOT RUN |
| Sun/spot/reflection camera mismatch guard | Implemented | NOT RUN |
| Spot shadow GPU compute/indirect | NOT IMPLEMENTED | N/A |
| Planar reflection GPU compute/indirect | NOT IMPLEMENTED | N/A |
| Real sun-depth + main RGBA A/B | TEST NOT EXECUTED | N/A |

**Next:** Stage 16.4c prepare spot-shadow and water-reflection
selections at their actual view-specific frame boundaries, **before**
starting those graphics render passes; never dispatch compute while
inside an active graphics encoder. Keep production opt-in and require
separate pass-specific parity acceptance before enabling the new
routes. Finish original Vulkan/Metal/GLES3 acceptance in Stage 19.


---

## Stage 16.4c — spot-shadow and planar-reflection GPU camera passes (2026-11-20)

**Branch:** `feature/strata-virtual-geometry`. Functional implementation,
recording assertions and handoff committed. **No native CMake, CTest,
actual Metal/Vulkan/GLES3 GPU test, main RGBA, sun/spot depth or planar
reflection RTT pixel-parity acceptance was executed.** Stage 16.4
is not a certified or benchmarked Nanite replacement; all GPU parity
acceptance options remain FALSE by default.

### Production architecture and API

- `GeometryScene::setStrataVulkanGpuAuxiliaryParityGates(
  sunShadowVerified, spotShadowVerified, reflectionVerified)` adds
  **three independent pass-specific manual acceptance switches**,
  all default false. Main `setStrataVulkanGpuParityGates(selection,
  raster)` remains required too. A scene with enabled sun shadows
  declines the GPU source route entirely unless its sun-shadow gate
  is accepted; likewise a scene requesting planar reflections
  declines when the reflection gate is still false. This prevents
  enabling an untested secondary view just by accepting main-view
  pixel parity. A disabled spot-shadow gate causes ONLY spot GPU
  selection to decline; spot retains its original Legacy shadow
  draw. GLES3/GL33/Metal do not enter this Vulkan-only GPU route.
- `GpuIndexedSelectionPacket` includes borrowed
  `GpuClusterCulling*` and GPU v2 metadata handle, stable scene
  instance token, CPU-tested original primitive/material identity,
  LOD threshold and explicit permissions for spot/reflection
  authoring. The pointer is valid only from the scene's
  `draw()` until `Renderer3D::render()` completes. This matches
  the existing borrowed `Model*` lifetime requirement; never
  call `GeometryScene::shutdown` between submit and render.
  `Renderer3D::ModelItem` additionally owns independent
  `gpuSpotShadowIndexed` and `gpuReflectionIndexed` **value
  copies**, in addition to main and sun value copies.
- **New `Renderer3D::prepareStrataAuxiliaryGpuPackets(pass, camera,
  width,height)`** (implementation in Renderer3DModels.cpp) can only
  encode Vulkan compute outside a graphics render pass. Before
  dispatch it verifies live model VB/IB, resource revisions,
  original primitive and material identity, opaque/ridid state,
  no skinned/deformed/GI/alpha/shader graph, exact per-instance
  node world transform and per-pass acceptance flag. Each eligible
  ModelItem gets ONE new independent GPU selection slot with a
  whole-original-primitive GPU fallback command. The method never
  calls any graphics draw; failures leave that ModelItem's
  auxiliary packet empty, so the original renderer item remains
  fully drawable through Legacy.
- `Renderer3DReflect.cpp::renderSpotShadow()` knows the spot
  lightViewProj and spot texture size *after*
  `lights.prepareSpotShadow()`, and now calls the auxiliary
  compute method **BEFORE** `dev_->beginPass(spot)`.
  `Renderer3DReflect.cpp::renderReflection()` computes the real
  water-plane mirroring and reflected view projection first, then
  calls the same method **BEFORE**
  `lights.beginReflection(...)`. This avoids accidentally using
  main-camera or sun-shadow cluster selections for either pass.
  If a reflection plane is absent, no extra GPU work runs.
- `Renderer3DModels.cpp::renderModels(phase)` selects precisely
  `gpuIndexed` for main opaque, `gpuShadowIndexed` for sun,
  `gpuSpotShadowIndexed` for the auxiliary phase0 spot shadow,
  and `gpuReflectionIndexed` for the auxiliary phase1 mirrored
  reflection view. `GpuIndexedSelectionPacket::matchesView()`
  verifies **pass kind, exact GPU viewProjection and dimensions**.
  All original material/light/instance U0 states are bound by
  `drawModelBatch(...,submit=false)`, then one bounded GPU
  `drawIndexedIndirect` submission replaces that original
  ModelItem only if the RHI accepts it. Failed/missing/expired
  packets are regular CPU/Legacy ModelItems, never double-drawn.
  No changes to Vulkan/GLES3/Metal graphics shader semantics.
- `ModelRenderStats` counts successful GPU spot/reflection
  *preparations* separately from actual spot/reflection *draw
  submissions*. GPU triangle counts remain unknown without
  asynchronous readback; do not fabricate them.

### Files and source commits

- `engine/render/Renderer3D.h`: `586141eb86ee22e215880b4929685a5f4c692ee5`
  (explicit spot packet/pass and borrowable selector), plus
  `2855b76dbeb2e034aa4232a473fe08eaf68e72b5`
  (separate per-pass prepared/submitted stats).
- `engine/render/GeometryScene.h`:
  `699935684a35a0fd84cb31bc5ab104128ed7c391`
  (explicit independent pass parity acceptance flags).
- `engine/render/GeometryScene.cpp`:
  `33e57ae439c8d705b54115d45c3bbb68e3bb5a67`
  (independent sun/spot/reflection gates; metadata and selector
  source-specific handles into renderer packets).
- `engine/render/Renderer3DModels.cpp`:
  `7e2e9ff4a85d63423734252b0cceea06a3f471ad`
  (just-in-time auxiliary prepass, camera-unique GPU slots and
  exclusive renderer draw selection),
  `b175f90b9d4add11e846f118a2409fd50735eb45`
  (mutated mesh identity validation),
  `1188bf1a07e1a396349fb121323815167f40d69c`
  (per-pass preparation counters).
- `engine/render/Renderer3DReflect.cpp`:
  `8ea6d96437f76cc31170d693b06690448d5e4a55`
  (spot/reflection compute BEFORE corresponding render pass).
- Recording tests, authored **NOT RUN**:
  `tests/render/strata_gpu_culling.cpp`
  `41e6a1b59b0e8f34347c2645133f239d11dc9116`
  tests unique slots/camera projection for main/sun/mirror dispatch.
  `tests/render/strata_render_routing.cpp`
  `0bd1c9af742cab4f933079898c9fd263e3dce8a4`
  tests strict view/camera identity for spot/reflection packet
  copies versus sun/main.

### Status and outstanding acceptance

| Function | Code | Actual tests |
| --- | --- | --- |
| Independent sun, spot and mirrored reflection GPU selection | Implemented | NOT RUN |
| Just-in-time compute before each auxiliary render encoder | Implemented | NOT RUN |
| Material+world state owned by one Renderer3D ModelItem | Implemented | NOT RUN |
| Per-pass manual acceptance flags and Legacy fallback | Implemented | NOT RUN |
| Multi-material + multi-instance main/shadow/reflection draw | Bounded implementation | NOT RUN |
| Real Vulkan main RGBA A/B and sun/spot DEPTH A/B | NOT VERIFIED | N/A |
| Real Vulkan planar reflection RTT A/B | NOT VERIFIED | N/A |
| Real GPU performance/LOD occlusion/overdraw profiling | NOT VERIFIED | N/A |
| Cracks between lossy neighboring cluster LODs | STAGE 17 REQUIRED | N/A |

**Remaining:** Build an actual native Vulkan Renderer3D raster
A/B harness (not just the earlier compute-byte test), compare
main-color, sun/spot shadow depth and actual planar reflection RTT
pixels across multiple cameras/materials/instances. Keep the
pass-specific acceptance flags false until exact tested source
SHA, driver and screenshot/depth evidence are recorded. Then
Stage 17 crack-free lossy LOD, Stage 18 GPU performance and one
combined final CI/native test pass. Preserve feature branch
isolation; `main` contains concurrent unrelated renderer changes.


### Stage 16.4c additional Vulkan offscreen GPU selection fixture

`tests/render/strata_vulkan_kernel.cpp` commit
`978cd6bf9ae1c017375f03d5b179bc6653194079`
adds independent headless Vulkan compute readback/parity checks for two
different clip projections (spot-shadow-like and reflected camera),
using the same metadata/source model with an independent GPU command
slot each time. The CPU oracle must agree with each projection's own
selected clusters; no unrelated earlier main-view output is accepted.
This tests selection and GPU argument generation, **NOT** actual
`Renderer3D` spot-depth or planar RTT pixel-parity. These new
assertions have NOT been compiled or run in this stage.


---

## Stage 17.1 — exact crack-free welded mesh LOD seam topology (2026-11-20)

**Repository/branch**: `chromealex/AIVERSE`,
`feature/strata-virtual-geometry`. The project is concurrent with
many other branches; **do not merge to main** without the final combined
integration build. **NO actual CMake, CTest, Vulkan/Metal/GLES3 device
test or screenshot/depth parity was executed for this stage.**
Source changes below are committed, **not yet hardware-certified**.

### Problem and conservative implementation

AIVERSE already used `meshopt_SimplifyLockBorder`, but that option
alone did not prove that a hierarchy parent preserved every original
boundary edge segment. Adjacent groups may draw at different LODs;
removing an outer border vertex of one group creates a T-junction
against a neighbor still rendering finer triangles.

The new `engine/render/StrataSeamTopology.h/.cpp` provides two
genre/backend-independent CPU-only routines:

- `sameSeamContour(originalIndices,originalCount,
  candidateIndices,candidateCount,error)` computes the exact
  **oriented topological boundary** of two triangle sets, using
  original **global vertex IDs** (not float-position epsilon).
  Every undirected edge may appear at most once per direction.
  An edge with one forward and one reverse incidence is manifold
  interior; single-incidence edges form the boundary. Duplicate
  same-direction incidence, nonmanifold edges, repeated triangle
  indices, truncated streams, and candidates with missing, split,
  reversed or extra boundary edges fail closed.
- `auditSeamHierarchy(asset,report,error)` independently checks
  every nonempty node index span, its manifold topology, and
  the boundary of each parent's indexed triangles against the
  **UNION of its immediate child triangle streams**. Report
  (`verifiedParents`, `checkedNodes`, `contourEdges`) is
  *transactional*: any malformed input leaves the caller's previous
  output unchanged. The proof is inductive: if all parents exactly
  preserve their children's combined boundary, any mixed-depth
  parent/child frontier preserves the SAME original material
  partition boundary segmentation. The checker must run while
  the indexed CPU triangles are still in memory; Stage 15.4 later
  may discard duplicate CPU payloads after archive/root admission.

`StrataBuilder.cpp` now applies the contour checker to every newly
simplified parent candidate, AFTER meshoptimizer and BEFORE creating
the node. A rejected candidate creates **NO parent**: its original
child IDs are retained in the frontier, source vertex IDs and LOD0
indices are unchanged. A `seamRejectedParents` count records these
failed parent candidates. After complete BFS hierarchy remap, the
builder runs `auditSeamHierarchy`; if it fails it returns false
transactionally, allowing the SceneStaging owner to keep the original
Legacy model. Only on full success does the builder set
`Asset::seamCertified=true` (false on direct/default construction).

`SceneStaging::stage` refuses any un-certified primitive, and
tracks `seamCertifiedPrimitives` and `seamRejectedParents`.
`GeometryScene` exposes corresponding statistics and requires
seam certification at **three production boundaries**:
portable STRATA source load, GPU metadata source load and the
Vulkan GPU indirect scene route. Failure retains the exact Legacy
source/renderer, never a cracked parent. Direct lower-level
`PortableSource::stage` or `GpuMetadataScene::stage` calls made
outside `GeometryScene` are *not* automatically production-certified;
native library callers must explicitly use the new checker or a
builder-certified Asset before opting into such paths.

No extra geometry/skirt shader, duplicated VB, fragment visibility
pass, ECS/world dependency, nonportable Metal technique or
source-material rewrite has been introduced. Both GPU and
CPU-selected LOD paths reuse the exact SAME certified immutable
hierarchy and original global vertex indices.

### Exact changed files / commits

- New `engine/render/StrataSeamTopology.h`:
  `3dd57b5c9391e8e9fd553ab4178b44820bc0d0d3`.
- New `engine/render/StrataSeamTopology.cpp`:
  `305fc9adfa76162f8da55707637c161ee7a0ad04`.
- `engine/render/StrataBuilder.h`:
  `557ada7920c95ad53eb0963dcb85a2f261825aab`
  (`Asset::seamCertified`, `seamRejectedParents`).
- `engine/render/StrataBuilder.cpp`:
  `481ae0fa02ade0501bdb91f16cefdbec6b1f2674`
  (reject unsafe parents before insertion; final audit).
- `CMakeLists.txt`:
  `2ed62a436b0e26706a89ebefc867bb6e8c2bccc3` links checker,
  `89005831a57a49dfc0602c16f5b5edd1277b3b1a` adds
  `aiverse_strata_seams_tests` CTest named `strata_seams`.
- New `tests/render/strata_seams.cpp`:
  `99db90232e8f031db83a0ecfad10b983470795f2`.
- `engine/render/StrataModelAdapter.h`:
  `5a9b5f991db24cb5404a59835a93bc586d4ea43e`.
- `engine/render/StrataModelAdapter.cpp`:
  `a919dbcec0b12facea83f6b53810d6f721ef7ead`.
- `engine/render/GeometryScene.h`:
  `1de26095dac4a3d1dde59d2e187ebf11d5b52428`.
- `engine/render/GeometryScene.cpp`:
  `a6ca36ba71cc0d951212ab29ad49b2737f521582`.

### New tests authored — NOT RUN

`strata_seams` (new CPU CTest) covers exact lossless perimeter
preservation when interior vertices disappear, T-junction due to
removed boundary vertex, reversed perimeter winding, duplicate
nonmanifold oriented edges, degenerate triangles, null/truncated
triangle streams, fully transaction-safe parent hierarchy failure
and invalid child spans, and the full off-line build of a 33×33
welded, wavy square grid. It checks that resulting leaf triangle
coverage equals the original input exactly and every accepted
hierarchy parent passed an independent post-build audit.

`strata_builder`, `strata_view`, disk/index residency, Vulkan
compute/indirect and Renderer3D tests remain part of the **later**
combined native validation. No GPU seam screenshot, wireframe
adjacency A/B, crack histogram or frame-time measurement is
available yet.

### Current Stage 17.1 table

| Function | Source | Tests |
| --- | --- | --- |
| Exact topological boundary matching | IMPLEMENTED | NOT RUN |
| Parent simplification rejected if seam changes | IMPLEMENTED | NOT RUN |
| Independent full hierarchy certification | IMPLEMENTED | NOT RUN |
| Production scene certification gates / Legacy fallback | IMPLEMENTED | NOT RUN |
| Seam rejection and certification stats | IMPLEMENTED | NOT RUN |
| Mixed-LOD welded partition guarantee | By topological construction | NOT HARDWARE VERIFIED |
| Cross-material duplicate-position seams | NOT FULLY CERTIFIED | N/A |
| Preexisting source T-junction repair | NOT IMPLEMENTED | N/A |
| Pixel/depth parity at dynamic mixed LOD | NOT VERIFIED | N/A |

### Scope and next bounded stage

The current proof is **topological by identical original vertex IDs
within ONE material partition**. It does not repair cracks already
present in the imported source geometry, independently authored
meshes/material partitions with duplicated seam vertices, or
different-world terrain chunks whose border coordinates disagree.
It does not guarantee pixel-identical lossy LOD *interior* shading
or hide normal LOD popping; those are separate geomorph/perceptual
problems. For suspect cross-material/mesh borders, Stage 17.2 should
add a source-wide shared-position seam certificate (or decline
unsafe parent LODs) and explicit multi-partition/stitching fixtures.
Only then perform real Metal/Vulkan/GLES3 GPU mixed-level visual
acceptance and Stage18 profiling. Maintain original Renderer3D
submission ownership and conservative fallback on every unsupported
case.


---

## Stage 17.2 — cross-material and source-mesh positional seam certification (2026-11-20)

**Repository/branch:** `chromealex/AIVERSE`,
`feature/strata-virtual-geometry`; do not merge directly to
concurrently-changing `main`. **NO native compile/CTest, real
Metal/Vulkan/GLES device run, raster screenshot, sun-depth capture or
planar RTT acceptance was executed in this stage.** Code and regression
fixtures have been authored and committed only.

### Why 17.1 was insufficient

`auditSeamHierarchy` compares exact original **global vertex IDs**
inside one STRATA `Asset` (one material partition). UV/normal/lightmap
seams often duplicate spatial vertices with *different* source IDs
across material partitions or even between distinct Model meshes
bound to different rigid nodes. A source could therefore already
contain an unmatched split edge or a tiny positional gap across
partitions, even if each hierarchy parent preserves its own original
vertex-index boundary. Naively claiming all mixed-depth sources
crack-free in that case would be wrong.

### Implemented CPU-only source-level proof

- **New `engine/render/StrataSourceSeams.h/.cpp`** exports
  `auditModelSourceSeams(model,modelAsset,report,error)`.
  It requires complete original `Model::vertices` and
  `Model::indices` plus each eligible primitive's successful
  Stage17.1 `geometry.seamCertified`. It extracts exact
  *source primitive* boundary edge segments using opposite-direction
  triangle incidence and rejects degenerate/nonmanifold geometry
  before attempting any GPU path. This also checks cases where the
  source Model contains original UV seams and model-local duplicated
  geometric positions.
- Each staged mesh/primitive is evaluated with the
  `Model::bindWorld` of EVERY rigid source node using that mesh.
  No nodes in an offline-procedural source means one identity
  transform per mesh. Missing/mismatched transform metadata,
  invalid primitive references and non-finite transformed edge
  positions are rejected rather than assumed safe.
- Edge geometry is compared by **exact transformed endpoint
  coordinates**, so different per-material vertex IDs may share
  the same physical source seam. A geometric edge reused by
  more than two distinct partitions is conservatively declined.
  Matched duplicate-ID UV seams are explicitly accepted.
- A bounded 3-D spatial AABB BVH indexes all source boundary
  segments (max **250,000**, otherwise Legacy fallback).
  For each endpoint it tests other partitions' nearby segments:
  an endpoint falling **inside** a segment of another material
  identifies a source T-junction; a near-but-not-identical
  endpoint identifies a potential existing physical crack.
  The near-neighbor filter uses a conservative source-diagonal
  relative epsilon clamped between 1e-6 and 1e-3 world units.
  **This epsilon is used to DECLINE suspicious sources, not to
  silently weld them.** Exact geometry is still required to
  consider an endpoint matched.
  Average BVH query complexity is O(E log E) instead of
  naive O(E²); memory and work are bounded by staged model limits.
- The report `SourceSeamAudit`
  (`examinedPartitions`, `boundarySegments`,
  `matchedSegments`, `rejectedNearJunctions`) is **transactional**
  on failure, like the Stage17.1 whole hierarchy proof.
- `StrataModelAdapter.cpp::buildModelAsset` now calls this source
  audit AFTER building all eligible material partitions, BEFORE
  publishing the `ModelAsset`. On rejection it leaves the previous
  output unchanged and surfaces the seam failure to SceneStaging;
  the source remains drawable through ordinary Legacy.
  `ModelAsset::sourceSeamsCertified` is set true only after a
  complete successful source audit, with
  `sharedGeometricBoundarySegments` diagnostics.
- `SceneStaging::stage` refuses missing source seam certification
  and tracks `crossMaterialCertifiedSources` and
  `crossMaterialMatchedSeams`. `GeometryScene` reports these
  metrics and now requires **both** the per-primitive Stage17.1
  seal and source-wide Stage17.2 seal at portable, Metal/Vulkan
  GPU metadata, and Vulkan Renderer3D production entry points.
  No GPU pipeline, draw batching owner, model vertex buffer or
  legacy render pass is duplicated or changed by this validator.
- Existing `SourceSeamAudit` and builder algorithms are neutral to
  RTS/FPS/genre, Metal/Vulkan/GLES/GL33. They execute only during
  asset import/load, not on each camera frame.

### Exact changed paths and commits

- New `engine/render/StrataSourceSeams.h`:
  `82a9ca6908747f56530c4e27cf402d72f9649345`.
- New `engine/render/StrataSourceSeams.cpp`:
  `c9ddce517b94869e0c2721a6b4d31a5c5885ea40`.
- `engine/render/StrataModelAdapter.h`:
  `dfec0fdc5e1d3d9e9dc2e9562809c445138faff3`
  (ModelAsset source certificate, staging stats).
- `engine/render/StrataModelAdapter.cpp`:
  `287ddbbf8e23080ef6e2c53ff15ac062c59d6d24`
  (build-time source audit and transactional Legacy fallback).
- `engine/render/GeometryScene.h`:
  `0efbef067ac8cde68be3f4c3932cc0ed07224a39`
  (cross-material audit stats).
- `engine/render/GeometryScene.cpp`:
  `54257ef2e88748f780ec55c9f50c0b8a78164a6d`
  (require both certificates before portable/GPU production).
- `CMakeLists.txt`:
  `961e6d3085130678c4002407eba7a2a3841d05c3`
  links the source checker; `c059e4d5ed7d1f2864647337c616b8f06197c735`
  registers `strata_source_seams` CTest.
- New `tests/render/strata_source_seams.cpp`:
  `cac6a8f753912907d131738b1f2003a61b671616`
  (two opaque materials, duplicated UV seam IDs, split border
  vertex T-junction, small positional gap, separate meshes bound
  by distinct source-node transforms and transform revision).

### Authored tests (none executed)

`aiverse_strata_source_seams_tests` includes exact spatial seam
acceptance across independent material vertices and independent
mesh Node transforms, matched-edge accounting, T-junction induced
by one material splitting a shared border vertex, epsilon-close
but non-identical material border offset, invalid source rejection
and transactionally preserved previously built assets. Add
`strata_source_seams` to final native CTest selection with the
existing `strata_seams`, builder, model adapter, view,
Metal/Vulkan/GLES portable, GPU compute/indirect and overall engine
tests. The stage has no real screen-space crack or shadow-depth
A/B evidence yet.

### Stage 17 completion table

| Subsystem | Source status | Verification |
| --- | --- | --- |
| Parent/child same-ID topological seam proof | IMPLEMENTED 17.1 | NOT RUN |
| Safe reject on coarse boundary collapse | IMPLEMENTED 17.1 | NOT RUN |
| UV/material duplicate-position seam matching | IMPLEMENTED 17.2 | NOT RUN |
| Separate rigid meshes / bind transforms | IMPLEMENTED 17.2 | NOT RUN |
| Cross-partition T-junction / near-gap refusal | IMPLEMENTED 17.2 | NOT RUN |
| Production render seam certification and Legacy fallback | IMPLEMENTED | NOT RUN |
| Across different Model sources / ECS worlds | NOT GUARANTEED | N/A |
| Pre-existing cracks fixed automatically | NOT IMPLEMENTED | N/A |
| Lossy LOD geometric morph/popping and shading continuity | NOT IMPLEMENTED | N/A |
| Real mixed-depth GPU pixel/depth crack parity | NOT VERIFIED | N/A |

### Important limitations and next stage

This is conservative **asset admission**, not topology repair.
Open original exterior boundaries are allowed. Potential false
positives for separate nearby surfaces cause Legacy fallback
instead of risking a missing seam. No cross-Model/world
coordinate stitching, no independent terrain chunk seam negotiation,
no promised crack-free source when original geometry has
unrelated intersecting faces, no fluid/morph/animated topology
and no guarantee of visually smooth transitions between lossy
interior LODs. The source material shading/UV coordinates are
unchanged; geometry continuity does NOT imply normal/lighting
pixel equality. BVH construction can use substantial temporary
CPU memory for large meshes but is bounded to 250,000 boundary
segments and import-only execution.

**Next:** Stage 17.3 optional LOD temporal hysteresis and
screen-space popping reduction (without compromising the current
exact contour proof), then Stage18 optimization/performance,
then the owner's requested single native build+CTest+GPU acceptance.
Never claim performance or GPU correctness before actual logs
identify the tested commit SHA.


### Stage 17.2 follow-up — same-material UV island seam admission

Review found a source-level blind spot: two disconnected UV islands
can have identical geometric seams but duplicated vertex IDs while
belonging to the **same** material/primitive. Treating each material
primitive as one whole partition would miss the original T-junction
between those islands. Commit
`9d18a88e38da1f1631fed6de656ce6650da486d2`
changes `StrataSourceSeams.cpp` to first partition the original
triangles by **shared manifold edges** (not just by material or
shared vertex). Each transformed connected component gets its own
boundary identity. The same source BVH then checks neighboring
components regardless of whether their material ID differs.
A component touching another component at only one vertex does not
imply its other boundary segments are safely welded.

`tests/render/strata_source_seams.cpp` commit
`18fc0960ed432d864cef83d7dd9a8d2724a5e96e`
adds a single-material primitive with two disconnected UV-island
triangle groups: exact geometric seam is admitted even with duplicated
IDs, while a tiny source gap must fail the entire source audit
without mutating the previously certified output.
**Both source changes remain uncompiled and untested.**

This extends Stage17.2 beyond multiple materials/meshes to duplicated
position seams inside ONE original primitive. The other limitations
remain: no cross-source/world stitching or source geometry repair.


---

## Stage 17.3 — temporal LOD hysteresis and streaming-demand consistency (2026-11-20)

**Repository:** `chromealex/AIVERSE`.
**Feature branch:** `feature/strata-virtual-geometry` (NOT merged).
**Evidence:** all listed changes are **committed source and authored
regression fixtures**. This stage has **NOT been compiled, CTest'd,
GPU-rendered or benchmarked**. Older Stage13 lossless Metal image parity
does not certify the new temporal, streaming or Stage16 Vulkan code.
The owner requested one final combined build/test pass after features.

### Architecture, semantics and public API

- `StrataGeometry.h` extends backend-neutral
  `SelectionInput` with optional
  `hysteresisFraction` (validated 0..0.45), borrowed
  `previousRefined` mask and count. A previously refined node
  remains fine until its projected error falls below
  `thresholdPixels*(1-h)`; a previously coarse node refines only
  above `thresholdPixels*(1+h)`. **Cold start uses the original
  threshold**, not the wider admission band. Missing GPU children,
  invalid roots, or a selected-node budget still force the prior
  conservative resident-parent fallback; temporal history cannot
  override safety. An optional `SelectionOutput::refinedNodes`
  bitmap tracks actually refined hierarchy nodes BEFORE frustum
  culling. At h=0 no bitmap is allocated and original CPU selection
  behavior stays unchanged.
- `StrataView.h/.cpp`: matching optional
  `ViewConfig::hysteresisFraction` and
  `previousRefined` pointer. `planView` validates source topology,
  history mask dimensions, error threshold and camera matrix,
  applies the backend-neutral refinement policy and exposes a
  per-node `ViewPlan::refinedNodes` snapshot. Invalid history leaves
  the caller's prior `ViewPlan` unchanged. It accepts the existing
  physical-residency mask, so it cannot display an unresident child.
  No GPU memory allocation or scene ownership is added.
- `StrataPortableRender.h` defines
  `PortableLodHistory` — one persistent history per
  **scene instance** and separate indexed records keyed by
  (staged primitive ordinal, original Model source-node index,
  render-pass slot). History includes borrowed asset identity,
  geometry revision and pass count; a different source, model
  revision or changed number of passes gets a cold start.
  `PortableSource::prepareModel(...,history,fraction)` uses the
  independent view-specific history for each pass when computing
  its conservative union frontier, but publishes **all** history
  changes only after EVERY material/primitive alias has been
  successfully prepared, and after confirming all selected GPU
  index pages are resident. A failed GPU upload, invalid material
  or malformed view leaves the previous drawn alias and all
  temporal histories untouched. Force-finest selection for the
  late-known water reflection camera deliberately **resets** the
  histories only after successful alias preparation.
  `PortableRenderStats::hysteresisTransitions` counts actual
  refined-state bit flips between successful plans.
- `PortableSource::collectResidencyDemands` also optionally
  consumes the SAME history/fraction. This is essential: a
  previously refined node kept fine by hysteresis must still
  request its children from the Stage15 GPU page manager even
  when the raw no-history threshold would now prefer its coarse
  parent. Without this, scene-wide streaming evictions could
  undo hysteresis. The routine remains nonmutating; it appends
  demands only on complete success.
- `GeometryScene` provides
  `bool setStrataLodHysteresisFraction(float value)` (0..0.45;
  0/default disables). Invalid values are rejected without
  changing the current policy. Changing the policy clears all
  histories for a safe cold start. Runtime keeps one
  reference-counted `PortableLodHistory` for each stable
  `GeometryScene::Instance*`; histories are not shared
  between objects or cameras and are released on scene shutdown.
  A GPU Vulkan indirect route or HLOD/surface proxy drops the
  stale portable history, while a successful portable alias
  **retains** the same original instance's history. Failed
  alias construction does not invalidate it. Per-instance
  history is also read in `updateStrataStreamingBeforeDraw`
  before the immutable GPU index residency update. Scene stats
  include current histories and successful LOD transitions.
- This is an **opt-in CPU/portable selector** for all Metal,
  Vulkan and GLES3/GL33/WebGL2 fallback raster backends. Stage16
  GPU Vulkan/Metal compute selector remains its original
  **stateless camera threshold**. Do not claim CPU/GPU temporal
  selection equivalence or enable manual Vulkan GPU parity
  gates on this basis. LOD hysteresis stabilizes the choice of
  already crack-certified geometry; it does NOT interpolate
  vertices, blend normals/UVs, or guarantee visually invisible
  sudden LOD jumps after large camera motion.

### API example

```cpp
scene.setStrataPortableRenderingEnabled(true);
const bool accepted=scene.setStrataLodHysteresisFraction(0.2f);
// Stage15 index budgets and optional disk streaming remain independent.
// Main and sun have separate slot histories, 1 per scene instance.
// Later reflection camera forces finest available LOD and resets histories.
scene.load(device,sceneJson,fileReader);
```

Cold behavior `h=0` preserves legacy CPU selection and does not
allocate/retain per-node temporal vectors.

### Changed files and exact commits

- `engine/render/StrataGeometry.h`:
  `9d847f1c042d669d7272d3aa9e8c41920f921f05`
  (optional per-node hysteresis and actual refinement-state bitmap).
- `engine/render/StrataView.h`:
  `1bb8bee8839be8aa86c71d72833478aa1ffae147`.
- `engine/render/StrataView.cpp`:
  `6994fd66bbf565e3e8550c7292d83e084b30cdab`.
- `engine/render/StrataPortableRender.h`:
  `9e5598d89a1f644b5e2a7eac28c856d754cb3e7a`,
  `f8cdc35a3304040f89843995c65dcc64fb5eda32`.
- `engine/render/StrataPortableRender.cpp`:
  `5de489a2b65869ecd8b2abe494a90ec0d6a2e3c9`,
  `ca7edf9f6516f83189a8ccd2149f6df7f95e96c4`.
- `engine/render/GeometryScene.h`:
  `1e96df7eb05443d112bace6a5f1c5b632b9c693b`,
  `079f4310e5a313a13c61d1ef7d818f76c42d0737`.
- `engine/render/GeometryScene.cpp`:
  `5078e9c7424b627c883869f487a2d1db5cc5ca36`,
  `10d795915ba3b2e9f13bbdbe38a699f8d0791b07`,
  `7563f09e883727b3ed77164edc14aa15a4847a59`
  (per-instance state, successful-alias retention and streaming
  planner history consumption).
- `tests/render/strata_view.cpp`:
  `69f17fee62da315916459f411653a9487861a85e`,
  `4c4c48c899f944be616f8be5442e97e87a15ea44`.
- `tests/render/strata_portable.cpp`:
  `151435281f7fbbf1c2f9e33aa52525b0fc657136`,
  `c2148e142f2487d45f3ede97417f0ee5c3494ef3`.

No new CMake target is needed; existing
`aiverse_strata_view_tests` and
`aiverse_strata_portable_tests` already compile the changed code
when the STRATA option and tests are enabled.

### Authored validation matrix (NOT RUN)

- CPU selector: repeated noisy projected error values around the
  threshold; different up/down transition bands; no history on
  cold start; missing physical child/selected-node budget forces
  the same safe parent; invalid masks/policy fail closed; h=0
  retains original output shape without allocating the history
  bitmap.
- CPU view: rejected malformed per-view historical mask does not
  modify previous successful `ViewPlan`; subsequent valid
  history and h=0 paths behave as expected.
- Portable alias on mock Metal/Vulkan/GLES3: independent
  history of two scene instances using the same source,
  multiple camera slots, exact warm-hysteresis transitions,
  aborted GPU preparation leaves old alias and history unchanged,
  force-finest reflection resets history and zero policy remains
  backward-compatible.
- GPU-index streaming: when a warmed fine LOD is retained at the
  wider coarsening threshold, the residency demand planner still
  requests BOTH children instead of evicting them like a cold
  no-history plan would. Checks that subsequent alias stays fine.

No actual GPU capture, peak-memory, LOD switching frequency,
flicker rate, frame-time benchmark or macOS native pass/fail exists
for Stage17.3 yet.

### Stage 17.3 completion table

| Function | Source | Native verification |
| --- | --- | --- |
| Two-sided per-node CPU LOD hysteresis | IMPLEMENTED | NOT RUN |
| Per-instance/per-camera history | IMPLEMENTED | NOT RUN |
| History transactional on failure | IMPLEMENTED | NOT RUN |
| Shared streaming/raster temporal demands | IMPLEMENTED | NOT RUN |
| Force-finest reflection policy and reset | IMPLEMENTED | NOT RUN |
| Default OFF and mutable runtime policy | IMPLEMENTED | NOT RUN |
| GPU Vulkan temporal LOD hysteresis | NOT IMPLEMENTED | N/A |
| GPU Metal temporal LOD hysteresis | NOT IMPLEMENTED | N/A |
| Continuous geomorph/dithered transition | NOT IMPLEMENTED | N/A |
| Crack-free mixed-depth hardware visuals | NOT VERIFIED | N/A |

**Next bounded step:** Stage17.4 GPU-compute per-pass temporal
refinement history with bounded scene/instance storage, GPU-fence
lifetime and CPU/GPU cold/warm parity. Only after verified native
GPU selection and pixel parity should stage18 profiling/optimization
compare hysteresis to the baseline. Never substitute a smooth
global-threshold animation for a real per-node history or invent a
performance result. Leave Vulkan GPU production parity switches OFF.


---

## Stage 17.4 — GPU temporal LOD history on Metal/Vulkan (2026-11-20)

**Branch:** `feature/strata-virtual-geometry`; not merged into main.
**State:** committed code and new regression fixtures, **NOT compiled,
NOT CTest-executed, NOT tested on real Metal/Vulkan hardware**. This
is an implementation handoff, NOT a passing acceptance certificate.

### Architecture and API

- `GpuPassConfig` adds `hysteresisFraction` (0..0.45, default 0),
  `temporalPassId` (1 main, 2 sun, 3 spot, 4 reflection),
  `temporalNodeCount` (all nodes in metadata buffer), and
  `temporalRevision` (source/material/policy generation).
  `GpuSelectionUniforms` grows from 96 to **112 bytes** with four
  32-bit temporal fields appended, preserving every original field's
  offset and fitting the existing Vulkan minimum 128-byte push
  constant size. Metal shares the exact layout at buffer slot 8.
  Nonzero hysteresis requires one enabled GpuSceneInstance per dispatch,
  nonzero stable instance token, pass ID, revision and node count.
  Default zero leaves the old stateless GPU selector active without
  allocating any temporal state.
- `GpuClusterCulling` owns a GPU-only cache keyed by metadata buffer,
  original primitive ordinal, scene-instance token, camera/pass ID,
  and source/policy revision. Each entry has three separate Storage
  buffers, one `uint32_t` mask bit per GLOBAL GPU metadata node:
  0 means parent/coarse, 1 means refined. Current-frame writes NEVER
  overwrite a previous-frame read buffer; GPU queue order preserves
  dependency without CPU readback. Slot reuse is guarded by
  `completedFrameSerial()`, including GPU work that was partially
  encoded before an error. Same-key duplicate dispatch within one
  frame declines. Cold start uses the original threshold; the
  previous one/two frame(s) can warm-start if source, camera,
  viewport, clip-depth and hysteresis policy match. A gap of >=3
  RHI serials deliberately resets to cold (no stale read).
- Limits: at most **262,144 global nodes**, **256 independent
  history keys**, **32 MiB** GPU logical temporal storage per
  `GpuClusterCulling` owner. If allocation/fences/capability fail,
  the original portable/Legacy path remains. Inactive caches are
  released after >120 frames and GPU completion. `shutdown(device)`
  destroys all temporal masks through existing RHI lifetime handling.
  The 32-MiB cap excludes implicit Vulkan/Metal in-flight copies,
  model indices, metadata, GPU indirect argument buffers and other
  worlds. No global physical VRAM accounting is claimed.
- GPU kernels implement **reset → select → safety rollback**.
  `StrataVulkanCullingShaders.h`: `kVulkanTemporalReset`;
  selection reads prev mask at binding 5 and writes next at binding 6;
  existing indirect safety pass restores prev mask (or cold zeros)
  on invalid metadata, overflow or oversized produced counter.
  `StrataGpuCulling.cpp`: Metal equivalent
  `kMetalTemporalReset`, extended `kMetalCulling`, and
  `kMetalTemporalSafety`. Next mask is reset to ZERO for every
  metadata node before traversal, matching CPU
  `SelectionOutput::refinedNodes` for unvisited nodes.
  Refinement bits only publish after residence and bounded-stack
  checks. No synchronous readback is part of normal rendering.
  When h=0, Metal and Vulkan bind harmless valid dummy storage
  descriptors to declared temporal slots, with no accesses.
- `GeometryScene::setStrataLodHysteresisFraction(h)` now applies
  the same threshold band to Stage17.3 CPU portable and Stage17.4
  Metal/Vulkan compute selection. A changed h increments a
  monotonic policy epoch rather than destroying GPU in-flight
  buffers; previous GPU records are evicted when safe.
  Each scene instance+primitive+main/sun/spot/reflection receives
  an independent GPU key, propagated into
  `Renderer3D::prepareStrataAuxiliaryGpuPackets` before spot or
  reflected graphics passes. `GpuTemporalStats` and
  `GeometryScene::Stats` count live GPU mask bytes, history
  records, submissions, cold starts, refusals and expirations.
- **NEW independent default-FALSE production acceptance gate:**
  `GeometryScene::setStrataGpuTemporalParityVerified(bool)`.
  Existing stateless CPU/GPU selection and raster parity flags do
  NOT certify this new warm-history behavior. If h>0 and temporal
  parity is not independently accepted for the actual build,
  `tryStrataVulkanGpuDraw` declines and retains portable CPU/Legacy.
  Vulkan and Metal lower-level GPU diagnostic culling still permit
  explicit temporal dispatches without renderer ownership changes.
  No source geometry, PBR material, Metal/GLES raster ownership or
  SceneStaging certification was modified for this stage.

### Changed files and exact implementation commits

- `engine/render/StrataGpuCulling.h`:
  `04cb033f363b6b870ada7b7c7b3179b94df3e6c2`
  (API and bounded GPU temporal records),
  `f426a06d269c9754548998808785d8e2ab4f0fa3`
  (attempted-frame fence tracking),
  `172ffd421a7b0c10363be15cd3d72558540373d8`
  (diagnostic previous-mask oracle).
- `engine/render/StrataGpuCulling.cpp`:
  `8c6b9dca04f9396cabdc28dfc1a1c0db06f615ff`
  (Metal shaders),
  `19a06ff560720ff975a0f547b51f2e382265a8ed`
  (bounded temporal GPU cache),
  `3d0208507fa87b15a7d8fec5fc0238601845f1e8`
  (dispatch integration),
  `643743b1ddff0cbf32dd39a72bb2cf4ae4ddce66`
  (failed-encode frame-fence safety),
  `ef31f82ba442f493a18b3c6a1ccac602184f09ee`
  (cold/warm CPU oracle),
  `4c4b613b39c05267b8ef558c265cdeaea1b0b99c`
  (failed allocation cleanup),
  `2c7d19e290f199e154765be7ed82cfc3481a9ac0`,
  `6d1a1ae3ff8379afdda94feeb63f72cecdb33a0c`
  (Metal/Vulkan stateless binding compatibility),
  `4e17100b902868ccb503478cb9148f380687a243`
  (Metal unused-variable cleanup).
- `engine/render/StrataVulkanCullingShaders.h`:
  `bb3c4a8ad46a294f59f5a57ee296f0a6ffaeceb7`.
- `engine/render/GeometryScene.h/.cpp`:
  `b152a38ba0a6246e6f9e3890b83a3982f302f013`,
  `e12e3d04472e7866213e8a81851f672ef78074fe`,
  `9bb4dd7a79c8ed42ce2a382f622d210e22181d2e`,
  `7029cc49f144e04c64253583e2148e8f09c7260b`,
  `97437a8f7d412e62fcb5a37e0838d9c0b2ce255a`.
- `engine/render/Renderer3D.h`:
  `ea5b01a6058602f67666e7c106a33bb39824057d`.
  `engine/render/Renderer3DModels.cpp`:
  `5dd3d460b43cfd4e7846b237cd5be2cfc436eb29`.

### Tests authored, NEVER executed

- `tests/render/strata_gpu_culling.cpp`:
  `4f26675eb3207d33a301aab9b1ab1f28e50279f5`,
  `5051c21079caba1141fcaff6ffb331d058ca9029`,
  `50467b1a111c513ef9562972903917bf42d708a6`.
  Recording RHI verifies cold/warm dispatch flags, independent
  source/pass/revision history keys, 3-frame fence reuse, no
  duplicate same-frame write, no write to an in-flight failed
  attempt and 32-MiB cache limit. Recording does NOT execute GPU
  shaders or prove image parity.
- `tests/render/strata_vulkan_kernel.cpp`:
  `7c71dd59055ec249d52053fbb36e7815050c8fd8`,
  `a4f318598be49c73acd8f4edf6582aefe58061ea`.
  Real Vulkan optional driver test now enqueues 4 CONSECUTIVE
  temporal GPU frames with error factors 0.95/1.08/1.30/0.70,
  then asynchronously compares both GPU selected ranges AND
  GPU refined masks to the CPU cold/warm `planView` history.
  No CPU GPU-wait is inserted between the four frames.
- **New** `tests/render/strata_metal_temporal.cpp`:
  `a15038bb9338242374c48093b7c4b6a3717c8ec3`.
  Runs three consecutive real Metal temporal selection frames
  with asynchronous status/mask readback, CPU oracle, original
  shared GeometryPool, no second graphics draw owner.
  `CMakeLists.txt` opt-in macOS GPU test target
  `aiverse_strata_metal_temporal`:
  `2d195a95e62d70b35292a1d0ea25d227638aed21`.
  Existing Vulkan test target unchanged, still conditional on
  `AIVERSE_VULKAN`. Both are authored, uncompiled and unrun.

### Stage 17.4 completion and next agent

| Workstream | Code | Real acceptance |
| --- | --- | --- |
| Metal GPU per-node temporal state | WRITTEN | NOT RUN |
| Vulkan GPU per-node temporal state | WRITTEN | NOT RUN |
| Per-instance/per-pass revisions | WRITTEN | NOT RUN |
| Triple-buffer GPU fence protection | WRITTEN | NOT RUN |
| GPU invalid/overflow history rollback | WRITTEN | NOT RUN |
| Bounded allocation, expiration and cleanup | WRITTEN | NOT RUN |
| Independent GPU temporal production parity gate | WRITTEN, default OFF | NOT RUN |
| Native Metal/Vulkan cold/warm readback fixtures | AUTHORED | NOT RUN |
| GLES3/GL33 fallback, h=0 stateless behavior | Code path retained | NOT RECHECKED |
| Actual main RGBA/shadows/reflection GPU pixel parity | NOT VERIFIED | N/A |
| Vertex geomorph/dithered interior LOD transitions | NOT IMPLEMENTED | N/A |
| GPU performance/popping profile and benchmarks | NOT MEASURED | N/A |

**Next:** Stage18 performance instrumentation and optimization:
whole-node reset traffic per primitive, Vulkan descriptors and compute
dispatch overhead, Metal encoder count, temporal cache high-water,
staged GPU page churn and lossless vs lossy LOD frame times.
Collect a true h=0 baseline. Then Stage19 clean integration worktree,
full CMake/CTest and real Metal/Vulkan/GLES GPU acceptance. Never
enable GPU temporal parity switch without evidence from an actual
tested source SHA; never overwrite concurrent main render work.


---

## Stage 18.1 — bounded CPU/RHI telemetry for Strata GPU selection (2026-11-20)

**Branch:** `feature/strata-virtual-geometry`; **not merged** into main.
**Scope:** instrumentation only, no new render path, backend shader or
production-parity gate enabled. All changes committed; native compilation,
CTest and GPU execution **NOT RUN** in this agent environment.

### Changes (exact implementation commits)
- `1bbebe9d212cf173c4f5f36f9cba613853e8cdcd` —
  `engine/render/StrataGpuCulling.h`: additive fields on
  `GpuTemporalStats` for `peakRetainedBytes`, `peakHistories`,
  `successfulDispatches`, `peakSubmissionsPerFrameSlot`.
- `9344c09d12aacb2b819410a10fc8ff48dcaa6305` —
  `engine/render/StrataGpuCulling.cpp`: update high-water marks
  after successful mask-buffer allocation/cache creation and count
  only successfully encoded/accepted dispatch submissions. Retired mask
  bytes reduce `retainedBytes` but not the historical peak.
- `e015a935abf4fa50d813fb20de82d36f6404cb9b` —
  `tests/render/strata_gpu_culling.cpp`: recording-device fixtures
  assert monotonic peaks and successful versus refused submissions.
  The fixture is **authored, not executed**.

### Semantics and safety
Counters are accumulated by each `GpuClusterCulling` owner and reset
by `shutdown`. They are **CPU-side logical RHI allocation and dispatch
counts**, not GPU timestamp measurements, physical VRAM usage, GPU
encoder overhead or frame times. All API changes are additive; the
`GpuTemporalStats` method still returns a value snapshot.
No GPU readback, extra dispatch, fence wait or memory allocation was
added to the hot path. All default-OFF and parity flags remain OFF;
no FPS uplift or performance win is claimed.

### Stage 18 checklist

| Workstream | Status | Verification |
| --- | --- | --- |
| GPU temporal cache high-water marks | SOURCE IMPLEMENTED | NOT RUN |
| Successful dispatch/slot density counters | SOURCE IMPLEMENTED | NOT RUN |
| Recording RHI regression assertions | AUTHORED | NOT RUN |
| Per-pass GPU timestamps (Metal/Vulkan) | TODO | N/A |
| Descriptor allocation/compute encoder counts | TODO | N/A |
| GPU page upload/eviction churn baseline | TODO | N/A |
| h=0 vs h>0 vs legacy workload bench | TODO | N/A |
| LOD popping/error and lossy seam image audit | TODO | N/A |
| Full Mac compile/CTest, actual GPU validation | DEFERRED | N/A |

**Next stage:** instrument per-frame page request/upload/eviction totals
and per-backend compute dispatch accounting without synchronous GPU waits;
then add an opt-in benchmark recorder with identical fixed camera traces
for h=0 and h>0. Preserve current user instruction: finish functionality
first, run one combined native CMake/CTest/hardware pass on Mac afterward.
Inspect current main before final integration, avoid overwriting other
render/geometry agents' changes, and do not enable unverified Vulkan GPU
parity routes.


---

## Stage 18.2 — per-frame STRATA residency/streaming telemetry (2026-11-20)

**Repo/branch:** `chromealex/AIVERSE` / `feature/strata-virtual-geometry`; **not merged to main**.
**Validation truth:** Source was committed; no local CMake compiler, CTest, Metal,
Vulkan or GLES GPU test was run. Added regression assertions are NOT a pass
claim. Existing Stage17 production parity gates remain disabled.

### Why this stage exists
Existing `GeometryScene::Stats::strataStreamingEvictions`,
`strataStreamingGpuTransferBytes`, and other source stats were sums
of **cumulative** `IndexResidencyStats` from each scene source.
Re-reading those values for successive frames cannot measure per-frame
upload churn or residency promotion/eviction rate.

### Committed source changes
- `a30e42bdb0bebd9c1d234cd7b97c4c4581b89f3c`:
  `engine/render/GeometryScene.h` — additive `Stats` fields
  `strataStreamFrameSerial`, `strataStreamFrameDemandRecords`,
  `strataStreamFrameGpuTransferBytes`, `strataStreamFrameUploadedBytes`,
  `strataStreamFramePromotions`, `strataStreamFrameEvictions`,
  `strataStreamFrameDeferrals`, `strataStreamFrameFailedUploads`,
  `strataStreamPeakGpuTransferBytes`,
  `strataStreamPeakActiveIndexBytes`.
- `7dba7d4923da84b49b4285299441d9f123aa6295`:
  `engine/render/GeometryScene.cpp` — at pre-draw residency update,
  reset frame values when `gfx::Device::frameSerial()` changes.
  Serial 0 is untrusted and resets per prepass. Count demand records
  after each source's instance union, compare before/after source
  counters, aggregate and retain high-water snapshots. A prepass
  repeated in the same RHI frame accumulates that frame's deltas.
- `b282355efc9bb87e51c4fcfe2b5f0cfddfd7cde0`:
  `engine/render/StrataIndexResidency.h` — pure
  `indexResidencyFrameDelta(before, after)` with nonnegative
  differences for checked GPU transfer, committed upload bytes,
  promotions, evictions, deferred demands and failed transfers.
- `acd4b2c4369b88abb99f3ccc316afb770653b76b`:
  `engine/render/GeometryScene.cpp` — reuse the pure delta
  helper rather than duplicating arithmetic in the scene loop.
- `8fe50c1410736b55f698a2bc2b64e70c198cfdfd`:
  `tests/render/strata_portable.cpp` — authored regression for
  failed-upload orphan transfer (32 bytes transferred, 0 bytes
  published), no-change deltas, counter reset handling and subsequent
  increasing counts; **not executed**.

### Scope, API and ownership
`scene.stats()` is the only public access surface; no new ECS
component, scene JSON field, RHI dispatch, GPU readback, allocation
or renderer route was introduced.
`strataStreamFrameDemandRecords` counts per-source collected demand
records before residency update; it is **not** distinct GPU pages,
deduplicated nodes, a demand success rate or triangle count.
`strataStreamFrameGpuTransferBytes` counts checked RHI copies
including writes from failed transactions. In contrast,
`strataStreamFrameUploadedBytes` is the delta in published upload
bytes reported by the residency owner. Frame counts are **CPU-side
observations**, not physical VRAM, GPU timestamps or PCIe bandwidth.
`strataStreamPeakActiveIndexBytes` aggregates the active source
index allocation snapshots rather than actual backend heap residency.

Lifecycle: frame fields reset on a changed valid device serial or on
each prepass with invalid serial 0. Peaks persist until scene
shutdown; scene shutdown clears `stats_`. If the prepass is skipped
because streaming is off or no sources are present, last sampled
frame values may remain visible; consumers should check the serial
and streaming status rather than assuming a fresh reading.

### Test plan, not execution result
```sh
cmake -S . -B build/strata-final -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_STRATA=ON
cmake --build build/strata-final --target aiverse_strata_portable_tests aiverse_strata_gpu_culling_tests --parallel 4
ctest --test-dir build/strata-final -R '^strata_(portable|gpu_culling)$' --output-on-failure
```
Then review the actual `CMakeLists.txt` target names, conduct
device-specific tests under their enabled build options, and
check the complete Stage17.4 temporal acceptance fixtures.
No pass/fail result was observed for Stage18.2.

### Next bounded step — Stage 18.3
Instrument backend compute encoder/dispatch counts and real GPU timing
capabilities only where supported, without synchronous waits, and
add an opt-in deterministic camera-trace benchmark for h=0 versus
h>0 under fixed scene/workload. Avoid reporting a speedup without
measured, reproducible native hardware results. Keep entire end-to-end
native test pass deferred until user-approved final feature completion;
do not overwrite concurrent batching/renderer work.


---

## Stage 18.3 — RHI compute dispatch overhead counters (2026-11-20)

**Branch:** `feature/strata-virtual-geometry` (not merged). Stage18.2
preceded this change. Code and tests are committed, but **native compilation,
CTest and GPU execution were NOT RUN**. Existing GPU temporal production
parity gates remain OFF.

### Discovery and boundary
`engine/gfx/Gfx.h` currently exposes `beginCompute`,
`dispatchChecked` and `endCompute`, but **no cross-backend GPU timestamp
query contract**. Accordingly, Stage18.3 instruments the number of
RHI calls and requested logical workgroups; it does NOT invent a
GPU duration, time GPU utilization, or synchronize with the GPU.

### Implemented changes and commits
- `af5ea02decd3fe2e89e28ebe7d4e0d89e2e323fd`:
  `engine/render/StrataGpuCulling.h` extends the existing
  `GpuTemporalStats` with `computeBeginAttempts`,
  `computeEncodersOpened`, `computeBeginFailures`,
  `computeDispatchAttempts`, `computeDispatchAccepted`,
  `computeDispatchFailures`, `computeGroupsRequested`,
  plus per-kernel counters `temporalResetDispatches`,
  `selectionDispatches`, `indirectClearDispatches`,
  `safetyDispatches`. These counters also count **partial**
  attempts that fail before a complete Strata submission.
- `6c819499748ae58a0bcab60f47d2e399a2e01567`:
  `engine/render/StrataGpuCulling.cpp` wraps five existing
  compute begin/dispatch sites with local tracked wrappers, preserving
  the original shader bindings, GPU queue order, encoder lifetime and
  error handling. The counters do not call any GPU readback or clock.
  Vulkan stateless normal path remains clear/select/safety; Metal
  stateless path remains select only. Temporal Metal/Vulkan paths
  account for their additional reset/safety operations.
- `f760cb256d3b7ccec414f8e8d43f9a94a788c6b9`
  and correction `2683e4a69bc8f51871ec6c828a3e1ec5ed7a1176`:
  `tests/render/strata_gpu_culling.cpp`, recording-RHI assertions
  of begin/dispatch conservation and three Vulkan view submissions
  (3 compute encoders each). The recording device's
  `computeCalls` counts pipeline creations, not encoder opens;
  the corrected assertions explicitly expect nine opens. **NOT RUN**.

### API and safety
The additive counters are retrieved through
`GpuClusterCulling::temporalStats()`; `shutdown` resets all.
The existing `successfulDispatches` counts complete selections,
whereas `computeDispatchAttempts` counts kernel dispatch calls,
including those from submissions that subsequently fail. Stage-kernel
counts represent attempted, not necessarily accepted, dispatches;
the accepted/failure totals distinguish these outcomes.
`computeGroupsRequested` sums the logical counts supplied to
`Device::dispatchChecked`, not hardware wave/warp/mesh-shader
occupancy. All backends keep their prior capability and parity gates.

### Stage 18 progress
| Area | Code | Native/GPU verification |
| --- | --- | --- |
| Compute encoder begin/failure accounting | IMPLEMENTED | NOT RUN |
| Total checked compute dispatch counters | IMPLEMENTED | NOT RUN |
| Kernel-category attempted dispatch breakdown | IMPLEMENTED | NOT RUN |
| Recording-RHI regression assertions | AUTHORED | NOT RUN |
| Real Metal/Vulkan GPU timestamps | NOT IMPLEMENTED | N/A |
| Real per-pass GPU milliseconds/p95 | NOT MEASURED | N/A |
| Deterministic h=0 vs temporal camera-trace benchmarks | TODO | N/A |
| Final Metal/Vulkan/GLES acceptance | DEFERRED | N/A |

**Next:** Stage18.4 implement an opt-in deterministic trace/replay
benchmark and exports for fixed frames, scenes and camera views,
record baseline h=0 and temporal h>0, and only then evaluate
performance tradeoffs after one combined Mac native test run.
If real GPU timings are needed, first design optional RHI timestamp
queries with backend-specific asynchronous safe readback and strict
frame/fence lifetime (do not emulate them using CPU wall time).
Continue to append a distinct stage handoff and list exact commits.


---

## Stage 18.4 — deterministic CPU LOD trace comparator (2026-11-20)

**Branch:** `feature/strata-virtual-geometry`; PR #35 remains draft.
**Actual validation:** Source authored and committed, **NOT built, CTest'd or
executed on native GPU**. Do not claim performance gain from this stage.

### Implementation and exact commits
- `cb194a0434e4c7b71ced1003cbbbc6e9d39a02c6`:
  `tests/render/strata_lod_benchmark.cpp`: standalone deterministic
  240-frame CPU selector test using the existing
  `StrataGeometry.h` header API. Compares
  fixed `legacy_root`, original stateless `strata_h0`, and
  temporal `strata_h02` (hysteresis fraction 0.2).
  Fixed sequence: 40 frames low projected error, 40 frames high,
  120 alternating around the LOD threshold, 40 low.
  Emits CSV `mode,frames,transitions,triangles,checksum,cpu_wall_ns`.
  Asserts stateless LOD switches more often than temporal, legacy
  does not switch, triangle counts are valid, and temporal
  checksum is exactly reproducible.
- `f78ae65a87631d7534bde104de6969d0e5bc1e10`:
  `CMakeLists.txt` registers
  `aiverse_strata_lod_benchmark` and `strata_lod_benchmark`
  only under existing Strata test configuration.
- `506fcd371e68cfa2ff5a146a27cd8413cd7fc70c`:
  adds explicit C++ `<utility>` include and fixes CSV newline encoding.

### Data interpretation
This is a synthetic, deterministic *projected-error* trace: it is
not yet a recorded camera transform replay through `planView`, GPU
raster or an instance-heavy production scene. Triangle counts sum
selected cluster triangles over frames, not triangles actually
drawn after frustum/occlusion. CSV CPU wall nanoseconds measure
only the isolated selector call on the executing machine;
they are NOT GPU timestamps, render frame times or FPS.
The fixed legacy root baseline is not identical to the full
existing legacy renderer or its material passes.
Only the checksums/selection counts are intended to be stable
across runs; measured CPU timings must not be used as acceptance
thresholds. No shipping Strata routing, memory policy, ECS,
streaming, Vulkan/Metal/GLES shader or visual parity gate changed.

### Next verification on the owner's Mac
```sh
cmake -S . -B build/strata-final -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_STRATA=ON
cmake --build build/strata-final --target aiverse_strata_lod_benchmark --parallel 4
ctest --test-dir build/strata-final -R '^strata_lod_benchmark$' --output-on-failure
./build/strata-final/aiverse_strata_lod_benchmark > /tmp/strata-lod-trace.csv
```
Verify target placement against the active build options. No above
command was run in this implementation session.

### Stage 18.4 completion
| Area | Code | Native acceptance |
| --- | --- | --- |
| Deterministic 240-frame reference trace | WRITTEN | NOT RUN |
| Three fixed CPU selection policies | WRITTEN | NOT RUN |
| CSV counters/checksums and optional wall time | WRITTEN | NOT RUN |
| Stateless versus temporal selector regression | WRITTEN | NOT RUN |
| Real moving-camera/scene replay | NOT IMPLEMENTED | N/A |
| Metal/Vulkan GPU timing | NOT IMPLEMENTED | N/A |
| Real image-based popping/seam quality | NOT VERIFIED | N/A |
| GPU frame-time/FPS optimization claims | NO CLAIM | N/A |

**Next bounded stage:** Stage 18.5 build a lossless/lossy multi-material
LOD visual validation matrix (main, shadow, reflection, Metal/Vulkan/GLES);
add camera/asset captures with strict image-diff quality budgets.
Then Stage19 merge latest main into isolated integration worktree and
perform one complete native CMake/CTest/real-device acceptance pass.
No automatic switch of unverified parity flags.


---

## Stage 18.5 — raw-capture visual quality audit foundation (2026-11-20)

**Branch:** `feature/strata-virtual-geometry`, not merged to main.
**Validation:** Code and documentation committed; **NO hardware capture,
native compilation, Python self-test, GPU visual parity or CTest was
executed in this session.** Stage18.5 provides audit infrastructure and
acceptance matrix, NOT completed Metal/Vulkan/GLES proof.

### Changes and commits
- `4191141aa457b06baa59ad3df1b9fab51276f3b7` —
  new `tools/strata-stage18-image-audit.py`, offline raw tightly
  packed RGBA8 and little-endian Depth32Float A/B image comparator
  with strict dimensions, ROI, tolerance checks, max error, failed
  pixels, JSON reporting, exit status 1 on failed comparison, and
  self-test cases. This tool does not capture or render.
- `0abb80324761945ea18c610b85ee26b4d46fc991` —
  add optional manifest `--manifest` mode with expected
  backend/pass/view/LOD keys. Reject duplicate observations,
  missing expected captures, unsupported format and wrong binary
  payloads, retaining per-case JSON results. A tiny manifest
  proves ONLY its own listed cases, not the full acceptance matrix.
- `98ea85f16ad257a2aa04c5a58e041d8282304cb3` —
  new `docs/strata-stage18-visual-acceptance.md`: required
  lossless/lossy scenarios, two-camera main/sun/spot/reflection
  hardware captures, seam ROI checks, threshold/popping acceptance,
  capture format, provenance and example commands.

### Boundaries, status and safety
The existing seam builder / Stage17.2 source audit and Metal Stage13
lossless render fixture were NOT changed. No new RHI rendering,
shader, ECS component, geometry ownership, parity flag toggle,
asset serialization or production route is introduced. Fail-closed
checks apply only to supplied raw data. The script cannot detect
fake captures, blank-frame parity or visual artefacts automatically.
No assertion is made that authored lossy LOD parents render crack-free
at mixed depths on actual hardware. Existing CPU structural seam
certification is distinct from real visual proof.

### Completion table
| Feature | Code | Real acceptance |
| --- | --- | --- |
| RGBA8 ROI exact/tolerant comparison | IMPLEMENTED | NOT RUN |
| Depth32Float ROI comparison + nonfinite rejection | IMPLEMENTED | NOT RUN |
| Deterministic JSON and failed-comparison exit status | IMPLEMENTED | NOT RUN |
| Manifest required capture coverage | IMPLEMENTED | NOT RUN |
| Metal two-camera lossless main/sun/reflection | Earlier fixture | Current SHA NOT RUN |
| Vulkan main/sun/spot/reflection lossy seam capture | NOT CAPTURED | N/A |
| GLES3 portable visual fallback | NOT CAPTURED | N/A |
| Temporal flicker and popping visual acceptance | NOT CAPTURED | N/A |
| Full hardware image diff evidence | NOT AVAILABLE | N/A |

### Next exact actions
1. At Stage19, merge current main into an isolated integration branch,
   checking concurrent batching/Renderer3D changes; never blindly
   overwrite main or force-push.
2. Run `python3 tools/strata-stage18-image-audit.py --self-test`
   and all CMake/CTest Strata builds on the owner's Mac; fix actual
   compiler and test failures.
3. Capture genuine GPU output for Metal/Vulkan/GLES from the tested
   commit (main, sun, spot, reflection where supported). Compare
   each same-scene Legacy/STRATA pair with a full manifest and
   nonblank checks, review visual seams/temporal flicker.
4. Only after independent hardware acceptance may the corresponding
   GPU parity gate be considered for opt-in production use.
   Existing default OFF flags remain unchanged.

**Do not describe Stage18.5 as proof of hardware image parity.**

## 2026-11-21 — review fixes (branch feature/aiv20261121_fix_gfx_strata)

STRATA part of the gfx-strata review pass; full detail in
[review-fixes-gfx-strata-2026-11-21.md](review-fixes-gfx-strata-2026-11-21.md).
- `8cd3603` `GeometryScene::loadTransactional` keeps STRATA config (copied into the staging owner) and moves STRATA resources into the live scene; new test `geometry_transactional_reload_keeps_strata_policy_and_resources` (NOT RUN, aiverse_tests not built here).
- `bbb0847` `StrataDiskPages`: failed reads retried with request-count backoff, permanent after 5; verified read clears the failure; the in-flight job's memory wait aborts when its page is dropped. `strata_disk_pages`, `strata_index_pages`, `strata_streaming_coordinator` built standalone and PASS.
- DEFERRED: Vulkan Storage+Dynamic arena whole-copy per upload frame (plan: Vulkan `updateStorageBufferChecked` + device-local arena); see the review handoff.
