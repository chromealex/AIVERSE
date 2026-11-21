# TerraWeave — AI agent handoff

Last updated: 2026-11-19. Branch: `feature/terraweave`.
PR #24 target: `main` (retargeted 2026-11-19; previous feature branch was historical base).
Owner request: implement generic terrain/trees/foliage for AIVERSE; **update this file
at the end of EVERY development stage** so a different AI can pick up immediately.

## Stage 1 — generic generation core (IMPLEMENTED, build NOT VERIFIED)

Changes committed to branch (each is an individual commit):
- `fc9acfabac8969872b7dd7821612e530545f4757`: `engine/world/TerraWeave.h`;
  integer signed value-noise terrain, seeded climate/biome classification,
  tile generation, world-global foliage scatter, exclusion rectangles,
  two ECS-ready patch descriptors.
- `7ebf0f3dc3405ab1d6240510df31a56c4bc25fb8`:
  `tests/terraweave_test.cpp` tests seams, negative chunks, deterministic
  foliage distribution and input-order independence, thread-safe pure sampling,
  settings validation.
- `94cbd6553b3ca797542baa02c9f852b7073b4a08`:
  `CMakeLists.txt` registers `aiverse_terraweave_test` / CTest `terraweave`.
- `8edcd1fb4e4f4702b3341368febc69c400577e85`:
  `docs/terrain/TERRAWEAVE.md` API and integration design.

**Verification status**: source committed to GitHub via connector. Full repository
checkout/build was not possible in the authoring runtime (GitHub DNS/network
access unavailable there). Unit tests are authored, **not claimed passing**.
Next agent MUST compile C++20, resolve errors/warnings, run CTest and update
this status with exact commands/results. DO NOT assert successful engine tests
without logs. No screenshots or performance measurements yet.

## Code and design details

- Namespace: `aiverse::terraweave`; header-only new engine API at
  `engine/world/TerraWeave.h`. Native C++ modules can include it directly.
- World X/Z horizontal, Y height. All input/output distances integer cm.
  `ChunkKey` is signed int64 for negative regions. No GPU or float state.
- `TerrainSettings` seed/base/relief/period/octaves; `BiomeSettings`
  seed/sea/temperature/humidity/period/variation/lapse.
- `generateChunk` returns (resolution+1)^2 height samples plus biome ids,
  globally anchored so adjacent chunk edges are bit-identical.
- `scatter` returns `FoliageInstance` (stable key, prototype id, position,
  yaw, scale), with per-species biome mask/height/slope/weight, and rectangular
  exclusions. One jittered candidate per global lattice cell; sorting species
  by id makes input order irrelevant. Bounded to 1e6 candidate lattice cells.
- Pure functions can run in parallel; host determines scheduling. Do not
  mutate ECS worlds inside a worker thread or during a `World::each`.
- No ECS update system wired yet; `TerrainChunkComponent` and
  `VegetationPatchComponent` are descriptors, not simulated systems.
- Existing `modules/rts/view/RtsTerrainRender.*`,
  `modules/rts/view/RtsTerrainSurface.h`, `apps/editor/core/TerrainOps.*`,
  `modules/rts/gen/GenNoise.*`, `MapGrid` retain ownership of existing RTS
  terrain. A future adapter converts integer cm to map units (1/16 units)
  and pushes generation results through existing dirty-chunk pipeline.

## Immediate follow-up — Stage 1b (blocking)

1. Compile `aiverse_terraweave_test` on Clang/GCC/MSVC, including -Wall/-Wextra.
   Run CTest; fix issues in branch, log results here.
2. Verify settings validation/overflow at extreme signed world coordinates;
   establish a supported finite world extent and fail cleanly.
3. Validate that all instances are stable when chunk size changes and with
   more than two species. Add dense forest quality stats.
4. Finish adding `skills/world/aiverse-terraweave/SKILL.md` and an `AGENTS.md`
   rule (if not already present on branch; verify before editing).

## Next stage — Stage 2: existing terrain integration

Goal: generated chunks visible in the existing AIVERSE renderer and editor,
not a competing renderer.

1. Read `docs/terrain.md`, `docs/editor-windows.md`,
   `docs/ecs-agent-handoff.md`, `docs/native-modules.md` and actual current
   source. Rebase carefully; FPS/ECS feature work is happening concurrently.
2. Define world-generation configuration (optional; legacy maps unmodified)
   and an adapter for `MapDoc` heightfield corners. Preserve authored edits
   with a separate override layer and regenerate only dirty chunks.
3. Make scene streaming select visible keys; worker jobs generate pure data.
   Commit on main/guarded ECS sequence; include cancellation, stale generation
   checks and bounded queue; don't enable unsafe generic ECS parallel systems.
4. Integrate heightfield collider, navmesh updates and exclusion/stamp providers
   for roads, buildings, bridges and water. Tests for unchanged old checksums.
5. Editor preview and paint/sculpt tools; negative coordinates; undo/redo.

Acceptance: generated terrain is visible in a real scene; regenerate deterministically;
existing maps identical when disabled; build plus screenshot test.

## Stage 3 — Arbor + Flora / rendering

- Add reusable prototype registry (native + data), procedural branch/crown mesh
  generation with parameters and seed, generated geometric LOD and impostors.
- Forest species weights, competitive exclusion / crown collision,
  biome blending, designer-painted areas and persistence for removed trees.
- Pack millions of cosmetic plants into instance groups, no per-blade ECS
  entities; GPU culling + indirect/instancing RHI path with GLSL and MSL,
  WebGL/mobile CPU fallback. Bench + screenshots + memory budget.

## Stage 4 — Eco, streaming and production polish

- Wind/interaction, seasons, runtime growth, destruction & harvesting.
- Optional ECS features, with deterministic authoritative gameplay state.
- Sparse chunk persistence with generator version migrations, LOD streaming
  budget, editor node graph, pipeline to roads/water, quality tiers.
- Bench CPU gen time, frame GPU time, memory/triangle/draw counts at scales.

## Mandatory handoff format for all subsequent stages

After EVERY stage, append:
- Commit SHA(s), branch and changed files.
- Implemented vs not implemented behaviors and public API/units.
- Commands/logs for compilation/tests (or explicit NOT RUN).
- Compatibility concerns, architecture decisions and blockers.
- Next precisely scoped tasks and acceptance criteria.

Do not overwrite previous stages; keep history available to the next agent.

## Stage 2a — Arbor tree skeleton, initial mesh and LOD (COMMITTED; TESTS NOT YET VERIFIED)

Commit history (all on `feature/terraweave`):
- `f773eca934ac677f841865326a6d08e850275985`: created
  `engine/world/TerraWeaveArbor.h`, seeded local-space integer skeleton:
  trunk, trunk-anchored branches, foliage clusters, Broadleaf/Conifer/Columnar
  profiles, tree LOD High/Medium/Low/Impostor and stable instance-key overload.
- `1112b747ba845492e40d80da16d368e907b5ed27`: added
  `tests/terraweave_arbor_test.cpp` to test branch topology, LOD, seeded
  variation, input validation and parallel purity.
- `20843a5a2d800fdb44a8089599e046275db25ba7`: added CMake/CTest
  `aiverse_terraweave_arbor_test` / `terraweave_arbor`.
- `5c2951343ca819360b45f31f0953bdc165ed7519`: test include fix.
- `dd0714b2e51306b87928032bff65dc792e8f3020`: added
  `engine/world/TerraWeaveArborMesh.h` to build bark tube triangles and
  crossed leaf alpha cards with normals, UVs and bark/leaf material IDs.
- `98e481dab46ff05298bd913356b3d1b2a9948fc8`: added mesh bounds,
  indices, material and finite vertex checks to Arbor tests.
- `23a40a35899dc98ecb669081a6aa8a62bcb67753`: documented Arbor API
  in `docs/terrain/TERRAWEAVE.md`.

**Test status:** C++ tests are written, but neither local compilation nor CI
success was observed at this stage. The PR is still Draft (#24).
Run:
```sh
cmake -S . -B build/terraweave-test -DAIVERSE_BUILD_TESTS=ON
cmake --build build/terraweave-test --target aiverse_terraweave_test aiverse_terraweave_arbor_test -j
ctest --test-dir build/terraweave-test --output-on-failure -R '^terraweave(_arbor)?$'
```
Record exact output here (including platform and failure lines); repair first.

**Implemented:** reproducible tree structural data for native projects; basic
CPU triangle/leaf-card mesh conversion; coarse geometry LOD and conservative
impostor bounds.

**NOT implemented:** actual tree assets in the editor, GPU uploads/draws,
texture/material assignment, tangent computation, billboard atlas bake,
forest/tree placement adapters, wind/deformation, collisions and physically
correct growth. No C++ runtime scene currently renders the Arbor mesh.

**Next agent:** verify both tests, then attach `buildArborMesh` to the
existing `gfx::Device` upload path and model/material pipeline; avoid
direct GL/Metal calls. Prefer integrating the existing `RtsTerrainRender`
chunk renderer over creating a second independent renderer. Preserve
legacy map checksums. Add forest LOD group batching and instance buffers,
editor preview and a screenshot test. Update this handoff again after
each phase.

## Stage 2b — Flora stable LOD batch preparation (COMMITTED; NOT TESTED YET)

- `5c1e233eff8edb756b3401af14bc2c5e93710814`:
  `engine/world/TerraWeaveFlora.h` introduces `FloraView`,
  `FloraBatch` and `buildFloraBatches(placements, view)`.
- `6a01f25e815d75f1398eb753eea58838be831c9a`:
  `tests/terraweave_flora_test.cpp`: species+LOD grouping, radius
  culling, stable order/dedup, configurable far thinning, validation.
- `97763f8e85b806d453268dae338c6859298361d4`:
  `CMakeLists.txt` registers test `aiverse_terraweave_flora_test` /
  CTest `terraweave_flora`.
- `1bad4434381c7316761796d036b675542975a7e3`:
  `docs/terrain/TERRAWEAVE.md` updated with public Flora API.

**Verification:** files committed to PR #24, but no successful
CTest/CI run observed. Run:
```sh
cmake -S . -B build/terraweave-test -DAIVERSE_BUILD_TESTS=ON
cmake --build build/terraweave-test --target aiverse_terraweave_test aiverse_terraweave_arbor_test aiverse_terraweave_flora_test -j
ctest --test-dir build/terraweave-test --output-on-failure -R '^terraweave(_arbor|_flora)?$'
```

**Scope**: CPU-only visibility/cull/LOD batch descriptions; uses no
GPU APIs and does not mutate ECS. `FloraView` units are centimetres.
Do not claim GPU instancing is delivered. It requires a new render
adapter through `gfx::Device`, prototype registry, shader parity,
quality fallback and profiling.

**Next priorities**: resolve any compiler failures; patch TerraWeave
core world-coordinate overflow at large signed coordinates (currently
not hardened); wire geometry into real editor/runtime renderer,
stabilize save formats and scene regeneration, integrate road
exclusion masks and biome painting. Maintain the stage-by-stage
handoff here.

## Stage 2c — bounded world extent hardening (COMMITTED; NOT TESTED YET)

- `c0e4fc1fc84d9e500b53631a374693ec5d36f979`:
  `engine/world/TerraWeave.h` defines `kWorldLimitCm = 1000000000`
  and pre-validates noise sampling, chunk key/span, slope neighbouring
  samples and scatter bounding box before signed coordinate arithmetic.
- `7820438062da8639ba55fc23675745bce5fd9b4f`:
  `tests/terraweave_test.cpp` adds extreme signed-coordinate exception
  cases; run test to confirm.
- `c0e336f344d1f09cbdac90830c72fd8edd31490c`:
  `docs/terrain/TERRAWEAVE.md` documents finite coordinate range.

**Verification:** still not compiled or executed in runtime; CTest and
CI results must be recorded after a successful run. World extent is
a validation limit, not proof of scalable streaming/rebasing.

**Next:** compile all three standalone CTest targets, fix any compiler
or runtime errors, and report checked platforms. Then implement
the first `gfx::Device` mesh upload/render adapter to display a tree
inside a real editor scene; add screenshot and FPS/RTS no-regression.

## Stage 2d — convert Arbor meshes to standard render::Model (COMMITTED; UNVERIFIED)

- `945e7e76a792f6269fbcd8d8ad0606b6f44c3cc6`:
  new `engine/render/TerraWeaveModelAdapter.h` converts three procedural
  geometry LODs into one `render::Model`, with 2 material primitives per
  mesh, Node root, explicit projected-size LOD transitions and
  `Model::finalize()`.
- `90bfd028d90d4628561920b7c8106d2838a27107`:
  `tests/terraweave_model_test.cpp` checks three meshes, bounds,
  material/primitive structure, indices and repeatability.
- `6745d9ea4ac8903fe25a09a0ea6384888f091e01`:
  `CMakeLists.txt` registers `aiverse_terraweave_model_test` /
  CTest `terraweave_model`, links `aiverse_models aiverse_zstd_dec`,
  and enables active assertions in Release for all TerraWeave tests
  on GCC/Clang.
- `b8948a55820b1b1650f03f2fa5cfb791bba5bab8`:
  `docs/terrain/TERRAWEAVE.md` records adapter API, units and limits.

**Tests**: not run or observed successful. Must compile and run:
```sh
cmake -S . -B build/terraweave-test -DAIVERSE_BUILD_TESTS=ON
cmake --build build/terraweave-test --target aiverse_terraweave_test aiverse_terraweave_arbor_test aiverse_terraweave_flora_test aiverse_terraweave_model_test -j
ctest --test-dir build/terraweave-test --output-on-failure -R '^terraweave(_arbor|_flora|_model)?$'
```

**Architectural choice:** reuse existing `render::Model` pipeline instead
of writing new low-level GLSL/MSL or backend-specific rendering code.
`makeArborModel` is CPU-only and converts cm to metres at render edge;
upload via `render::prepareModelUpload` and
`render::uploadModel(gfx::Device&, Model&)` on appropriate stage.

**Unfinished**: runtime/editor spawning and material textures, real
draw-call submission or instancing, impostor baking, wind, collision,
node graph/authoring and visual proof. The procedural leaves currently
render as **opaque green cards** unless a material/texture is supplied.
Prototypes must be cached/reused; per-tree unique Model allocations
are not acceptable for large forests. No GPU benchmarks taken.

**Next agent acceptance test:** open a real scene in AIVERSE and draw
an Arbor prototype via standard renderer with several instances,
then wire the same to flora groups. Validate on Metal/GL/Vulkan,
compare screenshots and existing RTS/FPS test checksums. Fix compile
and tests before extending further. Append the next stage to this file.

## Stage 2e — isolated TerraWeave GCC/Clang CI (COMMITTED; RUNS PENDING)

- `1fbbeb681201dd3d48dbabc4ba9c36641078f58c`:
  added `.github/workflows/terraweave.yml` (push to feature/terraweave,
  PR relevant changes, manual dispatch). Matrix GCC and Clang on Ubuntu,
  Release builds. Builds only the four TerraWeave CTest targets and runs
  the `^terraweave(_arbor|_flora|_model)?$` suite. Enables test checks without
  requiring the entire 1258-target AIVERSE build to succeed.

**Observed broader CI failure (PR #24 initial head
`f6491d60360a5703ce67906a336569862ca446c1`):**
- Workflow `CI` run `44296668203`, Linux ARM64 job
  `134775374705`: **failed** during overall build.
- Error in pre-existing ECS 2.0 experimental test compilation,
  `tests/ecs_wave_executor_test.cpp` (unused variables/functions) and
  `tests/ecs_experiments_test.cpp` (unused variables/functions), under
  `-O3 -DNDEBUG -Werror`. Compilation was stopped before full suite.
- These are *not* TerraWeave source files. Do not claim TerraWeave
  unit tests failed or passed based on this broad-build error.
- ECS team is working in parallel; avoid modifying those tests in this
  branch without coordination. Track them as unrelated CI blockers.

**Verification status as of this documentation commit:**
isolated CI workflow created; no successful run/result observed yet.
Review the new workflow runs on latest branch/PR head, fix any
TerraWeave-specific GCC/Clang errors and append exact results here.

When ready, continue the visible forest scene integration and
instanced prototype reuse. Keep this PR Draft until compilation,
render screenshot and core engine compatibility checks are verified.

## Stage 2f — existing legacy heightfield editor adapter (COMMITTED; UNVERIFIED)

Commits:
- `a3b46c2de44bb9c88c814679e839b1fb03989120`:
  `engine/world/TerraWeaveHeightmap.h` maps integer-cm terrain
  to existing signed int16 1/16-metre corners.
- `2c1b36777ca2f026191cbb89682fdbbc5f279fd5`:
  `apps/editor/core/TerraWeaveMapAdapter.h` provides optional
  `bakeTerraWeaveTerrain(MapDoc&, ...)`. It refuses to replace authored
  corners by default; uses `beginEdit()`/ `touched()` to preserve
  undo and viewer rebuild. Flat maps only.
- `187ef5573d4712087d79fa78576f01e813b3f7d9`:
  validate map origin/span before signed world-coordinate addition.
- `eb1b38f843451f64b482f10116cbf47926a26d36`:
  `tests/terraweave_heightmap_test.cpp`: corner seams, integer
  negative heights, out-of-range values and invalid dimensions.
- `4ef95f969839fc6beec54f78646e4e823c066b5e`:
  `CMakeLists.txt` registers new `terraweave_heightmap` CTest target.
- `452950ef7c67177151eefe026efeb00e944dbfb3`:
  isolated TerraWeave workflow extended to 5 test targets.
- `341765f95bb9615b1c501b87381387a6fd041574`:
  API and caveats added to `docs/terrain/TERRAWEAVE.md`.

**Tests:** authored/committed, no observed successful execution.
Run all five:
```sh
cmake -S . -B build/terraweave-test -DAIVERSE_BUILD_TESTS=ON
cmake --build build/terraweave-test --target aiverse_terraweave_test aiverse_terraweave_arbor_test aiverse_terraweave_flora_test aiverse_terraweave_model_test aiverse_terraweave_heightmap_test -j
ctest --test-dir build/terraweave-test --output-on-failure -R '^terraweave(_arbor|_flora|_model|_heightmap)?$'
```

**Important integration limitations:** adapter is callable C++, not an
editor UI panel/MCP tool, and the inline `MapDoc` wrapper does not yet
have an editor integration test. It is a destructive *bake* only with
explicit replacement, not a non-destructive generation graph with
painted override layers. No terrain collision/navmesh regen event
wiring yet beyond existing `MapDoc::touched()` rebuild semantics.

**Next acceptance:** compile; test the MapDoc adapter in an editor
test with `undo()`, `redo()`, save/load, `terrain.model` opt-in and
screenshot view. Add UI/MCP to preview/commit and persistent
generation settings; do not overwrite legacy maps automatically.


## Stage 2f — reusable ArborForestAdapter (COMMITTED; GPU/CTest UNVERIFIED)

**Branch:** `feature/terraweave` (PR #24, leave Draft).

**Commits (chronological):**
- `088027762542230e8260255182c215710736fe11`:
  `engine/render/TerraWeaveForestAdapter.h` — shared species prototype
  registry, GPU upload/release entry points, CPU model draw transforms,
  stable per-instance identities, origin rebasing, standard Renderer3D submit.
- `8d6a88d1880eb39ea40e075bae3e1816b08fb0f5` and
  `3019a72d02fb32ca53a79dc328fe479bb03e09bf`:
  `tests/terraweave_model_test.cpp` — assertions for prototype reuse,
  duplicate species rejection, per-tree world transforms, camera origin,
  impostor fallback, stable IDs and invalid inputs.
- `873d69ea785bc92bcfd77c5c90ce56e082d1ed48`:
  `docs/terrain/TERRAWEAVE.md` — new host integration/API section.
- This handoff update is another commit following the above; use the
  current `feature/terraweave` HEAD to locate the exact handoff SHA.

**Architecture and units:** `ArborForestAdapter` remains genre-neutral;
no ECS structural writes, no RTTI-specific or GPU-backend-specific code.
`addPrototype` creates one shared `render::Model` for each species.
`forEachDraw` emits lightweight `render::ModelDraw` descriptors with
persistent IDs and floats in relative metres, after subtracting an int64
centimetre origin. `submit` sends them through the *existing* Renderer3D
LOD/batching/shadow path. `uploadModels` / `releaseModels` run on the
render thread and use existing `gfx::Device` upload code. No per-tree
`render::Model` allocation. Caller must keep the registry alive while any
draw references its model. No new gameplay ECS component is required for
non-interactable foliage.

**Actual verification (this stage):** GitHub source files were read and
commits were created successfully. **No local compilation, CTest execution,
engine scene launch or GPU screenshots were run or observed** in this stage.
`tests/terraweave_model_test.cpp` was extended but is *not* yet known to
pass. The isolated GCC/Clang workflow may run after pushes; inspect its
actual run/job logs before marking tests as passing.

**Verification commands:**
```sh
cmake -S . -B build/terraweave-test -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_EDITOR=OFF \
  -DAIVERSE_BUILD_CLIENT=OFF -DAIVERSE_BUILD_SERVER=OFF \
  -DAIVERSE_BUILD_HEADLESS=OFF -DAIVERSE_BUILD_TOOLS=OFF -DAIVERSE_FBX=OFF
cmake --build build/terraweave-test --parallel 2 --target \
  aiverse_terraweave_test aiverse_terraweave_arbor_test \
  aiverse_terraweave_flora_test aiverse_terraweave_model_test
ctest --test-dir build/terraweave-test --output-on-failure \
  -R '^terraweave(_arbor|_flora|_model)?$'
```

**Known limitations:** the adapter is a bridge API, not a host runtime/editor
integration. No generated terrain or forest is drawn in a real AIVERSE scene
until a host constructs `ArborForestAdapter`, uploads its prototypes, builds
Flora batches and invokes `submit` between `beginScene` and `render`.
CPU `FloraView` distance LOD buckets are advisory; the renderer selects LOD
by pixel size. `TreeLOD::Impostor` falls back to lowest geometric LOD
because a billboard atlas is not implemented. Leaf material is still an
opaque green placeholder; alpha masking and wind remain outstanding.
GPU instance/draw-call numbers and perf have not been measured. No changes
were made to legacy RTS terrain, save/replay/networking or editors.

**Next agent, exactly:**
1. Check current PR #24 isolated GCC+Clang CI run results and fix any
   TerraWeave compile/runtime failures first. Report exact commands/logs.
2. Wire an **opt-in** WorldScene/scene manifest or editor preview to this
   adapter using the standard model upload/renderer order, without
   changing legacy maps when TerraWeave is absent.
3. Add a real camera/render-frame smoke test and screenshot for a
   multi-species forest on Metal + at least one non-Apple backend. Verify
   model prototype reuse and actual draw/instance stats at 100/10k trees.
4. Then connect terrain corner heightfield and road/building exclusion
   adapters; verify FPS/RTS regression checksums and save/replay determinism.
5. Append a new stage to this exact handoff file after each phase.


## Stage 2g — structural BranchLOD per tree branch (COMMITTED; TESTS UNVERIFIED)

**Branch:** `feature/terraweave` (PR #24 remains Draft).
**Commits:**
- `3fbb6cafce85e70a7af0f4af62d8071878e1cdd0`:
  `engine/world/TerraWeaveArbor.h` — add `LeafCluster::parentBranch`,
  fill it from deterministic branch generation.
- `efdddbb9e41c39befaff7cb6c2716f029e6d88c0`:
  `engine/world/TerraWeaveArborMesh.h` — `ArborMeshSettings::leafPlanes`
  can choose a single leaf card or the previous crossed pair.
- `dda5e55d2582909019206945d32dd34bf7daebf5`:
  `engine/world/TerraWeaveBranchLod.h` — independent Full/Reduced/Minimal
  branch meshes; respect the leaf-to-branch relationship.
- `0ce7cd300e718b324e92f0ee8e8ad4c71febf325`:
  `engine/render/TerraWeaveModelAdapter.h` —
  `makeArborBranchLodModel` with one `render::Node` per branch and
  three mesh alternatives per node; the native renderer independently
  selects each node's projected-size LOD.
- `15c6e82d22afa8706d0388a5638c457b5d0a0848`:
  `tests/terraweave_model_test.cpp` — parent topology, per-branch
  triangles monotonically decreasing, LOD indices, repeatability and
  invalid thresholds.

**Architecture/API:** render-only, deterministic seeds, centimetre CPU
geometry -> metre model vertices, no new renderer or ECS entity per branch.
Existing `makeArborModel` kept unchanged for compatibility.
Default `BranchLodSettings` thresholds: 96 and 30 screen pixels.
Per-branch LOD works because existing `Renderer3DModels.cpp` evaluates
each `render::Node` and its own `MeshDef::lods`, independently of the
overall model. `TreeBranch::parent` remains the render node parent.
The renderer also considers the shadow-camera projected size, so shadow
LOD may require extra detail.

**Verification:** GitHub commits and source inspection completed.
**No compiler, CTest, GPU render or screenshot test executed/observed**
for this stage. Do not claim tests passed.

**Limitations:** one structural draw node per branch increases CPU
model traversal and may increase draw submissions. Existing renderer
instancing may group compatible meshes across trees, but no measured
numbers or large-forest benchmarks. The leaf cards near camera still
use placeholder opaque green unless project textures are supplied.
`BranchDetail::Minimal` preserves branches with 3-sided tubes + one
leaf plane; it does not merge branches or support full-tree impostors
(that follows in Stage 2h).

## Stage 2h — final alpha-masked whole-tree billboard (COMMITTED; UNVERIFIED)

**Commits:**
- `adf4f0184a6cca012276fbcb2e85f7313251559f`:
  `engine/render/TerraWeaveArborBillboard.h` —
  `makeArborBillboardModel` CPU-rasterizes a real transparent RGBA8
  tree silhouette (bark and leaf clusters) and a single XY quad,
  `render::AlphaMode::Mask`, no fake green rectangle.
- `a10670d6bf0dcf249a227fb85910689e5728c192`:
  `engine/render/TerraWeaveForestAdapter.h` —
  per-species model pair (near structural + far billboard),
  conditional Flora batch submission, view-facing yaw for far trees,
  upload/release via the existing gfx Device path.
- `f8c5935aa1ba1ec38b16c97e77c85db103305a09`:
  `tests/terraweave_model_test.cpp` — alpha pixels, mask,
  repeatable silhouette, model choice, facing and persistent IDs.
- `e5ff1bc696d40eb85faf0657f0626678f1b68fb3`:
  `engine/render/TerraWeaveForestAdapter.h` — camera extent hardening,
  default 8 radial segments to preserve three distinct geometry tiers.
- `d9831237ea54d5186ea5b13cd3de35e61db2c91b`:
  `docs/terrain/TERRAWEAVE.md` — updated native API/usage and
  explicit one-view procedural billboard limitations.

**Behavior and invariants:** no separate GPU pipeline. Only far
`TreeLOD::Impostor` Flora batches use the billboard; all closer
instances use one shared branch-LOD model per species. Every whole-tree
impostor is one quad, one alpha-masked material/texture, camera-facing
horizontal yaw and origin-relative transform. Both near and impostor
share the same stable `ModelDraw::persistentId`. Models are uploaded
and released by `ArborForestAdapter::uploadModels/releaseModels` on the
render thread. Textured billboard is generated on the CPU, with no
platform shader dependencies. Billboard has castShadow=false.

**Actual verification:** source changes pushed to GitHub and test
assertions updated. No local repository clone was available in this
runtime, and no network resolution for Git clone was available.
**CMake/CTest, real scene render, GLSL/MSL and GPU screenshots: NOT RUN.**
No CI pass/fail observed from the last queried head; check the newest
isolated TerraWeave GitHub workflow before asserting status.

**Reproduce (same CI target as previous stage):**
```sh
cmake -S . -B build/terraweave-ci -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_EDITOR=OFF \
  -DAIVERSE_BUILD_CLIENT=OFF -DAIVERSE_BUILD_SERVER=OFF \
  -DAIVERSE_BUILD_HEADLESS=OFF -DAIVERSE_BUILD_TOOLS=OFF -DAIVERSE_FBX=OFF
cmake --build build/terraweave-ci --parallel 2 --target \
  aiverse_terraweave_test aiverse_terraweave_arbor_test \
  aiverse_terraweave_flora_test aiverse_terraweave_model_test
ctest --test-dir build/terraweave-ci --output-on-failure \
  -R '^terraweave(_arbor|_flora|_model)?$'
```

**Next exactly:**
1. Review isolated GCC/Clang workflow results (and CTest), fix any
   header/adapter test errors first. In particular validate
   `makeArborBranchLodModel` with renderer upload and its per-node
   projected-size selection (including camera and shadow clip).
2. Add an opt-in forest placement path to the WorldScene runtime/editor.
   A standalone adapter header alone does **not** draw forests in any
   existing game. Keep legacy scenes unaffected if TerraWeave is disabled.
3. Capture a real GPU screenshot of a partially simplified tree, its
   individual branch LOD switch, then whole-tree billboard; verify
   GL/Metal/Vulkan and alpha masking. Quantify 100/1,000/10,000 tree
   model nodes, draw submissions, triangles and memory.
4. For production, add multi-view (8/16) baked albedo+normal/depth
   impostor atlas, temporal or dithered tree↔impostor crossfade and
   shadow policy to avoid visible pop. Consider branch cluster merge
   to lower draw count without losing local LOD selection.
5. Append complete measured results and a new handoff stage.

As always, no untested claim of a passing build, GPU visual quality,
or runtime integration is permitted.

## Stage 3a — editor/MCP procedural terrain controls (COMMITTED, TESTS UNVERIFIED)

**Branch:** `feature/terraweave`, PR #24 (Draft), base
`feature/universal-entity-inspector-fps`. This stage adds the first opt-in
user-visible editor control for terrain. It intentionally does not change
existing RTS/FPS maps unless a user invokes the generation action.

**Commits / changed files:**
- `2df5b10676bf83a521b7b2ab42ac38de9ba2b906` —
  `apps/editor/core/TerraWeaveMapAdapter.h`: refuse silently overwriting
  existing detail layers; when explicitly replacing, clear detail/splat/
  detailFile/scale after MapDoc undo snapshot, so corners become visible.
- `3fd0bab8de98ebdb27cafc36113706af7abec447` —
  `apps/editor/core/TerraWeaveOps.h`: `terraweave_preview` and
  `terraweave_generate` registerable `Workspace` operations with
  validated generator settings; version 1 generator metadata stored in
  `map.data.extra.terraweave`.
- `2d6e621e6968e7da7822746b73a2377309cdf960` —
  `apps/editor/core/TerrainOps.cpp`: registers the operations in
  `registerTerrainOps`, automatically used by editor and MCP.
- `2686779f35e5b44241605f6b54b74b248e4f1302` —
  `apps/editor/windows/TerrainWindow.cpp`: UI controls for seed,
  base height, relief, period, octaves; Preview and Generate buttons.
  Overwrite requires an explicit checkbox.
- `0aab2e0d2b6d5002aafb4577acecb631801dcfaf` —
  `tests/terraweave_ops_test.cpp`: deterministic 9×9 preview, versioned
  metadata, invalid configuration checks.
- `308aed89ce74076cb447637b80f18f9a696702b4` —
  `CMakeLists.txt`: `aiverse_terraweave_ops_test` / CTest `terraweave_ops`.
- `a34e7cde606cb75160ccc176027fb889743bb1ca` —
  `.github/workflows/terraweave.yml`: six independent TerraWeave
  CTest targets and paths for editor changes.
- `afa8e34ac57c119c688a127a84adb41b44981727` —
  `docs/terrain/TERRAWEAVE.md`: new public API, safety and limitations.

**Architecture:** `Workspace::call` is the single mutation entrypoint
for both GUI and AI/MCP. `terraweave_preview` samples without changing
MapDoc, `terraweave_generate` bakes to the existing corner pipeline,
invalidates views with `Change::Map`, records generator version/settings
for future regen and supports Undo. Detail data is *not* preserved in
the new map but is contained in the Undo snapshot. No simulation model
changes. These operations apply **only to flat RTS grid maps** because
`MapDoc::corners` is legacy RTS data; generic Terrain/Flora/Arbor code
remains cross-genre. No new GPU or shader code.

**Tests ACTUALLY performed:** source inspection and successful GitHub
commits via connector. **No build, CTest, live editor launch, screenshot,
MCP functional invocation, or FPS/RTS regression was observed** in this
authoring runtime. Do not claim passing tests; retrieve current isolated
workflow logs and record actual results before merging.

**How to test immediately:**
```sh
cmake -S . -B build/terraweave-test -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_EDITOR=OFF \
  -DAIVERSE_BUILD_CLIENT=OFF -DAIVERSE_BUILD_SERVER=OFF \
  -DAIVERSE_BUILD_HEADLESS=OFF -DAIVERSE_BUILD_TOOLS=OFF -DAIVERSE_FBX=OFF
cmake --build build/terraweave-test --parallel 2 --target \
  aiverse_terraweave_test aiverse_terraweave_arbor_test \
  aiverse_terraweave_flora_test aiverse_terraweave_model_test \
  aiverse_terraweave_heightmap_test aiverse_terraweave_ops_test
ctest --test-dir build/terraweave-test --output-on-failure \
  -R '^terraweave(_arbor|_flora|_model|_heightmap|_ops)?$'
```

**Risks/limitations:** undo/restoration not verified in a live editor;
numeric preview only, not a 3D preview; material/detail regenerated only
when user approves overwrite. Existing maps are unchanged unless opt-in.
Any async worker runtime must use revision tokens and deterministic ECS
commit order. Parameter metadata is versioned but loading does not
auto-regenerate it. Do not touch unrelated concurrent RTS/FPS/graphics
feature branches.

**Next agent task:**
1. Compile and run all 6 CTest targets, investigate fail logs and update
   handoff with platform, commands and final pass/fail.
2. Add an end-to-end editor/Workspace test: preview leaves revision/data
   unchanged; generate mutates corners; existing detail refuses by default,
   overwrite removes detail and single Undo/Redo restores; save/reload keeps
   `map.extra.terraweave` metadata and corners. Run a real editor screenshot.
3. Connect `ArborForestAdapter` to opt-in runtime/editor scene preview:
   cache shared prototypes, use `buildFloraBatches`, call existing
   `gfx::Device` and Renderer3D, measure draw calls and frame times.
4. Follow with biome masks, painted exclusion regions, terrain+water/roads
   ownership, camera-relative chunk streaming and shader alpha material.
5. Append another stage to this file with exact commits and verification.


## Stage 3b — shared multi-angle Arbor impostor (COMMITTED; TEST EXECUTION DEFERRED)

**Branch/PR:** `feature/terraweave`, draft PR #24 (base is
`feature/universal-entity-inspector-fps`). Existing sources are
concurrently edited by other agents; always fetch current branch HEAD
and file SHA before writing or rebasing.

**Code commits and files:**
- `7a9eeb2e507b0cde80dedf83e9704af38b2a0da7` —
  `engine/render/TerraWeaveImpostorAtlas.h` creates deterministic
  4/8/16-view RGBA8 silhouette atlas, one base model and one stable
  Material override vector per view; provides azimuth selection.
- `15b743d99eaf8a7178ec97de13fa289e8ba23118` —
  `engine/render/TerraWeaveForestAdapter.h` chooses camera-relative
  atlas frame for `TreeLOD::Impostor`, reuses one GPU model and
  texture per species, assigns `ModelDraw::materials` to cached view.
- `8d6b0ae05f7c6f3c349ec973e198ca8a3cdd10b5` and
  `b5d3494a8bea3b19040d02c61f29b232ffc56ca9` —
  `tests/terraweave_model_test.cpp` updates *deferred* assertions for
  atlas dimensions, deterministic colours/pixels, material UV slices,
  camera azimuth and per-tree orientation.
- `8503c32cce5d8d3b45ebdc0eb85d337e2b3d126a` —
  `docs/terrain/TERRAWEAVE.md` API/architecture.
- This handoff update is committed separately; inspect latest HEAD
  for its own commit SHA.

**Design:** geometry remains shared among all trees of a species.
Each eight-view impostor uses one 1024×256 RGBA8 texture, one four-vertex
model and eight `Material` override vectors with UV transforms.
It **does not duplicate models or textures per camera direction**.
`ArborForestAdapter::forEachDraw` computes a world-camera vs
tree-local-yaw azimuth index; `Renderer3D` groups draw submissions
via its normal material-state logic. CPU baker is independent from
renderer backends/genre. All coordinates are signed cm until the
existing presentation adapter converts them to metres.

**Actually verified:** GitHub source reads and pushes only. User asked
to defer tests. **Compilation, CTest, scene screenshots, GPU captures
and benchmarks NOT RUN.** Added checks are still unverified.

**Known limitations:** rasterized cards use simple colours (not textured
albedo/normal/depth), nearest-angle selection may pop during camera
orbit, multi-view atlas only supports fixed horizontal azimuths.
Alpha-edge filtering/mipmap coverage and exact draw batching still
require GPU verification. Scene/editor auto-spawning is not provided
by this stage. No gameplay ECS state or legacy map files were touched.

**Next:** add deterministic spread of the geometry→impostor transition
to avoid a sharp visible LOD ring; optionally crossfade individual
trees using standard renderer mechanisms after checking material
semantics. Implement an opt-in runtime forest-scene host that owns
persistent prototypes, bounded placement chunks and upload lifecycle.
Capture screenshot/performance once the user requests tests.

## Stage 3b — optional desktop editor integration regression target (COMMITTED; NOT RUN)

**Branch:** `feature/terraweave` / PR #24 Draft.
**Commits:**
- `add07dc38341b2ef539bc61cec5b9e7d0ab79397`:
  `skills/world/aiverse-terraweave/SKILL.md` documents safe
  `terraweave_preview` / `terraweave_generate` agent workflow.
- `7393938e914ee647bf2b57b9e9d9395ebffb8fe9`:
  `tests/terraweave_editor_test.cpp` constructs the actual
  `Workspace` registry and `MapDoc`, tests preview without state
  mutation, generation, default refusal to overwrite, persistent
  `map.extra.terraweave` metadata, Undo/Redo, and detail/splat
  replacement with restoration.
- `524a6b4da21eb9d774ba722d45ead7ced83226c6`:
  `CMakeLists.txt` adds `aiverse_terraweave_editor_test` / CTest
  `terraweave_editor` **only if `aiverse_editor_core` is built**.
  Requires desktop `AIVERSE_BUILD_TOOLS=ON` or
  `AIVERSE_BUILD_EDITOR=ON`; intentionally not included in isolated
  six-test CPU-only workflow.
- `a0691a2790bb2960234bdc55dc1979e383d2f3e4`:
  updated the integration test to call with source `editor` to
  avoid writing `logs/editor.log` during test execution.

**Verification:** committed to GitHub. No local compilation or
CTest/editor runtime was executed/observed, so status is UNVERIFIED.
Check compilation before claiming integration works. Run:
```sh
cmake -S . -B build/terraweave-editor-test -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DAIVERSE_BUILD_TESTS=ON \
  -DAIVERSE_BUILD_TOOLS=ON -DAIVERSE_BUILD_EDITOR=OFF \
  -DAIVERSE_BUILD_CLIENT=OFF -DAIVERSE_BUILD_SERVER=OFF \
  -DAIVERSE_BUILD_HEADLESS=OFF
cmake --build build/terraweave-editor-test --target \
  aiverse_terraweave_editor_test --parallel 2
ctest --test-dir build/terraweave-editor-test --output-on-failure \
  -R '^terraweave_editor$'
```

**Next step:** run the tests on macOS/Clang (primary desktop) and
Linux/GCC, fix any API or CMake linkage issues; verify a real editor
can generate, Undo, save and reload with a scene screenshot.
Then connect opt-in runtime forest preview using the already-present
`ArborForestAdapter` and reuse `engine/world/AsyncChunkStream.h`
rather than introducing a duplicate chunk job scheduler. For each
later phase update this handoff including exact tests and commits.


## Stage 3c — stable forest-wide transition band (COMMITTED; TESTS DEFERRED)

- `50bc00298e0ad5d9c27dd895b016919513c70810`:
  `engine/world/TerraWeaveFlora.h` adds
  `FloraView::impostorBlendWidthCm`, a deterministic per-tree choice of
  low geometry vs far impostor threshold. Default remains 0.
- `455d319bc5d6265b8a1e4837e5a0e024bc6f7e94`:
  `tests/terraweave_flora_test.cpp` records deferred checks for
  output stability across shuffled input, transition endpoints and
  invalid ranges.
- Updated documentation in `docs/terrain/TERRAWEAVE.md`.

**API:** All distances in integer centimetres. A per-tree stable
hash combines persistent key and species; band midpoint is
`lowRangeCm`. No new gameplay state, frame history or GPU resources.
A 0-width band preserves prior exact behavior. Population stagger is
not an individual tree crossfade.

**Actual tests:** GitHub writes/source review only; no compiler/CTest,
live render, screenshot or GPU benchmark run, by explicit user request.

**Next agent:** implement opt-in chunked forest host and eventually
transition crossfade/dither in the shader if visually necessary. Never
claim per-tree fade or flicker-free camera orbit without GPU verification.

## Stage 4a — opt-in editor forest scene overlay (SOURCE COMMITTED; UNVERIFIED)

Branch: `feature/terraweave`, draft PR #24, base
`feature/universal-entity-inspector-fps`. Other agents are actively
editing this branch: fetch current HEAD and each file SHA before modifying.

**Commits/files (2026-11-18):**
- `f3f2bd007c88724af210f7730ede2d7f594b498b`:
  `apps/editor/TerraWeaveForestPreview.h`. Editor-only owner of a
  reusable `ArborForestAdapter`, cached 5x5 presentation tiles
  around camera (32m tile); deterministic local placement from
  `map.extra.terraweave` recipe. Two common tree prototypes
  (broadleaf/conifer) shared across instances. Uses existing
  `scatter`, `buildFloraBatches`, renderer, LOD atlas, and
  `gfx::Device` lifecycle.
- `157de8af70245766ac09153deec47a35470f4632`:
  `apps/editor/Editor.h`: persistent preview instance and
  window-accessible toggle/count.
- `e1efeafc2b7113cf8182fc7bc22bc0f0875f8d94`:
  `apps/editor/Editor.cpp`: submit forest after existing
  `RtsView::draw` and before `Renderer3D::render` in Scene view,
  sample *current* (possibly sculpted) editor heightfield, release
  uploaded resources on editor/project/map lifecycle.
- `a3f34307123d34c0e7b81153538dda2a8491f34b`:
  correct global scatter coordinates vs map-local render coordinates,
  including nonzero generation origins.
- `75e31f36d852e5cf8ec4fb1497dfd21a3813b586`:
  `apps/editor/windows/TerrainWindow.cpp`: explicitly opt-in
  `Показывать процедурный лес в «Сцене»` checkbox and visible
  placement count. Disabled when the map has no generator recipe.

**API/data flow:** Existing `MapDoc` generator recipe is read only.
Prototype models are generated once, uploaded via `gfx::Device`,
and reused in `Renderer3D::drawModel`. Placement buffers are
recomputed only when map revision, saved recipe or camera tile
changes. Forest is for editor display only, not game save/ECS
entities. World positions stay integer centimetres until the
existing renderer bridge. Height is sampled from current visual
map so trees follow manual sculpt changes. Gameplay, physics,
navmesh, FPS and planet map remain untouched.

**Limitations:** Preview is not yet an ECS forest runtime.
No tree collision, road/building exclusion (beyond biome/height/
slope filtering), seasons, growth, true texture-mapped tree
materials or incremental streaming jobs. The 5x5 tiles are
generated synchronously on the editor render thread; bounded
cost but not suitable as production streaming. Runtime streaming
should use existing `AsyncChunkStream` and revision-safe commits.
No screenshot or FPS measurements. The renderer shader/alpha atlas
behaviour is still unverified on Metal/GL/Vulkan.

**Verification actually performed:** GitHub commits and inspection
of real `Editor::renderScene`, `Renderer3D` and `MapDoc` APIs.
No local compilation/CTest/editor launch/GPU screenshot has been
run or observed. DO NOT mark as passing or ready to merge.

**Next concrete tasks:**
1. Compile the entire `aiverse_editor` target (Mac/Clang first),
   fix any API or linkage errors and record exact logs.
2. Add a CPU-only/editor compile smoke target for
   `TerraWeaveForestPreview.h`, and tests for coordinate rebasing,
   stable placement and map-revision refresh.
3. Preview in editor: generate terrain, toggle forest, orbit camera,
   verify branch LOD and impostor transitions via screenshots.
4. Add road/building/water masks, prototype configuration and
   configurable preview budget. Then integrate async chunk streaming
   via existing `AsyncChunkStream`, not a duplicate scheduler.
5. Append a fresh handoff stage with measured tests and next step.


## Stage 3d — opt-in native & WorldScene streamed forest (COMMITTED; TESTS DEFERRED)

**Branch:** `feature/terraweave`, draft PR #24; base
`feature/universal-entity-inspector-fps`. Do not modify legacy
game projects or main automatically.

**Commits / changed files:**
- `13e0cf245704eed1a93daa4979c3e13f279b21a6`:
  `engine/world/TerraWeaveForestScene.h` — persistent shared model
  prototypes, bounded per-chunk CPU foliage cache, deterministic nearest
  residency/eviction, explicit exclusion invalidation, GPU lifecycle and
  Renderer3D submission, CPU stats.
- `d87e17e53aa7a33441fc326831fa062f7eb0550d`:
  `engine/world/TerraWeaveForestManifest.h` — optional JSON schema
  parser and `forestSceneFromManifest`, validated species/foliage
  configuration, seeds, camera LOD ranges and rectangular exclusions.
- `9c94303571be1eeb0d095da593ebee3e64f08392`:
  `engine/world/WorldScene.h` — optional forest host ownership and
  model-upload state.
- `47e2bea0006a710d36eebf35515e2389f5b54751`:
  `engine/world/WorldScene.cpp` — create host if root
  `terraweaveForest` exists; build and release resident CPU patches
  in `update`; upload prototypes through existing GPU update stage,
  submit between Renderer3D begin/render in `draw`, release in shutdown.
- `438bb52c0265ebeead2b24d9ae0c844469442833` and
  `53b416df88ba94e907da97b3f13eb44b51eeff0f`:
  forest host/schema defaults aligned so a 24,000 cm render cull is
  covered by a 6,400 cm chunk × 4-chunk resident radius, with 64
  resident chunks budget; avoid the previous 50,000 cm cull vs
  3,200 cm × 3 radius mismatch.
- `c84eca7792e26bffc4302106a8ec7ee0463ab145`:
  `docs/terrain/TERRAWEAVE.md` — opt-in manifest example, public
  native API and limitations.
- This stage handoff is another documentation commit; retrieve the
  current branch head for its SHA.

**Architecture/API:** `TerraWeaveForestScene` can be called directly
from any native genre host (no RTS dependency). `addSpecies` registers
`TreeProfile`, `FoliageSpecies` and mesh seed before first render.
`streamAt(ForestCameraCm)` uses integer cm and pure deterministic
`scatter`, caches only the nearest bounded chunks, and gives stable
instance identities regardless of chunk eviction/regeneration.
`setExclusions` accepts world-space half-open rectangles and invalidates
CPU patches. `visibleBatches` uses existing Flora LOD. Render thread
calls `uploadModels`, `submit`, `releaseModels`. `WorldScene`
automatically owns the host **only** when the top-level manifest
contains an explicit `terraweaveForest` block. Without it, the
old path is unchanged. A tree is cosmetic, not a per-tree ECS entity.

**Actual verification performed:** read target source and committed
changes successfully through the GitHub connector. **No CMake/CTest,
binary launch, GPU screenshot or performance measurement executed**;
tests were explicitly deferred by the user. No claims of CI passing.
No source files under unrelated agent features modified.

**Known limitations / risks:**
- CPU `streamAt` is synchronous, bounded but may cause a hitch
  when many patches first enter range; no worker queue/cancellation yet.
- Forest manifest is only integrated into the existing asynchronous
  `WorldScene` host, not automatically into the RTS MapDoc editor.
- Only first observer controls forest residency; multi-observer union
  needs a future policy.
- Procedural forest height samples from TerraWeave seeds, not arbitrary
  authored terrain/physics heightmap; trees may float/intersect the
  independently authored ground until a height source adapter exists.
- No gameplay collision/navmesh obstruction, save/harvest, growth,
  water and road generator bindings or editor forest placement UI.
- No budget/perf proof, alpha atlas GPU proof, or actual viewer demo.
- Implementation assumes the normal WorldScene GPU update/draw/shutdown
  ordering; validate all three in a real scene after tests resume.

**Concrete next steps:** implement a generic CPU height source override
for scattering plus road/exclusion integration to align tree bases to
authored terrain; then move streaming generation to worker jobs with
bounded staging and revision-check commit, maintaining deterministic
order. Wire an opt-in editor forest preview, inspect model LOD and
GPU batching, and only afterward perform CMake/CTest, headless,
Metal/GL/Vulkan screenshots, FPS/RTS regressions and benchmarks.
Append a new handoff after each phase.


## Stage 3e — bounded residency work and upload rollback (COMMITTED; TESTS DEFERRED)

**Commits:**
- `e48735049c7b702142cdc59eac4f8f85a2043cce` —
  `engine/world/TerraWeaveForestScene.h`: validates candidate
  lattice density across the full resident cache. Caps one chunk
  at 65,536 candidate cells and the maximum scene allocation/work
  estimate at 1,000,000 candidates. Validates `FloraView` LOD
  ranges early. On partial GPU prototype upload failure, releases
  already uploaded model resources rather than leaking them.
- `07836187c1f5109d60f65c7b21cfb15a647ddab0` —
  `skills/world/aiverse-terraweave/SKILL.md`: instructs agents
  how to use the new atlas/forest host, and warns of deferred tests.

**Architecture:** CPU scatter bound is validated in
`TerraWeaveForestScene` constructor. No change to deterministic
scatter algorithm, global lattice positions, ECS simulation or
legacy projects. GPU ownership remains the render/update thread
via `gfx::Device`. No new direct graphics backend calls.

**Actually tested:** no binary compile/test, live scene or screenshot
run, as requested. GitHub commit succeeded. This cap is a guard
against pathological configurations, *not* a measured latency
guarantee.

**Next agent:** during formal verification, check that
`AIVERSE_BUILD_TESTS=ON` compiles the new WorldScene integration,
run `terraweave_model`/`terraweave_flora` and true runtime
WorldScene capture, validate memory/draw count under the bounds.
Implement asynchronous streaming with bounded pending job queue and
stale revision tokens before marketing this as non-blocking.

## Stage 4b — placement masks, smoke test, GPU cleanup (COMMITTED; UNVERIFIED)

**Branch**: `feature/terraweave`, draft PR #24; another agent
may be working on this branch concurrently. Always refetch HEAD.

**Commits/files:**
- `e3abd75038d0217c67dcd4502ee98dfc7fba6c0f`:
  `apps/editor/TerraWeavePlacementMask.h` — editor-only placement
  filter for non-ground map cells, `DocEntity` exclusions and
  `MapData::roads` line-clearance.
- `fc704fc2559b276abba6ea50850d11aa86d57677`:
  explicit include of `engine/world/TerraWeave.h`.
- `5266194bb10185855b0fc9a968b9b2fe9b7d0ce6`:
  `apps/editor/TerraWeaveForestPreview.h` invokes the mask before
  queuing tree instances.
- `c74a673db757d2a80ed987e55590b1b769ca0f15`:
  `tests/terraweave_preview_test.cpp` checks preview opt-in state,
  cell mask, entity clearance, road segments, outside-map values
  and invalid settings.
- `ba92d702ffe3d420e17fef24b5ff3fc637a75a52`:
  registers `aiverse_terraweave_preview_test` in CMake.
- `bf2a64e2835830bd41dd82aaffa02f8e218f2dd0`:
  adds preview target + changed editor paths to isolated
  `.github/workflows/terraweave.yml`.
- `22e29e98be046208ce375db85af68e119bdfdae8`:
  `apps/editor/Editor.h` releases GPU forest models when user
  unchecks preview.
- `878282e69641d274a41cd500ceddda5c49fae3d2`:
  architecture/limits added to `docs/terrain/TERRAWEAVE.md`.

**Implemented API**: `terraWeavePreviewAllowsPlacement(const MapData&,
localXcm, localZcm, cellSizeCm, entityClearanceCm, roadClearanceCm)`.
Pure editor presentation policy, not authoritative ECS or collision.
Uses squared distance to legacy roads and filters cells/objects.
No new GPU shaders/pipelines or gameplay mechanics.

**Actual verification**: Source commits succeeded. A diagnostic
`git ls-remote https://github.com/chromealex/AIVERSE.git` from the
authoring container failed with
`Could not resolve host: github.com`; therefore no full repo checkout
and no local CMake build, CTest, editor launch or screenshots were run.
GitHub workflow results have not been independently observed passing.
**All tests remain unverified.** Do not claim otherwise.

**Known limitations**: This is a small synchronous editor overlay,
not streaming runtime foliage. 5×5 tile placement is regenerated on
camera tile/map revision; CPU stalls are possible on dense or large
scenes. It doesn't yet parse `roadNetwork` and `waterNetwork`
splines, footprint data or physics/AI collision. Preview foliage is
not saved as authored trees and does not become ECS entities.

**Next agent exactly**:
1. Configure/build `aiverse_editor` and run isolated CTests for
   `terraweave_preview` + other TerraWeave targets. Fix errors and
   record actual commands/output.
2. In a real editor project, use `Рельеф → TerraWeave` to generate,
   enable forest overlay, orbit/zoom; capture close/medium/far
   screenshots with Metal and GL/Vulkan and measure draw/triangles.
3. Implement non-blocking `AsyncChunkStream` integration with bounded
   jobs and stale revision discard. Ensure all GPU submissions remain
   on rendering thread. Use authored road/water splines as exclusion
   providers and correct building footprint mask.
4. Add species/biome authoring and persistent interactive tree ECS
   integration as separate opt-in packages, not per-grass entities.
5. Append another handoff after every stage with tested outcomes.

## Stage 4c — correct map-unit scaling and preview lifetime (COMMITTED; UNVERIFIED)

**Changes/commits:**
- `3127556ebc92e2168379112b0cac9d3990fa5c51`:
  `apps/editor/Editor.cpp` releases and disables forest preview on
  project/map changes; automatically releases stale GPU resources if
  an Undo removes the generator recipe while the Terrain window
  is closed.
- `6d00097e2b7b817b50099d0acc5102e2deb4b38e`:
  `apps/editor/TerraWeaveForestPreview.h` fixes camera-to-global
  and world-to-map coordinate conversion when
  `LegacyHeightfieldOptions.cellSizeCm != 100`.
- `2fd0b6c628439fbedc0081e1c44581c035ebd99d`:
  `apps/editor/TerraWeavePlacementMask.h` adds validated
  `terraWeaveWorldFromEditorCm` and
  `terraWeaveEditorFromWorldCm`.
- `e13006e86020624a42d80c16321d6032b5f2f0e0`:
  `apps/editor/TerraWeaveForestPreview.h` uses those helpers
  for source sampling and renderer placement.
- `eed584129006e7d77677e9d16c6438f316a32bab`:
  `tests/terraweave_preview_test.cpp` adds 150/200-cm
  generator spacing, signed-origin and invalid-spacing cases.

**Why:** MapDoc's renderer always treats each authored map cell as
one metre. TerraWeave can use an arbitrary positive world-cm
sample spacing for the procedural source. Without a conversion,
tree positions and camera-based chunk selection move incorrectly
relative to the existing map when cell size differs from 100 cm.
The current code normalizes trees into editor-local 100 cm/cell
before building GPU transforms and placement masks.

**Actual verification:** source pushed through GitHub connector;
no complete checkout, compile, CTest or GPU screenshot observed.
Local GitHub clone remains blocked by DNS resolution in this
runtime. Keep PR Draft until verified.

**Next:** run all seven CPU/compile tests plus
`terraweave_editor` and a full editor build. Add a visual
comparison for `cellSizeCm=100` vs `200`, and verify that
the world-space camera tracking/LOD selection remains stable.
Move costly per-tile scatter to the existing async chunk scheduler
once baseline GPU behaviour is measured.

## Stage 5 — external height and placement sources for universal forest (COMMITTED; UNVERIFIED)

**Branch**: `feature/terraweave`, Draft PR #24. Concurrent agents
are changing adjacent TerraWeave components; fetch current file SHAs.

**Files/commits**:
- `5c2cf60f533ebf3a2ac8613095c5b374963261c2` —
  `engine/world/TerraWeaveForestScene.h` adds optional
  `HeightSource = std::function<int32_t(int64_t,int64_t)>`
  and `PlacementFilter = std::function<bool(const FoliageInstance&)>`
  with `setHeightSource`/`setPlacementFilter`. Both invalidate the
  resident cache and revision when changed. During `streamAt`,
  placement instances are re-elevated from the provider and rejected
  by the optional placement filter before publishing to host cache.
  Samples outside supported ±1,000,000,000cm heights are rejected.
- `4ed8816a3ac95a873d4e52cc09b8b202d9de9811` —
  `tests/terraweave_forest_scene_test.cpp`: verifies height
  callback updates while preserving placement keys, filters,
  invalidation/revisions and an invalid height exception.
- `e170f218bdfce3c5124faba2bb1938d4f8c7ee33` —
  registers `aiverse_terraweave_forest_scene_test` in CMake.
- `ed67760eb9c9d6b18cb841d6137360608c34ea47` —
  includes the new target in isolated GCC/Clang workflow.
- `e31b0fe1c820a0db9980b39c610953ddfcdb393d` —
  `docs/terrain/TERRAWEAVE.md`: public provider API and limitations.

**Design:** The generic forest host does not depend on any genre or
specific MapDoc/WorldScene collision representation. Caller supplies
an immutable snapshot-based height/predicate provider. Placed trees
remain cosmetic; geometry reuse, collision, ECS and GPU ownership do
not change. No extra RHI backend. Applying providers changes only
the final height/allow-list, NOT the underlying procedural noise
biome/slope prefilters. A project requiring physically correct
authored-surface biomes must supply/extend a full terrain evaluation
provider in a subsequent stage.

**Actual verification:** C++ code and test files committed via
GitHub connector. No local CMake compilation, CTest, editor launch,
GPU screenshot or performance test observed. Container GitHub
DNS/network lookup failed during prior steps. The user had previously
deferred full tests: do not report them as passing.

**Next agent:** integrate the provider with the existing
`WorldScene` terrain/collision snapshot (with exact coordinate
conversion), and build the typed road/water/structure exclusion
provider. Prefer immutable snapshots for eventual worker threads,
with ownership/lifetime and stale revisions documented. Then
implement bounded asynchronous patch jobs or reuse the existing
`AsyncChunkStream`, rather than generating terrain synchronously
on the renderer thread. Add targeted tests for overridden height
matching physical collision, camera origin rebasing, and stable
multichunk persistence. After formal tests resume, compile all eight
isolated TerraWeave CTest targets and optional `terraweave_editor`;
inspect Metal/GL/Vulkan screenshots and stats.


## Stage 6 — authored-world forest alignment, mask correctness and persistent tree identities

**Branch**: `feature/terraweave`, PR #24 remains Draft. Files/commits
completed in this stage:

1. `9435bb8d9f64a8a82a0d5166fe8c8ee5c0a06b0b` —
   `engine/world/TerraWeaveForestScene.h`: fixed both sync
   `streamAt` and async `preparePatch` so a **standalone**
   `setPlacementMask` works even with no height or predicate callbacks.
   Previously the mask was silently skipped in that case.
2. `ca3c1e63648e76408984279f060ba1afbe52623e` —
   new `engine/world/TerraWeaveWorldHeightSource.h`:
   immutable `AuthoredWorldHeightSnapshot` and typed height tile,
   triangulated interpolation matching WorldScene collider mesh,
   signed global world centimetres with negative-safe cell indexing.
3. `7c0c8d5453f507d1dbc3e83c16aac2067dbcaea1` —
   `engine/world/TerraWeaveForestScene.h`:
   `setLandscapeSources(HeightSource, shared_ptr<const FoliageExclusionMask>)`
   publishes a single coherent height/footprint source revision rather
   than invalidating all queued async work twice.
4. `2fd9f5e1ab92832df63d9a9783e0a24a994523d3` —
   `engine/world/WorldScene.h`: optional `Prepared` per-chunk
   height samples and footprint geometry.
5. `3aa68800b4551a251f2020cb0bbbe2dc4425a68b` —
   `engine/world/WorldScene.cpp`: convert loaded terrain Fixed
   metres to centimetres during worker preparation, account snapshot
   CPU footprint against chunk reservations, rebuild one immutable
   resident terrain + collision-box mask when a relevant chunk is
   published/unloaded, and publish both to the forest via one
   revision-safe call. Fallback to procedural height if no loaded
   authored terrain tile covers a sample. Does not run on legacy
   scenes lacking `terraweaveForest`.
6. `f2f063d6f1ab46b9c2136dc47d7f554f478ce3dc` —
   `engine/world/TerraWeaveExclusions.h`: spatial bucket lookup for
   collision building footprints (64 m cells, >256-bucket shapes in
   an overflow list). Avoid scanning all streamed building boxes
   for every forest candidate.
7. `2a7304027c7663f3be79b712d94273e8556125a1` —
   `engine/world/TerraWeaveForestScene.h`: stable
   `ForestTreeIdentity{key,speciesId}` removal/restoration API and
   sortable snapshot for native game save integration; filters only
   `visibleBatches()`, so the same tree stays removed after cache
   eviction/regeneration. Bound set at 1,000,000 identities and expose
   a `removed` count in scene stats.
8. `427a2f3ccd4d6b25019503264ecb1082b1bda8da` —
   `docs/terrain/TERRAWEAVE.md`: implementation contracts, units,
   limitations, and examples.

**Correctness contract:** The typed authored source matches the
triangulation used by `readChunkCollision`, with centimetre-level
rounding; it never updates simulation physics or authoritative ECS.
The snapshot and mask are immutable during worker use; old
snapshots stay owned by callbacks captured in a revision of
`TerraWeaveForestScene`. No worker may mutate GPU state or ECS
structural data. A changed landscape invalidates previous foliage
patches. All tree positions remain signed cm until the render adapter.

**Compatibility and limitations:** Unmodified/disabled WorldScene
manifests are unchanged. Only a manifest with explicit
`terraweaveForest` includes this host integration. Resident authored
heightfield tiles are used; unloaded tile samples fall back to
procedural noise and can exhibit visible terrain boundaries.
Collision boxes are conservatively projected to X/Z regardless of Y;
roads/water/complex meshes need their own explicit masks.
The scatter core still chooses species by its own procedural
biome/slope, so an authored steep/underwater surface needs a custom
placement policy for correct ecology. Rebuilding a snapshot when
terrain residency changes invalidates all current async patches,
which may cause temporary missing trees during frequent chunk churn.
`removedTrees()` is a visual state export, not an authoritative
save/network/destruction system and needs generator version/seed
matching for persistence.

**Actual tests:** user explicitly requested to run tests later.
Only GitHub file reads and successful code/doc commits were
performed; **no CMake/CTest, runtime, GPU screenshot, performance
benchmarks, headless cross-genre replays or CI success verified**
for this stage. These implementations must be considered
unverified until the user resumes testing.

**Next agent instructions:**
- Inspect HEAD and current file SHAs before editing, because other
  agents are committing simultaneously on this feature branch.
- Once tests resume, build the TerraWeave targets + `aiverse_world` /
  `aiverse_editor`, exercise negative-coordinate tile edges,
  irregular terrain resolutions, collision-box footprints, sync and
  async mask-only settings, concurrent landscape revisions, undo
  of editor heightfield changes, and persistence across patch unload.
- Attach the native save layer to `removedTrees()` with generator
  fingerprint/version; add opt-in gameplay tree interaction ECS,
  collision/navmesh footprint updates and network replication using
  generic mechanics modules, not genre-specific one-off code.
- Consider per-tile revision invalidation rather than invalidating
  the whole forest when a single WorldScene tile arrives; this is
  a performance enhancement, not yet implemented.
- Finish true multi-view impostor image-quality work and capture
  actual Metal/GL/Vulkan outputs when visual tests resume.
- Append the next stage to this document, recording real results.

## Stage 6 — bounded async forest jobs + typed exclusion geometry (COMMITTED; NOT TESTED)

Work on this branch was coordinated with a parallel agent; retrieve
current HEAD before changing anything under `engine/world`.

**Commits:**
- `833582c98bef6cbd1dc452467198dc1bf0258c36` and
  `76704ec0469743ce98b1b7a2d1a3222d775a0609`:
  `ChunkStreamer.h/.cpp` supports dynamic removal of catalog cells.
- `6edaaee27906c71c0cc93e34f0dbea707a560185` and
  `3015096bb6a7692d753f0b45a0b93a47680654c5`:
  `AsyncChunkStream.h/.cpp` can remove canceled dynamic cells,
  retaining staging reservations until completed/canceled work drains.
- `1a9c083964089440b27fc6eaaa96545072b812a2` and
  `7a4b7466e33c4c993fba9bcaf16449e5f0596ac6`:
  `TerraWeaveForestScene.h` adds opt-in worker scheduling,
  bounded chunk reservations, snapshot revision tokens,
  stale-result rejection and render-thread publish.
- `478b5de3ef06bf142c1841d863f9a5a962784ac9`:
  `TerraWeaveForestManifest.h` enables bounded asynchronous
  generation by default only for the opt-in WorldScene manifest.
  Direct native `ForestSceneSettings` construction remains synchronous
  unless opted in.
- `17c0f3a94f08152190ab669065234f23445b3667`:
  `WorldScene.cpp` joins forest worker before shutdown.
- `79f9928de1bd085960c3557b26003855da1c18f9` and
  `cb762cf2fd9724b3f10a6909fe6ef62afecbe1ce`:
  `TerraWeaveExclusions.h` (buffered road segments, rectangular
  structures, footprint/water polygons) and
  `TerraWeaveForestScene.h` mask callback support.
- `80aa190851de58a9452ef2fb7c7f156bba0c0409`:
  forest scene CTest target now links the existing
  `aiverse_world` streaming library needed by async methods.

**Verification:** GitHub commits succeeded. No C++ compiler/CTest or
runtime race test observed. Note that another agent subsequently
added `setLandscapeSources`, authored terrain snapshots and fixes for
mask-only predicates; inspect its latest handoff/history rather than
reimplementing those changes.

**Known caveats:** asynchronous `HeightSource` and
`PlacementFilter` must capture immutable snapshots whose lifetime
outlives pending tasks; mutable ECS access would race. The bounded
worker queue is not proof of a frame-time budget. The typed exclusion
geometry is presentation-only, not an authoritative collision system.
Formal race, stale revision, fallback/mobile and lockstep validation
remain needed.

## Stage 7 — TerraWeave Horizon, aggregated forest/terrain HLOD (COMMITTED; UNVERIFIED)

**Goal requested:** forest and mountain/landscape visibility across
several kilometres without rendering a billboard/model per tree.

**Commits/files:**
- `c5cb0845897ebb16abe783e136a23f6f08323656`:
  `engine/world/TerraWeaveHorizon.h` creates coarse forest
  **cluster** silhouettes directly from deterministic climate and
  height samples, without individual trees, plus low-poly terrain
  sample grid and packed single RGBA8 atlas.
- `907b4a50febc3e18de16bb0f425f890427aeda96`:
  int64 clamp portability fix.
- `ff0fb85a8444921785fab8e01af3418a217f2ed2`:
  `HorizonBudget` and `estimateHorizonBudget` upper bounds for
  atlas bytes/triangles/vertices/cluster slots.
- `6bee547a2d7d5c882431d5a7d65e907d2a56746f`:
  `engine/render/TerraWeaveHorizonModel.h` combines coarse
  terrain + crossed alpha-masked forest cards into one
  `render::Model` with two material primitives and one atlas.
- `eb8e929e6255886302dd3263d29d3d423903e294`:
  terrain proxy slope normals from signed height gradients.
- `132b7e105c41ee041ce457c3d265c357da855e76` and
  `57aca4701df888cea0ff417af4be2892f7d52316`:
  `engine/render/TerraWeaveHorizonScene.h`, async region CPU
  preparation via existing `AsyncChunkStream`, stale region
  cancellation, render-thread GPU upload/rollback, keeping
  previous GPU model visible until successful replacement.
- `2bec856136910e37ffd3924c53a8bfb658d63055`:
  `engine/world/TerraWeaveHorizonManifest.h`, opt-in
  `terraweaveHorizon` config (can inherit seeds from near forest).
- `a675f775e27bddeb38f5e0753dd8a655761442cd`,
  `a85e3d93f7089495390d839e06c138acb71ef76b`:
  `engine/world/WorldScene.h/.cpp` owns Horizon with
  update/draw/shutdown, without changing scenes lacking opt-in.
- `2d2cf991143b270386fb9316cf7bda673db877e5` and
  `5f81dacdbfb4677c91a10a0302abcea7ed03261d`:
  `tests/terraweave_horizon_test.cpp` deterministic atlas,
  mesh topology/indices, resident budget limits and constructor.
- `6e2b270e96d7bbeeacbadbffbb1431fc66c4c070`:
  CMake test registration (links `aiverse_world`, models, zstd).
- `154d6025bbb0e22adb7e9f8aeaa70912d885c360`:
  isolated GCC/Clang workflow includes Horizon target.
- `ec476fb4f6ff6d510de28923c6c8168973c39ccf`:
  `docs/terrain/TERRAWEAVE.md` HLOD architecture, config,
  budgets and known visual limitations.
- `3e61539f58657c3b2d55db9fe6161dabd4bd193c`:
  `skills/world/aiverse-terraweave/SKILL.md` agent usage.

**Architecture:** nearby trees remain the existing ForestScene
LOD/geometry. At ~250m..1km the canopy is represented by 128m
cluster silhouette ribbons, at ~1km..6km by 512m clusters, using
one packed RGBA atlas and two crossed cards per cluster. One
coarse **48×48-quad** terrain grid is clipped around near ground.
Both terrain and forest share one Model with at most two material
primitives. Generic RHI upload/draw, not a new shader backend.
No per-tree ECS data or separate far-tree model for this layer.

**Theoretical worst-case default** (from `estimateHorizonBudget`):
832 possible cluster slots; atlas 1392×1392 RGBA8 =
7,750,656 bytes (~7.39 MiB), max 9,057 vertices and 7,936
triangles. These are **not actual GPU measurements**. Runtime
cost also includes mipmaps, texture upload, alpha overdraw, CPU
bakes and renderer passes.

**Verification actually performed:** GitHub file writes and
source/API inspection only. Container `git ls-remote` for
github.com fails DNS resolution. No CMake/CTest success, GUI
screenshots or frame-time/draw-call benchmarks have been observed.
Keep PR Draft. Do not claim the Horizon renderer visually passed.

**Critical remaining work:**
1. Run GCC/Clang and real macOS editor/runtime builds, CTest
   `terraweave_horizon` and pre-existing TerraWeave/world-async tests.
   Resolve any compiler, ABI, race, upload and depth-pass issues.
2. Test `terraweaveHorizon` in a real WorldScene with close/mid/far
   camera views and visual captures (Metal + Vulkan/GL), inspect
   silhouette atlas orientation, clipping, terrain boundaries and
   alpha-cutout material consistency.
3. Tune per-tier crossfade or stable dither near ~250m and ~1km;
   avoid HLOD popping or simultaneous duplicate individual/cluster
   trees. Compare CPU/GPU timings and memory on mobile/WebGL.
4. For static worlds, prebake/persist horizon atlases as assets
   (with fingerprint/version/hash), avoiding repeated CPU work on
   camera translation; implement horizon origin-shifting verification.
5. Distinct authored terrain vs procedural terrain may disagree:
   `distantTerrain` must be disabled or explicitly coordinated
   with native authored terrain ownership and biome masks.
6. Preserve stage-by-stage handoff and do not merge before tests.

### Horizon stage follow-up fixes (same day, still unverified)
- `b89901e8714ac651860993c1b98586376ad9f572`:
  `TerraWeaveHorizonScene.h` now retires an old GPU proxy if the next
  region has no visible clusters and `distantTerrain=false`
  (ocean/desert), preventing stale silhouettes.
- `d4bc70b51ad98c757cc232a1bae6bfa270df95cc`:
  `TerraWeaveHorizon.h` uses half-open mid/far distance bands,
  preventing duplicate card ownership on an exact LOD boundary.
No new build/CTest/GPU verification was performed for these fixes.

## Stage 8 — Horizon silhouette quality and staggered coverage (COMMITTED; UNVERIFIED)

**Date:** 2026-11-18. Branch: `feature/terraweave`, Draft PR #24.
Refresh the branch HEAD/individual file SHA before further edits;
other agents have been committing into this same feature branch.

**Commits/files:**
- `fabcac7aaa63ca6b0cfafc1f2f4ee423324562bf`:
  `engine/world/TerraWeaveHorizon.h` replaces uniform vertical
  foliage bands with deterministic overlapping rounded/tapered
  canopy silhouettes, colour/shading variation, alpha-cutout feather,
  sparse trunks and `HorizonSettings::crownDetail` (1–3).
- `7e64976e6294d069317a70de0fa7e3cbae76b884`:
  `engine/world/TerraWeaveHorizon.h` adds `nearBlendCm`,
  `tierBlendCm`, Q16 smoothstep coverage and world-hashed
  pixel masking to stagger near/mid/far cluster visibility.
- `1c0a928f850827a740181893c28bb7310a390e3b`:
  `engine/world/TerraWeaveHorizonManifest.h` exposes quality
  and blending parameters; when both Horizon and near Forest
  blocks exist, closeForestEndCm defaults to near Forest
  `cullRangeCm` rather than leaving an uncovered 240–250m band.
- `44875e003c118a268f8098656bd9532f98c4f654`:
  `tests/terraweave_horizon_test.cpp` covers deterministic
  canopy colours, near band visibility, smoothstep, invalid settings.
- `4cc15e342575ea77986b5e0ae74ea3772d3d7c04`:
  `engine/world/TerraWeaveHorizon.h` adds
  `HorizonSettings::orientationViews` (1 cheap / 2 quality)
  and `HorizonCluster` second atlas-frame coordinates.
- `fe3b644a595afebeb503cb99f931098ee5de74e3`:
  CPU Horizon baker renders independent X/Z silhouettes per
  cluster into a **single packed atlas** at 2-view quality.
- `6610b3a233a3ddbcb2c50b1af22663714528e6cb`:
  `engine/render/TerraWeaveHorizonModel.h` selects distinct
  subrect UVs for each crossed card orientation.
- `860ee5aaa3c66a2267eb915f6be51de1c257e9b2`:
  `tests/terraweave_horizon_test.cpp` verifies 2-view atlas
  bounds, distinct frames, unchanged indices/triangles and
  invalid view count.
- `30013eb337377379824f23e9e1933b55ebae7c0b`:
  `TerraWeaveHorizonManifest.h` parses `orientationViews`.
- `6b907d5553dd0591130606a577bb917a9ab91963`:
  `engine/render/TerraWeaveHorizonScene.h` captures
  `HorizonRuntimeStats`: actual live CPU preparation and GPU
  upload ms, atlas bytes, vertices, triangles, clusters,
  pending/discarded async jobs. These counters are *not*
  measured GPU kernel timings.
- `c28ebe28a2d9a77707afaac1932a9b970b059277`:
  horizon test verifies zero-initialized runtime counters
  before a GPU model is submitted.
- `df8d06b28e0f3edebd66fbd4f5ac65f838b6f7b4`:
  `docs/terrain/TERRAWEAVE.md` documents all settings,
  limitations, theoretical memory and runtime counters.

**Architecture**: Entire far forest remains one Model with two
primitive/material ranges (coarse terrain + masked cluster cards),
regardless of `orientationViews`. No individual foliage
ECS entities, GPU shader fork or per-cluster submissions.
Deterministic alpha-to-coverage is **baked per snapped
region**, avoiding a synchronized hard 250m/1km switch. It
does not continuously crossfade on every camera frame.
A 2-view atlas uses two orthogonal captured silhouettes and
one shared texture; default 1-view stays ~7.39 MiB vs
2-view ~14.77 MiB before mipmaps, triangle budget unchanged.
Higher crownDetail increases only CPU raster work.

**What is tested vs NOT tested:** Changes were inspected at
source level and successfully committed using GitHub connector.
Tests were extended, but **no successful compilation, CTest
execution, editor launch, Metal/GL/Vulkan screenshot or real
frame-time measurement has been observed**. Do not mark
this PR ready for merge yet. Previous GitHub DNS/clone
limitations prevented authoring-container checkout.

**Critical acceptance for next agent**:
1. Build `aiverse_terraweave_horizon_test` plus
   `aiverse_world` and `aiverse_editor` using real Clang
   and GCC. Run `ctest -R 'terraweave|world_async'`;
   record failures and compiler/linker diagnostics.
2. Load opt-in `terraweaveHorizon` with
   `orientationViews=1` and `2` on macOS Metal
   and one GL/Vulkan device. Orbit camera and compare
   near/mid/far forest; inspect alpha-mask edges,
   texture page UV bleeding, double draw and pop.
3. Exercise boundary conditions at 240m, 250m,
   920m, 1000m, 1080m and camera-region snap
   boundaries; compare one rendered atlas across
   equivalent seeds/platforms and log memory/draw
   metrics from `runtimeStats()`.
4. Assess whether spatial stochastic fading is
   sufficiently stable. If not, implement true
   GPU-temporal dither/crossfade through shared
   renderer shading (GLSL+MSL) with an appropriate
   transient memory/overdraw budget; do not claim
   true frame-by-frame fading at present.
5. For static worlds, cache prebaked atlas models
   with generator-version fingerprints; keep
   distantTerrain off when a separate authored
   landscape renderer covers the horizon.
6. After each actual work phase, append new
   commits/tests/limits to this handoff.

## Stage 9a — authored Horizon terrain/biome/forest masks (CODE COMMITTED, TESTS DEFERRED)

**Commit:** `a96408462d8e9e753ff114e1499ecbb32c16f1ef`.
**Branch:** `feature/terraweave` / PR #24, draft.
**Changed source:** `engine/world/TerraWeaveHorizon.h`.

**Public API:** `HorizonSurfaceSources` provides optional immutable
`heightAtCm`, `biomeAt`, and `allowsCanopyAt` callbacks to
`bakeHorizon(..., const HorizonSurfaceSources* = nullptr)`.
Pre-existing procedural sampling is the fallback for absent
callbacks. Crown samples are masked before atlas rasterization.
No shader/renderer/backend code changed; old callers still compile
conceptually without changing source.

**Actually performed:** GitHub source inspection and successful
file update. **No build, CTest, GPU screenshot or performance
measurement performed**, per user instruction: another agent owns
testing. DO NOT claim this feature passed.

**Next scoped code task:** expose a shared snapshot setter on
`TerraWeaveHorizonScene`, invalidate only its currently pending
region, and connect it to resident authored-world height/masks in
`WorldScene`, without racing the existing worker or touching ECS.
Follow with stable region caching and transition ownership.
**Tester handoff:** add coverage for supplied sample callbacks,
water/road suppression, same terrain snapshot in near/far paths,
GPU atlas preview and mobile fallback.

## Stage 9b — Horizon worker source ownership and authored WorldScene integration (CODE COMMITTED; NOT TESTED)

**Branch:** `feature/terraweave`, draft PR #24.
**Commits/files:**
- `37d8b85a5c357fdaa5216823a8bfc10595009085`:
  `engine/render/TerraWeaveHorizonScene.h` introduces
  `setSurfaceSources(shared_ptr<const HorizonSurfaceSources>)`,
  atomically captured by an async worker. Cancels old region
  selection on source changes but keeps the last uploaded model
  until successful GPU replacement. Adds read-only
  `terrainSettings()/biomeSettings()` for owning host adapters.
- `b124382a81888673a9d14ce4db68c5a7916d805d`:
  `engine/world/WorldScene.cpp`: authored collision terrain
  height snapshot and obstacle mask are now assembled for either
  opt-in near forest or opt-in Horizon, not only near forest.
  `Horizon` captures the resident immutable height tiles and
  collision-box mask; unloaded far terrain falls back to its own
  procedural height seed.
- Stage notes appended to `docs/terrain/TERRAWEAVE.md`
  and this agent handoff.

**Behavior/API:** The user can create a far landscape using
their actual authored collision heightfield within loaded regions
and suppress distant tree canopy where buildings/obstacles
have authored boxes. No new GPU shaders or per-tree entities.
Callbacks are resolved only in background CPU preparation and
own read-only snapshots via shared pointers. Source changes
cancel stale region jobs; main-thread GPU resource handling
remains unchanged.

**Actual verification:** GitHub connector writes and source review
only. **No tests/build/CTest/CI/screenshot executed**, per explicit
user decision: another agent is responsible for all validation.

**Next scoped functionality:** reduce recomputation of nearly
identical Horizon atlases when the camera crosses a small
streaming cell: add bounded atlas/model caching and stable
region-key hysteresis; consider static asset bake/cache
fingerprints. Additional incomplete behavior: far terrain only
matches *resident* authored height tiles, not unloaded chunks;
terrain ownership overlap and true temporal crossfade still
require design and renderer validation.

**Tester handoff:** validate terrain matching, road/building
mask discontinuities, snapshot change during pending CPU job,
old GPU proxy retention and destruction order, optional
Horizon-only scenes, and no-effects when both modules absent.

## Stage 9c — immutable Horizon atlas LRU and camera hysteresis (CODE COMMITTED, TESTING DELEGATED)

**Branch:** `feature/terraweave`, PR #24 Draft, base
`feature/universal-entity-inspector-fps`.

**Commits/files:**
- `2de0338b2d0ee2e722e63db9f1d99929973d48d4`:
  `engine/world/TerraWeaveHorizon.h` adds
  `maxCachedRegions` (0–4, default 2) and validation.
- `71ed672d84c4c8fbd5a2666501f410fa7cb67852`:
  `engine/render/TerraWeaveHorizonScene.h` introduces
  thread-safe bounded LRU of `shared_ptr<const HorizonBake>`
  snapshots keyed by `world::Cell`, with coherent source
  generation tokens, safe cache invalidation and ownership
  of old surface generations through in-flight workers.
  `HorizonRuntimeStats` adds `cachedRegions/cacheHits`.
  Worker still prepares a new render::Model for each CPU-cache
  hit and re-uploads on the RHI thread; no GPU cache is claimed.
- `2542ff4c43789c370279925dfb33f3642117926d`:
  `engine/world/TerraWeaveHorizonManifest.h` exposes
  `maxCachedRegions` setting (0 disables CPU retention).
- `91617b912864037a891073c6ab2ad43a29abdf66`:
  `engine/world/TerraWeaveHorizon.h` adds
  `regionHysteresisCm` (default 3200, less than ½
  `middleTileCm`) and validation.
- `3d05d2be12e166171456e42dc1b04c4adbb72cdc`:
  `engine/render/TerraWeaveHorizonScene.h` holds the
  previously selected region within an expanded cell before
  requesting a new CPU bake.
- `1be779f0041616a3f73aded5f9985a71c533c819`:
  manifest exposes `regionHysteresisCm`.
- Additional stage explanations in `docs/terrain/TERRAWEAVE.md`.

**Implemented vs pending:** CPU re-bakes are skipped for the most
recent region(s) unless authored surface revision changes. A
bounded hysteresis prevents rapid streaming at cell boundaries.
The renderer still owns one GPU model only; there is no disk
atlas cache and no per-frame animated transparency. The
cache does not eliminate CPU-to-GPU upload cost.

**Verification:** GitHub writes and source inspection performed.
The user explicitly assigned ALL tests to a different agent:
**NO builds, CTests, benchmarks, live renders or screenshots
performed or claimed.** Remain Draft.

**Next implementation-only scope:** implement an engine-generic
Horizon quality preset/API for mobile/desktop and a future optional
static-world pre-baked atlas asset adapter; do not create or execute
test targets in this agent's work. The other agent should validate
cache misses/hits, source revision races, camera jitter near
128m cell edges, memory during one/two atlas allocations,
hysteresis after large teleports and GPU rollback on failed upload.

## Stage 9d — native Horizon quality tiers (SOURCE COMMITTED; TESTS ASSIGNED TO OTHER AGENT)

**Commits/files:**
- `3b7a0fe487c6c0e6fabc928d614211f2e4c57565`:
  `engine/world/TerraWeaveHorizon.h` adds
  `HorizonQualityPreset` enum and
  `horizonSettingsForQuality` for minimal/mobile/balanced/high.
  Constructor validation remains centralized in
  `validateHorizonSettings`; presets do not modify ECS or RHI.
- `d76d35bea787168f983f32e6699c3be3420ded76`:
  `engine/world/TerraWeaveHorizonManifest.h` parses
  `terraweaveHorizon.quality` and applies per-field overrides.
- API matrix documented in `docs/terrain/TERRAWEAVE.md`.

**Behavior:** Minimal/mobile use a 16/24px atlas tile, 16/24
terrain grid, one-view projection and no cached CPU regions.
Balanced retains the previous defaults; High selects two-view,
denser crowns and a 64-resolution distant terrain grid, with
the same number of forest cluster cards. Genre independent,
opt-in manifest only; unconfigured worlds remain unchanged.

**Verification performed:** GitHub write operations and manual
source inspection only. **No CMake, CTest, CI inspection, GPU
renders, screenshot comparisons or benchmark executed** because
the user assigned tests to a different agent. Other agent should
validate every preset/field combination on desktop and mobile.

**Next functional work:** optional pre-baked persistent asset
cache for static-world Horizon regions to avoid CPU rasterization
across restarts, with version/fingerprint and strict size bounds;
full per-frame crossfade would need shared Renderer3D shader
semantics and actual visual validation.

## Stage 9e — static Horizon archive format and game-pack read path (CODE COMMITTED; TESTING DELEGATED)

**Branch:** `feature/terraweave`, draft PR #24.

**Commits/files:**
- `05d3c91caf799ebc7e2ab74505bb26743b8f96a5`:
  new `engine/world/TerraWeaveHorizonArchive.h`,
  pure little-endian versioned TWH1 archive encoder/decoder,
  bounded data size, FNV integrity and deterministic settings
  fingerprint with optional authored content hash.
- `936a55fea569ff6e38d2bcc4420523c3163e3ede`:
  atlas-frame bounds hardening in archive parser.
- `b1bf905092dcc15b4ae3e4689bd815151d7479bc`:
  `engine/render/TerraWeaveHorizonScene.h` adds
  optional `HorizonArchiveStore` callbacks, source fingerprint
  admission, cache-first → archive-read → CPU-bake flow,
  best-effort fallback on corrupted/missing assets and
  `HorizonRuntimeStats::archiveHits`.
- `73d338f50913d4b03ea1a3c1785732462d1fa067`:
  `engine/world/TerraWeaveHorizonManifest.h` accepts opt-in
  `archivePrefix` and optional `archiveContentHash`,
  builds a read-only `AssetReader` callback for files
  `<archivePrefix>/<cellX>_<cellZ>.twh`.
- `e338715df9be50641905f458986a0ffb568e9758`:
  `engine/world/WorldScene.cpp` passes its content reader
  to the Horizon manifest adapter.
- API/manifest/limitations detailed in
  `docs/terrain/TERRAWEAVE.md` § Prebaked Horizon archive assets.

**Functional behavior:** Native project build pipelines can
encode stable region atlases at asset-build time and ship them
for runtime reuse. The old codepath is entirely unchanged
unless archivePrefix or setArchiveStore is opted in.
Archive reads are performed on the existing background worker,
using a trusted game-supplied reader rather than direct filesystem
operations. A valid cache skips expensive biome/crown sampling;
the ordinary render::Model conversion/upload still occurs.
Any invalid archive simply falls back to baking; missing data
must never crash a scene. Authored external sources require a
nonzero stable authored-content fingerprint for disk cache use.

**Actual verification:** GitHub source writes and static code
inspection. **No compilation, CTest, CI run, GPU capture,
native app invocation, archive round-trip or performance run**:
the user explicitly assigned tests to another agent. The
implementation is unverified. Do not merge the Draft PR.

**Tester agent exact work:**
1. Configure and compile `aiverse_world`,
   `aiverse_terraweave_horizon_test` and the editor for
   AppleClang/GCC/Clang; expand tests for round-trip, magic,
   wrong content hash, FNV checksum, out-of-bounds fields,
   truncated arrays and two-view atlases.
2. Confirm archivePrefix path safety and packed asset
   load, missing/invalid asset regeneration, async worker
   shutdown when read callback is slow, no stale archive
   after source generation changes, and GPU upload rollback.
3. Compare actual tree and terrain geometry of
   `encode→decode` vs regenerated Horizon, including
   negative region coordinates, 16 MiB atlas budget and
   mobile preset.
4. Record test logs and actual GPU/CPU benchmark
   metrics and update this handoff; testing owner may fix
   implementation defects found.

**Next function-focused agent:** engine-native offline asset
bake command/pack integration and optional per-region patch
invalidations instead of invalidating a whole 12km atlas when
only one authored terrain tile changes. Preserve
genre independence and the per-stage handoff rule.

## Stage 10a — bounded offline Horizon batch API (COMMITTED; NOT TESTED)

- Commit `67c569b2fe9bda594c10e01df5853e60e924d81a` adds
  `engine/world/TerraWeaveHorizonBatch.h` (C++20 header-only,
  no RHI/genre/filesystem dependency).
- `HorizonBatchBounds` selects half-open signed Horizon camera-grid
  region keys (exactly the keys used by runtime `HorizonScene`).
  `bakeHorizonBatch` iterates deterministically z-major then x-major,
  limits work to `maxRegions` (default 4096), refuses external authored
  snapshots without a nonzero stable authored content fingerprint,
  skips matching valid `TWH1` archives through optional read callback,
  regenerates stale/corrupt archives, forwards bytes through a required
  asset-writer callback, supports cooperative cancellation and reports
  visited/generated/skipped count and output bytes. It does not create
  directories or write files from engine code; native build tools own that.
- Region centre uses key*middleTileCm + middleTileCm/2 to match
  `TerraWeaveHorizonScene` and `<cellX>_<cellZ>.twh` filenames.
- **Verification:** source committed via GitHub; build, CTest,
  runtime archive regeneration and filesystem operation NOT RUN, per
  user's explicit functions-first/testing-by-other-agent split.
- **Next functional step:** expose the batch API via a host-only CLI
  `aiverse_horizon_bake` using the same manifest parser, without
  changing gameplay, and register it as an opt-in build tool. Add
  docs for exact invocation. A separate test agent validates
  region-key edge cases, stale hashes, partial outputs and cancellation.

## Stage 10b — host-only bulk Horizon archive CLI (COMMITTED; NOT TESTED)

- `7a5ad0698dd1da1b81d0634f4fa933a2e4593119`:
  `engine/render/TerraWeaveHorizonScene.h` exposes read-only
  `settings()`, allowing the host tool to reuse the same manifest
  configuration used by runtime.
- `4e17b7139dcd5415163b3647831d07e6b1d3c75b`:
  `tools/horizon/main.cpp` CLI `aiverse_horizon_bake`:
  `--manifest <world.json> --out <dir> --cells minX minZ maxX maxZ`
  with `--force`, `--max-regions N`, `--quiet`.
  It reads the same Horizon manifest, resolves procedural seed
  inheritance from an optional near forest block without generating
  any near tree models, creates the output directory, uses stable
  <cellX>_<cellZ>.twh asset filenames, skips valid cached archives,
  and stages writes to a `.partial` file before rename.
  No runtime filesystem writes, GPU device or ECS changes.
- `a1e59600763933b02c9a10133d9ddba3b5466a0e`:
  `CMakeLists.txt` registers `aiverse_horizon_bake`
  for `AIVERSE_BUILD_TOOLS=ON`, non-Emscripten, linked against
  the existing model/world libraries; it is an ordinary optional
  build target, **not a new test**.

**Example (once built by the tester/other agent):**
```sh
./build/<config>/aiverse_horizon_bake \
  --manifest content/world.json \
  --out content/world/horizon-cache \
  --cells -4 -4 4 4
```
Pack the resulting `*.twh` files as normal game assets, and set
`terraweaveHorizon.archivePrefix` to `world/horizon-cache`.

**Scope limits:** CLI bakes *procedural* terrain/biomes only and
does not have access to independently authored collision tiles,
painted biomes or water/road maps. Native projects can use
`bakeHorizonBatch` with immutable `HorizonSurfaceSources`
and a nonzero `authoredContentHash` for that path.
Windows replacing a pre-existing `.twh` may require an explicit
file replacement policy (rename-over-existing has platform
differences); current implementation reports an error rather
than deleting the old asset.

**Actual verification:** GitHub commits and source review only.
No builds, tests, CLI execution or filesystem writes were
performed. The user delegated all testing to another agent.
Next functional stage is per-region selective invalidation
for authored modifications, avoiding full CPU atlas re-bake.

## Stage 10c — stable Horizon atlas tile ownership (CODE COMMITTED; NO TESTS)

- `9b25ec5ad8edf7c3ac4d3b6f656ff92a2a42e4c6` updates
  `engine/world/TerraWeaveHorizon.h`. Both mid/far cluster
  atlas subrectangles now use stable dense lattice slot keys:
  `(layerBase + iz*count + ix)*orientationViews`. Cluster
  presence/biome/road exclusion no longer shifts UVs for unrelated
  tiles. This is a prerequisite for partial atlas regeneration.
- `517d2cf3112f775a75c17f2e466eb6d5ef83a201` updates
  `engine/world/TerraWeaveHorizonArchive.h`: incompatible
  packed-atlas assignment advances archive wire magic/version to
  `TWH2` / version 2. Old TWH1 is deliberately rejected by the
  decoder and regenerated by the batch CLI/runtime fallback.
  File extension remains `.twh`; generator fingerprint changes.
- **Actual verification:** GitHub source committed only. NO
  compilation/test/benchmark/GPU run (explicitly delegated).
  Another agent must update tests expecting TWH1 and verify
  fixed tile positions across biome/road changes and negative keys.
- **Next:** implement `bakeHorizonPartial` using stable slots to
  rebuild only dirty atlas tiles and terrain vertices, then
  connect it to `HorizonScene::setSurfaceSources` and WorldScene
  changed tile bounds. Preserve prior GPU model until updated.


## Stage 10d — partial Horizon CPU bake and authored tile invalidation (CODE COMMITTED; NOT TESTED)

**Branch:** `feature/terraweave`, Draft PR #24.
**Commits/files:**
- `e287a8324d9c815a7ce8a88de819e6961103a203`:
  `engine/world/TerraWeaveHorizon.h`, new optional
  `bakeHorizon(..., source, previous, edited)` parameters.
  Compatible old bakes reuse the RGBA atlas and unchanged
  cluster records by stable `(x,z,tileSize)` identity,
  clearing only edited atlas slots and rewriting affected
  terrain vertices; if the old bake doesn't match the camera
  region, do a full deterministic bake. Dirty bounds are
  checked and nearby cluster samples are conservatively
  considered affected.
- `3b0d895b2d84e5ad9f925b4dadc6e23f48f76ee6`:
  `engine/render/TerraWeaveHorizonScene.h` captures a
  `shared_ptr<const HorizonBake>` baseline, region edits
  and immutable source revisions; background jobs use
  the partial CPU bake when eligible. Stale authored
  static archives are bypassed during localized updates.
  Current GPU model remains until the replacement uploads.
  `maxCachedRegions=0` avoids retaining the prior CPU bake.
  If a changed area is outside current horizon footprint,
  the model stays resident rather than immediately rebaking.
- `4ad8136bed69cf83013753747f5ec5e386ffa61b`:
  `engine/world/WorldScene.cpp` records changed authored
  heightfield/obstacle cells from worker publish/unload,
  converts them into bounded world-centimetre half-open
  rectangles and publishes those to Horizon alongside the
  immutable authored landscape/mask snapshot.
- Stage description and API contract added to
  `docs/terrain/TERRAWEAVE.md`.

**Changed behavior:** No per-tree ECS entities or new GPU
renderer. Near forest still uses revision-safe resident
chunks, while Horizon can now avoid full CPU canopy/height
re-evaluation for small landscape edits. A full combined
`render::Model`/texture upload is still required; no
partial GPU update is claimed. TWH2 stable atlas slots are
a prerequisite, so generator archive version was bumped in
Stage 10c.

**Testing:** NOT RUN by explicit user instruction.
**Separate testing agent's acceptance:** complete Clang/GCC
compiles, CTest and negative/positive region keys; compare
full bake vs partial bake byte-for-byte for same source;
verify edited road/water polygons, repeated dirty rectangles,
terrain vertex boundary normals and no-change updates;
teleport/cancel/resume jobs, archive source-hash invalidation,
GPU rollover and FPS/RTS replay neutrality; retain actual
logs/measurements/screenshots.

**Next function implementation:** avoid re-uploading the
entire atlas for small edits using existing RHI texture
subregion API (if supported across Metal/GL/Vulkan), or
introduce bounded tile paging with compatibility on
WebGL/mobile; do NOT fork graphics backend. Another
potential improvement is incremental tree rendering in
the existing editor and static-world asset bake integration.

## Stage 10 completion checkpoint — functions only, no verification

**Branch:** `feature/terraweave`, Draft PR #24; last verified remote
feature source head before this checkpoint: `00cda4672a3310c8dd289cf52438f4a92bbcf52f`.
The PR remains separate from the base branch; no merge into main.

**Features shipped in stage 10:** bulk offline native Horizon atlas
encoder and host executable, stable fixed atlas slots and TWH2
format revision, asynchronous region-source ownership, optional
partial CPU atlas/terrain rebuild for world edits, WorldScene
publication of changed authored tile bounds. The head includes
all docs and `skills/world/aiverse-terraweave/SKILL.md`
changes (latest skill commit `00cda4672a3310c8dd289cf52438f4a92bbcf52f`).

**Tests actually executed:** NONE. Tests and screenshots were
explicitly delegated to another AI agent; even a successful
source push is not evidence that the implementation compiles.
No CMake, ctest, CI, GPU render, asset roundtrip or perf
measurement was performed in this feature-implementation turn.

**Remaining limitations / next functional owner:** full GPU
model/atlas upload after partial CPU edits; preview/authoring
support for painted biomes and region-level road/water provider
beyond the existing resident collision-box masks; static-world
authored surface offline batch adapter; optional RHI texture
subregion upload or page-oriented HLOD resource model after
backend API parity review. A separate **test agent** must compile
and execute the full Stage 10 matrix listed above and record
actual failures/results without silently marking NOT RUN as PASS.

## Stage 11a — merge main into feature/terraweave before further work (COMPLETE)

**User request:** First bring latest `main` into the active feature branch,
without merging unfinished TerraWeave into `main`. Do not spend this
feature stage running tests (dedicated test agent owns validation).

- Branch before merge: `feature/terraweave` at
  `00cda4672a3310c8dd289cf52438f4a92bbcf52f`.
- Main at merge: `9ee6ff6fbf390bd45363bc92c838f492dfe92eee`.
  The branches were diverged, with 325 main-only and 258
  feature-only commits in the GitHub compare view.
- Integration PR **#30** (head `main`, base
  `feature/terraweave`) was merged using GitHub's merge method.
  Resulting merge commit:
  **`7f1cedde055a58e0b12a224a6e2f9b1dfd89b8d0`**.
- Independent verification from GitHub compare:
  `base=main`, `head=feature/terraweave` returned
  **status=ahead, behind_by=0**, merge base exactly
  `9ee6ff6fbf390bd45363bc92c838f492dfe92eee`.
  PR #24 remains Draft and continues to target
  `feature/universal-entity-inspector-fps`. Branch
  `main` is unchanged by this integration.
- Following the merge, inspected `tools/horizon/main.cpp`,
  `TerraWeaveHorizonBatch.h`, and preceding handoff. A
  parallel agent already implemented batch offline archive
  baking, TWH2 stable slots, and CPU partial region invalidation.
  **Do NOT duplicate those modules.** Next functionality is
  partial GPU resource update/paging where existing generic RHI
  supports it, or another independent high-value feature.

**Actually tested:** GitHub merge and ancestry compare only.
**CMake/CTest, app execution, GPU tests and performance not run**
(per explicit separate testing-agent ownership). No success
claim is justified. Do not change unrelated main features.

**Next agent:** fetch current HEAD before source writes because
other agents commit concurrently; preserve `TWH2` archive
semantics, keep `Horizon` renderer through standard
`gfx::Device`, update this handoff after the next functional
implementation stage.

## Stage 11b — GPU atlas subregion patches via existing gfx::Device (SOURCE COMMITTED; TESTS DEFERRED)

**Branch:** `feature/terraweave`, draft PR #24.
**Commit IDs and changed files:**
- `20fe5898a06ed0908f99a971bb313b8249e28a82`:
  new `engine/render/TerraWeaveHorizonGpuPatch.h`.
  Pure `horizonCanReuseModelGeometry(oldBake,newBake)`
  checks all far terrain vertices (including colour), clip center,
  atlas dimensions and all crossed-card geometry/UV metadata.
  `planHorizonAtlasUpload` finds changed fixed atlas tiles,
  coalesces adjacent runs, limits updates (default 6) and total
  updated pixel bytes (default 25% of atlas). Returns a nullopt
  for changes that require a full model swap.
  `applyHorizonAtlasUpload` validates every rectangle, checks
  resident RGBA8 image ownership and calls only existing
  `gfx::Device::updateTexture` on render owner thread.
- `6c34cbb513de0a62d63045bf2a912b226f3e7591`:
  `engine/render/TerraWeaveHorizonScene.h` integrates
  the opt-in patch fast path at publication. If the current
  resident GPU model and baked CPU baseline have identical
  geometry, only changed atlas subregions update; otherwise
  the preexisting full model upload/release path remains.
  New `HorizonRuntimeStats` fields:
  `atlasPatchCount`, `atlasPatchBytes`,
  `atlasPatchRegions` and `fullUploadCount`.

**Important architecture:** Existing native `gfx::Device`
already implements partial `updateTexture` and mipmap
regeneration on GL/Metal/Vulkan, confirmed by inspecting
their source. No new shader, backend specialization,
per-cluster draw submission, or gameplay ECS changes.
The old shared GPU geometry and one GPU atlas are reused
only if all visible geometry fields are unchanged.
Dirty road/biome edits that add/remove clusters or
change terrain height/vertex colour fall back to full upload.
CPU model preparation still executes on the worker; this
stage optimizes GPU upload volume and resident model lifetime.

**Actual verification:** GitHub merge ancestry/source reads
and successful source commits only. **NO CMake build, CTest,
native app run, GPU screenshot or benchmark performed**; user
assigned all testing to another agent. PR remains Draft.
The full-update fallback cannot be described as GPU-patched.

**Next test agent:** validate `planHorizonAtlasUpload` on
identical/changed/removed tiles, wrong geometry, density,
two-view layouts and zero changed bytes. Verify actual
`Device::updateTexture` row pitch/mipmap behaviour on
Metal/GL/Vulkan (especially 6 partial updates per frame),
GPU timing and correct appearance after landscape revisions.
Check exception/partial texture mutation behaviour and
fallback on changed terrain vertex/topology.

**Next feature stage:** optional GPU vertex partial update for
terrain height/colour changes, only if generic Model/GPU buffer
semantics allow safe use without introducing a new renderer.
This is NOT implemented; do not claim it is.

## Stage 12a — dedicated geometry ownership for mutable Horizon (COMMITTED; NOT TESTED)

**Branch:** `feature/terraweave`; PR #24 Draft. Latest main had previously been merged as `7f1cedde055a58e0b12a224a6e2f9b1dfd89b8d0`; refetch before any next edit because multiple agents work on this repository.

**Commits / source:**
- `a332bd8e7da8d4a4646236fdcbf1144be9f0c3c4` — `engine/render/Model.h`: adds `Model::batching.allowGeometryPool` (default `true`) as a generic native-model opt-out.
- `7baba8a08efa39455fe32b70e4369e8ae8d98f33` — `engine/render/GlobalGeometryPool.cpp`: `canPool` honors the opt-out before sharing a GPU arena page.
- `017b4652a8c6055a838a5762f4cac893bd29771c` — `engine/render/TerraWeaveHorizonModel.h`: sets `allowGeometryPool=false` on Horizon models that may need partial VB writes.

**Reason:** `uploadModel` may otherwise place any rigid, static Horizon vertex/index arrays in `GlobalGeometryPool` and set `gpuBaseVertex`/`gpuFirstIndex`, meaning a naive model-local `gfx::Device::updateBuffer` could overwrite OTHER models' geometry. Explicit opt-out retains existing per-model dedicated GPU VB/IB through the standard RHI. All non-Horizon models keep the default pooling policy; no graphics API or genre-specific engine core added.

**Actual verification:** GitHub commit/write success and static source inspection ONLY. User explicitly assigned all CMake, CTest, CI, sanitizer and GPU testing to another agent. No test results inferred.

**Next action:** implement a pure CPU vertex-patch planner which validates identical index topology, retained bounds, unchanged crossed-card vertices and a small modified terrain vertex fraction. Integrate a render-thread RHI `updateBuffer` path with a guaranteed full-upload fallback and document it as a distinct handoff stage.

## Stage 12b — topology-safe partial Horizon GPU terrain vertex updates (COMMITTED; NOT TESTED)

**Branch:** `feature/terraweave`, PR #24 Draft. The preceding Stage 12a added `Model::batching.allowGeometryPool=false` for mutable Horizon models; do NOT remove that safety invariant.

**Committed source:**
- `6035654a11afb0606eab1ff0498e5b45ac959e7e` — `engine/render/TerraWeaveHorizonGpuPatch.h`: splits `horizonCanReuseHorizonLayout(old,new)` from the original strict `horizonCanReuseModelGeometry` so an index/UV/cluster-compatible layout can accept changed terrain heights and vertex colours. Existing `planHorizonAtlasUpload` remains strict by default; a new final `allowTerrainVertexChanges=false` optional parameter allows combined CPU/GPU patch planning.
- `086dec4673aca360189df85a4ac1cfee9bc78086` — new `engine/render/TerraWeaveHorizonVertexPatch.h` providing `planHorizonVertexUpload`, `horizonCanApplyVertexUpload`, `applyHorizonVertexUpload`. Exact CPU comparisons use an independent packed-vertex/index snapshot. Planner rejects modified tree crossed-card vertices, changed indices, node/primitive topology, changed atlas slot geometry, pooled geometry, invalid bounds or updates above 20 disjoint ranges / 35% of terrain vertices. All current renderer bounds and per-primitive bounds must conservatively CONTAIN the new bounding boxes; otherwise fall back to full model swap rather than risk incorrect frustum culling.
- `1dc62908a47de28d1151f00cbb5fb00144c83876` — `engine/render/TerraWeaveHorizonScene.h` integrates combined atlas + terrain vertex GPU patch preflight and standard RHI `gfx::Device::updateBuffer` submissions on the owning render thread. After a successful combined patch, replaces the CPU vertex snapshot and `activeBake_` without creating/releasing another GPU model. On full upload, captures prepared CPU vertices/indices before `uploadModel` discards them; these snapshots are retained only when `maxCachedRegions>0`, preserving minimal/mobile low-memory choices. Empty Horizon and shutdown clear CPU snapshots. Adds runtime stats `vertexPatchCount`, `vertexPatchBytes`, `vertexPatchRanges`; atlas patches and full uploads still have their existing counters.

**Compatibility and invariants:**
- The original `Model`/RHI and genre-independent `WorldScene` APIs remain. Horizon models own dedicated VB/IB and cannot accidentally overwrite shared `GlobalGeometryPool` memory.
- Only the terrain vertex PREFIX changes. The distant forest crossed-card vertices, atlas/UV geometry, model index layout, primitive metadata and conservative bounds must remain compatible. An entire terrain grid is deliberately NOT uploaded if only a handful of vertices change and patch plan fits budget. New atlas pixels may be patched in the same publication.
- `gfx::Device::updateBuffer` is a void method in GL/Vulkan/Metal. It is not a transactional success-reporting API; device-loss/backend failures cannot be fully detected by this feature. The plan preflights all ranges, limits work, and falls back to a full upload BEFORE writes if the layout is unsafe, but does not guarantee rollback on GPU device loss. Real device validation is required.
- Even when the geometry buffer patch succeeds, some far-terrain edits may move silhouette/card heights or culling bounds; these deliberately take the full model path. The optimization is strict correctness-first, not a blanket dynamic mesh update.

**Actually performed:** Github connector source reads, verified RHI/global pool code inspection and successful source commits. **NO CMake, CTest, CI execution, GPU screenshots or performance measurements**; user has assigned all tests and defect triage to another agent. Do not mark any tests as passing. PR remains Draft.

**Tester agent exact next checks:** compile on GCC/Clang/AppleClang; verify `Model::Batching.allowGeometryPool` does not change existing model API/serialized data; validate dedicated Horizon VB/IB allocations even with global geometry pool on; test same-layout new height/normal/colour, atlas-only, combined atlas+terrain, changed triangle topology, changed cluster bounds, disabled CPU cache and negative coordinates; instrument GL/Metal/Vulkan `Device::updateBuffer` to verify size, offsets and frame synchronization. Record real results in this file. Check no renderer-cache or culling regressions when updating a static Model in place.

**Next functional work (not implemented):** efficient persistent atlas/mask paging across multiple regions and/or optional GPU-resident HLOD page cache, if justified by observed upload cost. Avoid unbounded GPU memory. Keep build/test responsibility with separate agent.

### Stage 12b follow-up — forest-only Horizon compatibility (COMMITTED; NOT TESTED)
- `3913e0cd31440fd3ca3261bc9c9e23cdd7dbc8f4`:
  `engine/render/TerraWeaveHorizonVertexPatch.h`: removed the
  premature rejection of a Horizon bake with **zero** far-terrain
  vertices. An opt-in `distantTerrain=false` scene must still be
  able to use the existing atlas-only GPU patch fast path; the
  planner now validates its unchanged crossed-card vertices and
  returns an empty terrain-vertex patch plan rather than forcing
  a needless full model upload.
- **Actual verification:** code commit and static review only.
  Separate testing agent must cover atlas-only scenes, mixed
  terrain/atlas patch, and no-change frames. No tests performed.

## Stage 13a — bounded Horizon GPU cache configuration (COMMITTED; NOT TESTED)

**Branch:** `feature/terraweave`, PR #24 Draft. Work is functions-first; the separate agent owns all builds/CTest/GPU benchmarking.

**Commits and files:**
- `1bc171e4b9790e553c01daa23bd2065315f7f1b2`: `engine/world/TerraWeaveHorizon.h` adds `maxCachedGpuRegions` (default 1, 0–3) and `gpuCacheBudgetMiB` (default 32, 8–96), including quality preset overrides. Minimal/Mobile disable additional GPU retention. High requests two cached regions but remains subject to a hard GPU-byte allowance.
- `9516e84ce5aa0c12de2066bd6c6746e8d1bca68b`: `engine/world/TerraWeaveHorizonManifest.h` accepts the same per-project JSON overrides.

**Budget contract:** the allowance applies only to **inactive cached GPU models** and not the single active GPU model or the already-bounded asynchronous upload/staging pipeline. Models are not copied; the cache will own their GPU resources by transferring the existing `std::unique_ptr<render::Model>`. These settings do not alter the deterministic bake fingerprint or physics/ECS behavior.

**Verification:** GitHub source commits only; tests NOT RUN by explicit division of work.

**Next:** implement the actual GPU-region LRU on the render owner thread, keep worker staging drained for cache hits, reject stale source revisions, release via `render::releaseModel(device,...)` on eviction/shutdown. Then append the next handoff stage.

## Stage 13b — bounded GPU-resident Horizon model LRU (CODE COMMITTED, TESTS NOT RUN)

**Branch:** `feature/terraweave` (Draft PR #24). Changes operate only when `terraweaveHorizon` is opted in; core C++ / render RHI remains genre independent. Other agents may have committed concurrently; fetch current GitHub HEAD/each file SHA before editing.

**Commit history / files:**
- `ca7a7ebe3b739f76831819725d71d5726af74037` — new `engine/render/TerraWeaveHorizonGpuCache.h`. Render-thread-only `HorizonGpuRegionCache` owns **already uploaded** `render::Model` instances with optional prior CPU bake/vertex/index snapshots. Cache keys are `world::Cell` and authored surface revision; `take` removes the desired region without new upload, `retain` stores previous GPU model with byte+count limits, LRU eviction and explicit `render::releaseModel(device,...)`, `prune` discards obsolete source revisions, and `clear` releases GPU assets before device shutdown. GPU estimate budgets texture level 0 + approximate mip chain + VB/IB and fixed overhead; not a driver-measured allocation figure.
- `580f26413ad4c1fd630fc4c4d4f8ff9e4d4ac7b3` — `engine/render/TerraWeaveHorizonScene.h`: adds owner-thread region handoff helpers `retireActive`, `adoptCached`, and `drainCanceledJobs`, `activeCell_` and `activeRevision_`, and `gpuCache_`. Prepared worker results record immutable source revision. `HorizonRuntimeStats` gains `gpuCachedRegions`, `gpuCachedBytes`, `gpuCacheHits`.
- `0e2cba94d839c604633b7879d4516ee6bc2d1947` — same scene: `setSurfaceSources` cancels pending stale async work regardless of whether authored edits touch the active displayed region. If edits are proven outside the active region, current model is allowed to continue unchanged at the new source revision. `update(device,camera)` enforces region key hysteresis, prunes stale GPU entries, instantly reuses an active or cached region, cancels previous stream request, and **drains** canceled job results so staging bytes never remain reserved indefinitely after a cache hit. Publication explicitly rejects prepared results whose source revision is no longer current.
- `eae64dea29e37f1ebee7b3975483e24c9728780c` — same scene: on successful publication of a new GPU model, retain the old model only if revision/memory/count budget permit; otherwise release on render thread. On successful in-place atlas/VB patches, update active key/revision. For empty regions, retire old active to the cache (if allowed), publish empty active residency. On `shutdown(device)`, release active and all inactive cached GPU models and CPU snapshots.

**Functional behavior:** On A→B transition, last successfully uploaded A remains visible while B is prepared/uploaded. Once B is published, A can enter the GPU LRU without copying GPU buffers. On B→A, A's stored model/atlas/vertex+index references are moved directly into active display; no CPU atlas bake or GPU upload is requested. Repeated cache activations keep the asynchronous staging queue draining on the owner thread, and a source/terrain revision prevents stale GPU reuse. Budget is **extra cached GPU residency only** and does not include the single active region or bounded asynchronous upload staging. Minimal/Mobile disable extra cached models. Default Balanced keeps up to 1 extra with 32MiB cap; High requests 2, still subject to 32MiB byte limit. Same generic `gfx::Device` and `render::Model` paths, no GL/Metal/Vulkan-specific code, no ECS mutations.

**Important limitations:** RHI resource sizes are estimated with 4/3 atlas mip overhead; drivers may allocate differently. No GPU crossfade is added: switching cached regions is instantaneous and may still have a visible change. GPU-resident models must be explicitly released through `WorldScene::shutdown(device)`; skipping the owning device's shutdown leaks them just as it leaks any other uploaded model. Cached-region GPU models may temporarily reference stale GPU assets until the next `update(device)` prune after `setSurfaceSources()`; shutdown also clears them. `updateBuffer` and `updateTexture` remain void backend operations; GPU device-loss recovery remains external.

**Verification actually performed:** inspected `AsyncChunkStream`, `render::Model`, `gfx::Device` source and committed code through GitHub connector. **NO CMake, CTest, sanitizer, real editor/world launch, GPU capture or performance benchmark run** — explicitly owned by separate testing agent. Keep Draft PR; no claim of correct runtime execution.

**Testing agent acceptance tasks:** build all relevant AIVERSE targets; exercise A→B→A GPU cache hit and ensure no extra `uploadModel`, verify cached GPU IDs preserved, A→B→C over-budget LRU eviction, and high-quality two-view cache byte accounting; test async late results after return to A, removed catalog cells and staging capacity (including WebGL no-thread mode); test source-revision invalidation while B is pending, edits inside vs outside visible region, no-cache mobile mode, model teardown/shutdown/device swap, memory and GPU framing on Metal/GL/Vulkan. Record pass/fail logs, not assumptions.

**Next implementation-only work:** Add optional GPU residency visualization/diagnostics in existing editor scene UI, if useful, and consider seamless camera-region proxy transitions or atlas paging only with real GPU measurements. No tests added/executed in this stage.


### Stage 13b follow-up — cache diagnostics and move-only ownership (SOURCE COMMITTED, TESTS NOT RUN)

- `bdd9edbe411d7c6ba2040177ce96efc729fa1c43`:
  `TerraWeaveHorizonGpuCache.h` returns move-only
  `HorizonGpuRegion` via explicit optional move and records
  GPU cache evictions caused by LRU budgets or stale source
  revisions. Cache hits transfer model ownership rather than
  incrementing ref counts on RHI handles.
- `daf20ef6059679b266c60a17d7975cbfd2bdbb3e`:
  `TerraWeaveHorizonScene.h` exposes
  `HorizonRuntimeStats::gpuCacheEvictions`.

**Actual validation:** GitHub source review and committed changes
only. **No CMake/CTest/platform GPU run**, by the user's
explicit separate-test-agent policy. Add eviction/revision and
GPU memory accounting tests to the acceptance list above.

## Stage 14a — neighboring Horizon GPU-region transition crossfade (CODE COMMITTED; TESTS NOT RUN)

**Scope / branch:** `feature/terraweave`, Draft PR #24; user delegated CMake/CTest, screenshots and benchmarks to a separate testing agent. Keep both the feature and its transitions generic and independent of RTS/FPS gameplay. Re-fetch current HEAD and per-file SHAs before editing because multiple agents work concurrently.

**Commits/files:**
- `c7151ab765e57e95df26eaac7886afc023ce07c8`:
  `engine/world/TerraWeaveHorizon.h`: adds
  `HorizonSettings::regionTransitionMs` (0–1000ms, default 240).
  Minimal and Mobile presets explicitly disable transitioning
  to avoid two models / alpha overdraw on constrained hardware.
  `validateHorizonSettings` bounds the new option.
- `27f718a76e539ae36d703b02d2bb4f76ff178806`:
  `engine/world/TerraWeaveHorizonManifest.h`: adds optional
  `terraweaveHorizon.regionTransitionMs` JSON key.
- `fb069ee868cb5b5902e88729813a203fd058195d`:
  `engine/render/TerraWeaveHorizonGpuCache.h`:
  `reserveTransient(device, estimatedGpuBytes,
  maxInactiveGpuRegions, gpuBudget)` retires old inactive LRU
  entries if necessary to accommodate ONE fade-out model inside
  the existing **extra GPU residency budget**. It does not
  allocate any new GPU buffers or alter the active model.
- `51befc1077fea0c051ca9b5a6fed716033e3918a`:
  `engine/render/TerraWeaveHorizonScene.h` stores at most
  one outgoing `HorizonGpuRegion`, with old source revision,
  cell and monotonic start time. Neighbor-only transitions
  require one-cell distance in X/Z, matching authored
  source generation, nonzero transition duration and the
  ability to reserve the extra GPU budget. A previous transition
  settles into the normal LRU (or releases stale GPU assets)
  before starting another. A return to the outgoing cell can
  settle/retrieve its cached GPU model without rebaking.
  `finishTransition(device)` runs on the render owner thread;
  shutdown forces resource release. Stats:
  `transitionStarts`, `transitioning`,
  `transitionProgressPermille`.
- `bde25305027deb0198cfd5daf2a1cf1f3c57ddfb`:
  `TerraWeaveHorizonScene::submit` now draws outgoing
  and incoming proxy with per-model tint alpha and the
  existing `kDrawTransparent|kDrawNoCastShadow|
  kDrawNoReceiveShadow` flags while the time-based
  smoothstep transition is active, then returns to the
  ordinary single opaque/static Horizon model. No new
  shader or backend implementation.

**Behavior:** A→B remains single model while B is baking;
when B is ready and GPU-resident, A fades out while B
fades in. Maximum overlap duration 1000ms, default 240ms.
The extra GPU fade model is accounted against the same
inactive residency count/byte budget as the regular GPU
LRU, rather than being a free third tier. Fast teleports
and source-revision changes intentionally skip the
transition to prevent misleading stale landscape visuals.
The transition is presentation-only: it neither changes
tree ECS IDs nor collision/navmesh/authoritative simulation.

**Important limitations:** This uses standard transparent
alpha blending and two model submissions for a short time.
On overlapping triangulated far terrain and alpha-masked
cluster cards, conventional sorting/depth policies can
still produce transparency artifacts. Avoid claiming true
order-independent transparency or perfect temporal
alpha-coverage. The outgoing model remains resident
until `finishTransition(device)` can retire it. GPU
driver cost/latency and visual quality are unmeasured.
`gfx::Device` still owns device-loss recovery.
Minimal/Mobile default transition off.

**Actually verified:** commits written via GitHub connector,
source contract inspected. **NOT RUN:** compiler, full
AIVERSE build, CTest, shader capture, renderer screenshots,
memory/FPS measurements, sanitizer or native runtime.
Dedicated testing agent owns these and must report real
results here before the Draft PR is merged.

**Tester acceptance:** move camera by one cell on
high/balanced and capture alpha overdraw; A→B→A
during transition; jump several region cells
(should hard-switch); disable transition; no GPU budget;
device shutdown halfway through; road/terrain source
revision while fading (old proxy must not render again);
vary frame pacing; validate resource counts/opacity on
Metal/Vulkan/GL and mobile default.

## Stage 15 — split Horizon terrain depth from forest-only crossfade (CODE COMMITTED; TESTS DEFERRED)

**Branch:** `feature/terraweave`; Draft PR #24. Follow current branch HEAD: other agents may edit adjacent modules. The user explicitly assigned build/CTest, GPU captures and performance testing to a different agent.

**Source changes (2026-11-18):**
- `af57039985d61ca27c355c96c5fecfe7d69a0c76`:
  `engine/render/Renderer3D.h` adds generic
  `ModelDraw::primitiveMaterialFilter` (`-1` means
  all source model primitives, non-negative value filters by
  original `Primitive::material` index).
- `84cf3bb8495e801869e5f3b1aea0656187a69fe8`:
  `engine/render/Renderer3DModels.cpp` honors the filter
  before preparing each `ModelItem`. Only filtered draw
  calls bypass existing aggregated shadow batches/material
  batches and the static cache; **unfiltered drawModel**
  continues through all pre-existing optimization paths.
  Filtered draws are not passed to whole-model light-bake
  capture as if all their mesh primitives had rendered.
- `a005ac31bc3eaad2c260c5c548e293e4b1068fef`:
  `engine/render/TerraWeaveHorizonScene.h`: the crossfade
  draws **incoming terrain material slot 0 exactly once,
  opaque**, while only source material slot 1 (forest cluster
  crossed cards) is alpha-blended from outgoing to incoming
  GPU model. The two models still share their own already
  uploaded GPU vertex/index/atlas resources (no copies or
  new shaders). Split transient draws use `persistentId=0`;
  the normal full single-opaque draw continues to use its
  stable scene ID.
- `775761b22564bc1663f6d857fe6512488e623393`:
  `HorizonScene` directly restores the old transition-slot
  GPU model on **A→B→A while the fade is ongoing**, before
  LRU insertion could evict it at `maxCachedGpuRegions=1`.
  Large-teleport stale-proxy visibility is bounded by the
  far-region footprint even when CPU bake caching is off.
  Pending worker requests are still cancelled/drained and
  RHI ownership remains single.
- `9cb2c048fe0a295cb5f0a0e18d1d342eb5348ea0`:
  `Renderer3D.h` moves the new material filter to the
  END of `ModelDraw`, so existing positional aggregate
  initializers in native projects don't have their field
  positions shifted.

**Presentation contract:** while blending the model,
`drawRegion(incoming, alpha=1, materialFilter=0)` is
opaque and depth-writing, then the departing/arriving
`materialFilter=1` canopy portions fade in the standard
transparent pass. This removes the previous **two overlapping
semi-transparent distant terrain grids** and significantly
reduces Z-fighting risk. In the normal steady state,
`materialFilter=-1` keeps one standard combined model call.
The source-material-index filter is a general Renderer3D
feature, NOT a TerraWeave-specific renderer fork.

**Limitations:** the incoming far-terrain ring replaces the
old terrain ring immediately; changing terrain outer
silhouettes may still show a sudden edge and require real
geomorph/skirt handling in the future. The two canopy
alpha-blended layers can still exhibit sorting/overdraw
artifacts and are not order-independent. A teleport outside
the old region's safe footprint now suppresses stale
Horizon submission until a new region is available; this
may produce a brief horizon gap by design rather than a
completely wrong outdated skyline. Normal opaque
`Renderer3D`/shadow and native game draw paths should not
change when `primitiveMaterialFilter=-1`.

**Actual verification:** GitHub source reads and successful
commits only. **NOT RUN**: CMake, CTest, CI workflow,
shader, GPU screenshots, platform performance, runtime
regressions. Separate testing agent must validate all of
these before PR #24 is made ready for review.

**Test-agent acceptance:** check single visible terrain
material during transition, alpha-mask forest-only
coverage and draw sorting at several camera angles;
non-Horizon material filtering (including variants,
shadow/material batch bypass, unfiltered MDI and static
light capture), native positional `ModelDraw` initializers,
A→B→A while fade is active, several-km teleport while a
worker is outstanding, cache-disabled Mobile/Minimal,
texture/vertex range patches after fade, LRU accounting,
WorldScene shutdown and GPU resource lifetime. Compare
Metal/GL/Vulkan footage and frametime/draw counts.

**Next implementation-only stage:** improve the far-terrain
outer-edge behavior via a skirt/terrain LOD region boundary
policy or use screen-space temporal forest coverage if real
visual testing proves existing transparent depth sorting
insufficient. Do not duplicate the generic renderer.

## Stage 16 — globally aligned far-terrain lattice, consistent boundary normals, optional perimeter skirt (CODE COMMITTED; NOT TESTED)

**Branch:** `feature/terraweave`, Draft PR #24. **User requirement:** implement functions only; all build/CTest/platform rendering tests are owned by a separate agent. The source uses the shared C++ TerraWeave CPU baker and `Renderer3D` RHI, not genre-specific FPS/RTS terrain code.

**Commits and source changes:**
- `2b1e644769d95e8c73a80f40f69158b014bc6a96` — `engine/world/TerraWeaveHorizon.h`: terrain vertices now use a globally anchored signed **rational world-space lattice**: `gridIndex=floorDiv(cameraCm * terrainResolution, regionSideCm)-resolution/2`, `worldCm=floorDiv(gridIndex*regionSideCm, terrainResolution)`. This works for negative coordinates and arbitrary supported resolutions, including High 64×64 where the 512m forest key does NOT divide 192m terrain-grid spacing. The same world coordinate yields the same sampled height across neighboring Horizon revisions (assuming the same source snapshot). Ghost-neighbor heights one lattice step beyond the patch now determine `slopeXPermille` and `slopeZPermille` on each terrain vertex, preserving matching normals on overlapping borders. Partial CPU re-bakes conservatively dirty a one-grid-step neighborhood so editing a heightfield tile also refreshes neighboring normals. Adds `terrainSkirtDepthCm` to Horizon settings and `HorizonBake`.
- `d3bf262a09f03deb4a059c25324f00427c81498e` — `engine/render/TerraWeaveHorizonModel.h`: normals use the per-vertex global slope samples, **not** clamped one-sided local-patch differences. Creates one optional narrow **downward skirt along all four outer perimeter edges** (4×(N+1) additional vertices, 8×N triangles) to hide small outer-grid cracks while leaving actual top terrain heights unchanged. It remains part of the same existing material-0 primitive and shared GPU Model, with no extra material/draw submission.
- `509922881fbc46a4be83ce5da5dab4d797f152d6` — `engine/world/TerraWeaveHorizonArchive.h`: **TWH3** replaces TWH2; archive now stores `terrainSkirtDepthCm` and both signed integer slopes for each terrain vertex. `kVersion=3` changes the generator fingerprint. TWH1/TWH2 are deliberately refused as obsolete; the existing archive reader/batcher falls back to regeneration or explicit offline re-bake.
- `966a14c3d5823f19eb9d17c53617bca56682d9c4` — `engine/world/TerraWeaveHorizon.h`: `estimateHorizonBudget` includes skirt vertices and triangles. Minimal disables the skirt and Mobile uses 600cm depth; Balanced/High use default 1200cm. Default worst-case increases to **9,253 vertices / 8,320 triangles**, while atlas size/slots are unchanged (1-view atlas still 7,750,656 RGBA bytes before mips).
- `facbb877eae2d27ef9bf6854bbe4b944e2afafb2` — `engine/world/TerraWeaveHorizonManifest.h`: opt-in `terrainSkirtDepthCm=0..5000` project setting.
- `25bcfbed42dc9ee337e5b136553d73147c260d16` — `engine/render/TerraWeaveHorizonGpuPatch.h`: layout compatibility additionally requires identical skirt depth; strict atlas-only compatibility considers per-vertex slope changes as changed GPU geometry. Vertex-patch planning can still handle small terrain slope edits under its existing topology/bounds limits; skirt boundary edits that change skirt geometry conservatively fall back to full model upload.

**Technical rationale:** Old generator anchored its grid to `distantTileCm` region boundaries and computed vertex positions as `origin + floor(i * side / resolution)`. For High quality 64×64 the 512m region snap shifts the 192m vertex phase: the same world point may have different triangulation/slope across neighboring proxies. New globally anchored indices avoid per-region sample-phase changes for any valid `terrainResolution`, and ghost samples avoid region-dependent one-sided normals. The skirt hides small outer cracks without adding a third material, new shader, gameplay ECS entity or new rendering backend.

**Limits / remaining work:** This is geometry stitching and conservative crack concealment, **not** proof of visually seamless mountain morphing. A swapped opaque far-terrain ring can still show an abruptly changed outer silhouette (particularly across large teleports); the central near-terrain ownership hole is unchanged. A skirt may create a downward wall if the horizon edge is visibly within the view frustum; fog/atmospheric integration may be needed. Exact consistency requires all neighbor bakes to sample the **same immutable authored height source**; newly loaded authored tiles can legitimately change heights and require rebaking. Model-only skirt changes may push partial GPU vertex updates into the full-upload fallback, which is safe.

**Tests actually performed:** GitHub source read/write and manual static inspection of the generated source and archive contracts. **NOT RUN:** CMake, CTest, unit tests, CI execution, editor/game launch, GPU capture/Metal/GL/Vulkan, timing benchmarks. Keep PR Draft until validated by the dedicated testing agent.

**Tester agent work:** update assertions that assumed old TWH2 and exactly 9,057 vertices/7,936 triangles; verify TWH3 round-trip and invalid-old-version fallback; compare global-grid heights/normals for overlapping regions at 16,24,48,64 and nondividing 47 resolutions, including negative X/Z; validate partial-edit neighborhood normal re-evaluation and unchanged atlas offsets; validate skirt indices/orientation/topology/bounds, material 0 opacity and no extra draw calls; test RHI partial vertex updates vs full fallback; inspect screenshots at extreme terrain relief, near-region boundaries, high camera elevation, teleport and transitions; check atlas memory/fps bounds. Append observed results to this file.

**Next implementation-only scope:** share a generic stitch/geomorph policy with independently authored near-ground terrain and add optional distance-fog/horizon-edge treatment if those contracts exist in the renderer. Avoid creating a parallel terrain renderer. Testing agent owns all CTest/GPU evidence.

### Stage 16 continuation — reusable public Horizon terrain lattice (COMMITTED, NOT TESTED)
- `ae5cb9a1f2519e18df7ef8b13bf42cd4592298d2`:
  `engine/world/TerraWeaveHorizon.h` exports a small
  `HorizonTerrainLattice` POD and
  `makeHorizonTerrainLattice(settings,cameraXcm,cameraZcm)`.
  Native terrain producers and offline asset-bakers can obtain the
  same world-space X/Z sample coordinates via `worldX(index)`,
  `worldZ(index)` and `nominalStepCm()`; the CPU Horizon
  baker itself now uses this shared API instead of a local
  duplicate formula. Ghost neighbors use -1 / N+1.
- `873fd8495cf8ef72481d7a369feb0e3e7bfc5067`:
  `HorizonTerrainLattice::validate()` rejects zero/negative
  step or zero/invalid resolution before signed division.

**Actual verification:** GitHub source review and commits only.
No compiler, CTest, native execution or GPU checks have run.
The test agent should add direct lattice-coordinate tests for
48/64/47 resolutions, negative and adjacent region keys, and
TWH3 round-trip seam parity. Keep PR Draft.

### Stage 16 API-compatibility follow-up (COMMITTED, TESTS NOT RUN)
- `2d400e164d378b9231f8d3f548a7c36ee332b677`:
  moved `terrainSkirtDepthCm` to the end of both
  `HorizonSettings` and `HorizonBake`, preserving
  positional aggregate initializers in native C++ projects.
  This changes no stored TWH3 layout, archive fingerprint,
  default depth, generator output or renderer behavior.
- Actual verification is still **source-only**. Dedicated
  testing agent should add native aggregate-initializer
  source compatibility checks and compile/regression tests.

## Stage 17 — authored near/far terrain ownership + skyline grid stability (CODE COMMITTED; TESTS DELEGATED)

**Branch:** `feature/terraweave`, Draft PR #24. Work was performed after the Stage 16 main merge/horizon lattice work. The user explicitly assigned all CMake/CTest/driver tests to another agent; no test or benchmark was run by this feature agent.

### Source and commit trail

- `6ec149da23930b867e6aabc45dab1de71d84b7ca`: `engine/world/TerraWeaveWorldHeightSource.h`, new genre-independent `AuthoredWorldHeightSnapshot::coversRect(x0,z0,x1,z1)`. Conservatively returns true ONLY if the entire half-open centimetre rectangle is covered by resident near-ground height tiles, with bounded 4096-cell enumeration and signed negative coordinate support. A missing tile must never be mistaken for near terrain.
- `e62640a9aa0d95c946840f049bdbcb18fdecc0be`: `engine/world/TerraWeaveHorizon.h`, optional `HorizonSurfaceSources::ownsNearTerrainRect`, `HorizonBake::nearOwnedQuads` (N×N bytes, 0/1) plus `HorizonSettings::terrainGridSnapSteps` and globally snapped far-ground lattice. When a full-coverage provider exists, the previous baked circular terrain cutout is replaced by an exact coarse-quad ownership bitmap; the bitmap participates in incremental authored-region invalidation. Without a provider, the backwards-compatible radial cutout remains.
- `a471d2a2e584492215add6b368832d830a3c340c`: `engine/render/TerraWeaveHorizonModel.h` clips coarse far-ground **quads** only where `nearOwnedQuads` reports complete near coverage. Adds up to 512 inner boundary lip segments (two extra bottom vertices and two triangles each) along the near/far interface. The lip belongs to existing material slot 0 and uses the same model, without a new draw call. If the map has too many fragmented edges, only the lip is skipped; the conservative ownership clip remains intact.
- `a17c35826350a5db70fe230fe9a6a91fd07bad32`: `engine/world/WorldScene.cpp` connects the existing immutable `AuthoredWorldHeightSnapshot` to `HorizonSurfaceSources::ownsNearTerrainRect`. Worker reads a shared immutable snapshot, not mutable ECS/gameplay state. Loaded near terrain tiles own their actual coverage; missing/unloaded tiles let Horizon fill the gap rather than leaving a one-kilometre radial hole.
- `1333e0ae321f7e15da879ecf76f7f13f9e4a323a`: `engine/world/TerraWeaveHorizon.h` adds `HorizonSettings::innerTerrainLipCm` (0–200, default 40cm), corresponding `HorizonBake` field and worst-case analytic geometry bounds for 512 optional lip segments.
- `594aaf7e320306b4752473b826486e9a91d7b720`: `engine/world/TerraWeaveHorizonArchive.h` advances to **TWH4/version 4**. New wire fields contain inner lip depth and the near-owned N×N 0/1 map (including a distinct empty-map mode), while the generator fingerprint includes `terrainGridSnapSteps` and lip depth. Old TWH1–TWH3 blobs are rejected and may be regenerated by existing `aiverse_horizon_bake` or worker fallback. Archive decoder validates near map length and values; size bounds stay 20MiB.
- `3f01784d3c7b574ac88eb30228aff950bf42808a`: `engine/world/TerraWeaveHorizon.h` snaps terrain grid start to the **nearest** global multi-step anchor, with default 4-grid-step grouping (Minimal 1, Mobile 2). Repeated 128m forest region updates can reuse the same far terrain sample positions, reducing visible skyline shifts. The distant canopy radius is conservatively limited to the guaranteed area covered by the snapped terrain patch (approximately 5+km in default Balanced mode) so the forest cannot render over unsupported outer sky.
- `615c7f6eecda7a6ff9c4d0c28caa7700e6813c2b`: manifest adds optional `terrainGridSnapSteps=1..16` and `innerTerrainLipCm=0..200`.
- `43ad868544d97e406de0888945ca277273cbf335`: `engine/render/TerraWeaveHorizonGpuPatch.h` requires identical inner lip depth and per-cell near ownership mask before reusing a GPU index/layout. Changes in near-terrain residency fall back to full model upload instead of writing mismatched buffers.
- `041db3aaf2ad2ab2781702d55ba7b696621929a2`: the outer skirt is omitted when no actual top-ground triangles survive the near ownership cutout, avoiding an irrelevant isolated wall.

### Behavior and invariants

The near-vs-far handoff now reflects **actual resident authored WorldScene terrain tiles**. The far-ground cutout can have an irregular border rather than a fixed circle. A fully near-owned N×N coarse cell omits both distant triangles; an incomplete/missing nearby tile conservatively keeps the distant coarse cell visible. Short downward lips (40cm default) can conceal small edge cracks, with a 512-segment CPU/GPU geometry budget and no additional material/draw call. No FPS/RTS dependency, collision mutation, physics/navmesh change, new graphics backend, or per-tree entity is introduced.

The forest atlas still streams on its 128m keys while the far-terrain lattice anchor uses larger steps, making mountain silhouette positions more stable across successive forest updates. Conservative automatic far forest radius preserves terrain coverage even when a snapped terrain lattice center is offset from the camera; the radius can decrease from an earlier roughly 6km to approximately 5+km but remains several kilometres.

`HorizonBake` and `HorizonSettings` new members are trailing to preserve existing native positional aggregate initializer field order. TWH4 is a new incompatible persisted representation; do not claim TWH3 files remain readable. All old stage references to TWH1/TWH2/TWH3 are history, not the current wire version.

### Remaining limitations / tester acceptance

This is **conservative quad-granularity ownership**, not exact polygon intersection clipping. Coarse cells only partially overlapping an authored near tile still draw the entire far quad and can show small coplanar overlaps or Z-fight. The inner lip cannot repair large differences between separately authored near and far heights. Changes in the true terrain source between streamed snapshots may legitimately change the mountain silhouette, and the current outer edge has no GPU geomorph beyond prior alpha forest transitions and perimeter skirt. Minimal/mobile fallback presets may still display the radial hole if no authored near-ownership source is ever provided.

**Actual verification:** GitHub source writes, manual structure inspection and branch commit confirmation only. **NOT RUN**: compiler, CMake, CTest, shader tests, macOS editor/game runtime, Metal/Vulkan/GL screenshots, frame time/GPU bandwidth metrics or sanitizers. A separate testing agent owns them. Keep PR Draft.

**Test-agent checklist:** add and run authoritative tile `coversRect` tests including adjacent/missing/negative keys; WorldScene terrain tile loads/unloads and handoff masks; compare near-owned vs legacy radial cutout; near lip edge winding, material/topology, 512-segment cap and no output with fully-owned terrain; TWH4 encode/decode, corrupt near mask and TWH3 rejection; conservative GPU patch fallbacks on nearOwned changes; nearest snapped terrain lattice 16/24/48/64 and negative positions, far forest no-hole bound, fast observer teleport and source revision invalidation; screenshot near/far Z-fighting and opaque material ownership on Metal/GL/Vulkan. Record real outcomes here.

**Next function-focused stage:** if screenshots show unacceptable partial-cell Z-fighting, introduce bounded CPU clipping against rectangular authored near coverage (or a common terrain-stitch mesh) in the same Horizon Model, rather than another renderer or unbounded GPU mesh. Consider steady-state far outer skyline GPU geomorph after measurement. Do not duplicate tests.

## Stage 18a — bounded precise near-ground coverage inputs (COMMITTED; NOT TESTED)

**Branch:** `feature/terraweave`, Draft PR #24; code-first scope, all CMake/CTest/GPU tests by a separate validation agent.

**Commits and files:**
- `a37d6d1f5f2757f1bd8a9a9c2d852b43d082c0b1`: `TerraWeaveWorldHeightSource.h` adds `AuthoredWorldHeightSnapshot::coveredRectsIn(area,out,maxRects=1024)`, collecting the exact, nonoverlapping, clipped world-space rectangles of resident authored height tiles. Deterministically sorts them by geometry position, rejects invalid world extents and falls back safely if tile count exceeds configured cap.
- `18f818dbb48e841ccbb91e24bcfc3d45bae260a2`: `TerraWeaveHorizon.h` adds optional `HorizonSurfaceSources::collectNearTerrainRects`, trailing `HorizonBake::nearClipRects` and `maxNearClipTriangles` (0 disables, default 2048, mobile 512, minimal 0). The CPU baker captures at most 1024 immutable coverage rectangles once per region and verifies they are bounded to the far field. The old full-quad `nearOwnedQuads` is retained separately. Analytic vertex/triangle bounds include the maximum extra partial geometry.
- `f6fcd1ac1d3a9b762294ecf3098185693b6483a9`: `WorldScene.cpp` connects the existing immutable authored terrain snapshot as the rectangle provider, without accessing mutable ECS/physics state on the worker.

**Architecture:** near-coverage lists are strictly bounded, sorted world-centimetre rectangles, not per-vertex individual tree objects or a new terrain renderer. They can be used to replace only the portion of a coarse Horizon cell that is NOT owned by near terrain. If unavailable or overflowed, the old conservative full-quad ownership policy remains. This stage adds the input contract and data capture **only**; the following stage must implement actual mesh clipping, archive migration and GPU topology invalidation.

**Actual verification:** successful GitHub code commits and source inspection; compiler, CTest, GPU screenshots, runtime and benchmarks **NOT RUN** (testing agent owns them). Continue to the implementation of the bounded partial-quads geometry and update this handoff after committing it.

## Stage 18b — exact bounded CPU clipping / TWH5 / conservative GPU fallback (COMMITTED; NOT TESTED)

**Commits/files:**
- `7bd33d2d6bd67d8b6e085c6df87d7dec01c2881d`: new `engine/render/TerraWeaveHorizonClip.h`, CPU-only, genre-independent polygon subtractor. Each coarse far-ground quad consists of the SAME two original triangles. For every intersecting immutable resident near-terrain rectangle, subtract its X/Z footprint by clipping against four nonoverlapping outside halfplanes; fan-triangulate remaining convex polygons. The original triangle winding and exact far-plane surface are retained by barycentric interpolation. `planHorizonNearGroundClipping(bake)` returns an all-or-nothing plan capped by `bake.maxNearClipTriangles`; `nullopt` means fallback to the previous conservative full-quad ownership cutout.
- `757b6ba4480a93e97a84001d72e3fc575a350415`: clipping core computes polygon area relative to local first vertex to avoid cancellation at distant world positions; initializes triangle polygons with explicit iterator range.
- `d5475b5be1a579f3dab09014b841107599e21267`: `engine/render/TerraWeaveHorizonModel.h` uses the plan to suppress original triangles only in affected partial coarse cells, then emits the bounded resulting clipped triangles and interpolates height, normals, UV and vertex colours from each original source triangle. Keeps existing material slot 0, forest slot 1 and one RHI Model; does not create a new terrain renderer. If planner returns nullopt, **no partial clipping is applied** and preceding full-quad logic remains.
- `ee011b1c31c38ad07c2cbf967281b3dd7f960455`: `engine/world/TerraWeaveHorizonArchive.h` upgrades wire format to **TWH5/version 5**, persisting bounded near coverage clip rectangles and max clip triangle budget; adds bounds, counts and truncation checks and changes settings fingerprint. Older TWH1–TWH4 static assets are intentionally rejected and must be rebaked with `aiverse_horizon_bake` or runtime fallback.
- `4a7e403ef6abfe0a6e833b61422ba148db0e07b1`: `engine/world/TerraWeaveHorizonManifest.h` accepts `terraweaveHorizon.maxNearClipTriangles` (0..8192).
- `77b2b9d7351ceb6987260486c7c8b5c5bb64604c`: `engine/render/TerraWeaveHorizonGpuPatch.h` requires identical near clip rectangles and the same clip triangle cap for in-place RHI updates. Changes that affect triangulation use existing full-model replacement rather than unsafe partial vertex/index writes.
- `dbad7fed7ce4a62555e655585360d4ff74d16e11`: `engine/world/TerraWeaveHorizon.h` high-quality preset increases max partial clipping to 4096 triangles; Balanced remains 2048, Mobile 512 and Minimal disables. Analytic worst-case vertex/triangle counts include the configured cap.

**Architecture and limitations:** Only polygons from the area NOT covered by the already loaded near-ground height tiles are rendered for partial coarse far-ground quads. Exact X/Z tessellation respects the original coarse triangle planes; no double-covered horizontal area should remain inside these clipped cells. Triangle count is bounded, with deterministic fallback to old coarse full-quad mask if resident near geometry is excessively fragmented. Model/atlas GPU caching and RHI continue normally; no new material/shader/backend or ECS dependence.

**Caveat:** The clipped edge follows the old far-ground plane height, not the potentially different high-resolution near-ground surface height. X/Z overlap is removed but a small vertical seam may remain where the near/far heights disagree. Existing inner lip only handles whole-quad owned boundaries; this exact clipper does not promise a seamless vertical join. Additionally, the fallback if budget is exceeded deliberately retains partially overlapping far quads rather than exceeding memory limits. Both require real GPU screenshots/terrain source comparisons. Legacy procedural-only worlds with no near clip rectangles remain unchanged.

**Actual verification:** code written through GitHub API and static source inspection only. **NO compilation, CTest, GPU screenshots or benchmarks performed**; per user request the separate testing agent owns validation. Draft PR must remain Draft.

**Tester acceptance:** exact subtracted 2D areas for one tile inside a quad, half/quarter coverage, multiple nonoverlapping near tiles, tile coincident with outer boundary, negative world coordinates and diagonal source-triangle intersections; original triangle winding and interpolated heights; near ownership mask vs exact rectangles; hard overflow fallback; TWH5 roundtrip/version errors/malformed rect size and old TWH4 rejection; GPU topology patch fallback; native WorldScene with tile unload/fast camera move; no new materials/draw passes; screenshot Z-fighting and vertical seams. Record results in this handoff.

**Next feature-only work:** optionally add a bounded geometry skirt along the precise newly-clipped near-terrain tile edges (not only fully owned coarse cell boundaries), with height-aware seam correction if a stable near-surface sampling snapshot can be stored in the bake/archive. Do NOT add unbounded triangles or a second terrain renderer.

## Stage 18c — partial clip diagnostic propagation and degenerate handling (COMMITTED; NOT TESTED)

**Commits/files:**
- `a9ef5106df0a13d74d8d128e3f82eeb791defaf5`:
  `engine/render/TerraWeaveHorizonModel.h` adds optional,
  source-compatible `HorizonModelBuildStats*` output to
  `makeHorizonModel(bake,stats=nullptr)`. Reports count of
  coarse cells whose original far triangles were replaced,
  the number of emitted clipped triangles, and whether
  geometry-budget overflow forced the previous conservative
  full-quad fallback. This is CPU model-build telemetry only.
- `855c8b4c34f4208243bf7b222f03ce6053ffd2c6`:
  `engine/render/TerraWeaveHorizonGpuCache.h` stores the
  same three diagnostic values with a move-only GPU-resident
  cached region; a cache restore retains accurate measurements.
- `d740c5cf94ff17a2d8b6d13650bcee9d1ab0e8e6`:
  `engine/render/TerraWeaveHorizonScene.h` records clipping
  diagnostics on worker model preparation and propagates
  `HorizonRuntimeStats::nearClipQuads`,
  `nearClipTriangles`, `nearClipFallback` through
  full upload, incremental GPU patch, empty-model publication
  and GPU cache restoration.
- `6abbadd1d759abb66a5297101ad18e7dee356040`:
  `engine/render/TerraWeaveHorizonClip.h` rejects
  zero-area fan triangles formed when tile-boundary clipping
  leaves collinear polygon corners; they consume no
  GPU geometry/budget.

**Actual validation:** GitHub API commits and manual source
review only. **No tests/build/CTest/GPU runs**. Testing agent
owns source-compilation and synthetic geometry test fixtures
as documented in Stage 18b. Track clipping counters in
real scenes to decide whether to increase the default
`maxNearClipTriangles` cap; no measured FPS is claimed.

**Remaining limitation:** these diagnostics count the
precisely generated *remaining far-ground triangles*, not
the number of near-geometry triangles. When source/cap
fallback is used, `nearClipFallback=true` is explicit
rather than silently presenting the geometry as exact.

### Stage 18c follow-up — portable clip helper include (CODE COMMITTED; NOT TESTED)

- `a83421dc0e1c3ff933e65022a73f0da0697997d0`:
  `engine/render/TerraWeaveHorizonClip.h` now explicitly
  includes `<utility>` for `std::pair/std::move`, instead
  of relying on indirect inclusions. No runtime behavior
  or persisted TWH5 data changed.

**Tests:** NOT RUN per separate validation-agent ownership.

## Stage 19 — height-aware weld at exact Horizon near/far clip perimeter (SOURCE COMMITTED; TESTS NOT RUN)

**Branch/PR:** `feature/terraweave` / Draft PR #24. The user assigns compilation, CTest, GPU captures, perf and acceptance tests to a **different agent**. All code source commits below were made via the connected GitHub repository; no test run is implied.

**Architecture:** Stage 18's CPU clipper subtracts loaded near tiles from coarse far triangles. This stage samples the **actual authored near-ground height along the resulting clip boundary** and stores immutable Q16-centimetre X/Z keys plus signed near height in the `HorizonBake`. After the far terrain's old source triangle attributes have been interpolated, `makeHorizonModel` replaces ONLY the corresponding clipped-edge vertex Y with the baked near-surface height. The interior clipped polygon vertices keep the far source triangle plane; the implicit gradual slope is a geometric interpolation across the existing clipped triangles, not another mesh/material/draw. Distant foliage atlas/forest cluster geometry, gameplay ECS, collision and navmesh are unchanged.

**Changed files / actual commits:**
- `7b01dc6ec65bedd8653065b1e22242e67f1bed6d`: `engine/world/TerraWeaveHorizon.h` extends the optional immutable `HorizonSurfaceSources::nearGroundHeightAtCm` callback (returns `std::optional<int32_t>`) and appends `HorizonBake::NearSeamSample{xQ16cm,zQ16cm,nearHeightCm}` / bounded `nearSeamSamples`. Does not alter the native `HorizonSettings` or earlier positional bake initializer fields.
- `b6b48e4d984ac836eaef08a670b239db1cda45ba`: `engine/render/TerraWeaveHorizonClip.h`, new `populateHorizonNearSeamSamples(HorizonBake&,source)` CPU helper. Runs the already-bounded exact polygon clip planner, scans points on resident near rectangle edges, deduplicates with Q16 world-centimetre keys, and samples the near height at most **4096** distinct positions. Coordinates are clamped at most 1cm INSIDE a known resident half-open tile to avoid reading unloaded neighbors at max edges. If any required source height is missing/invalid or the geometry/sample cap is exceeded, the entire sample table remains empty: no half-completed weld is rendered. Does not access GPU or ECS.
- `8d29b862ea4c70fdc8b809755e76bb17bed2e7f6`: `engine/world/WorldScene.cpp`, immutable snapshot `AuthoredWorldHeightSnapshot::sample` is exposed as the new near-height callback; it retains the same near terrain height snapshot as `ownsNearTerrainRect` and `collectNearTerrainRects`.
- `89a5ec010f014a262bdd0337478c8ad33598befc`: `engine/render/TerraWeaveHorizonScene.h` invokes seam sampling immediately after CPU `bakeHorizon()` and BEFORE putting the immutable bake into the CPU cache, emitting a model, and writing a static archive. Valid archives are read without consulting the current collision state on the render thread.
- `540689e2d3de5aa19677d0c148f9a658b3f8d275`: `engine/world/TerraWeaveHorizonBatch.h` adds the same optional seam sampling to native offline `bakeHorizonBatch`, keeping asset builds and runtime worker results consistent.
- `3ed7395c4d8a9d314a3021bb00da5c34586c5bce`: `engine/render/TerraWeaveHorizonModel.h` looks up each clipped polygon vertex in the immutable height map and matches its near-surface Y; interior far vertices remain unchanged. `HorizonModelBuildStats` adds `weldedVertices` and `maxWeldHeightDeltaCm`. No extra geometry, GPU model, material or RHI backend.
- `3c9d709fc82aea3914ec10db9f2f008b6e820dac`: `engine/world/TerraWeaveHorizonArchive.h` bumps current asset format to **TWH6/version 6**, with a count and packed `(i64 XQ16, i64 ZQ16, i32 nearHeight)` per near seam sample. Checks world extents, count <=4096 and truncated data. The fingerprint uses the new version, so legacy **TWH1–TWH5 must be regenerated** with `aiverse_horizon_bake` or worker fallback; existing `.twh` asset path convention unchanged.
- `48fe3014995f457a3e80d4751e580a34c4a034fa`: `engine/render/TerraWeaveHorizonGpuCache.h` retains the two seam diagnostics during GPU LRU model reuse.
- `2f3bb3254887eda67f654fd616c41d623b1b56f3`: `engine/render/TerraWeaveHorizonScene.h` publishes `HorizonRuntimeStats::weldedNearVertices` and `maxNearWeldDeltaCm` from CPU preparation through full GPU upload, GPU patch, empty model and cached-region restore.

**Functionality/constraints:** This is **height matching on exact clipped X/Z boundary points**, NOT a full multiresolution terrain geomorph or recalculated near-tile normal-field blend. The resulting far triangles can become steep if nearby authored terrain is much higher/lower than their coarse source; the existing model normal interpolation may remain visually different from true welded face normals. On worlds without `nearGroundHeightAtCm` or without near rect clipping, the sample map stays empty and the previous Stage 18 clipper remains unchanged. When *any* required height sample is absent, no partial weld is applied. Because sample coordinates are derived by CPU polygon clipping, the Q16 coordinates are provided and read by the same shared C++ implementation in normal and offline pipelines; exact cross-compiler floating rounding must be verified by the testing agent. Sample count adds at most ~80KiB to the bake/serialized blob (plus container overhead), not GPU geometry.

**Tests actually performed:** GitHub source reads, modifications and code contract inspection only. **NOT RUN:** builds, CTest, independent geometry fixtures, cross-platform Clang/MSVC archive reproducibility, Metal/Vulkan/GL screenshots, GPU/CPU timing, or driver recovery. Keep Draft PR. Don't claim seamless vertical terrain joins until the testing agent confirms.

**Tester-agent acceptance matrix:** Add independent 2D clipped-edge point fixtures with half-open tiles/negative coordinates/fractional intersections; near-height sampling at a tile's east/north border and large world coordinates; compare near boundary vertex Y vs `AuthoredWorldHeightSnapshot` within <=1cm; validate multi-rectangle continuity, source-missing all-or-none fallback, 4096 point cap, TWH6 roundtrip and deliberate TWH5 rejection; compare offline batch and worker with authoredContentHash; verify GPU full/partial patch fallback on changed near seam sample heights/bounds, cache restoration stats, steep terrain normals and performance with 1024 rects. Record actual results here.

**Next implementation-only scope:** if terrain visuals reveal a steep wedge after height welding, add a bounded near-edge normal or explicit smoothing policy using the same near-height snapshot (no extra draw or cross-genre dependency). Do not silently alter shared collision geometry. Keep UI/editor changes optional.

## Stage 20a — seam steepness preflight and coherent normals (FUNCTION SOURCE COMMITTED, TESTS DEFERRED)

**Branch:** `feature/terraweave`, Draft PR #24. User assigned ALL build/CTest/GPU tests to another agent.

- `57ccba5f2f0adb84b245bf6a556f7e1965b2934d`: `engine/world/TerraWeaveHorizon.h`, trailing settings `maxNearWeldSlopePermille` (0=off, 2500 default, max 8000) and `nearSeamNormalBlendPercent` (65 default, 0..100); trailing `HorizonBake` policy/diagnostic fields preserve existing positional native aggregate initialization.
- `95f6494071ad5b87608fa7c6c33430e10f2b14d4`: `engine/render/TerraWeaveHorizonClip.h` preflights newly welded X/Z clipped triangles in real 3D, using the same original far-triangle barycentric heights as the model builder and signed centimetre near seam heights. If the seam weld would produce a NEW geometric slope above the configured permille threshold (beyond the original far-terrain slope), it publishes **no near-height seam sample table** and sets `nearWeldSlopeFallback`. X/Z near-ground clipping remains active. Boundary height is NOT misreported as welded in this case; a vertical gap can remain. This is a safety fallback rather than impossible physically perfect welding without a wider mesh.
- `e19a94ac499a8b13d2367a129c76f5b7674d4f89`: `engine/render/TerraWeaveHorizonModel.h` aggregates repaired face normals weighted by geometric face area and **world-space corner position**, then blends them at near seam vertices (65% default) or nearby far-side vertices (16.25% default). This reduces disconnected per-triangle normal changes without creating a separate shader or additional GPU vertices.
- `2d8592bb06a5094ed40201c81a339d51f822a9ee`: same model header limits normal-group iteration to actually emitted nondegenerate clipped triangles, keeping vertex/triangle lookup in sync.

**Architecture:** clipping, slope checks and normal adjustment are performed during the existing CPU model build; render backend, ECS authority, collision height and near-ground renderer are not touched. No additional mesh topology, material or atlas is introduced. The source archive/settings/manifest/runtime stats still need the Stage 20b wiring before this feature is fully configured in games.

**Actually tested:** GitHub source reads + successful file commits only. **NOT RUN**: CMake, CTest, geometry fixtures, Clang/MSVC, Metal/Vulkan/GL images, profile. Dedicated tester must explicitly check steep wedge policy, normal continuity, negative world coordinates, null near samples and triangulation degeneracy. Do not mark PR ready.

## Stage 20b — TWH7 slope/normal configuration, runtime diagnostics (SOURCE COMMITTED, TESTS NOT RUN)

**Continuation of Stage 20a, current branch** `feature/terraweave`, PR #24 Draft.
The *separate* testing agent owns all Clang/GCC/MSVC builds, CTest,
native editor/game execution, GPU screenshots and benchmarks.

**Commits and files:**
- `2e7983c4d0dac7aa456194c47674eec23b8adada`:
  `engine/world/TerraWeaveHorizonArchive.h` upgrades to **TWH7**
  (magic/version 7). Serializes bounded
  `maxNearWeldSlopePermille` (0=guard off; <=8000),
  `nearSeamNormalBlendPercent` (0..100), and the boolean
  `nearWeldSlopeFallback` alongside the previous TWH6
  near sample table. Fingerprint includes BOTH settings;
  incompatible TWH1–TWH6 archives are rejected and must be
  rebaked with the existing offline CLI or runtime fallback.
  Archive validation explicitly forbids fallback=true with
  a nonempty seam-height sample table.
- `e21673073f330e7f0d1be67971b7a8e9e37273ad`:
  `engine/world/TerraWeaveHorizonManifest.h` accepts optional
  JSON settings `maxNearWeldSlopePermille=0..8000` (default 2500)
  and `nearSeamNormalBlendPercent=0..100` (default 65).
- `4dea9ff9e5b31d716f95e3bf537028a578f1bc4a`:
  `engine/render/TerraWeaveHorizonGpuCache.h` keeps
  `smoothedNearNormals` and `nearWeldSlopeFallback` per
  GPU-resident region, so revisiting a region does not
  misreport its presentation policy.
- `d658d402e992a4cecc31ab3777feb6f5499acadd`:
  `engine/render/TerraWeaveHorizonScene.h` publishes both
  fields through `HorizonRuntimeStats` for CPU-worker
  preparations, full GPU uploads, incremental patch
  publications, empty-model handling and GPU cache restores.

**Semantics:** The triangle geometry gradient preflight is
all-or-none per Horizon region. The *chosen* slope threshold
guards against **newly introduced** excessive face gradients
caused by snapping near-edge Y while retaining unchanged
far vertices. Preexisting naturally steep terrain is not
flattened. If a Y weld would violate the limit, only the
optional height-weld sample table is removed and
`nearWeldSlopeFallback=true`; Stage 18's exact horizontal
X/Z clipping remains. This may leave a visible vertical
height difference; the model does **not** pretend it is
simultaneously perfectly welded and strictly slope bounded.
The normal pass uses area-weighted geometric face normals
grouped by shared world XYZ and conservatively blends them
into far-normal attributes, especially at near seam edges.
Its output is vertex shading only; the near collision mesh
is unchanged.

**Important limitations:** A steep, natural near-terrain
slope cannot be made flatter without adding horizontal
transition distance or modifying the authoritative near
heightfield, neither of which is part of this no-extra-GPU-
geometry stage. The `2500` permille default is a safety
threshold, not a guarantee that the entire world terrain
has <=2500 permille grade. Conventional alpha canopy
transition remains unchanged. Very small/fractional
clipped polygons may trip the guard: test actual authored
WorldScene tiles before deciding to adjust limits.

**Verification actually performed:** GitHub source commits
and static source review only. **NO build, CTest, GPU run,
visual screenshot, benchmark, cross-compiler archive fixture
or measured slope regression**. Keep PR Draft.

**Tester acceptance:** synthesize large/small positive and
negative terrain wedges; verify at least one unsafe weld
sets fallback with empty near sample table and X/Z clip still
active; source naturally steep terrain should not be
flattened; compare shared near-seam normal values on
adjacent clipped faces, negative world coordinates, degenerate
clipped polygons and 0%/65%/100% normal blend; confirm
zero added indices/tris/materials and correct RHI update
fallback when normalized vertices differ; verify
TWH7 serialize/deserialize policy and TWH6 rejection;
observe cached GPU region stats, and inspect grazing-angle
lighting/canyon screenshots on GL/Metal/Vulkan. Record
ACTUAL test logs and details in this handoff.

**Next implementation-only stage:** a constrained, optional
multi-quad transition band may be considered if the testing
agent finds visible gaps on slope-guard fallback. Such a
mesh requires a genuinely wider near-to-far transition zone
and explicit triangle-memory budgets. Do not fix a slope
constraint by simply clamping the exact near-edge Y (which
would silently reintroduce cracks).

### Stage 20b portability follow-up (COMMITTED; TESTS NOT RUN)

- `88e9865abddf56b4e15a32f82baaf01740bcb1da`:
  `engine/render/TerraWeaveHorizonModel.h` explicitly
  includes `<array>` and `<utility>` for the C++20
  clipped-triangle normal-group implementation instead
  of depending on transitive includes. No logic, budget,
  RHI backend or TWH7 payload format changed.

**Actual validation:** GitHub commit success only.
No CMake/build/CTest/GPU execution; the dedicated
testing agent retains ownership.

## Stage 21a — fractional authored height and source-normal seam alignment (SOURCE COMMITTED; TESTS DEFERRED)

**Branch:** `feature/terraweave` / Draft PR #24. Stage 20 TWH7 slope guard and area-weighted face-normal smoothing were already in branch on entry (head `a77ac835b0f59334b72a6604ffba3dad1ef0f887`), so do NOT reimplement them.

**Source commits and changed API:**
- `c23d9010643d066227347a9d1d2e63e441570c8e`: `engine/world/TerraWeaveWorldHeightSource.h`: new `AuthoredWorldHeightSnapshot::sampleQ16(xQ16cm,zQ16cm)` samples **fractional** world centimetres inside the same authored two-triangle collider surface as integer `sample`. Uses bounded Q16 integer barycentric weights, signed negative-height rounding, explicit world limits, and preserves the exact legacy result for integer input positions. No added persistent heightfield, ECS or GPU buffer.
- `96b1e5e8277381edd8d292c0eed47ffcf50b1faa`: `engine/world/TerraWeaveHorizon.h`: optional `HorizonSurfaceSources::nearGroundHeightAtQ16Cm` callback; extends **trailing fields** of `HorizonBake::NearSeamSample` with slope X/Z in permille and validity flag. Legacy native providers can omit the callback and continue using whole-centimetre `nearGroundHeightAtCm`.
- `2e027de24eb4fdfc68893a417d87fe5a3e420a03`: `engine/world/WorldScene.cpp`: supplies the Q16 callback from the SAME immutable authored snapshot as the existing near coverage and height callbacks.
- `036a105a06fb7428b7b5fafd8e00a94d19e8fa74`: `engine/render/TerraWeaveHorizonClip.h`: samples exact clipped perimeter Q16 positions, clamping to at most 1/65536 cm INSIDE the resident half-open authored tile instead of offsetting by up to 1 cm. Computes bounded near-surface slope X/Z using +/- 200 cm (2 m) Q16 finite-difference stencil restricted to the SAME resident tile. Missing gradient samples fall back to face-normal shading; missing required height keeps the prior all-or-nothing height-weld fallback and slope guard.
- `920ac22bfcfad73b753492a0c97955c53378a2c0`: `engine/render/TerraWeaveHorizonModel.h`: seam vertices blend with the authored source normal at Q16-welded positions when valid; otherwise the earlier Stage 20 area-weighted clipped face normal path remains. Interior far-terrain normals continue to use previous conservative blending. No new model vertices/indices or draw calls.
- `a2e8e489231f1dbfca029986658bee06a0cdd92f`: `engine/world/TerraWeaveHorizonArchive.h` migrates to **TWH8/version 8**, storing two signed slope permille values and a bounded normal-valid flag per `NearSeamSample` (now 32 bytes vs 20). TWH7 and older assets are intentionally rejected; regenerate via `aiverse_horizon_bake` or runtime fallback.

**Important invariants:** WorldScene captures a single immutable authored source snapshot in each worker; neither GPU renderer nor gameplay state is consulted while reconstructing model vertices. All existing CPU triangle clipping, 4096 maximum welded seam samples, area-weighted normal continuity and `maxNearWeldSlopePermille` all-or-none slope guard remain. The Q16 sampler does not change the native integer-height `sample()` behavior. Terrain/forest geometry counts and shader/RHI contracts remain unchanged; only CPU sampling, stored per-seam metadata, vertex normal attributes and the TWH8 archive change.

**Actual verification:** GitHub source inspection/commits only. **NO compiler, CMake, CTest, GPU screenshots, benchmarks, running editor/game, or independent geometry fixture tests performed** (user delegates all to a separate testing agent). Keep PR Draft.

**Test-agent matrix:** compare `sampleQ16(x*65536,z*65536)` to legacy `sample` for negative and positive tile coordinates/heights, triangle diagonal and corners; check fractional coordinate continuity across neighboring same-height authored tiles; verify Q16 high-edge 1-unit inset; compare 200cm one-sided normal stencil on x/z tile edges and sloped near terrain; confirm missing Q16 source falls back to integer heights, missing gradient maintains previous area-normal rendering; verify old TWH7 rejection / TWH8 signed normal roundtrip and bounded count, Full/partial GPU model updates, source revision and cached model restoration. Report measured outcomes separately.

**Next functionality:** include source-normal-specific diagnostics in `HorizonRuntimeStats`, enforce strict atlas-only GPU geometry equivalence when seam normals/heights change, update docs/skill and PR description. No additional triangles should be introduced.

## Stage 21b — authored near-normal diagnostics and strict GPU atlas policy (SOURCE COMMITTED; TESTS NOT RUN)

**Current branch:** `feature/terraweave` / PR #24 Draft. Stage 21a introduced fractional Q16 height sampling, immutable source normal gradients and **TWH8**; this stage closes the remaining runtime safety/observability hooks. Another agent owns ALL tests.

**Commits / code:**
- `2e2380b9766fc71aba2f927a8cb4c4ead55eb190`: `engine/render/TerraWeaveHorizonGpuPatch.h` tightens the strict atlas-only API `horizonCanReuseModelGeometry`: it compares old/new near-seam Q16 positions, baked height, both near-slope components, validity flags, normal blend settings and slope fallback state. A changed near seam is **not** atlas-only geometry; the scene's separate combined atlas+VB planner may still apply a valid bounded vertex patch using the unchanged index/cluster layout, or take the existing full GPU model replacement if unsafe.
- `bbe54567d4effbc615bb8bf94ada7f953577443e`: `engine/render/TerraWeaveHorizonGpuCache.h` adds `matchedAuthoredNearNormals` to its GPU region metadata for accurate model-restore diagnostics.
- `117f74567f5627350826ea747f8045a8f8162ed5`: `engine/render/TerraWeaveHorizonScene.h` adds `HorizonRuntimeStats::matchedAuthoredNearNormals` and propagates it through CPU worker model preparation, in-place GPU patch success, full GPU replacement and cached-region restoration.

**Exact result:** `matchedAuthoredNearNormals` counts render vertices at exact welded seam coordinates where the source-authored Q16 near height snapshot also provided a valid tile-local normal. Existing `smoothedNearNormals` includes face-normal fallbacks and interior blended clipped vertices. Both counters are presentation diagnostics, not test results or GPU performance claims. The same single combined terrain/forest RHI Model and existing memory/triangle caps are maintained; per-record TWH8 payload grows by 12 bytes vs TWH7 (two int32 slopes + encoded validity).

**Safety / fallback:** Legacy native providers with only `nearGroundHeightAtCm` still produce near-height welding and Stage20 geometric face-normal smoothing; `nearNormalValid=false` means no invented source normal. If the near Q16 sampler exists but a derivative stencil fails, the height sample remains valid while authored-normal matching is skipped. The Stage20 all-or-none slope guard still applies to any height weld. CPU archive/model outputs remain deterministic for a fixed immutable source and compiler; cross-compiler edge-rounding comparisons are assigned to the tester.

**Actual verification:** GitHub source API reads and successful commits only. **NOT RUN:** CMake/CTest, real GPU screenshots, renderer/asset launch, Vulkan/Metal/OpenGL benchmark, unit tests or cross-compiler portable archive fixtures. Continue to keep PR Draft.

**Tester acceptance:** confirm existing atlas-only comparison rejects a change only in `nearSlopeXPermille` or `nearNormalValid`, while the vertex-patch planner detects updated normals and uses correct VB offsets; verify GPU-region restore matches the latest normal count. Test authored slopes on both sides of a half-open tile at negative coordinates, exact lattice/collision diagonal and Q16 fractional high corners. Validate TWH8 truncation, checksum, forged normal-valid bits, invalid slope bounds, rejection of TWH7, size <=20MiB and reuse of valid archive. Run real Metal/GL/Vulkan screenshots of smooth lighting with 0%,65%,100% normal blend, and verify no increase in draw calls/indices.

**Next function-oriented step:** If GPU screenshots reveal a perceptible shading mismatch at disconnected authored terrain tiles, a neighbor-aware near normal blending policy can be built on the same Q16 snapshots. A physically wider height slope band still needs explicit width/triangle budget and must never violate the existing conservative all-or-none slope guard. No new feature is implied to have passed its tests.

## Stage 22a — neighbor-aware authored terrain slope primitive (SOURCE COMMITTED; TESTS NOT RUN)

**Branch:** `feature/terraweave`; PR #24 remains Draft.
**Commit:** `4125a2372cf2cf7c90182c8e1d5b08a38e28986d`.
**Changed:** `engine/world/TerraWeaveWorldHeightSource.h`.

Added `AuthoredWorldHeightSnapshot::continuousSlopeQ16(xQ16cm,zQ16cm,radiusCm=200,toleranceCm=1)`, returning optional signed X/Z slopes in permille. The probe uses existing immutable Q16 terrain height tiles, a bounded radius (1..4096 cm), central/one-sided world-limit differences, and explicit continuity checks at every crossed tile boundary. Missing tiles or mismatched boundary heights return nullopt; it never mutates collision, ECS, GPU, archive format, or the legacy integer sampler. This is a source-side primitive, **not yet wired to Horizon seam baking**. Existing TWH8 and near seam normal counters are unchanged.

**Actual verification:** GitHub write success only; no build, CTest, runtime, GPU screenshot or benchmark. Do not claim test pass.

**Next implementation:** route a snapshot-derived neighbor-aware normal callback through `HorizonSurfaceSources` and `WorldScene`, retaining tile-local fallback when neighbors are unavailable. Then integrate into `populateHorizonNearSeamSamples`, update TWH8 compatibility only if serialized layout changes, and ensure GPU vertex patch comparison handles any changed normal attributes. Update `TERRAWEAVE.md`, skill, and this handoff with each next commit.

**Testing-agent acceptance:** two adjacent equal-height/sloped tiles with positive/negative world coordinates, missing adjacent tile, height-discontinuous cliff, corner crossing, one-sided world-limit stencil, 0/1/100cm tolerance, radius bounds, Q16 negative-height rounding, and C++ compiler checks. Validate no smoothing across missing/discontinuous seams. All tests remain assigned to the separate testing agent.

## Stage 22b — neighbor-aware Q16 normals wired to Horizon (SOURCE COMMITTED; TESTS DEFERRED)

**Branch:** `feature/terraweave`, Draft PR #24.
**Code commits:** `9183fb6219976e4e1e978de0c15a1e408be3c48d`, `7a47ff68db25def5ba283981f4ef7b73270d06f8`, `8c3721cf237e4f71f8417857fa63148e3565d9f5`.
**Files:** `engine/world/TerraWeaveHorizon.h`, `engine/world/WorldScene.cpp`, `engine/render/TerraWeaveHorizonClip.h`.

Added optional `HorizonSurfaceSources::nearGroundContinuousSlopeAtQ16Cm` callback returning signed X/Z permille slopes. WorldScene captures the existing immutable authored height snapshot and supplies `continuousSlopeQ16`. Horizon seam baking prefers the neighbor-aware result at each clamped Q16 near seam sample, falling back to existing tile-local 2m derivatives when the callback is absent or returns nullopt. The existing TWH8 `NearSeamSample` payload (slopes + valid flag), normal blending, vertex patch comparison and GPU cache counters continue to consume the same representation; **no archive version/layout change**. No extra geometry, draw calls, shader or ECS changes.

**Limits:** fallback can still show lighting discontinuity at authored cliffs or absent neighbors; this is deliberate. No dedicated count distinguishing neighbor-aware and local-fallback normals yet. Stencil radius is 200cm and continuity tolerance is 1cm. Boundary-adjacent Q16 samples may use one-sided data if neighboring tiles are not resident. The exact code path was not compiled or run.

**Actual verification:** three GitHub source commits succeeded. NO CMake, compiler, CTest, GPU screenshot, game/editor run or benchmark. Separate test agent owns validation; keep PR Draft.

**Tester matrix:** continuous adjacent tiles (including negative world coordinates), discontinuous height steps, missing neighbor, half-open boundary Q16 inset, tile corners, cross-compiler deterministic archive bytes, TWH8 roundtrip, near normal 0/65/100% blend, strict atlas-only rejection when slopes change, partial VB update and GPU LRU restoration. Confirm unchanged triangle/index/material counts and no GPU allocation regression.

**Next implementation:** add explicit source-normal provenance diagnostics without changing TWH8 archive layout (derive or track during CPU bake), expose neighbor-aware vs local-fallback counts in HorizonRuntimeStats/GPU cache, then update TERRAWEAVE.md and skill. Tests remain deferred to separate agent.

## Stage 22c — near normal provenance diagnostics (SOURCE COMMITTED; TESTS NOT RUN)

**Branch:** `feature/terraweave`, PR #24 Draft.
**Commits:** `60590e01efa3301ebb84855f6c9ace2c38d35fd3`, `fa90692ce0ed5fabd2e364df4297026cd798c2ee`, `36cbd2e0c8716d19a0d09a7bed467deb6e428a52`, `4da16f5277be66b93ea964f9c99ce270a290c62d`, `589f7e5d70f27701b7f16531c0022d2593e45d5d`.
**Changed:** `engine/world/TerraWeaveHorizon.h`, `engine/render/TerraWeaveHorizonClip.h`, `engine/render/TerraWeaveHorizonGpuCache.h`, `engine/render/TerraWeaveHorizonScene.h`.

Added nonserialized `HorizonBake::continuousNearNormalSamples` and `localFallbackNearNormalSamples`, incremented during seam CPU sampling. Added corresponding `HorizonRuntimeStats` and GPU LRU metadata fields. CPU prepared publication paths read counts from `prepared->bake`, retirement stores runtime counts, and GPU cache adoption restores them. These are counts of **unique seam sample points**, not GPU vertices or draw calls; `matchedAuthoredNearNormals` retains its prior vertex semantics.

**Important:** TWH8 disk archives intentionally do NOT store provenance counters. An archive cache hit therefore reports zeros for provenance even when archived slope values are valid. Do not present zero as proof no neighbor-aware work happened. This is a known limitation; next stage should add an explicit `provenanceAvailable` status (or derive provenance when rebaking) before exposing counts as authoritative diagnostics. Keep the archive format unchanged unless there is a justified version bump.

**Actual verification:** GitHub source writes only. No build, CTest, GPU runtime, screenshots or benchmark. Separate test agent owns verification. Keep PR Draft.

**Tester acceptance:** compile C++20, verify unique seam counts and local fallback paths, all-or-none slope rejection, GPU full/atlas/VB patch publication and GPU LRU restoration, disk archive cache hit zero/unknown semantics, no changed draw/triangle counts, and TWH8 compatibility. Also verify source-revision invalidation and no stale diagnostics after an empty model or cache restore.

**Next implementation:** explicit provenance availability flag, distinguish unavailable archive provenance from true zero; expose diagnostics to editor/host as appropriate, update docs and skill. Tests remain separate.

## Stage 22d — explicit availability for near-normal provenance (SOURCE COMMITTED; TESTS NOT RUN)

**Branch:** `feature/terraweave`, PR #24 Draft.
**Code commits:** `69a8059c40e4ca7fdb5f6b085733a204638330c3`, `edd0b237c88a6c02bbe1b74afd88cdefe1c8b7a5`, `0c43a49f6a4270048e3d4ccd74d068a8de720303`, `62929f00c209a2f1eac814c7bdd47d3775d24fc0`.
**Files:** `engine/world/TerraWeaveHorizon.h`, `engine/render/TerraWeaveHorizonGpuCache.h`, `engine/render/TerraWeaveHorizonScene.h`, `engine/render/TerraWeaveHorizonClip.h`.

Added nonserialized `nearNormalProvenanceAvailable` to HorizonBake, runtime stats and GPU region metadata. `populateHorizonNearSeamSamples` resets the flag and sets it true only after the source/clip-plan prerequisites are available. Runtime publication from prepared bakes, GPU retirement and GPU cache restoration now preserve it. A TWH8 archive decode defaults to false, so zero provenance counts from an archive are explicitly **unknown**, not a claim that zero neighbor-aware samples occurred. TWH8 on-disk layout and archive fingerprint remain unchanged.

**Caveat:** availability means the CPU sampler path was entered with a valid clipping plan, not that a nonzero number of samples or successful height welds exists. Existing all-or-none weld fallback may reset counts while the flag remains true. This status is diagnostic only; it must not affect geometry, rendering or physics. Source revision/cache paths and archive hits need independent tester verification.

**Actually verified:** GitHub file update commits succeeded. No compiler/build, CTest, editor/game runtime, GPU screenshots or benchmarks. Keep PR Draft.

**Next:** audit full/partial upload, empty-model and source revision paths for stale stats, update TerraWeave design docs/skill, and have separate test agent validate archive miss/hit, GPU cache restoration and all-or-none weld rejection with real CMake/CTest runs.

## Stage 22e — Horizon shutdown diagnostics lifecycle (SOURCE COMMITTED; TESTS NOT RUN)

**Branch:** `feature/terraweave`; PR #24 Draft.
**Commit:** `2d9501dee12b8d0d9859897933d7a6cc8467ebd5`.
**Changed:** `engine/render/TerraWeaveHorizonScene.h`.

Audited existing async publication and GPU cache adoption paths, and fixed a definite stale-state path: `shutdown(device)` previously released active model, transition, GPU cache and CPU cache but retained runtime counters and region origin/cluster metadata. It now resets `runtime_={}`, `clusters_=0`, `terrainQuads_=0`, `originXcm_=0`, `originZcm_=0` after releasing resources. This also clears the Stage 22c/d provenance counters/availability so a stopped scene cannot report a retired region as active. No archive, rendering, ECS or GPU geometry change.

**Known remaining concerns:** Existing `runtime_` fields are a mixture of lifetime cumulative counters and active-region counters; clearing on shutdown is intentional but should be documented in telemetry consumers. Source revision changes retain the old GPU proxy until replacement succeeds, so runtime diagnostics still describe the **displayed old region**, not the newly requested revision; consumers should compare active vs source revision before interpreting stats as current terrain. Other patch paths have not been runtime-verified.

**Actual verification:** GitHub commit succeeded. No compilation, CTest, editor/game execution, GPU screenshot or benchmark. Dedicated test agent retains ownership; PR stays Draft.

**Next functionality:** add explicit active/source revision and stale-proxy status to HorizonRuntimeStats (without altering archive or GPU layout), then audit cache-hit/empty-model/failure behavior. Tests later should verify shutdown resets stats, no active model after shutdown, and source revision pending preserves old model without falsely claiming fresh data.

## Stage 22f — active/source revision and stale GPU proxy diagnostics (SOURCE COMMITTED; TESTS NOT RUN)

**Branch:** `feature/terraweave`, PR #24 Draft.
**Commit:** `2a0bc2cde12fbbfe522b7b836ee60266d542e602`.
**Changed:** `engine/render/TerraWeaveHorizonScene.h`.

`HorizonRuntimeStats` now exposes `sourceRevision`, `activeRevision`, `activeRegionPresent` and `staleProxy`. `runtimeStats()` derives these live from `sourceRevision_`, `activeRevision_`, `model_` and `activeCell_`; no duplicated cache fields or TWH8 archive changes. `staleProxy` is true only when an active GPU model is present and its revision differs from the current authored source revision. An empty region, pre-first-upload state or shutdown reports no active GPU model. The existing explicit `nearNormalProvenanceAvailable` continues to distinguish unknown archive diagnostics from true zero.

**Important semantics:** a camera teleport to a different region with unchanged source revision can still leave an old proxy until coverage gating/next upload; `staleProxy` intentionally denotes **source-revision mismatch**, not all possible wrong-camera-region cases. The separate `submit()` coverage check still owns teleport visibility.

**Actual verification:** GitHub source commit only; no CMake/CTest, compiler, editor/game launch, screenshots or performance measurements. Keep PR Draft. Separate test agent owns tests.

**Next implementation:** expose requested vs active cell status (without changing GPU/archive layouts), and verify diagnostics on source edits, empty regions, GPU cache hits, transitions and shutdown. Then update TERRAWEAVE.md and agent skill. Do not assert passing tests.

## Stage 22g — requested vs active Horizon region diagnostics (SOURCE COMMITTED; TESTS NOT RUN)

**Branch:** `feature/terraweave`, PR #24 Draft.
**Commit:** `cbe93ad60fa5db9ffc32f71cc7846b9374ad9f61`.
**Changed:** `engine/render/TerraWeaveHorizonScene.h`.

Added owner-thread `requestedCell_` updated after camera hysteresis chooses `desired` and before GPU transition, fast-return, cache-hit and worker paths. `HorizonRuntimeStats` now publishes `requestedRegionPresent`, `requestedRegionPending`, `requestedCellX/Z`, and `activeCellX/Z`. `requestedRegionPending` means a requested cell exists and either no active GPU model is present, the active cell differs, or the active model is from an older source revision (`staleProxy`). `shutdown(device)` clears requested cell as well as active cell and runtime stats. No TWH8 serialization, shader, ECS or GPU resource layout changes.

**Caveat:** An intentionally empty Horizon model may leave `requestedRegionPending=true` because no GPU model exists; the flag denotes missing active GPU presentation, not necessarily an outstanding worker job. `preparing` remains the separate stream status. Coordinates are meaningful only if the matching Present flag is true; absent cells default to zero in the returned struct. An out-of-range observer throws before requested cell is changed.

**Actual verification:** GitHub source commit only; NO CMake, compiler, CTest, running editor/game, GPU screenshot or benchmark. Keep PR Draft and send tests to separate testing agent.

**Tester acceptance:** initial/no observer, active same cell, camera hysteresis, cross-cell streaming, teleport, GPU LRU hit, transition reversal, source revision invalidation, empty model, invalid camera and shutdown. Check that requested pending does not masquerade as `preparing>0`; verify cache hit restores active cell and no stale coordinate after shutdown.

**Next implementation:** refine pending semantics for intentionally empty regions with explicit `activeRegionResolved` if needed; update public TerraWeave docs/skill, then separate agent executes full tests and benchmarks.

## Stage 22h — resolved empty Horizon region status (SOURCE COMMITTED; TESTS NOT RUN)

**Branch:** `feature/terraweave`, PR #24 Draft.
**Commit:** `f7d9b14ed159ab891ad8cbb8fa060687b1b3a0ab`.
**Changed:** `engine/render/TerraWeaveHorizonScene.h`.

`HorizonRuntimeStats` now exposes `activeRegionResolved` and `activeRegionEmpty` alongside `activeRegionPresent`. `activeRegionResolved` is true when `activeCell_` is set, including a successfully published empty bake; `activeRegionEmpty` is true when that resolved cell has no GPU model. `staleProxy` now detects an outdated active cell revision even for resolved empty cells. `requestedRegionPending` is true only when the requested cell is unresolved, differs from active cell, or has an outdated source revision. Active cell coordinates are published for resolved empty cells as well. No extra GPU allocations, TWH8 archive changes, ECS changes or draw calls.

**Semantics:** `activeRegionPresent` still means a drawable GPU model; `activeRegionResolved` means accepted region result (possibly empty). `requestedRegionPending` is not synonymous with an executing worker; consult stream `preparing` separately. `shutdown` resets both cells, preventing a false resolved status. No new persistent state was introduced; the existing active cell/revision already tracks accepted empty bakes.

**Actual verification:** GitHub source commit success only. No CMake/compiler, CTest, editor/game launch, GPU screenshots or benchmarks. Keep PR Draft.

**Tester acceptance:** first update before worker completion, successful empty bake, repeated same empty region (no requeue), camera move to nonempty cell, source revision invalidation of empty cell, GPU LRU restore, transition reversal and shutdown. Verify pending=false for resolved empty same-revision cell, and pending=true for requested different/unresolved or stale cell.

**Next:** update public TERRAWEAVE.md and skill docs to explain diagnostics, then independent test agent performs CMake/CTest and runtime validation.


## Stage 22i — public documentation and agent skill synced to Horizon state (DOCS COMMITTED, TESTS NOT RUN)

**Branch / PR:** `feature/terraweave`, [Draft PR #24](https://github.com/chromealex/AIVERSE/pull/24). The branch already contains Stage 22a–h code, including resolved empty region handling. This stage changes **documentation only**; no new source or tests.

**Commits and changed files:**
- `f8da28202882c62370799d4cff73ac644add9fab` — `docs/terrain/TERRAWEAVE.md`: updated 2026-11-19 status and appended a current Stage 22 contract (neighbor-aware Q16 seam slopes, optional WorldScene/native callback, nullopt fallback order, active/requested cells/revisions, empty vs pending, source-stale proxy semantics, CPU sample provenance and TWH8 archive limitations). Includes host C++ inspection example and a table of each diagnostic field.
- `e36ada1ed4a385ec7fb9e4ab4ef427a58fe42ada` — `skills/world/aiverse-terraweave/SKILL.md`: marks TWH8 / Stage 22 as current, adds deterministic/genre-independent implementation recipe, strict flags-before-values and unknown-vs-zero provenance rules, safe GPU patch/lifecycle guidance, and a concrete separate-test-agent acceptance checklist.
- This handoff append records documentation synchronization after those two commits.

**Architecture/API summary:**
- Use `AuthoredWorldHeightSnapshot::continuousSlopeQ16(xQ16cm,zQ16cm,radiusCm=200,toleranceCm=1)` only for continuous resident authored surfaces; optional `HorizonSurfaceSources::nearGroundContinuousSlopeAtQ16Cm` uses the same immutable snapshot captured per async bake, with tile-local then area-weighted fallback for missing neighbor data.
- Query `TerraWeaveHorizonScene::runtimeStats()` for `sourceRevision`, `activeRevision`, `staleProxy`, `activeRegionResolved`, `activeRegionEmpty`, `activeRegionPresent`, `requestedRegionPending`, `requestedRegionPresent`, active/requested middle-tile X/Z, and near normal provenance fields. Check presence/resolved flags before interpreting cells. `preparing` is worker progress, NOT synonymous with requestedRegionPending.
- `nearNormalProvenanceAvailable=false` means unknown provenance (especially after TWH8 archive decode); counters are unique seam samples, whereas `matchedAuthoredNearNormals` counts render vertices. Current **TWH8** serialized format intentionally omits provenance.
- Existing shader/RHI/geometry/archive APIs are unchanged in this documentation-only stage.

**Actual verification:** Both GitHub documentation writes succeeded and their resulting commit hashes were returned by the connector. **NOT RUN:** CMake, C++ compiler, CTest, code lint, AI skill audit, editor/game launches, archive fixtures, CPU/GPU benchmarks, Metal/GL/Vulkan screenshots. No pass/fail assertions are made about these. PR remains Draft.

**Limitations:** Docs describe committed source behavior, not verified on-device guarantees; older archived TWH6/TWH7 passages in this large document are historical and current version is explicitly called out at Stage 22. Neighbor-aware fallback deliberately does not remove cliffs or height discontinuities. Diagnostic provenance is not persisted in the archive; a zero counter without availability is not an observed zero.

**Next agent action:** The independent testing agent should compile and execute `aiverse_terraweave_horizon_test` plus related terrain/forest/scene tests, run an archive TWH8 hit/miss fixture and revision/empty-region/GPU cache lifecycle tests, inspect real slope/near-far seams and captures on target backends, measure draw/triangle/overdraw budgets, then append actual command outputs and outcomes here. If a source bug is found, create a focused source commit with an updated handoff. Preserve universal (genre-independent) APIs, avoid unrelated FPS/RTS code, and do not merge PR #24 until verified.

## Stage 23a — CI log triage and Stage 22 regression tests (SOURCE/TESTS COMMITTED; NEW CI PENDING)

**Date:** 2026-11-19. **Branch:** `feature/terraweave`; PR #24 remains Draft.

### Observed CI evidence

The repository's `.github/workflows/terraweave.yml` registers an isolated Release CMake/Ninja build with GCC and Clang. It enables assertions with `-UNDEBUG`, builds nine TerraWeave test targets and runs CTest on the TerraWeave label/name pattern.

**Actually executed upstream (historical head only):** GitHub Actions run [44320349413](https://github.com/chromealex/AIVERSE/actions/runs/44320349413) completed with `failure` on earlier feature branch commit `73d338f5...`. Both GCC job `134857326655` and Clang job `134857327368` reported successful CMake configure but failed **Build TerraWeave tests** at `TerraWeaveHorizonScene.h:240`:
`prepared->bake.terrainResolution` attempts a member access on `std::shared_ptr<const HorizonBake>` instead of dereferencing `prepared->bake->terrainResolution`. As a result, **CTest was skipped**. These logs confirm a real compilation defect in older source, NOT a test pass on the current branch.

Direct cloning from `github.com` into the agent's container failed with `Could not resolve host: github.com`; no full repository checkout, native engine GPU or local CMake invocation was possible in this environment. GitHub connector read/write, Actions metadata and job log retrieval were available.

### Fixes and test source commits

- `0d5ab33ede678b6bf3c337534f854c86353ee29e`: `engine/render/TerraWeaveHorizonScene.h` — fix erroneous `prepared->bake.terrainResolution` member expression in the full GPU model publication path. No intended behavior change beyond repairing compilation.
- `40cf576a920089282b621dc1d8d26d4557ef4a71`: `engine/world/TerraWeaveWorldHeightSource.h` — explicitly include `<utility>` required by the new `std::pair<int32_t,int32_t>` slope API rather than relying on transitive declarations.
- `0fed1bbb4efaa52d127045c3e1faaa4b0be301e6`: `tests/terraweave_horizon_test.cpp` — add Stage 22 assertions for neighbor-aware Q16 slopes across continuous positive/negative world tiles, absent/discontinuous neighbors and invalid radius/tolerance; TWH8 archive provenance omission, initial no-region Horizon runtime flags. Existing isolated CI already builds and executes this target; no new target registration needed.
- This handoff document append records the stage.

### Validation status (do not conflate earlier and latest heads)

- **Observed failure (older source):** GCC/Clang compilation error in historical CI, logged above; CTest skipped.
- **Confirmed actions (current branch):** three GitHub source/test commits successful. New GitHub Actions run [44477600335](https://github.com/chromealex/AIVERSE/actions/runs/44477600335), for head `0fed1bbb...`, was observed with both GCC and Clang jobs **queued**, not completed.
- **Not performed:** local CMake/Ninja build, compiler invocation against the complete repository, passing or failing results from newly committed regression assertions, editor/game runtime, TWH8 cross-compiler fixture, Metal/GL/Vulkan screenshots, frame-time and overdraw benchmark.
- Therefore **NO current-HEAD build or CTest pass may be claimed**. PR remains Draft.

### Exact next steps for testing agent

1. Await or inspect the **current commit**'s GCC and Clang CI jobs; extract errors from both job logs and fix compiler issues before interpreting CTest.
2. Once builds succeed, verify all nine isolated TerraWeave targets and their CTest results. Record actual counts and full failing test cases here.
3. Add/execute GPU lifecycle tests for requested vs active region, `staleProxy`, resolved-empty region, CPU/GPU cache restores, revision changes and partial atlas/VB/full uploads. Existing Stage 22 test additions cover the **CPU snapshot and archive**, not a live GPU scene.
4. Capture real editor/game seams at continuous/discontinuous tile boundaries, horizons under camera teleport and near/middle/far forest transitions; measure draw calls, vertex/index counts and GPU time on each supported backend.
5. If tests reveal new source bugs, commit narrowly scoped fixes and append another stage with exact SHA, files, observed results and remaining limitations. Preserve TWH8 format and genre-independent APIs. **Do not merge PR #24 without actual verified tests.**

## Stage 23b — all-or-none seam provenance cleanup and CPU regression fixture (SOURCE + TESTS COMMITTED, CI QUEUED)

**Date:** 2026-11-19. **Branch:** `feature/terraweave`, PR #24 remains Draft.

**Context:** Stage 23a fixed the documented GCC/Clang compilation bug in `TerraWeaveHorizonScene.h` and added Stage 22 Q16/TWH8 regression assertions. The most recent actions jobs were still queued when this stage was inspected, so do not claim a successful build or test suite.

**Code commits and files:**
- `9928e50c1d64dbb6ad7709f891ac1ff8122e55e8` — `engine/render/TerraWeaveHorizonClip.h`: on an incomplete height sample or sample-budget overflow, clear both neighbor-aware and tile-local slope counters and set provenance availability false. When the triangle slope guard rejects a full height weld, clear both counters while preserving the explicit `nearWeldSlopeFallback` and CPU sampling-path availability. Pre-existing degenerate-triangle fallback still clears both counters. This corrects a source-level diagnostic inconsistency: no partial sampled provenance may be published when the seam table itself is discarded.
- `affb6309df976e06f127eccc7152a9973f7b91ae` — `tests/terraweave_horizon_test.cpp`: CPU-only clipped-quad fixture using a flat 450cm authored height and an interior near coverage rectangle. Asserts that valid neighbor-aware callbacks populate the unique seam sample counters, tile-local callback fallback is counted when the neighbor callback is omitted, a second-sample height failure discards partial counters and provenance, and an excessive weld slope triggers all-or-none fallback and clears applied sample counts. Existing test also retains Q16 signed/negative tile, TWH8 provenance and uninitialized scene assertions from Stage 23a. The test executable is already registered in `.github/workflows/terraweave.yml`, with GCC/Clang Release `-UNDEBUG`.

**Actual validation:** GitHub commits for source/test files succeeded. A targeted *static source re-read* confirmed the incomplete-sample reset and slope-guard reset code and the presence of regression test assertions. **No compiler, CMake, CTest or GPU execution** was performed by this agent. GitHub Actions [run 44478385684](https://github.com/chromealex/AIVERSE/actions/runs/44478385684) at test commit `affb6309...` and [run 44478303914](https://github.com/chromealex/AIVERSE/actions/runs/44478303914) at source commit `9928e50...` were observed **queued**, with no GCC/Clang conclusions. Earlier historical failure on `prepared->bake.terrainResolution` is already documented under Stage 23a and fixed at `0d5ab33e...`.

**Interpretation:** This stage improves the all-or-none diagnostic **implementation**, not proven rendering quality or measured frame time. Counters record unique CPU seam sample points, not draw/vertex count. An archived TWH8 region intentionally lacks provenance; false availability remains unknown. No TWH8 payload/fingerprint, GPU buffers, draw calls, ECS, terrain collision or shader API changed.

**Next validation agent steps:** Retrieve the first completed workflow for the **latest source HEAD** (not an older queued run), review both GCC/Clang `Build TerraWeave tests` jobs, then `CTest` for `terraweave_horizon` and all isolated TerraWeave tests; fix any compiler or failing assertion and append the exact failing command/output here. Independently test source edits, stale proxy, GPU LRU/empty-region publication, archive roundtrip and live rendering. If GitHub Actions remains queued, report that explicitly; do not mark PR ready or merge.

## Stage 23c — deduplicated TerraWeave CI scheduling (WORKFLOW COMMITTED; CI RESULTS UNCONFIRMED)

**Date:** 2026-11-19. **Branch:** `feature/terraweave`, Draft PR #24.
**Commit:** `0b06301a02883c4fb440a980df5928b7d8ffb3cc`.
**Changed:** `.github/workflows/terraweave.yml`.

**Reason:** Since each Stage 22–23 feature and handoff commit triggers the isolated TerraWeave workflow, GitHub Actions accumulated many queued runs for superseded branch commits. The latest Stage 23b source/test fixes have not produced a passing GCC/Clang build or CTest result yet.

**Implementation:**
- Add top-level `concurrency` group `terraweave-${{ github.workflow }}-${{ github.ref }}` and `cancel-in-progress: true`. This should cancel obsolete **future runs using this workflow configuration** for the same GitHub ref; no guarantee of retroactively canceling already-queued runs created with the old workflow.
- Remove the `docs/terrain/**` and `skills/world/aiverse-terraweave/**` paths from the **push** trigger. A handoff/docs-only commit no longer schedules a redundant C++ build. The existing `pull_request` paths (source/tests/CMake/workflow) and manual `workflow_dispatch` remain unchanged.
- Leave GCC and Clang Release builds, `-UNDEBUG` assertions, nine test build targets and CTest invocation unchanged.

**Actual verification:**
- GitHub source update succeeded with commit `0b06301a...`. The latest workflow run [44478627264](https://github.com/chromealex/AIVERSE/actions/runs/44478627264) for that commit was **queued** at observation, without GCC/Clang results.
- Older runs [44478385684](https://github.com/chromealex/AIVERSE/actions/runs/44478385684) for `affb6309...` and [44478455786](https://github.com/chromealex/AIVERSE/actions/runs/44478455786) for `a6a09fdc...` were also still **queued** at this observation.
- GitHub Actions was **not observed** to have canceled obsolete runs yet. No local repository CMake compile or CTest, GPU screenshots, or performance measurement was executed by this agent. Do not claim that CI has passed.

**Limitations and next step:** This is CI orchestration only, not a fix to TerraWeave rendering or algorithm output. Next test agent should inspect the newest completed workflow for the **current source HEAD**, record GCC/Clang `Build TerraWeave tests` and `Run TerraWeave CTest suite` step outcomes and log excerpts, and only then decide whether further code changes are needed. Verify that new pushes of this workflow actually supersede previous runs for the same ref. Keep PR #24 Draft until tests/visuals are verified.

**Post-check: PR mergeability.** GitHub REST `GET /repos/chromealex/AIVERSE/pulls/24` reports `mergeable=false` and `mergeable_state="dirty"` at branch head `8f8e2f93...` against base `b71ad7b6...`. In addition to the queued CI, **merge conflicts with the PR base are a separate unresolved blocker**. Do not merge or overwrite unrelated systems while tests remain unknown. Future integrating agent should compare to latest base, resolve conflicts with targeted changes and re-run isolated + cross-module CI; record actual resolution commits and tests. The CI concurrency workflow commit itself was not proven to cancel older queued runs retroactively.

## Stage 23d — PR #24 retarget to main and SpectraFX scope isolation (SOURCE SCOPE COMMITTED, BUILDS PENDING)

**Date:** 2026-11-19. **Active branch:** `feature/terraweave`. **Target branch:** `main`. **PR:** [#24](https://github.com/chromealex/AIVERSE/pull/24), still Draft. **Scope cleanup commit:** `d1bfdec6a5978963f2df4308a7e1345bc0f65f51`.

### Root cause, resolution and preservation

Previous handoff reported PR #24 as nonmergeable with `main`. That was inaccurate: the PR **base was actually `feature/universal-entity-inspector-fps`**, not `main`. That other branch had ~402 independent commits after the common main ancestor, predominantly SpectraFX; TerraWeave had diverged with its own work. The common `main` ancestor was `9ee6ff6fbf390bd45363bc92c838f492dfe92eee` at inspection, which was already an ancestor of TerraWeave. Therefore there was **nothing to merge from main into TerraWeave**.

- Retargeted existing PR #24 from `feature/universal-entity-inspector-fps` to `main` using GitHub's PR update. GitHub subsequently returned `mergeable=true` and `mergeable_state=clean` against main at the existing Terra head. No merge commit, forced branch update or change to main was needed.
- Created a preserved safety branch `backup/terraweave-pre-scope-cleanup-20261119` at `85f9cbf1cf4bdb8bcec0780165cbad250cb22d65` **before** any scope cleanup. VFX work also remains in the separate `feature/universal-entity-inspector-fps` branch. Never delete the backup without explicit coordination.
- `d1bfdec6...` used a normal fast-forward Git commit on `feature/terraweave` (lease against expected head) to restore 15 SpectraFX-only files to their main state (or remove files absent from main). Also removed the SpectraFX-only alias and three VFX test registrations from `CMakeLists.txt` while **preserving every TerraWeave target**; removed the introductory SpectraFX-only block from `AGENTS.md` while **preserving the universal handoff protocol and TerraWeave ownership rules**.
- Deliberately retained shared `Renderer3D`, `Model`, `GlobalGeometryPool`, editor, `WorldScene`, `AsyncChunkStream` and `ChunkStreamer` integrations used by TerraWeave. No module-specific VFX shader or tests remain in this PR's final file diff. Scope changed from **74 to 59** files. GitHub reported `mergeable=true` and `main` as PR base after cleanup.

### Actual verification and limitations

**Actually checked:** GitHub PR base/head before and after retarget; compare-commits showed `main` already ancestral to TerraWeave (`behind=0`), whereas the unrelated feature branch diverged; GitHub recursive tree SHA comparisons and PR per-file patches were inspected before selectively restoring VFX paths; backup branch creation, cleanup tree/commit and fast-forward ref update succeeded; GitHub PR file list confirmed 59 files and no SpectraFX-only files remaining; PR REST reported `mergeable=true` after cleanup.

**NOT RUN:** local CMake/Ninja/compiler, current-HEAD GitHub Actions GCC/Clang tests, CTest, live editor or game, RHI snapshots, GPU rendering screenshots or benchmarks. Existing TerraWeave CI was queued in preceding stage; no pass or failure can be inferred from GitHub's `mergeable=true`. PR REST also reported `mergeable_state=unstable` after cleanup, so not ready to merge.

**Risks:** Although final PR diff is TerraWeave-focused, its historical commits still contain VFX work later undone by the cleanup commit; recommend a **squash merge only after full validation**. Scoped VFX paths were restored from `main` so no other agent's work is merged accidentally. Shared renderer/WorldScene/AGENTS/CMake modifications still require build/integration verification. The generic skill catalog on this branch was restored to the version from main; its registration of `skills/world/aiverse-terraweave/SKILL.md` should be audited before merge (the earlier branch catalog also did not contain that registration).

### Exact next step

1. Check current clean PR head against latest `main`, confirm no new commits and a clean final diff.
2. Obtain complete GCC and Clang build + CTest logs for the **latest cleanup commit**; repair compiler/test regressions with a separate stage/commit and handoff. Do not use results from previous VFX-contaminated commits.
3. Run editor/native-WorldScene integration and actual GPU captures/benchmarks; audit catalog registration and stale skill references.
4. Keep Draft until all required results are real and recorded, then decide on a tested squash merge into `main`.

## Stage 23e — register TerraWeave AI skill after SpectraFX scope cleanup (CATALOG SOURCE COMMITTED; RUNTIME TESTS NOT RUN)

**Date:** 2026-11-19. **Branch:** `feature/terraweave`. **PR #24 target:** `main` (Draft).

**Code commits and changed files:**
- `689209b46235220a4b04591a8d1cc521fc6378a1`: `tools/skills/catalog.py`. Added `aiverse-terraweave` to `WORKFLOWS`, sourced from `docs/terrain/TERRAWEAVE.md`, and added mixed English/Russian search vocabulary in `WORKFLOW_ALIASES`. Did NOT restore any unrelated SpectraFX skills after the Stage 23d cleanup.
- `2e214b088082260f9a11fe4deee8e53946233acd`: `skills/catalog.json`. Added the matching `aiverse-terraweave` workflow/tool entry for the existing `skills/world/aiverse-terraweave/SKILL.md`, preserving alphabetical ID ordering and the generator's JSON formatting. Existing catalog contained 211 skills; new catalog has 212.
- This handoff documents the catalog stage.

**Why needed:** The Stage 23d VFX scope cleanup restored shared skill registry files to their main versions. The TerraWeave skill already existed on this branch but had **never been registered** in the old shared catalog, so `tools/skills/catalog.py check` would reject it as unregistered against `skills/world/*/SKILL.md`.

**Verification actually done:** GitHub source reads confirmed the Python recipe row and alias exist exactly once each, JSON entry exists exactly once, catalog IDs are sorted, `JSON.stringify(data,null,2)+"\\n"` reproduces the stored 2-space JSON representation, and `.github/workflows/terraweave.yml` still contains the nine isolated targets plus concurrency settings. Successful GitHub commits were returned for both files.

**NOT RUN:** Actual Python `tools/skills/catalog.py check`, `tools/skills/audit.py`, CMake/CTest on full repository, GCC/Clang builds, macOS/Windows builds, GPU/editor/game runtime. A full local checkout was not available because network `github.com` resolution is blocked in the agent container. No test suite may be marked passing.

**Next agent action:** Run `python3 tools/skills/catalog.py check` and `python3 tools/skills/audit.py` in the checked-out branch, then full isolated TerraWeave CI and CTest; verify no stale catalog and no SpectraFX references from TerraWeave-only PR. Check PR mergeability against latest `main` and test shared renderer + WorldScene/editor integration. Keep PR Draft pending real results and GPU visual checks.

## Stage 23f — CI queue observation and Horizon header portability (SOURCE COMMITTED, CI PENDING)

**Date:** 2026-11-19. **Branch:** `feature/terraweave`. **PR:** [#24](https://github.com/chromealex/AIVERSE/pull/24) → `main`, Draft.

**Source change:** `dc10baec05aae559a3988e8cc7569eb3e6deb468` updates `engine/world/TerraWeaveHorizon.h` to explicitly include `<utility>` for the optional `std::pair<int32_t,int32_t>` neighbor-aware slope callback. Previously the type depended on transitively included standard headers. This is C++20 header portability hardening only; no HLOD logic, GPU buffers, atlas/TWH8 archive format, shader, physics, ECS or data-layout change.

**CI evidence as actually observed:** For pre-change PR head `d7735490a467acaf64f3ad8b8f38f25e1d7de8ae`, six GitHub Actions `pull_request` workflows were present and `queued`, including TerraWeave isolated checks [44480005718](https://github.com/chromealex/AIVERSE/actions/runs/44480005718) and the main CI run [44480005759](https://github.com/chromealex/AIVERSE/actions/runs/44480005759). Earlier per-commit workflow runs [44479880544](https://github.com/chromealex/AIVERSE/actions/runs/44479880544) (TerraWeave) and [44479880571](https://github.com/chromealex/AIVERSE/actions/runs/44479880571) (CI) were observed `completed/cancelled` after newer PR commits. These cancellations are **not** passing tests. The latest post-change commit requires separate current-HEAD workflow evaluation. No GCC/Clang pass, CTest pass, editor GPU screenshot or benchmark has been observed.

**Other static review:** Confirmed head `TerraWeaveHorizonScene.h` includes the Stage 22 empty/resolved and stale-region reporting fields, and the Stage 23 tests include authored Q16 normal, TWH8 provenance and clipping fallback assertions. These are code inspection findings, not execution results. The top-of-file handoff base label has been corrected from the old feature branch to the current PR target `main`.

**Actual validation performed:** GitHub repository file reads, workflow metadata inspection and one successful source commit. **NOT RUN:** full C++20 compilation, CMake/Ninja, CTest, Python skill catalog check, editor/WorldScene scene, Vulkan/Metal/GL screenshots or profiling. PR must remain Draft.

**Exact next work:** Inspect the newest completed TerraWeave isolated workflow on **post-Stage23f** source HEAD; retrieve GCC and Clang job logs, fix real compiler failures or test assertions; then run full and targeted CI, `tools/skills/catalog.py check`, and RHI/editor visual QA. Keep this handoff updated after every stage. Do not claim source tests passed until explicit run conclusions and logs are available.

## Stage 23g — unified push/PR concurrency and current CI status (WORKFLOW COMMITTED; TESTS NOT RUN ON NEW HEAD)

**Date:** 2026-11-19. **Branch/PR:** `feature/terraweave` → `main`, Draft PR #24.
**Implementation commit:** `f7a5810b9d175f30b24be2b7f0457c7c6bd4beee`.
**Modified file:** `.github/workflows/terraweave.yml`.

**Reason and behavior:** Stage 23c's workflow concurrency used `github.ref`, so `push` to `feature/terraweave` and `pull_request` synchronization for PR #24 had different group identifiers (branch ref vs PR merge ref), allowing redundant TerraWeave builds for the same source tree. The new expression combines `github.workflow`, normalized source repo (`github.event.pull_request.head.repo.full_name || github.repository`) and source branch (`github.event.pull_request.head.ref || github.ref_name`). The existing `cancel-in-progress: true` then applies to both event types of the **same head branch**, while unrelated branches/repos retain independent groups. No GCC/Clang CMake flags, nine TerraWeave test targets, CTest expression, engine source, archive or GPU paths were changed.

**Observed CI evidence BEFORE Stage 23g:** Head `e48b6b4b6533da796b974d643bd17f07c15aa801` PR jobs for TerraWeave isolated checks [44480515679](https://github.com/chromealex/AIVERSE/actions/runs/44480515679), main CI [44480515591](https://github.com/chromealex/AIVERSE/actions/runs/44480515591) and additional integration workflows were **queued** at observation, with no passing GCC, Clang, CTest or GPU evidence. An older `pull_request` TerraWeave run [44480455342](https://github.com/chromealex/AIVERSE/actions/runs/44480455342) was `completed/cancelled`, while the equivalent `push` run [44480446860](https://github.com/chromealex/AIVERSE/actions/runs/44480446860) was still queued. This confirmed the cross-event duplication that the expression is designed to address.

**Actual validation:** GitHub file read and source update commit succeeded; inspection confirmed the old `github.ref`-only group was replaced. No CMake/compiler/CTest executed locally (full repo cannot be cloned from container due DNS resolution restriction). **Not yet verified** that the new group cancels duplicates, nor any passing GH Actions tests or GPU screenshots; do not claim otherwise. PR remains Draft.

**Next exact action:** Inspect the newest TerraWeave workflow run for the Stage 23g head or later; verify whether `push` and `pull_request` now deduplicate and capture both GCC/Clang build + CTest outcomes from the surviving run. If failed, retrieve job logs and fix actual errors with source commit + fresh handoff. Then validate full CI, native editor/WorldScene and GPU visual quality before merging.

## Stage 23h — populated TWH8 seam archive regression coverage (TEST SOURCE COMMITTED; CI QUEUED)

**Date:** 2026-11-19. **Working branch:** `feature/terraweave`. **PR:** [#24](https://github.com/chromealex/AIVERSE/pull/24) -> `main`, still Draft. **Test commit:** `7f55b9a70c06db7382a80414f6b3b69624d0f5a0`.

**Changed file:** `tests/terraweave_horizon_test.cpp`. Extended the existing deterministic CPU interior clipped-quad fixture to serialize a **populated**, valid Q16 near seam via `encodeHorizonArchive(continuousSeam,fingerprint)` and read it back with `decodeHorizonArchive`. The new assertions compare every stored near-seam record's Q16 X/Z, exact near height, `nearNormalValid` and signed X/Z slopes. They also verify that **nonserialized** provenance availability/counters are unknown/zero on archive reload, that a corrupted payload byte is rejected by the archive checksum, and that a different expected fingerprint is rejected. The existing steep-weld fallback fixture now verifies successful TWH8 serialization/deserialization of `nearWeldSlopeFallback=true` with an empty seam table. This adds test coverage only; no runtime/archive/layout changes.

**Actually observed:** GitHub write completed for the test file. CI [run 44481763123](https://github.com/chromealex/AIVERSE/actions/runs/44481763123), the first PR TerraWeave run on source commit `7f55b9a...`, was **queued** for BOTH `terraweave (gcc)` and `terraweave (clang)` when inspected; equivalent push run `44481758254` was `completed/cancelled`, consistent with Stage 23g normalized workflow concurrency. The broader `CI` workflow on that commit was `pending` at inspection. Earlier head `20caef17...` CI had `in_progress` status but job summaries subsequently showed some canceled jobs; no usable CTest or GPU pass obtained from it. The previously confirmed historical GCC/Clang compiler error `prepared->bake.terrainResolution` belongs to older source and was already fixed in Stage 23a.

**Validation status:** Static source review and successful GitHub commit only. **NOT RUN / NO PASS CLAIM:** full C++ compilation, actual execution of new assert statements, CTest, cross-platform archive fixtures, editor/native GPU execution, screenshots, GPU frame time/draw calls. A complete AIVERSE checkout remains inaccessible in the local container (GitHub DNS failure). Current PR remains Draft. CI queued runs must NOT be reported as passing.

**Known limitations:** New tests use synthetic, flat authored terrain, not actual GPU/WorldScene residency. Checksum rejection covers one bit corruption only, not a fuzz corpus. Exact near-seam samples and normal blend output still require renderer/on-device validation. The `HorizonBake` provenance counters remain intentionally excluded from the TWH8 format.

**Next precise step:** Inspect first **completed** TerraWeave isolated run for the **current** post-handoff source revision, fetch GCC and Clang job logs and fix any compiler/test failures. Only if both build and CTest pass should the tester proceed to independent native editor and GPU visual regression, capturing exact platform/device metrics. Update this handoff with run IDs, command results and failures without changing unrelated engine systems.

## Stage 24 — deterministic sparse forest tree regrowth (FUNCTIONAL SOURCE COMMITTED; TESTS DEFERRED)

**Date:** 2026-11-19. **Branch:** `feature/terraweave`; **target:** `main`; PR #24 remains Draft. **User priority:** implement features first; a different agent will run tests later. Do not spend this phase repeatedly checking queued GitHub Actions.

**Implementation commits and changed paths:**
- `ec9a6a1f7d43c50125b29fda2764dece64f53001`: `engine/world/TerraWeaveForestScene.h` introduces sparse `ForestRegrowthSchedule`, deterministic caller-supplied `setPresentationTick`, single-key/bulk regrowth scheduling, cancellation and sorted save snapshots, lifecycle-only per-instance scale interpolation in `visibleBatches()`, and trailing `ForestSceneStats::scheduledRegrowth/presentationTick`. Reuses previously existing stable `ForestTreeIdentity`, `removed_` set, prepared foliage patches and `buildFloraBatches`.
- `769d0a5eca329e99de816a485f46a07166354c5b`: `engine/world/TerraWeaveForestScene.h` fixes `restoreTree` so both hidden and regrowth states are cleared even when the schedule existed, avoiding short-circuit side effects.
- `9fcd5de84db154fb2b42628b4f56fad7f9df0cd6`: `engine/world/WorldScene.h` exposes an optional const/nonconst `forestScene()` pointer to native C++ hosts, preserving null behavior for scenes without `terraweaveForest` opt-in.
- `7d297188858e1c6af29ba89bae99694994644e14`: `docs/terrain/TERRAWEAVE.md` documents API/units/save examples and lifecycle ownership.
- `4e2bf2fd7639ab0ce2ef97ca145ec04be8f0e27f`: `skills/world/aiverse-terraweave/SKILL.md` documents usage contract for other agents. This handoff is a separate follow-up documentation commit.

**Exact semantics and invariants:**
- `ForestRegrowthSchedule{ForestTreeIdentity tree, uint64_t regrowAtTick, uint64_t matureAtTick, uint32_t initialScalePermille=150}`. A scheduled identity is hidden before its start tick; for each supplied tick in [regrowAtTick,matureAtTick), `visibleBatches()` evaluates a deterministic integer interpolation factor, applied to the *copied* `FoliageInstance.scalePermille` before existing draw-list grouping. At or after mature tick, factor=1000 (full generator scale). Equal start/mature ticks imply immediate full-scale appearance.
- Regrowth state is sparse and persists in `std::map<ForestTreeIdentity,ForestRegrowthSchedule>` across streaming/revisions, max one million records. Invalid species/scale/backward schedule or maturity interval > 1e12 ticks is rejected. Max interval avoids unsigned 64-bit multiplication overflow; no floating wall clock or platform-specific 128-bit type is involved. `setRegrowthSchedules` validates bulk replacement before committing it and rejects duplicate keys.
- `removeTree` explicitly cancels a matching regrowth schedule; `restoreTree` clears both states; bulk removed-tree replacement erases any conflicting regrowth records; a new scheduled regrowth removes explicit removal. Caller owns gameplay authority, harvest resource changes, navmesh/collision and saving `removedTrees` and `regrowthSchedules`. State setters and tick updates run on owning update thread, **not** worker threads.
- No cosmetic tree ECS entity, new shader, atlas/material layout, additional GPU draw passes or tree-model regeneration. Async foliage patch snapshots do not depend on the presentation tick, so changing growth does not cancel workers or rebuild already loaded patches. `WorldScene::forestScene()` is an optional null-checked generic host integration; explicit forest header include is needed by calling native projects.

**Actual verification performed:** GitHub source/doc/skill writes returned the five commit SHAs above. Source was inspected for existing stable removal APIs, async snapshot ownership, `visibleBatches` and native scene adapter before making changes. **NOT RUN:** CMake, compiler, CTest, local render, editor/game launch, GPU screenshots, performance measurements, replay/serialization fixtures or cross-platform validation. This is deliberate per user feature-first request; no pass result is asserted. PR stays Draft.

**Limitations:** Far Horizon HLOD cluster silhouettes are statistical and presently do not reflect a single individually harvested tree. Regrowth is presentation only; a gameplay layer must retain authoritative collision/resources, replication/save. Per-render-frame `visibleBatches` performs a map lookup per visible instance; profile in the test phase before claiming zero CPU cost. Sorted saved schedules are exposed as POD records, but no automatic WorldScene save format or replication protocol was added.

**Concrete next feature:** preserve harvested **area** vegetation masks in the far Horizon canopy and near Flora using shared immutable patches so a game can clear an entire forest area coherently across LODs; keep individual tree regrowth sparse. A later seasonal/wind adapter can share the same host time and PHYSIS-driven weather input without genre dependencies. Testing agent should later verify deterministic growth at ticks 0/start/mid/end, invalid and duplicate schedules, remove/restore precedence, negative world chunks, async streaming and cleanup, native WorldScene opt-in/null API, renderer maxScale, and GPU LOD continuity, then record actual commands/logs separately.

## Stage 25 — cross-LOD runtime forest clearing rectangles (FEATURE COMMITTED; TESTS DEFERRED)

**Date:** 2026-11-19. **Branch:** `feature/terraweave`; PR #24 → `main` (Draft). **User instruction:** focus on features now; build/CTest/GPU testing will be handled later by a different agent.

**Source code commits:**
- `72d55a552d3ad4a23c60909d67139ef08ecd7150` — `engine/world/WorldScene.h`: added public `setVegetationClearings(std::vector<terraweave::RectCm>)` and const `vegetationClearings()`; retained the current and pre-update clearing rectangles plus one dirty flag in WorldScene. Header explicitly imports `engine/world/TerraWeave.h` for the reusable world-centimetre rectangle type. No genre-specific include.
- `c5fb660093501f0330629e95055a275cfde0bd37` — `engine/world/WorldScene.cpp`: setter enforces <=4096 positive-area half-open rectangles within +/-1e9cm, canonical geometric sorting, exact deduplication and no-op on unchanged set. The next WorldScene update rebuilds the existing immutable `FoliageExclusionMask` with resident collision footprints PLUS clearing rectangles, publishes it to near `TerraWeaveForestScene::setLandscapeSources` and far `HorizonSurfaceSources::allowsCanopyAt`. Both previous and new clearing rectangles are passed in the edited list to `TerraWeaveHorizonScene::setSurfaceSources`, invalidating old and restored canopy tiles. Multiple setter calls prior to update preserve only the first previous and latest new versions; jobs are canceled through existing revision-token lifecycle. No extra GPU buffers, materials, shader permutations, engine terrain geometry or per-tree ECS entities.
- `f145ece0b515851d712684dd6a64d0d956771a36` — `docs/terrain/TERRAWEAVE.md` documents clearings, save/restore API, units, shared near/far publication, limitations.
- `9f671b085681e1918fd121ba0089cf7e1bd29f9e` — `skills/world/aiverse-terraweave/SKILL.md` teaches AI agents the same cross-LOD ownership and no-genre rules.

**Behavior:** while Stage 24 sparse tree regrowth schedules affect per-tree `visibleBatches()`, Stage 25 rectangular forest area clearings affect both near scatter placement AND Horizon's far silhouette samples. This is the native project-facing mechanism for fields harvested as a group, construction clearings and restoration. Hosts own authoritative game state, saves, replication, collision/navmesh. The caller should capture and restore the sorted `vegetationClearings()` list; the scene has no new binary save format. Large edited arrays may cause bounded full Horizon rebakes instead of incremental re-use.

**Actually verified:** source interfaces of `FoliageExclusionMask`, `TerraWeaveForestScene`, `HorizonSurfaceSources`, `WorldScene` were inspected via GitHub; GitHub reported successful source/doc/skill commit SHA values above. No native C++ compile, CMake/CTest, editor game screenshot, GPU benchmark or on-device verification was executed. This is **intentional**, per feature-first request. **Do not assert passing tests or seamless transitions.** PR stays Draft.

**Limitations:** individual stable-tree harvest in Stage 24 is near-only; far Horizon is statistical, but rectangular clearings now affect it. Some far cluster cards may remain allocated when their entire canopy is masked. WorldScene rebuilds immutable resident terrain snapshots when only vegetation clearings change; this can be optimized separately after correctness. No polygon masks / wind / seasonal environmental coupling yet.

**Next functional step:** add a shared genre-independent seasonal growth/dormancy adapter and canopy density/color policy for near Flora/Horizon, or optimize fully masked Horizon clusters, still keeping PHYSIS/water ownership independent and all render effects optional. The later testing agent must verify reverting clearings, negative coordinates, unchanged-set no-op, asynchronous source updates, shader atlas/HLOD parity, and the 4096 input budget before release.

## Stage 26 — bounded deterministic resident-tree queries (FEATURE SOURCE COMMITTED; TESTS DEFERRED)

**Date:** 2026-11-19. **Branch:** `feature/terraweave`; **PR:** #24 (Draft, target `main`). User explicitly prioritized **features first, tests afterward**.

**Code and documentation commits:**
- `460213ea232cb2cc346a277a21e6e1da3ca3eab8`: `engine/world/TerraWeaveForestScene.h`. Adds `nearestTreeAt(xCm,zCm,radiusCm=500)` (optional instance) and `treesInRect(RectCm,maxResults=1024)` (sorted bounded instances) for generic native harvesting/selection. Queries inspect only existing bounded CPU resident patch cache, regardless of camera frustum or LOD. Uses a coarse per-patch rectangle precheck and exact circle or half-open rectangle checks, excludes removed/hidden regrowth and exposes the current grow-scale on returned copies.
- `460213ea...` also factors the Stage 24 regrowth/removal visibility logic into a shared private `presentationInstance` helper used by both `visibleBatches()` and these new queries, so interactions and rendering agree without duplicating lifecycle math. Added explicit `<optional>` include.
- `3f95caa75a28cb8e6efa12b1c6f926e9fa3ad53b`: `docs/terrain/TERRAWEAVE.md` describes query units, stable IDs, limits, native examples and residency-only scope.
- `cb2fef632821043f9def950225de6f73b7207aae`: `skills/world/aiverse-terraweave/SKILL.md` adds native-agent usage and feature-first handoff rules.

**Semantics:** Nearest query radius 1..10,000,000cm with bounded squared-distance arithmetic. At equal distance choose lowest stable `ForestTreeIdentity{speciesId,key}`. Query returns no tree if forest is not streamed, if an instance is harvested/removed, or if it is not yet visible due to a scheduled regrowth. Rectangle query bounds must be nonempty and inside +/-1e9cm and caller result cap 1..100,000; exceeding cap throws instead of returning nondeterministic truncated contents. Stable output order: species ID, key, X, Z. This does not create ECS entities, upload any GPU models or touch collision/navigation.

**Actual verification:** GitHub source and documentation writes returned commit hashes; source was re-read for method placement, the shared `visibleBatches` helper and signature consistency. **NOT RUN:** C++ compiler, CMake, CTest, live native game/editor, GPU metrics or platform CI. This is deliberate per features-first instruction. Do not claim CI passing; leave PR Draft.

**Limitations / next feature:** Queries access only loaded CPU patches (not remote/unloaded trees). A per-chunk spatial acceleration structure may be warranted after performance profiling for large maxResidentChunks, but is not required for correctness. Next functional stage: species-specific canopy phenology/wind response from PHYSIS via optional genre-independent environmental callbacks, shared with Horizon, preserving stable scene seeds and bounded snapshots. Defer GPU/CTest until user requests validation.

## Stage 27 — seasonal canopy phenology for near Flora and Horizon (FEATURE SOURCE COMMITTED, TESTS DEFERRED)

**Date:** 2026-11-19. **Branch:** `feature/terraweave`, PR #24 Draft, base `main`. **Owner priority:** complete functionality first; do not run CMake/CTest/GPU validation during this phase.

**Code commits / paths:**
- `b1e09ace9e10a15c2008ffffab7c00e8b28cac74`: NEW `engine/world/TerraWeavePhenology.h` defines pure `CanopyPhenology` with broadleaf/evergreen canopy cover (0..1000), RGB leaf tint multipliers (0..2000), exact value equality, bounds validation and deterministic per-channel integer tint helper. Default identity policy.
- `563d0517a885e140690ddbd407a63720eb5ed1f2` and `b5e5209c8948d90bc9710bd75606216c5550876f`: `engine/render/TerraWeaveForestAdapter.h` updates **shared per-species** near foliage material variants and per-azimuth impostor overrides for season changes; uses conifer profiles for evergreen coverage, preserves existing model/atlas assets, includes <algorithm>. Borrowed material vectors must remain stable between draw submission/render completion.
- `b0dad68bec7d546a6a563350665d9f1c4053db91`: `engine/world/TerraWeaveForestScene.h` adds `setPhenology` and `phenology()` without invalidating resident CPU patch scatter or pending workers.
- `a17fc99bba6447c695e8a7b36b6f534d916566cf`: `engine/world/TerraWeaveHorizon.h` appends `HorizonSurfaceSources::phenology`, validates it and deterministically thins sampled silhouette crowns by broadleaf/needle policy, and tints CPU atlas leaf pixels. Does not change terrain mesh, shader, GPU model topology, near clipping or samples.
- `84df0a75eb84aeec4aa9cffba06c990d812f4dd8`: `engine/render/TerraWeaveHorizonScene.h` prevents reading/writing static TWH8 disk atlases for nondefault phenology, because the current archive/fingerprint has no season field. Default case retains existing archive reuse.
- `e38c5d5ec6dfda9711c16e9545e686e298da602a`: `engine/world/WorldScene.h` exposes native `setVegetationPhenology` and `vegetationPhenology()`, state and dirty flag.
- `26de150117b5e941a07c68e242102e866076eec2`: `engine/world/WorldScene.cpp` validates/sets value, immediately adjusts near shared species materials without re-scattering, and republishes an immutable Horizon source on next scene update. Season-only changes leave near forest async sources intact. Unchanged value short circuits.
- `21cf2cad2f84ee46c2e4a0790ec8cc930e83fdd4`: `docs/terrain/TERRAWEAVE.md` updated with public API, cache behavior and limitations.
- `e4df9a45c11002f8facb57620bab75015434c364`: `skills/world/aiverse-terraweave/SKILL.md` updated for next AI agents. This handoff closes the stage.

**Contracts:** PHYSIS/calendar/gameplay owns authoritative environment timing and chooses POD values; TerraWeave does not link to PHYSIS, FPS, RTS or any game module. Near Arboreal branches keep bark opaque; leaf materials use shared tinted/opacity variants. Single-quad impostors include bark + foliage and therefore fade the entire billboard when cover<1000 (known visual limitation). Horizon silhouette atlas changes via existing bounded CPU worker and GPU replacement/patch; no extra model, atlas or draw pass. Live season changes can briefly show the old distant region while a replacement uploads. Static TWH8 archive is bypassed for nondefault seasons.

**Actual verification:** GitHub commit/write results and targeted read of existing material, Horizon sample and WorldScene architecture were observed. **NOT RUN:** CMake, C++ compiler, unit tests, CTest, real editor, GPU captures, frame timings, cross-platform validation or archive fixture. PR remains Draft. Do not infer runtime correctness from source commits.

**Known risks:** Nondefault partial-alpha leaf materials may increase blended GPU overdraw; impostor bark fades; biome-based far broadleaf/conifer classification approximates near prototypes; per-season transition is a stepped policy update, not time-blended dual atlases. Worker re-bakes may be costly if host updates canopy every simulation tick; caller should quantize changes or apply them at seasonal milestones until a future seasonal update scheduler is provided.

**Next feature (Stage 28):** Add an optional, bounded wind presentation state expressed in integer horizontal wind components and a deterministic simulation tick. Use existing Arbor transforms for a small per-tree lean/sway (no per-leaf ECS or extra shader) while maintaining world-origin rebasing and stable tree identities. Keep PHYSIS as an optional upstream data provider; document that it is a whole-tree cosmetic approximation, not vertex deformation. Do not run tests until a separate testing phase.

## Stage 28 — deterministic PHYSIS-neutral wind presentation (FEATURE SOURCE COMMITTED; TESTS DEFERRED)

**Date:** 2026-11-19. **Branch:** `feature/terraweave`, PR #24 Draft, base `main`. **User priority:** deliver remaining functionality before independent compilation, CTest or GPU tests.

**Code commits and files:**
- `6b1ea43a675f08f5479c6b683e8f8b9c908824ab` plus guard follow-up `6584c02416456cc4610a3d42862eae29fa8c9990`: new `engine/world/TerraWeaveWind.h` defines POD `ForestWindState` (signed horizontal world cm/sec, gust modulation, cycle ticks, max lean milli-degrees), bounded validation and integer `forestTreeLean(state,tick,stableKey)` triangle-wave phase. Extreme inputs are rejected before squaring, avoiding int64 overflow on INT32_MIN component pairs. No PHYSIS include.
- `596563d797c186190bcdece17bc67661c8baaa9b`: `engine/render/TerraWeaveForestAdapter.h` adds `setWind`, `wind()`, `setPresentationTick` and per-tree root-tilt render transforms. Lean is keyed by stable per-tree hash+species and caller tick, applied in WORLD orientation before tree yaw and scale. Zero wind preserves previous transform path; geometry, atlas, model count and draw-pass structure unchanged.
- `474e25e2e139986f3bb1a053c6ed464a507c1567`: `engine/world/TerraWeaveForestScene.h` forwards wind and presentation tick to the render adapter, using the previously exposed deterministic tree presentation time.
- `38340310cf6a4ed5b984dbbf11e6b908b680029a` and `d83cf44990b6564f8c38d32b7d55c576e0a48568`: `engine/world/WorldScene.h/.cpp` expose `setVegetationWind`, `vegetationWind()`, `setVegetationPresentationTick`, `vegetationPresentationTick()`. The native host feeds wind velocity and deterministic time; no leaf rescatters or Horizon worker invalidation occur for mere wind/tick changes.
- `779cb1930a687d001f569cc2f9846077465b53a5`: `docs/terrain/TERRAWEAVE.md` usage, unit rules, limitations and PHYSIS bridge.
- `68956710b552cc745653f2ff2df32de68607b83d`: `skills/world/aiverse-terraweave/SKILL.md` agent instructions.
- This handoff append completes the stage.

**API/invariants:** `ForestWindState{xCmPerSec,zCmPerSec,gustPermille=350,cycleTicks=120,maxLeanMilliDegrees=1500}`. Vector magnitude <=5000cm/s, gust 0..1000, cycle 8..4096 ticks, max lean 0..3000 millidegrees; `validateForestWind` rejects invalid states. `forestTreeLean` uses a stable hashed phase and bounded signed integer triangle wave, yielding X/Z lean in milli-degrees. `ArborForestAdapter` converts to cosmetic `render::mat4` rotations before draw submission. Trees/billboards share models, collision remains unaffected and existing world origin rebasing is preserved. PHYSIS may call the WorldScene setters from the owning host; TerraWeave has no compile/link dependency on PHYSIS, FPS/RTS, tick scheduler or a graphics backend beyond existing Arbor presentation.

**Actual verification:** Successful GitHub source/doc/skill writes and targeted API/source reads only. **NOT RUN:** compiler, CMake, CTest, editor/game execution, render frame, platform CI, graphics screenshots, draw-call measurements or performance benchmarks. As requested by user, tests will follow feature completion and are delegated to a separate agent. Do not claim compiled/visual correctness. Keep PR Draft.

**Known limitations:** Wind is a lean of the entire near tree/impostor around its base, not physically credible branch deformation; far 1–6km HLOD canopy remains static to avoid per-frame atlas rebakes. The float render matrix can vary slightly across platforms, and is never authoritative game state. High winds can expose visual LOD mismatch; motion and overhead need GPU measurements. The helper is intended for already validated wind state from `setWind`; direct native calls should validate their inputs.

**Concrete next functional step:** improve seasonal near impostors so leaf alpha does not fade the trunk (separate bark and foliage channels with bounded atlas/memory/draw budgets), or add sparse aggregated wind state for far Horizon with no per-tree geometry. Any new stage MUST end with a fresh handoff. Only after finishing requested features should a dedicated tester run compile/CTest and PHYSIS-to-TerraWeave integration fixtures.

## Stage 29 — single-texture bark/foliage impostor separation (FEATURE SOURCE COMMITTED; TESTS DEFERRED)

**Date:** 2026-11-19. **Branch:** `feature/terraweave`. **PR:** #24 to `main`, Draft. **Owner priority:** implement functions first; tests/visual QA later by a different agent. No tests intentionally launched here.

**Changes, exact commit SHAs and files:**
- `1baab68ebda77c9aae21a0e7b76e62cb2c69464f` and offset follow-up `4913bad412a533aa6cbca82a0d8aa79e5df3c22b`: `engine/render/TerraWeaveImpostorAtlas.h` now creates two disjoint horizontal page sets for each multi-azimuth tree atlas: bark-only first, foliage-only second, same shared RGBA8 texture/model. The 1-mesh/1-node output now has bark and leaf cutout primitives, 8 vertices, 12 indices, two material slots and two per-azimuth UV overrides; leaf quad sits +2cm in model camera-facing Z. Max input combination `2 * width * views <= 8192`. Identical TreeSkeleton across views and leaf/bark layers.
- `c6c6bea92ef899a62621ec4c1b474c3f0c416f22`: `engine/render/TerraWeaveForestAdapter.h` seasons foliage material index 1 only, leaves bark index 0 unchanged, and sets existing `ModelDraw::primitiveMaterialFilter=0` at 0 canopy coverage to draw only bark on both near tree and impostor models. Full-canopy default material overrides remain compatible with `viewMaterials[view]` as a vector of two rather than one.
- `677e9528c9ba956ac7e4b2e3e6801033819ad134`: `docs/terrain/TERRAWEAVE.md` describes output layout, costs, seasonal semantics and migration.
- `3eb62a4f44a955c9280915cce108fbdf1e2e33a0`: `skills/world/aiverse-terraweave/SKILL.md` documents renderer contract and required later test updates. This separate handoff append records the feature stage.

**API/architecture:** `makeArborImpostorAtlas` still returns `ArborImpostorAtlas{unique_ptr<Model>,viewMaterials,views}`; model material count changes from one to two and `viewMaterials[azimuth]` from 1 to 2. `ModelDraw::materials` pointer is borrowed for the renderer frame; lifetime remains owned by ArborForestAdapter. Atlas pixel width doubles relative to previous default; only one texture object remains. `makeArborBillboardModel` standalone single-azimuth utility is deliberately unchanged. Generic ECS, physics, PHYSIS input and TWH8 distant-Horizon archive formats untouched.

**Actual verification:** GitHub source re-read of multi-view atlas generator, legacy Billboard model, ArborForestAdapter and relevant test expectations, plus successful GitHub code/docs/skill writes. **NOT RUN:** compiler, CMake, CTest, model test, editor/Game runtime, screenshots, backend GPU measurements or performance benchmarks. The PR remains Draft.

**Known risks:** The previously committed `tests/terraweave_model_test.cpp` asserts old 4 vertices, 6 indices, 128x8 atlas and one material per view; it is **known stale** and will fail until the later testing agent updates expected atlas dimensions, mask-page contents, two primitives and both per-view UVs. Do not claim passing CI. Adding a bark+foliage material/primitive can increase draw calls and raw atlas RGBA memory (~2x); verify physically on Metal/GL/Vulkan. Near alpha-blended leaves are an approximation to shedding, but winter 0% correctly suppresses foliage draw while keeping bark. Far HLOD is separate and still statistically built. Source constructors must be examined for material batch metadata after refinalization.

**Next concrete feature:** add more organic **per-branch** wind deformation for near Arbor without modifying authoritative skeleton or collision, while retaining the Stage 28 low-cost whole-tree root-lean fallback and keeping far HLOD static. Do not begin tests until user changes priorities. Record any new stage in a separate handoff entry.

## Stage 30 — bounded hierarchical Arbor branch wind presentation (FEATURE SOURCE COMMITTED; TESTS DEFERRED)

**Date:** 2026-11-19. **Branch:** `feature/terraweave`. **Target:** `main`. **PR:** #24 Draft. **User priority:** complete feature implementation before builds and tests.

**Implementation and documentation commits:**
- `332e7577926c9161d31f103d6551c9501e2cda01`: `engine/world/TerraWeaveWind.h` appends `ForestWindState::branchFlexPermille=600`, validates [0,1000], 0=root-only behavior; preserves other fields and the deterministic integer wind phase API.
- `caf44be36a3ba9cd319d5bc7234785be9bec10a3`: `engine/render/TerraWeaveForestAdapter.h` adds per-species cached TreeSkeleton branch pivot coordinates; a bounded deque of per-visible-tree `std::vector<render::mat4>` node poses; parent-child cumulative model-space joint rotations about stable branch starts with per-branch hashed wind phase. `ModelDraw::nodeWorld` is populated for near High/Medium/Low tiers when nonzero wind and branchFlex>0. Existing root lean remains in `draw.world`; impostor stays root-only. Tiers scale additional branch sway by 100%, 65%, 25%. If CPU matrix budget exhausted, the remaining instances revert to root sway rather than allocating unbounded pose memory.
- `39f93f3a324e5f9579ce2d9bedec31a02c533fc2`: `engine/world/TerraWeaveForestScene.h` forwards native `setBranchPoseBudgetBytes(bytes)` and `branchPoseBudgetBytes()` (0..8MiB, default 4MiB) to Arbor adapter; no GPU state changes.
- `174b60822e1ecc5d0fcefb268dff63a66bd09088`: `engine/render/TerraWeaveForestAdapter.h` tapers branch sway by near Flora LOD tier so branch motion decreases approaching impostors.
- `8aac800d5ec8165ceacbfa3f20606dec9d0bf954`: `docs/terrain/TERRAWEAVE.md` API, pose lifetime, cost, limitations.
- `1786d8686cf7d491fd76bacd9c4e9d0300e0857c`: `skills/world/aiverse-terraweave/SKILL.md` feature and future QA instructions.
- This entry is the separate handoff commit for Stage 30.

**Architecture:** No change to TreeSkeleton authoritative procedural data, no ECS entities per branch, new shader, GPU buffer, skinning pass, TWH8 archive changes, or new draw API. Existing Arbor BranchLOD model uses one rigid render node per branch; their baked mesh vertices are in original model tree coordinates. The wind adapter composes each child node's model-space transform from its parent's pose, then rotates about its original branch-start pivot. Renderer3D::drawModel synchronously reads `ModelDraw::nodeWorld` and copies poses into render items. Borrowed nodeWorld references remain stable in a deque until the next `forEachDraw` call; callers storing ModelDraw objects longer than that must not dereference those pointers. The mutable pose arena is single-thread, render-owner usage only.

**Actually verified:** Source reads of Arbor Model, Renderer3D drawModel, TreeSkeleton parent hierarchy and math matrices via GitHub, followed by confirmed successful GitHub source/docs/skill commits. **NOT RUN:** CMake, C++ compiler, CTest, animations/scene playback, Metal/GL/Vulkan GPU screenshots, performance measurement, native PHYSIS adapter execution. This is intentional under the feature-first rule; do not claim test success. PR remains Draft.

**Limitations:** The wind response is a hierarchy of rigid node transforms rather than continuous bending of a skinned leaf/branch mesh. Branch geometry may open gaps at joints under higher angles. Native draw collectors need to use `nodeWorld` pointers before a subsequent forEachDraw; no thread-safe arena is implemented. 4MiB CPU pose budget introduces a deterministic per-submit fallback to root-only at high tree counts. Extra per-node transforms may degrade culling/batching CPU time, which must be profiled later. Far Horizon still uses static coarse atlas.

**Next feature/QA:** If additional features remain, add a quality-tier branch pose mode or opt-in blended sway rather than increasing per-tree memory without bound. Future independent test agent must update `terraweave_model_test.cpp` for Stage 29's two-layer impostor geometry (8 vertices, 12 indices, width 2x), then compile GCC/Clang and run CTest. Check Stage 30 pose pivot and parent inheritance, LOD fade, root-only wind, pose memory limits, renderer borrowed-vector lifetime, seasonal bark/foliage layering and GPU benchmarks before allowing PR #24 to exit Draft.

## Stage 31 — shared shader GPU crown wind and Horizon HLOD consistency (FEATURE SOURCE COMMITTED; COMPILATION / TESTS DEFERRED)

**Date:** 2026-11-19. **Branch:** `feature/terraweave`, **PR:** [#24](https://github.com/chromealex/AIVERSE/pull/24) to `main`, still Draft. **User priority:** finish functionality first; later independent C++/CTest/GPU verification, without claiming CI passes.

**Exact implementation commits, paths and contract:**

- `4ac85ba7fa02838c82b3d35124db8b51d57be9d8` — NEW `engine/render/TerraWeaveVertexWind.h`: `canopyWindGraph()` returns one shared immutable first-party `render::graph::Program`, containing existing shader graph `vertexOffset` GLSL and MSL code and passthrough fragment color; bounded root-weighted, spatially varied sine flutter. `canopyWindVertexParameters(ForestWindState,tick,yaw,strength)` converts world wind into local X/Z metre-scale offsets, tick-based phase and gust. Declares `kCanopyGpuWindMaxOffsetMetres=0.65f`. The phase is from game tick rather than arbitrary shader time, with no per-instance shader compilation.
- `17a99c0a89f3c9013c01f89a5b8a19500b617dcb` — `engine/render/Renderer3D.h` appends GENERIC `ModelDraw::useVertexParameters`, `vertexParameters` (float4) and `maxVertexOffsetMetres` fields. Existing aggregate initialization remains valid.
- `fe6c11869c78cf45365968a52d45506d4cb65b2b` — `engine/render/Renderer3DModels.cpp`: copies optional shader vertex parameters into existing U0 `Inst.anim` vec4 only on eligible rigid/non-baked/non-lightmapped graph materials, AFTER `bakedItem` reserved its own slot. No U0 block layout change. Adds finite positive vertex offset envelope support for model/node frustum, LOD and GPU persistent-scene recorded bounds. Unknown/unbounded custom graph displacement retains its prior conservative no-cull fallback. Model static-cache reuse is disabled for draws using per-instance vertex parameters.
- `655290140504f95db917b702abe600111b5d6ffc` — `engine/render/TerraWeaveModelAdapter.h`: BranchLOD foliage vertex alpha encodes normalized height weight 0..255, while material vertexColors=false prevents mask/shading changes. Original bark vertices stay unchanged.
- `c8b164ab14badbea6618ea00aa8b364b1be8904e` and `2e9ef9e08490e046956947a97c3195c2d0c8451c` — `engine/render/TerraWeaveForestAdapter.h`: use shared Shader Graph only on material slot 1 of near branch models and Stage 29 two-material impostors when wind magnitude, max lean and branch flex are nonzero. Seasonal + wind material variants use existing per-species/per-view shared vectors. Per-draw `ModelDraw::vertexParameters` carry local yaw-corrected wind, caller tick, gust, attenuated High/Med/Low/Impostor strengths of 100/75/50/35%. Bark-only winter rendering still specifies the offset bound for safe culling even though leaf graph geometry is filtered away. No new leaf model, GPU buffer, texture or pass.
- `37059d440ed72e61c621598b25aaacceff7200e8` — `engine/render/TerraWeaveImpostorAtlas.h`: encode 0 weight on the bottom and 255 weight on top of Stage 29's separate foliage quad; leaf fragment ignores vertex alpha, atlas opacity unchanged.
- `8761fcbddfa90d92e6046d491d48d1e1f34e2b28` — `engine/render/TerraWeaveHorizonModel.h`: far HLOD forest-card vertex alpha weight 0 at bottom, 255 at top, while forest material vertexColors=false preserves its alpha-cutout texture. Terrain material and near seam geometry unaffected; no new draw primitives.
- `d34f37983d29db14ba0001d2ec9d5780ad6399be` — `engine/render/TerraWeaveHorizonScene.h`: `setWind(ForestWindState)`, `setPresentationTick(uint64_t)`, getters; `submit` applies shared Shader Graph to forest material slot 1 on active and crossfade models, using a conservative 30% per-instance GPU lean. Terrain slot 0 is inert. Material graph changes do not invalidate source revisions, create new region bakes or update atlas pixels.
- `8ddcd9fb70bcd5961ab957fed2c3942abbce2be8` — `engine/world/WorldScene.cpp`: existing generic host `setVegetationWind`/`setVegetationPresentationTick` now feed BOTH optional `TerraWeaveForestScene` and `TerraWeaveHorizonScene`. No direct PHYSIS/FPS/RTS dependencies.
- `26be07288a59262dd2fae3e14c45b81a7d114339` — `docs/terrain/TERRAWEAVE.md` public API/assumptions; `695a767e64d6a37e2aff8eac159e4cfb1f10a49e` — `skills/world/aiverse-terraweave/SKILL.md` agent rules. This handoff append closes Stage 31.

**Architecture and compatibility:** Material Shader Graph's existing GLSL/MSL `// SG_VERTEX` injection supports the engine-authored `vertexOffset`; default no-wind behaviour retains original matrices and unmodified default model material paths. No TWH8 archive migration, renderer UBO layout change, per-tree ECS entity, new shader backend or per-frame Horizon CPU bake. Stage 30 CPU BranchLOD poses remain independently optional and can be disabled by setting the branch pose memory budget to zero; Stage 31's GPU leaf flutter remains bounded and enabled by nonzero branch-flex. `ModelDraw` per-instance shader parameter channel is generic and may be used by unrelated rigid ShaderGraph material effects when it does not conflict with animation/lightmap U0 data. GPU graph may still cause pipeline permutation warm-up on the first enabled draw, NOT on every tick.

**Static source verification actually performed:** GitHub source reads confirmed direct GLSL/MSL graph snippets and vertexOffset output, generic renderer model/instance hook, bounded frustum branches, Arbor and impostor foliage weights, Horizon 0/255 card weights, and WorldScene near/far forwarding. All source and documentation writes above returned GitHub commit SHAs. **NOT RUN:** local CMake/compiler, CTest, Python skill audit, shader compilation, Metal/Vulkan/OpenGL drawing, visual screenshot, CPU/GPU benchmark, PHYSIS game-host integration. No correctness/performance claim is supported by a passing test. PR remains Draft.

**Known risks:** New renderer vertexParameters overlay uses U0 `Inst.anim`, so cannot coexist on an instance with baked clips or lightmap UVs (the renderer takes precedence for those). Ensure only rigid material graph users opt in. GPU sine/cosine is visual and not bitwise cross-backend deterministic. Bounding envelope 0.65 metres is conservative for the current validated wind scale but dynamic custom graphs MUST provide honest independent displacement bounds. Near and far use different attenuation/tier weights and statistical HLOD cards, so the goal is coherent time/direction, not identical branch silhouettes. Material graph updates and borrowed material pointers must occur outside in-progress Renderer3D submissions. CPU rigid branch poses already consume a bounded arena; GPU wind has separate shader ALU cost. No test evidence for culling/shadows/LOD transitions yet.

**Next feature / later testing:** Finish remaining requested vegetation graphics features only if needed. When owner authorizes the separate QA phase, **first** update Stage 29 outdated `tests/terraweave_model_test.cpp` (8 vertex, 12 index, 2 primitives/2 materials, doubled impostor width), then compile GCC/Clang and run all isolated CTest. Add tests for Stage 31 vertex weight encoding, maximum displacement envelope, default zero-wind parity, native PHYSIS tick synchronization, per-LOD attenuation, shader graph GLSL/MSL compilation, both active/crossfade Horizon model references, Metal/Vulkan/OpenGL GPU screenshot and draw/overdraw cost. Record exact completed CI run URLs and error logs in a fresh handoff; do not merge PR #24 before verified.

## Stage 31a — late prototype registration after GPU wind enabled (SOURCE FIX COMMITTED, TESTS NOT RUN)

**Date:** 2026-11-19. **Branch:** `feature/terraweave`, Draft PR #24 → `main`.

**Code SHA:** `66f134e6c1165a032d94c9ec02cbf8bee2423436`.
**File:** `engine/render/TerraWeaveForestAdapter.h`. During a post-Stage-31 static source review, identified a valid ordering issue: a native host may call `ArborForestAdapter::setWind` BEFORE registering prototypes with `addPrototype`. The previously existing `addPrototype` built seasonal material overrides only for a nondefault phenology state, but a default-season / active GPU wind state also needs those immutable override vectors. The draw path would otherwise look up missing `seasonalNearMaterials_` or `seasonalBillboardMaterials_`. Fixed the registration path to build overrides whenever `phenology_!=CanopyPhenology{}` **OR** `gpuCrownWindActive()`, preserving the default no-wind fast path and avoiding any per-instance shader recompilation.

**Actually checked:** exact GitHub source read and successful corrective commit. C++ compiler, CMake, CTest, GPU shaders, runtime and screen captures **NOT RUN**, in accordance with the user's feature-first priority. Do not mark the branch as tested or release-ready.

**Remaining integration risks:** The Stage 31 generic U0 Inst.anim vertex parameter reuse must avoid baked-skin/lightmap conflicts. The existing Stage 29 test still assumes the old one-material impostor; update at the later testing phase. Validate late-prototype registration, wind enable/disable, zero-leaf season filtering, GPU shader GLSL/MSL compilation, root-to-crown weights, Horizon active/crossfade, and max-bound culling on real backends. PR remains Draft pending independently recorded results.

## Stage 32 — bounded spatial wind grid and continuous near-to-Horizon handover (FEATURE SOURCE COMMITTED; TESTS DEFERRED)

**Date:** 2026-11-19. **Branch:** `feature/terraweave`; **PR:** [#24](https://github.com/chromealex/AIVERSE/pull/24), Draft, base `main`. **Owner instruction:** features before tests; do not launch CMake, CTest, backend GPU or full CI debugging as part of this stage.

**Implementation commits and modified paths:**

- `ec5f57489053221cc389a340d9ffbd9b4eb4125d` — NEW `engine/world/TerraWeaveWindField.h`. Introduces `ForestWindSample{xCmPerSec,zCmPerSec,gustPermille}`, `ForestWindField{originXcm,originZcm,spacingCm,columns,rows,samples}`, `validate()`, `anyWind()`, `sampleForestWindField(field,x,z,fallback)`, `blendForestWind(local,macro,macroPermille)`. A bounded 2..64 by 2..64 node lattice (max 4096 nodes), spacing 100..10,000,000cm, all nodes inside +/-1e9cm; each node respects existing `validateForestWind` magnitude/gust. Integer Q16 bilinear interpolation includes negative world coordinates; half-cell interior feather blends field samples continuously into the uniform fallback at the grid border. Outside the published field use fallback exactly. Changing the field does not alter canonical terrain/physics data.
- `d00b32d6ccffcbbd0273b27bad13a027deaf3c33` — `engine/render/TerraWeaveForestAdapter.h`. Adds immutable `setWindField(shared_ptr<const ForestWindField>)` / `windField()`, detects whether a field enables the already-shared GPU crown ShaderGraph variant, samples a spatial field once per resident tree, and uses one consistent effective wind for Stage 28 whole-tree root lean, Stage 30 bounded branch poses, and Stage 31 GPU crown shader parameters. Farward near-to-Horizon wind transition blends each tree's local wind into the observer's macro sample between configured medium LOD and cull radius, without new per-tree ECS, per-frame atlas, draw or GPU resource. `setWindTransitionRange(start,end)` configures a bounded signed-cm continuous range. Without a wind field, existing uniform behavior uses the previous fast path.
- `4043d79fee589898f2cd1d949b4823f3953ec565` — `engine/world/TerraWeaveForestScene.h`. Exposes `setWindField`/`windField` directly for native projects and initializes the near-to-far wind blend range from validated `FloraView.mediumRangeCm/cullRangeCm` (with safe nonempty range even when both thresholds are equal).
- `13b03be3e7ebf4f513802d4f6c366406a77d9c2b` — `engine/render/TerraWeaveHorizonScene.h`. Exposes `setWindField`/`windField`; each `submit` samples the field at the most recent observer X/Z **once** for a consistent macro wind shared by the current and outgoing HLOD regions. Wind changes do NOT cause a Horizon source revision, CPU silhouette bake, TWH8 disk cache miss or atlas RHI upload. Horizon remains a statistical/batched forest: this single macro wind is a deliberate bounded approximation, not separate local PHYSIS state for every far card.
- `09acd151aa4d999e491b83415067e51dca353336` and `eeac1fd44cf925a5e1f17ce163f3c53e103ffed4` — `engine/world/WorldScene.h/.cpp`. Adds owner-thread `setVegetationWindField(shared_ptr<const ForestWindField>)` and `vegetationWindField()` (nullable), validates before publishing and forwards the **same** immutable snapshot to optional near Flora and Horizon systems, independent of FPS/RTS/PHYSIS modules. Setters are a no-op if the same shared-pointer object is submitted again.
- `dbdbe0d22ac68862496a5110583b1075a43d8d7b` — `docs/terrain/TERRAWEAVE.md` documents API, units, publisher/fallback model, bilinear/feather transition, LOD blend, cost, limits and known far macro approximation.
- `ec8d7e10ad9407c38a30233bfd45da94b9897c7d` — `skills/world/aiverse-terraweave/SKILL.md` documents ownership/agent guidance. This handoff append closes the stage.

**Native usage:** build a `std::shared_ptr<ForestWindField>` with signed absolute world-centimetre lattice node coordinates and X/Z wind speeds in cm/s; fill `samples` in row-major X-fastest order; call `validate()`, publish as `std::shared_ptr<const ForestWindField>` using `worldScene.setVegetationWindField(snapshot)`. Leave existing `worldScene.setVegetationWind(fallback)` / `setVegetationPresentationTick(tick)` active. PHYSIS or custom game weather remains an **upstream source only** and never becomes a TerraWeave link dependency. Rebuild an entirely new immutable snapshot when PHYSIS data changes, and set null to return to uniform fallback. No direct renderer worker callbacks should reference mutable field data.

**Architecture and edge semantics:** The field's stored node values are bounded and validated. At the lattice boundary the feather gives exactly the supplied uniform fallback, so adjacent unloaded/no-data regions do not generate speed discontinuities. Local near-tree interpolation is performed in CPU integer arithmetic once per draw; shader vertex flutter still uses float GLSL/MSL and is not cross-backend bitwise deterministic. At the far end of `FloraView`, each tree's effective wind approaches the observer macro sample used by Horizon to limit snapping on LOD handover. The observer macro sample is shared across old and new HLOD region models during crossfade. This is **not** exact per-cluster distant wind, which would require a bounded extra GPU grid or vertex-buffer update architecture. No change to `ForestWindState` serialization, ForestRegrowthSchedule, near/far terrain height, PHYSIS, ECS, TWH8 archive version or existing mesh/atlas layouts.

**Actual checks:** GitHub file fetches inspected current Wind, Arbor adapter, ForestScene, HorizonScene, WorldScene, GPU ShaderGraph and previous handoff. GitHub source/doc/skill commit writes succeeded and returned the exact SHAs above. **NOT RUN:** CMake, compiler, CTest, Python skill catalog checks, GPU/Metal/GL/Vulkan rendering, PHYSIS scene execution, screenshots, memory/CPU/GPU profiling, negative boundary fixtures. Keep PR Draft. Do not describe new code as compiled/passed.

**Known risks:** Native field callers must publish immutable snapshots, not mutate through an alias while the render thread samples. `sampleForestWindField` trusts validated dimensions/samples; the public setters call `validate()`. On dense forest draws the CPU field lookup adds bounded interpolation work per tree. HLOD still uses one macro wind across the entire statistical region; only the near forest samples exact local field positions. The current Stage 29 model tests require migration to two-layer bark/leaf impostors, and Stage 31 GPU wind shaders have not yet been compiled for backends.

**Next functional step:** If feature scope continues, add a *bounded far-Horizon spatial variation* mechanism (prefer GPU world-space low-frequency regional modulation from the macro vector, or a small sampled grid packed into an existing vertex attribute only after considering buffer update and atlas budgets). Do **not** produce per-tree far ECS entities or rebuild the silhouette atlas every tick. A future separate QA agent should validate signed/negative grid interpolation, half-cell feather edges, invalid dimensions/magnitudes, snapshot replacement and zero-field behavior, near-to-far LOD macro continuity, old/new region crossfades and CPU/GPU costs, then document actual CI results and shader pipeline logs.

## Stage 33 — independent smooth spatial wind across distant Horizon forest clusters (FUNCTIONAL SOURCE COMMITTED; COMPILATION AND TESTS DEFERRED)

**Date:** 2026-11-19. **Working branch:** `feature/terraweave`, **PR #24** → `main`, still Draft. **User priority:** deliver features first and leave GCC/Clang/CMake/CTest/GPU QA to a separate testing stage.

**Commits, changed files and behavior:**
- `e6fa78447de9279050642787b9d8cf0c348237a6` and portability follow-up `a55530595887e18f49ce8caab96392bcd9d0fe46` — `engine/render/TerraWeaveVertexWind.h`. Adds `horizonSpatialWindGraph()`: a single cached engine-authored Material ShaderGraph program for GLSL+MSL `vertexOffset`, based on the existing near/Stage31 graph, with spatial per-cluster direction (±30°), amplitude (0.75..1.25), phase, and per-vertex root/canopy weight. Adds `horizonSpatialWindVertexColor(worldXcm,worldZcm,seed,top)` encoding three smooth Q16 noise channels sampled over a **2.4 km** world-space period into RGB8; A=0 at card root or 255 at canopy top. Added explicit `<utility>` for portable std::move usage. Both crossed cards of the same cluster receive the same metadata; adjacent clusters interpolate smoothly because the function samples world-space continuous value noise, not a camera-anchored random ID.
- `6336518666a2699786d60362c28712ab5f25cd6f` — `engine/render/TerraWeaveHorizonModel.h`. Uses the helper to bake cluster-local RGB8 attributes into all existing far forest-card vertices, leaving terrain vertex colours/geometry/indices untouched. Uses a fixed `0x544552524157494eULL` noise seed so streaming across different camera regions cannot change cluster appearance; default no-wind program ignores the metadata. There is NO additional per-card buffer, uniform or atlas/texture. `Horizon` forest material already has `vertexColors=false`, hence RGB are auxiliary wind data, not visible tint.
- `7441b4bc6e5cbde21ea6b62ba6beaacd5c6f0a42` — `engine/render/TerraWeaveHorizonScene.h`. In active + outgoing region submission, selects `horizonSpatialWindGraph()` for nonzero macro wind by default; optional `setSpatialWindVariation(bool)` / `spatialWindVariation()` switches back to Stage31's old uniform macro `canopyWindGraph()`. Zero-wind still selects no shader graph. Shader graph selection reuses existing shared cached Program, vertex-data layout and U0 wind params; DOES NOT trigger a Horizon source revision/worker/atlas update or per-tick renderer shader recompile.
- `159d35a1cd76a34e6ea41cd05783743bc9d6232e` and `340f750cf8fda57b5c375d87d0ddae0b898f16a6` — `engine/world/WorldScene.h/.cpp`. Public `setVegetationHorizonSpatialWind(bool)` and `vegetationHorizonSpatialWind()` (default true) forward to optional HorizonScene. No genre, PHYSIS, weather solver, ECS or physics dependencies.
- `a9bd48a9b12161b13d65ee754589a70a7c97937f` — `docs/terrain/TERRAWEAVE.md` public API, format, actual limits and distinction from exact PHYSIS per-cluster fields.
- `7d4150efec1dce128492989f4ce5f5b0ae2e750c` — `skills/world/aiverse-terraweave/SKILL.md` added agent rules for cluster attributes, shader selection, unchanged archive/atlas and accurate quality scope. This handoff is the final documentation commit for Stage 33.

**Architecture, ownership and native API:** The existing PHYSIS-neutral `WorldScene::setVegetationWind(ForestWindState)`, `setVegetationWindField(shared_ptr<const ForestWindField>)`, and `setVegetationPresentationTick(tick)` still supply macro wind sampled at the observer (Stage32). This new effect adds **procedural local direction, speed and phase subvariation** for distant clusters, not a direct query of PHYSIS velocity for each cluster. Near Flora still receives exact near-tree samples and blends toward the macro vector before cull distance. Horizon active/outgoing models use identical world-anchored noise functions, preserving the wind pattern when an observer crosses HLOD region cells. Shader stays one GPU material and one existing forest primitive per region; heightfield/near terrain cutout, Q16 welding, archive fingerprint/TWH8, LOD geometry and atlas pixels unchanged. Runtime switch controls shader variant, not model data; the spatial attributes are always embedded so no buffer re-upload is needed when toggling. The default is ON, zero wind behaves like Stage31.

**Numeric bounds:** The only extra intensity factor is 0.75..1.25 of the already bounded `canopyWindVertexParameters(wind,tick,0,0.30)`, and the existing `ModelDraw::maxVertexOffsetMetres=0.65` envelope remains conservative for the current 50m/s wind input, bounded lean and gust. Rotation is capped ±30 degrees; root weight is zero at base. Smooth variation uses signed world cm, never render-rebased camera origin, for stable positive and negative coordinates. GPU float sine/cosine remain cosmetic and not cross-device bitwise deterministic.

**Actual verification performed:** Inspected current GitHub source for `makeHorizonModel` forest-card vertex generation, `HorizonSurfaceSources` and `TerraWeaveHorizonScene` active+transition submission, the Stage31 GLSL/MSL graph and Stage32 macro wind. Successful source/docs/skill GitHub writes returned the commit SHAs above. **NOT RUN:** actual CMake/C++ build, unit tests/CTest, shader program compilation, live Metal/GL/Vulkan renderer, crossfade screenshots, frustum tests, GPU draw/perf measurements, PHYSIS runtime bridge. Do NOT claim the feature compiles, that it runs, or that GPU batch counts are unchanged in practice. PR stays Draft.

**Limitations:** Far variation is PROCEDURAL substructure over a single macro wind vector; it cannot portray an exact local PHYSIS front or opposing gusts at two distant clusters unless a future field texture or bounded vertex metadata update system is added. The 2.4km per-cluster noise changes slowly across adjacent cards but does not model tree-by-tree turbulence. The shared shader includes per-vertex sin/cos and can have a cold pipeline warm-up cost. Seed is fixed per world position rather than tied to terrainSeed, so world instances at the same coordinates have the same subvariation pattern; this avoids region seam mismatches but may merit a world-seed setting later.

**Next functional step:** Decide whether actual distant field fidelity is worth an optional **small GPU wind-grid resource or partial forest-card colour VB patch when PHYSIS snapshots change**, preserving one combined model and bounded update count. If yes, implement immutable patch updates only when field revision changes, not each simulation tick; keep static noise as a fast default. Separately, the later QA agent must migrate Stage29 old impostor tests and verify GLSL+Metal Stage31/33 ShaderGraph compilation, direction/intensity bounds, stable world-seeded attributes across negative coordinates and HLOD region transitions, fog/shadows/material blending, far/near visual coherence and GPU frame time. Preserve feature-first instruction unless user explicitly changes it.

## Stage 34 — exact distant PHYSIS wind via bounded GPU forest-card colour patches (SOURCE COMMITTED; TESTS INTENTIONALLY DEFERRED)

**Date:** 2026-11-19. **Branch:** `feature/terraweave`; **base:** `main`; **PR:** [#24](https://github.com/chromealex/AIVERSE/pull/24) Draft. **Owner instruction:** implement all functional features before running builds/CTest/GPU tests. No test suite was run during this stage.

### Implementation commits and files
- `e3b11ec1397607f779e8a5fe8ea39362e9f40b87` + signed-zero quantization fix `6b1a08899c5d755e182fa7d526d115986218021a`: **new** `engine/render/TerraWeaveHorizonWindPatch.h`. Defines `HorizonForestWindOverlay`, `HorizonForestWindPatchResult`, `horizonFieldWindVertexColor()`, `captureHorizonForestWindOverlay()`, `applyHorizonForestWindOverlay()`. One exact PHYSIS wind sample is computed at each statistical cluster's signed world-centimetre centre using the Stage32 immutable `ForestWindField` and uniform fallback. RG8 encode signed X/Z cm/sec, B8 encode gust 0..1000 permille; alpha retains 0/255 root-to-canopy weight. Quantization uses [-127,+127]+128, so 0 cm/sec maps to byte 128 exactly, with roughly 40cm/sec resolution over +/-5000cm/sec. Both crossed cards get identical colour metadata. A compact CPU copy of ONLY the existing contiguous forest-card vertex range is captured before `render::uploadModel()` frees CPU arrays; model validation enforces the expected 8 verts per cluster and a **2MiB maximum per-region shadow**. The upload helper preflights valid unpooled VB/IB and `gfx::Device::canUpdateSharedBuffers()`, computes a complete replacement, and performs **one all-or-none contiguous `updateBuffer`** if quantized values changed and the render-owner budget permits. An unchanged overlay advances its revision with **zero GPU writes**; an unsafe or over-budget update retains the old model+revision and returns not-ready. No extra GPU buffer allocation, atlas write or terrain update.
- `59da76e201abb3b22004c7281f46226e158dfb07`: `engine/render/TerraWeaveVertexWind.h`. Adds a single cached engine-authored `horizonExactFieldWindGraph()` (GLSL and Metal ShaderGraph `vertexOffset`) that reconstructs local velocity from RGB signed channels, uses B as local gust, A as crown bend weight, and `I.anim` U0 as shared max strength/tick phase. `horizonExactFieldVertexParameters(policy,tick)` reuses Stage31's bounded PHYSIS-neutral intensity; no new shader source per tick, uniform layout, RHI backend, ECS module or per-cluster GPU object.
- `fb058af1710ebd7922046ecb2b27d8944e8563a5` and aggregate-order follow-up `a280da59b9915f9eec2fb25c9ab55ea3e076e5aa`: `engine/render/TerraWeaveHorizonGpuCache.h` appends `HorizonGpuRegion::forestWind` (trailing so older positional aggregate users keep their field order). The compact CPU shadow and applied wind revision now follow active and outgoing GPU region models into/out of the existing bounded LRU cache; dormant regions are not rewritten until reactivation.
- `049eb1b6bd2a5221b294e57e6951efed59224fae`, `55c78000dfb30f30ab3b4cdcc2f1b06bf895108d`, and transition-retire ordering improvement `6460f825c9c3d31fbdb59e5a8ebb021001771ea8`: `engine/render/TerraWeaveHorizonScene.h`. Adds `setExactSpatialWindField(bool)`/`exactSpatialWindField()` (default true). Tracks a **separate, presentation-only** `windFieldRevision_` advanced on immutable field pointer changes, exact-mode toggle and fallback wind changes while a field exists. Captures new forest-only CPU vertex shadow before initial full model upload, preserves/moves it through `retireActive` and `adoptCached`, clears it at shutdown. The render-owner `update(device)` calls `refreshForestWindGpu()` AFTER finishing expired transitions and BEFORE its early-return paths; attempts the active region first and outgoing transition second under one **2MiB total RHI upload budget per update**. A newly published/cached model with stale metadata continues to render with the existing safe macro graph until its own full region update is applied, then switches to exact-field graph. With no field or exact mode disabled, the bounded patch restores Stage33 procedural metadata before re-enabling procedural per-cluster graph. This keeps zero-wind, initial default, PHYSIS fallback and LRU reactivation safe. Adds `HorizonRuntimeStats::windOverlayPatchWrites/Bytes`, `windOverlayPending`, `exactWindFieldActive`. Existing terrain-source revisions, TWH8 archive fingerprint, atlases, vertex/index topology, near-seam welding and worker scheduling are untouched.
- `91a56b56fde5585e1d4bd4d05fb3357d8980b0a4`, `3883983620d69b073a1d39e5778ef94b0a60f466`: `engine/world/WorldScene.h/.cpp`. Adds public `setVegetationHorizonExactWindField(bool)` and `vegetationHorizonExactWindField()` forwarding to optional Horizon. PHYSIS/game code continues publishing a single immutable `ForestWindField` to both near and far modules using Stage32's native API.
- `74ef1ec716017c705a931b59af32199dbaa86c07`: `docs/terrain/TERRAWEAVE.md` details sample/quantization format, source-revision separation, lifecycle, budgets, fallback, native use, limitations.
- `0fe46d4f6c1a140509647e06e573201e59f4dfb2`: `skills/world/aiverse-terraweave/SKILL.md` teaches next agent exact field mode, buffer ownership, migration/QA requirements. This handoff is the separate last stage commit.

### Architecture/API and semantics
`ForestWindField` stays a bounded PHYSIS-neutral immutable world-centimetre lattice, with at most 4096 host-supplied nodes. Stage34 samples it via existing Q16 bilinear interpolation at every **distant HLOD cluster centre**. This is now a real geographically varying field sample, not Stage33's procedural variation on the observer macro. GPU vertex RGB8 is necessarily **quantized** (~40 cm/sec), not lossless PHYSIS force. The new first-party ShaderGraph uses existing GLSL/MSL vertexOffset hooks, one shared forest material, the existing U0 packed rigid `Inst.anim` and original atlas/mesh. No new GPU vertex/index buffer, compute pipeline, per-tree entity, vertex attribute, per-vertex field texture or renderer backend. The GPU update edits the original VB's forest suffix only, with maximum 2MiB bytes/update across active and crossfade models; upload only when a field snapshot actually changes and the quantized target differs. A frame with no safe RHI update shows the previous macro wind rather than risking an inconsistent exact-field graph. Cached regions retain their forest CPU shadow and can be restored without regenerating Horizon terrain or canopy silhouettes. Clear the wind field or switch `setVegetationHorizonExactWindField(false)` to restore Stage33 procedural metadata through the same bounded path.

The optional `setVegetationHorizonSpatialWind(false)` remains a choice about **procedural** Stage33 variation; Stage34 exact mode, when field provided and enabled, takes precedence. Field snapshots are owned by the host and MUST remain immutable until the last render/worker reference releases them. WorldScene setters operate on the scene owner thread. Rebased render coordinates do not enter field lookup: each cluster uses its original signed world-centimetre centre. Renderer GPU shader sine remains cosmetic, not bitwise deterministic physics.

### Actual checks and explicitly unrun tests
**Actually done:** Read existing `TerraWeaveHorizonModel`, `TerraWeaveHorizonScene`, `HorizonGpuRegion`, partial VB upload, GPU RHI `updateBuffer` and Stage31/33 graph code through GitHub; verified the new source files and changed APIs exist on the feature branch; successfully committed the source/docs/skill files listed above. No merge to main.

**NOT RUN:** CMake configure/build, GCC/Clang, CTest, Python catalog/audit, actual Metal/GL/Vulkan shader compilation, PHYSIS runtime field publication, device-loss recovery, GPU upload/partial patch, live Horizon transition, screenshots, frame-timing or GPU/CPU memory measurements. **Do not mark Stage34 as compiled, tested, visually verified or performance-qualified.** The PR remains Draft.

### Risks, limits and next step
- Full forest-card suffix is one bounded contiguous `updateBuffer` write; sparse remote wind edits currently update all forest-card vertices for that visible region if any quantized colour differs. This is <=2MiB, but must be profiled on bandwidth-limited platforms. The CPU shadow is up to 2MiB per active/cached GPU model, in addition to the optional existing terrain cache; GPU cache budget counts **GPU** bytes, not this CPU shadow.
- `updateBuffer` has no per-write success return. Backend device loss requires higher-level recovery. Update postponement depends on `canUpdateSharedBuffers()`; exact mode does not activate until its entire region's colour stream reaches the desired revision. Cached model adoption may show one frame of macro fallback before the next update refresh.
- Per-cluster sampling is exact in *location*, but values are quantized to RG8 and gust to B8; no single-leaf turbulence or wind pressure effect on physics. CPU Q16 sample remains deterministic on validated inputs, GLSL/MSL trigonometry does not.
- Existing Stage29 `tests/terraweave_model_test.cpp` assertions for 4 vertices/6 indices/one material/doubled atlas remain stale; user explicitly requested tests only later. No test code was modified in this stage.
- **Next feature if authorized:** optional sparse contiguous dirty-run VB patching for weather changes confined to only a few clusters (max 8 ranges), with conservative single full-suffix fallback under low fragmentation; or a bounded host quality option to cap exact-field precision/CPU shadows per region. Do not create a separate GPU weather-field texture without demonstrating its actual benefit. When owner ends feature-first phase, a separate tester should update old impostor tests; compile and run CTest; verify signed centre sampling, zero velocity encoding, macro/exact/procedural switches, one GPU write per new field revision, same-colour zero writes, RHI unsafe postponement, GPU cache/crossfade, negative world coordinates, shader compilation and perf on Metal/Vulkan/GL. Record the *actual* resulting CI run IDs and outcomes in a new handoff before merging.

## Stage 35 — sparse dirty-run Horizon PHYSIS wind GPU vertex updates (FEATURE CODE COMMITTED; TESTS INTENTIONALLY DEFERRED)

**Date:** 2026-11-19. **Working branch:** `feature/terraweave`; **PR:** [#24](https://github.com/chromealex/AIVERSE/pull/24), base `main`, Draft. **Owner instruction:** implement features first; a separate agent will later own compilation/CTest/GPU validation. No live C++/GPU tests were run during this stage.

### Actual implementation, commits and changed files
- `2450371d8fa8cafbea6889429147ab7c5fe3ccc3` — `engine/render/TerraWeaveHorizonWindPatch.h`: replaced the Stage34 one-full-forest-suffix update with the pure C++ `planHorizonForestWindUpload(oldVertices,newVertices,maxRanges=8,fullFallbackPercent=70)`. Defines `HorizonForestWindUploadRange { firstVertex, vertexCount }` (relative to forest range, always 8-vertex-aligned) and `HorizonForestWindUploadPlan { ranges,changedClusters,updatedBytes,fullRangeFallback }`. Compares **only** `ModelVertex::color` changes from per-cluster PHYSIS samples, groups adjacent changed eight-vertex clusters into contiguous upload runs. If at most 8 disjoint runs and fewer than 70% clusters changed, selects sparse ranges. Otherwise consolidates into **one full-suffix fallback**. Planner verifies equal nonzero vertex counts, 8-vertex cluster alignment, <=2MiB shadow, maxRanges 1..8, density threshold 1..100. An empty plan on identical quantized bytes advances revision with **zero RHI traffic**.
- Same source commit: `applyHorizonForestWindOverlay(...,remainingUploadBytes)` builds the full candidate CPU colours **before any RHI mutation**, verifies independent unpooled device buffers and a forest primitive matching the captured absolute firstVertex/count, computes the pure plan, checks the sum of **selected** bytes against the remaining budget, validates all individual absolute vertex offsets/counts/byte addresses, then issues only the planned `gfx::Device::updateBuffer` contiguous ranges. On budget failure or unsafe GPU state it performs **no writes and does not advance the applied revision**, preserving the safe fallback graph. After successful issue of all writes it atomically swaps the candidate CPU shadow and advances its revision. `HorizonForestWindPatchResult` adds `ranges`, `changedClusters`, `fullRangeFallback`; old readiness/upload/byte fields retain their meaning.
- `0c97d30a8d0419aaf2cf93a2a6a25506c7b6d7cd` — `engine/render/TerraWeaveHorizonScene.h`: `refreshForestWindGpu(device)` retains its **2MiB total budget for active + outgoing crossfade models** but now accounts for the actual sparse byte sum. Existing `windOverlayPatchWrites` counts region update batches rather than individual writes. New runtime counters: `windOverlayPatchRanges` (actual RHI buffer calls), `windOverlayChangedClusters`, `windOverlayFullRangeFallbacks`, and `windOverlayNoopRevisions`. Deferred updates still keep their old macro/Stage33 graph until a complete region revision reaches GPU.
- `956a8e515b4deb7dbeaa0eafef7161face589428` — `docs/terrain/TERRAWEAVE.md`: Stage35 architecture, thresholds, public planner API, budget/fallback, telemetry, existing rendering contracts, QA debt.
- `bc4e9aefff2d5a90a16ab2e98a46e89e00c7b991` — `skills/world/aiverse-terraweave/SKILL.md`: Stage35 agent-facing sparse GPU update instructions and exact future verification cases.
- This handoff append completes Stage35.

### Important architecture and invariants
The immutable PHYSIS/game `ForestWindField` is still sampled at the original signed world-centimetre cluster centre; RG8 encodes quantized X/Z wind, B8 gust, A8 root height, as in Stage34. Horizon still has **one combined model and shared forest material/atlas**; updating the weather does not change GPU index/vertex topology, near-terrain seam, ground material, authored landscape source revision, async bake, TWH8 archive or genre/ECS physics. A cluster is exactly two crossed 4-vertex cards; metadata updates always transfer a whole cluster, even when only one attribute byte changed, preserving coherent cards. `HorizonGpuRegion::forestWind` remains the CPU shadow owned together with the active, cached and transition model. No weather upload occurs for mere ticks, only new immutable field/switch revisions.

Sparse transfer permits updating a small set of clusters even when a complete forest-card suffix is larger than the *remaining* shared frame upload budget. Excessively fragmented (>8 runs) or dense (>=70% changed clusters) updates choose one bounded full-range write instead of unbounded API calls. Existing `canUpdateSharedBuffers()`, GPU pool/base-offset and per-region model/forest range checks are retained. No extra GPU allocations, RHI backend implementations, shaders, texture, compute passes or per-cluster native entities were introduced. Runtime diagnostics distinguish no-change field revisions, actual GPU upload calls, uploaded bytes, changed clusters and full-range fallback frequency.

### Actual verification (do not misrepresent)
**Actually checked:** GitHub source reads of Stage34 patch helper and Stage34 Horizon update order/stats before editing; successful GitHub source/doc/skill commit responses above; static review of the selected algorithms and integration points via GitHub file fetches. **NOT RUN:** CMake, GCC, Clang, CTest, shader compilation, live Metal/Vulkan/GL framebuffer, PHYSIS integration, synthetic unit benchmark, GPU buffer instrumentation, device-loss simulation, screenshots or performance measurements. The Stage35 changes are source-implemented but **not compiled or validated on device**. Keep PR #24 Draft until an independent testing phase.

### Known limitations and risks
- Multi-range patch sequences are **preflight-all-ranges-before-first-write**, not hardware-atomic transactions. `gfx::Device::updateBuffer` returns void; if a GPU device fails between range writes, the scene cannot detect/rollback partial writes here and must rely on higher-level device-loss recovery. Do NOT claim genuine atomic GPU writes; only the candidate CPU shadow is committed after issuing the full sequence.
- CPU still samples all clusters and allocates a candidate shadow to locate dirty groups each new weather snapshot, though GPU traffic is sparse. For extremely large regions (>2MiB shadow) exact far-field mode safely falls back because Stage34 refused capturing that many card vertices.
- A highly fragmented field update (9+ runs) uploads the entire forest suffix if within budget. If a preceding active-region update exhausts the shared budget, an outgoing transition remains safely deferred.
- New runtime counter `windOverlayPatchWrites` is regions with successful nonempty upload plans, while `windOverlayPatchRanges` is the number of actual RHI updateBuffer calls. Neither metric is a measured frame-time improvement.
- The previously known Stage29 model test still asserts old 1-primitive/4-vertex impostor geometry; user requested test updates later, not now. Stage31–34 GLSL/Metal and Stage34 exact-field integration also remain uncompiled/untested.

### Concrete next step for the following agent
Continue **feature-first** work only where a functional gap remains, e.g. a configurable quality/performance policy for far GPU field sampling/CPU shadow residency, and keep its handoff in this same document. When the owner starts QA, update `tests/terraweave_model_test.cpp` for Stage29 2-layer impostors; add pure Stage35 planner tests for 1 adjacent/8 disjoint/9 disjoint dirty groups, 69%/70% dense transition, identical quantized colours, invalid offsets, shared 2MiB budget with active+outgoing models, cached-region restore, unsafe canUpdateSharedBuffers. Run real GCC+Clang CMake/CTest and GPU Metal/Vulkan/GL screenshots/perf; record exact commands/results/CI run URLs and fix failures without claiming old queued runs passed. Do not merge PR #24 before independent validation.

## Stage 36 — runtime-configurable Horizon wind GPU upload policy (SOURCE COMMITTED; TESTS DEFERRED)

**Date:** 2026-11-19. **Branch:** `feature/terraweave`. **PR:** [#24](https://github.com/chromealex/AIVERSE/pull/24) Draft, target `main`. **Instruction:** continue functional work; independent tester will compile/benchmark afterward. Before starting, GitHub compare indicated `main` is an ancestor of this branch (behind_by=0), so no main merge/rebase was required.

### Source and documentation commits (actual GitHub write responses)

- `7bdce959a8117ab086cc10e620e01706dae6229a` — **new** `engine/world/TerraWeaveWindUploadPolicy.h`. Defines public `HorizonWindUploadPolicy`, defaults 2 MiB combined GPU bytes/update, 8 sparse ranges/region, >=70% dirty cluster full-fallback; `validateHorizonWindUploadPolicy` enforces bytes [0,2MiB], ranges [1,8] and percentage [1,100] with fail-before-publication validation.
- `674e39eebbe64742d416e9dc6bb8901825b5581a`, comment correction `8ba64d8c8bec9fcf87dba7d480c8d3f50b9358af` — `engine/render/TerraWeaveHorizonWindPatch.h` includes the public policy and adds a trailing backward-compatible defaulted policy argument to `applyHorizonForestWindOverlay`. The existing Stage35 pure planner now receives the configurable fragmentation/density settings and RHI transfer is rejected before the first write when the selected plan exceeds either the caller's remaining frame bytes or the policy's own cap.
- `af97a891646ebee7d7bae590981c503006ce6b5e`, comment correction `dcfa1c955e9b747c4bb0e88606ddf3a526b1f0b8` — `engine/render/TerraWeaveHorizonScene.h`: `setWindUploadPolicy`/`windUploadPolicy` on the owner thread; the per-update budget now originates from policy rather than a hardcoded 2MiB. Existing active-first/outgoing-second retirement and revision handling remains intact. No revision bump merely for policy changes: pending regions are automatically retried on following updates.
- `eb532e841d3f42b396ce4b384c6dbd6a8948c9f8` — `engine/world/WorldScene.h` publishes `setVegetationHorizonWindUploadPolicy` and `vegetationHorizonWindUploadPolicy`; stores default value even when the optional Horizon system is absent.
- `7aaa2ef0b4e5c389a0f18ea88d0b3898b69bba0b` — `engine/world/WorldScene.cpp` validates and forwards policy to the optional Horizon system before changing its stored value. No PHYSIS/ECS/genre-specific includes.
- `2c140bf77a13bd99d7816c078c60359317602e23` — `docs/terrain/TERRAWEAVE.md` Stage36 public API, sample mobile budget, contracts and limitations.
- `52b4d4a70ba2986aa448d871aac35ce05f535de4` — `skills/world/aiverse-terraweave/SKILL.md` Stage36 native agent rules, invariants and future QA.
- `7e9a2273eae21e65075134efe03deb07f9aaf02e` — `AGENTS.md` Stage36 cross-agent owner-thread, policy and validation contract.
- This separate handoff entry closes the Stage36 functional stage.

### API, behavior and compatibility

```cpp
terraweave::HorizonWindUploadPolicy policy{};
policy.maxBytesPerUpdate=256u*1024u; // 256 KiB combined per update
policy.maxSparseRanges=4;
policy.fullFallbackPercent=85;
worldScene.setVegetationHorizonWindUploadPolicy(policy);
```

`maxBytesPerUpdate=0` intentionally defers nonempty weather VB updates while allowing a zero-colour-difference revision to advance without GPU traffic. Switching back to a larger budget retries pending active/crossfade regions on the next render-owner `update(device)`. If the configured budget is smaller than the full-suffix fallback selection, a dense/fragmented update may remain pending indefinitely; the preexisting macro wind visual fallback stays safe. Stage35 telemetry still counts actual region update batches, RHI ranges and byte totals, not theoretical performance improvements.

Exactly the same default 2MiB/8/70 Stage35 policy is preserved. Terrain+canopy geometry, TWH8 archive, atlas pixels, wind sampling/quantization, shader source, render materials, existing bounded 2MiB **CPU forest shadow per resident GPU model**, GPU cache policy, active/outgoing crossfade ownership, source revision/CPU bake and native PHYSIS input are untouched. This feature is a **runtime adjustable GPU transfer scheduling policy**, not a new CPU-memory cap or different wind solver. Calls must be serialized on the render/WorldScene owner thread; no concurrent setter/update/submit use is supported.

### Actual verification vs not-run work

**Actually verified:** Read Stage35 source, Horizon update/render selection, WorldScene interface and last handoff directly from GitHub; GitHub returned successful separate code/documentation commit SHAs listed above, with comments corrected after initial writes. No local source checkout or native runtime is available from these connector operations.

**NOT RUN (user-requested QA deferral):** C++ compilation, CMake configure/build, GCC/Clang, CTest, CI outcome validation, ShaderGraph GLSL/MSL compilation, Metal/Vulkan/OpenGL GPU tests, PHYSIS runtime publication, performance sampling, screenshot matching and device-loss recovery. **Do not claim passing tests, working rendering, validated memory savings or production readiness.** Keep PR #24 Draft, do not merge to `main` yet.

### Risks and next actionable step

- Tight per-update transfer budgets may starve full-suffix uploads; no automatic budget escalation or GPU region scheduling fairness exists. Sparse planner is per region and maximally eight runs; actual GPU call overhead may make other thresholds better on mobile.
- CPU field sampling still walks all resident clusters on each field revision and keeps an up-to-2MiB shadow for each GPU model, including cached/outgoing models. Configuring **CPU shadow residency/priority** separately is a possible Stage37 feature, but requires explicit policies for preexisting GPU models, in-flight worker publishes and cache re-adoption. Do not silently discard the only source of exact field RGB while claiming the exact shader is ready.
- Same known Stage35 device-loss caveat: `gfx::Device::updateBuffer` returns void; an interrupted multi-range write is not hardware-atomic.
- The Stage29 impostor fixture still contains stale 1-layer/4-vertex expectations. This stage intentionally did not edit tests.

**Next feature-first work (Stage37):** Add a bounded optional CPU shadow residency/selection policy for Horizon's exact-field metadata, with safe fallback for regions lacking a shadow, preserving immutable PHYSIS and active/crossfade shader revision invariants. First audit GPU cache lifecycle before evicting or recapturing any shadow. The later independent testing agent must update Stage29 atlas assertions and exercise Stage35+36 default parity, invalid policy ranges, zero budget/no-change revision, starvation-and-retry, dense threshold at configurable 85%, 4 vs 5 sparse runs, combined active+outgoing bandwidth cap, region LRU restoration, unsafe RHI updates, and real GCC/Clang/CTest/Metal/Vulkan/GL visual performance. Report actual CI run IDs and outcomes in a separate handoff.

## Stage 37 — bounded CPU wind shadow residency for inactive Horizon GPU cache (CODE COMMITTED, TESTS DEFERRED)

**Date:** 2026-11-19. **Working branch:** `feature/terraweave`. **PR:** [#24](https://github.com/chromealex/AIVERSE/pull/24), Draft, base `main`; `main` was verified ancestral at stage start/end (behind_by=0). **Owner's explicit priority:** implement requested functional stages first, tests after.

### Actual code and documentation commits

- `c7b9c2e8c7870e015f27e71218123c983b5e615c` — **new** `engine/world/TerraWeaveWindShadowCachePolicy.h`. Defines public, trivially copyable `HorizonWindShadowCachePolicy { uint32_t maxCachedShadowBytes=16MiB; }` and `validateHorizonWindShadowCachePolicy`. Bounds [0,16MiB]; default retains preexisting Stage35 GPU cache behavior.
- `366a74930eb7650d995cdf5dcc173fa8cf3ba160` — `engine/render/TerraWeaveHorizonGpuCache.h`. Adds `horizonForestWindShadowCapacityBytes` counting actual retained `vertices.capacity()` and `centersCm.capacity()` CPU memory (with saturated addition), `cachedWindShadowBytes()`, `windShadowEvictions()`, and `trimWindShadowBytes(device,budget)`. When over budget, selects the *oldest cached model that actually owns a nonzero CPU wind shadow*; calls the existing normal `release(device,...)` to destroy **the whole matching GPU region plus CPU overlay**. The `retain(...)` method now accepts a backward-compatible defaulted final shadow budget and enforces it as soon as the model is cached. The existing general eviction counter still increments, and a separate subset counter tracks shadow budget evictions.
- `a57539e680bc8f1d0ec0adfcd2d9fca6d9949c11` — `engine/render/TerraWeaveHorizonScene.h`. Adds `setWindShadowCachePolicy`/`windShadowCachePolicy` for the graphics owner thread, with validation. The next `update(device)` trims after stale source pruning and transition completion; finished transition/normal retirement passes the budget to `gpuCache_.retain`, so new dormant entries are immediately constrained. Adds live `HorizonRuntimeStats::gpuCachedWindShadowBytes` and cumulative `gpuWindShadowBudgetEvictions`. The active model and outgoing crossfade are NEVER selected by the CPU-cache eviction routine.
- `53f409307fe35e9aa6ac4b736eb69314b476e129` — `engine/world/WorldScene.h`: includes new public policy header; exposes `setVegetationHorizonWindShadowCachePolicy`/`vegetationHorizonWindShadowCachePolicy` to any native project without PHYSIS, FPS or RTS headers. Stores policy even with optional Horizon disabled.
- `b064c330fb71d409c33677165053b0853f5e5af6` — `engine/world/WorldScene.cpp`: validates before publishing or forwarding to the optional Horizon system; no new runtime jobs, engine-specific logic or GPU call inside setter.
- `b215de03bf60d303535134bb8171a1c59e59021c` — `docs/terrain/TERRAWEAVE.md`: user-facing API example, safe eviction/reentry model, byte semantics, telemetry, limits and QA.
- `a2c0ff8a309925e9a0a55a2d66be420a7908cdb3` — `skills/world/aiverse-terraweave/SKILL.md`: new agent usage, invariants and future QA.
- `75cf032caff17f418449cc7db79e7b6602c7728e` — root `AGENTS.md`: project-wide reminder to never detach an overlay from an exact-field GPU VB and to keep accurate Stage37 handoffs.
- This **separate handoff commit** closes Stage37 and is the durable next-agent continuation checkpoint.

### Public API, behavior and compatibility

```cpp
aiverse::terraweave::HorizonWindShadowCachePolicy policy{};
policy.maxCachedShadowBytes=0; // Do not keep dormant wind overlays in GPU LRU
worldScene.setVegetationHorizonWindShadowCachePolicy(policy);
```

Direct HorizonScene `setWindShadowCachePolicy` and matching getter are equivalent. The limit counts **only dormant cached CPU wind overlay vector capacity**, not active or outgoing transition shadows, GPU bytes, generic CPU model patch backups or CPU `HorizonBake` atlases. Stage36's `HorizonWindUploadPolicy` still independently governs the *shared GPU vertex update bandwidth* across active/outgoing regions; Stage37 does not alter any exact-field shader revision or wind colour quantization.

A forest GPU vertex buffer may contain *exact PHYSIS-packed RGB8* rather than procedural RGB8 while its matching CPU shadow tracks the last successfully applied revision. There is no portable GPU readback or authoritative original bytes after `uploadModel` drops the CPU mesh. Therefore simply freeing `HorizonGpuRegion::forestWind.vertices` while keeping `region.model` would make Stage35 sparse patch comparisons unsafe. Stage37 deliberately evicts an **entire dormant cached GPU model** with its shadow instead, using the existing RHI-owner release path. A later camera revisit is a standard GPU cache miss and runs the existing asynchronous CPU prepare/model upload path; the new prepared model captures a coherent wind overlay before upload, optionally reusing the current CPU bake cache or TWH8 archive. Unaffected cached GPU entries still re-adopt instantly with both model and shadow, and camera fade regions are never trimmed mid-transition.

Default policy 16MiB exceeds the upper bound of three cached overlays (each existing shadow contains up to 2MiB forest vertices plus bounded cluster-centre storage), keeping existing behavior by default. At a budget of zero, shadow-bearing inactive cache entries are released instead of retained; this can increase new GPU uploads and CPU preparation costs on camera revisits. A normal model that contains no wind overlay may remain cached because it consumes no wind-shadow bytes. No separate GPU resource/shader, material, terrain-seam/mesh topology, TWH8 archive version, PHYSIS dependency, ECS component or gameplay state has been introduced.

### Actually verified vs NOT RUN

**Actually checked:** GitHub source reads of `TerraWeaveHorizonGpuCache.h`, `TerraWeaveHorizonScene.h` streaming/retire/transition logic, capture-on-upload, `WorldScene`, and the new policy after write; successful GitHub source/docs/skill commit results above; GitHub PR remains Draft and `main` ancestral (not merged). Static source only: `cachedWindShadowBytes` and `trimWindShadowBytes` are present, policy is forwarded by WorldScene, renderer `update` trims and both cache-retain paths pass the policy.

**NOT RUN:** local code checkout, GCC/Clang/MSVC build, CMake configure/build, CTest, CI workflow validation, GLSL/MSL compilation, Metal/Vulkan/OpenGL runtime/captures, real PHYSIS field input, GPU cache LRU/transition gameplay tests, measured memory savings, CPU/GPU performance timings, device-loss recovery. The stage is source-implemented, **not proven to compile or perform**. Keep PR #24 Draft; do not merge to `main` on this evidence alone.

### Risks, limitations and precise next actions

1. **Deliberate granularity:** CPU wind shadow budget evictions are whole GPU cache entries, not independent shadow drops or partial compression. Low budgets may cause full model+atlas GPU re-upload at every revisit. This is required for correctness with current no-readback RHI and existing patch planner.
2. **Scope:** Only inactive cache `forestWind` vectors count. Other retained CPU data such as cached `HorizonBake`, optional `vertices/indices` for terrain partial patch, and active/transition memory are outside this policy. The resident GPU-cache model budget remains controlled by `HorizonSettings`.
3. **Lifecycle:** Changes to policy do not call RHI in setter; trimming happens on next `update(device)` and each `retain`. Existing `prune` still handles stale source revisions. Worker completion and GPU upload continue respecting prior owner-thread barriers.
4. **Next functional step (Stage38, only if still desired):** Design truly compressed/reattachable per-region wind shadow or a staged model-geometry reconstruction path that saves CPU bytes **without evicting its GPU model**. Must prove reconstituted CPU vertex colours match actual GPU bytes or force a validated complete forest suffix upload first, and must respect Stage36 budget and shader fallback. This is optional; prioritize an independent compile/QA phase once the owner finishes feature scope.
5. **Required later tester acceptance:** Small policy boundary [0,16MiB] and rejection >limit; exact vs procedural field applied before dormancy; 0/MiB/16MiB behavior and oldest-first eviction; active/outgoing fade continuity; cached model without shadow; multiple camera reversals, stale source generation, budget changed at runtime; CPU-reentry worker source revision safety, upload budgets, Stage29 two-primitive impostor fixture update, Stage35 sparse ranges, and actual Metal/Vulkan/GL backend performance. Record actual command lines and CI run URLs in the next validation handoff, never infer passing tests.

## Stage 38 — reversible, lossless inactive Horizon wind shadow compression (SOURCE COMMITTED; QA DEFERRED)

**Date:** 2026-11-19. **Branch:** `feature/terraweave`. **PR:** [#24](https://github.com/chromealex/AIVERSE/pull/24), Draft, target `main`. The user approved Stage38 after Stage37 and keeps the existing feature-first / separate tester arrangement. No merge or branch rebase performed.

### Implementation commits and exact affected paths

- `52f61e5571a6f11c492b1df9e5398d5aa7ba825d` — `engine/render/TerraWeaveHorizonWindPatch.h`: trailing in-memory `HorizonForestWindOverlay::packedVertices` and `packedVertexCount` fields. Existing vertex model, wind revision and signed cluster centres remain authoritative; GPU patcher still operates exclusively on fully expanded `vertices`.
- `73ecf60a327fe42323fde604042f90d46c45b122` — NEW `engine/render/TerraWeaveHorizonWindCompression.h`: deterministic, bounded bytewise lossless zero-run/literal encoding and inverse decoding. `packHorizonWindVertexBytes` and `unpackHorizonWindVertexBytes` preserve all `ModelVertex` object bytes, including float bits and packed RGBA wind/height data. `compressHorizonForestWindOverlay` validates max 2MiB, 8-vertex cluster alignment and matching centres, packs only when actual vector capacity improves, then drops only the dormant raw CPU vertex storage. `restoreHorizonForestWindOverlay` verifies token/output sizes and reconstructs a temporary full vector before publishing it; the original compact payload is retained on failure.
- `2b2846c3dedfab4e78e30dc90f10c3ee10175bb3` — `engine/world/TerraWeaveWindShadowCachePolicy.h`: adds `bool compressCachedShadows=false` opt-in to the existing Stage37 policy. Default behavior remains unchanged; existing 0..16MiB `maxCachedShadowBytes` still governs inactive CPU shadows.
- `9cf48063f3bee4f4f60b030194af416a9cf593be`, `ca7ed7ddd41fe24fd54873fe9d2f04084df5a3d7`, `8e86910bb6fc28a414f2ef62090e18b7576e2ff1` — `engine/render/TerraWeaveHorizonGpuCache.h`: counts both packed and uncompressed vertex vector **capacities**, plus uncompressed signed cluster centres, toward the inactive CPU budget. `retain` may compress a dormant region without RHI modification. `compactWindShadows` supports enabling the policy **after** cache population: at most one packing attempt per cache entry, even for nonbeneficial/no-forest entries. `take` restores exact bytes before returning the still-uploaded GPU model to active ownership. The device-aware `take(device,cell,revision)` catches malformed-stream logic errors, retires that full GPU cache entry with the existing RHI owner-thread release operation, and reports a cache miss so the normal asynchronous region prepare/upload path can run. `std::bad_alloc` is not swallowed. Added compact/restored/decode-failure counters and compressed inactive-entry count.
- `35ff5b91db99520f7fbe3786854f8295f47d9b3e`, `26292dc242e4a315d02e885ce4253b373d39b39d`, `0a8e061175e47490c8d77ed0c097354ae33ec01e` — `engine/render/TerraWeaveHorizonScene.h`: forwards `compressCachedShadows` at both retirement/transition completion `gpuCache_.retain` sites; compacts old entries on the next owner-thread `update(device)` before Stage37 LRU budget trimming; reactivates cache using device-aware `take`. Exposes `HorizonRuntimeStats::gpuCachedCompressedWindShadows`, `gpuWindShadowCompressions`, `gpuWindShadowRestores` and `gpuWindShadowDecodeFailures` without changes to prior stats semantics.
- `c85679786b8e79e2ee1da4c143fd3c7cd33b4dce` — `docs/terrain/TERRAWEAVE.md`: API example, codec, CPU budget, RHI lifecycle, error fallback and QA boundaries.
- `49110c8794f97b9b62967849094a06ffc6987c38` — `skills/world/aiverse-terraweave/SKILL.md`: next-agent rules including safe ownership, exact restoration and data validation.
- `99127d85d263a2f7b87d6e62867431820511a2a1` — `AGENTS.md`: genre-independent Stage38 ownership/QA constraints.
- This entry is a distinct committed **per-stage handoff** after the functional source/doc commits.

### Native API, correctness and lifecycle

```cpp
aiverse::terraweave::HorizonWindShadowCachePolicy policy;
policy.maxCachedShadowBytes=2u*1024u*1024u;
policy.compressCachedShadows=true;
worldScene.setVegetationHorizonWindShadowCachePolicy(policy);
```

`WorldScene` already forwards the policy to optional `TerraWeaveHorizonScene` (Stage37), so a new setter is not needed. Stage36's `HorizonWindUploadPolicy` still independently limits actual GPU weather **vertex-buffer transfer** bytes. Stage38 only compresses **dormant** GPU LRU-associated CPU references; it never mutates active/outgoing crossfade CPU shadow, uploaded VB/IB or shader selection. Inactive `HorizonGpuRegion` keeps its GPU model, source revision and unchanged wind `appliedRevision`, but stores bytewise lossless packed `ModelVertex` data and the original signed world-centimetre cluster centres. At a GPU cache hit, unpack precedes `adoptCached`; Stage35 can then apply the next weather field revision against the exact previous uploaded vertex bytes, not approximations of geometry or samples. There is no GPU readback, model upload, GPU buffer allocation, atlas regeneration, PHYSIS dependency, ECS change, shader recompilation or TWH8 archive format update in the ordinary compressed-cache-hit path. A bad packed stream is retired with the complete cached GPU resource and falls back to the already-existing prepare/upload process, not unsafe partial writes.

Compression is deliberately **opt-in** and accepted only when byte capacity shrinks, not a guaranteed ratio. Incompressible data stays raw and may still be fully evicted under Stage37's budget. The codec is an in-process byte stream, not a portable/durable file, and memory pressure during packing/unpacking creates temporary extra allocations. Source/worker generations, full-vs-partial GPU patch, active/outgoing model residency, and CPU `HorizonBake` caches continue to follow existing rules. Modifying the policy requires the owning WorldScene/renderer thread; compaction of already-resident inactive regions happens on a later `update(device)`. All given runtime stats are counters, not verified performance improvements.

### Verified work and explicit testing debt

**Actually verified:** Fetched the current Stage37 GitHub branch and read `ModelVertex` layout (60 bytes), `TerraWeaveHorizonGpuCache` take/retain/prune/eviction, `TerraWeaveHorizonScene` owner-thread update, fade and GPU-cache reactivation, the existing Stage35 wind patcher, and Stage37 policy/handoff. GitHub write actions returned the source/docs/skill SHA values above. After writes, re-read the new codec, GPU cache compression/restore and scene forwarding via GitHub. PR remained Draft targeting `main`. Stage37 baseline was ancestral to `main` (behind=0).

**NOT RUN:** source checkout/native C++ compile, Clang/GCC/MSVC, CMake configure/build, CTest, synthetic codec roundtrip fixtures, fuzz/malformed-stream tests, PHYSIS integration, cached-HLOD GPU revisit, GL/Metal/Vulkan screenshots, RHI memory instrumentation, frame timing or measured compression ratios. This is **source-implemented but not compiler- or GPU-validated**; do not mark complete for production or merge the PR before testing.

### Limitations and next steps

- **Correctness QA:** verify byte-for-byte preservation of representative `ModelVertex` arrays (zero runs at 1/2/3/128/129 bytes, high-entropy literals, negative/NaN float bits, RGBA alpha); rejected/truncated/overlong packed tokens; preservation of `firstVertex`, `centersCm`, `appliedRevision` and exact shader variant on reactivation; corrupt stream must release full cached model and queue safe re-stream.
- **Scheduling QA:** policy opt-in after cache is populated, one compression attempt per cached entry, unaffected active/outgoing crossfade, source-generation stale pruning, camera reversal, immediate reuse from compressed GPU LRU, compression ratio and restore cost with native real forest geometries.
- **Budgets QA:** zero/small/16MiB limits, mixed compressed and raw shadows, long-lived no-forest cache entries, general GPU LRU eviction accounting vs Stage37 CPU-budget-specific evictions, Stage36 2MiB/sparse upload caps with new PHYSIS field revision after restore.
- Existing Stage29 outdated one-layer impostor CTest assertions and uncompiled Stage31–37 shader/render work remain test debt; separate tester must update, compile and run actual supported backend checks.
- **Possible next feature (Stage39, only if desired):** targeted memory representation optimizations or a native GPU-cache quality preset for mobile/desktop. Prefer benchmarking Stage38 first; do not promise compression is cheaper until measured.

## Stage 39 — independent mobile/desktop Horizon runtime presentation quality (FEATURE SOURCE COMMITTED; TESTS DEFERRED)

**Date:** 2026-11-19. **Branch:** `feature/terraweave`. **PR:** [#24](https://github.com/chromealex/AIVERSE/pull/24), Draft, target `main`. At stage start, `main` was already ancestral (`behind_by=0`), so no merge/rebase was required. The owner approved continued features with QA assigned to another agent; CMake/CTest/GPU checks intentionally remain deferred.

### Actual implementation / documentation commits

- `bfd17531b7f03ab2b12b18b3a670800b3ed70542` — **new** `engine/world/TerraWeaveHorizonPresentationQuality.h`: independent, genre-neutral POD `HorizonPresentationPolicy` combines Stage33/34 exact and procedural spatial wind switches, Stage36 GPU upload policy and Stage37/38 inactive wind CPU shadow policy. `HorizonPresentationQualityPreset {BatterySaver,Mobile,Balanced,Desktop}` with `horizonPresentationPolicyForQuality` provides bounded starting values. `validateHorizonPresentationPolicy` checks the entire bundle prior to publication.
- `f1a3268bca41ac90171e27729d77bce933bdc48f` — `engine/render/TerraWeaveHorizonScene.h`: `setPresentationPolicy`, `presentationPolicy`, `setPresentationQualityPreset` compose existing setters on render-owner thread; no new GPU resource, PHYSIS solver or geometry.
- `e2a1cba210712f9a63122509a5fd1e63123c3f2a` — `engine/world/TerraWeaveHorizonManifest.h`: optional `terraweaveHorizon.presentationQuality` accepts `battery-saver|mobile|balanced|desktop` (invalid values throw `JsonError`). This is **separate** from existing `terraweaveHorizon.quality` terrain/atlas bake preset; absent presentationQuality preserves Stage38 legacy defaults.
- `17486c8b0f42f2451e66b78b9bd882158a0d81bc` — `engine/world/WorldScene.h`: native `setVegetationHorizonPresentationPolicy`, `vegetationHorizonPresentationPolicy`, `setVegetationHorizonPresentationQualityPreset`; getter composes actual currently selected individual settings, including manual overrides.
- `228c62a589d8bf701c6e5c6c1be9d7b917bd1e9b` — `engine/world/WorldScene.cpp`: copies the optional manifest-applied direct HorizonScene policy into WorldScene's pre-existing per-policy fields during construction. Setter validates the full policy before applying the four existing switches/budgets, so an invalid bundle cannot cause partial mutation. No authoritative gameplay state touched.
- `5a101a849942dca0ac6427033150fb30ffe8883a` — `engine/render/TerraWeaveHorizonScene.h`: `HorizonRuntimeStats` effective `windUploadBudgetBytes`, `cachedWindShadowBudgetBytes`, `windUploadSparseRangeCap`, `windUploadFullFallbackPercent`, `windShadowCompressionEnabled`, `spatialWindVariationEnabled`, independent of any originally chosen named preset.
- `f59e79205151490471e87abae1c692119628714f` — `docs/terrain/TERRAWEAVE.md` explains independent geometry/presentation policies, manifest/native samples, exact preset values and limits.
- `a32de2e9bb9dc5efdba3750570c6324bbf536325` — `skills/world/aiverse-terraweave/SKILL.md` records API ownership, safe defaults, testing and why to reuse the existing wind/cache stages.
- `3bb59e2de4cd4bb472a5c8798223f55b0a9e5408` — root `AGENTS.md` captures the Stage39 composability rule, avoiding duplicate wind render subsystems.
- This separately committed handoff is the mandatory final entry for Stage39.

### Preset values / compatibility

| Preset | Exact field | Procedural variation | GPU bytes/update | Ranges | Full fallback | Inactive shadow CPU budget | Compress |
|---|---|---|---:|---:|---:|---:|---|
| BatterySaver | No | No | 0 | 1 | 70% | 0 | No |
| Mobile | No | Yes | 128 KiB | 2 | 90% | 1 MiB | Yes |
| Balanced | Yes | Yes | 512 KiB | 4 | 75% | 4 MiB | Yes |
| Desktop | Yes | Yes | 2 MiB | 8 | 70% | 16 MiB | Yes |

Existing worlds **without** `presentationQuality` still use Stage38 runtime defaults (exact spatial field true, procedural variation true, 2MiB/8/70 GPU upload, 16MiB dormant CPU shadow, compression false). Geometry `HorizonSettings` and `HorizonQualityPreset` remain unchanged: terrain resolution, forest cluster atlas and seam welding continue to depend only on geometry/source settings and TWH8. Using a presentation preset does not invalidate workers or cause Horizon CPU bakes, geometry rebuild, shader source regeneration, new RHI allocations or PHYSIS/ECS changes. Policy changes are applied on WorldScene/Horizon owner thread; no concurrent `update`/`submit`.

Native usage:

```cpp
using namespace aiverse::terraweave;
worldScene.setVegetationHorizonPresentationQualityPreset(
    HorizonPresentationQualityPreset::Mobile);
auto policy=worldScene.vegetationHorizonPresentationPolicy();
policy.windUpload.maxBytesPerUpdate=256u*1024u;
worldScene.setVegetationHorizonPresentationPolicy(policy);
```

Startup JSON: `"terraweaveHorizon": { "quality":"high", "presentationQuality":"mobile" }`. This combines high geometry quality with a mobile runtime policy deliberately, not implicitly. A tiny or zero update budget may defer restoration of exact/procedural field metadata, with the existing safe macro wind shader fallback; the corresponding preset is **not a guarantee of specific FPS, GPU bandwidth, battery usage, wind visual parity or memory savings**. Dormant CPU shadow budget may evict whole GPU cache entries if even compressed bytes exceed it.

### Verification actually performed

**Source-only checks performed:** read current feature-branch `TerraWeaveHorizon.h` geometry presets/validation, `TerraWeaveHorizonManifest.h` JSON parser, the Stage36–38 CPU/GPU budget policy headers, Horizon GPU scene update/render/lifecycle and native WorldScene API. Confirmed Stage39 GitHub source writes succeeded with exact SHAs above and fetched the new header, direct scene API, manifest parser, WorldScene constructor/setter and runtime stats after writes. No local container native build was executed. Existing Draft PR status was inspected, and Stage39 changes are on `feature/terraweave`, not merged to main.

**NOT RUN:** Clang/GCC/MSVC compiler, CMake configure/build, CTest, live native runtime, JSON manifest fixture, GLSL/MSL shader compilation, Metal/Vulkan/GL visual QA, PHYSIS field integration, CPU/GPU bandwidth benchmarks, battery/frame-time captures, cache/crossfade tests, device-loss simulation. Do not claim Stage39 is compiled, runtime-verified or production-ready. Keep PR #24 Draft until independent QA and fixes.

### Known limitations and next concrete actions

- A preset is a convenient **initial settings bundle**, not automatic device detection or dynamic scaling. After manual property overrides, query effective `HorizonPresentationPolicy` rather than claiming one named preset remains active.
- Low transfer budgets can cause a dense or fragmented GPU wind metadata revision to remain pending indefinitely. BatterySaver disables GPU wind metadata transfer entirely and can retain the existing macro fallback after switching from exact mode; explicitly changing back to higher quality may be required for detailed distant wind. Automatic anti-starvation/budget boosts would need separate review and performance constraints.
- The Stage38 lossless CPU shadow codec is optional; Mobile/Balanced/Desktop presets enable it for dormant GPU cache entries. Activation still restores full bytes before Stage35 GPU RHI patches, with the same correctness contract and staging memory cost.
- **Independent tester's next step:** refresh Stage29 outdated 1-layer impostor fixtures first, then compile/test Stage31–39, including `HorizonPresentationQualityPreset` values and invalid enums, optional manifest defaults and parsing errors, constructor getter synchronization, bundled validation with no partial mutation, switching exact/procedural wind during active/outgoing crossfade, per-update bandwidth budget vs sparse/dense runs, zero-budget safe fallback, LRU compressed/raw cache retention/re-entry and Metal/Vulkan/GL visual/perf. Save actual command/output and CI links in a new handoff; do not merge merely because GitHub source writes succeeded.
- **Optional later feature (Stage40 only if explicitly continued):** bounded user-controlled automatic quality adaptation, using measured sustained GPU/CPU frame times and hysteresis with explicit opt-in. Do not silently alter `HorizonSettings` geometry or TWH8 archive fingerprints at runtime.

## Stage 40 — bounded host-fed adaptive Horizon presentation quality (SOURCE COMMITTED; TESTS DEFERRED)

**Date:** 2026-11-19. **Branch:** `feature/terraweave`. **PR:** [#24](https://github.com/chromealex/AIVERSE/pull/24), Draft, targeting `main`. At start of stage GitHub compare returned `behind_by=0`; `main` is already ancestral and no branch merge/rebase/force push was necessary. Owner approved **functional feature work first; compile, QA, GPU benchmarking belong to a separate stage**.

### Implementation and documentation commits (actual GitHub write responses)

- `9f04ba49913edc3c96f1c2cd895e643a9ea56128` — NEW `engine/world/TerraWeaveHorizonAdaptiveQuality.h`. Adds `HorizonAdaptiveQualityConfig`, `HorizonFrameTimingSample`, `HorizonAdaptiveQualityState`, `initialHorizonAdaptiveQualityState` and pure `advanceHorizonAdaptiveQuality`. The policy has a configurable bounded quality floor/ceiling/initial level and validated target in microseconds, over/under target percentages, consecutive frame streaks and cooldown. It refuses invalid, duplicate, out-of-order, zero/missing, or >10s samples; only contiguous frame IDs contribute to a persistent streak. The worst available **real** CPU/GPU duration determines the decision, one Stage39 preset rank per successful transition. No GPU backend or PHYSIS dependency.
- `60fd2a39547311f5c048495509273ed396a8aa4b` — `engine/render/TerraWeaveHorizonScene.h`: runtime owner-thread `setAdaptiveQualityConfig`, `adaptiveQualityConfig`, `adaptiveQualityState`, `submitAdaptiveFrameTiming`. Enabling selects its validated initial Stage39 runtime profile and resets sample history; disabling preserves the last applied quality. The controller uses existing Stage39 setters to apply a changed preset. Explicit manual Stage39 whole-policy or any of the individual wind presentation setters disables adaptation, while ordinary field/tick updates leave it untouched. Adds `HorizonRuntimeStats::adaptiveQualityEnabled`, `adaptiveQualityLevel`, samples/switches/downgrades/upgrades and last measured worst frame microseconds.
- `d403699bda226bc88174487a681665d20a786476` — `engine/world/WorldScene.h`: native `setVegetationHorizonAdaptiveQualityConfig`, `vegetationHorizonAdaptiveQualityConfig`, `vegetationHorizonAdaptiveQualityState`, `submitVegetationHorizonFrameTiming` with no genre-specific include.
- `9b28474845ebb59e9533b7c8b19dfb125ee0b0fa` — `engine/world/WorldScene.cpp`: forwards real timing samples to the optional Horizon scene; after an automatic transition keeps all existing `WorldScene` Stage39 effective policy getters synchronized; mirrors manual override opt-out and initializes the config getter from an optional manifest-provided Horizon controller. Disabled/absent Horizon returns false on submissions rather than inventing sample data.
- `1dcaf5a71b6227bb0bb88ad0b7564444a59ab502` + BatterySaver floor follow-up `8b6ccb2e46043df6499557b109a32821e28d2cbb` — `engine/world/TerraWeaveHorizonManifest.h`: strictly opt-in nested `terraweaveHorizon.adaptiveQuality` object. Bounded fields: `enabled`, `minimum`, `maximum`, `initial`, `targetFrameUs`, `downgradePercent`, `upgradePercent`, `downgradeFrames`, `upgradeFrames`, `cooldownFrames`. Values are parsed/validated; bad names/types/ranges throw `JsonError`. If initial is omitted, inherited `presentationQuality` is used, else Balanced; choosing static BatterySaver expands the default minimum to BatterySaver. Existing `quality` geometry bake semantics are untouched.
- `48eb033eb9d0a9d00d1a054c5ed27b8693031bab` — `docs/terrain/TERRAWEAVE.md`: native/manifest usage, real CPU/GPU time ownership, hysteresis and QA limitations.
- `4220a0b5f00fdb8b46cae221f182feb857606ff0` — `skills/world/aiverse-terraweave/SKILL.md`: agent-facing source ownership, native API, safe timing and future test cases.
- `d68767f738823c5ed714b09f13d23aa54df5ce09` — `AGENTS.md`: root cross-agent rule on measured frame samples, no geometry/PHYSIS ownership or fabricated GPU telemetry.
- This appended **separate handoff commit** closes Stage40 implementation and records actual status for the next developer.

### Native API and manifest example

```cpp
using namespace aiverse::terraweave;
HorizonAdaptiveQualityConfig config;
config.enabled=true;
config.minimum=HorizonPresentationQualityPreset::Mobile;
config.maximum=HorizonPresentationQualityPreset::Desktop;
config.initial=HorizonPresentationQualityPreset::Balanced;
worldScene.setVegetationHorizonAdaptiveQualityConfig(config);

// The native host measures actual frame durations; 0 means unavailable.
// Call from the same WorldScene/render owner thread after each real frame.
worldScene.submitVegetationHorizonFrameTiming({
    .frameId=presentedFrameSequence,
    .cpuFrameUs=measuredCpuFrameUs,
    .gpuFrameUs=measuredCompletedGpuFrameUs
});
```

The optional JSON configuration:

```json
{
  "terraweaveHorizon": {
    "quality":"high",
    "presentationQuality":"balanced",
    "adaptiveQuality":{
      "enabled":true,
      "minimum":"mobile",
      "maximum":"desktop",
      "initial":"balanced",
      "targetFrameUs":16667,
      "downgradePercent":115,
      "upgradePercent":80,
      "downgradeFrames":30,
      "upgradeFrames":240,
      "cooldownFrames":240
    }
  }
}
```

Runtime defaults are **disabled**, target 16,667 microseconds (~60 Hz), downgrade after 30 consecutive frames >115% target, upgrade after 240 consecutive frames <80% target, cooldown 240 frame IDs between any two switches, and Mobile..Desktop bounds. A quality change is exactly one level, never an immediate jump to extremes. Both CPU/GPU inputs are optional individually; at least one must be real/nonzero. Host must correctly relate completed asynchronous GPU timings to supplied frame IDs or submit GPU=0 if unsupported; `gfx::Device::gpuFrameMs()` may be absent/lagging and is not silently treated as current data. The controller does not inspect frame time without a host-provided timing submission and does not invent GPU timers.

The controller is an **optional presentation quality policy over existing Stage39 presets**, not a new GPU allocation, asset/atlas format, render pass, mesh regeneration or automatic terrain LOD replacement. It never mutates `HorizonSettings`, authored near-landscape masks, TWH8 bake fingerprint, PHYSIS weather state, deterministic ECS gameplay or network replication. Changing the enabled/profile policy is owner-thread-only. `WorldScene` mirrors effective runtime settings whenever the controller changes levels; manual Stage39 API calls deliberately disable automatic decisions. A disabled controller keeps its last selected presentation profile and no further sample can switch it until re-enabled.

### Verified and explicitly unrun work

**Actually verified:** GitHub inspection of Stage39 `HorizonPresentationQuality`, HorizonScene owner-thread shaders/patch policies/stats, WorldScene lifecycle, manifest quality reader and `gfx::Device::gpuFrameMs()` backend contract. Re-read the added pure Stage40 config/state step, Horizon scene methods, manifest parser, WorldScene owner API and public controller diagnostics after successful GitHub commits. No local C++ working tree or runtime measurement was involved; PR was Draft at the last start-of-stage observation.

**NOT RUN:** native compilation (CMake, Clang, GCC, MSVC), CTest, unit timing fixtures, integration with actual frame loop, GLSL/Metal/Vulkan shader pipelines, real GPU timestamp provider, UI screenshot, PHYSIS world load, exact-wind crossfade comparison, device-loss/restart, CPU/GPU FPS benchmarks or CI run outcome validation. Do not assert production readiness, completed visual QA or measured adaptive gains. Keep PR #24 Draft; changes have not been merged to main.

### Risks and next specific test/development steps

1. A global CPU/GPU frame duration may be dominated by unrelated game systems; moving TerraWeave quality alone may not resolve the bottleneck. The host is responsible for appropriate timing samples; a zero metric means absent, not 0ms. Missing/gapped frame IDs deliberately break consecutive streaks.
2. Runtime policy switches obey Stage36's budget and Stage37/38 cache/shadow invariants. Particularly, moving to BatterySaver (0 GPU wind VB upload bytes) may defer restoring procedural RGB metadata, leaving safe macro wind presentation until a higher-bandwidth profile returns. This is not visual equality of all profiles, but does not mutate geometry.
3. Manual Stage39 overrides intentionally switch the adaptive controller OFF; re-enable/reset config to resume automatic changes. No host-level timing capture scheduler or platform-specific auto-GPU-query is installed by merely enabling adaptiveQuality in the manifest.
4. The separate QA agent should first migrate stale Stage29 single-layer impostor tests, then compile Stage31–40 and exercise: exact cutoff equality (strict >115%, <80%), 30/240 consecutive samples, cooldown before/after frame boundary, min/max level clamps, missing/zero/duplicate/reversed/gapped/implausibly long samples, CPU-only/GPU-only/worst of both, invalid bounds/enum, enable-disable-reset, direct and WorldScene manual opt-outs, manifest defaults/inherited BatterySaver, no-Horizon module, active/outgoing wind field crossfade, Stage36 budget starvation and Stage38 compressed GPU LRU restoration. Run real Metal/Vulkan/OpenGL CPU/GPU frame time probes, record exact CI/job URLs, result logs, captured screenshots and measured overhead in a new handoff. Do not merge PR #24 on source review alone.
5. Future feature work (only if needed after QA): integrate a supported host timing provider with completed GPU frame IDs and optionally isolate Horizon's own GPU cost from total frame time, but NEVER infer a GPU timestamp from CPU upload duration or assign a made-up FPS.

## Stage 41 — real RTS/FPS frame timing integration into adaptive TerraWeave Horizon (FEATURE SOURCE COMMITTED; TESTS DEFERRED)

**Date:** 2026-11-19. **Branch:** `feature/terraweave`. **PR:** [#24](https://github.com/chromealex/AIVERSE/pull/24), Draft, base `main`. At stage start GitHub compare reported `main` was ancestral (`behind_by=0`); no merge, rebase or force push performed. User approved the preceding Stage40 follow-up: wire the controller to real AIVERSE game frame measurements. The user continues **features first, tests by a separate agent later**.

### Exact implementation and documentation commits

- `9f5ad62aa77f5237017a5139333214fc9809dd7f`, small instrumentation/include follow-up `e90c29ac3476aadec9b6d46a4eb55a20567e20b3` — NEW `engine/world/TerraWeaveHorizonFrameTimingBridge.h`. Adds a reusable, genre-independent host-side `HorizonFrameTimingBridge`. `begin(device,WorldScene*)` checks the existing `vegetationHorizonAdaptiveQualityConfig().enabled`; disabled or absent Horizon means no sampling and existing pending records are cleared. When enabled, it increments an independent monotonic frame sequence, calls `gfx::Device::timeGpuFrame(tag)` during the currently active device frame (without stalls), collects asynchronous completions with `takeGpuTimings()` and binds them by their original 48-bit ID and reserved tag prefix, while `finish(scene,realFrameStart)` records actual steady-clock CPU callback elapsed microseconds. A short ordered deque holds GPU-pending samples for at most 8 frame IDs of lag before using CPU-only fallback (`gpuFrameUs=0`). Samples are submitted to Stage40 `WorldScene::submitVegetationHorizonFrameTiming` once, always in original increasing frame ID order. Invalid/non-finite/negative/>10s GPU times are rejected; missing GPU backend/timed-out completions do not block rendering. Diagnostics: `lastFrameId`, `pendingFrames`, `deliveredGpuFrames`, `deliveredCpuOnlyFrames`. `discard` clears outstanding local samples while preserving the monotonically advancing frame counter.
- `9c174f2004216a128bb42ff974d9985d26ed9491` — `apps/client/GameClient.h`: native RTS client's streamed-world owner stores a bridge beside `worldScene_`. No genre header dependency was added to the bridge.
- `096c12ea1269cb14ca833fdac8a7f188f992cdc0` — `apps/client/GameClient.cpp`: in an actual game frame, bridge `begin` is called before streamed WorldScene GPU uploads/render submission, and `finish` after the existing client UI/render work on the same device frame. Real CPU time starts at `GameClient::frame` entry (not clamped `dt`). Loading, planet/non-streamed scenes, shutdown and new match reset local pending timings. Platform already owns `beginFrame/endFrame` for the RTS callback; no duplicate device frame calls introduced.
- `f71f308db6615a4190e1c33294b27ea306c45bd7` — `apps/fps/client.cpp`: FPS App owns the same bridge; measured CPU callback begins at FPS frame entry. The bridge calls `begin` immediately after the FPS client's **existing** `device.beginFrame`; `finish` executes after the original `endPass/endFrame`, with shutdown discard. Existing FPS network, simulation, prediction, weapons, input and renderer code untouched.
- `204b98dc20f9525553b052e5aaeb6c0b02faa293` — `docs/terrain/TERRAWEAVE.md`: Stage41 measurement architecture, backend API semantics, RTS/FPS host ownership and caveats.
- `da4d1124f82e14e7d6d524370e6b7a808e9b0f61` — `skills/world/aiverse-terraweave/SKILL.md`: agent-facing instructions for reusable bridge, correct timing tags and device stream ownership.
- `d2d94c06d0380dcee1d23f628950f6c02c09a2c0` — root `AGENTS.md`: duplicate-frame/timer restrictions, no invented GPU time and deferred native QA.
- This Stage41 handoff commit is separate from all code/docs and is the final next-agent checkpoint.

### Public API, ownership and exact behavior

`terraweaveHorizon.adaptiveQuality.enabled` (Stage40, default false) remains the **only activation requirement** in existing native RTS and FPS clients. The newly wired game frame automatically submits actual frame timings when a streamed WorldScene with an adaptive Horizon is present; no extra manifest key or gameplay/controller-side call is needed. Other game projects may instantiate the bridge and bracket their own actual device frame with `begin`/`finish`, or continue calling Stage40's `submitVegetationHorizonFrameTiming` directly; **do not submit the same frame twice**. The bridge explicitly does not call `gfx::Device::beginFrame` or `endFrame`: its caller owns the device frame. It does not mutate any gameplay ECS component, engine physics, PHYSIS wind field, terrain source revision, GPU geometry, TWH8 disk archive, shaders or region bake data.

The existing `gfx::Device::timeGpuFrame(tag)` / `takeGpuTimings()` are already implemented for Metal and supported GL. Metal reports a completed command-buffer GPU interval (the full command buffer even if the tag was registered later in the callback); GL query scope begins when `timeGpuFrame` is invoked and ends at device `endFrame`. These are platform-defined intervals of the whole/partial game frame, **NOT isolated TerraWeave GPU cost**, and they may not be directly comparable cross backend. The CPU duration is the game-client callback wall time; RTS excludes platform event pump and outer `endFrame`; FPS includes its own `endFrame`. No universal same-frame GPU `gpuFrameMs()` association is assumed, because that API only returns the last asynchronously completed time without its matching frame ID.

The bridge polls already completed GPU tags on later frame submissions and outputs each measured frame once, matching its original frame ID. A GPU result that is absent/unsupported/invalid or delayed by more than eight subsequent frame IDs is dropped; the original measured CPU duration is used and GPU=0. Duplicate/late matching results cannot overwrite an already delivered sample. `HorizonAdaptiveQualityConfig` still controls hysteresis, sustained sample thresholds, cooldown and min/max Stage39 presentation quality level. Its published current level and manual-override disabled behavior are untouched.

**Critical device timing concurrency contract:** `takeGpuTimings()` is a destructive *whole-device* drain. Static GitHub code search found its current call sites in benchmarks, editor SceneWindow and test tools but no competing collectors in the two native GameClient/FPS frame paths. A future feature that adds benchmark GPU tag polling to the same device/client must route all GPU tags through one shared collector, not call `takeGpuTimings()` independently (or it may steal another consumer's results). The bridge notes this explicitly. Other unsupported GPU backends still have native measured CPU adaptation without device timer queries; no new backend code was added.

### Actual verification vs unrun checks

**Actually done:** Read Stage40 current branch handoff and APIs; inspected actual WorldScene usage in native GameClient and `apps/fps/client.cpp`; inspected `gfx::Device::GpuTiming`, `timeGpuFrame`, `takeGpuTimings`, `gpuFrameMs` and `frameSerial`; inspected Metal async command-buffer timing callback and supported GL GL_TIME_ELAPSED query lifecycle; searched for other native query consumers. Successful GitHub writes reported the exact code/doc/skill SHAs listed above. After code writes, fetched the new timing bridge and integration locations to verify signature/ownership placement and absence of duplicate graphics frame calls. **No local native build was executed.**

**NOT RUN:** CMake configure/build, Clang/GCC/MSVC, CTest, Linux/macOS/Windows native RTS/FPS game run, GPU timing callbacks on actual Metal/OpenGL/Vulkan/other backend, real wall-clock FPS/battery measurements, shader compilation, UI screenshots, end-to-end stream/HLOD fade quality, controller switching under measured load, stress memory or device-loss simulation. Do not assert successful compilation, working GPU tag correlation on hardware, exact FPS gain, fixed jitter or production readiness; PR #24 remains Draft.

### Known limitations, blockers and concrete next step

- The GPU tag API belongs to the entire device; a second independent `takeGpuTimings` consumer would conflict. Add a shared tagged-query router before introducing additional GPU timing subscribers in these clients, or test against known single-consumer use only.
- Eight-frame pending timeout is an explicit nonblocking bounded policy; a severely delayed GPU completion falls back to CPU time and may bias the controller toward CPU-only decisions. CPU callback time covers rendering submission and main-thread work but not necessarily vsync/present/pump; Metal and GL measure different GPU intervals.
- Stage40's controller responds to global frame cost, not exclusively Horizon cost. If some unrelated RTS/FPS system dominates the frame, reducing far-forest presentation quality may not improve performance; that needs real benchmarks.
- **Next QA agent action (priority):** fix Stage29 old 1-primitive impostor tests, compile Stage31–41 together; run CPU-only bridge with unsupported Device, GPU tag collision/ordering/timeout probes, fully tagged Metal and supported GL real frames, adaptive enabled/disabled manifests, multiple WorldScene lifecycles and loading/match changes, CPU/GPU time mismatch, editor benchmark conflicts if a shared Device is used, FPS's own begin/end ownership and RTS platform frame ownership. Then collect cross-platform GPU/CPU perf and screenshots; record exact commands, CI run links and outcomes in a separate handoff before considering PR #24 merge. Continue feature work only for verified missing capability (e.g. a common device GPU timing router), never invent pass/failure results.

## Stage 42 — shared per-Device GPU timing router for TerraWeave, editor and render benchmarks (SOURCE COMMITTED; TESTS DEFERRED)

**Date:** 2026-11-19. **Branch:** `feature/terraweave`. **PR:** [#24](https://github.com/chromealex/AIVERSE/pull/24) Draft → `main`. At the beginning of the stage `main` was ancestor (`behind_by=0`), so no merge/rebase/force push required. User explicitly approved this next feature and continues to defer native compilation/CTest/GPU validation to a separate testing agent.

### Actual implementation commits and changed files

- `6e15307e1c4283b59d1fa8830d52a2cdf478706c` plus `a05285c60f495ee1cf4f8a62cbecfca45fdaf555` — NEW `engine/gfx/GpuTimingRouter.h`: `gfx::GpuTimingRouter` is a genre-independent single render-owner-thread collector and bounded distributor. Call `subscribe()` (max 32), `request(device,subscriber,clientTag)` (up to 64 in-flight requests), `poll(device)` (THE shared destructive backend `takeGpuTimings` call), `take(subscriber)` (up to 64 results per subscriber), `cancel(subscriber,clientTag)`, `unsubscribe(subscriber)`. Assigns unique device-private raw tags with prefix `0xD17A` and returns client-original tags in isolated per-consumer mailboxes; out-of-order GPU completion is retained with original subscriber. Unknown raw tags and overflowing same-owner completed queues increment diagnostics, not a neighboring owner's sample. No GPU waits, memory readbacks, added shader or GPU backend change.
- `3e9cad9e03b1d37dd94eae679e0c046f8c79c3c9` — `engine/gfx/Gfx.h`: includes shared router and embeds one `GpuTimingRouter` directly inside each `gfx::Device`, with `gpuTimingRouter()` reference API, ensuring no global singleton or cross-device lifetime collision.
- `7e548153fa90c8246c284545e82403a06db53372` and `7de5178533cdfc976dca2553a87e60d1b15c17b8` — `engine/world/TerraWeaveHorizonFrameTimingBridge.h`: replaces direct `Device::timeGpuFrame` / `Device::takeGpuTimings` with a private subscriber on the shared device router. Preserves Stage41 original frame ID, bounded 8-frame asynchronous GPU wait and CPU measured fallback. Explicit `detach(device)` unsubscribes at shutdown/disabled adaptation; timeout now cancels only this bridge's abandoned router mapping. No change to runtime sampling, scene geometry or PHYSIS.
- `4de8ffb0dca8630dbcbcf9a8de7ce359ff30da3d` and `fb9229137f698368801d95a4a91de891e1c55a23` — `apps/client/GameClient.cpp` and `apps/fps/client.cpp`: detach their WorldScene timing subscriptions on RTS world/match teardown and FPS shutdown. Stage41 begin/finish points are unchanged. They do NOT create duplicate graphics frames.
- `5bcc35acc63674a7e483c93384a149e72b61df0b` plus device-owner recreation guard `5a10ac4283b4b4cca764e9f1b02a518811160714` — `apps/editor/windows/SceneWindow.cpp`: existing Scene-batching benchmark calls the router for tag requests and receives ONLY matching subscriber samples. It preserves original `0xA17E` tag generation/mode encoding, Metal full-command-buffer timing labels and CPU-mode benchmark behavior. The owner pointer guard resets subscription when the graphics device instance changes, rather than using an ID assigned by a prior device.
- `ed3b6bafe74c7f2d76c1db7fddcf743345a5bc8e` — `apps/renderbench/Bench.h`: replaces direct GPU tag collect/request with subscriber mailbox. Original benchmark frame tags and report IDs remain unchanged; subscriber unsubscribes during shutdown.
- `3d320adc9cc21574bfcc2b998dbf63d718c2615a` — `tools/geometrybench/main.cpp`: same per-device request/poll/take migration for geometry perf report, preserving frame IDs.
- `0350ced1a89006ee6c6460a03f222d8b7229e77f` — `tests/geometry/instance_preview.cpp`: same migration for GPU timing preview, with unsubscribe at shutdown; existing gameplay/model logic untouched.
- `64674249a24c7031d314e8b4a9d6d815cb7d3910` — NEW generic `docs/render/GPU_TIMING_ROUTER.md`: stable API, ownership, limits, backend semantic limitations, diagnostics and future testing.
- `9a4ff0ca0a0ee8ee8f898b54bffbd937cef483de` — `docs/terrain/TERRAWEAVE.md`: Stage42 migration and Stage40 QoS compatibility.
- `d3b298366c5aff55b96bebfffa1726c5307729c2` — `skills/world/aiverse-terraweave/SKILL.md`: updated per-agent usage and no-direct-device-drain rule, reusable by Prisma/VFX systems.
- `9e5bdad3c387c8b27276440b3874a1e31057d506` — root `AGENTS.md`: cross-agent contract that all GPU timing consumers use a shared per-device router; no duplicate GPU collection loops.
- This separate Stage42 handoff commit is the mandatory final stage artifact.

### Architecture/API / compatibility contract

AIVERSE already had `gfx::Device::timeGpuFrame(tag)` and `takeGpuTimings()`; the latter destructively consumes the entire device-wide result queue. Previously Stage41's TerraWeave bridge and editor/render benchmarks would race when they used a shared Device. The Stage42 router gives every consumer an independent subscription ID and maps a globally unique internal raw tag back to `{subscriber,originalClientTag}`. `GpuTimingRouter::poll(device)` alone calls the destructive raw method and buffers each known result in a bounded per-subscriber mailbox. `take(owner)` can **never steal another owner's completed measurement**. Request rejection is nonblocking (unsupported GPU timing, GL busy, per-router capacity); clients retain CPU-only/unsupported behavior rather than waiting. `cancel` and `unsubscribe` drop only that owner's metadata, while a late backend completion becomes an unowned result safely ignored. This is a **shared RHI service**, not TerraWeave weather physics or a genre-specific GPU profiler.

Usage:

```cpp
auto& router=device.gpuTimingRouter();
const auto owner=router.subscribe();
if(owner && router.request(device,owner,myFrameTag)) {
    // Continue rendering without blocking GPU completion.
}
router.poll(device);                 // on future Device owner-thread frames
for(const auto& sample:router.take(owner))
    if(sample.valid)consume(sample.tag,sample.milliseconds);
router.unsubscribe(owner);          // before owner lifetime ends
```

**Strict scope and limits:** 32 active owners, 64 in-flight GPU queries, 64 delivered-but-unconsumed results per owner, one owning render thread. A `GpuTimingRouter` lives as part of a `gfx::Device` and is discarded when that device is destroyed. The backend maintains independent timing limits: current GL33 allows **one concurrent `GL_TIME_ELAPSED` query** and may reject a second request in the same frame, while Metal returns the completed **whole GPU command buffer** duration for any tag on that command buffer. The router solves cross-consumer delivery, **NOT** simultaneous independent per-pass GPU timing, GPU utilization, power draw or FPS. Existing gfx backend implementations, shader sources, GPU buffer lifetimes, vertex format, terrain geometry, Horizon TWH8 bake, PHYSIS weather, authoritative ECS/gameplay/network state and native project mechanism composition remain untouched.

**Migration coverage:** Source code scan of main-before-stage showed direct `takeGpuTimings` consumers in SceneWindow, RenderBench, GeometryBench, instancing preview, and Stage41's TerraWeave bridge, in addition to the RHI implementations. All known listed consumers are now routed. Future native game/plugin modules (e.g. Prisma, SpectraFX/VFX) MUST use the generic `gfx::Device::gpuTimingRouter()` instead of adding new direct destructive polls. Existing benchmarks retain their original tag semantics (router translates). Unsupported backends can return false. Device owner thread must call `router.poll(device)` regularly while expecting GPU results. No background polling or worker-thread submission is allowed.

### Actually verified, unrun tests, risks and next action

**Source inspection actually performed:** Read feature-branch `Gfx.h`, Stage41 `TerraWeaveHorizonFrameTimingBridge.h`, editor SceneWindow benchmarking code, `apps/renderbench/Bench.h`, geometrybench and preview timing consumers; searched direct timeGpuFrame/takeGpuTimings usage, inspected Metal and GL backend timing semantics from prior stage, and fetched changed source segments after successful GitHub code writes. GitHub returned the exact code/documentation/skill commit SHAs listed above. No `main` merge.

**NOT RUN:** native CMake configure/build, Clang/GCC/MSVC compilation, CTest, unit/router fixtures, FPS/RTS native client startup, editor batching benchmark, RenderBench, GeometryBench, Metal/GL/Vulkan asynchronous completion, simultaneous queries, race/timeout stress, screenshot captures, CPU/GPU timing comparisons or performance measurement. No claim of passing tests, working GPU telemetry on device, independent per-feature GPU cost, or production readiness. PR remains Draft until independent QA.

**Known limitations:** (1) The generic router still consumes all raw backend results when polled. A legacy client that ignores the new API and independently calls `takeGpuTimings` can still steal results; all shared-Device clients must cooperate. (2) Backend timing may reject requests because Metal/GL/Vulkan capabilities differ; GL exclusive query limitation is not removed. (3) Completion order is backend-specific; router delivers each owner its samples by GPU completion order, with original client tags for manual ordering. (4) Abandoned/canceled metadata is bounded, but the backend may continue holding an outstanding GPU query until its own completion/device recovery. (5) Only the render owner thread may register/poll/consume/cancel; no worker-thread API is provided. (6) Editor SceneWindow currently retains its subscriber during its window's lifetime; the device recreation guard resets the ID when the associated device changes. Other long-lived tools should unsubscribe explicitly before destruction. (7) The Stage41 Horizon bridge uses only supported real timing sources; no GPU query when adaptation is OFF.

**Next independent QA agent step:** validate Stage42 with a fake Device that returns intentionally out-of-order tagged completions for two consumers (without relying on real GPU hardware), check mailbox isolation, same original client tag across subscribers, cancel/late completion, unsubscribe, repeated subscribe across device recreation, 32 subscribers/64 pending/64 per-mailbox overflow, unsupported query returns false, and GL busy semantics. Then compile actual renderer and native RTS/FPS/editor/RenderBench/GeometryBench/preview on Clang/GCC and run Metal/GL/Vulkan profilers. Verify all Stage29–41 deferred atlas/wind/shader/stream and QoS tests, record commands and actual CI run links in a new handoff and fix errors before PR #24 can merge. Do not run unrequested tests in this feature-first stage or claim passing validation.

## macOS Clang QA fix — portable atomic ForestScene snapshot publication (2026-11-19)

**Branch:** `feature/terraweave`. **Source commit:** `7095aceecc9e9668c7bd2b7b0528d001e584e973`; file `engine/world/TerraWeaveForestScene.h`.

**Issue actually reported by the Mac tester:** Apple Xcode/macOS26.2 libc++ rejects `std::atomic<std::shared_ptr<const GenerationSnapshot>>` with an internal `_Atomic requires trivially copyable` diagnostic when compiling `terraweave_forest_scene_test`. The project requests C++20 but this SDK/toolchain does not provide a compatible atomic shared_ptr specialization.

**Fix implemented:** store the member as `std::shared_ptr<const GenerationSnapshot> asyncSnapshot_` and access it **exclusively** via standard `std::atomic_store_explicit` (memory_order_release) and `std::atomic_load_explicit` (memory_order_acquire) free functions, including shutdown's typed empty pointer. The two release publications, one acquire worker read and shutdown release were all updated. No plain concurrent read/write is introduced. The snapshot is still immutable and ref-counted for worker lifetime. No API, generation revision, ECS/PHYSIS, render, archive or game behavior changes intended.

**Verification:** exact source pattern counts checked prior to source replacement and GitHub commit returned successfully; recompile/test on user machine **NOT RUN by this agent**. Next local action: pull `feature/terraweave`, run `cmake --build build/terraweave-qa --parallel 4 --target aiverse_terraweave_forest_scene_test`, then `ctest --test-dir build/terraweave-qa --output-on-failure -R '^terraweave_forest_scene$'`. If further failures, capture full compile/CTest logs and report the first error; never mark test passing without actual output. Keep PR #24 Draft.

## Mac Studio Horizon Clang QA fix — near seam lambda / portable atomic shared_ptr (2026-11-19)

**User-provided actual failure log:** `cmake --build build/terraweave-qa --parallel 4 --target aiverse_terraweave_horizon_test` under Mac Studio Xcode/macOS26.2 SDK stopped while compiling `tests/terraweave_horizon_test.cpp`. `engine/render/TerraWeaveHorizonClip.h:218` declared `const discardIncompleteSamples=[&bake]() { ... }` with no `auto` (hard C++ error); same file around line 309 had `const auto slope=[scale]` where `scale` is a local `constexpr` and clang emitted `-Wunused-lambda-capture`. `engine/render/TerraWeaveHorizonScene.h:1118` declared `std::atomic<std::shared_ptr<const SourceGeneration>>`, which this Apple libc++ rejects because the atomic specialization is not available. CTest subsequently printed `Unable to find executable`; this was **Not Run** due to failed compilation, not a failing test assertion.

**Fix source commits:**
- `30679da09fe7311e254251e2a8ea8f265db96b0b` — `engine/render/TerraWeaveHorizonClip.h`: `const auto discardIncompleteSamples=[&bake]() { ... }` and remove the unnecessary `[scale]` capture from the non-capturing slope lambda (`scale` is a compile-time integral constant used directly).
- `15c134077a8ae8f84b9e9f597e09d8ae5e721233` — `engine/render/TerraWeaveHorizonScene.h`: convert `sources_` to `std::shared_ptr<const SourceGeneration>` and use `std::atomic_load_explicit(&sources_,std::memory_order_acquire)` at BOTH worker reads, plus `std::atomic_store_explicit(&sources_,std::shared_ptr<const SourceGeneration>(generation),std::memory_order_release)` at publication. Retains immutable snapshot ownership, shared_ptr reference accounting and acquire/release synchronization. Explicit const conversion is necessary for free-function template deduction; no plain concurrent accesses remain.

**Actually verified:** GitHub source read and source-write commits; read-back source confirms `const auto` lambda, no unnecessary capture, both atomic load sites and one atomic store; no remaining specialized atomic shared_ptr field in HorizonScene. **NOT RUN by agent:** Mac compilation after fix, CTest, GPU shader validation, engine integration, runtime visual tests. PR #24 remains Draft; do not assert those tests pass until tester provides output.

**Next command on isolated user worktree (always fetch/pull the feature branch, no interaction with main repo's rebase):**

```bash
cd ~/Projects/AIVERSE-terraweave-qa
git fetch origin feature/terraweave
git pull --ff-only origin feature/terraweave
cmake --build build/terraweave-qa --parallel 4 --target aiverse_terraweave_horizon_test
ctest --test-dir build/terraweave-qa --output-on-failure -R '^terraweave_horizon$'
```

If compilation fails again, capture the **first primary compiler error**, not just the secondary CTest "Unable to find executable" output. Keep the QA worktree separate from any conflicted PHYSIS rebase.

## Mac Studio Horizon QA fix — conservative HLOD triangle budget test (2026-11-19)

**User-provided, actually executed local QA result:** after pulling `f2165994957f` into the isolated `~/Projects/AIVERSE-terraweave-qa` worktree, `cmake --build build/terraweave-qa --parallel 4 --target aiverse_terraweave_horizon_test` **succeeded**. The executable linked without the earlier lambda / Apple libc++ errors. The subsequent `ctest --test-dir build/terraweave-qa --output-on-failure -R '^terraweave_horizon$'` **failed 0/1** with `Assertion failed: (budget.maxTriangles<=settings.terrainResolution*settings.terrainResolution*2+budget.maxForestClusters*4)`, `tests/terraweave_horizon_test.cpp:39`. This log supersedes an older pasted build log containing the already-fixed lambda and atomic shared_ptr errors.

**Diagnosis:** the old test assumed the conservative triangle *upper bound* included only base ground quads (`resolution²*2`) plus 4 crossed-card triangles per forest cluster. The actual `estimateHorizonBudget` in `engine/world/TerraWeaveHorizon.h` conservatively includes additional optional skirt triangles (`8*terrainResolution`), up to 512 seam inner-lip edges (2 triangles/edge = 1024), and `maxNearClipTriangles` for near-owned ground clipping. Thus the previous `<=` test assertion was stale and contradicted intentional geometry feature budgets, not evidence that the production generator was over-budget. The next default test (`defaults.maxTriangles<10000`) was stale for the same reason: its exact conservative default is 4,608 ground + 384 skirt + 1,024 inner lip + 2,048 near clip + 3,328 forest = **11,392** triangles.

**Fix source commit:** `a1128babccd4ca3daff13a6dd32c3ea331f35285`, changed **only** `tests/terraweave_horizon_test.cpp`: introduce test-local `expectedMaximumTriangles(settings,clusters)` modelling the documented independent ground/skirt/lip/clip/forest caps, assert `estimateHorizonBudget` equals the formula for a custom test config and for defaults, and assert the default value 11,392. No changes to `HorizonSettings`, the renderer, near-ground seam geometry, clipping algorithm, authored data, PHYSIS, ECS or TWH8 archive. The updated test is stronger (checks exact conservative component accounting) than the obsolete inequality.

**Actually verified:** fetched GitHub feature-branch test/production budget source, calculated and independently compared component accounting, wrote the test fix and received source commit SHA. **NOT RUN after this commit:** local Clang compile, CTest or GPU visual test; do not claim assertion fixed in execution until user supplies actual new output. PR #24 remains Draft.

**Next local command (always pull the feature branch, isolated worktree only):**

```bash
cd ~/Projects/AIVERSE-terraweave-qa
git fetch origin feature/terraweave
git pull --ff-only origin feature/terraweave
cmake --build build/terraweave-qa --parallel 4 --target aiverse_terraweave_horizon_test
ctest --test-dir build/terraweave-qa --output-on-failure -R '^terraweave_horizon$'
```

On a further failure, capture and report the *first new failing assertion/compile diagnostic*, not the already-superseded errors in the older attached log. The deferred all-TerraWeave CTest and RTS/FPS/editor full build still require independent validation.

## Mac Studio Horizon QA fix — skirt-aware model vertex count, plus separate local rebase warning (2026-11-19)

**Actual user-provided QA:** local `~/Projects/AIVERSE-terraweave-qa` compiled and linked `aiverse_terraweave_horizon_test` after the previous conservative triangle-budget fix, but CTest `terraweave_horizon` failed `tests/terraweave_horizon_test.cpp:120`: `model->vertices.size() < (13*13)+bake.clusters.size()*8+8`. This is the next obsolete Stage29-era assertion, not a production crash.

**Diagnosis:** `makeHorizonModel` explicitly adds `4*(terrainResolution+1)` bottom skirt vertices when `terrainSkirtDepthCm>0`, optional near-ownership inner-lip vertices (up to 1024) and clipped triangle vertices (up to `3*maxNearClipTriangles`). The old test allowed just 8 extra vertices over base terrain + 8 vertices per forest cluster; for the tested resolution 12, the legitimate skirt alone adds 52 vertices. `estimateHorizonBudget(settings).maxVertices` already includes these additional conservative components.

**Actual source fix:** commit `36479dc3b5ae70f8bcf0a7bec69055154af7a9c1`, changes only `tests/terraweave_horizon_test.cpp`: checks exactly two ordered material primitives; ground vertex range begins at 0, forest follows the complete ground range; forest primitive has exactly `8*bake.clusters.size()` (two crossed cards per cluster), the whole vertex vector equals the end of the forest primitive, it includes at least the base terrain + forest, and it stays **within the configured conservative `budget.maxVertices`**. This preserves substantive assertions and models true skirt/lip/clip behavior rather than inflating an arbitrary tolerance. Production geometry, PHYSIS/ECS/weather, archives and shaders unchanged.

**Actually checked:** GitHub source inspections of `makeHorizonModel` terrain, skirt, lip and canopy construction, `estimateHorizonBudget` and exact failed test. GitHub write commit returned; native CTest after this fix **NOT YET RUN**.

**Separate local Git state:** user reports a recurrent local Git rebase while multiple feature agents work on AIVERSE. This is separate from the TerraWeave test assertion. **Do not** rebase the shared `~/Projects/AIVERSE` checkout to fetch TerraWeave; use the isolated QA worktree `~/Projects/AIVERSE-terraweave-qa` for `git fetch origin feature/terraweave` + `git pull --ff-only origin feature/terraweave`. Never run `git rebase --continue`, `--skip`, `reset --hard` or `clean -fd` blindly on unrelated PHYSIS/SpectraFX changes. Inspect `git status -sb`, `git worktree list` and any rebase state in each checkout independently; if a rebase is running, finish/abort it only after preserving intended conflict resolutions and local commits. No local Git operations were performed by the agent.

**Next QA:** pull latest TerraWeave into the isolated QA worktree, build `aiverse_terraweave_horizon_test`, rerun `ctest --test-dir build/terraweave-qa --output-on-failure -R '^terraweave_horizon$'`; record further failing assertions verbatim. Keep PR #24 Draft until full native tests and graphics acceptance.

## Mac Studio QA verified — Horizon test pass after vertex-layout assertion correction (2026-11-19)

**Actually executed by user (not inferred from GitHub CI):**
- In isolated detached QA worktree `/Users/aleksandrfeer/Projects/AIVERSE-terraweave-qa`, fetched/pulled `feature/terraweave`, fast-forwarded from `5b51c2779` to `561a85f87`.
- `cmake --build build/terraweave-qa --parallel 4 --target aiverse_terraweave_horizon_test` **PASSED**: dependencies `aiverse_zstd`, `aiverse_meshoptimizer_lod`, `aiverse_engine`, `aiverse_pack`, `aiverse_world`, `aiverse_models` were built; test C++ compiled and linked successfully.
- `ctest --test-dir build/terraweave-qa --output-on-failure -R '^terraweave_horizon$'` **PASSED 1/1**: `Test #101 terraweave_horizon`, 0.92 s; 0 tests failed. This confirms the updated triangle budget and skirt-aware vertex assertions no longer abort the tested code path on the user's macOS/Xcode toolchain.
- Separately, the user already reported `terraweave_forest_scene` **PASSED 1/1**, 1.00 s on the same QA worktree, after the portable atomic shared_ptr fix.

**Main worktree caution:** `~/Projects/AIVERSE` returned `## HEAD (no branch)`, unresolved merge entry `UU docs/agent-handoffs/universal-niagara-vfx-2026-11-18.md`, and untracked `build-metal/`. This is a completely separate interrupted rebase with another agent's SpectraFX/PHYSIS changes. Do not touch or reset it as part of TerraWeave tests; do not infer it was fixed.

**Remaining QA:** The two named suites above are confirmed green, **not the entire TerraWeave suite**. Next compile the 9 TerraWeave targets documented by `.github/workflows/terraweave.yml` using the isolated QA worktree; run `ctest --test-dir build/terraweave-qa --output-on-failure -R '^terraweave(_arbor|_flora|_model|_heightmap|_ops|_preview|_forest_scene|_horizon)?$'`. Capture any first failing assertion and fix on `feature/terraweave`. Native RTS/FPS/editor integration and GPU Metal/Vulkan/GL are still **NOT RUN**. Keep PR #24 Draft.

## Mac Studio all-TerraWeave CTest: 8/9 passed; two-layer Arbor impostor test correction (2026-11-19)

**Actual user-local QA:** in `~/Projects/AIVERSE-terraweave-qa`, all nine named C++ test targets compiled and linked; running `ctest --test-dir build/terraweave-qa --output-on-failure -R '^terraweave(_arbor|_flora|_model|_heightmap|_ops|_preview|_forest_scene|_horizon)?$'` returned **8/9 pass, 1/9 failure** in 3.57 seconds. Passed: `terraweave`, `terraweave_arbor`, `terraweave_flora`, `terraweave_heightmap`, `terraweave_ops`, `terraweave_preview`, `terraweave_forest_scene`, `terraweave_horizon`. Failed: `terraweave_model` (#96) with `Assertion failed: (billboard->indices.size() == 6 && billboard->vertices.size() == 4)`, `tests/terraweave_model_test.cpp:106`.

**Diagnosis:** `ArborForestAdapter::addPrototype` constructs `makeArborImpostorAtlas`, not the single-quad legacy `makeArborBillboardModel`. `engine/render/TerraWeaveImpostorAtlas.h` now deliberately creates *two* overlapping 4-vertex, 6-index cards, one for bark and one for seasonally hideable foliage, with **two alpha-masked materials and two primitives** but still **one mesh, one node, one RGBA8 atlas image**. The image has 8 viewpoints per layer, total `128*8*2=2048` pixels wide, 256 high. The old model test asserted only one quad and atlas width `128*8`, so it would fail more than once despite correct production logic.

**Fix source commit:** `eb8ca7455dba186d848ea49b6f143a6880b7b3e2`, changed `tests/terraweave_model_test.cpp` only. Assert exactly 8 vertices, 12 indices, 2 materials, 2 material primitives with non-overlapping index/vertex ranges, correct alpha-mask/no-shadow semantics for each, one atlas image sized 2048x256, and both bark/foliage view-material slices are present and in their respective atlas halves with monotonically increasing offsets. Preserves tests for texture coverage, determinism, near tree model/LOD, draw IDs, yaw and invalid input handling. No generator, rendering, ECS, PHYSIS or VFX production code changed.

**Actually verified by agent:** read source of test, `TerraWeaveForestAdapter.h`, `TerraWeaveImpostorAtlas.h` and `TerraWeaveArborBillboard.h`, committed source test correction. **NOT YET RUN after this commit:** local C++ build / CTest, real graphics tests. PR #24 remains Draft; never claim 9/9 until tester supplies a new result.

**Next QA command:** in isolated `~/Projects/AIVERSE-terraweave-qa`, `git fetch origin feature/terraweave && git pull --ff-only origin feature/terraweave`; compile `aiverse_terraweave_model_test` then rerun `ctest --test-dir build/terraweave-qa --output-on-failure -R '^terraweave_model$'`. If successful, rerun all nine named TerraWeave tests. Continue keeping main checkout's unrelated rebases/merge conflicts untouched.

## Mac Studio isolated TerraWeave QA complete — all nine CPU tests green (2026-11-19)

**Actual user-reported complete result, after fixing the split bark/foliage impostor test:** `cmake --build build/terraweave-qa --parallel 4 --target` (nine TerraWeave targets) **succeeded**. `ctest --test-dir build/terraweave-qa --output-on-failure -R '^terraweave(_arbor|_flora|_model|_heightmap|_ops|_preview|_forest_scene|_horizon)?$'` in the isolated macOS `~/Projects/AIVERSE-terraweave-qa` worktree passed **9/9, 0 failures, 1.01 seconds**. Exact tests:
- #93 `terraweave`: PASSED 0.04s
- #94 `terraweave_arbor`: PASSED 0.03s
- #95 `terraweave_flora`: PASSED 0.03s
- #96 `terraweave_model`: PASSED 0.72s
- #97 `terraweave_heightmap`: PASSED 0.03s
- #98 `terraweave_ops`: PASSED 0.03s
- #99 `terraweave_preview`: PASSED 0.03s
- #100 `terraweave_forest_scene`: PASSED 0.04s
- #101 `terraweave_horizon`: PASSED 0.05s

This validates the native CPU suites including the Stage29 legacy assertions updated in `terraweave_horizon_test.cpp` (`a1128bab`, `36479dc3`) and `terraweave_model_test.cpp` (`eb8ca745`). **No further code changes were necessary in this verification stage**. Independent Linux CI, full native Editor/RTS/FPS client integration build, optional `terraweave_editor` integration test, Metal GPU rendering and performance tests have **NOT** been reported/run at this stage.

**Git safety:** the main `~/Projects/AIVERSE` checkout still has an unrelated VFX/PHYSIS rebase conflict as previously reported; use only `~/Projects/AIVERSE-terraweave-qa`, whose pull from `origin/feature/terraweave` fast-forwarded successfully. Do not automatically rebase, reset or clean the main checkout.

**Next step: native integration build in a separate build directory (avoid changing current tested configuration):** fetch/pull `feature/terraweave` in the QA worktree; configure `build/terraweave-integration` with `AIVERSE_BUILD_CLIENT=ON`, `AIVERSE_BUILD_FPS_CLIENT=ON`, `AIVERSE_BUILD_EDITOR=ON`, `AIVERSE_BUILD_TESTS=ON`, server/headless/tools/FBX OFF; compile `aiverse_client`, `aiverse_fps_client`, `aiverse_editor`, and `aiverse_terraweave_editor_test`, then run `ctest --test-dir build/terraweave-integration --output-on-failure -R '^terraweave_editor$'`. The actual CMake targets and options were inspected in `CMakeLists.txt` and `modules/fps/CMakeLists.txt`. Capture the first actual compile error on Mac and keep PR #24 Draft until integration and graphics acceptance.


## Stage 43 — shared GPU timing router CPU regression and CI coverage (2026-11-19)

**Owner / branch / PR:** TerraWeave agent, `feature/terraweave`, Draft PR #24 to `main`.
**Feature baseline:** Stage42 introduced `gfx::GpuTimingRouter`; no RHI behavior, graphics backend, Horizon geometry, ECS, PHYSIS or live gameplay update is intentionally changed here.

**Actual source commits and paths:**
- `35d4b9c10fe76862ccdabff4589f214e1427aeff` — NEW `tests/gpu_timing_router_test.cpp`, pure C++20 fake GPU backend fixture.
- `a2e2d3c5e9d223c64084aeddcc2d634052d4b2d1` — `CMakeLists.txt`, adds `aiverse_gpu_timing_router_test` and CTest `gpu_timing_router`; assertions explicitly enabled on AppleClang/Clang/GCC Release.
- `97d3e99739cb2913d57820ebfed48ffe979cbfba` — `.github/workflows/terraweave.yml`, builds the router fixture in isolated GCC/Clang matrix and runs it with the existing nine TerraWeave CPU cases.
- `e717646a54c680cb37104748faf8b7b47efbb241` — `docs/render/GPU_TIMING_ROUTER.md`, hardware-free regression coverage; this handoff and the AI skill are updated in following source commits.

**Scenarios encoded:** (1) two independent subscribers with identical original tags and unique raw device tags; (2) out-of-order replies; (3) valid vs invalid measurements, independent mailbox `take()`; (4) cancellation/unsubscription, ignored late completions and non-reuse of subscriber IDs; (5) 32-owner and 64-pending capacity; (6) 64-entry mailbox drop-oldest accounting; (7) rejected unsupported/busy GPU requests without creating phantom pending requests; (8) distinct device/router lifetimes. The test uses an intentionally fake `timeGpuFrame/takeGpuTimings` backend, never requires graphics hardware or Vulkan/Metal/GL runtime.

**Verification performed:** GitHub returned new source commits and read-back confirmed the new test file and CMake registrations. **NOT RUN in agent runtime:** CMake configure, Clang/GCC compilation, the new `gpu_timing_router` CTest, macOS integration/editor/native game/graphics tests, profiler, screenshots, performance measurements. Existing 9/9 TerraWeave CPU green result was reported by the Mac Studio user on the previous head; do not attribute that result to this new revision or count the new case as passed. GitHub's commit workflow query returned no PR-triggered runs for the queried docs commit; that is not evidence of a successful CI build.

**Branch integration warning:** Current `main` has moved significantly since TerraWeave's feature base (last checked: 605 commits ahead / 370 behind and PR mergeability `dirty`). Do not merge this Draft PR or run a destructive rebase in the user's shared `~/Projects/AIVERSE` checkout. That checkout was previously reported to contain an unrelated VFX/PHYSIS rebase conflict. Use the isolated `~/Projects/AIVERSE-terraweave-qa` worktree and coordinate integration/reconciliation separately.

**Next independent QA commands** (in the existing isolated macOS worktree, after confirming no local rebase):
```sh
cd ~/Projects/AIVERSE-terraweave-qa
git fetch origin feature/terraweave
git pull --ff-only origin feature/terraweave
cmake --build build/terraweave-qa --parallel 4 --target aiverse_gpu_timing_router_test
ctest --test-dir build/terraweave-qa --output-on-failure -R '^gpu_timing_router$'
```
Then configure a **separate** `build/terraweave-integration` with `-DAIVERSE_BUILD_CLIENT=ON -DAIVERSE_BUILD_FPS_CLIENT=ON -DAIVERSE_BUILD_EDITOR=ON -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_TOOLS=OFF -DAIVERSE_BUILD_SERVER=OFF -DAIVERSE_BUILD_HEADLESS=OFF -DAIVERSE_FBX=OFF`; build `aiverse_client aiverse_fps_client aiverse_editor aiverse_terraweave_editor_test`; run CTest `terraweave_editor`. Record exact first C++/link error and fix feature-branch source only. After native integration, run real Metal graphics screenshot/performance and inspect GPU timing behavior; do not infer real GPU correctness from fake backend tests. Keep PR Draft.


## Stage 43 QA follow-up — CMake cache did not contain the new GPU router target (2026-11-19)

**Actual tester output:** after successful `git fetch origin feature/terraweave` and `git pull --ff-only` (`Already up to date`), the command `cmake --build build/terraweave-qa --parallel 4 --target aiverse_gpu_timing_router_test` stopped with `make: *** No rule to make target 'aiverse_gpu_timing_router_test'. Stop.`. The following `ctest --test-dir build/terraweave-qa -R '^gpu_timing_router$'` printed `No tests were found!!!`.

**Diagnosis, bounded to observed evidence:** GitHub branch source inspection confirms that `CMakeLists.txt` registers the new executable and CTest when `AIVERSE_BUILD_TESTS=ON` and `NOT EMSCRIPTEN`, and `tests/gpu_timing_router_test.cpp` exists. The local `build/terraweave-qa` build tree therefore does not currently contain that target: it has not been regenerated for this source/configuration, or its cache disables tests, or it belongs to a different CMake source root. This is **NOT** a compiler failure, failing assertion, or evidence of a source defect. The exact local CMake cache was not provided and the precise cause among these alternatives was not confirmed. Separate NEREID `ctest-full exit=0` applies to NEREID only.

**Corrected non-destructive Mac Studio invocation** (new isolated build tree, preserving the prior 9/9 CPU-tested tree and untouched shared main worktree):
```sh
cd ~/Projects/AIVERSE-terraweave-qa
git fetch origin feature/terraweave
git pull --ff-only origin feature/terraweave
cmake -S . -B build/terraweave-router-qa \
  -DCMAKE_BUILD_TYPE=Release \
  -DAIVERSE_BUILD_TESTS=ON \
  -DAIVERSE_BUILD_EDITOR=OFF \
  -DAIVERSE_BUILD_CLIENT=OFF \
  -DAIVERSE_BUILD_FPS_CLIENT=OFF \
  -DAIVERSE_BUILD_SERVER=OFF \
  -DAIVERSE_BUILD_HEADLESS=OFF \
  -DAIVERSE_BUILD_TOOLS=OFF \
  -DAIVERSE_FBX=OFF
cmake --build build/terraweave-router-qa --parallel 4 --target aiverse_gpu_timing_router_test
ctest --test-dir build/terraweave-router-qa --output-on-failure -R '^gpu_timing_router$'
```

**Status:** Source read-back verified; no code fix needed yet; native execution of the new test remains **NOT RUN**. Record a real compiler error or CTest assertion if either appears after this fresh configuration. Do not mark Stage43 passed prematurely. PR #24 stays Draft.


## Stage 43 QA verified — standalone GPU timing router CTest passes on Mac Studio (2026-11-19)

**Actual user-local validation (after the corrected fresh CMake configure):**
- CMake build tree: `/Users/aleksandrfeer/Projects/AIVERSE-terraweave-qa/build/terraweave-router-qa` in isolated TerraWeave worktree.
- `cmake --build build/terraweave-router-qa --parallel 4 --target aiverse_gpu_timing_router_test`: C++ object compilation and executable linkage **SUCCEEDED**.
- `ctest --test-dir build/terraweave-router-qa --output-on-failure -R '^gpu_timing_router$'`: **PASS 1/1**, `gpu_timing_router` #93, individual runtime **0.32s**, total **0.33s**, `100% tests passed, 0 tests failed`.

This resolves the previously observed stale/misconfigured `build/terraweave-qa` "No rule to make target" / "No tests were found" issue for the new router test, using a **fresh** build tree. The error did not require production or test source modifications.

**Cumulative confirmed validation (separate runs):** previously reported TerraWeave CPU suite **9/9** passed in `build/terraweave-qa`; newly introduced `gpu_timing_router` **1/1** passed in `build/terraweave-router-qa`. Do not describe this as a single 10/10 run or infer a full AIVERSE test suite pass. Separately reported NEREID `ctest-full exit=0` is for the NEREID test suite, not TerraWeave.

**Still NOT RUN / NOT REPORTED:** desktop editor integration target `aiverse_terraweave_editor_test` and `terraweave_editor` CTest, native `aiverse_client`, `aiverse_fps_client`, `aiverse_editor` build/startup, cross-backend GPU visuals/profiling and real GPU timer concurrent-tag behavior, Linux CI on this head, complete PR/main reconciliation. PR #24 remains Draft.

**Next isolated native integration commands (preserve old passing test directories and shared AIVERSE checkout):**
```sh
cd ~/Projects/AIVERSE-terraweave-qa
git fetch origin feature/terraweave
git pull --ff-only origin feature/terraweave
cmake -S . -B build/terraweave-integration \
  -DCMAKE_BUILD_TYPE=Release \
  -DAIVERSE_BUILD_CLIENT=ON -DAIVERSE_BUILD_FPS_CLIENT=ON \
  -DAIVERSE_BUILD_EDITOR=ON -DAIVERSE_BUILD_TESTS=ON \
  -DAIVERSE_BUILD_TOOLS=OFF -DAIVERSE_BUILD_SERVER=OFF \
  -DAIVERSE_BUILD_HEADLESS=OFF -DAIVERSE_FBX=OFF
cmake --build build/terraweave-integration --parallel 4 \
  --target aiverse_client aiverse_fps_client aiverse_editor aiverse_terraweave_editor_test
ctest --test-dir build/terraweave-integration --output-on-failure -R '^terraweave_editor$'
```
On failure capture **first** compiler/linker/CTest diagnostic for the agent, not only aggregate make/CTest status. Do not alter the unrelated main-worktree VFX/PHYSIS rebase. Integrate `main` via a separate safe integration branch/worktree only after inspecting the current ahead/behind and preserving concurrent teams' work.


## Stage 44a — deterministic tree/shrub uprooting from excavated or eroded soil (2026-11-19)

**Current branch:** `feature/terraweave` (Draft PR #24). **Source commit:** `64c8941a6fb8d918f5786c5a2bde655144120a27` — NEW `engine/world/TerraWeaveRootEvents.h`. This builds on `TerraWeaveEarthworks.h` (sparse soil excavation) and `TerraWeaveVegetationSoil.h` (root profile, support and optional exposed-root segments).

**Implemented:** `RootSupportRegistry` stores at most 1,000,000 *interactable* rooted plants, keyed by stable species/key, with saved `RootAnchor`, `RootState`, `PlantEnvironment` and permanent fallen flag. Its `applyGroundChange(SoilHeightSampler, EarthworkChange, tick)` evaluates only root disks intersecting the changed X/Z rectangle, emits `PlantSupportChange` on exposure/support changes and one `PlantFallEvent` when a tree or shrub loses supporting soil. Events include stable plant ID, root support/exposure, authored anchor position, deterministic downhill fall direction (seeded tie fallback), soil revision and simulation tick. `initialize` samples natural cliffs at spawn so roots may show *before* digging. Snapshot/restore, upsert, erase, explicit replant reset and environment updates are available. Grass has no visible roots, no per-blade ECS entities, and is intentionally excluded from this registry.

**Ownership:** This header is pure CPU; it does NOT insert physics bodies, topple renderer transforms, alter collision/navmesh, modify authoritative game state or emit audiovisual effects. The native game host must consume falling events at a deterministic ECS barrier, move/hide the upright `TerraWeaveForestScene` instance (`removeTree`), create a fallen rigid-body entity if desired, and persist/replicate the event. Visual root mesh creation remains opt-in through `generateExposedRoots`; no automatic GPU mesh upload is claimed.

**Actual verification:** GitHub create-file returned source commit. **NOT RUN:** CMake/Clang/GCC/MSVC compilation, CTest, integrated ECS/Physics animation, native/editor tests, real water/terrain scene and GPU visuals (user requested features first). The prior 9/9 TerraWeave tests and 1/1 GPU router test are from prior revisions, not a validation of new Earthworks/roots/erosion features.

**Next functional steps:** 44b add a native NEREID→TerraWeave erosion tick bridge that applies per-bank WaterSample forcing to Earthworks with transaction/soil revision guards and feeds `RootSupportRegistry` events; 44c sparse grass coverage disturbance/age/light scheduling; 44d optional roots mesh and falling tree render/physics event bridge. Preserve source/agent ownership and append a handoff after each stage. Do not merge Draft PR or claim live gameplay behavior until actual integration is observed.


## Stage 44 — non-destructive heightfield rebase and main/NEREID integration (2026-11-19)

**Branch:** \`feature/terraweave\`, Draft PR #24. Source commits \`be5dceed\`, \`efc0a01b\`, \`a8820349\`, \`73092063\`, \`da557c4b\` implement \`TerraWeaveLayer.h\`, \`TerraWeaveMapAdapter.h\`, \`TerraWeaveOps.h\` and TerrainWindow support for \`preserveEdits=true\`. The old generated base is recreated from recorded settings/dimensions, checked against a reproducible fingerprint, and replaced by a new base while retaining authored corner residuals, fractional detail surface residuals, splat textures, file names and a single MapDoc undo snapshot. The mode refuses old version-1 recipes without provenance. \`overwrite\` and \`preserveEdits\` are mutually exclusive. This is a *rebase of a baked heightfield*, not a fully live, procedural node graph or cave/voxel terrain. The previous \`terraweave_generate\` and no-overwrite behaviors remain.

**User requested main water:** Existing NEREID water modules were already in \`main\`. Created \`backup/terraweave-before-nereid-main-20261119\` from the pre-merge feature head. Commit \`a56ff765311a8f464a2e180d5dd23e7eacdcb5dd\` merged current \`main\` \`d71c6d47e532b25fb816a8bb1f96c41e5275a165\` as the second parent into \`feature/terraweave\` without rewriting either history. Seven shared files (\`WorldScene.h/.cpp\`, \`Renderer3D.h\`, \`CMakeLists.txt\`, \`AGENTS.md\`, \`skills/catalog.json\`, \`tools/skills/catalog.py\`) were reconciled by combining distinct source additions. GitHub confirmed \`main..feature/terraweave\` **behind 0**, and checked merged paths for NEREID and TerraWeave. NEREID remains the sole water system; this merge does NOT validate native build/integration.

**Testing:** no native build, CTest, editor or GPU run after Stage44/source merge; separate previously reported 9/9 TerraWeave CPU + 1/1 gpu_timing_router and NEREID full test run predate this merged head.

## Stage 45 — NEREID erosion, Earthworks, rooted vegetation and grass patches (2026-11-19; SOURCE COMMITTED, TESTS DEFERRED)

**Task:** NEREID must undermine soft sandy banks and fell supported trees/shrubs, gameplay excavation must expose roots, and grass must scale to hundreds of thousands of blades per area without per-blade ECS or expensive full vegetation reconstruction. Every plant needs stable age/light environmental inputs and deterministic draw state. Physics and renderer updates remain explicit owner-thread boundaries.

**Source commits:**
- \`c085a48d\`, \`2968f177\`: new \`libraries/include/aiverselib/water/NereidErosion.h\`. Pure deterministic fixed/integer shoreline erosion with \`ShoreSoil\` (sand/silt/loam/clay/gravel/rock), wave/current/slope/wetness forcing, root-binding bank reinforcement, bedrock bound, per-tick cut/deposit budgets, suspended sediment and fractional carries. This is a soil policy consuming NEREID water velocity, NOT a second water simulation.
- \`3b701005\`, \`26ea5afd\`, \`26dddd2c\`: new \`engine/world/TerraWeaveVegetationSoil.h\`: tree vs shrub smaller root radii/depth and falling support thresholds; no visible grass roots; sample exactly 8 locations around a plant's retained planting height, expose roots only when a cliff/excavation/erosion lowers ground sufficiently, choose integer fall direction, produce stable root segments only on demand. \`PlantEnvironment\` and \`plantAppearance\` derive deterministic maturity, light, moisture, nutrient, aging/leaf/scale responses. \`GrassPatch\` is ONE bounded patch with deterministic procedural blade samples, age/light and coverage; \`erodeGrassCoverage\` edits patch coverage in constant time rather than per blade.
- \`0b973a0a\`: new \`engine/world/TerraWeaveEarthworks.h\`: genre-independent sparse grid-overlay heightfield dig/fill, fixed world bounds, per-stroke 65,536-vertex work cap, bedrock limit, signed cm heights, dirty-rectangle and explicit replayable edit list, atomic copy/swap publication. Does not support caves/undercuts, or automatically change GPU/collision/navmesh.
- \`88ec8c01\`: new \`engine/world/TerraWeaveNereidSoilBridge.h\`: checks authoritative soil vs native terrain height, applies NEREID erosion output to Earthworks, emits root support + one stable fall-event ID on threshold crossing, handles grass coverage per patch. No ECS structural writes in the pure kernel.
- \`c0268488\`, \`e544cb9b\`: \`engine/world/TerraWeave.h\`: \`PlantHabit\`, species-configurable deterministic min/max native age, maturity/lifespan and environmental factors for individual tree/shrub placements. \`FoliageInstance\` appends age/light/water/nutrients/habit/foliage fields, preserving older aggregate initializers. Individual Grass is rejected by \`scatter\`; use \`GrassPatch\` to avoid a million separate objects.
- \`44fe1db1\`: \`engine/world/TerraWeaveForestScene.h\`: sparse per-individual condition overrides keyed by stable species+tree ID, \`setPlantEnvironment\`, \`setPlantConditions\`, \`plantConditions\`, \`setRootStates\` and \`rootState(s)\` APIs, all bounded and validated; \`visibleBatches\` derives visual scale/leaf health and preserves old default appearance with no new inputs. Conditions are caller-persisted/replayed; regrowth still has its existing separate schedule.

**Not yet completed:** root segments currently exist as CPU data but do not yet draw through the GPU adapter; tree falling produces a deterministic *event*, not a fully simulated tumbling collider/animation, which needs the universal physics/authoritative gameplay host to consume it. Earthworks changes must be committed to the real streamed terrain heightfield, collision/navmesh and near/Horizon renderer by a host adapter; NEREID ECS water queries must supply per-bank \`ShoreErosionForcing\` per simulation tick. Grass coverage produces an instancing descriptor and age sampling, NOT a built render backend draw yet. Far Horizon canopy and full save migration need parity. Keep these as explicitly unfinished next stages, not claimed functional in editor or game.

**Verification:** Only GitHub source commits and readbacks; NO CMake/CTest/Metal/GL/Vulkan/gameplay/replay tests run for Stage44/45. Previous QA results apply to earlier feature heads. PR #24 remains Draft.

**Next steps:** (1) lazy/shared root render prototypes for visible near Arbor bushes/trees, no geometry if roots concealed; (2) map-to-native Earthworks commit adapter updating collision, navmesh, near + Horizon; (3) NEREID bank sampling ECS system, sediment mass conservation, growth/soil state save migrations and procedural grass instancing; (4) write independent tests for deterministic rebase/replay, sparse excavation, cliff roots, loss of tree support, grass batch density, lifecycle overrides, NEREID physical sign/priority. Test agent compiles and validates later, per user's feature-first request.


## Stage 44b — NEREID water-to-Earthworks authoritative tick bridge (2026-11-19)

**Branch / PR:** `feature/terraweave` / Draft PR #24. **Source commits:**
- `498a46afa619bc8726a301c1c06d245fe6308b0a` — NEW `engine/world/TerraWeaveNereidErosionBridge.h`.
- `ca21d1c41a680b845cb5ec78e0f4c19f7874f30e` — `engine/world/TerraWeaveRootEvents.h`, transactional support update and correct explicit replant fall reset.
- Prior required kernels: `libraries/include/aiverselib/water/NereidErosion.h` (`c085a48d` / rate follow-up `2968f177`), `engine/world/TerraWeaveEarthworks.h`, `engine/world/TerraWeaveVegetationSoil.h`.

**Behavior and public API:** `terraweave::NereidEarthworksBridge(EarthworksLayer&)` consumes at most 16,384 bounded, unique `NereidBankProbe` entries from a completed NEREID water ECS tick, sorted by `EarthVertex` independent of input order. A probe supplies `lib::nereid::WaterSample` (flow), wet-bank contact, wave energy, local slope, substrate type, bedrock and plant-root reinforcement. Missing water suppresses erosion; the bridge never invents currents or pretends every surface below sea level belongs to a river. `step(probes,completedTick,dtMs,RootSupportRegistry*,settings)` derives local water-driven erosion/deposition through `lib::nereid::erodeShore`, accumulates integer sediment/fractional carry state, preflights all sparse `EarthVertexEdit`s and commits to the existing `EarthworksLayer::applyEdits` single transaction. The bridge retains bounded state with `snapshot()/setSnapshot()` for save/replay and rejects duplicate/backward ticks. It then optionally evaluates `RootSupportRegistry` against the same new terrain and reports `PlantFallEvent` for trees and shrubs, once per persistent identity. No grass-blade ECS records.

**Safety/performance:** No GPU, physics, navmesh or render mutations occur inside the bridge. The native host owns fixed-timestep schedules, water sampling (ECS `sampleWater` or query index), persistent soil and plant registries, terrain collision/navmesh dirty publication and spawning actual falling rigid bodies. Per-cell sediment currently remains **local** for erosion/deposition; cross-cell downstream advection is not yet implemented. A cell's `wetContactPermille` must be computed by a real water-body/shoreline query, not inferred solely from terrain height. All regular terrain edits continue to use the authoritative Earthworks owner. Missing soil samples conservatively avoid uprooting.

**Verification:** New source committed via GitHub; no native compiler/CTest/CI/GPU or gameplay execution performed. Previous TerraWeave 9/9 + GPU timing router 1/1 results validate earlier revisions only. **Feature-first** phase, not safe to claim live shoreline erosion/falling trees yet.

**Next:** Stage 44c needs area-weighted sparse grass coverage disturbance from `EarthworkChange` and persistent age/light inputs, with no per-blade entity; Stage 44d optional roots geometry + fall event bridge into renderer/ECS and actual terrain/collider/navmesh streaming. After source feature completion run CMake/CTest and real Metal scene QA. Keep PR Draft.


## Stage 46 — lazily visible roots, ECS shoreline erosion and persistent plant conditions (2026-11-19, SOURCE COMMITTED; QA DEFERRED)

**Implementation commits:** \`1ac01bd2\`, \`8903d526\`, \`aea7fadb\`, \`f665298d\`, \`195c1b07\`, \`59a84621\`, \`4d3ac51c\`, \`ca13a87d\`, \`0a20fa96\`, \`78b5ab9b\`, \`6709cb9a\`, \`441719d1\`, \`252870aa\`, \`7f1eb293\` (see Git history for full SHAs).

- **Conditional root GPU geometry:** \`engine/render/TerraWeaveRootModel.h\` converts pure exposed root segments to one shared \`render::Model\` bark mesh per species/exposure tier. \`ArborForestAdapter\` now holds per-species root policy and sparse \`RootState\` identities, lazily creates at most 3 root LOD tiers per exposed species (no model/allocation/draw until root exposure is reported), uploads/releases with regular model cache via \`gfx::Device\`, and submits one extra standard Renderer3D draw only for near/medium/low exposed plants, not impostors/grass. No new shader backend. Individual root directions share prototype seeded morphology and tree yaw, not unique GPU meshes. Root height is now anchored at original planted Y instead of following eroded ground.
- **WorldScene:** \`world::WorldScene::setVegetationRootStates(vector<RootState>)\`, \`vegetationRootPrototypeCount()\` forward soil presentation state to TerraWeaveForestScene. \`WorldScene::update\` performs lazy GPU uploads only on the owning Device phase when new root models are pending. Existing NEREID water and TerraWeave forest/Horizon code paths continue sharing \`WorldScene\`.
- **ECS shoreline:** \`libraries/include/aiverselib/water/NereidShorelineSystems.h\` adds persistent \`WaterBankSoilComponent\` and \`WaterBankErosionOutputComponent\` plus \`addNereidBankErosionSystem\` after water sampling, with fixed native dt, all fixed/integer source values, 1 stable bank-cell entity per terrain sample (never per plant/blade). Opt in through \`NativeWaterPipelineConfig::enableBankErosion\` and \`bankErosionTickMs\` (OFF by default). Existing WaterSample flow/current/confluences/cascades are consumed unchanged; shoreline slope/wave/wet-contact and root reinforcement are host-authoritative inputs. \`terraweave::applyEcsBankErosion\` applies the precomputed output to the sparse soil layer after the simulation read barrier without calling \`erodeShore\` twice.
- **Editor excavation:** \`apps/editor/core/TerraWeaveEarthworksOps.h\` registers \`terraweave_excavate\` from \`TerraWeaveOps.h\`: bounded dig/fill of existing flat RTS corner heightfield using \`EarthworksLayer\`, optional saved generation world origin/spacing, change preview/quantization and one MapDoc undo step, detail residual replay and splat-material preservation. Reject FPS/planet MapDoc operations; no silent flat map changes. Heightfield holes/ditches only, not volumetric caves/undercuts.
- **Detailed design/ownership:** \`docs/nereid/TERRAIN_EROSION.md\` documents the pure ECS/water/soil/physics/root/grass contracts, units, save boundaries, launch order and limitations.

**Important genuinely unfinished behavior:** a real Box3D/ragdoll/rigid-body fall animation, collision/navmesh patch publication, runtime authoring of wet-bank cells from actual water geometry, 3D/voxel excavation, and GPU grass patch instancing are NOT delivered by the pure kernels. Native game must consume \`PlantGroundResponse::fallEventId\` to replace rooted standing tree with simulated fallen collider; it must own save/replication. Static forest roots are only visible when a valid root snapshot has been explicitly supplied to WorldScene. Turf remains aggregate CPU \`GrassPatch\` data until an RHI patch draw adapter is wired. Do not describe these as production-ready.

**Validation:** GitHub commits/source inspection only. NO new CMake, CTest, Metal/GL/Vulkan screenshot, latency/perf, ECS replay, Box3D integration or editor runtime run. User explicitly asked to finish functions before QA. Previous green Mac Studio suites were before these changes. Draft PR #24 retained. Next: wire live soil/collision/revision pipeline, authoritative falling rigid-body adapter, and GPU grass patch batches, then independent tests/build.


## 2026-11-20 integration checkpoint
User authorized TerraWeave ownership of PBR vegetation/terrain and integration into main. Merge commit 51ffcb8ff33579b204e6b4d327a7523d55d1f625 reconciles origin/main f9aed9a70 with TerraWeave d9de76dcd in isolated worktree. Six conflicts reviewed: retain main Replica implementation/comments and Place progress helpers (avoid duplicated helpers), main signed material-index check; retain TerraWeave fixed CI regex including micro/native checks; retain main CMake comments and all additive TerraWeave targets. Scoped git diff --check PASS. Repository-wide diff --check reports pre-existing Markdown whitespace from main, not rewritten. Native build/CTest/GPU acceptance NOT RUN on merged tree yet; CI NOT RUN. No push or main update yet. Next merge PBR pack history, rebuild native editor and applicable TerraWeave tests before publishing main.

PBR history integration commit 39501d9213b1cda25e564a0e39e9ae2334be1cbb: combines TerraWeave reconciliation with terrain/vegetation packs and experimental offline authoring checkpoints. Native APIs retain merged main ownership; generated demo is not production Arbor replacement. Native merged build/tests pending. Next: rebuild editor and TerraWeave tests on this exact source, then publish feature/terraweave.

## 2026-11-20 bark UV and integration verification
Code 9be468a4149bfd8aea56bd48882abab6aaff93f1: experimental authoring tool now projects bark V on the unbounded segment tangent rather than clamping at each capsule endpoint; fixes constant-V ring bands. Corrected periodic U seam unwrap for signed [-0.5,0.5] angles. Real editor reload and scene screenshot /private/tmp/aiverse-feature-showcase-20261120/bark-uv-verified.png confirm trunk ring bands removed. Root/branch UV junction seams remain visible; not complete. Native editor and all 13 TerraWeave test targets built Release arm64; ctest -R '^terraweave' PASS 13/13 (2.60s), assertions retained with -UNDEBUG. PBR pack/installer audit PASS; 48 GLSL/GLES pairs and 12 actual Metal shader libraries PASS. Full regression/CI/GPU LOD transitions NOT RUN. User clarified: merge main ONLY after beautiful varied trees, existing planned TerraWeave branch LOD and screenshots are ready. Therefore do not publish main based merely on this build/test success. Next: smooth junction UVs, reduce polygon counts, reuse native per-branch LOD and prototype variation; capture scene-only evidence and then merge.

## 2026-11-20 bounded meshes and connected bark atlas
Implementation 112030a39009a3ecb1cb2c2559c8eac4da1b9fc1: tools/vegetation/bake_bark_atlas.py uses xatlas connected charts and object-space triplanar SAMPLE blending baked to standard basecolor/normal/ORM textures. Removed UV-coordinate blending that caused swirls. Offline prototype budgets: oak 9000 wood+770 leaf triangles; shrub 1400+350. Runtime/shader/save APIs unchanged. Dependencies: numpy/scipy/scikit-image/fast-simplification/xatlas/Pillow. Native editor screenshot bark-atlas-verified.png and scene-only crop bark-atlas-scene.png visually confirm ring bands and root UV hard seams/swirl removed at inspected angle.
Validation: generation completed for oak seed414 and shrub seed317; inline GLB audit FAILED at unit-normal assertion (investigate zero normals after simplification). Do NOT report audit passing. LOD export, native Forest integration, multiple species, seed placement anti-repeat and complete visual QA remain unfinished. Current editor still needs refreshed shrub model. main must stay unchanged until user-requested visual/LOD readiness. Next: diagnose/fix failed normal audit, verify canopy attachment and budget, then per-branch LOD export using existing mesh extras.rtsLods contract and native Forest prototype support.

Normal audit repair 47ef377e161158b703d37680887d628be8783e58: remove zero-area and duplicate opposite triangles after simplification, compact used vertices, provide incident-face fallback before normal normalization. Added tests/vegetation/check_authored_model.py. Executed audit on regenerated oak seed414 PASS 9584 triangles and shrub seed317 PASS 1546 triangles: finite attributes, nondegenerate faces, unit vertex/texture normals, valid indices, resolved PBR textures, dielectric ORM. Existing visual atlas screenshot remains prior cleanup revision; new geometry needs reload. User now requires roots smoothly descending into ground; current root silhouettes still wedge-like. Next change root upper profile to tangent descent and verify scene screenshots; native LOD/species integration still pending, no main merge.

Root contact checkpoint 7bb270b234288b38517b8b17b35234c0ee3583e2: authoring root upper surface uses smooth cubic descent plus below-ground tail, rather than a straight centreline wedge. Regenerated oak seed414: audit PASS 9576 triangles. Real editor roots-ground-profile.png inspected at low angle on flat ground; roots shortened above ground and buried ends, but organic silhouette refinement and slope validation remain. Real six-plant overview with regenerated shrub reports 34.9K triangles / 9 draws (previous user screenshot 537K / 9); no frame-time speedup claim. User now supplies Witcher forest reference as MINIMUM visual bar: mixed conifers/broadleaves, age/height/crown diversity, dead branches and layered understory. Current sparse repeated oaks do NOT meet it. Next finish species/seed variants, full crown and existing branch LOD, mixed forest scene and near/far captures. main unchanged.

## 2026-11-20 expanded PBR library and proven terrain banding fix
Implementation: e3bbc69ae00bc15fcb0fe569cbc734d1fc47d46f on local feature/aiv87_terrain_pbr, published through feature/terraweave. Terrain pack now has 10 materials / 90 maps, adding forest floor, wet soil, gravel soil and snow crust alongside fresh snow. Vegetation pack has 8 materials / 64 maps, including oak/pine/birch/spruce bark and oak/shrub/spruce/pine foliage. Sources and provenance are retained under tools/{terrain,vegetation}/pbr_sources; bakers, pack tests and documentation updated. Ordinary PBR textures and existing material format; no C++ API/save changes. Translucency maps remain authoring data, not consumed by the standard shader. Snow materials do not implement automatic snow accumulation on crowns.
Terrain stripes were isolated in real Metal editor A/B: shadows disabled still produced stripes; flat color removed them; original albedo alone restored them; removing broad albedo illumination drift removed them. Full PBR with normal/AO and normal shadows then passed both near and far inspection. Grass baker removes periodic low-frequency color drift, preserving detail. Regression blur-64 luminance standard deviation: old 0.0057615, new 0.0003337, limit 0.001.
Executed: terrain pack audit (90 maps) and installer preservation/refusal PASS; vegetation pack audit (64 maps) PASS; scoped git diff --check PASS. Real native material gallery inspected on secondary editor port 47323, with normal shadows. Evidence: /private/tmp/aiverse-feature-showcase-20261120/expanded-materials-render-scene.png; terrain-bands-fixed-full-pbr-scene.png and terrain-bands-fixed-far.png in the same directory. Initial gallery generation with system Python failed (missing Pillow); explicit ARM Python with temporary dependencies succeeded. No new C++ changes; full regression/CI NOT RUN for this asset stage. Earlier 13/13 TerraWeave native checks apply to the earlier integrated code checkpoint.
Initial handoff write and Git lock creation failed with disk-full errors after implementation commit. Cleaned owned reproducible temporary dependencies/worktree; disk now reports 32 GiB available; retried handoff. No user assets deleted.
Outstanding: ten tree species, stable growth seeds/heights, native age-driven presentation, winter morphology and per-branch LOD/impostor integration are NOT completed by these assets. Preserve FoliageInstance age and CanopyPhenology as existing authorities. Current repeated sparse oak demo does not meet the supplied visual reference. Next implement bounded diverse presentation prototypes and mixed forest QA; do not merge main before visual/LOD readiness.

## 2026-11-20 native sky integration for valley showcase
Merge 247f811e467be424cdff4c3c796da685492b9f35 imports existing main physical sky implementation 1397dfb52 into local feature/aiv87_terrain_pbr, destined for feature/terraweave. Merge clean; no renderer reimplementation. User explicitly rejected generated sky backdrop in favor of existing engine sky. Current offline valley scene is external at /private/tmp/aiverse-alpine-valley. Prior renders used older binary without native sky and are not final. Compile/new GPU validation NOT RUN at this checkpoint. Next forward optional postfx.physicalSky through renderbench host, rebuild and capture native sky; remove backdrop from final project. main untouched.

## 2026-11-20 renderbench native sky host
Code d9c9ab1ec3761400f4269eef91db73e12ad194c1 forwards optional postfx.physicalSky through existing parsePhysicalSky / Prisma setter, supplies frozen or presentation clock, and reports physicalSkyRequested (requested is not proof of active HDR pass). Existing absent-block default remains OFF; low quality retains existing fallback. No new sky shader. Native Release arm64 renderbench build PASS; first combined build command finished renderbench but could not find newly regenerated test target in the invoking Make instance. Repeated target build PASS and CTest physical_sky 1/1 PASS (0.18s). Real 1600x1000 Metal valley capture with native sky PASS, sourceMisses44 / pipelines49 / pipelineFailures0; image inspected at /private/tmp/aiverse-alpine-valley/vista-native-sky.png. Full suite/GL/Vulkan/mobile/CI NOT RUN. External scene remains presentation geometry with a static PBR river, not live NEREID simulation or TerraWeave lifecycle/LOD completion. Generated sky backdrop removed from active scene per user correction. Next finish external scene cameras/save package; main stays unchanged pending original vegetation acceptance.

## 2026-11-20 native procedural rocks and boulders
Implementation f22e60ae6500e82b38b6e15a670a1e5e5b14718d on feature/aiv87_terrain_pbr (publication target feature/terraweave). New engine/world/TerraWeaveRocks.h: integer stable-key lattice scatter, half-open bounds, negative-coordinate/chunk invariance, four-color immediate-neighbor variant selection, one-million candidate cap, optional height source/filter, nine support samples, slope rejection and burial. New engine/render/TerraWeaveRockModel.h: Rounded/Boulder/Slab/Shard profiles, seed-owned clipped weathered shape, nested ico geometry 1280/320/80 triangles, analytic surface normals and generated tangents, standard Model LOD 100/28px, origin-relative rockPose. CPU geometry is cosmetic float, not authoritative collision or a portable replay checksum. Default PBR fallback; native host owns shared textures and bounded prototype cache.
Tool tools/terrain/rocks.cpp / aiverse_terraweave_rocks reuses native generation and existing modeling::exportGlb, writes external glTF/bin with rtsLods and four once-shared PBR maps. Refuses any existing output directory. Public/save compatibility additive: no old state changed, tool catalog v1; no automatic WorldScene/Studio installation. Docs rocks.md, skill, AGENTS and all nine READMEs updated.
Executed: native Release arm64 targets aiverse_terraweave_rocks_test and aiverse_terraweave_rocks build PASS; strict c++ -std=c++20 -Wall -Wextra -Werror -fsyntax-only on both sources PASS. CTest ^terraweave final 14/14 PASS (0.35s), assertions explicitly -UNDEBUG. Checks cover closed outward nondegenerate triangles, unit normals/tangents, seed reproducibility and distinct full-geometry hashes, LOD budgets, buried bottoms at every LOD, chunk/negative/neighbor/filter invariance, missing support/steep-slope rejection and bad settings/budget. Initial uniqueness check based only on AABB sum failed (different surfaces can share extrema); replaced with full geometry hash, not weakened geometry requirements.
Unified run-tests.sh --quick --no-build refused Release cache because this older wrapper requires Debug; recorded in /private/tmp/rocks-test-report.log. Direct CTest above actually passed with assertions enabled; wrapper/full suite/CI NOT passed or claimed. Exported 12 prototypes to /private/tmp/aiverse-native-rocks-v3, native glTF importer roundtrip PASS for LOD, four 1024 images and dielectric material. Existing-directory overwrite refusal PASS. README checker 9 files / 0 problems; git diff --check PASS.
Actual native Metal 1600x1000 twelve-shape preview inspected: /private/tmp/aiverse-rock-garden/rocks-final.png; report complete=true, no asset warnings and zero pipeline failures. Existing renderbench binary hosts exported assets; its recorded revision predates this CPU-only generator. No new shader/render engine. GL/Vulkan/mobile, live LOD transition timing and performance NOT RUN. Sampling is approximate support, no terrain-conforming collision, automatic slope tilt, physics/destruction, erosion or lifecycle. Spherical UV poles can pinch. Immediate lattice-neighbor variety does not promise uniqueness after sparse filtering. Next install generated pack into external forest valley and capture natural close-up; keep main unchanged while earlier tree visual/LOD acceptance remains unfinished.

## 2026-11-20 native canopy/grass and Terra-first routing

Implementation 88cd9b8b9ce3ff5ff6628991019aeafe1cea877e on feature/aiv87_terrain_pbr, publish to feature/terraweave. Existing ArborMesh/ModelAdapter now have opt-in naturalConifer needle sprays anchored to curved branches, 2x2 shared atlas, reduced side-branch thickness, longitudinal/radial/spray LOD and generated tangents. The integer skeleton and gameplay saves are unchanged. GrassRender adds bounded 1..4-segment indexed bent ribbons, randomized yaw, height/width and CPU placement mask, with quality invalidation. Existing grass default visual orientation/width changes; density belongs to host patch settings. New native exporter tools/terrain/vegetation.cpp reuses these APIs (no Python tree/grass solver), 12 spruce/pine prototype profiles, shared PBR maps and optional bounded grid-driven grass. No live-host default switch, ten species, seasonal snow or root-junction completion claim.

New skills aiverse-terra-first and aiverse-terra-vegetation, catalog 228, wizard/orchestrator routing and nine READMEs. User explicitly requires ALL environment creation through TerraWeave (terrain/biomes/plants/rocks) + NEREID (all water), custom only for demonstrated gaps. AGENTS, core Terra skill, water and NEREID skills updated; retain PRISMA/PHYSIS ownership and legacy compatibility.

Executed: full rebuild of 14 TerraWeave test targets and exporter Release arm64 PASS; final ctest -R '^terraweave' 14/14 PASS (0.37s), assertions -UNDEBUG. Strict c++ -std=c++20 -Wall -Wextra -Werror -fsyntax-only exporter/model test PASS. Native export 12 prototypes +205 grass patches PASS; existing-directory refusal PASS. Actual Metal scene-only riverbank capture /private/tmp/forest-final.png inspected; zero pipelineFailures. Final tree triangles pine ~17736/5904/2316, spruce ~23448/7808/3064, not scene totals. Earlier first image was too skeletal; side branches thinned and crossed sprays enlarged before final capture. Grass indexed ribbon uses 7 vertices versus prior 15 at three segments. No measured GPU speedup.

Skill catalog generation/check, routing tests, strict audit (0 errors/warnings), 9 README checks PASS. Generic skill validator initially missing PyYAML; installed only in /private/tmp/terra-skill-validation, both new skills then PASS. Unrelated tools/libraries/catalog.py check FAILS on pre-existing HEAD NEREID status='experimental' not accepted by its validator; library catalog unmodified, do not report full repo audit green. Full regression/CI/GL/Vulkan/mobile/animated LOD transitions NOT RUN.

Critical remaining scene migration: current external AlpineValley still had custom Python terrain and static PBR river; user explicitly rejected both and requires replacing them with TerraWeave and NEREID. That migration is NEXT, not completed by vegetation export. Keep old scene files only as provenance; do not present them as native water/terrain. Engine main unchanged until complete visual acceptance.


## 2026-11-20 native Terra landscape + NEREID host integration

Implementation b2c8c5d9235d697274239b17f29e64fbad621d0d on feature/aiv87_terrain_pbr, publication feature/terraweave. New TerraWeaveRiverbed authoring bridge carves native Earthworks from NEREID spline geometry with half-open bounded edits. Native landscape exporter uses TerrainSettings/Earthworks/terrain patches, existing scatter and rockPose; shared ground snapshot drives grass/trees/water masks. Shared ExportModel helper fixes a discovered dangling Json nested-member reference (adding material sibling keys invalidated pbr). No replacement water shader or project noise solver.

NereidManifestHost composes existing parser/residency/draw in renderbench and native editor Scene/RTS Game views. Missing water block clears previous scene. MapDoc extra owns presentation settings, not authoritative ECS water state. API additions preserve existing saves; offline assets are not live terrain/collision/ecology. Water uses renderer clock even with renderbench freeze-time. No new production rendering flags.

Executed: Release arm64 editor, renderbench, landscape/vegetation exporter and new landscape test builds PASS. CTest ^terraweave 15/15 PASS (0.72s), assertion checks enabled. Strict c++20 Wall/Wextra/Werror syntax landscape PASS; skills catalog 228 PASS; diff check PASS. Exported 48 terrain tiles, 12 tree prototypes and 202 grass patches; PBR binding audit PASS (192 terrain material bindings, 24 tree bindings, 202 constant dielectric grass materials). First generic PBR audit incorrectly required textures on constant-PBR grass and raised KeyError; corrected assertion explicitly checks grass dielectric factors. First landscape run aborted due dangling Json reference, fixed above and rerun successfully. Earlier build attempts lacked native manifest link library, then fixed a namespace ambiguity and RockInstance field name before successful build. Do not repeat those failures.

Real Metal renderbench capture /private/tmp/terra-nereid-final.png inspected: native river present, pipelineFailures=0, 61 pipelines. External project /Users/aleksandrfeer/Projects/AIVERSE-AlpineValley active rules use models/native-world.gltf; map NEREID points offset +24m X/Z to match asset entity. Actual editor Scene screenshot /private/tmp/terra-nereid-editor.png inspected and confirms water/terrain placement. Terrain/forest/mountains are authored by Terra, river by NEREID, sky uses existing PRISMA. The project has 935 trees and 200 rocks; counts are objects, not draw calls. Initial broad foam ribbon reduced with standard NEREID parameters, no shader fork.

Limits: current mountains are visibly round, crowns still lack requested species/age/winter diversity; this is integration evidence, NOT final art acceptance or SpeedTree parity. Editor lighting differs from renderbench; no Game-view capture, native gameplay buoyancy/contact test, GL/Vulkan/mobile, CI or full suite run. No performance improvement claimed. Main remains unchanged. Next: refine Terra mountain composition, canopy profiles and mixed species, season/age and near/far LOD acceptance; keep native ownership and capture scene-only results before main merge.


## 2026-11-20 terrain PBR wrap-band repair

Implementation 7c66e0a17b3f13e04af6a9adea1438419367c427 on feature/aiv87_terrain_pbr, publication feature/terraweave. User reported ground repeat stripe. Repeated albedo contact sheet exposed narrow seams on gravel/rock. Previous periodic decomposition forced only endpoint rows equal, leaving a neighborhood discontinuity; replaced with smooth weighted half-period overlap on both axes, with no duplicated endpoint texels. Regenerated all nine organic terrain materials and derived normal/ORM/AO/height maps; checker unchanged. No runtime/API/save change. Tests now compare wrap gradients with interior gradients rather than requiring duplicated endpoints.

Executed: Python PBR bake PASS 90 maps; tests/terrain/pbr_pack_test.py PASS including seam gradients, unit normals, packed parity, low-frequency grass and installer preservation/refusal. Diff check PASS. Inspected 2x2 grass/gravel contact sheet /private/tmp/ground-repeat-final.png and actual Metal scene /private/tmp/ground-fixed.png, zero pipelineFailures. First renderbench command rejected samples=10, rerun samples=30 PASS. Ground partly obscured by forest in overview; contact sheet is the direct seam evidence. Updated active external project's four terrain material sets locally. Native rebuild/full suite/CI/cross-platform NOT RUN (asset/Python-only stage). Existing editor may retain loaded textures until reopen. Next inspect near ground in editor after texture reload; broader mountain material classification and visual art work remain distinct issues.


## 2026-11-20 alpine surface and PBR stage

Implementation e4f52de4b9d2774f7ca3d6aee3e92931b1060c3c on feature/aiv87_terrain_pbr (publish feature/terraweave). Native bounded AlpinePeak profiles reuse integer Terra noise for asymmetric ridged relief and optional broad valley. Native patch export now 129/64m, smooth Earthworks-derived normals. Removed triangle-based material buckets: offline PBR atlas blends grass/sand/rock/scree/snow continuously, triplanar rock projection, linear color blend, normalized normal detail; standard glTF sampler clamps atlas edges. Riverbed lip now +8cm above water so NEREID perimeter is buried instead of exposed over submerged ground. Additive authoring APIs; existing saves/live world defaults unchanged. Atlas baking required after export. 1024 demo atlas =16px/m; default2048, close hero quality may need higher resolution. No hydraulic erosion claim.

Executed: Release native exporter/test builds PASS; CTest Terra 15/15 PASS, strict syntax landscape PASS before final valley/lip adjustment (native rebuild passed after adjustment). Python compilation PASS. Export48 native tiles and fresh flora PASS. Initial Metal capture alpine-vista.png inspected: new ridges/smooth normals visible but mountains intruded into river causing tall walls. Added native valley and regenerated /private/tmp/alpine-land-v3; final1024 atlases currently baking via4 independent CPU partitions. Final visual acceptance PENDING next stage. First compile attempted nonexistent Fixed.sqrt; corrected to fxSqrt. Do not claim initial render final. No full suite/CI/mobile/Vulkan.

Next: inspect final valley/shoreline and texture atlas seams, then fix NEREID animation clock: existing water shader uses cloud offset as time, which stays zero in this host. Separate water code and handoff stage required. Main untouched.


## 2026-11-20 NEREID animation clock stage

Implementation d3b7f872054f54449347c52e1ed9b2210baeab77 on feature/aiv87_terrain_pbr, publish feature/terraweave. Tagged NEREID water waves/cascade/whitewater use existing U1 camPos.w render clock, not cloud offsets. GLSL/MSL parity; untagged legacy water keeps cloud-clock behavior. Wave coordinates advect by configured local NEREID current, not wind direction. No uniform/save change or gameplay flow solver change.

Release editor/renderbench build PASS. Actual Metal sequences /private/tmp/nereid-flow-sequence and /private/tmp/nereid-advection-sequence captured16frames each at fixed camera/sky; zero pipeline failures. Initial frame change at800x500 mean4.84/255 (includes water/temporal effects, not quantitative flow velocity). First high foam tuning made regular bright blobs; reduced foam/refraction in external scene. Source clock repair is verified, not a substitute for editor flow controls.

Final Terra valley export /private/tmp/alpine-land-v3:48 native tiles, normal smoothness, atlas sampler/normal/dielectric audit PASS, max horizontal seam gradient ratio1.78<2. Newbank screenshot /private/tmp/nereid-bank-final.png inspected: sand transitions improved but grass still on sand, water rim too bright; user explicitly requires dense tufted grass and editor LOD verification. These remain next work. Current active externalproject assets updated locally but recipes/captures need commit once final. Main untouched.

New user requirement: flow must be editable in editor; automatic default from actual slope (example1degree), clocks only animate configured flow. Existing bodies are horizontal and current fields accept manual velocities; need additive native grade/profile contract shared by sampling, mesh and Terra bank, deterministic tests/persistence plus editor controls. Do not claim slope-based flow exists yet. Also verify LOD using editor node-level counters, not merely glTF presence. No full regression/CI/Vulkan/mobile testing.


## 2026-11-20 editable river grade native stage

Implementation f9b764f83e5b42c26585eeff0f099b933d52c449 on feature/aiv87_terrain_pbr, publication feature/terraweave. New optional WaterRiverGradeComponent schema1, opt-in native pipeline registration, deterministic inclined water plane, nearest-tangent automatic downhill velocity with configurable linear-drag response/cap, manual body.flow retained. Reference/indexed samples, contacts, ECS presentation, mesh vertices/normals/residency keys share grade. Parser accepts signed slopeDegrees and downhillHeadingDegrees or explicit risePerMetre. Typed editNereidFlow plus legacy editor agent operations validate before Undo transaction. Current velocity is supplied even when foam strength0. Terrain authoring can alignRiverGrade and carve bank from local water plane; grass mask excludes relative shore heights<270cm. PBR shoreline weights read same grade. No old component byte/version changes; optional new registration defaultsOFF to preserve old native catalog/checksums. Horizontal planar reflections and confluence/cascade reference-level connectors still approximate graded water and need dedicated future parity. Automatic current is friction-limited estimate, not hydraulic discharge conservation.

Executed: new standalone native CTest ecs_nereid_river_grade_test PASS (0.73s): reference/indexed parity, two independent hosts, native save/load/replay, COW rollback, manual/flat modes, mesh-vs-contact grade, invalid authoring. Earlier Terra landscape expanded test PASS. Release exporter/renderbench build PASS; legacy editor completed build before last minor header updates, final recompile pending. Initial direct test link missed worldVector/worldInteger; corrected WorldAssets/model dependencies and registered CMake target. New CMake target first invocation before regeneration failed no rule; explicit configure then target build PASS. No final graded GPU capture yet. Full NEREID suite/CI/cross-platform NOT RUN.

HTML5 UI is being implemented independently on feature/studio-html5 using generated Studio UI panel, typed bridge, native MapDoc Undo and native NEREID host; source copied only after baseline equality checks, no unrelated root changes overwritten. Its build/QA/handoff still pending; do not claim editor UI ready. Next finish Studio native tests/visual QA, rebuild external scene at1degree with matching Terra grade, dense tuft grass/no sand, and investigate editor LOD counters. Main untouched.


## 2026-11-20 tuft grass and independent camera/shadow LOD

Implementation d182b5c32111db5c12d919de390c0b39c67f1721 on feature/aiv87_terrain_pbr, publish feature/terraweave. Grass quality appends stable tuft size/radius, exporter uses16 blades per15cm tuft plus three stable density LODs. Ground export adds optional waterHeightCm and exact blade filter excludes local shore heights below270cm, preventing interpolated cover from bleeding onto sand. Density recipe8192 blades/64m2; actual final pack154 nonempty patches,897888 base-tier blades (asset total, NOT frame submissions). Audit reads exported roots and confirms min relative height269.5cm within integer grid rounding, all patches3 strictly decreasing tiers.

Generic per-asset sidecar independentShadowLod defaultsfalse. Eligible rigid meshes choose camera detail independently, with separate shadow-only mesh at the original shadow projection and no shadow fade. Variants/node overrides/deformation/geometry keep existing gates. Opt-in disables immutable CPU packets and GPU-LOD path; ordinary transforms/cache/material validation unchanged. Model sidecar strict bool parsing. Native SceneWindow diagnostics via AIVERSE_LOD_DIAGNOSTICS; geometry_stats and renderbench expose camera-level counters. Minor grade integer-ID and disabled-ECS-grade fixes included.

Executed: Release arm64 native editor, renderbench, exporters, terraweave_model and model_batching_policy builds PASS. Targeted CTest model_batching_policy + ecs_nereid_river_grade_test + terraweave_model3/3 PASS0.43s. Tests include tuft proximity/determinism and sidecar import; actual Metal sidecar A/B at same far camera: baseline camera levels1015/258/117 with1171 shadow-limited nodes; independent102/905/383 with0 shadow-limited nodes. Native editor source counters via scene-only API: near508/159/107, far568/241/159; blocked0/shadow-limited0 in both, visible counts differ with camera. Thus no false whole-frame triangle/FPS comparison. Captures /private/tmp/editor-{near,far}.png are Scene API verification only; final user media uses renderbench without editor controls. Main vista /private/tmp/terra-final-vista.png inspected, zero pipeline failures. Far diagnostic shows finite terrain edges, not final hero art.

External scene now native grade1degree, Terra terrain aligns same plane and bank, dense grass and sidecar opt-in. Root HTML5 controls code2acfad6a4 / handoff f6328bb4f on feature/studio-html5; compiled and session/web tests passed, visual panel QA blocked by CUA failure and desktop screenshot auto-review rejection (user permits scene-only). No main merge. Remaining: improve visibly hard white water contact rim using actual depth coverage, polish mountain composition/material contrast, persist external recipe/captures and final handoffs. Full fresh Terra/NEREID suites, Vulkan/mobile/CI and shadow pixel parity NOT RUN for this stage. Experimental MDI/ICB stays OFF.


## 2026-11-20 explicit surface ownership and shoreline coverage

Implementation 8457e97bd7712ebcf187d964220aeaadef787b08 on feature/aiv87_terrain_pbr. TerrainLook.render defaulttrue for legacy projects; game/rules terrain.render=false suppresses legacy terrain/water and stale trace heightfield. New MapDoc.reset writes terrainPresentation=entities; old loaded maps missing key retain legacy behavior. RtsView per-map enable flag and editor sync honor the document mode. Root HTML5 Studio is receiving the corresponding flag/empty-grid suppression separately. User explicitly confirmed green rectangular pieces were the unwanted standard plane and now requires no automatic terrain on blank maps, plus individually selectable entity/component terrain, rocks, trees and grass. Current packed single-valley entity must therefore be replaced, not relabeled as finished.

NEREID shader adds undistorted depth shoreline coverage and lower broken foam (GLSL/MSL), avoiding refraction moving the physical shoreline. Offline PBR baker darkens/roughness-adjusts wet sand by the same water plane. Executed: actual GLSL330 and GLES300 compile/link via glslangValidator PASS; native editor/renderbench build PASS for renderer/rules flag and shader. Later new-map default/per-map flag source additions need fresh build/test. Metal capture /private/tmp/nereid-final-shore.png has0pipelineFailures; visually white rim reduced but water looks excessively transparent, so final water optics acceptance is NOT claimed. No-optics debug /private/tmp/water-nooptics.ppm pending to isolate opacity vs contact. Wet atlas48tiles currently rebaking.

Next bounded stage: new blank-map tests and canonical authored entity components with stable IDs, native model transforms and shared selection/Inspector/Undo; replace packed external scene with individual native assets. Preserve old game compatibility and do not claim full authoritative RTS/ECS migration or physics ready just because editor entities exist. Main untouched; final visual capture and any corrective depth work pending.


## 2026-11-20 explicit authored environment entities

Implementation db2263ba1887a190415d5e0afa58cb14b5cc9f62 on feature/aiv87_terrain_pbr, publication feature/terraweave. Canonical MapDoc entities now support opt-in Transform Y/yaw/uniform scale plus StaticModel/provenance components; X/Z remain canonical pos, with existing LocalTransform affine presentation math. Shared transform adapter reads/edits actual authored Y and atomically validates Transform patches. Legacy planar objects still reject Y. Native editor model parentFrame consumes the authored transform. Duplicate retains components with a fresh document ID and fresh NEREID numeric surface ID. See docs/terrain/entity-authoring.md for schema/limits.

NEREID entity-owned local surface data compiles read-only to the existing native host; body heights/centre, river/polygon points, grade origin and vortices translate with the object. Rotation/non-unit scale explicitly unsupported. Flow edits write only the canonical component with MapDoc Undo; legacy top-level manifests continue to parse. No new native ECS registry/save/checksum contract; this is authored document identity, NOT completed gameplay ECS/physics migration. Scene/game screenshot requests now fail closed if viewport unavailable and do not create a notification over their own image.

External AlpineValley active map replaced packed valley entity with1391 stable IDs:48 TerraTerrain tiles,967 TerraTree,221 TerraRock,154 TerraGrassPatch and1 NereidWater. Shared assets under content/models, generated legacy rules bindings match canonical StaticModel refs. Generated assets~992MiB remain local/reproducible; native/install-entities.py and recipe/notes persist the conversion. Grass patches are individual document entities, NOT individual tuft/blade identities. User wants all environment objects as entities; per-tuft authoring remains a known unfinished part rather than an implied ECS conversion.

Executed: Release arm64 aiverse_editor and aiverse_editor_scene_adapter_tests builds PASS; final targeted CTest editor_scene_adapter PASS0.33s. Coverage includes new blank-map entity mode, authored XYZ/Y/scale/yaw, Undo/Redo, invalid atomic edit, duplicate identity, translated water and component-owned flow Undo. Initial build target AiverseEditor failed (wrong target name); corrected aiverse_editor. Two temporary Python editing scripts had indentation errors before any execution; corrected and reran. No current build failure. Actual final Metal editor via local API rendered1390 Terra model nodes plus native water; LOD chains1342, blocked0, shadowLimited0, camera levels48/834/508 at initial camera (counts are nodes, not draw calls/FPS). Clean scene capture /private/tmp/terra-entity-clean.png inspected: green rectangular standard ground absent. Actual map_new32x32 capture /private/tmp/terra-empty-entity-map.png inspected: empty dark viewport, no floor. Returned test editor to maps/valley.json afterward. Screenshot-only API never captures desktop; full-editor fallback prohibited for scene/game requests.

Studio HTML5 counterpart code f1aaffadab5b4212a3688955aa367d4622132248 on feature/studio-html5: stable entity:@ID, real model component transform/asset, Inspector Y/yaw/scale, masked/LOD rigid render support, no automatic plane/grid. Native app/tests built PASS; CTest editor_scene_adapter + studio_terrain_geometry + studio_session3/3 PASS6.19s. Its actual UI visual QA remains NOT VERIFIED; prior CUA session failed and desktop screenshot was auto-review rejected, no bypass. Root unrelated AGENTS/npm changes preserved.

CI/full Terra/NEREID regression/Vulkan/mobile/shadow pixel parity/runtime collision-buoyancy NOT RUN. New metadata defaults preserve old loaded-map rendering; no production MDI/ICB flags enabled. Current forest/mountain/water optics are still not accepted as final beauty art; river looks too transparent,10species/age/winter and per-tuft editing remain. Main untouched. Next: finish per-tuft entity/presentation strategy without unbounded meshes/DOM, precise masked/LOD/water picking and runtime ECS ownership; separately repair water optical visibility and continue final tree/mountain art before merge/gallery publication.


## 2026-11-20 camera-triangle diagnostic checkpoint before main/HTML5 sync

Code3cfb95bd1fa82d5cfcea31e46f55fdb4896fc84f on feature/aiv87_terrain_pbr. Editor geometry_stats and renderbench report actual camera-model triangles and sun-shadow model submissions separately, using existing RenderProfile counters (no new timing). Renderbench exposes camera LOD levels/shadow-limited counters. Native editor Release build PASS; renderbench header change NOT rebuilt in this stage. Actual unchanged native scene captured near65m/mid140m/far300m at identical target256,220,pitch45,yaw180,903x605. Evidence /private/tmp/terra-trees-before/{near,mid,far}.png and metrics.json. Camera model triangles4,796,779 /12,402,662 /13,223,809; LOD levels201/34/30,481/135/124,277/727/352. ShadowLimited0/blocked0. Counts differ in visibility, not an FPS comparison. This confirms user's15M concern is plausible: LOD exists but tiers/thresholds/asset geometry remain too expensive. Do not claim fixed budget from mere LOD counters. Water supports autoLod in native host; audit authoring opt-in/import/consumer for all requested types, including shrubs.

User steering: fetch fresh main973b32970654cc5318fa6b6571f35d020501dcdd and feature/studio-html5, merge BOTH into this working Terra branch before continuing trees. Root Studio checkpoint24de8603acb115e0530ef5fde75b88528ee9e796 preserves independent CPU camera/shadow LOD with GPU-authored packet exclusion and opt-in native diagnostic log. Root app build PASS; root renderbench target missing in existing config, not a shader/compiler failure. Model policy build PASS. No tree geometry changed yet. Preserve root unrelated AGENTS/npm changes. Main has1137 incoming commits from this branch's baseline; resolve semantically rather than overwriting core Renderer3D. Next immediate action: merge main then current HTML5 (including local Studio checkpoint/handoff), compile integrated checkout on KINGSTON and resume native tree canopy/LOD-budget work. Main itself must not be modified or force-pushed. CI/full regression/new tree visual acceptance NOT RUN.


## 2026-11-20 main integration before HTML5 sync

Merge code a25d8ade51259b768199e3d01820a9ac0a6422a6, branch feature/aiv87_terrain_pbr, parents Terra366b8e100 and origin/main973b32970654cc5318fa6b6571f35d020501dcdd. User explicitly requested both main and HTML5; this is the first merge only. Resolved19 files semantically: retained new main GI/Luma/LENS immutable terrain preparation and GPU packet identities; retained Terra PBR uniforms/shading and NEREID host, no implicit terrain flag, independent CPU camera/shadow LOD. GI contribution and Terra dielectric PBR coexist in GLSL/MSL. Staged TerrainRenderer clones now preserve mapSurfaceEnabled so secondary captures do not restore hidden ground. SpectraFX uses main's validated resource-retirement call labels. CMake includes both new main tests and native Terra exporters/tests. Skills catalog regenerated/check PASS236; README translations updated to236. New instructions mandate ALL future build/temp/compiler/module outputs on KINGSTON; source worktree remains /private/tmp/aiverse-terrain-pbr.

Executed: copied prior668MiB feature build dependencies to /Volumes/KINGSTON/AIVERSE-builds/terraweave-unified/native, relocated CMakeCache paths and configured merged main with Studio+renderbench enabled PASS. Log /Volumes/KINGSTON/AIVERSE-builds/terraweave-unified/configure-main.log. Existing external Studio web bundle is only a configuration placeholder until fresh HTML5 merge/build. Compile/tests/Metal/Vulkan/CI for merged result NOT RUN yet; next merge requested HTML5 before final build. git diff cached check reports inherited main trailing whitespace in unrelated docs/STRATA/tools; resolved files have no conflict markers and scoped whitespace check is clean. No engine main push or force operations. Root original checkout AGENTS/npm changes remain untouched. Next immediate action: merge local feature/studio-html5 dffc3e8e12794c1935df2d1ff3b6d45fb75d7a72 (contains freshly fetched remote545e3daf3 plus this chat's LOD checkpoint), resolve UI/lighting/authoring integration, separate merge handoff, then build/verify on KINGSTON and resume tree/LOD art/budget work.


## 2026-11-20 HTML5 merged into main-synchronized Terra

Merge 070ea79054e2a98ccc63a3e7ab11d5701794716d on feature/aiv87_terrain_pbr includes local feature/studio-html5 dffc3e8e12794c1935df2d1ff3b6d45fb75d7a72 (fresh remote545e3daf3 plus preserved LOD checkpoint) and previous main merge a25d8ade5. Resolved24 overlapping files and integrated the remaining native/UI interfaces; original root checkout's dirty AGENTS/npm artifacts untouched. One working tree now contains main GI/Luma/LENS, current HTML5 navigation/game/assets/pinned Inspector, canonical Terra/static/water entities and independent CPU camera/shadow LOD. Shared DocumentEntityIdentity now owns authored entity:@ID as well as stable light IDs; Studio adapters delegate to it. Preserved typed light component edits, NEREID Undo flow and pins. Pinned Inspector reads its own flat fields rather than another selection's components. Main frame-scope cleanup, HDR/PRISMA and lighting owner preserved. Actual assets render independently; fallback lighting proxy model excludes them/water and respects entities terrain mode, so no hidden automatic floor/proxy boxes return. Shared giModelGeometry snapshots real opaque model instances with explicit one-million-face admission, never silently truncates. Oversized GI snapshot reports rejection and retains normal rendering; alpha foliage is still outside opaque GI geometry. Existing Metal lighting fixture explicitly requests legacy terrain receiver instead of depending on a new map's automatic floor.

Executed: CMake arm64 Studio+renderbench configure PASS on KINGSTON; TypeScript check PASS, Vite web bundle PASS /Volumes/KINGSTON/AIVERSE-builds/terraweave-unified/web/dist; web88/88 PASS, PythonUI contracts41/41 PASS. Initial TypeScript merge error missing select destructuring fixed; initial Python contract expected old rules-only asset string/opaque-only rejection, updated to component asset priority and permitted alpha-mask/LOD behavior, rerun PASS. Skills236 checkPASS. Native unified Studio/renderbench/exporter/tests build currently RUNNING in /Volumes/KINGSTON/AIVERSE-builds/terraweave-unified/native, log native-build.log; no compiler error observed at checkpoint. Configured web bundle still previous placeholder: after native build, reconfigure AIVERSE_STUDIO_WEB_DIST to fresh unified dist and rebuild asset packaging before app QA. Old internal-drive build will not be used for further compilation. Original unchanged editor scene /private/tmp/terra-trees-before captures remains baseline13.22M far camera. No tree geometry changed yet.

NOT RUN for final integrated result: completed native build, CTest, real integrated Studio/Metal scene, Vulkan/mobile/CI. Do not call source merge visual acceptance. Next: complete native compile and fix any integration errors; then implement lower-cost native conifer canopy geometry/LODs and sensible screen thresholds, audit all Terra object consumers plus NEREID autoLOD, regenerate prototypes and measure same cameras/actual editor host before main/gallery. Append a separate code/handoff after each bounded stage. Main itself unchanged.


## 2026-11-21 native vegetation LOD checkpoint (art acceptance pending)

Code 8c72af851 on feature/aiv87_terrain_pbr, intended publication feature/terraweave. Added opt-in integer conifer whorls/rounded crown, compact curved bark/sprays/leader and whole-bough far cards. Existing profiles remain unchanged by default. Prototype tool now uses pine60/spruce95 branches; only two families, not ten species/winter completion. Shared grass resident renderer and exporter now build authored density LODs; rock/tree adapters automatically enable independent shadow LOD. ExportModel emits batching policy sidecars. NEREID automatic finite-surface LOD includes observer vertical distance and grade. Renderer shadow-only split items use existing role2, preventing reflection auxiliary re-culling from showing duplicate shadow meshes. No GPU ABI, gameplay ECS/save migration or experimental default enablement.

Actually executed on KINGSTON: Release vegetation/model/arbor/NEREID-contract/renderbench targets build PASS (tree-lod-stage-build.log). CTest terraweave_arbor + terraweave_model2/2 PASS0.64s; exact ecs_nereid_renderer_contract_test1/1 PASS0.27s. Initial regex omitted _test and matched only2; corrected exact invocation. Initial sandbox CTest could not write external log; escalated rerun passed. Earlier integrated native build8targets PASS and integration tests5/5 PASS6.82s including actual studio_metal_lighting. Web88/88 and UI41/41 from previous stage pass. Fresh web dist still must be wired into packaged Studio config. Vulkan/mobile/CI, current shadow-only reflection pixel validation and final editor triangle A/B NOT RUN.

Latest prototypes NOT installed in AlpineValley. Earlier v7 Metal preview still sparse and not final beauty art; latest95branch/upper-biased/whole-bough version has compiled but NOT visually reviewed. Old project editor still renders original assets. Do not claim triangle reduction in active scene from unit-budget tests. User now supplied Witcher grass refs: continuous dense overlapping clumps, height/tint diversity and sparse flowers; current isolated short ribbons unacceptable. Next bounded grass stage must preserve tuft coverage through LOD rather than simply thinning it, keep sand/channel excluded and use native Terra shared generator. Water remains visibly unfinished: repeated pattern, sideways motion and missing asymmetric obstacle/shore foam. Implement in shared NEREID after checkpoint, retain editor-authored/downhill flow; no custom project shader. Final scene-only screenshots and overall tree/grass/water acceptance remain required before main/gallery publication. No main push.


## 2026-11-21 grass reference response and terrain texel diagnosis

Code cbe48a210 on feature/aiv87_terrain_pbr. Shared grass model LOD now retains seeded root subsets and tuft centres, reduces blades within each tuft, compensates width and simplifies segments. Full-detail default unclustered sampling remains compatible. New presentation-only sourceSamples/sampleStride and bounded heightVariationPermille; exporter exposes patch size/variation fields, defaults65cm/45% variation/24cm tuft radius. Existing soil sampler/filter remains authoritative; no new terrain generator or per-blade entities. Quality changes dirty resident patches. Native live host is not reinstalled simply by exporting.

Actually run: KINGSTON Release model-test/vegetation targets build PASS dense-grass-build.log; CTest terraweave_model1/1 PASS0.21s. Regression checks coarse roots remain actual full-tier roots for logical populations2048 and8192 with2048 render cap, existing grounding/exclusions and decreasing budgets. Actual Metal renderbench capture /Volumes/KINGSTON/AIVERSE-builds/terraweave-unified/grass-reference-v1/near.ppm (inspection copy /private/tmp/dense-grass-review.png) reports pipelineFailures0. Generated16 candidate patches but coverage/water mask admits only4 along this diagnostic strip; do not claim16patch dense meadow. Native ribbons look fuller but still artificial flat blades and hard coverage edges; isolated terrain tile shows black horizon and is NOT final user beauty screenshot. No project scene/assets replaced, no full-editor QA, cross-platform/CI NOT RUN. Flowers/wind/grass PBR microstructure and reference-level coverage still pending.

New user steering: ground visibly pixelated. Verified active content/models/native-terra/terrain--1--1_basecolor.png1024x1024, native exporter UV spans1 across64m (tools/terrain/landscape.cpp), yielding16texels/m. glTF sampler omits magFilter, importer ModelGltf.cpp defaults9729linear; this is not nearest-filter pixelation. bake_landscape_pbr.py bakes all repeating high-frequency detail into each coarse macro map, losing source resolution. Correct next stage: reuse shared material/Shader Graph capability if present to combine continuous macro layer weights with repeated high-resolution PBR detail; avoid enormous per-tile16K atlas multiplication or scene-local shader fork. Then actual close/grazing/mid/far pixels and repeat seams. User water complaints, tree species/art and final main merge remain open. Keep main untouched until acceptance.


## 2026-11-21 general bilinear texture verification

Code33adfb5e9 on feature/aiv87_terrain_pbr. User clarified filtering must be general, not Terra-only. Existing common gfx TextureDesc.filter defaultsLinear; SamplerDesc min/mag defaultsLinear, mipLinear. Metal/GL/Vulkan source map to backend linear samplers; ModelGltf defaults linear and retains explicit9728Nearest. No replacement filter implementation or forced pixel-art change. Added real generic2D GPU regression to existing opt-in Metal texture fixture: a2x2 checker sampled at centre must return128 from four texels withLinear versus0 withNearest, tolerance1LSB. Independent of terrain/material/genre. Terra ExportModel now explicitly writes mag9729/min9987 and clamp/repeat wrap for all exported PBR assets, preserving mip filtering and addressing semantics.

Executed on KINGSTON: CMake configure with AIVERSE_TEXTURE_METAL_GPU_TESTS=ON PASS and fresh AIVERSE_STUDIO_WEB_DIST path corrected; build aiverse_metal_texture_gpu_test + vegetation PASS filter-build.log. Real Metal CTest metal_texture_gpu1/1 PASS1.65s, including original texture/VT/readback checks and new PASS bilinear averages four texels / PASS nearest retains authored pixel sampling in LastTest.log. Vulkan/GL hardware tests NOT RUN; backend source inspection only. Studio packaging not rebuilt yet after dist config correction. Existing active project assets not rewritten. No main publish.

New requested Terra material mode still pending implementation: grayscale texture as underlay, overlaid surface textures with0..20percent TRANSPARENCY (therefore80..100percent opacity), independent noise world scale. Must preserve opaque surface/PBR normals/ORM, not make ground optically transparent. Explain this does not restore lost microdetail16texels/m. Next implement opt-in shared/native material path (or clearly label offline bake scope), retain continuous world coordinates/seed and bounded assets, inspect no-tiling near/far. Tree/grass/water final art and10species/winter/age, active-scene LOD A/B and scene-only final screenshots remain open. Main untouched.


## 2026-11-21 soil physics audit checkpoint

Report code/docs commit bab4563585e82fa97b92a920483185753187c6f0 on feature/aiv87_terrain_pbr; docs/terrain/soil-physics-audit-2026-11-21.md records existing substrate/erosion/sediment/root-support/fall/persistence APIs and missing external project integration. KINGSTON builds of real physis_terraweave_fall_bridge and native_micro_physics_compile fixtures PASS; CTest2/2 PASS0.80s. The latter is compile smoke only, not live erosion. Complete erosion-to-root-to-fall demo NOT RUN; active AlpineValley remains static exported entities. No production default or main change. User also requests1K/4m texture density, multiple sand PBR variants and grayscale underlay; pending native material architecture, not fixed by shrinking baked macro UVs. New underwater report investigated: existing camera sampler ignores river grade and native manifest hosts do not update PRISMA underwater state. Next bounded stage repair graded query and shared optical bridge for editor/Studio/renderbench, run regression and actual scene-only immersed capture. Preserve soil audit and do not conflate optics with gameplay water authority.


## 2026-11-21 graded underwater host integration

Code 1ba820ca4b34e9871439e6fb3e97797b7dc589d1, feature/aiv87_terrain_pbr. NereidUnderwaterView now resolves graded local surface/bottom before camera volume admission/priority/depth. New small NereidUnderwaterPrisma adapter maps snapshot to existing PrismaUnderwater; old WorldScene bridge delegates. Shared NereidManifestHost updates PRISMA after prepare in native editor Scene/Game, HTML5 Studio and renderbench. Studio always passes its HDR/PRISMA pipeline even with GI/direct lighting disabled; underwater no longer depends on lighting opt-in. No new water solver, authoritative simulation change or postprocess copy. Editor still respects its Scene postprocess toggle. Temporal underwater snapshot is currently one shared Prisma state; multi-camera smoothing isolation remains a limitation. Optical ray clipping uses local horizontal plane approximation of inclined water, not a full graded-plane ray equation.

Executed KINGSTON: ecs_nereid_renderer_contract_test build PASS and CTest1/1 PASS0.44s, includes graded above/below regression. Full native aiverse_editor build PASS; command then stopped on wrong target aiverse_studio_html5 (actual target aiverse_studio). Corrected renderbench+Studio build RUNNING underwater-host-build.log, not yet a PASS. No GPU underwater scene capture yet. Prepared above/crossing/submerged reference configs on KINGSTON. User video /Users/aleksandrfeer/Projects/video.mp4 decoded via AVFoundation18.033s,12frames; ffprobe unavailable, Swift extraction succeeds with deprecation warnings. Reference shows moving waterline and lingering lens drops after exit. User explicitly assigns screen effects to PRISMA and requires lens film to be separate DISABLABLE/CONFIGURABLE effect, independent of underwater absorption. Next stage add opt-in PRISMA lensWater catalog effect (amount/drop size/density/refraction/drain/dry time), per-view wetness, common GLSL/MSL compositor and real frames. Do not describe current host fix as full video parity. Water tiling/current/foam,1K/4m high-detail terrain, multiple sand variants/noise underlay and final tree/grass art remain pending. Main untouched.


## 2026-11-21 configurable PRISMA lensWater

Code 14ac558b7e1c4bdde5749d5e654aeedf8b19b046, feature/aiv87_terrain_pbr. Optional new PrismaType appended beforeCount; old numeric effect IDs preserved. Independent lensWater catalog entry, default absent/OFF, enabled/amount disable; amount/dropSize/density/distortion/flowSpeed/dryTime/film parameters share parser/serialization/volume/catalog UI. Native setUnderwater uses bounded64 per-view wetness histories; never-wet camera stays dry, submerged camera wets and exit starts complete bounded drying. retireView/shutdown clear lens ownership; disabled/zero/tier-off clears on next input update. Underwater absorption is separate and still works with lens effect absent/disabled. Screen compositor uses bounded9-neighbour jittered variable-size drops plus draining film and two source samples; no new image/target/pass or water/ECS solver. Internal composite U0 adds32bytes, GLSL/MSL field/feature count now array-derived. Output-pixel radius/distortion/speed preserve scale under input reconstruction. Renderbench preview uses real elapsed preview time for wetness, deterministic benchmark uses1/60; captured reports record actual effect execution and camera immersion after timing interval.

Actually executed on KINGSTON: Release lens/volume/NEREID contracts + renderbench build PASS; CTest prisma_lens_water + prisma_volume + ecs_nereid_renderer_contract_test3/3 PASS0.74s. Tests cover dry/no input, immersion/exit/dry interval, nonfinite delta, independent disabled parsing/roundtrip and grade. Final shader/bench source builds PASS lens-flow-build.log. Real Metal captures /Volumes/KINGSTON/AIVERSE-builds/terraweave-unified/underwater-reference: lens-on capturedPrismaEffects lensWater requested/encodedTRUE, lens-off requested/encodedFALSE reason effect disabled in camera profile, cameraUnderwaterFALSE and underwaterBlend0 in both; submerged without lensWater cameraUnderwaterTRUE, underwaterBlend0.999. Each completeTRUE/error empty, pipelineFailures0. Captures are diagnostic finite-terrain views with black horizon, not final forest/water beauty/reference acceptance.

Meaningful corrections: first implementation left hard-coded16feature/23uniform counts, so new pipeline key compiled without lens code. Actual pixel inspection found this; replaced with array-derived bounds, reran real GPU. First regular grid droplets looked artificial; new jitter/size+neighbour version inspected and more irregular, still screen approximation not video parity. Initial reference capture failed because glTF buffer URIs were absolute and importer prefixes directory; changed diagnostic relative paths using read-only ground symlink. System Python3.8 lacked is_relative_to; fixed diagnostic path script. ffprobe unavailable; supplied18.033s video read through AVFoundation12frames (Swift deprecation warnings only).

NOT RUN: latest full editor/Studio rebuild after adding LensWater enum, final editor UI toggle interaction, actual GL/Vulkan/GLES pixels, full regression/CI/performance. Previous underwater-only editor/Studio builds PASS; do not equate them with latest lens application build. PRISMA skill and docs/prisma-lens-water.md updated. No main merge or remote push this stage; full active-scene/tree/material acceptance remains pending. Requested1K/4m detail mapping, multiple sand PBR variants and grayscale noise-underlay material are not implemented yet. User now requests editor: silhouette-only selection contour, visual triangle/alpha picking instead of radius, no generic HP bars (health overlay only opting-in gameplay modules). Next bounded stage inspect/fix shared editor/HTML5 picking/outline and remove generic Scene HP HUD, preserve game module behavior and all dirty user files.


## 2026-11-21 editor silhouette and static visual picking

Code: 5ccdf941a10caa0a3e99b62d6f00abdd6bf206e5 on feature/aiv87_terrain_pbr (intended remote feature/terraweave). Same combined branch as Stage88 by explicit user merge instruction; original root Studio checkout untouched.

Shared ModelSurfacePicking.h implements rigid visual triangle hits, node/world transforms, front/back material policy, UV transforms and nearest/bilinear alpha coverage. Bounds accelerate only. Studio helper forwards; legacy authoring pickEntity records real ModelDraw packets with stable document IDs, validates cache generation and leases model ownership before reads. Existing pickObject already routes authored non-FPS entities to pickEntity; view_pick now calls pickObject itself. Planet rays use actual PlanetCamera. Editor opts into retaining picking geometry and masked/blended base-alpha pixels before upload; default games still release CPU copies. uploadModel appended fifth default argument changes native C++ symbol: rebuild callers; no document/save migration.

Selection mask disables normal extrusion, applies small read-only depth bias and produces exterior contour without coplanar internal holes. Studio enables shared screen outline and replaces AABB decoration with actual cube selection masks. Scene/game-preview Scene suppress generic world/screen gameplay overlays via appended default-true RtsScene.gameplayOverlays; actual Game view preserves module HUD. No authoritative health/ECS changes or experimental batching default changes.

Executed on KINGSTON (TMPDIR and CLANG_MODULE_CACHE_PATH there): cmake --build native --target aiverse_studio_model_picking_tests aiverse_model_batching_policy_tests aiverse_editor aiverse_studio aiverse_studio_metal_lighting_tests -j6 PASS, editor-visual-selection-final-build.log. Final mouse-dispatch/planet correction rebuilt aiverse_editor PASS, editor-visual-selection-route-build.log. CTest -R '^(model_batching_policy|editor_scene_adapter|studio_metal_lighting|studio_model_picking)$' --output-on-failure:4/4 PASS5.61s, actual Metal fixture5.18s. Tests cover alpha holes, bilinear threshold, UV offset, missing alpha fail-closed, face policy, triangle-vs-empty bounds, retention/default release. Python unittest discover tools/studio:41 run,40PASS/1SKIP (source-local Vite dist absent; external configured dist used), editor-visual-ui-contracts.log. No web source changed; TypeScript/Vite not rerun this stage (Stage88 PASS). git diff --check PASS with existing CRLF normalization warnings.

Actual latest native editor loaded AlpineValley1391entities, dirty:false. Owned test editor port47325 rebuilt/restarted; original user editor47324 preserved. Scene-only screenshot /private/tmp/editor-rock-selection-final.png903x605 visually reviewed: exterior white rock silhouette, no internal worms, no HP bar. At camera x253.8/z136.05/distance6/pitch25/yaw135, view_pick(.485,.47) => rock-0054,index1069; (.538,.397) outside contour => terrain-014,index14. Earlier occluded rock-0057 does not steal terrain click. Diagnostic calls same pickObject dispatcher as mouse, not a synthetic radius test. No map/assets saved or changed. Screenshot is selection evidence, not final environment art.

Failures corrected before PASS: picking-retention member initially landed in nested scope and failed compilation; moved to outer ModelResourceCache. Python source contract expected moved helper body in Studio; updated to verify shared engine implementation plus delegate and reran. No hidden test failures claimed green.

Limits/NOT RUN: full FPS/runtime migration (legacy FPS SceneRenderable AABBs and live RTS radius remain), skinned/morphed/baked/GPU-displaced models, exact rendered LOD/crossfade and water/non-Model picking. CPU adapter fails closed for unavailable geometry;4096 packets/1M triangles per model limits. Retained CPU memory/click latency not profiled. Planet camera correction compiled but no planet interaction QA. WKWebView full interaction, GL/Vulkan/mobile pixel parity/CI not run. No remote push/main merge. Next implement shared rendered entity-ID picking for remaining animated/LOD/water/FPS/runtime routes, preserving stable identities; add full overlapping alpha/terrain/animated actual mouse QA. Original terrain1K/4m, sand/noise mode, water current/foam, reference-quality trees/grass and main/gallery acceptance remain open.


## 2026-11-21 missing Y axis correction

Code 4855fae0b0a71cb5a532e2b5445fc60f334c5164, feature/aiv87_terrain_pbr; follows Stage89 and user report missing third axis. SceneWindow still deliberately enabled/rendered X/Z only under stale legacy2D comment although component-authored entities support Transform.y. Enable XYZ for authored entity selection; legacy grid units remain X/Z because their adapter rejects Y. Matching render/hit pivot uses authored height instead of terrain height. Y drag is independent of ground intersection, filters locked/unsupported selection members and writes through existing translateDocumentEntities with one beginEdit per gesture. No new UI panel, save format, authoritative ECS or renderer change. Existing FPS implementation unchanged.

Actually executed on KINGSTON: aiverse_editor Release build PASS editor-y-axis-build.log; CTest ^editor_scene_adapter$1/1 PASS0.04s including authored XYZ+Undo/Redo; Python tools/studio/test_reference_pin_zoom.py18/18 PASS; git diff --check PASS (existing CRLF warnings). Actual new native editor Scene-only screenshot /private/tmp/editor-rock-xyz-gizmo.png903x605 viewed: redX/greenY/blueZ all visible on selected rock-0054. Map dirty:false before restarting owned test instance47325; original user instance47324 preserved. No project file edits/saves. Mouse drag end-to-end NOT RUN; adapter regression proves persistence/undo, screenshot proves display. Vertical drag uses existing FPS-like screen delta scaled by camera distance; exact projected world-axis drag and near top-down interaction remain future improvements. Web/native Studio gizmo unchanged, no TS/build rerun required. CI/GL/Vulkan/mobile NOT RUN; no remote push/main merge.

Next: verify real Y mouse drag/release/Undo and mixed selection in editor, then continue Stage89 remaining picking routes and original terrain/water/art work. Do not infer all environment acceptance from gizmo fix.


## 2026-11-21 1K/4m native PBR detail checkpoint

Code 768fdb19e106c111b1396e3edb4977211634c069, feature/aiv87_terrain_pbr. Shared tools/terrain/terrain_detail_graph.py + build_detail_materials.py build native Shader Graph from existing source PBR: four shared layers grass/sand/rock/snow, each1024px per4m, three2176px guttered atlases and continuous per-tile RGB weights. Mask alpha packs grayscale noise. Optional0..20% gray underlay transparency; surface remains opaque. Existing offline baker adds --detail, retains normal fallback outputs and replaces broad sinusoidal cosmetic bands with continuous world lattice noise. No height solver/ECS/physics changes. Material-only explicit sampleBudget1..16 keeps default8 and four texture bindings/node128; graph uses13samples/126nodes. Renderbench applyGraphs previously compiled programs without loading referenced images; now decodes/binds all graph resources before upload and logs counts.

Executed KINGSTON: shader graph+renderbench native Release builds PASS terrain-detail-build.log and terrain-detail-host-build.log. CTest ^shader_graph$1/1 PASS0.22s, explicit budget/default/domain/bounds tested. Existing Python environment PYTHONPATH=/private/tmp/aiverse-pbr-python with /private/tmp/aiverse-substance-venv/bin/python; system python lacks NumPy. One existing native tile exported to KINGSTON/terrain-detail-v1, --size1024 --detail and atlas package PASS. CPU assertions verified12source images unresized1024 and exact periodic gutters; invalid noise transparency rejected; finite world noise. py_compile3files and diff --check PASS. Actual final Metal report complete:true/error empty/textures8, graph4resources/13samples, pipelineFailures0. Close image reviewed, high-detail grass and sand visible. Technical terrain-only view, not final art acceptance. Main/native editor not rebuilt or installed with these materials yet.

Diagnostics corrected: initial camera missed tile because local0..64 geometry lacked its(-64,-64)placement; fixed only diagnostic node transform. First host run showed white mask because old linked binary did not yet include graph image loading; rebuilt/logged actual4resources and verified report textures8 instead of4. One helper ran from external output cwd and failed relative source paths; reran from explicit worktree. No original map/assets modified. Earlier all-white frames are NOT acceptance evidence.

Limits: only four layers, multiple sand variants still pending; dominant-axis cliffs have projection seams, not smooth triplanar. Explicit derivative LOD cap4 preserves gutters but may shimmer at long distance; full native editor close/mid/far/performance and GL/Vulkan/mobile NOT RUN. Macro weights are offline, not live Terra terrain editing. Shared mask clamp uses actual256..4096mask resolution. New graph JSON sampleBudget appended optional; no old save/API IDs changed. No remote push/main merge. Next bounded stage install/export whole demo with immutable backup/new pack and actual editor LOD/material A/B, then remaining tree10species/winter/age, dense meadow, NEREID/physics/picking/gallery work. User now explicitly authorizes completing ALL listed remaining work, not only the next fix. Do not end by calling the whole request done after this checkpoint.


# Stage91 — full native Terra PBR/LOD scene comparison,2026-11-21

Validation report ed6591fd9299d3c932ad2afbbfeacd54705755cb, feature/aiv87_terrain_pbr, follows detail code768fdb19e and handoff5d35629dd. docs/terrain/editor-lod-comparison-2026-11-21.md/.json contain executed commands and actual counters. Native editor+Studio Release builds PASS on KINGSTON.48tile detail pack and12prototype/154grass native export PASS. Separate1391entity review project at KINGSTON/terraweave-unified/review-project-v1, port47326, own exec83229; original dirty game.json/map/rules/assets and original user editor47324 preserved. Port47325 original asset baseline also remains open (own exec10158). Screenshots Scene-only903x605; near frame visually inspected.

Same x256/z128/pitch25/yaw135 distances65/140/300: actual camera triangles10,949,072 ->2,899,639;16,790,047 ->2,766,159;10,232,616 ->1,888,490. New LOD node counters prove tiers selected. Baseline app predates graph-budget rebuild, so end-to-end content comparison, NOT isolated renderer performance A/B; no GPU/FPS claim. Full graph bound into existing view materials on48terrain types; source meshes/assets not overwritten.

NOT READY: excessively bare leaders/far conifer cards, only two botanical families, grass/reference art, water/shore flow foam, sky/haze/background. Terrain dominant-axis seams and distant mip cap/performance still need validation. No actual Y mouse drag yet; no universal GPU entity-ID picking. Full native gameplay ecology/erosion not wired. No GL/Vulkan/mobile/CI or web interactivity rerun this validation-only stage. No remote/main publish. Next bounded stage repair native canopy silhouettes and add ten real parameterized botanical profiles with existing age/phenology ownership; regenerate and inspect actual near/far before accepting art. Whole user 'делай все' request remains active.


## 2026-11-21 botanical recipes, actual exports and PBR leaves

Code 4c218cee84df82019d2697400227abe380a52010, feature/aiv87_terrain_pbr. Ten named native presets in TerraWeaveTreePresets.h share existing Arbor generator: spruce,pine,fir,larch,oak,birch,maple,apple,willow,poplar. Opt-in integer secondary/tertiary hierarchy, rounded broadleaf envelope, independently seeded scaffold points and upward/downward twig inclinations. Legacy TreeProfile defaults retain prior generation. Append CanopyHabit defaultsAutomatic; explicitDeciduous makes larch seasonal even though conifer-shaped. ForestAdapter consumes habit. ForestScene.addSpecies appended optional mesh argument defaults preserve callers. Offline treeProfileAtAge/treeCanopyAtAge reuse existing PlantEnvironment/CanopyPhenology; live Scene already sizes instances: use UNSCALED profile there to avoid double age scaling. New naturalBroadleaf leaf cards anchor on owned woody segments and grow upward (willow downward), stable skeleton across whole-model LOD tiers. Per-branch extraction now remaps parent leaf ownership to section0 and disables trunk flare/whole-tree leader for nontrunk sections. Compact conifer medium retains crossed cards; thresholds180/48 and wider far cards prefer cover over earlier320/112 aggressive cut.

Exporter --catalog botanical --variants1..8 --season summer|winter --age-ticks/--mature-ticks/--lifespan-ticks; default conifers still12names. V5exports on KINGSTON: botanical-summer-v5, botanical-winter-v5, botanical-young-v5 (10/100ticks), each40prototypes/10kinds and3decreasing geometric LODs. Metadata records seed/species/age/maturity/lifespan/foliage. Winter removes leaf geometry only for deciduous recipes; no snow coat yet. Four new bundled PBR leaf families birch/maple/apple/willow: four original imagegen transparent2x2 atlases in tools/vegetation/pbr_sources and36derived1024maps in engine/assets/materials/vegetation/textures. Source provenance/prompt set in README. First cloudy birch alpha was rejected and corrected through built-in image edit. Existing tuned bark maps preserved; selective baker --only avoids rerunning/replacing old art. Relief derives from albedo detail, NOT a measured scan/translucency solver.

Actually executed on KINGSTON (all TMPDIR/module caches there): final aiverse_terraweave_arbor_test + model_test + vegetation Release build PASS botanical-shoot-direction-build.log. CTest terraweave_arbor+terraweave_model2/2 PASS0.63s (assertions enabled via-UNDEBUG). Includes deterministic presets, younger size, seasonal leaf removal, parent centreline attachment, normalized meshes/LOD budgets, isolated branch foliage and native larch seasonal material retaining bark alpha1/leafalpha0. ForestScene target build+CTest1/1 PASS0.22s botanical-forest-build.log, public optional mesh API checked; last subsequent leaf-direction body change covered by model tests. Native exporter V5three runs PASS. CPU pack assertions PASS36maps1024, alpha holes+opaque leaves, unit normals tolerance.015, metallic0;3x40exports,10kinds, decreasing tiers and expected juvenile/winter canopy. Selective PBR bake PASS botanical-pbr-bake.log, py_compile and diff --check PASS. Three actual Metal captures botanical-summer-0-v5, summer-1-v5, winter-1-v5 report completeTRUE/error empty; each pipelineFailures0, source prototypes/geometry loaded. PNG review copies /private/tmp/botanical-{summer-0,summer-1,winter-1}-v5.png inspected. These are plain blue diagnostic lineup images, NOT final beauty screenshots.

Failures corrected: secondary limbs exceeded former crown bound; conservative hierarchy bounds expanded. Tiny1cm integer limbs need bounded-segment tolerance rather than strict .3.. .95interpolation fraction; upward-only old validator relaxed only for new descending botanical recipes. Source compile failed once due skeleton variable-name collision in regression; renamed leafySkeleton. A following CTest accidentally ran older binaries because shell lacked fail-fast: that run is NOT final acceptance. Corrected set-e build botanical-shared-verified-build.log and subsequent final build passed before real tests. Full vegetation bake would have replaced existing manually tuned bark; restored only our own tracked texture changes, added selective mode, retained new maps. Two helper commands ran from external output cwd and failed relative py_compile/source paths; reran in explicit worktree. Imagegen built-in used, never substitute generated artwork for actual engine frames.

NOT READY: plain lineup still synthetic/pruned, no smooth branch junction welding or terrain-fitting organic roots. Pine/larch lose thin needles at distance. Actual CPU mip diagnostic at alpha cutoff.38: pine coverage13.7% at1024 ->13.0%@64 ->12.1%@32 ->10.2%@16 ->1.6%@8; spruce27.2% ->25%@8. This is real minification coverage loss, not fixed solely by more geometry. Next bounded stage implement common opt-in/derived alpha-test coverage-preserving mip upload with native RHI explicit mip storage, correct shared texture cache identity, unchanged base alpha/pixel-art/compressed assets, CPU+actual Metal regression; then full forest install/near-mid-far again. Do not claim beautiful SpeedTree parity from preset count. Conifer PBR species still share spruce/pine source families; poplar uses shared broadleaf source. Seasonal snow coating and multiple bark/sand families remain open. Botanical source not installed in original AlpineValley nor review-project-v1 (that copy still uses12older flora-v8). No native editor/Studio rebuild after latest botanical header changes, no GL/Vulkan/mobile/CI, no gameplay erosion/physics host wiring, no universal GPU picking or Y mouse interaction. All original overall user 'делай все' tasks remain active, no main/remote publish. Keep terrain1K/4m and measured full-scene LOD checkpoints, original dirty game.json/user editor preserved.


## 2026-11-21 coverage-preserving alpha-test mip upload

Code ed9400f3e42dc53f4a7da8a67a35d25a16ab4662, feature/aiv87_terrain_pbr. Common AlphaCoverageMips.h builds bounded explicit smaller RGBA8mips for ordinary uncompressed static model cutouts; original/baseRGBA unchanged. RGB reduces in linear light weighted by alpha and bleeds one transparent edge texel to avoid bilinear black fringes. Histogram chooses coverage nearest source at material cutoff/base-alpha factor. Subsequent reductions use unscaled alpha, not cumulatively dilated output. <=4096per dimension/4Msource pixels. Model upload derives compatible policy, GPU texture cache includes byte threshold, metadata alphaCoverageMipmaps exposes applied/fallback. Mixed opaque/blended/data/graph image reuse or differingcutoffs disable policy conservatively; nearest/compressed/VT fallback retain legacy paths. Image.preserveAlphaCoverage=false opts out BEFORE upload; env AIVERSE_ALPHA_COVERAGE_MIPS=0 enables same-binary load-time A/B. MutableHorizonModel opts out, GPU patch refuses manual chains so backend auto-regeneration contract survives. No shaders/GPU uniform/RHI ABI/authoritative ECS/save format change; appended ImageCPU fields require native client rebuild. Primary renderbench report counts applied primary images.

Executed KINGSTON: explicit CMake configure PASS alpha-coverage-configure.log after initial new target 'No rule to make target' (not yet regenerated). Final CPU helper/model policy/Metal fixture/renderbench targets build PASS alpha-coverage-color-build.log. CTest alpha_coverage_mips + model_batching_policy + metal_texture_gpu + terraweave_horizon4/4 PASS2.29s, realMetal1.59s. Tests cover sparse coverage, exact source retention, alpha-weighted color ignoring transparent magenta, bilinear color bleed, odd edge pixels, bounds, full per-mip model upload, backend fallback, pixel-art bypass, opaque/shared/conflictingpolicy bypass, baseAlphaFactor and exact threshold cache separation/lease replay; mutableHorizon opts out. Actual Metal fragment test samples same minified alpha-test source: ordinary chain discards black, preserved chain returns opaque white. Existing generic bilinear/nearest and all texture/VT fixtures continue PASS. CPU direct binaries also passed. Final git diff --check PASS(CRLF warnings only).

Actual native pine same scene/4140triangles: KINGSTON/terraweave-unified/pine-alpha-final-0/1-report.json completeTRUE/error empty, primaryAlphaCoverageImages0/1 respectively; both pipelineFailures0. PNG /private/tmp/pine-alpha-final-0/1.png. Identical geometry/camera; source A/B before last color-bleed version also inspected, final captures generated. Simple RGB non-background detection threshold10 counts2471->3235pixels, includes color-filter differences and trunk, NOT an exact coverage ratio or FPS gain. Capture still plain-blue technical small pine; crown quality NOT final. Initial pine source diagnostic13.7%coverage at1024->1.6%@8 explains texture issue. Do not infer every thin needle or full-tree impostor quality is solved.

Limits/NOT RUN: GL/Vulkan/mobile pixel parity, latest editor/Studio app rebuild after Image layout change, whole forest art/triangle A/B with new40botanical prototypes, dynamic source edits beyond explicit reload, temporal AA/alpha-to-coverage/impostor baking. Base-level alpha/RGB intentionally unchanged; per-draw/vertex alpha remains shader-owned. Source cutoff change needs normal reload to rebuild policy. Unsupported explicit mip upload falls back, not a false applied flag. No performance claim; helper allocates only at texture load, not render hot loop. All original broad user tasks remain active: smooth roots/junctions, richer materials/sand/snow, dense meadow, NEREID current/foam/underwater reference, live erosion/fall demo, GPU-ID selection, Y mouse check, sky/final scene/gallery/main publication. No remote/main push.

Next bounded stage: rebuild actual native editor/Studio, install40botanical summer/age/season banks into separate review scene with native no-adjacent-prototype choice, compare actual scene near/mid/far and repair remaining canopy/roots. Also inspect shared material graph fallback: native detail graph needs4source slots(tex13..16); on RHI with16fragment slots modelPipeline currently returns invalid rather than fallback. Implement base-PBR fallback consistently in pipeline/bind/VT paths before making detail mode a default for all game workflows. Preserve current verified1K/4m/48tile graph pack and original dirty project files. Review-project-v1 currently stillolder12flora-v8, ports47325 baseline/47326review; original user editor47324untouched.


## 2026-11-21 constrained-RHI material graph fallback

Code 9dc2ac5eddbaf0ff30d4f245dd245f66393c1cdd, feature/aiv87_terrain_pbr. Shared allocation-free ModelSamplingPolicy.h computes effective graph/base-PBR, VT, R8 alpha and dynamic-GI bindings for BOTH Renderer3D modelPipeline and drawModelBatch. Four graph sources require slots13..16; on16-slot devices use authored base PBR instead of invalid pipeline/dropped terrain. Dynamic GI shifts sources to14 when no VT; re-evaluate admission after graph fallback. If VT+GI cannot fit, use resident material fallback before rejecting base GI. Only effective graph resources bind; below-quality graphs no longer bind unused source slots. Existing32-slot Metal layouts retained, experimental ICB flags unchanged/OFF. No RHI/GPU ABI/save/ECS change. Fallback omits graph-only displacement/material behavior by definition; base authored assets must remain valid. Additional resolver query is read-only, no saved view/LOD decisions.

Actually executed on KINGSTON: fail-fast Release build aiverse_model_batching_policy_tests, aiverse_metal_texture_gpu_test, aiverse_renderbench, aiverse_editor PASS, model-sampling-fallback-build.log. CTest model_batching_policy+alpha_coverage_mips+metal_texture_gpu3/3 PASS1.34s, realMetal1.31s, model-sampling-fallback-tests.log. Admission table/exhaustive16/32-slot combinations cover graph/GI/VT/R8/palette exclusions and oversized source count without unsafe casts. Direct policy binary PASS. Actual native terrain detail graph render detail-policy-metal-report.json completeTRUE/error empty,32768triangles,8textures, Metal pipelineFailures0. This is positive supported-Metal pixel execution; constrained16-slot GL/Vulkan pixel parity NOT RUN (CPU admission tested only). No GPU speedup claim. diff --check PASS apart from CRLF warnings. CI NOT RUN/no push.

Current native editor binary rebuilt with latest botanical/Image layout and fallback; running review editor47326 still older process until restart. Review project original files preserved,1391entities dirty:false. Technical screenshots forest-current-before-botanical.png / forest-current-sky.png show poor crown/LOD silhouette, excessive haze and black lower horizon; NOT beauty acceptance. view_postfx high/scene:true exposes existing physical sky, so no replacement sky needed. Computer-use native inventory reported Mac locked; mouse Y drag/release/Undo NOT RUN, async unlock request sent; socket and Metal work remains available. CUA completed with blocked-interface summary. Original user app47324 untouched. Next: native canopy morphology (flat broadleaf layers, monotonic pine envelope), organic roots/junctions and native no-adjacent botanical palette; export/install separate review project, actual near/mid/far again. Remaining broad user tasks in previous checkpoints stay active, no final/main publish.


## 2026-11-21 botanical crown-volume iteration

Code41a79fcff57ba91c2f5586055e5a367612eae08c, feature/aiv87_terrain_pbr. Rounded pine envelope is now a bell, not a second monotonic cone. New roundedBroadleaf recipes stratify scaffold heights through the crown; child forks inherit parent vertical growth instead of flattening dy to zero. More primary scaffolds/fewer nested children keep a similar bounded hierarchy; six broadleaf profiles use narrower species-specific proportions, birch lower crown. NaturalBroadleaf leaf shoots get stable per-spray lateral/vertical direction instead of all pointing up. Same native Arbor/model exporter and3geometric tiers, parent attachment unchanged. No new API/save/ECS schemas; changed cosmetic seeded output for these opt-in rounded/child recipes is intentional, not a deterministic simulation migration. Legacy zero-secondary/unrounded profiles retain generation.

Executed on KINGSTON: fail-fast native Arbor+model tests/exporter builds PASS botanical-volume-v8-build.log; CTest2/2PASS0.68s. Pine-envelope regression plus existing determinism/parent attachment/normalization/season/age/LOD checks. Export40prototypes/10species botanical-summer-v8 PASS; every3tier triangle count decreases. Actual Metal lineups botanical-summer-0/1-v8 reports completeTRUE/error empty, primaryAlphaCoverageImages1, sceneTriangles42784/97120; each pipelineFailures0. PNGs /private/tmp/botanical-summer-0/1-v8.png inspected. V6 produced overly upward/umbrella canopy and was rejected as final; V7 restored vertical spread, V8 corrected width and varied leaf-shoot directions. V6/V7 artifacts retained for comparison, not install candidates. diff --check PASS(CRLFwarnings). CI/otherGPU NOT RUN. These remain technical blue-background renders, NOT SpeedTree parity or final beauty acceptance: maple/young broadleaf still pruned-looking, willow asymmetry and thin conifers need work, smooth roots/junctions absent.

Owned review editor47326 restarted on rebuilt9dc2ac5edgeneration, session19731, original user47324 untouched. Existing flora-v8 content still installed; same near view2899639camera/2378576shadowtriangles, LOD186/382/109,0blocked. PRISMA high/scene:true exposes existing sky. New botanical source changes not rebuilt into editor yet; exported glTF can be inspected without that. Mac locked so CUA Y mouse check still NOT RUN; no unlock response yet. Original project untouched.

Next bounded stage native botanical placement palette/export: keep every entity transform and select species/seeds with globally stable neighbor coloring; install40V8models in a separate review scene, inspect near/mid/far. Then organic roots and smooth branch attachment; grass/ground/snow/water and final scene/site still pending. Water diagnosis: NereidWater entity exists, foamWidth0; preview uses manual/automatic grade existing API. Do not call it fixed yet. Potential alpha VT exception: Model.cpp intentionally excludes reduced VT fallbacks from manual alpha mips; full-resolution R8 uses ordinary mipmaps. This is a real unhandled optional path but NOT proven cause in current editor (no configureModelVirtualFallback call found in native RTS path). Do not conflate observed sparse silhouettes with a proven VT bug. No remote/main publication.


## 2026-11-21 native botanical neighbor palette and editor install

Code49392abc6b0e55b72d08f3c7fa3efd20209afcba, feature/aiv87_terrain_pbr. TreePresets treePrototypeVariant reuses global rockVariant four-color cell key: with>=4species all8neighbors differ in species; fewer species require>=4variants and differ in prototype ID. Bounded16species/8variants, no mutable cross-chunk cache, negative coordinates stable. Guarantees neighboring cells, not arbitrary Euclidean nearest-neighbor exclusion after editing/removing instances. Exporter botanical --placements LANDSCAPE/catalog.json --placement-spacing-cm550 preserves source pose fields, emits variant/kind/prototypeSeed in appended catalog.placements. Default conifer output unchanged; no scene/save/ECS authority schema migration. Public header additive; offline palette does not install live ForestScene ecology/physics.

Actually executed KINGSTON: exporter+Arbor build PASS botanical-palette-build.log, CTest terraweave_arbor1/1PASS0.47s. Tests enumerate negative/positive grids,1/2/3/4/10/16species, all8neighbors, all IDs exercised, invalid insufficient palette rejected. Botanical-palette-v1 export40models +967placements PASS. Python verified every xCm/yCm/zCm/yaw/scale field byte-value equal to original967records, all40IDs/10kinds used. External installer /private/tmp/install-botanical-review.py copied review-project-v1 to review-project-v2 with symlinks preserved, added40ordinary rule types/StaticModel paths;1391entity identities/transforms retained, original project untouched. View tree metadata records native source seed/age. No custom geometry/scatter solver.

Owned editor47326 reopened review-project-v2, session43171; native binary generation9dc2ac5ed, reads latest exported models. Real Scene-only forest-botanical-near/mid/far.png captured atsame65/140/300camera. Actual camera triangles7543271/11125367/8968318, shadow8052728/12809006/7730916. LOD near445/166/92, mid551/309/289, far145/838/400;0blocked. This is a budget regression against earlier12coniferpack, NOT accepted performance: broadleaf220tinywoodysegments each receive8radial*4longitudinalrings at high detail, medium5*2rings, although nontrunk geometry is straight. Newly visible variety is real, but beauty/SpeedTree quality remains unfinished. Next bounded stage remove redundant longitudinal subdivisions on straight broadleaf branch tubes, adapt radial detail to twig radius, tune medium/far tiers and re-export samepalette/camera; do not lower geometry blindly or claim low counts alone prove quality. Existing actual LOD works; source high/medium cost needs fixing.

No main/remote push, no CI/otherGPU/animated selection/Ymouse acceptance. Source doc diff --check PASS(CRLFwarnings). Mac stilllocked; user app47324/baseline47325 preserved. PRISMA high enabled in owned preview only. New water/grass/root fixes notstarted in thisstage. Technical frames retain haze/poor water/unfinishedskyhorizon. Need actual organic roots, smooth junctions, PBR snow/bark/ground expansion, meadow, NEREID shore/current, live erosion/falls, GPU picking, final site/screens before overall completion.


## 2026-11-21 broadleaf tube geometry and LOD budget

Codef4f51fe9172902fba8dda1f7e8d9893f49468c49, feature/aiv87_terrain_pbr. NaturalBroadleaf nontrunk tubes have straight linearly tapered centrelines: use one longitudinal span instead of subdividing the same planar faces4times. Radial3/4/5/8 capped by requested tier follows twig radius; retain every woody segment, leaf, attachment and seeded position. Trunk flare/curved conifers unchanged. Authored broadleaf LOD thresholds180/64pixels instead of128/32. No public/save/ECS changes; camera/shadow LOD still Renderer3D-owned per view, no experimental defaults enabled.

KINGSTON fail-fast build Arbor+model tests/exporter PASS botanical-twig-lod-build.log; CTest2/2PASS0.60s. Export botanical-palette-v2 PASS, placements exactly equalV1. Example high/medium/low triangles oak19424/7940/2206 ->7012/5112/2206; birch21184/8660/2406 ->7360/5396/2406; maple19424/7940/2206 ->6898/5028/2206; apple14144/5780/1606 ->4992/3638/1606; willow19424/7940/2206 ->6946/5064/2206; poplar22944/9380/2606 ->8122/5926/2606. Existing finite-normal/index/LOD/determinism tests passed.

Actual editor same1391entities/967trees/10kinds/40prototypes in separate review-project-v3, only asset pathsv1->v2. Owned47326 session98057, user47324 untouched. Same65/140/300cameras: camera triangles7543271->4301331,11125367->5988193,8968318->5844502; shadow8052728->4253028,12809006->6548348,7730916->5387774. LOD418/193/92,442/408/299,77/756/550;0blocked. Scene-only forest-botanical-lod-near/mid/far.png inspected. Near keeps canopy/branch arrangement; far still noisy/hazy and is NOT beauty acceptance. Draw calls1370/2581/2672 increased against prior1262/2347/2260 as LOD/material groups redistribute: do NOT claim FPS/GPU speedup from triangle reduction. No fresh renderer binary required to read authored glTF; C++ editor source rebuild after this header stillpending. CI/otherGPU NOT RUN.

Next organic basal root/trunk flare as one continuous bark skin, then branch junctions; existing conditional exposed-root model/soil lifecycle remains owner of erosion-visible roots. Existing TerraWeaveImpostorAtlas is disk-rasterized temporary silhouette, not a PBR mesh bake; do not insert it into final forest just to claim tiny triangle counts. A real multiview albedo/normal/ORM bake remains an open shared rendering feature. All broader user goals stillactive; no remote/main push. Mac locked/mousecheckpending.


## 2026-11-21 outward bark faces and continuous basal root skin

Code4e030eef90b20b54ed62d1cfdece1d5fe715d8f5, feature/aiv87_terrain_pbr. Critical shared mesh bug: tube triangles had opposite orientation to their outward normals. CPU dot(faceCross,vertexNormal) was negative; Metal uses CCW/backface culling, so visible bark was the hollow back wall. Reversed woody triangle order globally, including ordinary branches and conditional root tube consumers. Added outward-facing regression on every bark triangle in organic fixture. No renderer/RHI workaround or double-sided bark. This intentionally fixes old visual/shadow culling, no save/authoritative state change.

New trailing ArborMeshSettings organicRootFlare=false, botanical exporter enables it. One continuous trunk skin with5..7seeded asymmetric buttress lobes, buried open foot ring, square-root shoulder to flatten toward soil; high/medium/low root tube48/32/16sides and10/6/3rings. Surface derivatives supply outward normalized normals; barkV follows radial shoulder rather than onlyY. No capped separate basal struts. Existing conditional exposed-root/soil/erosion/fall owner untouched. Default/isolated trunkFlare=false path retained. C++ struct layout changed: rebuild clients. Cosmetic source skeleton/placement IDs unchanged; no new persistence schema.

KINGSTON fail-fast Arbor/model/exporter build PASS botanical-root-shoulder-build.log. CTest2/2PASS0.66s; finite positions/UVs, unit normals, valid indices, buried asymmetric foot, disabled-flare nonnegative wood, positive outward face dot. Previous root-skin/winding builds also2/2PASS, not failures. Export botanical-palette-v5 PASS40nativeprototypes/967placements; placement records exactlyV2. Oak7012/5112/2206 ->7908/5476/2296triangles(root tessellation cost). Actual Metal root-skin-shoulder-report.json completeTRUE/error empty,40676scene triangles,8textures, pipelineFailures0. Uses native oak +actual Terra tile terrain--2--1 translated to original placement; sample tile height2.33m vs treeanchor2.2m. PNG /private/tmp/root-skin-shoulder.png inspected. Earlier V3skin andV4winding captures root-skin-close/after retained; camera changed between first/after, NOT a same-camera winding A/B.

NOT FINAL: root bark still stretched/diagonal on shoulders, root toes remain broad and need multi-angle/slope refinement. Existing tools/vegetation/bake_bark_atlas.py already solved object-space triplanar sample blending/xatlas on older authoring meshes (112030a39), so REUSE/extend it for native glTF instead of another projection generator. It currently accepts a single numpy part, returns unwrapped geometry/PBR textures; no CLI for native multi-LOD glTF. Next bounded stage adapt native glTF bark-only UV/PBR authoring around this existing baker, preserve all native triangle positions/foliage/LOD refs and group layers into bounded shared atlas, test one oak close before full pack. Avoid claiming roots fitted to all slopes or exposed-root simulation installed. Branch junction anatomy/UV stillpending; no full forest roots install yet (review-project-v3 stillpaletteV2).

Current owned47326 session98057; user47324/baseline47325 untouched. Maclocked mousecheckpending. Full native editor C++ rebuild after Arbor header changes NOT RUN; currentbinaryreads exportedglTF. OtherGPU/CI/performance NOT RUN. All previous scene/water/grass/material/PRISMA/physics/picking/site goals active; no main/remote push.


## 2026-11-21 native multi-LOD bark authoring fallback

Codedfb6dde9e3bf5b02e94067dfcb9252ddb2a52c62, feature/aiv87_terrain_pbr. Reused existing bake_bark_atlas.py object-space triplanar SAMPLE blend/xatlas pipeline, added bake_bark_atlas_parts (old single-part API preserved) and strict rigid-native bake_native_bark.py CLI. One shared PBR atlas across1..4woody LODs; append UV-split positions/normals/UV/indices, exact triangle positions/order checked, no remeshing/newtree generator. Foliar primitives and mesh LOD references unchanged; new UV removes stale tangents for normal native loader regeneration. Native generator guard, no animation/skins/extensions/unsupported attributes, bounded triangles and256..4096resolution; existing destination refused, failure may leave partial fresh directory. Source files untouched. Standard glTF PBR/no engine/save/API layout changes.

Executed KINGSTON: native-oak-bark-v1 bake2048 PASS,3LODs bark2628/1956/1416triangles. PBR image audit3maps2048RGB, finite/unit normaltexture tolerance.015, ORMmetallic0 PASS. CLI exact geometry checks PASS, leaf primitive equality PASS. Actual Metal root-skin-baked-report.json completeTRUE/error empty,40676triangles(sameasbefore),11textures, primaryAlphaCoverageImages1, pipelineFailures0. PNG /private/tmp/root-skin-baked.png inspected: stretches reduced but whole-tree3LOD atlas visibly soft at close range. Legacy wrapper256planarquad smoke exacttrianglepositions PASS; py_compile with KINGSTON cache PASS; diff --check PASS(CRLFwarnings). CI/otherGPU NOT RUN. No40prototype atlas install/fullforest rebuild.

Do NOT default to40unique2K/4Katlases: high GPU texture residency and lower texel density across3LODsurfaces. This optional authoring tool is useful for ordinary PBR fallback, not final close bark acceptance. Next preferred bounded stage shared native ShaderGraph object-space smooth triplanar bark, using existing operators and TWO source textures: pack source height into opaque basecolor alpha, keep ORM; blend3projections (6samples), compute surface-gradient bump from blendedheight/world derivatives, worldN and worldPos. Existing compiler ops objectPosition/worldPosition/normal/cross/ddx/ddy/sign/abs/dot3 allpresent. Derive blend weights from abs(cross(ddx(objectPosition),ddy(objectPosition))) normalized(power4); objectcoords keep texture attached under instance rotation, worldderivatives frame keep bumpcorrect. Two textures fit16slotRHI with dynamicGI(14/15), unlike4source terraingraph. Keep coloralpha1, metallic0 and source material PBR fallback. Must visually verify actual root/camera/yaw and compile nativegraph; no custom runtime shader/renderer fork needed if graphfits128nodes. This is design nextstep, NOT implemented.

Tree root source palette-v5 currentgoodgeometry incloutwardbark; fullreview-project-v3 stillpalette-v2withoutroots/flip. Native editor source rebuild after Arbor header additions pending. Original project/usereditor preserved. All broad user goals active; no remote/main/sitepublish. Maclocked mouseY/Undo stillpending.


## 2026-11-21 shared close-detail PBR bark and material graph AO

Code805582709ab3e994a7963d03f97d7e146c2092ca, feature/aiv87_terrain_pbr. Added generic material outputs.occlusion (Rclamped0..1) to compiler and Renderer3D GLSL/MSL surface injection; old graphs keep ordinary AO, postfx/particle reject it. No RHI/GPUuniform/ECS/saveABI change; graphv1 adds recognized output and requires updated compiler for new assets. Legacy ImGui output selector NOT extended/no GUIparityclaim. Native renderbench top-level materialQuality low|medium|high adds exact tier control and report; existing profilelow->Medium behavior unchanged.

Shared bark_projection_graph.py uses existing ShaderGraph operators, no customshader/rendererfork: object-space three-axis smooth projection, world-space surface-gradient height bump, projected ORM/AO, opaquealpha, metallic0.73nodes/6samples/2sources; sourcepackedheight in albedoalpha(neveropacity), aligned originalRGB unchanged, ORMbytecopy. Derivative cross/finalnormal scaled before generic epsilon guards to keep macro cameras stable. Four bundled1K oak/pine/birch/sprucefamilies (~14MiB) in engine/assets/materials/vegetation/bark_projection, sourcecatalogrepeat1.5m; four opt-in inherited *_bark_projected definitions in childmaterials.json. Merge BOTH parent+childlibraries wheninstalling; snapshots/projectrelativepaths, standardPBRfallback unchanged. No per-treeatlas or texturecopyperinstance. Normalrelief authoredheight*.35, not a scan. Two source slots14/15 fit16-slotRHIwithGI under verifiedCPUadmission; actualGL/mobilepixels notproven.

KINGSTON actuallyrun: shader_graph_tests+renderbench builds PASS bark-graph-renderer-build.log; CTestshader_graph1/1PASS0.56s(outputaccepted/rejecteddomains). NativeEditor+Studio builds PASS botanical-bark-editors-build.log after Arbor/Image/lens/bark changes, alltmp/modulecachesexternal. Renderbench quality host finalbuildPASS bark-graph-quality-host-v2-build.log. ActualMetal finalroot-skin-projected-v2/yaw-v2/low/macro reports completeTRUE/errorempty,40676trianglessamegeometry; tiersHigh/High/Low/Low, eachpipelineFailures0. PNGs /private/tmp/root-skin-projected-{v2,yaw-v2,low,macro}.png inspected: retainedclosebarkdetail vssoftbakedatlas, no cylindricalrootstretch attheseviews. SeparateactualMetal AO0->1 fixedviewmeanRGBdelta42.34645 in barkROI; validatesaoaffectsambientPBR, NOTGPUperf.4family liveeditorshader_graph_validateallok73nodes/6samples.Standalonecompilerhelper +glslangValidator8/8GLSLES300/desktop330fragment compiles PASS; helper/buildsourcesonlyKINGSTON. BundledRGBA exactsourceRGB+height range,ORMbyteidentity,4librarybaseIDs/graphsnapshot/resourcepaths auditPASS.

Corrected verification failures: rotatedglTFfixture initiallyusedabsoluteURIs; importerprefixeddirectory and reportcompleteFALSE. Fixed withrelativebuffer+textures symlinks, reran successfully. First exact-quality config placed materialQualityinsidevalidatedlook; rejected unknownkey, moved to top-level and reranallfourcases. Nativebenchcanexit0onfailedreport: ALWAYSassertcomplete/erroraswellasexitcode. GLSL helper initiallygavebothversionssamefilename; fixed300/330namecondition andreranall8. SystemPython3.8 lacksstr.removesuffix duringreview4preparation; correctedsuffixslice andcompletedcopy. No sourceasset/userfilelost, failureswerenotcountedaspasses.

Actual editor review-project-v4:1391entities/967trees/40prototypes/10kinds, updatedonlytreepackV5 inclorganicroots/outwardfaces and40barkslotgraphs withshared4familyresources. OriginaldirtyAlpineValleygame.json anduserapp47324 preserved; baseline47325unchanged. Owned47326 session37501/PID76242 atcapture. Mapdirty:false. Scene-onlyforest-root-bark-near/mid/far.png:camera4679369/6537203/6199520triangles;shadow4746554/7143922/5691738;LOD418/193/92,442/408/299,77/756/550;0blocked. Rootadds~0.35..0.55MtrianglesversusrootlessV2pack, noFPSclaim. Newnativeeditoractuallyrendersgraphs; numericeditorpipelinefailurecountnotexposed, onlyrenderbench0countsareproven. Scene stillnoisy/hazy/poorwater/mountainshape, NOTfinalbeauty/SpeedTreeparity. NativeStudiosourcebuildverified, CUAUIstillMaclocked; nofullvisualparityorYmouseUndoacceptance. ShaderGraphskillapplied.

Read-onlyfetchorigin--prune succeeded:origin/main973b32970,origin/feature/terraweavef5d9dbcad, noTerraremote-onlycommits atinspection. No push/mainmerge/CIrun. Currentoverallgoalsremainactive. Nextboundedwork: screenshotagentAPIforcescamera.targetY0 (EditorAgent.cpp67), makinggroundcloseinspectioninraisedentityterrain awkward; addoptionalYthroughsharednative/Studio camera protocol, preservedefaultandreadonlycapture. Then nativecurvedbranchattachment/junctionanatomy; existing exposed-rootphysics staysseparate. ForNEREID docs373+alreadyprojectautomaticgradevelocityontonearestsplinetangent—doNOTreinventthatkernel. InspectactualGPUtileuniform/UVflowandfoamWidth0againstrealframes. Densemeadow/multiplesand/mountains/groundtiling/snowcoat/liveerosion-falldemo/PRISMAfullreference/GPUuniversalpicking/finalscenesite/screens/mainstillpending.


## 2026-11-21 3D camera height for actual root inspection

Code9b3a2c9c024ca02b34bb9abe3aa366732c1009ab, feature/aiv87_terrain_pbr. Native EditorAgent view_screenshot acceptsworldtargetY instead offorcing0; x,zwithomittedY remains0, Yaloneupdatesheight. DistancecanbechangedwithoutXZ. Pitch/yawdescriptorsnowmatchalreadyexistinghandler. Allnumericcameraargsvalidatedfinite/abs<=1M intoa localcopy beforeapplyinganything; malformedX/Zpair/numericfieldreplieserrorwithoutpartialcamera/devicechangeorqueuedshot. view_focus acceptsY, validatesbeforeflight, preservesplanetfacefocussemantics(noYprojectionthere). SharedWorkspaceschemasupdated; noGUIwidgets, simulation/entity/mapmutation, renderercameraalgorithmsorStudioallowlistchanges. Studioindependentcamera/screenshottransportnotimplementedbythisstage.

Actuallyrun KINGSTON: nativeEditor+Studio buildPASS camera-target-y-build.log. Owned47326 restartedreview-project-v4 session54870, original47324/baseline47325unchanged. Screenshotatbirchentitytree-0000 nativeworld166.19,2.2,88.07, cameragoalY2.6,distance3.8,pitch18,yaw135 showsgroundcontact/PBRbark. /private/tmp/editor-root-camera-y.png vsdefaultY0meanRGBdelta67.61 provesoldplanarframingdiffers. view_focusY2.6alsoacceptedandcaptured. InvalidpitchstringwithXZ999andY2Mrejected, invalidfocusYstringrejected. Before/afterinvalidshotbirchmaskIoU.998274; initialstrictPNG-byteidentityassertFAILED(max28,mean.32liveframedrift), notacameramove; doNOTcallpixelidentitypass. Switchedtoexplicitstablegeometrysilhouettemetricandvisualcheck. Mapdirty:false/1391entitiesunchangedafterallcalls. No newunitmirrorfortinylowimpactcameraadapter; realnativecaptures+negativeprotocolcasesused. diff--checkPASS(CRLFwarnings),CI/otherhostsNOTRUN. Macstilllocked/YgizmomousedragUndoNOTRUN.

Actualground-closeSceneconfirmslargeunplantedgreenareasdespiteflora. Nextboundedstage inspectnativegrasscoverfilter: tools/terrain/landscape.cpp excludeswhenheightminuswaterplane<270cm evenfaroutsideriverfootprint; vegetation.cpp repeats270cmglobalplanecheck. This mayremovelegitimatelowlandgrass, notmerelylowbladecount. MatchTerraPBRsandmask/NEREIDactualfootprint/shorebuffer, preserveno-grassonsand/water andnativeaggregateGrassPatchLOD. DoNOTwriteanothergrassgeneratororblindlymultiplyeverypatchbudget. Densemeadow,wateroptics/foam/current, mountains/sand/snow,organicbranchjunctions/livesoilphysics/GPUuniversalpicking/finalgallery/mainstillpending.


## 2026-11-21 finite NEREID shore masks restore native meadow cover

Code 23dde575c0ffd9530bf7d03c480989c79213b537, feature/aiv87_terrain_pbr. Added optional fixed-point waterFootprintDistance for finite box/disc/polygon/river bounds and Terra shoreCover authoring helper. River distance is a projected centreline/width approximation, not exact tapered SDF; existing water membership/contact and authoritative state unchanged. Ground v2 shares graded sand/wet masks between native PBR baker and grass exporter. Native mask replaces the erroneous additional infinite water-plane exclusion; v1 compatibility retained. Recipe exposes bank width, sand limit and grass maximum elevation. No live erosion/moisture/fall integration or save/wire change.

Executed on KINGSTON: geometry/landscape tests and both exporters built PASS (native-shore-mask-build.log); CTest ecs_nereid_geometry + terraweave_landscape 2/2 PASS 0.91s. terrain-shore-full-v1 export and four 1024/detail baker parts PASS; detail materials build PASS. Heights and waterHeight unchanged, all48 terrain.bin buffers byte-identical. Eligible ground vertices75954->107323. flora-shore-v1 exported40 prototypes/194 patches versus154. CPU audit1326768 High-LOD blade roots: terrain-height error<1.1cm, interpolated sand<=50.4875 for threshold50 with integer tolerance. Far-river tile sand channel mean43.2019->0. Python compile PASS after cache write rerun with external-drive permission; first sandbox cache write denied. diff --check PASS (CRLF warnings).

Review-project-v5 preserves1391 old entity IDs/transforms and adds40 grass entities;1431 total/mapdirtyfalse. Original dirty AlpineValley and user47324 unchanged. Owned editor47326 session60968. Actual Metal same-camera scene-only /private/tmp/grass-shore-before.png and grass-shore-after.png inspected: lowland grass restored, sand bank remains bare. Camera6366348 triangles, shadows554980,1433 draw calls, LOD492/218/40,729 chains,0 blocked. These are scene counters, not GPU speedup. Native grass still uniform green ribbons; water tiling/foam/flow, organic branch junctions, mountains, snow and full physics/reference/picking/site goals unfinished. No final-beauty claim, push, main merge or CI. Mouse Y/Undo NOT RUN: Mac locked. Next bounded stage improve shared native grass pigment/shape at unchanged bounded population, verify real close/mid/far and preserve tuft-root LOD correspondence.


## 2026-11-21 narrower seeded meadow blades

Code ae519e2f2, feature/aiv87_terrain_pbr. Appended opt-in GrassRenderQuality.naturalBlades=false, resident invalidation includes it. Shared CPU generation adds seeded sweep/width variation, narrow-root shoulder, folded per-edge normals and tuft-coherent pigment/root-to-tip gradient with8% dry tufts. No topology/population/material/texture budget growth. Exporter enables naturalBlades, default halfwidth1cm versus3cm, explicit overrides retained; ordinary rough dielectric PBR, not a new textured-species/wind system. C++ layout requires rebuild; no save/ECS/schema change.

KINGSTON build exporter+terraweave_model PASS natural-grass-build.log; CTest1/1 PASS0.54s, checks repeated seeded vertices/colors, grounded roots, finite/unit normals, pigment variation and identical topology against plain profile. First build FAILED because new test used unqualified render namespace; corrected aiverse::render and reran. flora-meadow-v1 export40prototypes/194patches PASS. Actual native review-project-v6 retains1431entity IDs/transforms, mapdirtyfalse; original user app/project untouched. Scene-only /private/tmp/grass-meadow-v1.png inspected at prior shore camera: thinner varied meadow, less solid green plank appearance.1433drawcalls same; camera6475462triangles/shadow554980, LOD495/217/39,730chains,0blocked (live settling differs from prior counters, not a controlled GPU performance result). Owned47326 session69397. No nativeEditor rebuild needed for exported-only asset check; resident header consumer full builds NOT RUN this stage. CI/GL/mobile/performance NOT RUN. Textured grass varieties, wind and final art acceptance remain. Next stage fix NEREID authored-current advection versus independent six-direction wave phase and move contact foam to undistorted depth, then verify actual Metal movement/shore frames. Full broad goals/main/gallery remain unfinished.


## 2026-11-21 independent NEREID optics and transported irregular whitewater

Code ac2d3cdff66736ac884b778a7f18cf7e4529a407, feature/aiv87_terrain_pbr. Shared Renderer3D now captures opaque colour/depth for tagged NEREID optical coefficients/refraction regardless of traced-shadow quality. Existing trace2.w=.25 means optics-only;1/2 retain water/model SSR. Ray marching and opaque reflection replay remain trace-gated; legacy water/sprite helper overload preserves old behavior. No GPU uniform layout, ECS water velocity/save/wire schema change. Renderer3D appends explicit setPresentationTime (finite seconds, negative resumes steady clock), renderbench --freeze-time now fixes shader time too; C++ object layout requires rebuild. Current still comes from the existing authored/manual/grade-spline Fixed owner. Native wave/noise/whitewater detail uses worldXZ-flow*time; independently moving six-direction phases no longer override a moving current. Planar and SSR normal distortion uses the NEREID resolved normal, not cloud-offset sines; native planar Fresnel dielectric .02, legacy helper retains its old stylization.

Replaced rapid whitewater sine-product ellipses with transported, stretched domain-warped noise (1/2/3 layers Low/Medium/High). Undistorted contact-depth foam composites after refraction; vertical band no longer changes shore-coverage fade width. Screen-depth obstacle tail uses bounded4/8/16 upstream probes by rapidDetail, overlapping average and transverse taper/downstream fade. Zero shoreFoamDepth disables contact/tail, rapidFoamStrength remains independent. This is screen-space presentation, NOT hydrodynamics/erosion; offscreen/occluded obstacles absent, steep silhouettes/large-world/performance still need QA. Editor geometry_stats appends read-only cameraMeshTriangles/opaqueSceneCapture/ssrRaymarch/nereidConfiguredSurfaces; bench separates capture from SSR. Existing counters retained.

KINGSTON actual commands: final renderbench+nativeEditor+Studio+geometry contracts build PASS nereid-final-optics-build.log; CTest river_grade/surface_mesh/renderer_contract3/3 PASS0.58s. Follow-up nativeEditor diagnostic-only build PASS nereid-editor-capture-diagnostics-build.log. Earlier native-reflection-editors build also PASS but shader source changed during its later Studio dependency pass; explicit final rebuild of ALL targets above is the authoritative result. Full common Lit GLSL + real ColorSpace prefix compiles ES300/GL330 2/2 PASS with glslangValidator; actual GLES/Vulkan pixels NOT RUN. No source/temp/compiler products on internal build dirs.

Actual Metal fixture water-contact-v1 uses existing Terra terrain--1--1 and a native Terra rock. Final final0/final1/reverse/foamoff/low/manual-minus/full/no-water/repeat0 reports ALL completeTRUE/error empty and pipelineFailures0. hdr+Low capturesTRUE/SSRfalse, full captureTRUE/SSRtrue, no-water captureFALSE/SSRfalse. fixed time0 repeat PPM bytes exactly identical; time0->1 mean RGBdelta0.81607 (whole frame, NOT a flow-direction metric; sky clock also changes). Opposite manual1.8m/s vectors produce tail on opposite sides, PNGs inspected. Nine reports retained on KINGSTON/water-contact-v1; /private/tmp/water-final0.png, water-reverse.png, water-full.png, water-graph.png. Grid of white ellipses reproduced strongly with reverse1.8 before the rapid-noise fix, absent in final render. First4probe prototype made detached ghost-rock foam; next8max version made comb tips; rejected, replaced with overlapping4/8/16 average and taper. Source remains procedural with no new texture allocations.

Important NOT FINAL: full native forest still looks too bottom-dominant/brown with weak contact foam, despite actual water geometry2048triangles, captureTRUE/SSRtrue/configured1. ShaderGraph terrain bench reproduces stronger bottom appearance: graph.json/report completeTRUE, pipelineFailures0, shows correct scene-colour/detail involvement. This is a remaining optical/art/composition investigation, not missing surface or disabled capture. Need isolate actual depth/thickness and soil-detail colour before claiming reference-quality water. Review-project-v7 retains1431entities/all poses; only NEREID style .16m shore depth, absorption2, tint1, refraction.24 and existing autoLOD55/170m hysteresis8 tuned. Original user app47324, baseline47325 and dirty AlpineValley files untouched. Owned47326 session12108, screenshots forest-water-native-v7[-hdr|-diag].png. Transform Y+2.5 test through canonical entity:@water-1 succeeded; Undo restored position but MapDoc dirty flag stayedTRUE by its existing contract. Restarted from unchanged disk afterward; no map_save. Invalid initial unprefixed scene entity/component calls rejected with no mutation.

Corrected verification errors: initial GLSL dump omitted mandatory RHI ColorSpace prefix (missing rtsEncodeScene), fixed and reran; initial fixture used file instead of path and unplaced local tile, reportfailed despite native exit0; corrected path and original -64/-64 glTF root placement, all subsequent reports asserted. Existing string contracts expected refracted-depth foam; first CTest failed, updated existing contracts for undistorted foam and explicit non-npos post-SSR order, reran PASS. First reflection wrapper generation malformed before compilation, regenerated from unchanged HEAD source and GLSL verified. One non-escalated ctest -N failed external log write; reran with external-volume permission, discovered absent surface_mesh executable, built it explicitly. No failures counted as passes.

No CI/performance/GPU time or mobile parity claim. No main merge/push/final gallery publication. Mouse Y/Undo interaction still NOT RUN because Mac locked (API Transform/Undo above is not mouse parity). All original tree junction/LOD/art, snow/ground/sand/mountains, live erosion-root/fall host, PRISMA video parity, universal deformed/water picking and final scene/site goals remain active. Next bounded stage: isolate NEREID optical thickness in terrain-graph/full-editor path; source geometry has1.58m actual water-minus-bed at (-12,-32), so do not hide the issue by raising the whole river. Then shared parent-centreline branch attachment/junction anatomy, preserving foliage/LOD ownership.


## 2026-11-21 remove double riverbed compositing; inspect real optical inputs

Code f9d094e336afb9f93c02d733b18da98fc06e1de5, feature/aiv87_terrain_pbr. Resolved NEREID absorption/refraction already includes background colour; old85%-capped mix followed by ordinary opacity added unattenuated riverbed again. Now one absorbed-background/Fresnel surface composite, authored opacity scales optical density/refraction/Fresnel internally and final framebuffer alpha is contact coverage when optics resolved. Invalid depth/legacy untagged water retains old alpha fallback. Contact coverage fades only over3.5cm, independent of foam band, and foam strength scales with authored opacity. Existing style range .001.. .98 unchanged; this intentionally changes NEREID appearance, not save format/Fixed current/body or GPU uniform layout. LDR colour-space approximation/TAA/underwater/backfaces still need broader parity; do not call this full spectral/physical water BRDF.

Explicit read-only AIVERSE_NEREID_DIAGNOSTIC env selects depth,coefficients,basecolor,transmission,coverage shader variants in a separate owned process. Default0 compiler guards preserve ordinary GLSL/MSL; invalid value leaves normal rendering. Red depth/4m, negative green/unavailable blue; other modes inspect actual inputs/opacity after display postfx. No scene mutation, frontend UI/bridge or public struct ABI addition. Diagnostic images are NEVER final screenshots.

Investigation actual Metal water-contact-v1: depth-scene-v2 and depth-graph both centreRGB205,0,0, matching1.58m known ground snapshot; coefficient graph211,131,239; basecolor0,50,58; transmission46gray; coverage231gray. All7 inspection reports completeTRUE/error empty/pipelineFailures0. This excluded missing depth/parameters/source albedo and located excessive background mixing rather than moving river height. Final optical-new/minimum/no-water reports3/3 completeTRUE/error empty/pipelineFailures0, source terrain ShaderGraph13samples/4textures actually enabled. Minimum opacity.001 versus no water: final meanRGBdelta0.0631963, P99=1,max=1; ordinary.88 vs absent has a substantial colour difference (previous iteration47.5261mean), no fake screenshots. First test tried opacity0; existing manifest rejected it as expected, corrected to documented minimum. No widening of valid save/style input.

KINGSTON builds: nereid-optical-alpha-final-build.log all nativeEditor/Studio/renderbench/contracts PASS; contact-final-editors buildPASS with narrowercoverage, CTest river_grade/surface_mesh/renderer_contract3/3PASS0.34s. Last guard-only source build all four targets PASS nereid-default-diagnostic-guard-build.log; renderer_contract1/1PASS0.20s. Actual12 GLSL ES300/GL330 mode0..5 variants PASS before contact constant change; final RAW/default ES300/GL330 PASS with final guards. Raw ES initially FAILED because undefined macro in #if is illegal in GLES (the matrix had explicitly defined0); added #ifndef default0 to BOTH shader bodies, reran raw compile and all nativehosts. Commit attempt stopped on failure before Git mutation. No default-WebGL failure left. Other GPU pixels/CI/performance NOTRUN. Updating a header during an earlier build meant explicit final rebuilds, not relying on partial source snapshots.

Actual owned nativeEditor47326/session52132 review-project-v7: forest-water-optical-alpha.png and final forest-water-contact-final.png show water now blue/green and coast foam line visible with same terrain/poses; inspected Scene-only903x605. Final screenshot prior to guard-only recompilation, which changes default macro preprocessing only. MapdirtyFALSE1431entities. Same camera921748modeltris +2048water/meshtris,372drawcalls, captureTRUE, SSRtrue, configuredWater1; no FPS claim. Original user47324/baseline47325/dirtyAlpineValley untouched. /private/tmp/water-optical-contact-final.png actualMetal standalone diagnostic scene (black finite-ground horizon), NOT final beauty hero. Full forest still has synthetic foliage, repeated sand ripples and flat mountain/composition gaps. No main merge/push/gallery publication yet.

Next bounded work returns to native Arbor organic branch anatomy: current conifer mesh sags branches but source children start on STRAIGHT parent skeleton; the parent mesh/child/foliage must use one shared presentation centreline/attachment offset. Broadleaf twigs are straight1ring; add bounded opt-in curved scaffold/collars while keeping thin-twig budgets and exact leaf ownership/LOD support. Do not bend only woody mesh and leave leaves/children floating, and do not claim intersecting normal-blended collars are actual welded geometry. Shared PBR material currently has no true thin-leaf subsurface scattering (shadowDiffusion is shadow radius only); don't mistake it for SSS. All broad tree/season/ground/mountain/live-erosion/picking/video/site/main requirements remain active. Maclocked mouseY/Undo stillunverified. No new question/approval needed for existing authorized independent work.


## 2026-11-21 shared organic branch centreline, LOD attachments and native wind

Code 22c2bc898 on feature/aiv87_terrain_pbr. ArborMeshSettings appends opt-in organicBranches=false and curveSegments=4; legacy defaults unchanged. Native presentation builds hierarchical BranchCurves once, moves child roots onto the actual emitted parent polyline, and shares curve/attachment with wood, broadleaf cards and conifer sprays. Structural samples remain independent of radial/foliage LOD to prevent attachment drift. Thin straight twigs retain one span. Curved bark V follows arc length. Actual emitted-face area normals replace derivative-at-cusp normals, joining UV seam copies. Seeded shoulder curves, buried narrow child collars and tapered tips remove protruding open-ring shelves; these remain intersecting opaque skins, NOT welded geometry or final SpeedTree parity. Canonical integer skeleton/profile/RNG/save unchanged. Adapter branch pivots use rendered roots so existing wind rotates at correct attachment. New botanicalArborMeshSettings preset factory is shared by exporter/native callers; age/season still host-owned and applied once. Branch-only organic meshes retain whole-parent context. Header layout requires native rebuild.

KINGSTON final arbor-native-appearance-editors-build.log builds nativeEditor, Studio, exporter and model test PASS; CTest terraweave_arbor + terraweave_model 2/2 PASS0.46s. Tests cover old >50cm displaced child versus corrected <.002cm parent-polyline attachment, foliage ownership, independent LOD roots, outward normals, hidden base collar, taper, unchanged skeleton, all10 species finite/decreasing LODs, and real adapter nodeWorld child pivots invariant under 4500cm/s wind at tick53. Earlier analytic cusp-normal attempt FAILED outward test; corrected to emitted-area normals. Geometry draft v6 had LOD root drift, v7 open shelves; REJECTED. Current botanical-palette-v8 and botanical-winter-curves-v2 preserve 40 prototypes/967 placements; winter deciduous leafless, snow coating NOT DONE. Architecture sampling costs more woody triangles: oak7908/5476/2296 ->10050/7330/3484, not a performance gain claim.

Actual Metal macro summer/winter completeTRUE/error empty/pipelineFailures0: 42818 and37538 total triangles including32768 terrain. Final branch-summer-buried.png / branch-winter-buried.png inspected, shelves gone; lighting/material seams remain. Native owned47326 session34561 review-project-v10 with v8 assets, mapdirtyFALSE1431entities. Actual Scene-only forest-arbor-v10-near/mid/far.png, no UI. Near7144682 modeltris+2048water, LOD492/219/39; mid959302+2048, LOD67/23/0; far4018034+512, LOD271/152/105. Camera coverage changes, so total triangle comparisons are NOT same-visible-set performance. All report lodBlocked0, native near/mid/far LOD selection active. User original app/project untouched. First git commit failed sandbox index.lock with no mutation; repeated authorized escalation succeeded. CI/non-Metal pixels/actual mouse Y NOT RUN (Mac locked).

Next bounded stage smooth shared bark projection weights (current face derivatives make per-triangle texture changes), then reduce fork lighting seam and add generic thin-leaf transmission. Existing main/site/beautiful scene, winter snow/age art, more sand/ground/mountain textures, live erosion/root/fall, PRISMA reference video and universal picking remain unfinished; no push/main merge.


## 2026-11-21 smooth shared bark projection weights

Code305b9e5ea, feature/aiv87_terrain_pbr. Object-position face derivatives formerly gave constant-per-triangle projection weights despite smooth lighting normals. New graph recovers interpolated normal into object space via corresponding world/object derivative frames. Native uniformly scaled/rotated rigid trees share the same graph. Derivative epsilon scaling retained. Bundled4family graphs/material library/catalog rebuilt:92nodes (was73),6samples/2sources unchanged, below128node/8sample budget. No public GPU layout/save/ECS change. Nonuniform transforms/deformed poses remain unverified. Height/ORM/albedo sources unchanged; geometric fork intersection and directional grain on horizontal branches are NOT solved by this stage.

KINGSTON aiverse_graphcheck and shader_graph_tests build PASS bark-smooth-graph-build.log; CTest shader_graph1/1 PASS0.03s. Four bundled graphs compile with engine GLSL/MSL emitter,92nodes6samples2sources. Existing dump-bark-graph emits real expressions; glslangValidator ES300/GL330 eight variants PASS. Actual Metal bark-smooth-summer/winter/low/opposite reports4/4 completeTRUE/error empty/pipelineFailures0; actual summer/winter PNGs inspected. Same emitted geometry as previous stage. First regeneration script incorrectly searched small float constants and failed on pine after one oak file write; abandoned extraction and regenerated ALL graphs from canonical source catalog heightMeters*.35 before testing/commit. No rejected partial graph left.

NativeEditor/Studio code unchanged from prior full build; fullscene installation of new graph NOT RUN yet, review-project-v10 still uses native-bark-projection-v2. Actual rigid macro checked using new shared bark-projection-smooth-v3 on KINGSTON. Non-Metal pixels/mobile performance/CI NOT RUN. Next stage generic opt-in thin-sheet PBR transmission for leaves (shadowDiffusion is NOT SSS), then install verified shared material changes in full native scene. All broad goals still active; no main merge/push/gallery finalization.


## 2026-11-21 opt-in generic thin diffuse PBR transport

Code4f34cfb71, feature/aiv87_terrain_pbr. Appended Material.thinTransmission default0; material-library clamped0..1, modeling input validates0..1, glTF/GLB native extras.aiverseThinTransmission roundtrip validates finite0..1. Non-native viewers may ignore extras. Botanical organic recipes and bundled leaf materials opt into.35. Generic PBR sun diffuse splits reflected/transmitted energy using opposing normal hemispheres, suppresses metal transmission and uses light-facing receiver shadow normal for backlit sheets. Zero retains the legacy sun path; unlit fixed shading unchanged. This is bounded direct diffuse thin-sheet transport, NOT volumetric SSS/refraction/point-light BTDF or spatial translucency/thickness-map support. Ambient/GI remain existing approximation. GPU U2 nine-vec4 ABI unchanged: uv1.w is thin fraction for PBR, old fixedShade.y for Unlit. C++ Material layout changed, rebuild clients. Equality/hash, merge keys, GeometryScene and static packet material fallback include the value; versioned owners must bump revisions on edits. ICB pack shares same helper; experimental ICB remains OFF/unverified.

KINGSTON thin-pbr-host-final-build.log renderbench/exporter/batching/appearance/modeling targets PASS. CTest model_batching_policy/view_appearance_authoring/modeling_authoring3/3PASS0.69s, covers material-state distinction, inheritance and actual GLB load/save. NativeEditor+Studio both built PASS thin-pbr-editors-build.log; command afterward FAILED because plural aiverse_terraweave_model_tests target does not exist. Correct aiverse_terraweave_model_test built in thin-pbr-model-build.log, CTest1/1PASS0.42s. First build FAILED meshSettings reference accidentally applied to branch adapter; fixed to settings.mesh then reran all consumers. No failed command presented as a pass. Six full model GLSL ES300/GL330 Low/Medium/High (normal+alpha mask) compilePASS.

Actual Metal thin-sheet-metal-v1 nine reports completeTRUE/error empty/pipelineFailures0. Fixed light/camera/quad alpha atlas, fractions0/.35/1: front meanRGB14.3211/11.7742/4.09056, back1.06226/6.42552/12.6172, sun-off all1.06226 and BYTE-IDENTICAL pixels (no fake emission). Actual native review-project-v11 Scene-only forest-thin-pbr-v11-near.png inspected, still needs art improvement under canopy; do not call SpeedTree parity. All1431entityIDs/transforms preserved/mapdirtyFALSE,7144682cameraModeltris+2048water,1442drawcalls, LOD492/219/39,729chains0blocked. Owned47326/session90040. New botanical-palette-v10 preserves prior v8 catalog/40binarygeometry files exactly; material-only. Initial v9 exported defaultseed42 and equivalence check FAILED; rejected. Recovered originalseed2718 from bijective mix64, reran v10 with explicitseed. This provenance must be retained. Smooth bark-projection-smooth-v3 graphs installed in all40native rules. Original user apps/project untouched.

No CI/GPU-time/non-Metal pixels/mobile/deformed-sheet acceptance. Maclocked mouse interaction still NOT RUN. No push/main merge/finalgallery. Next bounded work winter snow material and native seasonal appearance, then fork shading/organic transition, terrain sand/mountain art, live erosion-root/fall integration and PRISMA/picking gaps. Full task remains active.


## 2026-11-21 opt-in PBR snow surface coating

Codec0e08432d, feature/aiv87_terrain_pbr. New snow_coating_graph(base,coverage) composes upward-facing procedural snow over material graph outputs, retaining alpha/vertex outputs and using no new texture sources/samples. Coverage0 keeps existing base graph EXACTLY. Twelve bundled winter material IDs inherit original/projected PBR families: bark119nodes6samples2sources atcoverage1, foliage31nodes0extra sources/samples atcoverage.65. Leaf normal/PBR textures/mask remain built-in; snow smooths normal detail and sets rough dielectric surface. Absent source graph AO output no longer overwrites ordinary leaf AO. Existing source graph sampler/node budgets retained below128. Native snowcoat material thinTransmission0; winter deciduous leaf removal still existing TreeCanopy/phenology. Library must be explicitly merged/bound; NO automatic weather/season/runtime integration, physical load, melt or volumetric snow silhouette. Not claiming SpeedTree/winter full art parity. No C++/GPU ABI/ECS/save change.

Engine graph compiler12/12PASS after final AO/coverage adjustment. Representative actual GLSL statements bark+foliage ES300/GL330 four variants compilePASS. Helper linker warned newer15.6library vs15.0default executable target, but executable ran and all variants compiled. Existing native renderbench binary from verified prior stage, no source-host rebuild required. Actual Metal winter-pbr-v1 oak/spruce plus final spruce-final/spruce-bare/oak-low five reports completeTRUE/error empty/pipelineFailures0; low receives coating. Final spruce snow/bare foreground silhouette IoU1.0, alpha cards not solid rectangles. Actual oak macro and final spruce inspected. Early fullwhite foliage prototype too uniform; replaced with65%coating, retained leafAO in final. Extra snowy geometry NOT present. Final images /private/tmp/winter-pbr-oak.png, winter-pbr-spruce-final.png are component proofs against clear background, NOT beauty hero. All outputs/compiler/temp onKINGSTON, repository only source/assets. Native forest review-project-v11 remains SUMMER and unchanged. CI/mobile/non-Metal pixels/deformed seasonal runtime NOT RUN.

Next stage terrain detail: investigate repeated sand grain/ripples and mountain texture/projection before final composition. Native tree material/branch improvements still do not make complete welded forks or slope/rest-soil roots. Full live erosion/fall host, PRISMA reference/optional lens interaction, deformed/water picking, mouseY, hero/GI/Nanite/Metahuman evidence, site publication, push/main merge remain unfinished. Maclocked mouse checks remain externally blocked; continue independent work.


## 2026-11-21 authored river/pebble sand PBR; remove dune ripple source from riverbank

Codeca88a3bfe, feature/aiv87_terrain_pbr. Inspected real source sand_height.png: strong evenly spaced dune ridges, so repeated lighting bands were authored into the source, not proof of new geometry stripes. New built-in image_gen river_sand and pebble_sand source pairs saved under tools/terrain/pbr_sources (original sheets+lossless left albedo/right height halves). Prompt set/provenance in README; authored artistic maps, not scans/physical measurements. Full PBR baker now12materials108maps; new families1K/4m, relief.002/.012m, rough means.87/.82. Bundled18newmaps including16bit height, normals/ORM/AO, metallic0. All old catalog entries compared exactly unchanged, copied only new textures and metadata. Atlas --sand-layer chooses old sand|river_sand|pebble_sand; default old sand preserved. Still four logical channels/126nodes13samples4sources. This does NOT yet blend two sand families per tile in world space; that broader request remains open.

KINGSTON full bake PASS terrain-library-river-sand-v1. Pixel audit both families allmaps1024square; wrapped seams RMS<=max(2,1.6xordinary adjacent diff), normals unit error<.008, ORM metalchannel0. Twelve library catalog/defaultchecker remains. Shadergraph engine compiler detail126nodes13samplesPASS. Two actual standard-PBR Metal fixtures with new albedo/normal/ORM/AO: river-sand-pbr-proof-v1 reports2/2completeTRUE/error empty/pipelineFailures0. Existing native binary from prior source stage, no C++ build required. New terrain-river-detail-v1 shares original48tilegeometry and weights read-only via symlinks; atlas only rewritten. Review-project-v12 preserves1431IDs/poses and clean map; all48terrain rules bound new graphs. Actual Scene-only forest-river-sand-v12-mid.png inspected at identical previous camera: dune ridge bands gone from beach and riverbed, still NEREID flow-aligned optical streaks and synthetic conifer art.959302cameraModeltris+2048water,370drawcalls,LOD67/23/0,86chains0blocked,opaqueCapture/SSRtrue. Owned47326 session56047, original user app/project untouched.

No native/Studio frontend changes, C++/save/ECS/GPU ABI unchanged. CI/GLSL recompile/non-Metal pixels/mobileperformance NOT RUN for texture-only stage. Previous full native build remains verified. Next bounded feature: integrate spatial sand-family variation with normal projection while respecting128node/16sample/four-source budgets; do not merely raise cost caps. Then mountain detail/composition and remaining root/branch art, live erosion/falls, PRISMA reference, universal picking and final scene/site/main. No push/main merge.


## 2026-11-21 continuous two-family shore sand detail within existing limits

Codefda08a571, feature/aiv87_terrain_pbr. Optional --secondary-sand splits the original normalized shore weight with existing continuous alpha world-noise mask, while preserving sum/snow/grass/rock masks and0..20%noise underlay. Five PBR families use3x2 atlas cells/32pxwrap gutters/explicit mips; unused sixthcell zero-initialized. PBR albedo/normal/roughness/AO share weights. Five-mode normal perturbation is projected onto surface tangent plane without independent axis normalization (avoids boosting relief on inclined banks); dominant projection still switches axis, NOT smooth triplanar. Four-layer legacy basis retained. Final128nodes16samples4images fits EXISTING limits; no compiler/RHI source edits or raised caps. Texture samples+23%and atlaslogicalstorage+50% vs4mode, NOT a performance gain. Newly authored graphs only; existing material snapshots unchanged until explicitly reapplied. No C++/ECS/save/GPU ABI change.

KINGSTON terrain-river-blend-v1 built48tilegraphs using original geometry/height/weight symlinks. Actual final native graph engine compile128nodes16samples4sourcesPASS. Eighteen mask-resolution256/1024/4096 x noiseon/off x underlay0/.12/.2 combinations allwithinbudget. First straightforward5layergraph129nodes rejected; simplified only alpha-preserving opaque underlay+normalmath, added real ORM AO to final128. Shaderwrapper firstGLSLFAILED missing SG_TEXTURE_LOD macros (not renderer error); added matching textureLod macros and finalES300/GL330 bothPASS. Actual terrain-five-proof-v1 high/low reports2/2completeTRUE/error empty/pipelineFailures0, including NEREID scene capture. No core rebuild needed.

Final native owned47326/session41077 review-project-v13 restarted after finalAO graph bindings,1431entities/mapdirtyFALSE. Scene-only forest-river-blend-v13-mid.png and river-blend-v13-close.png inspected: mixed gravel/fine sand detail, no repeated dune ridges or square stamps. All48terrain bindings changed; IDs/poses/geometry retained. Mid959302modeltris+2048water,370drawcalls, LOD67/23/0,86chains0blocked, capture/SSRtrue. Same aspreviousmaterial-only stage, NOT FPSclaim. Originaluser app/project untouched. Need still improve foam art/optical streaks, conifer morphology and composition; these are not finalbeautyhero. CI/mobile/non-Metal pixels/largecliff parity NOT RUN.

Next action inspect actual mountain range with camera target map[380,100,450], distance180,pitch15,yaw180. Native ground513x385,origin[-192m,-128m],max234.69m atsource[179,375] (logicalmapoffset256,128). Mountain source/detail are nativeTerra, no customheightgenerator. Need realistic multiple mountain materials and smoothprojection before composing hero. Then remaining fork/root art, live ecology/erosion/falls, PRISMA video and universalpicking; finalscreens/site/remoteTerra/main remain active/unpublished. Maclockedmousevalidation outstanding.


## 2026-11-21 generic world-space height-normal graph node

Codef8b61eb47, feature/aiv87_terrain_pbr. normalFromHeight(Position,Normal,Height) material-fragment-only node emits shared surface-gradient GLSL/MSL. Position and Height.r use same world units; no tangent/UV or extra texture sample. Scale-relative final normalization preserves centimetre macro detail; degenerate screen derivatives retain supplied normal, zero input normal usesup. Source follows existing bark normal formulation / Mikkelsen surface-gradient math, primary paper linked in shader docs. Shading only, no geometry/collision/depth/picking change. Version1 graphs with new op require updated compiler/native hosts; old graphs unaffected.128node/16sample/four-source caps unchanged. Existing graph source hashes invalidate affected shader variants; no new material/GPUuniform layout.

KINGSTON height-normal-native-build.log compiler/tests/graphcheck/renderbench/nativeEditor/Studio allPASS; CTest shader_graph1/1PASS0.40s. Covers arity/catalog/fragment generation and vertex/postfx/particle rejection. Actual Metal height-normal-gpu-v1 seven reports completeTRUE/error empty/pipelineFailures0. Baseline vsflat and degenerate derivative outputs PIXEL-BYTE-IDENTICAL. World height H=.25*X vsmesh-authored analytic normalize(-.25,0,1): meanRGBdelta.000005697 max1 at3m, .000007324 max1 at5cm macro. This is numerical normal/reference validation, not performance measurement. ActualGLSL ES300/GL330 flat/slope/degenerate sixvariantsPASS. No mobile/Vulkan/GLGPU pixels orCI. NativeEditor APIreview process still previous executable until restart; diskEditor/Studio rebuilt. No actualmouse checks becauseMaclocked.

Mountain investigation BEFORE node: actual source masks RGBmean terrain-1-4 B240.7, implicit snow13.85/255, so overwhelmingly ROCK, not snow. Nativeunlit mask/albedo +lit Metal3reports PASS mountain-inspection-v1 confirms blue rock mask and very pale limestone albedo. First fixture failed expectedversion1 because graph filename overwritten byscene filename; fixed separate *_graph/_scene. Primary terrain localnodeorigin0, so camera corrected subtracttileorigin[64,256]. Viewport white mountain was not proof of snow-mask defect. Need darker granite/schist materials and morphology/atmosphere composition. Generated future sourcepairs remain indefault image_gen output, not yet imported/bound: granite exec-b0931ea2-a560-4609-b385-3d35b29ef19a.png; schist pendingtool g4:2095. Do not start next implementation until this handoff committed.

Next bounded stage import two darkrock PBRpairs, pack shared color+height/ORM atlas and compose6layers using thisnode to stay underexistingcaps. Then actualnative mountain/forest/coast checks. All broad tasks/main/site still active; no remote push/mainmerge.


## 2026-11-21 dark granite/schist and six-layer physical-height PBR detail

Codeff13806cf489311e6a52b4da2d9176f40af994d1, feature/aiv87_terrain_pbr. Two new built-in image_gen sourcepairs imported losslessly, originals/prompts/provenance retained. Granite/schist1K/4m, relief.04/.035m, tuned dielectric roughness/AO, NOT scans/measured maps. Library now14families126maps, original12catalog entries compared unchanged. Six-layer opt-in --height-normals uses grass/two sands/two rocks/snow; native RGB masks/summed sand/rock weights preserved, alpha noise splits families. Shared centered physical height packed into albedo alpha, ordinary ORM separate, normalFromHeight reconstructs shading once. Output color explicitly alpha1. Height mode sets normalScale0 so lowres old baked normal is not applied first. Default four/five normal-atlas modes remain. No C++/geometry/soil/ECS/save changes, new generated graphs require preceding updated native compiler. Actual117nodes13samples3sourceimages under EXISTING caps, not performance proof. Two3x2 atlases; no independent per-tree textures.

First schist bake FAILED seam audit: height seam/adj1.79, AO3.656vs1.767byte differences. Corrected coherent quarter-period second overlap on BOTH albedo/height before derivatives/AO; dark-rock-v2 all18maps1024/unitnormerror<.008/seam<=max(2,1.6xordinaryadj)/metal0 PASS. First failed audit command sequence continued into pack preparation and hit missing granite catalog key; no demo switch occurred. Repeated audit entirely before copying new maps/catalog, then pack buildPASS. Rejected dark-rock-v1 not final.

KINGSTON terrain-height-six-v1/48tilegraphs built from existing geometry/height/masks. Enginecompile117nodes13samples3sourcesPASS; final actualGLSL ES300/GL330 PASS. Metal terrain-height-six-proof-v1 shore/mountain x High/Low4reports completeTRUE/error empty/pipelineFailures0. These4standalone renders preceded the normalScale0 authoring override, so final factor separately checked in native scene after restart, not falsely calling identical config. Shared renderer/compiler/nativeEditor/Studio already built and tested f8b61eb47; no C++ rebuild this authoring stage.

Final owned47326/session1628 review-project-v15 includes final48overrides+normalScale0, mapclean1431entities/allposes retained. Source nativev14 inherited default lighting fogDensity.0025, which heavily washed out180m mountains. Added scene-only game lighting.fogDensity.00035 through existing owner in v15 (no global engine default change). PRISMA off/on diagnostic showed same distance haze; restoredHigh before finalrestart. Final terra-mountain-final-v15.png and forest-terra-six-v15-mid.png actualScene inspected; dark rock/snow separation improved but cone/gully morphology and framing still below reference. Same mid959302modeltris+2048water,370drawcalls, LOD67/23/0,86chains0blocked, capture/SSRtrue. Fog/sky are existing lighting/PRISMA owners, not a newshader. Earlier no-postfx shot includes transient asset-watcher TOASTS and must NOT be used as final scene-only screenshot. Original user app/project untouched. No CI/mobile/Vulkan/GLGPU pixels/perf/mainmerge/push/gallery.

Next bounded stage camera/capture: view_screenshot pitch currentlyclamps10..89, SceneWindow orbit.05..1.5radians. This prevents eye-level/upward cinematic views (and sky) even with newYtarget. Need shared scene-camera inspection support allowing upward look while preserving validation/nonmutation, then actual clean high-resolution scene capture rather than UI/toast overlays. Do not label current903x605component proofs as finalhero. Then improve native alpine/conifer/fork/root art where actualnewcamera reveals it, finish live erosion/falls/season host, PRISMA reference and deformed/water picking, finalsite and Terra/main. Maclocked mouse parity still unverified, continue authorized independent work.


## 2026-11-21 clean high-resolution native Scene capture and upward orbit

Coded24752fcf, feature/aiv87_terrain_pbr. view_screenshot windowscene now defaults clean:true, captures actual final RGBA8 Scene texture through Device.captureTexture (not OS/editorframe crop), converts to RGBPNG without encoder upscaling. Optional paired integerwidth/height256..4096, <=8388608pixels; outputdimensions and actualinternalrender_width/height returned separately. Defaultcurrent viewport is bounded. clean:false preservespreviousviewport/UIcrop. Scene is rendered ONCE at exportsize forone requestedframe, then normalUIresolution resumes; no extra simulation/lens/water tick. Existing scoped Scene LOD/material/culling policies retained. Input skipped on that exportframe to avoid aspectratio hit-test mismatch; normalframe refreshes pickcache beforeinput. DerivedRTS/FPS selected/hover flags and ghosts/gizmos suppressed forcleanframe, canonicalselection/doc unchanged. NativeFPS/RTS orbit andagentpitch allow -89..89degrees; planet cameraunchanged. Source capture lifetime token andViewportGpuRetirement owninflightGPU/shutdown. No directRHI/batching/defaultexperimental flagschanged.

Onepending capture only. Invalidwindow/clean types,size pair/fraction/8MPbounds/nonfinite-range camera rejectBEFORE cameraassignment. HiddenScene/unsupportedtextureRHI returnsERROR, neverfull-editorfallback. All/Gameplaycapturesemantics retained. Wireadditivefields; defaultsceneappearance intentionallyclean, oldcropavailableexplicitly. No save/schemachange. Editor.h rebuildrequired. New docs/native-scene-capture.md documentslimits andinternal-resolutionmetadata.

KINGSTON clean-scene-capture-build.log nativeEditor+Studio+scene picking/gizmo tests buildPASS, CTest2/2PASS0.55s. ActualownedEditor47326/session85671 review-project-v15: forest-clean-capture-first-v15.png2560x1440, INTERNALrender2560x1440, upwardpitch-14.5 accepted andinspected via900x506analysispreview. Realforest/river/sky/mountains inoneframe, stillnotfinalheroart. Firstattemptdisplayingfull2Kimage tool discardedimage dueoutputlimit; nativePNGvalid, thumbnailonlyforanalysis, finaloriginalretained. Source/sceneunchanged,1431entitiesdirtyFALSE.

SelectionQA actualeditor_select schema is docsarray (EditorWorld overridesolder kind/index registration). Firstkind/entity requestselected0, NOT countingasproof. Repeateddocs[87]selected1. Before/clean/after same903x605: coolwhitecontourpixels14929/0/14912, gizmoremovedonlyfromcleanPNG; normalafterviewportretainsselection. Initial>245allchannelwhitecounter0wasincorrectthreshold (actualoutlinecoolwhite), correctedmetricandvisuallyinspectedbefore/clean. Centreview_pick tree0011index59 occludesselectedtree, remainsSAME after4invalidrequests(widthmissing,heightfraction,8MPoverflow,pitch1e300) thatattemptedmovingcamerato450/450. Concurrent twoTCPrequests: first2560x1440complete, secondbusyerror, centrepickunchanged; nosecondcameramutation. ClosingScene->cleanrequestexplicitunavailableerror(noUIcapture), reopening->1280x720actualrender/outputPASS. Wrongcleanstring/window rejected. AllmapchecksdirtyFALSE1431entities. Originaluser/baselineapps untouched.

FreshCUA checkafterdocumentationrewrite: MacSTILLLOCKED, automaticunlockfailed; noUIinputattempt. CompletionSummarysent. Mouseorbit/Ygizmodrag NOTRUN. FPScleanruntime NOTRUN (compiledonly), GL/Vulkantexturecapturehardware NOTRUN. StudiohasseparateStudioMetalScene implementation; sharedbuildPASS doesNOTprove itsHTML5captureparity, whichremainsseparatework. NoCI/performanceclaim/mainmerge/push/websitepublication.

Nextfix actualYgizmoclarity: beforePNGstillvisuallyshowsX/Z whileYmayhideinsideopaque trunk (world-depth gizmo). Need inspectcurrentdrawEditOverlays3D/2DscreenGizmo, ensurevisibleXYZwithoutalteringvisualmeshpicking; testactualpointerwhenMacunlocked. Then improvecrown/forest/mountaincomposition, actualGIandSTRATA/VIVIDcapabilityintegration (avoidclaimingUnrealfeatures). Readonlyfetchmain973b32970/HTML5545e3daf3/Terrareturnednoadvances; no mergesduringdirtycapturestage. RemoteVIVIDbranch2b7c4f967 exists,butNOTincurrentmain/worktree. Latest7BAhandoff explicitlysource-only/NOTNATIVE/GPUtested; donotblindlymergeorclaimMetaHuman. ItalsohasfutureGPUsourcepickinggates. RemainingliveTerraerosion/root/fall/seasonhost, PRISMAreference/picking/artandfinalsite/goalsremainactive.

## 2026-11-21 branch collar shading continuity

Code e4ae117a3, feature/aiv87_terrain_pbr, engine/world/TerraWeaveArborMesh.h and tests/terraweave_arbor_test.cpp. Organic child collars blend face-area tube normals toward the same curved parent radial/cone normal near the intersection, with spatial falloff and maximum 50% contribution. Parent curve context retained for independent branch Full/Reduced/Minimal generation. Only normal/tangent shading changes; integer skeleton, vertices, UVs, indices, counts, save/public APIs and opt-in defaults unchanged. This is intersecting geometry with improved shading, NOT a welded manifold union. Deformed/nonuniform-scale pixel parity NOT RUN.

Initial unlimited blend FAILED outward-triangle regression; rejected. Bounded .5 blend passes. First weak >=3 alignment check also matched old normals; corrected physical cone reference and >=6 threshold (standalone old/new fixture measured 3 versus 6 aligned collar vertices). Full isolated branch normal equality and all exterior triangle orientation regressions PASS. KINGSTON branch-normal-host-build.log nativeEditor + Studio + exporter build PASS; final-test-build then CTest terraweave_arbor/model 2/2 PASS0.26s. Compiler/cache/build artifacts only KINGSTON. Actual Metal branch-normal-final summer/winter reports complete TRUE, no error, pipelineFailures0; final winter inspected /private/tmp/branch-normal-final-winter-preview.png (analysis thumbnail), full original KINGSTON branch-normal-final-winter.ppm. Earlier unrestricted v11/winter-v3 captures rejected as final.

Final packs KINGSTON botanical-palette-v12 / botanical-winter-curves-v4, seed2718, botanical4variants. Audit all40 final summer prototypes: exact catalog (including placements/seeds/triangles), POSITION/TEXCOORD_0/index accessor bytes unchanged versus v10; 104 normal accessors changed. review-project-v16 prepared with 40 route replacements, map byte-equivalent JSON/1431 objects; NOT launched yet. No FPS/performance claim. No CI/mobile/Vulkan pixels, push/mainmerge/gallery.

Correction to previous next-step hypothesis: Y gizmo IS present in selected-before-v2 image (122 bright-green pixels around450,379); SceneWindow screen overlay projects XYZ, authored Transform includes editable Y. It can blend visually into foliage. Do NOT change depth rules based on previous unverified occlusion speculation. Actual pointer drag remains NOT RUN because Mac locked. Scene entity get stableId field currently tests lights only, metadata issue distinct from stable authored @IDs.

Next: launch v16 on owned47326, inspect full forest/crowns and improve alpine morphology/sky/shore art; finish actual GI/STRATA/VIVID capability-gated showcase, live erosion/root/fall/season host, PRISMA video parity, deformed/water/FPS picking and website/main publication. Broad user 'do everything' still active, no final completion claim.

## 2026-11-21 opt-in alpine massifs, shared-height audit and expanded scene

Code d2f17025e, feature/aiv87_terrain_pbr. AlpinePeak appends AlpineMorphology (LegacyCone default, RidgedMassif opt-in), Q16 world-space domain warp/ridged octaves and zero-slope support. Existing legacy branch/six-field aggregate/default source retained. Native landscape accepts per-peak morphology legacy|ridged_massif; no archive/ECS schema or default terrain changes. This is procedural cosmetic/source generation, NOT physical hydraulic erosion. Added actual recipe examples/terraweave/alpine-valley.json and glTF export audit tests/terrain/check_landscape_export.py; docs/TERRAWEAVE API. No shared renderer/batching modifications.

KINGSTON massif-build/ridge-build logs exporter+landscape test build PASS; final CTest1/1 PASS0.44s. Export v2 48tiles:10543 shared-edge comparisons,1179648 integer planting-height checks PASS. First v3 audit called before export finished FAILED missingcatalog; second completed export audit FAILED overly strict .002cm tolerance at y258.539978 versus25854cm (-.002197cm). No terrain seam found: cm->Q16->float32 quantization. Audit now uses derived100/65536+abs(y)*100*2^-24+1e-6 cm bound, not arbitrary relaxed metres. Final v3 128tiles:29823 shared-edge height/normal comparisons +3145728 integer-ground checks PASS. Source commit after this PASS. CI/mobile/otherGPU/performance NOT RUN.

Initial v1 was too smooth; v2 shifted octaves to half-radius and .6 amplitude rolloff, but actual native v18 shows excessively narrow spires, rejected as final beauty. Lighting also mattered: original positive-Z sun shaded every visible mountain face. Existing lighting_set time.5/dayLength0/sunDir[-.6,.8,-.65] illuminated fronts in diagnostic; this did NOT add GI. Moved range from 200m peaks immediately behind trees to broader far peaks650/760m, expanded native recipe128tiles (512x1024reviewmap), v3 assets retain same current ridged function; spiky morphology still below reference and needs amplitude/frequency slope tuning. No final-art claim.

All48 v1/v2 and128v3 PBR bakes+six-layer shared height-normal graphs PASS (1K/4m sources,117nodes13samples3sources, finalnormalScale0). Native grass exports v1/v2/v3 all194patches, same seed2718. Owned Editor47326 now session66065, review-project-v19,1511entities/mapdirtyFALSE. Original1431 IDs/XZ retained;80far terrain entities added,438 tree/rock Ys adjusted by exact new-minus-old shared grid height, preserving planting depth (largest45.596m because old nearby mountains removed). Grass rebuilt from new grid/NEREID sand masks, not moved by guessed offsets. Canonical StaticModel paths now agree with rules for every entity. Earlier v16 updated rules to v12 but kept old v10 StaticModel path; v17+ corrects both, do NOT count v16 as canonical asset-route proof. New trees botanical-palette-v12 incl bounded collar normals. Sky via existing physicalSky coverage.38/base1000/thickness600/scale1100; game scene front-light settings and fog.00035 only. No original user project touched.

Actual native clean internal/output1280x720 forest-massif-v19.png and forest-valley-cinematic-v19.png inspected (analysis thumbnails only). Camera latter target[258,80,500],distance400,pitch-11,yaw180 gives clear river/forest foreground, distant spires still artificial. Earlier attempt thumbnail before async capture command finished FAILED filemissing, then awaited session72060 and captured successfully. Existing hero widev16 stats8,467,133modeltris +2048water; LOD[516,252,104,0],839chains,0blocked, so LOD operates but workload high. Read-only strata_capabilities reports CPU builderavailable, changesRendererfalse/gpuVerifiedfalse; renderLighting indirectoff/directlegacy. Do NOT label native scene as GI/STRATA/VIVID verified. Fresh ancestry checks origin/main973b32970, HTML5545e3daf3 and Terraf5d9dbcad allancestorHEAD true. Gh-pages separate branch b7d934452, current site still old Alpine screenshot; no site edits/push/mainmerge.

Next bounded stage tune massif octave gradient rolloff (half-radius/.6 increases high-frequency slopes) and recheck source/actual silhouette; add seam-preserving authored terrain LOD for immutable exports, since dynamic collision patch intentionally stays LOD0 and exporter currently writes autoLodfalse without terrain alternatives. Preserve full-resolution border and original collision/near geometry, use shared Renderer3D LOD with measured error/actual pixel checks. Then source/runtime grass/tree art and live soil/season/fall/picking/PRISMA reference/GI/VIVID/site. All broad goals remain active; no completion claim.

## 2026-11-21 massif gradient rolloff correction

Code89dec054d, feature/aiv87_terrain_pbr. RidgedMassif only: dominant period now full minimum peak radius, higher-octave height amplitude .35 per doubled frequency (was .6), carrier16k/65536 instead8k. Thus nominal high-frequency slope contributions decay rather than grow. LegacyCone/defaults/save formats untouched; new opt-in massif seeds intentionally change before publication. Fixed peak seed2718,height320m,radius260x340m/shear.1 measured at72541locations with1m central differences: previous maximum18.151m/m,P95 3.766,5743samples>3; new maximum3.650,P95 1.592,267samples>3. Standalone old/new clang++-O2 probes compile Fixed.cpp, executables on KINGSTON massif-gradient-baseline. New native regression scans500cm grid, asserts this fixture remains rough but maxslope<4; no universal slope limit imposed on deliberately extreme authored profiles.

KINGSTON massif-gradient-rolloff-build.log native landscape exporter+test buildPASS; CTest1/1 PASS0.38s. Actual new-profile GPU pixels still NOT RUN: alpine-massif-v4 128tile export running session44158 at checkpoint, recipe same far-rangev3. Do NOT label v19 native scene fixed: it still loads rejected v3 old-gradient spires. Need awaitexport before rebuilding that exporter target, audit/bakev4 and nativev20 with same shared-height replanting. Source stage is verified numerically, visual acceptance remains open. No CI/mobile/perf/remote/main/site. Next independent source stage seam-preserving authored static terrain LOD, while waiting only on asset generation; keep current full-resolution collision and edges intact.

## 2026-11-21 immutable terrain authored LOD with complete shared borders

Codec3d5887aa, feature/aiv87_terrain_pbr. New engine/render/TerraWeaveTerrainLod.h appendTerrainPatchLods runs only on CPU before upload. Validates full original grid triangulation/heights/shared normals/UV/colors, finite/ranged coordinates, no skins/clips/morphs/baked-lightmap UVs/oldLODs/GPUbuffers. Invalid source rejection preserves values. Existing LOD0 attributes/indices exact, physics TerrainPatch unchanged. Strides2/4/8 append only used vertices, retain all512 outer edge segments for129samples, no skirts; counts32768/8952/2680/1080. Measures maximum vertical error at all fine vertices AND coarse edge crossings with fine X/Z/X-Z diagonals; target1.5px through existing conservative projected-size metric, caps512/256/128. No shared Renderer3D/cache/RHI/defaultexperimental changes. Calls markGeometryChanged before finalization; renderer retains camera/shadow/crossfade ownership. Native landscape immutable exporter now emits existing rtsLods+independentShadowLod and additive catalog.lods. Mutable live patch constructor remains unchanged/fullcollision. Source extra vertices6691 per tile plus indices, NOT memory reduction. Custom tangent normal-map/deformation/othercamera parity still needs checks; current default height-normal graphs use normalScale0.

KINGSTON terrain-authored-lod-build/final-build/guard-build exporter+modeltest PASS; final CTest terraweave_model/landscape2/2 PASS0.44s. Tests exact source prefix, positive winding+fulltile area, every internal edge paired, all512 unit boundary segments identical atalltiers, known30m spike error15/22.5/26.25m andadaptivepixel caps, existingLOD/mismatchedheight/lightmapUV rejection withoutmutation. Full128 native alpine-massif-lod-v1 export PASS. Serialized root index/POSITION/NORMAL/TEXCOORD_0/TANGENT bytes exact versus alpine-massif-v4 for every128tile;4meshes each. Default false-lightmap positive outputs pre-finalguard unchanged; guard only rejects unsupported sources.

Actual Metal terrain-lod-metal-v1 (4cases) completeTRUE/noerror/pipelineFailures0: root32768->middle2680 bothflat andmountain. First mountain camera was almost edge-on, weak silhouetteROI; not calling complete orbit parity. v2 found3760=2680+1080 near a crossfade band, and nearcamera32768 because conservative projectederror larger than naive projecteddiameter estimate. Read ModelLodTransition: STATELESS SPATIAL, not time; more warmup cannot settle overlap. Reframed v3 outside bands:8cases allcomplete/pipelineFailures0, flatnear8952/far1080; worst-error mountainmid2680/far1080, eachoff32768. Submitted-triangle reductions72.7/91.8/96.7%, NOT FPS. Silhouette difference outside othermask dilated2px zero on4pairs; objectROI13398/1557/414/77pixels, low-coverage farcases limited. RGBmean differences.005720/.006318/.003310/.001281 over1280x900. PPM originals KINGSTON, enlarged analysis crops only /private/tmp/terrain-lod-worst-v3-analysis.png; do not publish diagnostic cropped/magnified patch asbeauty. Private fixture normalTexture.scale0 matches native factor; freeze-time0, no movingwater, same lighting. NonMetalGPU/lowmaterial/cameraorbit/fullforestA/B/A NOT RUN.

Source docs and existing Terra skill updated narrowly (systemskill-creator read). Skill catalog236checkPASS; selectedstrict Terra audit0errors0warnings. Systemquick_validate NOT RUN successfully: yaml module missing in system andexistingvenv; no globaldependency installed. No newskillregistration required.

Owned Editor47326 session22713 review-project-v21,1511entities/mapdirtyFALSE. 128 terrain canonical/rule assets routed toLODpack, all IDs/poses/materialgraphs unchangedv20. PBR masks+graphs andnormalScale0 reusev4; model image symlinks read-only tov4 textures (do NOT bake intoLODpack and overwrite targets). Existing Editor binary reads authored glTF via unchanged renderer; no direct app dependency on newCPUheader, source tool/test rebuild covers it. Actual clean default903x605 forest-terrain-lod-v21.png inspected. Close camera[258,80,500],distance400,pitch-11,yaw180:8,743,488modeltris+2048water,2102drawcalls,LOD[504,258,119,0],881chains0blocked0shadowlimited. Farcamera[256,80,500],distance1200,pitch25,yaw180:1,922,557modeltris+128water,1301drawcalls,LOD[0,19,1150,10],1179chains0blocked; confirms fourthterrain tier andNEREIDfar tier inactualEditor. Differentcameras, NOT a noLODvsLOD speedup. Currentcamera FAR. CPU visual picking may still use source geometry, separate acceptance backlog.

Gradient89dec054d actualnativev20 pixels nowchecked: no prior thinspires, but a smooth whiteegg peak remains. v3/v4 recipe protects an infinite X valley strip evenbehindriver; since farpeaks start beyond310m, background valley protection canbe disabled instead of adding another solver. Next art stage setvalleyHalfWidth0, add overlapping distant peaks andextend native forest scatter tofoothills while preserving oldentity IDs/placementseeds, useexistingTerra/NEREID. No fakeGI/STRATA/VIVID claims (stilloff/unsupportedinthisscene), no finalbeauty/site/main acceptance. Need actualliveerosion/root/fall/age/season host, PRISMAreference/deformedpicking/UIunlock andallotherbroadgoals. No CI/remote push/mainmerge thisstage. Beforepublication check BOTH originHTML5 andlocal feature/studio-html5 ancestry; original rootdirtyUI changes untouched.


## 2026-11-21 local HTML5 integration and independent PBR controls

Merge code 23d05c50f, branch feature/aiv87_terrain_pbr; parents 8d7d744a6 and local feature/studio-html5 68c43489f. Remote HTML5 was already integrated, but the original local checkout had 37 additional commits. Preserved both incoming SSS/client-module/Substance work and Terra/Thin/DYNAMIC_GI changes across eight textual conflicts. Original dirty user checkout and projects untouched. Model material layout changed: rebuild C++ consumers; U2 remains nine vec4 /144 bytes. PBR uv1.z=sssStrength, uv1.w=sssScale, light.rgb=sssColor, light.w=thinTransmission. Unlit keeps fixedShade/fixedLight packing. GLSL/MSL and material equality/hash/merge include both controls. Versioned material owners must advance revisions for either control. No experimental ICB/default flags changed; ICB GPU parity NOT RUN. Incoming SSS is sun diffuse approximation, not volumetric transport.

Native MaterialOps/Studio validation/preview and existing Studio schema expose thinTransmission [0,1], using existing UI controls and allowlist. No new CSS/window/browser file writes. Added tests/render/verify_sss_transmission.py with reproducible capture mode and optional HDR fixture controls. Default fixture unchanged. No new save migration; additive material fields/default-zero compatibility.

All build/cache/temp/compiler outputs KINGSTON/AIVERSE-builds/terraweave-unified. Initial native build completed Editor/Studio/renderbench, then FAILED absent HDR target because local AIVERSE_BUILD_HDR_TESTS was OFF. Enabled existing local test option, rebuilt HDR Metal/GL, native Studio/material/batching/appearance/session tests, aiversebuild and fresh Terra model/exporter tools PASS. CTest five material/batching/appearance/session tests 5/5 PASS 1.02s; fresh terraweave_model 1/1 PASS .45s. HTML5 typecheck and test:web PASS. First external source-mirror Vite build FAILED relative branding import; corrected invocation from actual source with external outDir, no app import modification. Production build PASS (953ms); copied native Studio index references index-CRpxJ1sF.js containing Thin transmission. Python UI contracts 41 tests OK, 1 skipped; Substance tests 6/6 PASS .014s. Logs html5-local-merge-*.log on KINGSTON.

Public capture command: PYTHONPYCACHEPREFIX=KINGSTON/python-cache python3 tests/render/verify_sss_transmission.py KINGSTON/material-interop-metal-gl-v2 --build-dir KINGSTON/native. All16 actual GPU captures PASS; inactive SSS scale leaves Thin byte-identical, all PBR controls leave unlit byte-identical, positive controls change >100 pixels. Metal/GL mean RGB differences .2624-.2821. Earlier v1 independently passed. Thin nine-case High/Low regression 18/18 complete/noerror/pipelineFailures0; all nine High PPMs byte-identical to premerge, sun-zero thin0/1 identical (no emission). These are fixture pixels, not full scene/mobile/performance acceptance.

Module selection check on review-project-v21 FAILED invalid inherited game id com.aiverse.alpine-valley, not module logic. Valid templates/showcase --target macos --check-engine-modules PASS legacy defaults. No optimized wasm build/size claim. Fix only next owned review project id to com.aiverse.alpine_valley. Fresh Mac CUA check STILL LOCKED, no input attempted: native HTML hover/focus/click and full parity NOT RUN. Live owned Editor47326 still previous binary at checkpoint; on-disk editor rebuilt, restart only that owned process before postmerge source claims. No CI run, remote publication, main merge or gallery acceptance.

Next: restart owned editor on new binary; improve mountain recipe (disable infinite valley protection behind forest), overlapping far silhouettes and native forest extension with preserved entities. Finish live Terra soil/root/fall/age/winter host, PRISMA video parity, deformed/water/FPS picking, actual GI/STRATA/VIVID capability checks, final scene-only screenshots and GitHub gallery. User do-everything objective remains active; no broad completion claim.

## 2026-11-21 overlapping alpine composition and native foothill extension

Code cca9bf791, feature/aiv87_terrain_pbr. Recipe only: five overlapping RidgedMassif profiles, valleyHalfWidthCm0 (no infinite valley cut through distant peaks), vegetation bounds maxZ250m. No new generator/render/physics API. Main128tile native export alpine-massif-v5 used samefinal geometry but initial190mvegetation bound; cheap single-tile massif-v5-foothill-scatter native export evaluated same source at270m then installed only half-open190..250mstrip. Botanical-foothills-v1 native exporter uses existing treePrototypeVariant policy, seed2718,40 prototypes EXACT catalog equality v12. Initial audit comparing old placement yCm FAILED because original v12 placements precede previous landscape re-grounding; corrected audit preserves XZ/yaw/scale/species/prototypeSeed, and current shared ground owns Y. All967 original coordinate/palette entries match. No custom scatter/height solver. 292 newstrip trees, total1803entities, original1511IDs/XZ retained, 0 existingYchanges. First260 sharedgroundrows exactv4, existing grass untouched. NewprojectvalidIDcom.aiverse.alpine_valley (does not modify user's project).

First native export command FAILED wrong executable path native/tools/terrain; corrected actual native/aiverse_terraweave_landscape PASS128tiles. check_landscape_export.py PASS29823boundary comparisons/3145728 integer ground checks. Native PBR bake1024/detail all128 PASS, six-layer1K4m height-normal graph builder PASS. Newreview-project-v22 installs canonical/rule routes consistently, everyterrain4LODs; unique1803IDs. All generated assets/logs/build/cache KINGSTON. Source recipe is portable; generated scene remains external. No C++change/tests needed for recipe-onlystage.

Postmerge Editor owned47326 restarted safely after exactPID/commandguard; original47324/47325untouched. First v21 cleanrequest timedout50s while sample33712 showed Metal pipeline compilation in setPipeline; later same1280x720request PASS and inspected. Actualshaderfullscene afterSSS merge verified, not onlyfixtures. ZeroLODdiagnostics initially meant disabledinspector, notLODfailure; v22 launched AIVERSE_LOD_DIAGNOSTICS1. Current ownedPID34786/session11122, mapclean1803entities, clean forest-massif-v22.png internal/output1280x720 inspected. Cinematiccamera[258,80,500],distance400,pitch-11,yaw180:11711152modeltris+2048water,2616drawcalls, LOD[598,454,121,0],1173chains0blocked0shadowlimited. Addedforest increasesworkload; noFPSclaim/no noLODcomparison.

Newsilhouette removes single isolated egg/valleycut, but broadwhite snow still hides rockstrata and looks artificial. NOT finalart. Next boundedstage make offline snowline recipe-configurable (legacy default retained), increase alpine snow altitude, break monotony with slope/seed variation through existing bake, inspectactualnativepixels. Then treeLODcalibration/photorealart, actualliveerosion/root/fall/age/seasonhost, PRISMAreference/deformedpicking/GI/STRATA/VIVID/site/mainremainopen. MacstilllockedUIpointerNOTRUN. RemoteTerrafastforward f5d9dbcad->d04ca32a0 already succeeded; latestrecipe/handoff notpushedatcheckpoint. No CI/main/gallerypublication.

## 2026-11-21 recipe-controlled cosmetic snowline

Code c71258b66, feature/aiv87_terrain_pbr. tools/terrain/bake_landscape_pbr.py reads optional exportedrecipe.surfaceSnow: low/highMetres, min/fullNormalY, variationMetres. Strict finite numeric/order/range/unknownfield validation beforebaking. Defaults preserve exact previous100..155m,.52..88normalY,8mnoise. Existingworldcosmeticnoise only; noheight/planting/collision/climate/accumulationauthoritychange. SamefallbackPBR/detailweights updated, not onlysurfacegraph. Source example now220..320m,.6..9normalY,18mvariation. Documentation records limits andalltiles-samerecipe requirement. NoC++/savebreakingchange.

Actuallyexecuted tests/terrain/test_snow_cover.py with existingPIL/numpyvenv:3/3PASS.034s, includes exactlegacyarrays, exposedlowrock/noverticalcliffsnow/topwhite/partialtransition andinvalidNaN/Inf/type/order/unknownreject. Fourdisjointparts of1024detailbake alpine-massif-v6 allPASS32tileseach, outputsKINGSTON; geometry BINssymlinkreadonlyv5, glTF/copiedsharedtexturesindependent. Height/catalogunchanged. Six-layergraphbuilder128tilesPASS. Majoritysnowmaskpixels8602688->118671 of134217728 (notphysicalarea);eightz0foregroundtiles byteidentical. Newreview-project-v23 preservesall1803IDs/poses, only128terrainpaths/materialsroutedv6. API mapclean1803entities.

OwnedEditor47326/session27812 v23 actualclean forest-snowline-v23.png and mountains-snowline-v23.png internal/output1280x720 inspected. Forestframe mountainROImeanRGBdifference4.46 vs v22, changesrealbutnotdramatic. Closemountaincamera[256,180,760],distance360,pitch8,yaw180:1362416modeltris+128water,193drawcalls,LOD[38,9,3,0],50chains0blocked0shadowlimited. CameraCURRENTmountainclose. Itreveals underlyingpyramidal/foldedmassifgeometryandfeaturelessmacroappearance, NOTfinalrealisticmountains; finiteterrainblackhorizon makescloseframe diagnostic, notpublishhero. Nextstage inspectTerraWeaveAlpine.h existing RidgedMassif formulation (do not invent scene-script generator), improvecragged/multiscalemorphology withboundedslopes+deterministicseams andactualnativepixels, then treeLOD/artandallremaininggoals.

Modulechecker firstFAILED guessednative/tools/aiversebuild/aiversebuildpath; correctednative/aiversebuild review-project-v22 --targetmacos --check-engine-modules PASSlegacydefaults. No wasm/sizeclaim. Build/Pythoncache onKINGSTON, noadditionalnativebuildneededPython-onlystage. NoCI/nonMetalpixels/performanceclaim/main/site. MaclockedpointerparityremainsNOTRUN. LatestTerraremoted04ca32a0; thesecomposition/snowstagesnotpushedyet. Userdoeverythingobjectiveactive, liveerosion/root/fall/age/season/PRISMAreference/deformedpicking/GI/STRATA/VIVID/finalgallery/mainallstillpending.

## 2026-11-21 optional cragged alpine profile, isolated native proof

Code36355fd4c, feature/aiv87_terrain_pbr. AlpineMorphology appends CraggedMassif=2 andnativeexporter accepts cragged_massif. ExistingRidgedMassif branchretainedwithconditionalconstants; olddefault/structaggregate/saveformatsunchanged. Third-radiusbaseperiod, .42octaveheightrolloff, smaller1/8perioddomainwarp, high52000/65536smoothcarrierkeepsreliefbounded. AllnativeQ16/sourceEarthworks, no scene-script heightgenerator/physics erosionclaim. PublicTerra/nativeauthoringdocsupdated. SourceexampleCURRENTstillridged_massif; fullscenehasNOTyetadoptedcragged profile.

Initialprototypecarrier30000 produced maxslope9.793m/m,P953.147,4135of72541samples>3; rejected beforeassetproof. Revisedcarrier52000 yields max4.024,P951.990,102samples>3 (same320m/260x340mseed2718probe); existingRidgedmax3.650,P951.592,267>3unchanged. Privateexactpre-changeheadercomparison compiledonKINGSTON check-alpine-legacy PASS241402 LegacyCone/RidgedMassif heights acrosssameworldgrids, noapproxtolerance. Nativeexporter+landscape_testbuildPASS cragged-profile-build.log; CTestterraweave_landscape1/1PASS.20s, new deterministicseedvariation/heightbounds/boundarytangent/fixturegradienttests. Thesearefixturebounds, notuniversalsteep-profileguarantees.

Exported16tiles each crag-proof-ridged_massif/crag-proof-cragged_massif, sameflatbase/seed2718/100mradius/120mnominalheight/shear100. Baked256fallbackPBRactualnativeground, NOTfinal1K4mgraph. renderbenchmodelsarrayusesoneprimarytileand15translatednativeassets, same1280x720camera/sun, LODOFF. Both actualMetalcompleteTRUE/noerror/pipelineFailures0, camera524288tris. /private/tmp/cragged-massif-compare.png is labeled resizedANALYSISpair; originalsKINGSTONcapture.ppm. Craggedsilhouettehasmoreheight/detail, nooldfoldedknife, butprototypealoneisstillamassifandnotphotorealhero. Do notpublishcomparisonasfinalscene. NonMetalGPU/perf/CI NOTRUN. Allcompiler/cache/tempoutputsKINGSTON.

Nextboundedstage selectnewprofileinfullnative128tile recipe, export/audit/PBRalltiles, preserveoriginal1803IDs/poseswheregroundsame, replantbysharedheightwhererequired; nativecleanactualhero. Currentowned47326review-project-v23/session27812stilloldridgedgeometryandnew220..320msnow; currentcameraCLOSEmountain[256,180,760],distance360,pitch8,yaw180. Nooriginalusereditor/projectmodified. NeedcontinueforestLODcalibration/art/liveTerraerosion/root/fall/age/seasonhost/PRISMAreference/deformedpicking/GI/STRATA/VIVID/site/main. No broadcompletionclaim/push/mainmerge.

## 2026-11-21 full forest adopts cragged massifs

Code8babbdb36, feature/aiv87_terrain_pbr. Fivepeaks in portableexamples/terraweave/alpine-valley.json selectcragged_massif; otherrecipefieldsunchanged. Nativealpine-massif-v7 export128tilesPASS; sharedgroundauditPASS29823boundaries/3145728checks. Fourpartsof1024/detailPBRbake32tileseachallPASS, six-layerheightnormalgraphbuilder128PASS. Newreview-project-v24,1803entitiesallIDs/posespreserved, all128canonical/ruleterrainroutesv7; 1480existingtree/rockroot neighbourhoods havefourintegergroundcorners EXACTsamev6, noYreplantnecessary. Grassforegoundunchanged. Newrecipevegetationscatterbounds250m consistentwithinstalled292foothilltreesplus967original. Allgenerated/build/cache/logassetsKINGSTON, no userprojectedits.

OwnedEditor47326/session84300 openedv24using/private/tmp/restart-terra-editor.py: assertatmostoneportPID, exactKINGSTONbinaryandlisten47326beforeSIGTERM; originaleditorsuntouched. Firstcleanrequesttimedout50s duringnewMetalvariantscompilation, laterrequestcomplete1280x720internal/output. /private/tmp/forest-cragged-v24.png inspectedandsharedINCHATasWORKINPROGRESSonly; improvesbackgroundridgeforms, NOTfinalGI/STRATA/VIVID/sitehero. Camera[258,80,500],distance400,pitch-11,yaw180. Actualcamera11950936modeltris+2048water,2618drawcalls; shadow6640971; LOD[604,456,113,0],1173chains0blocked0shadowlimited. NoFPSspeedupclaim; startupcompilationmustbedistinguishedfromsteadyframecost. Mainstillnotready.

Sourceinspectionfornextwork: ModelSurfacePicking isrigid-only/rootLOD, skipskin/morph, nativecachecapturedposednodes/materialsbutnotmorphPose/bakedsample. GiModelGeometry alreadyhasCPUskin/morph mathusingAnimation::paletteMatrices; reusabledeformationextractioncouldservebothinsteadduplicating. ActualGPUdisplacement/bakedanimation/waterstillneedrendered-IDpath. FPSScene currentlydrawscollider/spawnSceneRenderablesandpickSceneRenderable, NOTsameRTSpersistentModelcache; donotassumeRTSownerworksforFPS. SceneEntityOps stableId falselylight-onlywhileDocumentEntityIdentitysupportsentity:@authoredIDs. Separatefixifneeded.

Rendererstartupread-onlyfinding: ModelShaderKey comparesProgramtexturesURIalongsidebothbackendcodes; identicalgeneratedterrainprogramswithdifferentweighttexturepathsgetdistinctpipelinekeys. DoNOTremovethisguardwithoutreadinglatestMetal/incrementalhandoffsandprovingper-drawmaterialbindingsremainexact; GPUprogramsharingisnotmaterialbatchingandnoinferredspeedup. Nochangestosharedrendererthisstage. NextfocusforestLOD/artandactualsharedpresentationintegration, thenPRISMAreference/deformedpicking/liveTerraecology/GI/STRATA/VIVID/site/main. MacstilllockedactualpointertestsNOTRUN. LatestremoteTerra d04ca32a0; pendinglocalcomposition/snow/craggedstagesnotpushedyet. NoCI/finalpublication/broadcompletion.

## 2026-11-21 measured immediate surface publication CPU fix

Code c8c0c108c, feature/aiv87_terrain_pbr. User prioritised poor performance (18FPS screenshot), then camera-dependent/wrong bend current and rock undercut reactions. Native unlocked byuser; CPU source sampling independently found191/752 samples in mapLeaseFingerprint from stageSurfaceFrame. Steady view setup ~12.6ms hashed every byte of512x1024 MapGrid on every ordinary frame even though the resulting fingerprints were never consumed. Added private Immediate/Retained lease use. Ordinary synchronous publishSurfaceFrame fallback skips unused dependency snapshots ONLY; same OwnerPrepareCommit participant preparation, rollback, commit, terrain/culling/LOD/material state and publication barriers run unchanged. Retained topology, prewarm and captured-primary paths still capture/revalidate exact map/scene/camera/visibility fingerprints; immediate lease.matches failsclosed. Appended read-only SurfacePrewarmStats.sourceSnapshots diagnostics (not save/network state).

Editor geometry_stats now reads alreadycompleted Device.gpuFrameMs and existing PRISMA per-view timing records, separately labelled scopes. No GPU polling/waits/newtimestamps, empty viewportGpuTimings means unsupported/notavailable; never convert triangle counts intoGPUtime. Additive JSONfields only, no Studio bridge allowlist/UIchange. No simulation/schema/shader/mesh/texture/LOD/batching/defaultICBchange. Rebuild all C++ hosts for changed RtsView stats layout.

Actuallyexecuted KINGSTON: nativeEditor+aiverse_tests buildPASS immediate-surface-perf-build.log; later EditorAgent observabilitybuildPASS immediate-surface-observability-build.log. Existing unusedvariable/parameterwarnings atRtsView.cpp414/RtsGroundFx.cpp1314 unrelated, notsilentlyfixed. native/aiverse_tests lens_ PASS440checks0failed, immediate-surface-lens-tests.log. New test threeordinaryframes sourceSnapshots0, retainedprewarm1, in-place grid.creep mutationstillrejectsretainedlease andfreshimmediatefallbackpublisheswithoutanotherunusedsnapshot. Existing stale map/camera/visibility/topology/midprepareGPUfail/rollbackcohort suite included. Actual retainedLENS Metal RTcamera/fullGL/Vulkan NOTRUN; ordinarynativeMetalScene ran below.

A/B/A actualsame review-project-v24,1803entities, shorelinecamera[244,1,99],distance25,pitch48,yaw135. SourceA executable saved KINGSTON/perf-baseline-AiverseEditor, Bnativec8code. All runs same AIVERSE_FRAME_TRACE1,AIVERSE_PRISMA_VIEWPORT_GPU_TIMING1,AIVERSE_LOD_DIAGNOSTICS1, no experimental flags. Eight latest120-frame rolling summaries perrun, medians (notperframeP95): A setup12.6035/view14.550/Scene18.2485ms; B .300/2.0575/5.155ms; repeatedA12.3855/14.1705/17.1125ms. RendererCPU A2/B1.4595/1.462ms unchanged. JSON forest-performance-aba-cpu.json andthreeforest-performance*.logs KINGSTON. About70%less sceneCPU vs repeatedA, NOTa70%FPS/GPUclaim. A/Bcamera counters EXACT959302model+2048water,355drawcalls,1653731shadowtris,LOD[67,23,0,0],90chains0blocked0shadowlimited. before/afteractualclean1280x720PNGs inspected numerically; animatedwater/cloud/tone differenceswholeRGBmean2.105,foregroundROI.672, notbyte-identicalpixelproof. No geometry/appearanceconfigchange.

GPUwholecompletedtimevariable20..63ms inB, onesample34.799708; per-viewGPUcounterempty onthisbackend/config. Anotheroriginaleditor active, so NOTisolatedGPUbenchmark/FPSacceptance. OwnQA stoppedbetweenmeasurementsandafterA2 toavoidunnecessarycompetition; ONLYport47326 exactownedPID/binaryguard, user47325untouched. CUAcom.aiverse.editor resolvedoldbaselinewindowatfirst, so createdKINGSTON/AiverseTerraQA.app copiedbuiltbundle183MiB, uniquecom.aiverse.editor.terraqa/displaynameAIVERSETerraQA, removeddocumentassociationsandadhocsignedlocalartifact. No engineInfo.plist/sourceprojectchange. UIunlockconfirmed, actualYdrag/UndomouseNOTRUN becauseusersteeredtoperformance. BaselineUIread27FPSatdifferentfar10.23Mcamera isNOTcomparabletouser18FPS4.90Mimage; do notclaimthatgain. QA CURRENTNOTRUNNING, itsbinarycurrentlyAfromfinalrepeat. NativebuiltbinaryBincludesfix. Helper/private/tmp/run-terra-perf-version.py restoresAorB guardedonlyQAport; /private/tmp/terra-review-api.py needslive47326.

NEW active waterbug findings (notfixed): NereidWaterScene.h draw samples whitewaterAt(tile.center) into onehydraulicsuniformpertile, untiledfallbacksamplesobservercamera(windX/Z) andappliesit towholebody. Shaders.h basewave/rapid/wakeadvection alluseuniformwaterHydraulics.xy, so localSpline current/grade existscanonically butrenderusescoarse/camera-dependent direction. User providedbendimage andexplicitlyrequiresturningalongriver. Nextwaterstage needs camera-independent localflowpresentation fieldusingexisting gradedWaterBody/waterCurrentWithVortices/manualeditorcontrols; stableworldmapping acrossLOD, useit consistentlyfornormaladvection/rapidfoam/waketails. DoNOTcreate scene-localwatersolver. Existing Renderer3D.setPresentationTime +renderbench.freezeTime DOcontrolwater (Bench.h411); earlierauthoringdocstatementnotfrozenisoutdated. Usefixedtime/cameraA-Bworld-pointflowdebug andnativeMetal/GL captures toprovebend/camerainvariance.

NEW rockrequirement: whenNEREID/Terra erodesunderarock itmustsettleorsink/tilt/fall. CurrentTerraWeaveRocks.h ninefootprintsamples onlyinitialscatter/placement, notlivephysics. Existing soil/rootfallbridges plant-only; needsharedgenericrigidsupport component/system andactualnativePHYSIS/Box3D terrain-collider/rigidbodyhost, stableentities/persistence, no duplicatedfloatingpositions orfaketeleport. This remainsOPENwithliveTerraerosion/root/fall/age/seasonintegration.

Read-onlysecondCPUhotspot: fullfarforest RendererCPU~14ms andsamplemanyModelShaderKey::operator== longsamecodecomparisonsbeforetexturesdifferentURI. A cheapnegativebindingcheckbeforeexistingexactequalityinRenderer3DModels.cpp couldpreserveallshader/bindingidentityandavoidlargeGLSL/MSLmemcmp; doNOTremovetextureguardorclaimbatching/GPUgains. Nochangeimplementedyet. Startupnewgraphvariantcompilationhitchesalsoremain. Nextpriorities waterlocalflow +rocks andmeasuredperformance, thenpendingforestart/LOD, PRISMAvideo/deformedselection/GI/STRATA/VIVID/site/main. LatestremoteTerra719b42da4, CPUfixnotpushedatcheckpoint. NoCI/mainmerge/broadcompletion.

## 2026-11-21 camera-independent local current grid — CPU stage only

Code87ea8c294, feature/aiv87_terrain_pbr. Newengine/world/NereidFlowMap.h snapshots EXISTING gradedWaterBody +waterCurrentWithVortices atlocalgraded surfaceY-.1m. NereidFlowMapSource borrowscanonicalbody/geometry/grade/current/vortexcomponents onlyduringbuild; outputownsfloattexels, noWorld/GPUhandles/RNG/save state. Finiteboundsindependentofcamera/meshLOD, default2mworldlattice, max256axis/16384texels, power-of-two spacing growthunderbudget. Infinitewaterrequiresboundedexplicitpatch; overlappingequal-scale patches sampleidenticalworldcentres. RGworldXZvelocitym/s,Blocalrapidintensity,Avalid. CPUclamp+bilinearsampler, rebasedrecthelper, invalidnonfinite/budget/boundsreject ordisabledsourceempty. Headeruses Fixed::fromRaw roundedcanonicalpositions (no nonexistentfromDouble); floatneverfeedsphysics. No canonicalwatermembershipreplacement.

KINGSTONflow-map-configure.log CMakePASS; flow-map-build/final-build/guards-build PASSnewtargetaiverse_nereid_flow_map_tests. Final CTestnereid_flow_map1/1PASS.53s. Testsmanualconstantflow/rapidintensity/clamp/nonfiniteinput, gradeprojectionfollows31degreebend(nearp0X0,p1XnegativewithXZratio.6), manualoverrideunchanged, additiveexistingcurrentnodes/vortexincludingcanonicaltexelcompare, copiedsnapshotunchangedaftersourcemutation, overlappinginfinitepatchsamplingidentical, boundedenlargedgrid, invalidbudget/NaN/disable. NoGPU/editorintegrationclaimed. CMakeonlytesttarget, nopackagedclientdependency/moduledefaultchange. Architecture explicitlystatesCPUreadinessandmissingrendererhook.

NEXT REQUIRED GPUstage: currentNereidWaterScene drawstillonehydraulicsvectorpertile/observer-dependentuntiledfallback; userbugNOTyetfixedinvisiblewater. UploadthisCPUfield asboundedlinearRGBA16F (reusehdr_detail::toHalf), texslot6freeinlitMeshshader(existing0shadow1lights2reflection3height4opaque5depth); increaseShaderDesc.textureSlots6->7andbindwhite6fornonwater/reflection. AddwaterFlowRectvec4 toDrawUniforms+DrawUniformsR andALLmatchingGLSL/MSLU0blocks; appendborrowedflowTexture/rect toCameraGeometryCommand+DrawItemandpreservecapture/replayinCameraModelSnapshot. SampleflowatfragmentWORLDXZ, useitforbasewaveadvection/rapidfoam/obstacletails. Keepmanual/grade controlscanonical, legacyuntaggedwaterunchanged. Avoidunboundedworld-flow*time shearingforvariableflow: boundeddual-phaseflowmapadvectionwithsmoothresetmayberequired. Fixed renderer.setPresentationTime /bench.freeze-time alreadyexists, doNOTinventanotherclock.

Ownership/gates: flowmapversionseparatefromstaticVB/IB(meshrevisiondoesNOTchangeoncurrentfield/styleonlychanges). Finitefieldmustnotrebakeoncamera/LOD. Preserveconfluence transferFlow throughprepare-timecanonicalresolver; currentdraw callbackisnotavailableatGPUprepare unlessnativehostpassesitthere. NoGPUallocationsinsideactiveencoder/drawcallback. Immutabletextureversion+CPUcaptureleaseandGpuResourceRetirementfencesneeded; capturedsecondarycameras mustpinoldfielduntilCPUreplaydoneandlastGPUfencecompleted. No borrowedowner destruction afterDevice lifetime; explicitshutdown/quarantineifcapturesremain. Bounduploads/retainedtexturebytes andreportdeferredratherthanunboundednewtextures. DoNOTstealripple/wakearrayentries, overloadnormalattributes, addglobalMeshVertexoverhead, orincreasewaterdrawspersegmentjusttofakebends.

GPUacceptance: nativeflowdebugcolouratSAMEworldpoints projectedintotwocamerasandacrossLOD; actualcurvedriverfourframes(time0/1,cameraA/B), manualreverse, foamoff, vortex/grade,movingcamera, Metal+GLshaderpixels completeTRUE/pipelineFailures0. UserlatestrockrequirementstillOPEN: supportlostunderrockmustsettle/tilt/fallviaactualPHYSIS/Box3DhostandupdatedTerra colliders, notinitialscatteronly/teleportedvisualY. Broadtree/LOD/liveecology/PRISMA/deformedpicking/GI/STRATA/VIVID/site/mainremainactive. CPUperffixc8c0c108c+d71cbc24falreadyPUSHEDfeature/terraweave, ABACPU~17.1->5.155->17.1SceneMs,440LENSchecksPASS; noFPS/GPUclaim. CurrentownQA47326STOPPED(freesGPUforuser), QAartifactbinaryAfromrepeat, nativebuiltbinaryBhasCPUfix/GPUread-onlyfields. UnlockedMacconfirmedbutmouseYUndoNOTRUNduetouserperformance/flowsteering. NoCI/mainmerge/finalacceptance.


## 2026-11-21 river bends — GPU/current integration verified

Code ff84b6648bd3825eea62471614611fced9f196fc + GPU-test6402e0bbd28ceffa575a86ee283d4c1841322920. NEREID local world-space field now drives actual wave/foam advection, independent of camera and finite mesh LOD; fixed origin-dependent foam chevrons. Full API/lifetime/budget/failure/verification record: latest docs/nereid/HANDOFF.md. KINGSTON nativeEditor/Studio/Metal+GL hosts buildPASS,4CTestPASS,440LENSchecksPASS,22actual enabled-foam GPU capturesPASS including bent/manual/far/foam-off. Native review-project-v24 clean scene-only forest-local-current-final/close.png visually inspected; water fix only, not final art hero. No live rock support/fall implemented; this is the next user requirement, through existing Terra+NEREID+PHYSIS/Box3D owners and actual collider updates. Preserve original project/user app; no main readiness/GPU FPS/CI claim.
