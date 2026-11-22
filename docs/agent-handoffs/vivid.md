# VIVID agent handoff — chronological log

This is the **canonical living handoff** for AIVERSE VIVID. Read `docs/vivid.md`, `AGENTS.md`, the active branch HEAD and this log before touching code. After each implementation or design stage, commit the implementation first and append its handoff in a **separate** commit on `feature/vivid-digital-humans`. Never claim unexecuted tests passed.

## Stage 0 — 2026-11-19 — architecture contract

- **Branch**: `feature/vivid-digital-humans`, created from main `d71c6d47e532b25fb816a8bb1f96c41e5275a165`.
- **Implementation/doc commit**: `4ae116ecd3b80ecc99104c0734139b5045798898`.
- **Files changed**: `docs/vivid.md` (new).
- **What works**: stable product scope, layer boundaries, ownership, requirements, staged validation contract. No executable VIVID implementation at this stage.
- **API/persistence**: no API introduced, no engine ABI or save format change.
- **Tests actually executed**: NOT RUN — design-only stage.
- **CI**: NOT RUN / no CI result.
- **Risks / dependencies**: existing AIVERSE 8 GPU morph-target limit, 80 palette joints per draw, four skin influences, FBX blend shapes ignored, no verified groom pipeline. Must avoid touching `engine/render/` ownership without coordination. OpenRigLogic/MetaHuman asset licensing not audited.
- **Next exact step**: create `engine/character/vivid/` pure C++20 semantic facial core and its native unit tests; link through a separate, opt-in CMake target. Keep its dependency graph clean; commit implementation and then append Stage 1 handoff with actual tests.

## Stage 1 — 2026-11-19 — semantic face runtime / CMake / tests authored

- **Branch**: `feature/vivid-digital-humans`.
- **Implementation and documentation commits**:
  - `986a52ec64057964a07e582af493df95b0162f4c` — typed facial API (`Face.h`).
  - `fa5ef617aed009c05caf09a1b79771530b9e16f5` — 52 channel names, layering, presets, temporal smoothing (`Face.cpp`).
  - `a29913e15e08f4f22dc94983d38e0c5b2cc09226` — `tests/vivid/face.cpp` (release-safe exception-based checks).
  - `12aea6665b1866ca829fe4c04b878ce450c9de76` — separate `AIVERSE::vivid` target and `vivid_face` CTest in local CMake.
  - `675ee74e73442da8d12ae1470639531ef2a7c893` + `a66c9fd61eff8bed45baaa4f4e2a0af4f4ecbe00` — root CMake add_subdirectory and corrective newline fix.
  - `8f6c8bfef09ca76e30f15c88464ce04f47470a6f` — subsystem `AGENTS.md`.
  - `44f5bf558a4b1340d542e0d31730b1139f8ad1e3` — API and limits in `docs/vivid.md`.
- **Public API**: `aiverse::vivid::Channel`, `FaceFrame`, `FaceController`, `Layer`, `Emotion`, `Viseme`; source uses 52 fixed channel indexes in canonical order. No persistence, ECS or network shape changed.
- **Actual verification**: used GitHub fetch to verify the two code files have exactly 52 enum members, exactly 52 unique strings, 1:1 enum/name ordering and a correct root CMake registration line. **Native compile, CTest and rendering NOT RUN**: local container cannot resolve github.com to fetch a complete repository; no AIVERSE checkout/build host is connected. No GPU/Metal claim.
- **Known limitations**: no glTF/FBX facial binding, no actual mesh deformation or rendering, only 6 authored viseme presets and 6 non-neutral emotion presets; no MetaHuman DNA parsing, no audio analysis, no eye/skin/groom shaders; responseSeconds retained by reset intentionally. Do not label as complete character system.
- **Build/test commands for a checkout (NOT RUN here)**:
  ```sh
  git fetch origin feature/vivid-digital-humans
  git switch feature/vivid-digital-humans
  cmake -S . -B build/vivid -DAIVERSE_BUILD_TESTS=ON
  cmake --build build/vivid --target aiverse_vivid_face_tests -j 4
  ctest --test-dir build/vivid --output-on-failure -R '^vivid_face$'
  ```
- **Next exact step**: implement `VividMorphBinding` bridge to `render::Model` / `render::Pose` with typed per-node 0..7 morph-index routes, validation and weight policies; author headless pose tests and document that native tests still require the AIVERSE build host. Keep 52 semantic channels separate from 8 legacy GPU morph slots.

## Stage 2 — 2026-11-19 — native-to-render::Pose adapter

- **Branch**: `feature/vivid-digital-humans`.
- **Implementation/doc commits**:
  - `ae4838657d6c2a08529db47728f72234b3e6ee9d` — public `MorphBinding.h` contract.
  - `a90d8c3b4348d305e12a7561abdf6fc0f33ca723` — validated adapter implementation.
  - `9a0b2f1c8faa6e84771efe20d301b6fbc3b65313` — headless `tests/vivid/morph_binding.cpp`.
  - `4b5a761fde6e57f02b3646863fd2ecfd9a602d3a` — `AIVERSE::vivid_render` CMake target, new `vivid_morph_binding` CTest.
  - `daddf30df5fadafe0dbaadc468bad9d48aebc8ff` — stage 2 usage/status in `docs/vivid.md`.
  - `5bea692d31bf83ed48269e619471afdde4de5571` — root `AGENTS.md` VIVID pointer.
- **Public API**: `MorphRoute{Channel, node, target, scale}`, immutable `MorphBinding::compile(Model, routes)`, `MorphBinding::apply(FaceFrame, Pose, influence, MorphApply)`. No game save/network ABI changes; `Model` and `Pose` layouts untouched. Adapter is opt-in; callers must invoke it **after** AnimationGraph evaluation.
- **Tests actually executed**: no native compile/CTest/GPU (NOT RUN; remote source was modified through GitHub connection, no checkout/build host available). Static GitHub fetch verified both sources and CMake definitions on this branch. CTest source includes invalid route, missing target, duplicate route, invalid source and pose, override/additive/preserve-other-values cases; these are **authored, not passing tests**.
- **CI**: NOT RUN / not checked.
- **Known limitations**: 8-slot GPU morph cap remains, bridge currently requires explicit routes with node/target indices; no user-facing asset config parser; not wired automatically into renderer/editor; no FBX blend-shape import, MetaHuman DNA/rig logic or real character appearance.
- **Build/test commands for checkout (NOT RUN here)**:
  ```sh
  git fetch origin feature/vivid-digital-humans
  git switch feature/vivid-digital-humans
  cmake -S . -B build/vivid -DAIVERSE_BUILD_TESTS=ON
  cmake --build build/vivid --target aiverse_vivid_face_tests aiverse_vivid_morph_tests -j 4
  ctest --test-dir build/vivid --output-on-failure -R '^(vivid_face|vivid_morph_binding)$'
  ```
- **Next exact step**: Stage 3 investigation/implementation of sparse >8-target morph buffer layout with backends and animation channel ABI compatibility, or first add validated asset-authored per-character route mapping and a CPU modelview preview. Test Stage 1+2 in the real checkout before changing shared renderer/animation sources. Follow root `AGENTS.md` build protocol and record measured numerical/visual results.

## Native validation checkpoint — 2026-11-19 — user-provided macOS run

- **Branch tested by user**: checkout at `/Users/aleksandrfeer/Projects/AIVERSE-vivid/build/vivid`, created from `origin/feature/vivid-digital-humans`. Exact tested SHA not included in terminal output; the test results should NOT be attributed to a later commit.
- **User-reported build**: `[100%] Built target aiverse_vivid_morph_tests`.
- **User-reported CTest**: `vivid_face` PASS (0.69 s), `vivid_morph_binding` PASS (0.38 s), **2/2 tests passed**, total real 1.08 s. These are the first actual native test results reported for Stages 1-2. Not independently rerun by this agent.
- **Unrelated shell errors**: after successful CTest, zsh attempted to execute pasted diagnostic/prompt text, generating `no matches found` and `command not found`; not a build/test failure.
- **GPU/visual**: NOT RUN. No rendering acceptance or MetaHuman import demonstrated.
- **Next**: stage 3 implementing an extensible CPU-reference facial morph representation and GPU-ready offline sparse buffers, without changing existing `Model` ABI or claiming that the engine's GPU render backend accepts >8 morphs.

## Stage 3A — 2026-11-19 — scalable sparse morph CPU/reference format

- **Branch**: `feature/vivid-digital-humans` (based on main `d71c6d47e532b25fb816a8bb1f96c41e5275a165`).
- **Implementation/document commits**:
  - `3c14d358b6c29682cbfa704af40829f3b2b95f22` — `SparseMorph.h` public geometry, immutable packed layout and CPU evaluation contract.
  - `4c57498612f982d9dcd8b7e76eefb7b81b386a71` — `SparseMorph.cpp` name/index validation, canonical sorting, zero-delta compaction and CPU deformation.
  - `a3eb0c30480a73ffa2028b94992a8b9c0a3cdfd9` — `tests/vivid/sparse_morph.cpp`.
  - `2a60fcbfaf61d7aa64400a2b86abd81b3d645757` — add `SparseMorph.cpp` to `aiverse_vivid`, register `vivid_sparse_morph`.
  - `ea7c3a5899cf18281d470467998bef0b92c872c6` — documentation design/capabilities/status.
- **Public API**: `MorphVertex`, `MorphDelta`, `MorphTargetInput`, `GpuMorphDelta` (64B align16), `GpuMorphRange` (16B align16), `SparseMorphLibrary::compile/evaluate`. Versioned GPU binary serialization NOT yet implemented; current ABI only defined in C++.
- **Compatibility**: no shared `render::Model`/RHI or ECS mutation; existing morph and animation pipeline unchanged. Core independent of render/genre.
- **Tests actually executed by this agent**: NOT RUN, no local AIVERSE checkout. Static source review only. Earlier user Mac run of Stage 1+2 reports 2/2 PASS, and says nothing about new test `vivid_sparse_morph`.
- **CI/GPU**: NOT RUN. GPU layout *specified*, not uploaded or dispatched.
- **Remaining limitations and risks**: sparse accumulation by target is a CPU oracle and not a usable GPU implementation because naive scatter would need atomics. Per-vertex gather adjacency must be compiled before backend implementation. Each delta uses 64 bytes, capacity fixed to 16M deltas/4096 targets. Names are case-sensitive. No MetaHuman import or actual model asset path. CPU evaluator assumes base/output are either identical spans or nonoverlapping buffers; partial overlap must not be provided.
- **Next exact step**: implement compiler to map a semantic `FaceFrame` to named sparse targets (one channel can affect multiple targets; no duplicate target ownership), and CPU tests. Then define and test GPU gather representation, choose how to integrate with existing `render::Model` and RHI without affecting other agents' renderer work.
- **Build/test on user's Mac (NOT RUN in this stage)**:
  ```bash
  cd ~/Projects/AIVERSE
  git fetch origin feature/vivid-digital-humans
  git -C ../AIVERSE-vivid switch --detach origin/feature/vivid-digital-humans
  cmake -S ../AIVERSE-vivid -B ../AIVERSE-vivid/build/vivid -DAIVERSE_BUILD_TESTS=ON
  cmake --build ../AIVERSE-vivid/build/vivid --target aiverse_vivid_face_tests aiverse_vivid_morph_tests aiverse_vivid_sparse_tests -j 4
  ctest --test-dir ../AIVERSE-vivid/build/vivid --output-on-failure -R '^vivid_(face|morph_binding|sparse_morph)$'
  ```

## Stage 3B — 2026-11-19 — typed semantic-to-sparse target routing

- **Branch**: `feature/vivid-digital-humans`.
- **Implementation SHA / paths**: `0c4e55901df73b06265336a81cbe07132a46574c` (`FaceShapeBinding.h`), `6f0ebd68d9d91797081d3fc81b9ded6eb74629b0` (`FaceShapeBinding.cpp`), `96ab1360bcb260a61f66708281e30eb22abd3e83` (`tests/vivid/shape_binding.cpp`), `c524927b58cf144bd0d65c38979fc95bd54b5fd3` (local CMake adds compilation/test), `feb841d717f51c6fea5663ce5d5550527c1e7fc7` (`docs/vivid.md`).
- **API**: `ShapeRoute`, `FaceShapeBinding::compile(SparseMorphLibrary, routes)`, `evaluate(FaceFrame, weights, influence)`. Case-sensitive names, one target destination per route; one channel may drive many targets. Explicit output length = library target count.
- **Compatibility**: pure portable C++20 in `AIVERSE::vivid`, no engine RHI/Model changes and no saved game/network schema changes.
- **Tests actually executed**: NOT RUN on this new source revision. New test `vivid_shape_binding` authored with 97 routes and end-to-end CPU reference. Earlier 2/2 tests user-reported PASS refer only to Stage 1+2.
- **CI/GPU**: NOT RUN. No actual render or GPU compute.
- **Next exact step**: add deterministic CSR per-vertex GPU gather adjacency derived from `SparseMorphLibrary` so shader can evaluate every vertex without float atomics. Do not rely on the current 8-morph `render::Pose` for 52+ channels. Then test GPU compute on Metal and GL/Vulkan.
- **Mac native test commands (NOT RUN yet)**:
  ```sh
  cd ~/Projects/AIVERSE
  git fetch origin feature/vivid-digital-humans
  git -C ../AIVERSE-vivid switch --detach origin/feature/vivid-digital-humans
  cmake -S ../AIVERSE-vivid -B ../AIVERSE-vivid/build/vivid -DAIVERSE_BUILD_TESTS=ON
  cmake --build ../AIVERSE-vivid/build/vivid --target aiverse_vivid_face_tests aiverse_vivid_morph_tests aiverse_vivid_sparse_tests aiverse_vivid_shape_tests -j 4
  ctest --test-dir ../AIVERSE-vivid/build/vivid --output-on-failure -R '^vivid_'
  ```

## Stage 3C — 2026-11-19 — immutable CSR adjacency for race-free GPU morph gather

- **Branch**: `feature/vivid-digital-humans`.
- **Implementation SHA / paths**: `f647646dbd136a84915ccb37d2bfe3ced744908a` (`SparseMorph.h`, public `GpuMorphLink` and CSR accessors); `1328583597b4605c2f0aab1333a656bf5c6c868e` (`SparseMorph.cpp`, CSR compiler and `evaluateGather()`); `90429d7c8a88c71a1900fa640c5cecdaca23c114` (`tests/vivid/sparse_morph.cpp`, CSR ordering/CPU parity fixtures); `e8de015b93d165aadc34ecbb1530a96d8edc6338` (`docs/vivid.md`, GPU handoff contract).
- **API/ABI**: Added `GpuMorphLink` (8 bytes), `SparseMorphLibrary::gpuVertexOffsets()`, `gpuVertexLinks()`, `evaluateGather()`; `bufferBytes()` now counts CSR allocations. No existing `render::Model`/Pose or save/network ABI changed. `SparseMorphLibrary` binary layout is still internal and not serialized into external assets; provide a schema version before persistence.
- **Implementation**: CSR gather is computed once at library compilation. Offsets count = vertices+1; links count = nonzero delta count and are ordered by target index per vertex. CPU gather mirrors intended one-thread-per-vertex shader, not requiring float atomics. CPU `evaluate()` remains a separate reference implementation.
- **Tests actually run**: NOT RUN on this revision, no build host attached to the coding session. Test fixture source is authored with 160-target parity, invalid input and expected offsets. Prior user-confirmed Stage 1+2 tests 2/2 PASS do not validate this new stage.
- **CI, Metal, Vulkan, GL**: NOT RUN. GPU kernel, dispatch/barrier integration, GPU buffers and character preview are not implemented. Do not claim performance or visual fidelity.
- **Risks**: CSR duplicates the target-major lookup (additional ~8 bytes/delta plus 4 bytes/vertex). Both formats remain until GPU backend chooses an optimal representation. Model vertex interleaving, skinning order and resource upload lifetime are unresolved and must be coordinated with renderer/texture RHI agents. Data is bounded to 16M vertices/deltas to avoid untrusted content overflows.
- **Next exact step**: native compile and full `^vivid_` CTest on user's Mac. If that passes, implement a backend-independent VIVID `GpuMorphDispatch` buffer/command descriptor and start a Metal compute backend behind an opt-in feature flag, comparing GPU readback vs `evaluateGather()`. Do not modify common RHI contracts without coordinating changes with texture/render agents.
- **Mac checkout and test commands (NOT RUN by this agent)**:
  ```sh
  cd ~/Projects/AIVERSE
  git fetch origin feature/vivid-digital-humans
  git -C ../AIVERSE-vivid switch --detach origin/feature/vivid-digital-humans
  cmake -S ../AIVERSE-vivid -B ../AIVERSE-vivid/build/vivid -DAIVERSE_BUILD_TESTS=ON
  cmake --build ../AIVERSE-vivid/build/vivid --target aiverse_vivid_face_tests aiverse_vivid_morph_tests aiverse_vivid_sparse_tests aiverse_vivid_shape_tests -j 4
  ctest --test-dir ../AIVERSE-vivid/build/vivid --output-on-failure -R '^vivid_'
  ```

## Stage 3D — 2026-11-19 — fix Xcode/Clang C++20 span compilation in shape test

- **Branch**: `feature/vivid-digital-humans`. **Fix commit**: `9b7e5b1e8e2cd6a25a842a25da0437de7756c354`.
- **Affected file**: `tests/vivid/shape_binding.cpp`. No runtime source, library API, ECS, network or persistence changes.
- **User's Mac evidence (before fix)**: Xcode 26.2 SDK libc++ rejected six `FaceShapeBinding::compile(library, {{...}})` calls because C++20 `std::span<const ShapeRoute>` cannot be built implicitly from a braced initializer list. The log contains 6 primary compiler errors and 60 secondary `note:` diagnostics; no `warning:` entries. The test executable failed to build, so CTest reported `vivid_shape_binding (Not Run)`. The other three tests — `vivid_face`, `vivid_sparse_morph`, `vivid_morph_binding` — **passed** in the user's pre-fix run (3/4).
- **Fix**: tests now use `rejectRoutes(library, std::initializer_list<ShapeRoute>)`, materialize an owned `std::vector<ShapeRoute>`, and pass it as a valid view to `compile`. All six invalid-input fixtures remain intact, including duplicate destinations; production API continues to accept `std::span<const ShapeRoute>` without acquiring ownership.
- **Tests actually executed by this agent**: local isolated C++20 Clang `-fsyntax-only` example of the exact owned-vector-to-span helper passed with `-Wall -Wextra -Wpedantic -Werror` (no warnings). Static inspection confirmed six replacements and zero remaining braced-list direct calls in `shape_binding.cpp`. **Full AIVERSE Mac build, CTest and GPU tests NOT RUN** in this session; require user's follow-up run before marking 4/4.
- **CI**: not run / not verified.
- **Next exact step**: pull latest branch in existing `~/Projects/AIVERSE-vivid` worktree, configure/build all four VIVID tests, run `ctest --output-on-failure -R '^vivid_'`. If warnings or errors remain, capture the full unformatted build log and repair the first primary compiler diagnostic; do not mask compiler warnings.
- **Mac commands**:
  ```sh
  cd ~/Projects/AIVERSE
  git fetch origin feature/vivid-digital-humans
  git -C ../AIVERSE-vivid switch --detach origin/feature/vivid-digital-humans
  cmake -S ../AIVERSE-vivid -B ../AIVERSE-vivid/build/vivid -DAIVERSE_BUILD_TESTS=ON
  cmake --build ../AIVERSE-vivid/build/vivid --target aiverse_vivid_face_tests aiverse_vivid_morph_tests aiverse_vivid_sparse_tests aiverse_vivid_shape_tests -j 4
  ctest --test-dir ../AIVERSE-vivid/build/vivid --output-on-failure -R '^vivid_'
  ```

## Validation checkpoint — 2026-11-19 — macOS VIVID 4/4

- **Verified source commit**: `31d04c986ada3ff4602d303d28fa4cdc08686de8` (VIVID Xcode `std::span` fix + Stage 3C), in `feature/vivid-digital-humans`. User's local worktree was detached at exactly this commit per the provided `git switch --detach origin/feature/vivid-digital-humans` output.
- **Documentation status commit**: `b50851f11b0068a9c20417a5a2545c2c8bcc04aa` (`docs/vivid.md`), following this user's validation report.
- **Actual test evidence**: user reported successful CMake configure/generate (4.1 s / 0.9 s) and completed builds of `aiverse_vivid`, `aiverse_vivid_face_tests`, `aiverse_vivid_morph_tests`, `aiverse_vivid_sparse_tests`, `aiverse_vivid_shape_tests` on macOS. CTest at `/Users/aleksandrfeer/Projects/AIVERSE-vivid/build/vivid`:
  - `vivid_face`: Passed (0.03 s).
  - `vivid_sparse_morph`: Passed (0.03 s).
  - `vivid_shape_binding`: Passed (0.61 s).
  - `vivid_morph_binding`: Passed (0.03 s).
  - **100% tests passed, 0 failed / 4, total real 0.71 s.**
- **Diagnostics**: The supplied post-fix output contains no `error:`, `warning:`, or `note:` compiler diagnostics; this is limited to the displayed incremental build, not a warning-as-error clean full rebuild.
- **Status interpretation**: Stages 1, 2, 3A, 3B, 3C now have user-confirmed local native unit tests on macOS for this exact revision. Testing was performed by the user; this agent recorded evidence but did not independently rerun it. GitHub CI, comprehensive engine suite, GPU shaders, render captures, cross-platform tests and MetaHuman DNA import remain **NOT RUN / NOT IMPLEMENTED as appropriate**.
- **API/save compatibility**: this checkpoint changes no executable API, engine ABI or game persistence, only documentation. Existing `render::Model` eight-morph GPU limit is still in effect.
- **Next actionable step**: begin Stage 4 as opt-in GPU morph execution, first inspect the existing RHI buffer/compute and model skinning contracts on current main and feature branches. Implement Metal compute dispatch and readback parity against `SparseMorphLibrary::evaluateGather`, with a backend-neutral command/resource interface, while keeping legacy rendering unchanged. Add real GPU tests and a separate implementation + handoff commit before claiming working GPU rendering.

## Stage 4A — 2026-11-19 — opt-in RHI GPU morph dispatch

- **Branch:** `feature/vivid-digital-humans`, previous checkpoint `2dfa495471e055adf69179db384db754934365e9`. **MotionWeave source inspected** at `feature/universal-ecs-animation-pipeline` HEAD `2ae54740331d7ed6ba8d8e01e6c39e753391852f`; **not merged**, because Stage 4A does not consume it and 913 unrelated commits/245 changed files would introduce large simultaneous integration risks.
- **Exact implementation SHAs/paths**:
  - `2eb2e3308f852b5cf424b5b7cd02b7b0f9d32318` — NEW `engine/character/vivid/VividComputeShaders.h` (matching Metal/GLSL CSR compute kernels).
  - `823c690802853aeeb84a64e6b78de9ebd67b1b84` — NEW `engine/character/vivid/GpuMorph.h` (48B GPU vertex, 32B uniform, backend-neutral API, resource ownership).
  - `fb32ea3b0027d90f87778bdd425d86728ec1bf10` — NEW `engine/character/vivid/GpuMorph.cpp` (six storage buffers, memory caps, weight validation, dispatch/readback/cleanup).
  - `9d641cd433df367127fc42eb8c832beb3a8f9ca0` — NEW `tests/vivid/gpu_contract.cpp` (recording RHI and error-path fixture).
  - `23370138ff37e5672bf9e427aabf16d10eb9a3cc` — `GpuMorph.cpp` correct buffer allocation size to avoid reading more bytes than 1–3 small weight values at initialization.
  - `6342d505f37610894c4b8e2ac60bc59ab8fe6f4d` — remove unused partial GPU fixture.
  - `9ed748eaa639f3d3e1484d25b0c696cc78bee4b3` — `engine/character/vivid/CMakeLists.txt` adds `AIVERSE::vivid_gpu` and `vivid_gpu_contract`.
  - `51d1b4accdbb50eeeff24cf9d060aff23fda52e0` — recording test correctly handles weight upload before an out-of-frame `beginCompute` rejection.
  - `cb4fc06813c897a62e4bb9f695ce97b082ca8d24` — `docs/vivid.md` feature documentation and MotionWeave compatibility plan.
- **What works at source level**: VIVID now supplies RHI-neutral `GpuMorphDeformer` for the existing `gfx::Device` compute contract, paired MSL/GLSL kernels, explicit GPU buffer packing, opt-in CMake target and headless fake-RHI fixture. CPU `SparseMorphLibrary::evaluateGather` remains a reference; no change to 8-morph legacy `render::Model` runtime.
- **Compatibility and public API**: `AIVERSE::vivid_gpu` adds `GpuMorphDeformer::initialize/dispatch/download/release/outputBuffer`, `GpuMorphVertex` and `GpuMorphUniforms`. No changes to `engine/gfx/Gfx.h`, Metal/GL/Vulkan backend implementation, MotionWeave, simulation, game saves or network.
- **Tests actually executed by this agent**: **NOT RUN** (the development environment does not have a full local AIVERSE checkout and cannot resolve github.com for clone). Static connector review of gfx::Device, Metal compute, source and CMake contracts performed. **New CTest `vivid_gpu_contract` has NOT RUN**; its fake backend validates command binding but **does not run a GPU shader**. User-confirmed earlier 4/4 native CPU CTest pass applies only to `31d04c986` and earlier.
- **CI, GPU execution, real device readback, screenshots, warnings**: NOT RUN/NOT CHECKED. No GPU-vs-CPU numerical comparison has actually been executed on Mac yet. Do not claim Metal correctness from the fake-backend test.
- **Known issues and risks**: RHI has no `inFrame()` query. `dispatch` may upload weights before `beginCompute` rejects an out-of-frame call; no GPU dispatch occurs, but it can increment upload metrics. Real GPU buffer synchronization/usage, Metal shader acceptance, render->skinning pipeline integration, GL/Vulkan compute support and resource lifetime must be tested. The output format is `GpuMorphVertex` (48B), not legacy `ModelVertex` (60B); never feed it straight into model draws. No 52+ GPU shapes render until the renderer explicitly uses this deformed stream.
- **MotionWeave handoff**: use presentation ECS `MotionWeave::update(presentationWorld,dt)` or persistent scheduler overload from `<aiverselib/motionweave/MotionWeave.h>` once that branch stabilizes. Final rig/retarget pose must remain MotionWeave-owned; VIVID applies optional facial offsets afterwards and deforms only the character's render stream. MotionWeave Stage 125 user deferred tests; do not call it production verified. Never copy/squash 913 animation commits into VIVID just for this GPU step.
- **Next exact step**: build and run all five VIVID CTests on user's Mac. Fix any compiler warnings/errors and append new handoff with exact test SHA. Then create a REAL Metal graphics-device test that launches compute, reads output between frames, compares to `SparseMorphLibrary::evaluateGather()` across 160 targets and checks normal/tangent parity. Add opt-in integration to the render-stream ownership only after GPU parity and resource lifetime tests pass. Coordinate with MotionWeave agent when entering final-pose integration.
- **Mac commands (NOT RUN by this agent)**:
  ```sh
  cd ~/Projects/AIVERSE
  git fetch origin feature/vivid-digital-humans
  git -C ../AIVERSE-vivid switch --detach origin/feature/vivid-digital-humans
  cmake -S ../AIVERSE-vivid -B ../AIVERSE-vivid/build/vivid -DAIVERSE_BUILD_TESTS=ON
  cmake --build ../AIVERSE-vivid/build/vivid --target aiverse_vivid_face_tests aiverse_vivid_sparse_tests aiverse_vivid_shape_tests aiverse_vivid_morph_tests aiverse_vivid_gpu_contract_tests -j 4
  ctest --test-dir ../AIVERSE-vivid/build/vivid --output-on-failure -R '^vivid_'
  ```

## Native validation — 2026-11-19 — user confirms Stage 4A 5/5

- **Source revision**: `b256a1ec35da33cc8f2a484551cc02e7b50ab822` (`feature/vivid-digital-humans` as last reported to user). Exact local `HEAD` not reprinted in this log, so the revision is inferred from the supplied pull/build instructions, not independently obtained from the Mac.
- **User-provided macOS build evidence**: `[100%] Built target aiverse_vivid_gpu`, compiled and linked `aiverse_vivid_gpu_contract_tests`; five CTest results PASS: `vivid_face` (0.15 s), `vivid_sparse_morph` (0.16 s), `vivid_shape_binding` (0.16 s), `vivid_gpu_contract` (0.53 s), `vivid_morph_binding` (0.16 s); **5/5 passed; total real time 1.18 s**. No warning/error lines in supplied excerpt.
- **Scope**: this validates CPU/reference and a **fake gfx::Device** recording contract. It does not validate shader compilation on Apple Metal hardware, GPU numerical parity, graphical face deformation, MotionWeave integration, cross-platform output or the full engine suite.
- **Actor of verification**: tests executed by user on Mac; this agent recorded the result, did not rerun CTest locally.
- **No runtime changes** for this checkpoint. Next: implement a macOS-only real-device Metal compute CTest, compare output readback after `endFrame` to `SparseMorphLibrary::evaluateGather`, preserve previous five tests.

## MotionWeave merge checkpoint — 2026-11-19

- **VIVID destination branch:** `feature/vivid-digital-humans`, source `feature/universal-ecs-animation-pipeline`.
- **Exact source snapshots:** VIVID parent `dfd4f3fab67cd573da4322ddad2f108a27f7cb38`; MotionWeave parent `548f75f0ce824db6f468f6577da95387d388ea0b`. Complete two-parent **merge commit `f27c076410a5cb0aa7d12cc689d83c7eff302928`**, source tree `c0f7a96eeb711bf5f895787212df479a3451203a`. MotionWeave snapshot tree `a72c12db735584caabb808ac5340ddd0effa49af`.
- **Resolution method:** PR #37 reported conflicts. Reconstructed normal Git merged tree using full MotionWeave tree as base, restored intact VIVID-added directories (`engine/character/vivid`, `tests/vivid`), docs (`docs/vivid.md`, `docs/agent-handoffs/vivid.md`) from the VIVID parent, and combined both branch deltas in the only shared edited files `AGENTS.md` and `CMakeLists.txt`. Created a true merge commit with both parents and advanced only VIVID with a fast-forward expected-head lease. MotionWeave branch and `main` did not move.
- **Verified using GitHub comparison and file fetch:** merged head `f27c0764` is ahead of MotionWeave snapshot by 52 commits and 0 behind; tree contains public `libraries/include/aiverselib/motionweave/MotionWeave.h`, `engine/character/vivid/GpuMorph.cpp`, VIVID's CMake registration, MotionWeave target `AIVERSE::MotionWeave` and respective docs/agents ownership.
- **Public API/save format:** no symbols renamed or removed deliberately; the merged branch now exposes MotionWeave's presentation ECS, scheduler, AnimationPose, retargeting, existing VIVID face controls and opt-in morph GPU. No game save/network migration. Before relying on APIs, compile on Mac; the merge may reveal latent incompatible changes or code-generation dependencies.
- **Documentation status commit:** `de8612040d52e49259ff96f04421e372da9d1cb2` (`docs/vivid.md`).
- **Tests actually run on this merged revision:** **NOT RUN**. Prior VIVID 5/5 Mac tests were on the pre-merge code. MotionWeave Stage 126 handoffs state deferred build/tests; do not claim any inherited CI pass or platform parity. No merged Metal GPU numerical readback has been run.
- **Risks:** MotionWeave includes extensive shared Renderer3D, Gfx, Model, render texture and LOD lifetime work. Test cross-module link order, FPS and RTS asset/presentation regressions, experimental GPU resource ownership, especially under hot-reload. If current MotionWeave source continues advancing, integrate new deltas deliberately after validation; do not copy worker or graph implementations into VIVID.
- **Next exact implementation step:** add a small bridge consuming MotionWeave `AnimationPoseComponent` **after** MotionWeave::update; produce a derived render::Pose with VIVID facial morph weights, never modify MotionWeave's cached pose or create a duplicate AnimationGraph. Tests must cover repeat-frame composition without weight accumulation and invalid pose. Then build/run all VIVID + MotionWeave tests on Mac.

## Stage 4B — 2026-11-19 — consume MotionWeave pose without extra animation pipeline

- **Branch:** `feature/vivid-digital-humans` (MotionWeave source `548f75f0ce824db6f468f6577da95387d388ea0b` merged previously as `f27c076410a5cb0aa7d12cc689d83c7eff302928`). PR #37 was opened during conflict diagnosis, then closed after the equivalent two-parent merge was completed manually; it was not an additional PR merge.
- **Implementation SHAs and files**:
  - `ddc93cd481eca7e108d3e6494090b0d18c2cf7c5` — `engine/character/vivid/MotionWeaveBridge.h` public post-pose composition API.
  - `a883e2f6ba3c3ff5be892db9d7626ce3fdb4e11e` — `MotionWeaveBridge.cpp`, caller-owned output/scratch pose double-buffer, copy final MotionWeave source and apply only VIVID morph weights, swap on success.
  - `e2fa835f689cf974bf2f32b5d6a574b4ec26bbb1` — `tests/vivid/motionweave_bridge.cpp`, MotionWeave ECS World + AnimationGraph bind fixture and repeated additive/no-drift checks.
  - `944b9a115c051518feb53bf4a16c83ad4551b5e3` — local VIVID CMake adds `AIVERSE::vivid_motionweave`, target and CTest `vivid_motionweave_bridge`.
  - `3096f54f337ef332b95fd6a191148c6e733c6244` — root CMake moves VIVID add_subdirectory AFTER declaring the `AIVERSE::MotionWeave` alias/interface to support linking correctly.
  - `62bf6edd715d99642f6474e0578d0f4671718801` — add headless `aiverse::platform::readContentFile` fixture hook, matching MotionWeave's existing standalone native test.
  - `82e0b760e9bd5f519e9914ffef1d1c151d4021cf` — `docs/vivid.md` usage and status.
- **Public API**: `aiverse::vivid::composeFaceAfterMotionWeave(const aiverse::motionweave::AnimationPoseComponent&, const FaceFrame&, const MorphBinding&, render::Pose& output, render::Pose& scratch, float influence, MorphApply)`. Output/scratch must differ and must not alias MotionWeave's cached local pose. Invalid source/influence/binding must not change the previously completed output.
- **Ownership:** MotionWeave is the only writer of skeletal pose, AnimationGraph state, workers, IK and retargeting. VIVID derives a separate final render pose with legacy facial morph channels; no new animation scheduler/graph/ECS world. Only 0..7 `render::Pose` morph indices are used in this adapter; VIVID's separate sparse GPU stream remains 52+ capable but not yet consumed by renderer.
- **Tests actually run:** **NOT RUN** on this merged revision. Earlier user reported 5/5 VIVID native tests PASS before merge; these do not cover combined MotionWeave CMake or the new bridge. No Mac build, GitHub CI, GPU readback, visual capture or full suite has verified this integration.
- **Risks and limitations:** MotionWeave feature branch contains extensive yet-unverified resource and renderer changes; merged tree should be configured in a fresh build directory. The new adapter intentionally does not change bone matrices for facial jaw or eye joints, and it must execute after MotionWeave worker tasks are complete. A separate owned render pose is necessary to avoid accumulating facial values when MotionWeave animation LOD throttles pose evaluation.
- **Next exact step:** on Mac, fetch/reselect merged `feature/vivid-digital-humans` in existing worktree, configure with `AIVERSE_BUILD_TESTS=ON`, build six VIVID tests including `aiverse_vivid_motionweave_tests`, run `ctest -R '^vivid_'`. Build and run MotionWeave's standalone `aiverse_motionweave_api_tests` too; then consider full graphics/preview integration. Fix any first compiler warning/error and append a new handoff with actual results.
- **Mac verification commands (NOT RUN in this stage):**
  ```sh
  cd ~/Projects/AIVERSE
  git fetch origin feature/vivid-digital-humans
  git -C ../AIVERSE-vivid switch --detach origin/feature/vivid-digital-humans
  cmake -S ../AIVERSE-vivid -B ../AIVERSE-vivid/build/vivid -DAIVERSE_BUILD_TESTS=ON
  cmake --build ../AIVERSE-vivid/build/vivid --target aiverse_vivid_face_tests aiverse_vivid_sparse_tests aiverse_vivid_shape_tests aiverse_vivid_morph_tests aiverse_vivid_gpu_contract_tests aiverse_vivid_motionweave_tests aiverse_motionweave_api_tests -j 4
  ctest --test-dir ../AIVERSE-vivid/build/vivid --output-on-failure -R '^(vivid_|motionweave_api$)'
  ```

## Stage 4C — 2026-11-19 — MotionWeave ECS visibility compile fix

- **Branch:** `feature/vivid-digital-humans`. Merge source snapshot MotionWeave `548f75f0ce824db6f468f6577da95387d388ea0b`; original `feature/universal-ecs-animation-pipeline` was **not modified**. Previous VIVID integration HEAD `65ef792af7f7650a4701caa3fc5a0f433184d4d3`.
- **External build evidence from user:** In `~/Projects/AIVERSE-vivid/build/vivid`, Clang/Xcode compilation of `aiverse_vivid_motionweave` failed at `libraries/include/aiverselib/animation/MotionWeaveScheduler.h` lines 53, 54, 55, 62. Four primary errors: `World::poolIfExists<T>()` is private in `engine/core/ECS.h:548`; the many `note` messages only identify that declaration. No standalone `warning:` messages appear in the shared log. Other previously built tests still passed: `vivid_face`, `vivid_sparse_morph`, `vivid_shape_binding`, `vivid_gpu_contract`, and `vivid_morph_binding` — **5 passed**. `motionweave_api` and `vivid_motionweave_bridge` were **Not Run** because their executables did not exist after the failed build. CTest reported 5/7 effective passes, 2 Not Run. Do not describe these as runtime test failures.
- **Code fix commit `3b413656fafecc54c466baa5ab6b83681a43cb6d`:** replace the four private `read.poolIfExists<Component>()` accesses with public `read.tryGetConst<Component>(e)` inside the existing per-entity loop. Read-only, no implicit pool creation, no COW detachment; retains use of `prepareAnimationPresentation` for explicit writable pools and MotionWeave's existing worker/barrier logic. No `World` API exposure or ECS layout change.
- **Regression fixture commit `c1f76ed9bb0c21d0b3056225d0666ee29b81a938`:** extend `tests/animation/motionweave_api.cpp` to invoke the three-argument `MotionWeave::update` with a persistent 0-worker scheduler, assert exactly one eligible actor, check that missing optional Inputs/WorkHint/Budget components do not increase `World::poolCount()`, and verify a valid pose.
- **Public API/ABI and persistence:** unchanged. MotionWeave scheduler now uses the existing public const entity accessor; no genre modules, renderer, VIVID morph ABI, save or network schema touched.
- **Tests actually executed after fix:** **NOT RUN** by this agent. GitHub source diff/inspection verified replacement of the four private API uses and presence of the new test; native Xcode compilation and CI still require confirmation.
- **Risks/limitations:** Additional latent compilation failures in other deferred MotionWeave tests may emerge after this first error. Do not suppress compiler diagnostics globally or weaken `World` encapsulation. Original MotionWeave feature branch needs this fix separately upstream after its owner coordinates cherry-pick.
- **Next exact step:** on user's Mac, fetch VIVID HEAD and build both formerly missing targets **before** running CTest. If either build fails, capture the first compiler error and do not run CTest on stale executables. After success, run six VIVID tests plus `motionweave_api`, then full MotionWeave standalone tests as a separate compatibility gate.
- **Mac commands (NOT RUN in this stage):**
  ```bash
  cd ~/Projects/AIVERSE
  git fetch origin feature/vivid-digital-humans
  git -C ../AIVERSE-vivid switch --detach origin/feature/vivid-digital-humans
  cmake -S ../AIVERSE-vivid -B ../AIVERSE-vivid/build/vivid -DAIVERSE_BUILD_TESTS=ON
  cmake --build ../AIVERSE-vivid/build/vivid --target aiverse_vivid_motionweave_tests aiverse_motionweave_api_tests -j 4 &&
    ctest --test-dir ../AIVERSE-vivid/build/vivid --output-on-failure -R '^(vivid_|motionweave_api$)'
  ```

## macOS native validation — 2026-11-19 — 7/7 integration tests

- **Verified branch state at request:** `feature/vivid-digital-humans` head `5013fe6d96796abaae4f0596d9129dac33f33729`. The user's excerpt does not repeat a local `git rev-parse HEAD`; this is the latest GitHub state given to the user before their run, and the exact tested local SHA should be checked when running subsequent commands.
- **User-executed build:** `aiverse_engine` and `aiverse_models` targets built; `aiverse_motionweave_api_tests` compiled and linked successfully. No warnings/errors shown in supplied output.
- **User-executed CTest**, `/Users/aleksandrfeer/Projects/AIVERSE-vivid/build/vivid`: `motionweave_api` PASS (1.03s), `vivid_face` PASS (0.14s), `vivid_sparse_morph` PASS (0.15s), `vivid_shape_binding` PASS (0.15s), `vivid_gpu_contract` PASS (0.16s), `vivid_motionweave_bridge` PASS (0.79s), `vivid_morph_binding` PASS (0.15s). **7/7 tests passed, 0 failed, 2.59 seconds**. These are the first reported passes of the integrated MotionWeave bridge, resolving Stage 4C's previous `World::poolIfExists` compilation failure.
- **Native/CI status:** Real macOS execution reported by the user. Not rerun by this agent and not a verified GitHub Actions CI run.
- **Scope:** all seven selected CPU and fake-RHI contract tests passed; this is **not** a native Metal shader compilation/readback result or proof of the full MotionWeave test suite, renderer/skin integration, Vulkan/GL support, or visual correctness.
- **Next action:** add an isolated macOS Metal shader parity CTest running the real `kVividMorphMSL` kernel against 160 named morph targets; then move to a separate gfx::Device integration test. Keep build status separate for each new commit.

## Stage 4D — 2026-11-19 — real Apple Metal shader/CSR CPU parity fixture

- **Branch:** `feature/vivid-digital-humans`. This stage starts after the user's successful 7/7 VIVID + MotionWeave native tests on the pre-test branch HEAD `5013fe6d96796abaae4f0596d9129dac33f33729`; validation record commit `b27728c6cd01029b7d10570eb947a9ce9a5fe14f`.
- **Implementation commits / changed paths:**
  - `ba6bfcdbbe5a95ab2da279adcfb83804d4b618f6` — new `tests/vivid/metal_gpu.mm` runtime MSL kernel compilation, GPU buffers and 3 parity passes with 160 morph targets / 137 vertices.
  - `ff7eaa56292b91d79333a2c30e31a38e82435a4e` — add required standard C++ includes to Objective-C++ fixture.
  - `a296aaf3b1d3992873d2bb49612ca0dc44ae84f5` — CMake macOS-only `aiverse_vivid_metal_gpu_tests` and `vivid_metal_gpu` registration, Metal/Foundation frameworks, Objective-C++ and ARC, SKIP_RETURN_CODE=77 when no Metal device.
  - `c35b6a576900b008d0f225d241aa164b7c9ea210` — `docs/vivid.md` stage 4D design/status.
- **Architecture / behavior:** The direct macOS Metal test compiles the unchanged `kVividMorphMSL` from `engine/character/vivid/VividComputeShaders.h`, constructs `SparseMorphLibrary` CSR with three sparse deltas per target, creates native Metal shared buffers, dispatches 64 threads per group and compares GPU bytes after `waitUntilCompleted` against the CPU `evaluateGather` oracle. 137 vertices forces tail-thread bounds checking. The three passes verify normalised directions, raw directions and zero weights. All vertex positions, normals and tangents require max absolute error ≤ 5e-4; nonfinite values/pipeline errors fail.
- **Public API, persistence, compatibility:** no changes to VIVID core ABI, MotionWeave, Renderer3D, gfx::Device, model asset layout, ECS components, save/network formats. This is an isolated opt-in test target on macOS; other platforms do not compile Objective-C++ or link Metal.
- **Tests actually executed in this stage:** **NOT RUN**. No macOS Metal device is attached to this connected-GitHub coding session. Previous 7/7 user-reported CTests are real but predate this stage; **do not** report `vivid_metal_gpu` as passing. Native GPU test binary and MSL compilation remain unverified.
- **CI:** NOT RUN / not checked for this new commit.
- **Known risks:** Direct MTLDevice dispatch tests the shader and packed ABI independently of the RHI; it does not exercise `GpuMorphDeformer` inside `gfx::Device` begin/endFrame or readBuffer. Host GPU availability/compilers vary; the test allows a skip on machines without a Metal device and fails for genuine shader/command errors. Any Xcode compile warning/error must be fixed at the first primary diagnostic; do not silence warnings globally.
- **Next exact step:** macOS owner should fetch this VIVID branch, configure, build `aiverse_vivid_metal_gpu_tests` and run `ctest -R '^(vivid_|motionweave_api$)'`. Expect **8 tests registered** (7 previous + native Metal parity). If the Metal GPU test passes, add a separate real `gfx::Device` Metal dispatch/readback fixture and then integrate output into model render/skinning, while reusing MotionWeave's pose ownership. Append a new handoff recording actual Mac results.
- **Exact commands (NOT RUN here):**
  ```sh
  cd ~/Projects/AIVERSE
  git fetch origin feature/vivid-digital-humans
  git -C ../AIVERSE-vivid switch --detach origin/feature/vivid-digital-humans
  cmake -S ../AIVERSE-vivid -B ../AIVERSE-vivid/build/vivid -DAIVERSE_BUILD_TESTS=ON
  cmake --build ../AIVERSE-vivid/build/vivid --target aiverse_vivid_metal_gpu_tests aiverse_vivid_gpu_contract_tests aiverse_vivid_motionweave_tests aiverse_motionweave_api_tests -j 4
  ctest --test-dir ../AIVERSE-vivid/build/vivid --output-on-failure -R '^(vivid_|motionweave_api$)'
  ```

### Stage 4D build compatibility addendum — Objective-C++20

- **Follow-up code commit:** `ecefa8a6cb285e8a3c32a08807d5e36939f7eaf6` (`engine/character/vivid/CMakeLists.txt`): explicitly sets `OBJCXX_STANDARD=20`, `OBJCXX_STANDARD_REQUIRED=ON`, `OBJCXX_EXTENSIONS=OFF` for `aiverse_vivid_metal_gpu_tests`, because the test's `.mm` file includes VIVID C++20 `std::span` headers and a CXX-only compile feature is not sufficient to guarantee Objective-C++ language mode. This does not change any other target.
- **Tests run on this follow-up:** NOT RUN. The real Metal GPU test still requires the user's Xcode/macOS host. Treat Stage 4D as unverified until that native build/CTest is reported.

## Stage 4E — 2026-11-19 — fix macOS Objective-C++ CMake generation

- **Branch:** `feature/vivid-digital-humans`; prior HEAD `2114f5e7293fa78d287e5cf1a2e539ff50c32c6d`.
- **Mac repro supplied by user:** `git fetch` and detached checkout to `2114f5e72` succeeded; macOS CMake recognized AppleClang 17.0.0 Objective-C++ and completed configure, but failed *generate* repeatedly: `CMake Error: Error required internal CMake variable not set ... CMAKE_OBJCXX_COMPILE_OBJECT`. The failure is not a Metal shader compile error or a test failure. No fresh 8-test run was executed in this log.
- **Root cause:** `enable_language(OBJCXX)` was incorrectly called inside `engine/character/vivid/CMakeLists.txt`; CMake's language initialization must occur at the highest common directory of affected targets. The generated compile rule was not available throughout the root's target graph. `OBJCXX_STANDARD=20` alone cannot initialize the language.
- **Implementation commits:**
  - `6f9e6fbd6885e7fd39fa195b62cc1e87c3225c6b` — `CMakeLists.txt`: after platform detection and `AIVERSE_BUILD_TESTS` option, conditionally call `enable_language(OBJCXX)` for macOS test configurations, before declaring build targets or adding the VIVID subdirectory.
  - `09f389a293bc1d223824afaad1f63b116998f18d` — `engine/character/vivid/CMakeLists.txt`: remove the nested `enable_language(OBJCXX)` call. Retain the VIVID-only Metal GPU test target, `OBJCXX_STANDARD 20`, ARC and Metal/Foundation frameworks.
- **Verification actually performed:** remote GitHub file inspection confirmed **one** `enable_language(OBJCXX)` at root line 45, after the tests option at line 39 and before VIVID `add_subdirectory` at line 524; the VIVID subdirectory has no language-enabling call and retains its macOS-only test definition. **Native CMake generation, compilation, CTest and Metal readback NOT RUN after this fix** by the assistant; no Mac build runner is available through GitHub. User's previous 7/7 CPU and MotionWeave test result predates Stage 4D and is unaffected in source.
- **Scope/compatibility:** CMake-only change; no animation, renderer, shader, VIVID public API, save/network schema or RHI ABI modifications; macOS tests OFF and non-macOS targets should not enable Objective-C++ through VIVID.
- **Known risks:** an existing CMake build directory may retain inconsistent state from the failed generation; retry same directory first, and if the generator still fails, configure into a **fresh** directory instead of patching generated CMake variables. The real `metal_gpu.mm` test has never compiled/run and could expose a separate Clang/Metal diagnostic next.
- **Next exact step / owner Mac commands (NOT RUN by assistant):**
  ```sh
  cd ~/Projects/AIVERSE
  git fetch origin feature/vivid-digital-humans
  git -C ../AIVERSE-vivid switch --detach origin/feature/vivid-digital-humans
  cmake -S ../AIVERSE-vivid -B ../AIVERSE-vivid/build/vivid -DAIVERSE_BUILD_TESTS=ON &&
    cmake --build ../AIVERSE-vivid/build/vivid --target aiverse_vivid_metal_gpu_tests -j 4 &&
    ctest --test-dir ../AIVERSE-vivid/build/vivid --output-on-failure -R '^vivid_metal_gpu$'
  ```
  If stale generation persists: substitute a *new* `-B ../AIVERSE-vivid/build/vivid-metal-clean` for both configure/build/CTest paths. Run the 8-test `^(vivid_|motionweave_api$)` selection only after building the corresponding targets. Append exact results to this handoff.

## User macOS validation — 2026-11-19 — real Metal kernel PASS

- **Revision tested:** `424e8382144511c1b453ce9d3234cc813734356c` confirmed explicitly by the user's `git switch --detach` output from `origin/feature/vivid-digital-humans`.
- **Actual user-provided evidence:** CMake configured in 5.7 seconds and generated in 1.1 seconds; `aiverse_vivid_gpu` and Objective-C++ `aiverse_vivid_metal_gpu_tests` compiled and linked. `ctest -R '^vivid_metal_gpu$'`: `vivid_metal_gpu` **Passed** (0.66 s), 1/1 tests passed, 0 failed, total real time **0.68 s**. No compiler note/warning/error diagnostics appeared in the supplied output.
- **What this verifies:** native Apple Metal compiler and execution accepted `kVividMorphMSL`, with 160 sparse targets, 137 vertices, three dispatch modes and readback comparison to `SparseMorphLibrary::evaluateGather`. This was executed by the user on macOS, not independently by this agent.
- **Still NOT RUN / NOT VERIFIED:** real `gfx::Device` RHI-backed dispatch/readback; gfx vertex stream + MotionWeave skinning; visual render, shadows, memory/latency benchmark, Vulkan/GL; full merged 8-test suite at revision `424e838` (previous VIVID+MotionWeave 7/7 were separately confirmed on earlier commit).
- **API/persistence:** no production code changes from this validation checkpoint; `main` and MotionWeave source branch untouched.
- **Next exact step:** build isolated macOS real `gfx::Device` lifecycle/dispatch/readback parity test on new source commit, record exact results and warnings. Do not treat direct native Metal parity as automatically proving RHI integration.

## Stage 4F — 2026-11-19 — real gfx::Device Metal RHI parity

- **Branch:** `feature/vivid-digital-humans`. Previous source revision `424e8382144511c1b453ce9d3234cc813734356c`; confirmation handoff commit `e98b74ce948e25b182a0900cbf93a9a79aef8a3a`.
- **User's actual validation at previous stage:** `vivid_metal_gpu` **1/1 PASS**, total 0.68 s on a real Mac GPU; CMake configure/generate and OBJCXX compilation completed. This is the native kernel test from Stage 4D, **not** the RHI test added in this stage.
- **Implementation SHAs / changed paths**:
  - `88f035bfee3b504c7d61e4d11d7d89950b090824` — new `tests/vivid/metal_rhi.mm`, uses the real AIVERSE Metal `gfx::Device`, `GpuMorphDeformer`, four GPU frames, 160 targets, 79 vertices, CPU gather comparison.
  - `2991bd678ffac4a987a430bd9bfcb08f049a6166` — `engine/character/vivid/CMakeLists.txt` registers macOS-only `aiverse_vivid_metal_rhi_tests` / CTest `vivid_metal_rhi`, compiles the existing `engine/gfx/metal/GfxMetal.mm` into a standalone native test binary, links `AIVERSE::vivid_gpu`, ARC/Objective-C++20, Metal/QuartzCore/Foundation.
  - `ed4569d73d0e6ce23ebabf3fe33982c0d8559de7` — `docs/vivid.md` Stage 4F overview, test constraints and scope.
- **Runtime behavior tested by authored fixture (not yet executed):** create headless `CAMetalLayer` and `gfx::createMetalDevice`; initialize 6 VIVID storage buffers once; call `beginFrame` / `GpuMorphDeformer::dispatch` / `endFrame` / `download`; read back after each completed frame and compare all position/normal/tangent components to `SparseMorphLibrary::evaluateGather`. Frames vary weights/normalization to catch buffer updates, synchronization and stale output. Per-component absolute tolerance ≤ 5e-4. Release VIVID before gfx device teardown. Empty optional shader-archive platform hook is scoped to this executable, not game runtime.
- **API/save/network:** no production public ABI, `engine/gfx/Gfx.h`, MotionWeave, simulation or persistence changes. New test source/link definitions only.
- **Tests actually executed for this stage:** **NOT RUN**. The user has **not** compiled the new RHI test. Native Metal compiler, headless CAMetalLayer behavior, Metal backend linkage, output parity and resource cleanup remain unknown. GitHub structure verified; do not claim 9/9 or `vivid_metal_rhi` Passed until Mac reports it. No CI result.
- **Potential blockers:** headless `nextDrawable` behavior varies with OS, though compute command buffers should not depend on presentation. Linking `GfxMetal.mm` standalone may uncover additional platform dependencies. Check the first real build error instead of editing shared graphics backend unnecessarily; if the device cannot supply a drawable, consider an explicit graphics test harness or a small backend factory allowing an offscreen GPU frame instead of pretending the test passed.
- **Next exact action / Mac commands (NOT RUN by agent):**
  ```bash
  cd ~/Projects/AIVERSE
  git fetch origin feature/vivid-digital-humans
  git -C ../AIVERSE-vivid switch --detach origin/feature/vivid-digital-humans
  cmake -S ../AIVERSE-vivid -B ../AIVERSE-vivid/build/vivid -DAIVERSE_BUILD_TESTS=ON
  cmake --build ../AIVERSE-vivid/build/vivid --target aiverse_vivid_metal_rhi_tests -j 4
  ctest --test-dir ../AIVERSE-vivid/build/vivid --output-on-failure -R '^vivid_metal_rhi$'
  ```
- **After real RHI parity:** integrate opt-in extended facial morph stream with MotionWeave skeleton and common `Renderer3D` material/shadow passes using stable buffer ownership. The existing `render::Model` eight-target path must remain unchanged for non-VIVID assets until visual parity and runtime benchmarks prove the transition. Record numerical/screenshot/real GPU measurements in the handoff after each bounded stage.

## User-verified macOS Metal RHI — 2026-11-19 — Stage 4F accepted

- **Exact checked source SHA:** `22b60c4714c257e9714bcfd48943f4f62f1fb9d5`; user explicitly reported detached worktree HEAD `22b60c471`.
- **User-executed build:** `cmake` configured in 5.3 s, generated in 1.2 s; `aiverse_vivid_gpu` and real `aiverse_vivid_metal_rhi_tests` (including Objective-C++ `tests/vivid/metal_rhi.mm` and existing `GfxMetal.mm`) compiled and linked successfully.
- **User-executed test:** `ctest -R '^vivid_metal_rhi$'` → `vivid_metal_rhi` **Passed** (1.17 s), **1/1 passed, 0 failed**, total real 1.18 s. No errors, notes or warnings in supplied output.
- **Meaning:** on the tested Apple hardware, the existing AIVERSE Metal `gfx::Device` path successfully created GPU resources, encoded multiple VIVID CSR morph compute frames and read back results within the test's 5e-4 CPU `SparseMorphLibrary::evaluateGather` tolerance. This is stronger than the direct-Metal-kernel test (Stage 4D); both are now user-confirmed PASS at their respective SHA.
- **Verification provenance:** Mac test executed by user; this agent is recording the reported terminal output, not claiming an independent device run. No full 9-test CTest run on this SHA, graphics mesh render, shadows, MotionWeave skinning integration, performance benchmarks or Vulkan/GL verification yet.
- **Compatibility:** this checkpoint changes documentation only; no model ABI or genre-specific code was modified.
- **Next exact step:** inspect the actual `render::Vertex` / `Model` / `Renderer3D` layout and GPU skinning order. Build an opt-in VIVID render-stream integration adapter preserving the original model path, with explicit buffer ownership and a regression fixture. Only after native build + screenshots may it be enabled for characters.

## Stage 5A — 2026-11-19 — GPU ModelVertex stream repacking

- **Branch:** `feature/vivid-digital-humans`. Pre-stage confirmed user native Metal RHI source `22b60c4714c257e9714bcfd48943f4f62f1fb9d5` passed 1/1, 1.18s total. Validation-only commit `3ec4b5e5a757de9abb6390a5ed3cc9a9b2f443f8` records it. New Stage 5A commits are **not** covered by that pass.
- **Implementation commits and exact paths:**
  - `ccd59be309192a2cec734258bed62be714472aab` — NEW `engine/character/vivid/GpuMorphMeshStream.h`: model-compatible 60B GPU output, ABI assertions, public opt-in class, no direct Renderer3D ownership.
  - `b2c0bcda150d49107997223063528054f5ea933e` — NEW `engine/character/vivid/GpuMorphMeshShaders.h`: paired Metal/GLSL compute kernels that preserve 15 packed 32-bit words from `ModelVertex`, replace only morphed position/normal/tangent.xyz.
  - `f24365d7d4cd646498f4e60e3745cd517ba13c20` — NEW `engine/character/vivid/GpuMorphMeshStream.cpp`: opt-in separate compute pipeline with resource validation, 512MiB budget, dispatch/download/release.
  - `c6750f35c1e8c0633cdd37ecc36e9de377341b6a` — `engine/character/vivid/CMakeLists.txt`: `AIVERSE::vivid_mesh` static library and link into the real Metal RHI test target.
  - `bc2ab74663ff7898b97addd64c49e868e45a5d48` — `tests/vivid/metal_rhi.mm`: four-frame actual Metal RHI pass now includes mesh conversion and CPU parity, asserts bitwise preservation of UV/colour/tangent handedness/joints/weights.
  - `6895ba3a67f2651e37eaccd6b552d5c32e0217d9` — correct a malformed newline character literal in test diagnostics before compilation.
  - `a696eceef4e450555bd950fce5c2d02487b7f6a2` — `docs/vivid.md` technical API and compatibility limitations.
- **Functional path:** VIVID sparse 48-byte `GpuMorphVertex` → `GpuMorphMeshStream` GPU repack to full 60-byte `render::ModelVertex`. Same model vertex indices are preserved. Copy all original 15 × uint32 words per vertex, replace offsets 0–2 (pos), 3–5 (normal), 8–10 (tangent xyz), and preserve UV, tangent sign, colour and skeletal joint influences bitwise. Model shaders already deform before skin. No duplicate skeletal animation: MotionWeave still owns pose, retargeting, IK, worker scheduler and bone palettes.
- **Public API and compatibility:** new `GpuMorphMeshStream::initialize/dispatch/vertexBuffer/download/release` and `AIVERSE::vivid_mesh` only; no `ModelVertex`, `ModelDraw`, `Renderer3D`, `gfx::Device` or MotionWeave ABI changes. No saved game/network format changes. Vertex output is a `gfx::BufferType::Storage` allocation containing valid 60B words; renderer has not yet been changed to use it.
- **Tests actually run:** **NOT RUN** for new Stage 5A code. GitHub source fetch and layout review only. The modified `vivid_metal_rhi` fixture was written and statically inspected, but Mac compilation, actual Metal shader compilation, runtime parity, full CTest, and renderer screenshots remain outstanding. CI NOT RUN. Prior pre-stage user-confirmed Metal `vivid_metal_rhi` 1/1 PASS cannot be used as Stage 5A test evidence.
- **Risks:** GPU buffer type Storage-as-vertex use must be checked on each RHI, especially Vulkan/GL. Existing `render::ModelDraw` binds `model->vb`; this stage does NOT change normal draw behavior. Renderer colour/shadows/geometry pool/ICB/LOD must explicitly opt in and preserve full indexed stream offsets and device resource retirement. A fixed 512MiB maximum limits this bridge but does not solve all dynamic streaming or cross-frame draw hazards.
- **Next exact step:** user Mac: fetch current VIVID, configure, build `aiverse_vivid_metal_rhi_tests` and `aiverse_vivid_metal_gpu_tests`, run `vivid_metal_rhi` + `vivid_metal_gpu`, record compiler warnings and numerical parity. Then implement an opt-in per-ModelDraw vertex-stream override in Renderer3D (with direct draw/colour/shadow parity first, batching/ICB initially disabled and tested separately) rather than bypassing legacy UV/bone attributes. Append handoff for every implementation stage.
- **Commands for owner (not run by assistant):**
  ```sh
  cd ~/Projects/AIVERSE
  git fetch origin feature/vivid-digital-humans
  git -C ../AIVERSE-vivid switch --detach origin/feature/vivid-digital-humans
  cmake -S ../AIVERSE-vivid -B ../AIVERSE-vivid/build/vivid -DAIVERSE_BUILD_TESTS=ON
  cmake --build ../AIVERSE-vivid/build/vivid --target aiverse_vivid_metal_rhi_tests aiverse_vivid_metal_gpu_tests -j 4
  ctest --test-dir ../AIVERSE-vivid/build/vivid --output-on-failure -R '^vivid_metal_(rhi|gpu)$'
  ```

## Stage 5B — 2026-11-19 — reserved MSL identifier and actionable failure diagnostics

- **Branch:** `feature/vivid-digital-humans`; previous Stage 5A HEAD reported in user verification: `208b4d37c06f49ba774c14dfc9a3a96fb57bab25`.
- **Actual user-run macOS evidence before fix:** `aiverse_vivid_mesh`, `aiverse_vivid_metal_rhi_tests` and `aiverse_vivid_metal_gpu_tests` compiled and linked. `vivid_metal_gpu` **Passed** (0.12 s); `vivid_metal_rhi` **Failed** (2.03 s) with `VIVID RHI 60-byte mesh stream initialization failed`, prior to mesh-conversion dispatch. CTest: **1/2 passed, 1 failed, 2.16 s**; output did not contain compiler warnings or notes. The old Metal deformer path was previously independently confirmed, but the new 60B packing path is **NOT** verified.
- **Root-cause analysis:** `GpuMorphMeshStream::initialize` can fail on no compute, invalid count/budget, `gfx::Device::createCompute`, or GPU storage allocation. Valid small test dimensions and a previously working Metal compute backend narrow this to new shader/pipeline compilation as the leading hypothesis. Source inspection shows MSL kernel parameter `uint vertex [[thread_position_in_grid]]`; `vertex` is the Metal shader-stage keyword. This inference has not yet been confirmed with the native Metal compiler diagnostic.
- **Implementation commits / files:**
  - `af74fb9c23139de9d20d8cfb506dbc83387754f3` — `engine/character/vivid/GpuMorphMeshShaders.h`: rename Metal compute parameter to `vertexIndex` and update every use, leaving GLSL source untouched.
  - `3488f98b5e444976786315e98e6b0eec4436b446` — `tests/vivid/metal_rhi.mm`: on failed stream initialization, include `meshStream.lastError()` and `gfx::Device::lastError()` in the thrown error. Future Metal compiler/allocator failures will be visible instead of generic.
  - `f434e01bdc349528a7337cdd09942c549a0b3a17` — `docs/vivid.md` Stage 5B source explanation and actual failed test status.
- **Public API / ABI / persistence compatibility:** neither `ModelVertex` 60B nor `GpuMorphVertex` 48B layout changed; no production `GpuMorphMeshStream` C++ signatures, render contracts, CMake targets, MotionWeave, simulation, save/network changes. Only the internal MSL variable naming and test diagnostics changed.
- **Tests actually executed AFTER fix:** **NOT RUN**. GitHub fetched modified source for verification; no remote macOS execution tool is available. Do not claim the user-visible `vivid_metal_rhi` is passing before rerun.
- **Known risks / next step:** if initialization still fails, the new error path should reveal the exact MSL compile error or allocation error; fix that primary diagnostic first. If it compiles, the test's four-frame output comparison will validate that 60B ModelVertex UV, colours, tangent handedness and joint data remain intact while positions, normals and tangents are morphed.
- **Mac commands (unrun by assistant):**
  ```bash
  cd ~/Projects/AIVERSE
  git fetch origin feature/vivid-digital-humans
  git -C ../AIVERSE-vivid switch --detach origin/feature/vivid-digital-humans
  cmake -S ../AIVERSE-vivid -B ../AIVERSE-vivid/build/vivid -DAIVERSE_BUILD_TESTS=ON &&
    cmake --build ../AIVERSE-vivid/build/vivid --target aiverse_vivid_metal_rhi_tests aiverse_vivid_metal_gpu_tests -j 4 &&
    ctest --test-dir ../AIVERSE-vivid/build/vivid --output-on-failure -R '^vivid_metal_(rhi|gpu)$'
  ```

## Stage 5B validation checkpoint — 2026-11-19 — 60-byte Metal vertex stream PASS

- **Source revision under test:** user followed the latest `feature/vivid-digital-humans` fetch/checkout after Stage 5B (`60f95a95e32dda1886833cbce374f7238e0481be`). The supplied terminal excerpt does not independently repeat `git rev-parse HEAD`; attribute this to the latest user-instructed branch revision, not to future commits.
- **Mac build evidence supplied by user:** `aiverse_models`, `aiverse_vivid_mesh`, `aiverse_vivid_metal_rhi_tests`, and `aiverse_vivid_metal_gpu_tests` compiled/linked. No compiler `error:`, `warning:` or `note:` shown.
- **Native GPU test results (user executed):** `vivid_metal_gpu` PASS (0.94 s); `vivid_metal_rhi` PASS (0.75 s); **2/2 Passed, 0 Failed; total real 1.71 s.** The extended real RHI test checks the packed 60-byte `ModelVertex` stream for four GPU frames, CPU numeric parity and bitwise preservation of UV, colour, tangent sign, joint indices and skin weights.
- **Limitations:** no full 9-test suite executed after this shader fix, no Renderer3D draw/visual, no shadow and LOD parity, and no measured FPS or GPU time. Apple Metal only; Vulkan/GL and MotionWeave full suite remain separate gates.
- **Next exact step:** implement a strictly opt-in `ModelDraw` vertex-buffer override (same 60-byte ModelVertex format) with explicit capability, lifetime and fallback validation, routing both colour and shadow direct draws while bypassing unsafe shared geometry pool/MDI/ICB paths. Add a targeted regression test and test on Mac before enabling it for characters.

## Stage 5C — 2026-11-19 — Renderer3D opt-in 60B dynamic vertex stream (first source integration)

- **Branch:** `feature/vivid-digital-humans` based on merged MotionWeave snapshot. User Mac validation at previous Stage 5B: `vivid_metal_gpu` 0.94s **PASS** and extended `vivid_metal_rhi` 0.75s **PASS**, 2/2 pass, total real **1.71s**. Result handoff commit `8c25570c224b3a508f334191e2e51fc3cf0a7262`.
- **Code commits & changed files:**
  - `275f8f6952551ec8ccce01a0083593231a684fd5`: new `engine/render/ModelVertexStreamOverride.h`, model-relative 60-byte GPU vertex stream acceptance status policy and checks, no save changes.
  - `bd634b3d87c44780aac505dd5aaec03fa633b5b8`: `engine/render/Renderer3D.h`, opt-in `ModelDraw.vertexOverride` and `vertexOverrideCount` plus `ModelItem.vertexOverride`. Both default to off.
  - `02efa1f5120284093fc776c46fddb8e9095c8d4a`: `engine/render/Renderer3DModels.cpp`, opt-in MetaHuman-like stream submission using original 60B vertex layout for direct model draws, disabling legacy morph pass, cache/LOD/batching/MDI/ICB and geometry shortcuts for custom-stream items, bypassing stale bind-pose bounds.
  - `f2e0d87e9422da54b75a434e25182b4f1b0f4fa7`: new `tests/vivid/render_vertex_override.cpp`, regression fixture for model sizes, legacy default/off, rejected shared geometry/base offsets.
  - `84bb150a61456ddb29020334995eeb5897958c77`: `engine/character/vivid/CMakeLists.txt`, new `aiverse_vivid_vertex_override_tests` / CTest `vivid_vertex_override`.
  - `3c52771a46a09fab40e2e725969439f358b23dc3`: `Renderer3DModels.cpp` additionally rejects invalid override with diagnostic, retires an obsolete persistent GPU scene record on transition, conservatively disables reflection bounds culling for deformed stream.
  - `c7dea56172e72b2a99b578a7757f7e88a9b55cbb`: `docs/vivid.md`, API/ownership/integration restrictions and next steps.
- **Functional contract:** `GpuMorphDeformer` computes 48B position/normal/tangent → `GpuMorphMeshStream` packs full 60B model-relative vertices → `ModelDraw.vertexOverride` is passed by the host, with matching count, a matching device, and lifetime across the queued colour/shadow draws. `Renderer3D::drawModel` validates compatibility and requires Metal, dedicated (not pooled) dynamic model, no baked animation. Unsupported/invalid overrides reject instead of silently showing original mesh. `drawModelBatch` chooses override VB for all direct model phases, keeping existing material attributes and MotionWeave skin palettes. Normal ModelDraw path remains unchanged by default.
- **No new animation or simulation:** MotionWeave is the sole skeleton pose/IK/retarget/LOD animation policy owner; VIVID only supplies morph vertices and an opt-in presentation render stream. No game genre modules, ECS simulation checksum, saves or network ABI touched. `ModelDraw` source ABI adds two trailing fields; rebuilding all native C++ clients is required. Renderer3D internal item layout changes, no serialized format.
- **Acceptance gates:** invalid vertex counts, model shared geometry page, model GPU base vertex/first index, specialized geometry clusters, baked/static usage and non-Metal are blocked. Morphed streams use no old shader morph to prevent double application. CPU model batching, material/shadow composite batches, LOD fade, GPU model authorship, MDI, CPU instancing and GPU-encoded ICB are bypassed for streamed draws. Source model bounds cannot account for facial morph offsets, so conservative culling is disabled for these items pending correct dynamic bounds. Persistent geometry record is retired when switching from legacy source.
- **Tests actually executed for these new code commits:** **NOT RUN**. This agent inspected GitHub source and authored a native CTest, but there is no complete local AIVERSE checkout or Mac GPU execution. The user-confirmed previous 2/2 Metal RHI tests were performed BEFORE renderer code changes and do not demonstrate colour/shadow visual draw correctness. No CI run, full CTest or screenshot verified.
- **Risks/blockers:** RHI storage buffer binding as a vertex stream is known supported by inspected Metal `setVertexBuffer` code, but must be visually checked; no validation of handle-device ownership or in-flight resource lifetime is enforced by gfx::Device; the caller must keep the GPU stream alive to completion. Model vertex-range checks depend on original primitive firstVertex/vertexCount metadata (CPU vertices may be freed). The opt-in path deliberately trades performance for correctness and must not be enabled by default. The renderer's other shader permutations, outlines and render targets need real visual QA; no guarantee of consistent scene bounds/presentation without those tests. Ensure newly merged MotionWeave code still compiles; do not mutate its source branch.
- **Next exact step:** native macOS CMake configure + build `aiverse_vivid_vertex_override_tests` and `aiverse_vivid_metal_rhi_tests`, run `^vivid_(vertex_override|metal_rhi|metal_gpu)$`. Then a targeted minimal real Renderer3D drawable fixture with colour+shadow capture and skeletal palette, before any broader shader/batching enablement. Capture warnings/notes and errors and add another handoff. Re-run all `vivid_` plus `motionweave_api` and selected renderer/persistent scene tests before merge.
- **Mac commands (NOT RUN here):**
  ```sh
  cd ~/Projects/AIVERSE
  git fetch origin feature/vivid-digital-humans
  git -C ../AIVERSE-vivid switch --detach origin/feature/vivid-digital-humans
  cmake -S ../AIVERSE-vivid -B ../AIVERSE-vivid/build/vivid -DAIVERSE_BUILD_TESTS=ON
  cmake --build ../AIVERSE-vivid/build/vivid --target aiverse_vivid_vertex_override_tests aiverse_vivid_metal_rhi_tests aiverse_vivid_metal_gpu_tests -j 4
  ctest --test-dir ../AIVERSE-vivid/build/vivid --output-on-failure -R '^vivid_(vertex_override|metal_rhi|metal_gpu)$'
  ```

## Stage 5C macOS verification — 2026-11-19 — 3/3 Passed

- **Source revision expected:** `f21777a6b79f8c3087d1a9260b499eebc56f3f0f` (`feature/vivid-digital-humans`, previous advertised build command); the provided excerpt does not independently echo the detached HEAD SHA. Do not attribute this result to a later code commit.
- **User Mac build evidence:** `aiverse_engine`, `aiverse_vivid`, `aiverse_vivid_gpu`, `aiverse_models`, `aiverse_vivid_mesh` and `aiverse_vivid_metal_gpu_tests` built, `aiverse_vivid_metal_rhi_tests` linked. Three CTest fixtures ran: `vivid_metal_gpu` PASS (0.96 s), `vivid_metal_rhi` PASS (0.46 s), `vivid_vertex_override` PASS (0.31 s). **3/3 passed, 0 failed, 1.75 s real time**. The source excerpt contains no warnings or notes.
- **Evidence scope:** numeric GPU-vs-CPU and 60-byte mesh stream parity plus the headless renderer override policy are verified at this revision. **No actual Renderer3D colour/shadow draw, capture, image comparison, skinning-on-camera, FPS benchmark, or full test suite was run in the provided excerpt.** The `vivid_vertex_override` policy test does not compile/link the whole `Renderer3DModels.cpp` render path, so independent renderer compilation remains required.
- **Verifier:** native tests executed by user; this agent records user output, not an independent Mac run.
- **Next step:** construct a narrow real-Metal renderer test for a moving/deformed model and capture actual colour/depth/shadow results using existing Renderer3D and gfx device. Preserve default-off stream override, MotionWeave skeletal ownership and per-stage handoff. Run full renderer build and targeted CTest before claiming visual integration.

## Stage 5D — 2026-11-19 — real Renderer3D visual preview authored

- **Branch:** `feature/vivid-digital-humans`; previous user's verified Stage 5C test selection 3/3 Passed (0 failed, 1.75s): `vivid_metal_gpu` 0.96s, `vivid_metal_rhi` 0.46s, `vivid_vertex_override` 0.31s. User log did not display an explicit SHA; corresponding source from prior suggested commands: `f21777a6b79f8c3087d1a9260b499eebc56f3f0f`. Recording commit: `96d5c6d6aecee4a663966da818b0e491c6880cfe`.
- **Implementation and docs commits**:
  - `431d3abb5d7a639c578ead79d14eaddae727715a`: new `tests/vivid/renderer_preview.cpp`, platform-driven *actual Renderer3D* Metal colour+shadow and 3 GUI frame captures. Uses procedural head/floor, VIVID 52-channel FaceController + named shape mapping into sparse morph targets, real `GpuMorphDeformer`, `GpuMorphMeshStream`, and **single MotionWeave presentation World/clip**, no copied IK or animation scheduling.
  - `6aadceb365866af6eb80aa2ee360611a8310c05e`: root `CMakeLists.txt`, opt-in standalone macOS GUI target `aiverse_vivid_renderer_preview` with `aiverse_runtime`, `AIVERSE::vivid_mesh`, `AIVERSE::MotionWeave` and existing zstd dependency. No automatic CTest because a real GUI drawable/capture loop is required.
  - `a32ad84e6f721d08ac761235238b6ad841d7f6e1`: `docs/vivid.md`, workflow, capture files, status and limitations.
- **What it is intended to exercise:** platform-owned Metal gfx::Device frame → VIVID GPU CSR morph → VIVID 60B `ModelVertex` packing → `ModelDraw.vertexOverride` → original `Renderer3D` indexed direct shadow+colour passes. The final capture also advances `MotionWeave::update` with an animation clip, using its output matrices as the sole skeletal/node-pose source. Baseline and active-morph image captures share a fixed MotionWeave pose so that pixel differences originate from facial deformation; a later capture validates visible graph-driven pose motion. Textures/materials are drawn by the original model renderer.
- **How it validates:** `Renderer3D::profile().draws[0]` and `draws[1]` must be nonzero after warmup, `Renderer3D::modelError()` empty, 3 actual 960×640 RGBA captures must complete, and two staged pixel differences must exceed conservative thresholds. Writes PPM files to `/tmp/aiverse-vivid-{baseline,morph,motionweave}.ppm` unless `AIVERSE_VIVID_PREVIEW_PREFIX` overrides path. Diagnostic screenshot differences are **not** shader visual-equivalence golden images or proof of MetaHuman quality.
- **Deliberately disabled in the fixture:** geometry pooling, model LOD, model instancing and MDI; `ModelDraw` override remains default-off elsewhere and works only for dedicated dynamic Metal models. Any shader/renderer compile failure or headless screenshot issue must be fixed without relaxing production safety rules.
- **Actual tests performed for this new stage:** **NOT RUN**. The GitHub source and root CMake entries were authored/inspected only. The authoring environment cannot compile the full AIVERSE platform renderer or launch macOS GUI Metal. Prior 3/3 native tests cannot certify this preview program or real colour/shadow output. CI NOT RUN. No actual screenshots were produced.
- **ABI/save / dependencies:** no changes to production `gfx::Device`, `Renderer3D`, `MotionWeave`, `render::Model` ABI or saved/network schemas in this stage. Only a desktop diagnostic tool. Rendering uses the already integrated Stage 5C `ModelDraw` vertex override and inherited MotionWeave APIs.
- **Next exact step:** owner builds `aiverse_vivid_renderer_preview` on Mac and runs it in a graphical session. If C++ compilation fails, fix first diagnostic and commit another handoff. On a live GUI, inspect the printed shadow/colour counts and the three PPMs, ensure file captures differ in the intended face/head region and shadow silhouette, and record concrete screen/log evidence. After the preview passes, design proper in-flight buffer slotting and device-generation ownership for many dynamic character instances, then test model shadow/LOD and render performance before enabling default VIVID visual playback.
- **Mac commands (NOT RUN here):**
  ```sh
  cd ~/Projects/AIVERSE
  git fetch origin feature/vivid-digital-humans
  git -C ../AIVERSE-vivid switch --detach origin/feature/vivid-digital-humans
  cmake -S ../AIVERSE-vivid -B ../AIVERSE-vivid/build/vivid -DAIVERSE_BUILD_TESTS=ON
  cmake --build ../AIVERSE-vivid/build/vivid --target aiverse_vivid_renderer_preview -j 4
  AIVERSE_VIVID_PREVIEW_PREFIX=/tmp/aiverse-vivid \
    ../AIVERSE-vivid/build/vivid/aiverse_vivid_renderer_preview
  ```

### Stage 5D follow-up — explicit preview index bounds

- Code commit `d6898b98d747fece18416995353b8eae1b84ec6a` updates `tests/vivid/renderer_preview.cpp`: guard `model.vertices.size()` against the GPU uint32 index limit, iterate vertices with `std::size_t` and convert to `uint32_t` only after the bounds check. This keeps synthetic morph target indices compatible with `SparseMorphLibrary` on both 32- and 64-bit builds, without narrowing warnings or changing engine code.
- Tests after this fix: **NOT RUN**. Still need the user's real macOS GUI compile and colour/shadow/MotionWeave PPM captures before calling Stage 5D visually verified.

## Stage 5E — 2026-11-19 — fix procedural preview vertex ranges and asynchronous shader warmup

- **Feature branch:** `feature/vivid-digital-humans`. Previous published preview implementation HEAD `8b81646ca68d415650028682cf4abe885bc789f3`. `main` and `feature/universal-ecs-animation-pipeline` not modified.
- **Native Mac evidence actually reported by user BEFORE this fix:** `aiverse_vivid_renderer_preview` compiled/linked successfully, started with `VIVID render preview ready: 925 vertices, 2 GPU shapes`, and saved only `/tmp/aiverse-vivid-baseline.ppm`. Preview **FAILED** with `VIVID preview missing shadow/colour model submissions: 0/0` and `model vertex override: rejected invalid/pooled 60B stream`. Metal assets summary: `sourceMisses=11 pipelines=15 pipelineFailures=0`; it does not prove colour/shadow correctness. This is not a CTest failure: this is a GUI preview failure. No successful morphed or MotionWeave captures were reported.
- **Root cause confirmed by source:** `engine/render/Model.cpp::modelFromParts` supplied `Primitive.firstIndex/indexCount` but did not supply `Primitive.firstVertex/vertexCount`. The preview's 925 vertices were valid, but `modelVertexOverrideStatus` computed no ranges because every primitive had zero vertex count, correctly rejecting the override. Initial zero draw counters can also occur while Metal model shader/pipeline jobs warm asynchronously; the fixed-frame screenshot script treated that as fatal prematurely. The log alone does not prove whether shader warmup was the sole cause of the initial `0/0`.
- **Code commits and exact paths:**
  - `ce21f978f59cd9a2a7bf050f3746e5e68f11b5b5` — `engine/render/Model.cpp`: set `p.firstVertex=base` and `p.vertexCount=sv.size()` for each procedural part in `modelFromParts`, preserving model-relative indexing. This repairs asset metadata rather than bypassing renderer validation.
  - `ca38810949f44c71c97b58617410305ce807a68a` — `tests/vivid/render_vertex_override.cpp`: regression for two procedural parts with expected 0..2 and 3..5 ranges; requires `ModelVertexOverrideStatus::Ready` before/after releasing the CPU vertices. Old invalid/pooled policy checks remain.
  - `b9579965c8af1256eb51254464c77f911c9c1ef0` — `tests/vivid/renderer_preview.cpp`: explicit `modelVertexOverrideStatus` preflight and range log after upload; explicit shadow bounds; replace fixed frame 6/17/39 captures and instant `0/0` failure with a 3-stage capture state machine. It requires at least 4 consecutive frames of nonzero shadow AND colour model submissions, retains each stage until asynchronous `captureNextFrame` callback completes, waits for MotionWeave animated translation, and times out with diagnostics after 400 frames.
  - `a52acfae9dbce6e89d0f3f68b99ad38fbd17a42f` — `docs/vivid.md`: source cause and preview-specific constraints.
- **API/compatibility:** no new public CMake targets, `ModelDraw`, `gfx::Device`, MotionWeave animation API, saved/network schemas, GPU compute shader layout, renderer queue/batching or simulation changes. Procedural `Model` instances now have correct `Primitive.firstVertex/vertexCount`. This changes previously invalid metadata as intended; downstream model batching/LOD code should be built and tested on Mac.
- **Actual tests executed AFTER this stage:** **NOT RUN**. GitHub code/file checks only; no macOS CMake build, GPU test rerun, actual preview screenshots or full renderer test suite on these source changes. Previous user 3/3 Metal GPU/override CTests were on the pre-preview revision.
- **Known limitations / risks:** GUI GPU shader first-use may still need substantial warmup; waiting for 4 consecutive draw frames prevents blank screenshots but does not prove shadow pixels match morph silhouette. A stage can fail at the new 400-frame timeout, in which case report `stage`, draw counts, pending pipeline jobs and GPU errors. Large motions or artistically subtle morphs may require adjusting fixture parameters based on real captured images. Some backends may generate additional LOD/material batches; retain the post-upload range preflight. Buffer/device lifetime and cross-frame re-use remain explicit host responsibilities. MotionWeave remains the single owner of body pose/retargeting.
- **Next actionable step:** on macOS GUI host fetch latest VIVID, configure and build `aiverse_vivid_vertex_override_tests`, `aiverse_vivid_metal_rhi_tests`, `aiverse_vivid_renderer_preview`. Run the two CTests first; run the GUI preview; ensure three captures and meaningful pixel deltas, visually inspect shadow edge and deformation. Record exact results in new handoff before moving on to multi-character in-flight buffering or batching/LOD.
- **Commands to reproduce, NOT RUN by this agent:**
  ```sh
  cd ~/Projects/AIVERSE && git fetch origin feature/vivid-digital-humans
  git -C ../AIVERSE-vivid switch --detach origin/feature/vivid-digital-humans
  cmake -S ../AIVERSE-vivid -B ../AIVERSE-vivid/build/vivid -DAIVERSE_BUILD_TESTS=ON
  cmake --build ../AIVERSE-vivid/build/vivid --target aiverse_vivid_vertex_override_tests aiverse_vivid_metal_rhi_tests aiverse_vivid_renderer_preview -j 4
  ctest --test-dir ../AIVERSE-vivid/build/vivid --output-on-failure -R '^vivid_(vertex_override|metal_rhi)$'
  AIVERSE_VIVID_PREVIEW_PREFIX=/tmp/aiverse-vivid ../AIVERSE-vivid/build/vivid/aiverse_vivid_renderer_preview
  ```

## Stage 5E native macOS visual validation — 2026-11-19 — PASS

- **Source revision confirmed by user's fetch/checkout:** `53e5f0cb069dff8d4c173001947ffce3b4791a98` (`feature/vivid-digital-humans`), macOS Apple Silicon worktree at `/Users/aleksandrfeer/Projects/AIVERSE-vivid`. This run began in a different `AIVERSE-physis-terra-integration` terminal, but the commands explicitly fetched and built the *VIVID* detached worktree; its unrelated TerraWeave status/conflict listing does **not** establish any merge into VIVID.
- **Native configure and compilation executed by user:** macOS CMake configure 6.2 s and generation 1.1 s. Targets `aiverse_vivid_vertex_override_tests`, `aiverse_vivid_metal_rhi_tests`, `aiverse_runtime`, and full `aiverse_vivid_renderer_preview` built and linked without warning or error lines in supplied output. `Model.cpp` / `renderer_preview.cpp` newly compiled.
- **CTest actually run:** `vivid_metal_rhi` **Passed** 0.45 s and `vivid_vertex_override` **Passed** 0.93 s; **2/2 Passed, 0 Failed**, elapsed **1.39 s**. No other CTest is claimed for this exact run.
- **Real GUI Metal / Renderer3D visual preview actually run by user:** procedural head **925 vertices**, **2 GPU shape targets**, confirmed `Primitive.firstVertex=0, vertexCount=925`. Stage 0 baseline captured at frame 3 (`shadow=1, colour=1`); stage 1 VIVID GPU morph captured at frame 10 (`shadow=1, colour=1`); stage 2 MotionWeave-driven pose captured at frame 31 (`shadow=1, colour=1`). Three PPM files reported: `/tmp/aiverse-vivid-baseline.ppm`, `/tmp/aiverse-vivid-morph.ppm`, `/tmp/aiverse-vivid-motionweave.ppm`.
- **Image change metrics from test:** baseline-to-GPU-morph **81,988 changed pixels**; GPU-morph-to-MotionWeave **66,972 changed pixels**, test status `pass=1`. Output summary `[metal-assets] libraries=0 sourceMisses=11 pipelines=15 pipelineFailures=0 archive=0 strict=0`. `sourceMisses` reflects cache misses, **not** compilation failures. No output excerpt shows `modelError`, missing draw submissions, GPU parity errors, or failed screenshot comparisons.
- **What is verified:** actual Metal RHI compute morph to 60B ModelVertex, renderer's direct indexed shadow + colour draw calls, user-executed screenshot capture and measurable pixel changes, and use of MotionWeave animation state. This is an integration milestone, **not** proof of anatomically realistic human, production-quality skin/groom, exact visual shadow silhouette (PPM images are on user's Mac, not inspected here), multi-character scheduling/LOD/ICB throughput, or long-running resource lifetime.
- **Code changes for this checkpoint:** documentation only. `main`, MotionWeave and TerraWeave/PHYSIS source branches unchanged.
- **Next exact milestone:** design per-character lifecycle and safe in-flight compute-output buffers using gfx::Device's resource-retirement status and no unconditional per-frame GPU idle. Keep VIVID morph/renderer default-off outside opt-in paths and MotionWeave authoritative for skeleton animation. Add regression tests and handoff on every feature step; benchmark with many characters before enabling batching or LOD.

## Stage 5F — 2026-11-19 — per-character Metal GPU stream ownership

- **Destination branch:** `feature/vivid-digital-humans`. Scope: VIVID only; `main`, TerraWeave/PHYSIS and MotionWeave source branches were not mutated by this stage. MotionWeave remains the only skeleton pose/graph/IK/retargeting scheduler. Stage 5E prior source HEAD `53e5f0cb069dff8d4c173001947ffce3b4791a98`, user GUI verification documentation SHA `4dea755d192b70abe2501a63eba553d4230ffd06`.
- **Evidence actually reported for preceding Stage 5E:** native `vivid_metal_rhi` and `vivid_vertex_override` CTests **2/2 Passed** (1.39s total). Real Renderer3D GUI produced three PPMs for baseline, VIVID facial deformation and MotionWeave pose; colour/shadow draw submissions `1/1` at all captures; pixel change counts `81,988` and `66,972`, `pass=1`, zero shader pipeline compilation failures. These were user-executed on Mac, not the authoring agent; images have not been visually inspected here.
- **Stage 5F implementation / source commits**:
  - `d86864698732080e549c0c0e29d7eaae2da93425` — NEW `engine/character/vivid/GpuActorStream.h`: one owner per character; lifecycle/mesh/facial binding API, last-error diagnostics, strict one-dispatch-per-frame ownership contract and transactional ModelDraw attachment.
  - `118ea1d36f392707825ed9c25d8bd8df15477e80` — NEW `engine/character/vivid/GpuActorStream.cpp`: builds MorphVertex from original 60B model input, validates dedicated uploaded model stream, holds separate `GpuMorphDeformer`, `GpuMorphMeshStream` and weights for each actor, performs ordered GPU morph + packed vertex compute, blocks duplicate dispatch in a frame, rejects stale/wrong-model attaches, explicit idle-teardown via `waitForResourceIdle()`.
  - `b4dd888e0e9687a1d1a63cbe99c388491616a1b3` — `engine/character/vivid/CMakeLists.txt`: NEW `aiverse_vivid_actor` / `AIVERSE::vivid_actor` library; the existing native `aiverse_vivid_metal_rhi_tests` target now links it.
  - `0ff7370d11c7107dcd28ec91aa9e24c31ca405e4` — `tests/vivid/metal_rhi.mm`: two independent actors with one shared immutable Model/library fixture, distinct compute and model vertex buffers, three frames of different facial weights, CPU gather parity for both, immutable attribute integrity, stale/duplicate submission guards and lifecycle shutdown.
  - `39d46a12a0fb7bb00aaa46d50839e836bba33f47` — test correction: stale-frame attachment guard runs **after** the Metal `beginFrame()` advances the frame serial, not before.
  - `1fb8aaa4a7de8f9eb25475efd4f803051b90ef22` — `docs/vivid.md`: public API, ownership, safety gates, limitations and example use.
- **Architecture/API:** `GpuActorStream::initialize(gfx::Device&,const render::Model&,const SparseMorphLibrary&,span<const ModelVertex>)`, `dispatch(const FaceFrame&,const FaceShapeBinding&,influence,normalizeDirections)`, `attach(render::ModelDraw&)`, `vertexBuffer()`, `download(span<ModelVertex>)`, `shutdownAfterIdle()`. A single actor's writable output and weight data are independent of every other actor. `dispatch` requires Metal and a nonzero current device frame serial; duplicate same-frame submissions are deliberately rejected (no accidental partial overwrites). `attach` is transactional, references the exact uploaded source Model, validates the 60B stream and requires last successful compute to belong to this frame. Material and bone animation still run in the original `Renderer3D` and MotionWeave.
- **Resource/lifecycle:** both deformer and repacker own persistent per-actor GPU allocations. No per-frame allocation or forced GPU idle in `dispatch`. `shutdownAfterIdle` may block at rare actor removal: call AFTER the host has committed the GPU frame, never during an active frame; a failed idle barrier keeps the actor resources alive so the host can retry. Hosts must not destroy the actor or gfx device before safe retirement, because the low-level buffer destructors are ordinary nonblocking destructors and cannot infer current uncommitted work. Frequent dynamic actor spawn/despawn still needs an opt-in shared-asset cache + async retirement queue.
- **Public ABI/schema:** new C++ library/API; no modifications to `render::ModelVertex` layout, `Renderer3D` rendering path, `gfx::Device`, MotionWeave, authoritative ECS, save files or network serialization. `main` unaffected.
- **Tests actually executed for Stage 5F code:** **NOT RUN** by this agent (no native Mac GPU/GUI runner). The extended `vivid_metal_rhi` CTest is authored and source-inspected, but this new multi-actor code has not compiled or run on macOS. No CI or full suite has been checked. Do not claim Stage 5F passed based on prior 2/2 or GUI screenshots.
- **Risks / limitations:** each actor duplicates the immutable base and CSR library GPU allocations and compiles two compute pipelines, so memory/CPU setup scales poorly with large character counts (address via shared immutable asset/program cache in a future measured stage). The first release is **Metal-only** and an opt-in path. `frameSerial()` gates duplicate submission but is not a device-generation proof and cannot detect a host trying to attach a stream after `endFrame()` but before the next `beginFrame()`; callers must obey frame boundaries. No GPU-backed crowd benchmark yet; no async per-actor resource retirement. `GpuActorStream` destructor cannot replace an orderly explicit `shutdownAfterIdle()` after GPU submission.
- **Exact next action:** Mac owner fetch current VIVID branch and build/run `aiverse_vivid_metal_rhi_tests`, `aiverse_vivid_vertex_override_tests`, `aiverse_vivid_metal_gpu_tests`; record first compiler diagnostic, real 2-actor 3-frame parity and actual CTest results. If green, extend the GUI preview to use per-actor stream and test many independent characters, then design asset-shared immutable CSR/pipeline residency and nonblocking GPU retirement. Continue writing one handoff per stage.
- **Owner Mac commands (NOT RUN by this agent):**
  ```sh
  cd ~/Projects/AIVERSE
  git fetch origin feature/vivid-digital-humans
  git -C ../AIVERSE-vivid switch --detach origin/feature/vivid-digital-humans
  cmake -S ../AIVERSE-vivid -B ../AIVERSE-vivid/build/vivid -DAIVERSE_BUILD_TESTS=ON
  cmake --build ../AIVERSE-vivid/build/vivid --target aiverse_vivid_metal_rhi_tests aiverse_vivid_vertex_override_tests aiverse_vivid_metal_gpu_tests -j 4
  ctest --test-dir ../AIVERSE-vivid/build/vivid --output-on-failure -R '^vivid_(metal_gpu|metal_rhi|vertex_override)$'
  ```

## Stage 5F user macOS verification — 2026-11-19 — 3/3 PASS

- **Source revision:** test run follows the Stage 5F pull/build commands and the then-current GitHub branch head `d206b815fe34df068ba2f3eb40f48e96fd9cca2d`. The supplied excerpt does **not** show local `git rev-parse HEAD`, so the specific SHA is inferred from the immediately preceding instructions rather than independently echoed by the Mac.
- **Mac compilation reported by user:** `aiverse_vivid_actor` (including new `GpuActorStream.cpp`) built and linked, `aiverse_vivid_metal_rhi_tests` Objective-C++ source rebuilt and linked, plus all prerequisite VIVID/Model GPU targets. No warning/error lines in supplied build output.
- **User-executed CTest:** `vivid_metal_gpu` PASS 0.89s, `vivid_metal_rhi` PASS 1.81s (includes Stage 5F's two-actor, three-frame Metal GPU parity/ownership assertions), `vivid_vertex_override` PASS 0.58s; **3/3 Passed, 0 Failed, total 3.29s**. These are test results on real Apple Metal hardware, **not** a rendering benchmark or proof of dozens of simultaneous actors.
- **Gaps:** existing 3-frame GUI Renderer3D/MotionWeave visual preview was previously verified at Stage 5E, not rerun in the supplied Stage 5F excerpt. No full `vivid_` / MotionWeave suite or long-term hot-reload/device loss soak test reported.
- **Next action:** replace per-actor blocking shutdown on frequent despawn with the existing genre-neutral `render::AsyncResourceRetirementQueue` and GPU retirement markers. Keep a single GPU completion barrier per batch, avoid reimplementing the retirement state machine. Test Pending/Complete/Failed/idle fallback with a recording backend and a real Metal lifecycle pass. No changes to the MotionWeave animation owner.

## Stage 5G — 2026-11-19 — batched token-based GPU actor retirement

- **Branch:** `feature/vivid-digital-humans`, preserving both merged MotionWeave source history and the VIVID presentation-owned GPU actor implementation; no changes to source `feature/universal-ecs-animation-pipeline`, `main`, TerraWeave/PHYSIS or authoritative simulation.
- **Previous Stage 5F user Mac evidence:** `aiverse_vivid_actor` compiled/linked; `vivid_metal_gpu` 0.89 s **PASS**, `vivid_metal_rhi` 1.81 s **PASS**, `vivid_vertex_override` 0.58 s **PASS**; **3/3 Passed, 0 failed, 3.29 s total**. The user's log does not repeat `git rev-parse HEAD` but follows the build instructions for the Stage 5F HEAD `d206b815fe34df068ba2f3eb40f48e96fd9cca2d`. Verification documentation commit `895b412ddd2a3fa0ff1ae99cc5d8f6d52b58e519`. These are real user-executed tests, not independently run by this author.
- **Code commits / exact file modifications:**
  - `7aadcb91484b8022934ff54ee694f28583005f41` — `engine/character/vivid/GpuActorStream.h`: friend-only completed GPU retirement function, deliberately inaccessible to ordinary game code.
  - `58561c66b71276744e2985aa508d14e65852df1f` — `GpuActorStream.cpp`: factor existing immediate-after-idle cleanup into private `releaseAfterGpuCompletion()`; pre-existing `shutdownAfterIdle` still checks idle barrier.
  - `476f66c6066e2f83268e812ca11e4cc6ef0d9bef` — NEW `engine/character/vivid/GpuActorRetirement.h`: device-thread-only `GpuActorRetirementQueue` using `render::AsyncResourceRetirementQueue<std::unique_ptr<GpuActorStream>>`.
  - `1a15bf35b0fd3419636601d7c5935a3fd3739326` — NEW `GpuActorRetirement.cpp`: batch token submission and complete/pending/failed handling via existing gfx::Device, idle fallback/drain, deferred actor memory release.
  - `d7e7e0e2b48ae361f5e250209d5f0aea43426e71` — add explicit required standard `<utility>` include in queue API.
  - `2c016196cc335d48dbe0dddebbfca1cb2b33c520` — NEW `tests/vivid/actor_retirement.cpp`: recording backend verifies two actors sharing a GPU completion token, pending retention, successful release, failed-token + failed idle fallback, later successful idle recovery, unsupported token fallback, device identity rejection.
  - `4dd9462e7fc1f274758cdb71c910198a0df23871` — `engine/character/vivid/CMakeLists.txt`: build retirement implementation into `AIVERSE::vivid_actor`; register `aiverse_vivid_actor_retirement_tests` / CTest `vivid_actor_retirement`.
  - `2bb30669f8e183274b920964e075e1da7bcef8cf` — `tests/vivid/metal_rhi.mm`: add actual Apple Metal actor resource retirement token validation after three frames of two-actor GPU parity. Bounded token polling must complete without fallback.
  - `a2e40729951f3e0c45ec6c438b0fedcf0bfdf754` — explicit `<utility>` in portable fake-RHI test.
  - `85e3f795c6a285cf2b272bf229646c1fbf7379eb` — `docs/vivid.md`: Stage 5G API, GPU lifetime, usage, risks and test scope.
- **API:** `GpuActorRetirementQueue::retire(std::unique_ptr<GpuActorStream>&)` takes actor ownership only after same-device/ready validation; `collect()` polls/reports per-batch GPU tokens, `drain()` requires explicit successful GPU idle barrier for exceptional shutdown/fence failure. `pending()`, `stats()`, `saturated()`, `setDiagnosticObserver()` reuse the renderer's genre-neutral retirement diagnostics. `GpuActorStream::releaseAfterGpuCompletion()` is private and may only be called by the retirement queue on a genuinely completed GPU marker. One token covers all newly pending actors; repeated polling is nonblocking when markers are supported.
- **Scheduling and safety:** all `retire/collect/drain` calls on the same `gfx::Device` owner thread **after `endFrame`** and before the next frame records resource uses. Metal's `beginResourceRetirement` token only follows previously COMMITTED work, not the uncommitted current command buffer. Never delete an actor, queue, Model or device while its GPU stream may be referenced. The queue MUST drain successfully before destruction. If token creation is unsupported, existing shared queue code performs one explicit idle recovery, never silent free. Token failure cannot be bypassed without full idle.
- **Compatibility:** no `gfx::Device` or renderer/Model ABI or GPU shader changes, no MotionWeave skeleton/graph duplication, no saves or network format changes. `aiverse_vivid_actor` grows a new callable API but existing code paths remain default-off.
- **Tests actually run for Stage 5G code:** **NOT RUN** by this agent; only GitHub source and design review. Neither the new portable CTest nor the extended real Metal RHI test has been compiled on Mac. Do not claim 4/4 Passed or hardware retirement throughput yet. Prior 3/3 report predates this stage.
- **Known risks:** the retirement queue requires explicit successful drain before gfx::Device destruction (matching existing generic engine owners), otherwise a caller could destroy GPU buffers prematurely. A token does not protect an actor still referenced by commands not yet committed; boundary discipline is mandatory. Shared immutable morph CSR/pipeline cache, GPU benchmark with many actors, device-loss handling and GUI multi-actor visibility have not been implemented. Real Metal marker completion may be delayed; its test has bounded polling and a failure branch requiring recovery.
- **Next exact step:** user Mac fetch Stage 5G, configure `AIVERSE_BUILD_TESTS=ON`, build `aiverse_vivid_actor_retirement_tests` and `aiverse_vivid_metal_rhi_tests`, run `ctest -R '^vivid_(actor_retirement|metal_rhi|metal_gpu|vertex_override)$'` **after building the named binaries**. Report the first compiler error/warning and test status. If green, integrate the actor queue in the real 3-capture preview for lifecycle events and benchmark dozens/hundreds of characters before enabling shared immutable assets.
- **Mac commands (NOT RUN here):**
  ```sh
  cd ~/Projects/AIVERSE
  git fetch origin feature/vivid-digital-humans
  git -C ../AIVERSE-vivid switch --detach origin/feature/vivid-digital-humans
  cmake -S ../AIVERSE-vivid -B ../AIVERSE-vivid/build/vivid -DAIVERSE_BUILD_TESTS=ON
  cmake --build ../AIVERSE-vivid/build/vivid --target aiverse_vivid_actor_retirement_tests aiverse_vivid_metal_rhi_tests aiverse_vivid_metal_gpu_tests aiverse_vivid_vertex_override_tests -j 4
  ctest --test-dir ../AIVERSE-vivid/build/vivid --output-on-failure -R '^vivid_(actor_retirement|metal_rhi|metal_gpu|vertex_override)$'
  ```

## Stage 5H — 2026-11-19 — shared immutable morph GPU data and weak cache

- **Current work branch:** `feature/vivid-digital-humans`. Preserves the MotionWeave merge; `main` and MotionWeave source not changed. Prior actual Mac native 3/3 pass was Stage 5F, 3.29s; new Stage 5G async retirement and Stage 5H shared geometry **NOT RUN on Mac** in this coding turn.
- **Source commits / files:**
  - `d1962b5d543a3fc6d0c82f93397fd6b352c2cd4e` — NEW `engine/character/vivid/GpuMorphSharedAsset.h`, public immutable shared GPU asset contract.
  - `2596a8461e61a6aa059994d410197342693b8bdf` — NEW `GpuMorphSharedAsset.cpp`, uploads packed base48, CSR data, original ModelVertex60 and two compute pipelines; device budget 512 MiB; read-only shareable across actors.
  - `3032f014f5c23ae8bf5991dc72642d072b45f5b6` — `GpuActorStream.h` now has `initializeShared(device,model,sharedAsset)` and retains backward-compatible `initialize`; writable weights and 48B/60B outputs remain private, shared asset is retained by reference.
  - `d470488884dd158ded8625c84580dc3a64d1126d` — `GpuActorStream.cpp`, per-actor storage upload + two ordered compute dispatches reusing shared shaders/immutable CSR, same-frame/old-frame guard, post-fence resource reset.
  - `b0f63419d71bf807eb8c78e5356f48494c6e5e8a` — `engine/character/vivid/CMakeLists.txt` includes shared asset code.
  - `dec3134d9358fb5abd4250af68e955c551b530f7` — NEW `GpuMorphAssetCache.h`, device-local weak content-addressed GPU asset cache.
  - `e50454d48933e8af0d608e6dafcfde8ec93236c7` — NEW `GpuMorphAssetCache.cpp`, double 64-bit streaming digest of exact GPU-relevant `ModelVertex` and sparse CSR source bytes, weak-entry lookup and pruning.
  - `c88ca699d69d5900510d0d50a541fecc04f2741f` — CMake adds asset-cache source to `AIVERSE::vivid_actor`.
  - `7f1e2b9b80814a7008fdecec1c2586b0c14c9a99` and `ad3aa29b730a87b3029f1a8b9cd61a67aff5bbe4` — `tests/vivid/actor_retirement.cpp` checks shared GPU buffer/program usage, weak cache hit/miss/prune and last-reference release.
  - `1db0c020fdd10fe15f48da2ed55c32868affaff6` and `cf2cf653ee49fb309fa71925f6e4db1eaa96ec9b` — `tests/vivid/metal_rhi.mm` checks two actual Metal actors using one immutable cached GPU asset, 3 frame CPU reference parity and cache release.
  - `d9f459420771a9092a7ae60229aa5b3ef2546f83` — fake-Metal recording GPU stress: 128 independently deforming actors across four frames, one shared asset, one completion token for all 128.
  - `2aa40112a89c2f5d16a43dd202363d35787854f5` — preallocate actor CPU weight buffer before any GPU-owned resource publication to prevent partial GPU resource leaks on allocation exceptions.
  - `24f3783edef5949351aee52d41d3ea42aac77504` — `docs/vivid.md` Stage 5H API and test-status details.
- **Architecture and API:** `GpuMorphSharedAsset::create(device,library,vertices)` uploads five shared GPU buffers and two compute pipelines; `GpuMorphAssetCache::acquire` reuses an immutable asset for identical GPU-relevant source bytes. `GpuActorStream::initializeShared` retains a shared_ptr to this data and creates only three writable per-instance buffers (weights, 48B morph output, 60B final render output). MotionWeave continues to own pose/retargeting/IK; the existing `ModelDraw.vertexOverride` consumes final output. Asset GPU resources stay alive until the last actor/external reference is retired after confirmed GPU completion.
- **Memory scaling target:** `5+3*N` buffers and 2 compute programs for N instances sharing an asset, versus `8*N` buffers and `2*N` programs previously. Caller may opt into legacy independent init; no serialized format change. The 512 MiB limit applies per shared asset and per actor output set; broad scene-wide budgets/performance are not yet measured.
- **Test coverage authored but NOT RUN**: fake RHI `vivid_actor_retirement`: identical vs modified asset content, 128 actors/4 frames, distinct vertex handles, one GPU fence for all, Pending/Complete/Failed/idle fallback. Real Metal `vivid_metal_rhi`: shared asset initialization, per-actor weights and full 60B CPU parity. No Mac compiler/GPU tests, GUI visuals or CI results have been confirmed for these new Stage5G/5H commits.
- **Risks:** calling GPU compute or queue retirement outside device frame/boundary is prohibited. `GpuMorphSharedAsset` destructor destroys GPU resources immediately; it is safe only when ALL actor references have passed a successful completion token or idle barrier. The cache holds weak references to avoid premature lifetime extension; host must still retain original `render::Model` and device. GPU-asset content key is a noncryptographic 128-bit hash; no heterogeneous-backend cross-GPU sharing. CPU model revisions can require new cache entries and deferred old resource retirement. Direct GUI crowd/LOD and Vulkan shader tests remain open.
- **Exact next action:** one-pass macOS build + CTest for all VIVID/MotionWeave tests, GUI Renderer3D preview, and check the new shared-C SR/retirement cases, then record results. If red, fix primary compiler error and rerun ONE full command. New automated CI workflow exists but a runner's queue status is not equivalent to tests having passed.

## Stage 5I — 2026-11-19 — single-run Mac validation and focused CI

- **Branch:** `feature/vivid-digital-humans`; neither `main` nor MotionWeave/TerraWeave/PHYSIS feature branch mutated. Entire work remains portable C++20/Metal opt-in with MotionWeave owning skeleton animation.
- **Implementation commits and paths:**
  - `56191d9beedd5e7a32807bb8d4535e84544b3063` — new `tools/vivid-validate.sh` on Mac: safe non-dirty checkout, fetch current VIVID head, record SHA, configure dedicated build tree, compile all VIVID and MotionWeave API tests plus GUI preview, sequential CTest, real Renderer3D/MotionWeave screenshot verification. Logs/screenshots/summary retained in `build/vivid-onepass/vivid-validation`.
  - `c06533de9c2350547007cf4c37c6c80c00ba0eca` — `.github/workflows/vivid-validation.yml`, focused macOS-15 GitHub Actions build+CTest on VIVID branch pushes/dispatch. Hosted CI may have no Metal GPU; skip is not GPU parity. GUI preview intentionally omitted from CI.
  - `56c71b575432d00e5a165e90d0f8b9c7d0884f86` — `docs/vivid.md`, exact runner usage, validation scope and outstanding authoring/product features.
- **One single command for user Mac:** `bash ~/Projects/AIVERSE-vivid/tools/vivid-validate.sh` (run after already having a local worktree with this script; if not yet pulled, use `git -C ~/Projects/AIVERSE-vivid fetch origin feature/vivid-digital-humans && git -C ~/Projects/AIVERSE-vivid switch --detach origin/feature/vivid-digital-humans` once to obtain the script). The runner handles future fetch and checkout. `--no-gui` is incomplete headless validation; `--full` optionally builds/runs the complete engine suite, potentially slower/affected by unrelated code.
- **Exact acceptance tests selected:** `vivid_face`, `vivid_sparse_morph`, `vivid_shape_binding`, `vivid_morph_binding`, `vivid_gpu_contract`, `vivid_metal_gpu`, `vivid_metal_rhi`, `vivid_vertex_override`, `vivid_actor_retirement`, `vivid_motionweave_bridge`, and `motionweave_api` (11 selected when configured on macOS). Followed by GUI `aiverse_vivid_renderer_preview` which must capture baseline, morph and MotionWeave images and print `pass=1`. Serial Metal CTest avoids running GPU jobs concurrently.
- **Native tests actually run by this assistant for new Stage 5G/5H/5I:** **NOT RUN**. User's previous Stage 5F macOS results were 3/3 Passed (0 failed, total 3.29s); those do not cover the new shared-cache refactor or async retirement tests. Existing GitHub Actions workflow run `44584352685` was **queued** when last checked, not a PASS. This is a launch attempt for one-shot CI, not proof of success.
- **Integrity/compatibility:** native GPU kernel and 60B `ModelVertex` ABI unchanged. Authoritative ECS, save/network schema and legacy renderer model path unchanged. `AIVERSE::vivid_actor` now consumes shared immutable asset API and stage 5G retirement. Both asynchronous and lifecycle tests only free GPU resources when exact completion token or successful idle barrier permits it; GPU resource owner must outlive model/device.
- **Remaining feature gates:** resolve any first compiler or GPU test failure in the ONE runner, then rerun the same command; do not use previous pass counts to imply new branch has passed. GPU shared asset/hardware character crowd benchmarking, Vulkan/GL parity, true hair/groom/skin/eyes/rig-DNA content and editor authoring remain unimplemented; VIVID is not a feature-complete MetaHuman replacement.

## Stage 5J — 2026-11-19 — real Renderer3D multi-character preview and one-command two-mode GUI acceptance

- **Branch and ownership:** `feature/vivid-digital-humans`, no merge to main, MotionWeave or TerraWeave. MotionWeave remains the only skeleton animation scheduler. VIVID only supplies per-instance face morph GPU vertex streams.
- **Code commits and exact files:**
  - `d887e31af195d676cfe8a560e7d23e567cd7618e` — `tests/vivid/renderer_preview.cpp` replaces single `GpuMorphDeformer` / `GpuMorphMeshStream` with cached immutable `GpuMorphAssetCache`, many independent `GpuActorStream`s and per-instance semantic face weights. Supports `AIVERSE_VIVID_PREVIEW_ACTORS` clamped to 1–32. Different actors draw the same original `Model` but independently deform their mesh outputs with distinct handles/expressions; all use the same MotionWeave graph/node pose. Retains baseline/morph/MotionWeave screenshots with shadow+colour draw count checks.
  - `bf346a88638142564bb9416a2b89c73f425d8a86` — `renderer_preview.cpp` removes now-unused CPU MorphVertex extraction helper and avoids a second unnecessary idle barrier: actors are retired/drained through `GpuActorRetirementQueue` once, after frame completion.
  - `8bc6d89ce1102ae7470114cbc37dbbe306d0452d` — `tools/vivid-validate.sh` runs one-actor and 12-actor real Metal preview in the same single command after CTest, captures/checks six PPMs, retains both logs.
  - `9eb03b780d7369ca29c6981db543619260de449f` — root `CMakeLists.txt` links `AIVERSE::vivid_actor` into the GUI preview target so GPU cache/actor symbols resolve.
  - `aa8ddc5cf0f78442a4179fa15d390ee05bbfd1b0` — `docs/vivid.md`, crowd preview and acceptance workflow.
- **Architecture / data flow:** host `platform::run` → gfx Metal frame → `MotionWeave::update` for shared body/node pose → one cached immutable CSR/pipelines asset → per-actor `GpuActorStream::dispatch` with unique facial weights → `ModelDraw.vertexOverride` per instance → one shared `Renderer3D` performing real direct colour and shadow passes. No duplicate skeleton ECS system or per-frame CPU readback. At shutdown `GpuActorRetirementQueue::drain` gates final actor GPU destruction and cached immutable asset release.
- **API / compatibility:** only diagnostic preview/CMake and single-run script modified. Production renderer and existing VIVID public C++ API unchanged in Stage 5J; no save/network formats. The 60B ModelVertex ABI and original model shadow shaders are unchanged. Crowd branch defaults to 1 actor when env var absent, user test explicitly uses 12.
- **Tests performed on this new source:** **NOT RUN** by this agent. Prior user macOS Stage 5E real scene (single actor) and Stage 5F 3/3 GPU CTests predates Stage 5H asset cache and this preview refactor. There is a GitHub Actions workflow on push, but it may still be queued; do not claim CI pass until completion. No screenshots for 12 actors have yet been reported by user.
- **Risks:** 12 procedural heads within the same camera may overlap/saturate screen; image difference thresholds remain conservative (>200, >100 changed pixels) but need real screenshots and render profile output. Limited to Metal dedicated nonpooled dynamic models, normal batching/ICB/LOD intentionally excluded from opt-in stream. Shared asset cache is per GPU device and must not outlive it; any failed queue drain must retain model/device resources and report a failure rather than silently freeing them.
- **Next exact action:** run `bash ~/Projects/AIVERSE-vivid/tools/vivid-validate.sh` after fetching latest branch (or bootstrap once with `git -C ~/Projects/AIVERSE-vivid fetch origin feature/vivid-digital-humans && git -C ~/Projects/AIVERSE-vivid switch --detach origin/feature/vivid-digital-humans`). This one run includes 11 CTests and both GUI stages. Capture build/CTest/preview logs and compiler diagnostics; if a step fails, fix the primary cause on this branch and rerun the entire script. Record the final Mac SHA and evidence here. Complete product scope (realistic skin/eye/groom/DNA, editor, Vulkan) remains unimplemented and cannot be called fully shipped.

## Stage 5J — macOS one-pass acceptance result — 2026-11-19 — PASS

- **Confirmed tested source SHA:** `fa5504a9f2e1f64087ef73139525e5bde31976b4`, from the user's actual `tools/vivid-validate.sh` `summary.txt` output. Branch at time of this run: `feature/vivid-digital-humans`.
- **Owner-run test command:** `bash tools/vivid-validate.sh` on macOS in `/Users/aleksandrfeer/Projects/AIVERSE-vivid`. It completed and printed `VIVID one-pass verification COMPLETED.`. The user supplied actual CTest and graphics capture logs, not merely a command.
- **Scoped CTest results: 11/11 PASS, 0 failed, 10.19s total.** `motionweave_api` 0.90s; `vivid_face` 0.61s; `vivid_sparse_morph` 0.33s; `vivid_shape_binding` 0.98s; `vivid_gpu_contract` 0.33s; `vivid_motionweave_bridge` 1.76s; `vivid_metal_gpu` 1.22s; `vivid_metal_rhi` 0.63s; `vivid_actor_retirement` 1.60s; `vivid_vertex_override` 1.16s; `vivid_morph_binding` 0.66s. This includes new shared-asset and 128-actor fake-RHI contract assertions, real Metal two-actor GPU execution and async resource lifetime tests.
- **Real macOS Renderer3D one-actor GPU/MotionWeave GUI preview: PASS.** Procedural model `925` vertices, `2` GPU shapes; immutable GPU model/CSR data `158324` bytes. Baseline, morph and MotionWeave captures each reported **1 shadow and 1 colour** model draw. Capture frames: 3, 10, 32. `81988` morph-changed pixels and `66972` MotionWeave-changed pixels; `pass=1`. Three nonempty PPM outputs under `build/vivid-onepass/vivid-validation/vivid-{baseline,morph,motionweave}.ppm`.
- **Real macOS Renderer3D 12-actor GPU/MotionWeave GUI preview: PASS.** Shared immutable data still `158324` bytes, `925` vertices and `2` GPU shapes; `12 shadow / 12 colour` model draws at each of frames 3, 11, 32. `179673` morph-changed pixels and `183275` MotionWeave-changed pixels; `pass=1`. Three nonempty PPM outputs under `build/vivid-onepass/vivid-validation/vivid-crowd-{baseline,morph,motionweave}.ppm`.
- **Real Metal shader diagnostics in both GUI runs:** `[metal-assets] libraries=0 sourceMisses=11 pipelines=15 pipelineFailures=0 archive=0 strict=0`. Source-misses are shader-asset cache lookups, not GPU compilation failures. No error/warning/note diagnostics appeared in the supplied excerpt.
- **Runner's persisted summary:** `GPU+MotionWeave CTest=PASS`, `Renderer3D colour/shadow/MotionWeave visual preview=PASS`, `Renderer3D shared GPU asset crowd preview=PASS`. Exact log directory: `/Users/aleksandrfeer/Projects/AIVERSE-vivid/build/vivid-onepass/vivid-validation`.
- **Verification provenance:** test and GUI work executed by the user on Mac. The agent has *not* opened the six local PPMs; claims are limited to actual render-pass counters, capture outputs and programmatic pixel-difference results. No full-engine CTest suite was run by this command (`--full` was not selected), nor production GPU frame timings/FPS, real photorealistic human assets/eyes/hair/DNA, Vulkan parity or a device-loss soak.
- **Source changes for this checkpoint:** documentation only; no reimplementation of MotionWeave, new simulation mutations, or branch merge.
- **Next step:** treat Stage 5J *focused Metal VIVID integration acceptance* as PASS. Follow-up milestones, if required for full product readiness, are accurate GPU timing and scaling at high actor counts, non-Metal backend parity, streaming/LOD/ICB safe activation, actual human asset/rig/groom/skin/eyes and editor authoring. Do not label those unfinished features as completed, and do not re-run focused tests without a source change or new QA target.

## Stage 6A — 2026-11-19 — HumanDNA schema and serialization

- Branch: `feature/vivid-digital-humans`; MotionWeave core and main unchanged.
- Commits: `f98f7e220dfb12ac76f0d4b17fbcae5efa406a56` `HumanDNA.h`; `accfd04657f95bbde794293dd5006c9098fdbad6` `HumanDNA.cpp`; `611f79c088e8dcc55b103398b7fc26a48e544885`, `ae3c8f38633a0433fc302015291d43d79fa5cbe3` corrections to public include/range parsing; `94b786f603dad567d16cc11b753db0f9f0dc80f0` `tests/vivid/human_dna.cpp`; `710d7b670fc198cf827615c05548b420fda0d1f4` `engine/character/vivid/CMakeLists.txt` new `AIVERSE::vivid_human` and `vivid_human_dna`.
- API: `HumanDNA::preset`, `HumanDNA::fromJson`, `HumanDNA::toJson`, `HumanDNA::validate`. Version 1 includes anatomy/skin/eyes/groom/rig and 52 facial rest channels. Strict finite/size guards and canonical serialization, no proprietary DNA parsing.
- Tests actually executed: **NOT RUN** for Stage 6A Mac compile/CTest; code authored and checked remotely. Do not infer passing tests from previous VIVID 11/11.
- Next: build material/shader integration, eye/groom procedural asset generation, editable authoring UI; then one Mac validation. Preserve genre-neutral MotionWeave ownership and write a stage handoff.

## Stage 6B — 2026-11-19 — rig, procedural appearance, groom and editor state

- Branch: `feature/vivid-digital-humans` only; MotionWeave and main remain unchanged. Previous focused VIVID Metal stage 5J passed all 11/11 tests and both GPU previews at tested SHA `fa5504a9f2e1f64087ef73139525e5bde31976b4`; this is NOT verification of newly added Stage 6 code.
- Implementation commits/files: `00f9fed74987534f0852d94ce03fd1c34cbe45e5` `HumanRig.h`, `703e85be1ebec862aed3721a3a59a18b2c403d0d` `HumanRig.cpp`, `87ecaf50285c486eb59a291bb66170e475257828` `tests/vivid/human_rig.cpp`; `bb65d6841ea9526219c6650487abce11b90a0b92` `HumanMaterials.h`, `b1c499951c6dedbf232453ae9df9bc7937915cb2` `HumanMaterials.cpp`; `0c8207209fe781929dca4b76362f44da576e942a` `HumanGroom.h`, `710b38561c07b461e3c8f9a6e280188a68cb8116` `HumanGroom.cpp`, `54bd71572b807102d24df25834336f6b83b7d160` post-finalize dynamic groom geometry pooling exclusion; `7b82369f97446295c269e042296ff3ecd311603f` `HumanPreview.h`, `710b91ae0d7506f6151c145d475ffffb22780421` `HumanPreview.cpp`; `f724bc3a2c1b6b16b6bfa4985ddf955a2bce2203` `HumanCreatorSession.h`, `4748f79e333fd2d2d82266f267505677086e2d86` `HumanCreatorSession.cpp`; `814722a86f0f9eb40c13a66cdfcabebdbab9f9c1` CMake wiring; `96eee91e92829aec0bda4cba41ebf2791a473ba8` `human_appearance.cpp`; `a36f05ec31f5849fb197744466c26c8e6e7a3ed8` `human_creator.cpp`.
- APIs and behavior: HumanRigBinding::compile/compose generates jaw and eye pose corrections using a copy of MotionWeave node matrices; DNA-named hierarchy validation, no authoritative ECS mutation. HumanMaterials::humanMaterial creates per-surface PBR ShaderGraphs with MSL/GLSL parity under existing renderer. HumanGroomRuntime::generate/step/makeCardModel/updateCardVertices supports 3 quality levels, bounded vertex/card budgets, deterministic roots and presentation physics. generateHumanPreview returns a rigged procedural `HumanPreviewAsset` with hair and five named correctives. HumanCreatorSession::edit/undo/redo/loadFile/saveFile validates transactions and avoids destructive invalid edits.
- Tests: 3 new portable CTest cases authored, **NOT RUN**. No new native Metal shader compilation, GUI screenshot or complete MetaHuman-quality comparison has been executed. Earlier VIVID 11/11 success is historical to Stage 5J.
- Limitations: procedural reference mesh rather than scanned head, approximate screen-space-independent skin scatter and hair-card highlight (no full physically measured multi-layer skin/strand shading), scalp collision proxy (no full body hair/cloth contacts), no GUI editor yet, glTF/FBX partial legacy morph import limitations, no Vulkan GPU parity for new materials. ShaderGraph generated code and model uploads require actual Mac build/pipeline warmup testing.
- Next exact step: complete interactive Human Creator GUI and asset import/export with presentational preview; extend single command to build new appearance targets and run CTest/GUI, record first actual compilation and visual result before declaring done.

## Stage 6C — 2026-11-19 — native VIVID Creator UI

- Branch: `feature/vivid-digital-humans`; owner `engine/character/vivid`, `tools/vivid-creator`, and new CMake target. MotionWeave unchanged and sole skeleton scheduler. No main merge.
- Implementation commits: `0cb91b095df7928d21401131335060f3080f33e3` added `tools/vivid-creator/main.cpp`; `3f2242835c929bfb68fbacda415c777d6aa4a417` added `aiverse_vivid_creator` root CMake target linking runtime/VIVID human/actor/MotionWeave; `2adeee4864bf48d680c42fa41a122e8d1722ae70` native `--smoke` GPU render/capture mode; `bf7c898c5da8e48cc186d2c2521023117e521a64` repaired stable ImGui groom sliders and engine font; `32c0463d09d9bb57794c6e8b98473c4e0ce2aa74` passes `AIVERSE_SOURCE_DIR`; `f61f8757703012873e37894584b5048ca25983bd` one-shot tester builds and smoke-runs Creator; `d9c52eb21f92b1e162b8cc4b993b22d2e1773800` focused macOS CI includes Creator compiler target.
- API/pipeline: UI owns `HumanCreatorSession` (transactional validated DNA + undo/redo + JSON save), `generateHumanPreview` model, `GpuMorphAssetCache` plus isolated `GpuActorStream`, `HumanRigBinding` overlay and a presentation MotionWeave World, not a second skeleton engine. The visual scene uses original `Renderer3D` with dedicated dynamic groom vertex storage. `--smoke` captures actual RGBA through `gfx::Device::captureNextFrame` after 4 stable shadow+colour frames; prints `pass=1` only on saved output and creates `vivid-creator.ppm`.
- Tests **NOT RUN** for new 6C C++/GUI. Prior Stage5J 11/11 and 2 GUI tests PASS at `fa5504a9f2e1f64087ef73139525e5bde31976b4`, but do not verify new Creator.
- Risks: failure of new model shader permutations, morph/rig model index mismatches, pending GPU framebuffer capture, dynamic buffer lifetime and GUI hot-reload; verify with user's native Mac one-shot command. Standalone Creator executable, not yet embedded as main AIVERSE editor pane. Imported model integration described separately in 6E.
- Next: add specialized BRDF skin/eye/hair and open import, run Mac full focused tests/GUI. No automatic merge.

## Stage 6D — 2026-11-19 — opt-in human Skin/Eye/Hair BRDF

- Branch: `feature/vivid-digital-humans`. Main and MotionWeave unchanged.
- Implementation commits/files: `49a00a68b48740de08a7c605532ee4d6ce3bccf0` `engine/render/Model.h` extends `ShadingModel` with `Skin/Eye/Hair` and adds `Material.humanProfile`; `10ac2caff3250e96ee52031d46a5ad71847ee6c4` `engine/render/GpuMaterialState.h` profile-aware equality/hash; `a0ac7df1c834d8f9f2dc5ec7025c4b031df22009` `engine/render/Renderer3DModels.cpp` passes profile to the existing material uniform U2, preserving the old zero/one PBR/Unlit values; `a0ec4e0dd8b20429461c8db6ba1b97c81c151095` `engine/render/ModelShaders.h` paired GLSL+Metal skin scatter, cornea/glint and tangent-aligned hair reflection; `065d1198b75683062a1439c6106cea802fae2d30` `engine/character/vivid/HumanMaterials.cpp` DNA-to-shading mode/param mapping; `1bb3688d1fbb8209b137385240f540a808655970` `tests/vivid/human_appearance.cpp` shader source/pipeline state/hash coverage.
- API/architecture: existing PBR renderer; no new RHI APIs, texture binding slots, material uniform size or vertex layout. New `ShadingModel` enum values appended after `Pbr=0,Unlit=1`, so old material output remains unchanged by default. Values `Skin=2,Eye=3,Hair=4` and two per-surface humanProfile floats control compact approximation. `equivalentGpuMaterial` and `gpuMaterialStateHash` now include humanProfile to prevent accidental shared U2 skin/eye/hair state. Avoids genre-specific dependency. Material shader compile still relies on paired `ShaderGraph`.
- Actual new tests: **NOT RUN**; hosted CI may queue, and remote source review does not prove compiling GLSL/MSL. Existing Stage 5J GPU/Renderer3D 11/11 from earlier SHA is historical.
- Scope limits: physically inspired wrapped diffuse/hemoglobin, wet corneal highlights, strand tangent lobe; **NOT** full diffusion-profile multi-layer SSS, ray-traced cornea/aqueous refraction, Marschner/dual scattering, measured scan textures or MetaHuman pixel parity. Batching, instancing, shadow and performance regression must be tested on Mac before production enablement.
- Next: import real glTF/FBX heads with open DNA/morph manifest, then run focused CTest plus actual Creator GUI shader compilation and full engine regressions. Record exact Mac diagnostics.

## Stage 6E — 2026-11-19 — open glTF/FBX DNA/morph package import

- Branch: `feature/vivid-digital-humans`. Open, non-proprietary import. No modifications to MotionWeave scheduler or simulation, `main` not merged.
- Source commits / paths: `5406fdcf31ab88fc3e89923a464fa77206884cc4` `engine/character/vivid/HumanImport.h` manifest + imported human contract; `2d5d85de2d0aa8bdb41889977a807f23fcd31753` `HumanImport.cpp` strict external manifest, DNA, sparse corrective morph JSON and render::loadModel through project FileReader; `32adc384558d5141536e52933c42a9721c46c393` `HumanImport.cpp` fills missing OBJ primitive model-relative firstVertex/count and guards original vertex positions after upload preprocessing; `35caba907465af5fa447712d3174f6cc26b0a7aa` `tests/vivid/human_import.cpp` manifest parse/roundtrip, security/range negatives, 3-vertex fake OBJ root/head import, two sparse morphs and morph-only rig tests; `2d1b73ed7ad815bf0f7641f2b01820a476a2cc06` CMake links new importer into `AIVERSE::vivid_human` and registers `vivid_human_import`; `823abfb980c89f2d49779881e2d613fc7be3437b` `tools/vivid-creator/main.cpp` adds `--asset /path/to/manifest.json` CLI, UI imported-model chooser, DNA sessions and imported material/groom updates while preserving original skinned mesh; `d144dbf5fea0d04bf1a40af564dbba494705bd8b` and `4df2d769d293bb227f91773e387d5898d8d5f8f8` single Mac script/CI include importer test.
- API: `HumanImportManifest::validate/fromJson/toJson`, `parseHumanMorphs(Json,vertexCount)`, `importHumanAsset(manifest,render::FileReader)`, `HumanImportedAsset`. JSON schema: `version=1`, model, optional DNA/morphs JSON, routes mapping named ARKit channels to sparse morph target names, materials mapping authored slots to `HumanSurface` types. Native render::loadModel loads glTF/GLB, FBX (when compiled), OBJ and other engine-supported formats. Asset paths must be project-relative and reject path traversal/absolute, numeric input finite/bounded, shape/CSR names validated. Original mesh vertex IDs are checked before/after prepare; OBJ omitted ranges reconstructed from actual indices. Invalid imports throw instead of publishing incorrect facial geometry.
- Creator import: manifests resolve assets relative to manifest directory, editor accepts user-provided manifest path, uses stored DNA to bind materials and generate groom, MotionWeave node pose continues to own rig. Imported meshes keep authored topology. Editing geometric anatomy parameters does not automatically resculpt a scanned imported mesh; edit materials/groom/expressions or reauthor external mesh.
- Tests actually run AFTER Stage 6E code: **NOT RUN** by this assistant or user. Previous Stage 5J 11/11 and two GUI passes are earlier commit only. No CI green or native import GLB/FBX test evidence. CTest source was written but Mac compilation and real imported 3D human visual preview not yet confirmed.
- Risks: glTF/FBX source must contain a root/head hierarchy matching the JSON DNA bone aliases; morph JSON must reference imported mesh CPU vertex indices after loader canonicalization, not Blender's original unordered mesh indices. Driver/file path and Model::prepareModelUpload topology safety must be checked on actual production assets. Asset importer currently does not parse proprietary Epic MetaHuman DNA binary; only open VIVID schema. Imported external rig/groom look still needs artist-provided model, real textures, animation clips and benchmark.
- Next exact step: do one macOS `bash tools/vivid-validate.sh` after pulling latest, now including 16 CTests, single/12-actor facial previews and real Creator skin/eye/hair smoke PPM. Then separately launch `aiverse_vivid_creator --asset /path/to/manifest.json` to visually validate a user-supplied actual rigged GLB/FBX. Fix first compile/runtime diagnostic; record handoff. Do not merge to main before full regression.

### Stage 6E follow-up — preserved scan textures and real imported-head GUI smoke

- **Implementation commits:** `68d001067d61af3f03795d8aa0565a46095bbccf` `HumanMaterials.h` adds `humanMaterialFromSource`; `c29b19278d61806db38a62762a05fe321b755bcf` implementation preserves imported base/MR/normal/AO/emissive/team texture IDs and UV transforms while replacing shader BRDF; `81f69e41bbf7ef68dfb663b6267fa098f0fb58d7` `HumanImport.cpp` uses it; `48e4f5001bcb050cdd45c580845ea94cabb689b7` Creator preserves imported texture maps during live DNA changes; `f3dbfc2ff485c79132da7cbf8cf196e048bb464d` adds PBR texture-ID regression.
- **Open asset GUI test fixture:** `3e8e149929bb4774d4770092249a7042f0d54def` `tests/vivid/assets/import-head.obj`, `32aba615cd2dca33340f1c3dd70984e996c98050` `tests/vivid/assets/import-face.json`, `d149d1d1fd8b0c6fc69c25d6d6eaefe2b2f2a07a` `tests/vivid/assets/import-head.manifest.json`; all are original tiny reference data, no proprietary MetaHuman meshes. `ac02aeb27cb203a474244e2739fae1591f505240` extends `tools/vivid-validate.sh` to launch GUI `aiverse_vivid_creator --smoke --asset <manifest>` after procedural Creator smoke; requires nonempty `vivid-imported-head.ppm` and `pass=1`.
- **Tests actually executed for follow-up source:** **NOT RUN**; no new Mac compiler/Metal result. Expected onepass now includes **16 CTests** (prior 11 plus DNA, rig, appearance, creator and import) and **four GUI acceptance runs** (1 actor, 12 actors, procedural human Creator, imported OBJ human Creator), with **eight PPM images**. Only earlier Stage5J 11 CTests + two GPU previews are verified as PASSED by user.
- **Next:** Mac owner run the same one-shot script, report first build/test/preview error. After green, full engine regression and realistic licensed scan assets are separate readiness gates. Do not merge source into main until the broader regression passes.


## Stage 6G — 2026-11-19 — DNA-proportional scalp collision

- Branch: `feature/vivid-digital-humans`. `main` and MotionWeave scheduler unchanged.
- Implementation commits: `66173dbe7fbff0313a02cb8e57b81e166b91ad3c` (`HumanGroom.h`), `bc6deb5cd3fe46d0420814623086b6f842dfbc27` (`HumanGroom.cpp`), `5ebb1fd0561aec9c7bc4673cf127bddcff589680` (`tests/vivid/human_appearance.cpp`), `a62d72b3153822775dac3c8e66fd6b7e2fe595bf` (final constraint pass).
- Issue: old grooming used a fixed 0.72-world-unit scalp sphere even though guide root positions use an ellipsoid whose x radius depends on `anatomy.headWidth`, with all axes depending on `headScale`. This permitted guide penetration on wide/tall heads and excessive separation on narrow heads.
- API: no public signature changes. `HumanGroomRuntime::generate` now retains three scalp radii matching the DNA-driven guide ellipsoid, and `step` projects penetrating points to the ellipsoid shell (including degenerate centre contacts). An additional final contact pass prevents a later segment constraint from moving earlier points back into the scalp.
- Regression authored: `tests/vivid/human_appearance.cpp` steps Low groom for 24 frames under wind at head width 0.65, 1.0, 1.35 and head scale 1.2, then rejects nonfinite points and world positions penetrating the corresponding ellipsoid.
- Actual tests executed for these new commits: **NOT RUN**. No native macOS/Metal test execution is available to this agent. The earlier 11/11 CTests + two Renderer3D previews were for Stage 5J SHA `fa5504a9f2e1f64087ef73139525e5bde31976b4`, not Stage 6G.
- Limits: scalp collision is an approximated ellipsoid, not mesh-based collision against arbitrary imported heads, neck, shoulders or cloth. Constraint projection can stretch strands and does not supply self-collision. Imported author-authored hair geometry is not simulated by this procedural card groom.
- Next exact step: on macOS, run `bash tools/vivid-validate.sh` from the VIVID worktree after fetching this branch. Address first compile/CTest/Creator Metal GUI failure, append the real logs/SHA here, then do `bash tools/vivid-validate.sh --full` for merge readiness. Longer-term 6H: scalp mesh collision and motion-stable hair simulation for imported heads.


## Stage 6H — 2026-11-19 — opt-in triangle collider and bounded grooming

- Branch: `feature/vivid-digital-humans`, without modifying `main` or MotionWeave.
- Implementation commits: `9b7d93b8d63e829d4f216600e0b7efc024fc58ef` header API, `86fac42582a985653ae9ac4cbe7524032b98654a` triangle collision implementation, `ad90d4b464516874f15a209b867912b827f2b6b9` regression fixture, `f2dc81e578d36fbc83ec5516dc333d084276eb50` bounded Verlet displacement and collision damping.
- New API: `HumanGroomRuntime::setCollisionMesh(vertices, triangles)`, `clearCollisionMesh()`, `hasCollisionMesh()`. It copies validated bounded local-space triangle data (up to 16,384 vertices / 32,768 triangles); failures keep last-good collider. When present, a closed triangle mesh replaces ellipsoid contact checks. Nearest-triangle projection plus ray intersection parity detects interior points. Groom step also caps violent single-step travel and removes velocity on corrected contacts.
- Tests authored: `vivid_human_appearance` verifies 4-frame collision against a 12-triangle closed box, invalid degenerate collider rejection without state loss, finiteness and reset; existing custom ellipsoid tests remain.
- **Tests actually run: NOT RUN** (remote GitHub source edit only). No compiler, GPU, exact-runtime or frame time claims. Use `bash tools/vivid-validate.sh` on native Mac, then `--full` after fixes.
- Limitations: mesh must be **closed**, in the same local coordinate system as the groom. This is a simple CPU O(strand-segments × constraint-passes × triangles) implementation with naive parity ray tests; ray-edge degeneracy, winding-independent odd-parity tests on non-manifold geometry, thin shells, moving/skinned colliders, backface contact, self-collision, neck/cloth integration and BVH are not production-ready. Root remains pinned, so if the imported closed mesh encloses roots, segment separation may be large; a scalp attachment/transfer rig is required. The Creator UI does **not** automatically pass imported model geometry into this opt-in API.
- Next bounded milestone 6I: BVH or signed-distance collision acceleration, coordinate-space/skin-pose adaptation, explicit collider mesh selection in Creator, tests for moving collision and deterministic dt subdivision. Following separate tracks: measured multilayer skin and corneal refraction with depth, strand hair BRDF, rig/morph/LOD authoring UI. Do not claim MetaHuman equivalence.


## Stage 6I — 2026-11-19 — DNA sclera color integration

- Branch: `feature/vivid-digital-humans`, main unchanged.
- Code commit `076b91bbd479db68343102d237e4461bd3515e27` (`HumanMaterials.cpp`): skin/iris/cornea/hair shader paths were already present; this bounded fix makes `HumanEyes::scleraRedness` affect the actual PBR sclera baseColor, rather than only the iris ShaderGraph's internal sclera fallback.
- Regression commit `636cc177b534fa68356ed72c87d6efb1fbd5c485` (`tests/vivid/human_appearance.cpp`): verifies green/blue attenuation for authored sclera redness 0→1.
- Actual compile/CTest/Metal pixel validation: **NOT RUN**. This is *not* measured skin diffusion, true cornea refraction or strand scattering; these larger shading targets remain open.
- Next: build on Mac, render matched eye screenshots, evaluate pupil/tear-film and cornea against licensed realistic reference assets, then consider depth-aware corneal refraction without changing existing opt-in PBR defaults.

## Stage 6J — 2026-11-19 — Creator facial channel authoring

- Branch: `feature/vivid-digital-humans`; code commit `d93ecd489ba84e0bc39e58eb6d15cfcfb0e8def7` (`tools/vivid-creator/main.cpp`).
- Creator includes an expandable per-channel authoring UI for the canonical 52 `Face.h` channels. Each drag commits via `HumanCreatorSession` and therefore participates in DNA save/load + undo/redo. The runtime installs DNA defaults in the Tracking layer before interactive jaw/blink/smile overrides; a reset button restores zero default channels.
- Contracts: canonical ARKit channel order unchanged; MotionWeave still owns node pose; `HumanDNA` serialisation schema version unchanged and existing `expressionRest` array reused.
- Actual native Creator compile, GUI and pixel acceptance: **NOT RUN**. No new dedicated UI automation or display screenshot exists for this change; the prior Stage 5J passes do not verify it.
- Limits: 52-channel authoring changes semantic weights, not the imported vertex-to-morph `ShapeRoute` map, skeletal bind pose, 3D morph sculpt geometry, shape LOD topology, or animation retargeting. Current Hair High/Med/Low buttons select pre-existing groom quality, rather than authoring production mesh LOD assets.
- Next: add visual inspector/editor for `ShapeRoute` and joint-to-morph binding, editable preview LOD and external asset publishing, then cross-backend tests. Never claim full MetaHuman Creator parity from channel sliders alone.


## Stage 6K — 2026-11-19 — bounded CPU triangle-collider BVH

- Branch `feature/vivid-digital-humans`; **no merge** into `main`.
- Implementation commits: `ae224b5f64650e5b1675ad9b8a0d135bb38e7673` (`HumanGroom.h` indexed BVH nodes) and `376f4adcd4adf08f8fd087e25face07e1284ebe6` (`HumanGroom.cpp` build/traversal).
- `setCollisionMesh` now rejects open or non-manifold topology using an undirected edge-incidence count (each edge must appear twice); only fully validated meshes replace the previous collision cache. Deterministic median-centroid axis splits produce a fixed leaf budget of 8 triangles. `resolveCollision` uses nonallocating BVH traversal for nearest surface and ray-parity inside/outside queries; the former brute-force triangle scan is retired.
- API unchanged from Stage 6H. `clearCollisionMesh` now releases triangle, order and node data. Legacy DNA ellipsoid remains fallback.
- **Tests actually run: NOT RUN** by request. Existing `vivid_human_appearance` source fixture contains a closed box collider; no native compiler/CTest or actual performance measurements have occurred for 6K.
- Limitations: this static BVH is rebuilt on collision mesh replacement; not refitted per skinned frame. Odd-ray parity near shared edge/vertex boundaries can still be ambiguous, and mesh must be closed. Root transfer/attachment, self-collision and hair/cloth collisions are not solved by this stage. Next: explicit user-specified scalp proxy selection via import manifest and Creator, with coordinate-space validation; then long-hair fixed stepping and authoring workflow.


## Stage 6L — 2026-11-19 — explicit imported scalp proxy hook

- Branch `feature/vivid-digital-humans`; `main` and MotionWeave unchanged.
- Commits: `80dbe63edcf9e23e9740ae8320c007a64bcdbc61` (`HumanImport.h`), `606be477d20349689ab9c975560d492f69fe48ca` (`HumanImport.cpp`), `53da1ca564571600b74b204d90f43e19ef60e61e` (`tools/vivid-creator/main.cpp`).
- Open manifest v1 gains optional `scalpColliderMesh` string. Validated and round-tripped independently of DNA/shape data, default empty preserves all existing manifests. Creator selects named mesh primitives from the imported CPU model, remaps their index subset to compact vertices, rejects missing/invalid/explicitly skinned meshes, then invokes the BVH collision loader before generating cards. Explicit proxy only: no attempt to infer a closed collider from a potentially open visual head, eyelid, teeth or haircard model. Existing authored hair disables procedural groom as before.
- Authoring requirement: the named proxy must be a **closed, nondegenerate bind-pose mesh authored directly in groom-local coordinates**; mesh node transforms are NOT baked by this hook. Animated head/scalp collision proxies, skinning or arbitrary coordinate conversion remain unsupported. CPU model vertex availability is required before renderer upload; unchanged after GPU publication.
- Tests/builds actually run: **NOT RUN** at user's request. No native macOS build, CTest, smoke preview or BVH performance claim. The first run must validate imported manifest with a real eligible proxy and avoid silent incorrect coordinate alignment.
- Next: add retargeted/scalp attachment mode with explicit coordinate transform, fixed timestep/substepping and controlled mesh refit; build full editing/publishing workflow for joints, shapes and LOD. UI still does not sculpt arbitrary face geometry, and measured multilayer SSS/cornea optics remain separate shader research.


## Stage 6M — 2026-11-19 — frame-independent groom stepping

- Commits: `f0369ff5617cfb3c3ef2e931752ebf0da8f9e780` (`HumanGroom.cpp`), `0e32259dc2724222fa41fc2cfd5d4772c1fa5bc8` (`tests/vivid/human_appearance.cpp`). Branch `feature/vivid-digital-humans`; main/MotionWeave untouched.
- `step(dt,wind)` now divides accepted variable presentation dt (at most 0.05s) into at most six equal intervals bounded by 1/120s. Damping is scaled by the actual substep duration, while the existing 5-iteration length/contact solver is applied per substep. This reduces frame spikes and keeps 30/60/120 Hz preview updates more consistent.
- Added (not executed) regression comparing a 1/30s call against four 1/120s calls with equivalent forces, verifying finite positions and near-equal results.
- **Actual tests: NOT RUN**, per user request to implement features before one final verification run. No claim of determinism across different floating-point architectures or of cloth/hair stability on full production assets.
- Next: if performance requires it, factor fixed substeps into an amortized accumulator with max catch-up budget and report their count in stats. Keep MotionWeave presentation-only ownership.

## Stage 6N — 2026-11-19 — imported morph-route authoring

- Commit `1edc7b64d12f3c51024547a159e7147ceaeda0b8` (`tools/vivid-creator/main.cpp`), import regression `5e9bdfe944b8c2e73ed619d831a7badeb8dfad6d` (`tests/vivid/human_import.cpp`).
- Creator retains the validated imported v1 manifest in memory and exposes an `Imported facial morph bindings` panel listing actual ARKit semantic channel → imported shape. Gains are adjustable from 0–4 and the scene rebuilds after editing. `Save imported manifest` validates and writes via staging plus atomic rename (no delete-of-last-good fallback). Rebuilds use the edited in-memory manifest rather than discarding changes by rereading the file. Morph vertex data and channels are not edited; only route gains.
- Import contract now includes optional `scalpColliderMesh` name and tests cover its roundtrip and invalid type. **Tests/build/GUI: NOT RUN**.

## Stage 6O — 2026-11-19 — explicit scalp root transfer

- Source commits: `b76b4acb83b0b655b416817f2de9f877ee20f68b` (new `attachRootsToCollisionMesh()` API), `5ee65ce212f8b0e4f039bb6f0ef56c52f4808a5e` (one-time closest-triangle root projection, outward normal and rest/position/previous translation), `ac000bfdc099d4aaec67a722b05598786d0837ea` (Creator hook), `2bfc39b3140777e145ba45d601fec948dc7a6618` (authored regression).
- Roots are attached to the authored closed scalp proxy before hair-card mesh creation, keeping each guide's offsets coherent. This is one-time bind-pose placement; BVH still handles per-frame collisions.
- Constraints: outward normal orientation uses mesh AABB centre, suitable for simple closed convex-like scalp proxies only. Complex concave/self-intersecting collider and moving/skinned scalp need robust signed-distance normals and per-frame skinning/refit. The one-time attachment still scans triangles per root, not an accelerated nearest traversal. Automatically generated cards do not replace externally authored hair.
- **Actual tests: NOT RUN**. Do not treat the implementation as integrated MetaHuman quality or production-ready until Mac C++ build, 16 CTests, actual imported closed proxy GUI preview and full-engine regression.
- Follow-up: build one small closed proxy fixture for Creator smoke, run `bash tools/vivid-validate.sh --full` once, resolve concrete diagnostics, then review gameplay-neutral rig/LOD mesh authoring and physically measured eye/skin/hair material parity.


## Stage 6P build-log triage — 2026-11-19 — animation budget public ECS API + groom warning

- Branch: `feature/vivid-digital-humans`; no main merge.
- Mac compilation report from user: `tests/animation/budget.cpp` failed because `AnimationBudgetPolicy.h:42` accessed private `World::poolIfExists<AnimationSignificanceComponent>()`. Separate compiler warning `HumanGroom.cpp:14` reported unused `clampFloat`.
- Fix commits: `3f2514d496b2e83169dd8032e5b82cbfc6e32d09` replaces private access with public `countExisting<T>()` and `eachExistingConst<T>()`, preserving non-registering read behavior and existing writable `prepareExistingWrite<AnimationBudgetComponent>()` budget handle. `e94368d8b61b5f278db36355516ce238dace1e16` deletes the unused `clampFloat` helper without changing groom runtime behavior.
- Scope: this is a compile-blocker fix in shared animation library plus VIVID diagnostic cleanup, **not** a claim of fully tested MetaHuman feature completion. Actual compilation after fixes: **NOT RUN** in this session; the supplied failing logs precede these commits.
- Next action: update the VIVID validation worktree to latest `origin/feature/vivid-digital-humans` and continue the previously requested one-pass build. Capture any subsequent compiler error without guessing success. No rewrite of ECS private interface or MotionWeave scheduler.


## Stage 6Q — 2026-11-19 — transactional animated scalp-root attachments

- Branch: `feature/vivid-digital-humans`; `main` and MotionWeave untouched.
- Changes: `10cd53325f7418efffe38ae3d9a3c77bc2da117f` makes `setCollisionMesh` publish all allocations only after successful staging and makes `attachRootsToCollisionMesh` atomically publish the entire guide array. `879de55dcedda5a989905d05f13995099d5dcf12` fixes scalp-proxy mesh name control-character validation. `7915e971848e71b00f95d7d4d8b9571dc026c669` adds negative input test.
- Barycentric animation source: `975565f37fa709966b1bed22bdc9d2e7c369b031` stores per-guide source triangle and barycentric location; `a799aa7de074fe2d12da05c9693ab3deb142b31b` captures these during root attachment and updates root/rest/previous/current poses when `refitCollisionMesh` receives a deformed scalp proxy, while staging both new BVH nodes and guides before publication. `5110d1e2da761b5ca30a70e6679036f9a4bf14c5` adds translation and failed-refit root stability regression; `a17258762abea5f09ba614c8a23e1bcc81232886` invalidates stale anchor triangle indices on collider replacement/clear.
- API: unchanged public `refitCollisionMesh(std::span<const render::float3>)` / `attachRootsToCollisionMesh()`; `GroomGuide` gains `scalpTriangle` and `scalpBary`. Existing code without an authored collider still falls back to a DNA ellipsoid.
- Actual build/tests: **NOT RUN** by user request. The C++ source, tests and docs are committed, but no native compiler verification or Metal GUI run has taken place since these changes.
- Known constraints: interpolation tracks triangle vertices, not a true skinned mesh deformation pipeline. Caller still must supply valid groom-local deformed proxy vertices, preserving topology. Closest-surface attachment normal outward orientation uses collider AABB centre and may fail on concave anatomy; self-intersection, severe deformation, hair–hair collision and real-time performance under large crowd counts are not solved. Creator currently uses bind-pose static imported proxy and does not stream skinned deformed proxy updates. Never claim skinned import fully complete.
- Next: implement explicit vertex-space/rig coordinate conversion and a well-defined deformed proxy feed from MotionWeave, plus Creator UI selection/validation and a real production head demo. Then advanced skin/eye/hair shading, sculpt/rig/LOD tools and once-only `tools/vivid-validate.sh --full` verification.


## Stage 6R — 2026-11-19 — Creator scalp-proxy authoring

- Branch `feature/vivid-digital-humans`, main unchanged. Code commit `4eb685ba2010c1057006197d294e415f9fbbbd0a` (`tools/vivid-creator/main.cpp`).
- The imported-facial-bindings Creator panel now has a `Scalp collision mesh` field. Its edits validate a scratch copy of `HumanImportManifest`; invalid values restore the prior selection, successful values update the in-memory manifest and request rebuild. The existing Save imported manifest action persists the choice to versioned JSON atomically. An empty field selects the DNA ellipsoid fallback.
- Contract: selected imported mesh is still required to be closed, nondegenerate, unskinned and authored directly in groom-local coordinates. The UI does not automatically transform nodes, drive skinned geometry, or author the closed proxy mesh itself. A supplied wrong mesh can fail Creator rebuild with a readable status.
- Actual tests: **NOT RUN** (user requested feature implementation before one combined native test pass). Previous Stage 5J acceptance is unrelated to new editor features. Follow-up: full Creator Metal GUI smoke with a real imported closed proxy and visual root motion, then full engine CTest.


## Stage 6S — 2026-11-19 — MotionWeave rigid scalp-proxy pose feed

- Branch: `feature/vivid-digital-humans`. No merge to `main`; shared MotionWeave scheduler and core animation architecture unchanged.
- Source commits: `97a921d261d13e613efb31c190674f12b839bc4f` (`tools/vivid-creator/main.cpp`) and `80df00d3639881f3a9bfb297c7bbed4eed4bf25a` (finite transform and vertex guards).
- Creator's imported closed `scalpColliderMesh` still requires exactly one unskinned rigid owning node. A `ScalpPoseBinding` now retains the bind-pose proxy positions, owning node ID, inverse bind node matrix and rigid head bind matrix. The live frame obtains node-world matrices **from MotionWeave**; an explicit relative transform `headRigid^-1 * poseNode * bindNode^-1 * headBindRigid` maps authoring-local bind vertices into the **current groom-local space**. With groom simulation enabled, these transformed vertices are fed to `HumanGroomRuntime::refitCollisionMesh` before stepping and updating dynamic card vertices. The groom continues to render with the same rigid head transform, avoiding double head scaling. Presentation only; no second ECS scheduler.
- Validation in source: missing owning node, multiple owners and skinned proxies remain explicit errors. Nonfinite pose matrix elements and transformed positions skip publication and keep the previous collider; the existing refit itself is transactional. Source geometry and render vertex IDs are unchanged.
- Tests **NOT RUN** by user instruction (feature implementation first, one test pass later). No Mac native C++ compile, Creator Metal GUI result, performance measurement or skinned-proxy visual parity for Stage 6S. Historic Stage 5J 11/11 must not be quoted as validation of this integration.
- Known limitations: source bind mesh must already use the documented groom-local convention; rigid node-only update cannot interpolate multiple skin joints, vertex-morph scalp changes, corrective skinning, self-collision or arbitrary imported coordinate conventions. No authored screenshot of an animated proxy was produced. The pose-matrix inverse helper has existing identity fallback on singular matrices, so production asset validation should explicitly reject degenerate input matrices.
- Next bounded stage: implement a separate CPU skinning adapter with per-primitive joint palette, normalized weights, skin.inverseBind and MotionWeave node matrices, preserving groom-local coordinates; wire an opt-in skinned proxy without modifying existing rigid import semantics. Follow with actual source compiler diagnostics, targeted tests and final `bash tools/vivid-validate.sh --full`.


## Stage 6T — 2026-11-19 — CPU multi-joint scalp skinning through MotionWeave

- Branch `feature/vivid-digital-humans`; `main` unchanged. No new independent animation scheduler or gameplay ECS mutation.
- Implementation commits: `2aa79bf72fcb52ff6e0cdcdc8944f459d786c742` new header `engine/character/vivid/ScalpSkinning.h`; `c3e33ae1ce458cb93d1e579048dec3f5afea9c74` `tools/vivid-creator/main.cpp` integration; `82a402c718c6f769b57a3db4012e1be98a9b08bf` linear palette compilation; `6d739919d5a459a7ae7578fde7157f690ef48650` checks for shared-vertex palette consistency; `f1324dc83b9a50ef6acac72e82664da7bd2f0212` UI status; regression source `8b1c9ad81cb068957cc2a0614b01835e0fd52745`, `57a37a0031c39711752c842edaf42b2e6feb6350`.
- New API: header-only `aiverse::vivid::ScalpSkinning::compile(model,meshId,nodeId,sourceIds)` builds bounded per-vertex four-weight palette mappings using each render primitive's joint palette and `Skin::inverseBind`. Rejects unknown skin/node/palette, unassigned/duplicate source vertices, zero skin weights, and conflicting shared-vertex palettes. `evaluate(nodeWorld,headRigidWorld,output)` calculates weighted world-space vertex positions and returns them in head-local groom coordinates, normalizing raw 8-bit weights and refusing nonfinite/invalid matrices without writing any output elements.
- Creator detects an imported scalp-proxy's single owning node. Both rigid and skinned node forms use the same `scalpColliderMesh` manifest property. Skinned mode compiles the adapter using compact-to-original vertex IDs, computes bind-pose head-local proxy vertices, installs the existing BVH and attaches hair roots. During each simulated groom frame, `facePose` (derived from MotionWeave) feeds `ScalpSkinning::evaluate` and the existing transactional `refitCollisionMesh`, then the cards are stepped and updated. Rigid node mode remains unchanged. The imported inspector reports rigid vs multi-joint CPU mode.
- Regression code authored but **NOT RUN**: synthetic 2-joint, 2-weight skinned proxy expected weighted translation, rejection of nonfinite world pose without output changes, and rejection of invalid joint palette slots. No Mac compiler, CTest, OpenGL/Vulkan GPU, Metal GUI or actual skinned production asset visual check for this stage.
- Constraints and risks: CPU linear blend skinning only, up to four weights, one owning node, closed collision proxy and original topology stable across frames. Real production joint palettes / vertex index ranges require native compiler and actual GLB/FBX validation. Deforming proxy geometry still uses expensive CPU refit and strand simulation. No dual-quaternion skinning, cloth contact, hair self-collision, GPU scalp skinning, realistic skin/eye/hair physical optics, or full rig/shape sculpt/LOD authoring has been completed.
- Next step: implement cross-frame scratch buffer reuse and no-allocation evaluate, inspect true model coordinate mapping/skin bind transforms with actual imported rigged GLB, extend creator acceptance to skinned scalp and high-density groom, then finish the separate photoreal BRDF/sculpt/LOD modules. User wants one full test run **after** implementation, not between stages.


## Stage 6U — 2026-11-19 — reusable CPU scalp skinning scratch

- Branch `feature/vivid-digital-humans`; code commits `065466c0f7b9170429e1befe570843af2f87a1ea` (`ScalpSkinning.h`) and `fff2951e9c1e8b38cfb90ac997b3e148b1883399` (`tools/vivid-creator/main.cpp`).
- New runtime overload `ScalpSkinning::evaluate(nodeWorld,headRigidWorld,output,scratch)` receives exactly one preallocated scratch position per proxy vertex. It computes there first and only commits to `output` once all positions are valid, preserving failure atomicity. Existing 3-argument convenience overload remains source-compatible but allocates scratch on demand. Creator sizes persistent `scalpSkinScratch` on human rebuild and reuses it each groom frame; skinning no longer allocates its per-vertex scratch vector each frame.
- Actual Mac build/CTest/GPU GUI: **NOT RUN**, per user instruction. No timing or performance speedup proven.
- Remaining: animated real asset validation, bounded GPU/CPU performance metrics, correct model-to-groom space in arbitrary imported model scenes, full artist rig/morph/LOD authoring, realistic skin/eye/hair optical layers.


## Stage 6V — 2026-11-19 — non-destructive sparse morph vertex sculpting

- Branch: `feature/vivid-digital-humans`; `main` and MotionWeave untouched.
- Implementation commits: `b763c1a58bb7729a6fd685939345ebd66f3385e4` introduced `engine/character/vivid/MorphAuthoring.h` with bounded transactional sparse position edits, 32-step undo/redo, CSR recompilation and JSON export; `88f03dc938db973fef19cf5509f165e4c864beab` + `7e9ac10748035f1aa63952ba2b089d1c25ec0a89` expose stable original imported vertex count; `a6ee4695689e6f6c98a7dfeab96a3f6181dff01d` prevents generated LOD vertices from being published to original mesh's morph source; `3a6e0a94211b8080e0311cff74b66772b1fa26d5` integrates editing + sidecar export into Creator; `11d6f5307a5617b2c323c8a8718eeceb2e2b9ccc` added source tests.
- New public API: `MorphAuthoring::fromCompiled(SparseMorphLibrary)`, `setPosition(target,sourceVertexId,vec3)`, `undo()/redo()`, `compile()`, `toJson(originalVertexCount)`. Edits are sparse deltas, never destructive changes to imported source `render::Model::vertices`. Invalid target/index, excessive/nonfinite deltas and CSR compile failures preserve the previous data. JSON sidecar stays compatible with the existing open `parseHumanMorphs` schema (version 1).
- Creator: an imported-human `Sparse morph vertex sculpt` panel selects target and source vertex index, reads current position offset, edits XYZ, applies/undoes/redoes, and renders through existing GPU VIVID actor rebuild. Export writes the morph JSON to the package manifest directory using a staged atomic rename; if the package originally had no morph sidecar, it uses `vivid-authored-morphs.json` and updates the in-memory manifest path. User must then Save imported manifest to publish a newly introduced sidecar path. External mesh itself is not overwritten.
- Limits: this is **vertex-ID numeric sculpting**, not mouse/brush picking, falloff, symmetry, normal/tangent recomputation, cage deformer or a complete DCC replacement. Rig pose authoring and LOD transfer remain separate stages. In a package with no existing morph targets the panel currently cannot create a brand-new named target. No new engine-wide RHI API or genre dependency was introduced.
- Tests **NOT RUN**, per user request to finish features before a single combined run. `tests/vivid/human_creator.cpp` now has authored tests for edit/undo/redo, source JSON roundtrip, invalid index/value and rejection of generated LOD vertex IDs. No native compile, CTest or Metal GUI verification yet.
- Next action: support named target creation/deletion, viewport ray/mesh picking, brush falloff and symmetry, introduce non-destructive rig bind-pose edits, and add authored LOD source→target morph transfer. After implementation, run `bash tools/vivid-validate.sh --full` once and record exact SHA/logs.


## Stage 6W — 2026-11-19 — named morph targets and semantic route authoring

- Branch: `feature/vivid-digital-humans`. `main` and MotionWeave untouched.
- Source commits: `6361e86fabf3fec0ecbeeaced363d172455c2053` adds `MorphAuthoring::addTarget(name)` / `removeTarget(index)` with validated transactional 32-step undo/redo in `MorphAuthoring.h`; `8ffa1e101ea0691f2afd875006c7bb0ace2b76d9` adds Creator actions; `695e5ad49761e4d6901cf7adb7d844bf6f1c3076` guards against invalidated selection and permits empty morph export. `45a525f1f71cc60247cb2bbf4912a00d6efa6e9f` adds a semantic ARKit channel-to-target route action; `4998087ad50e1bc4056c95610dc14ea78622928c` removes mappings that reference a deleted target. Test commit `43e970e19b22609979505dd5d9054838ee664d49`.
- Functional flow: Creator now creates a named empty corrective target, selects its source vertex, edits position deltas, and binds it to any of the fixed 52 semantic facial channels. Existing FaceShapeBinding compilation routes new targets to VIVID GPU weights on the next preview rebuild. Deleting a target drops its routes to avoid invalid compile; users can save the changed morph JSON and separately save the package manifest. New route gain defaults to 1.0. The original imported head topology remains immutable.
- Limitations: source vertex numeric editing (not 3D brush/cage sculpt); deletion undo restores the target but does not automatically recreate deleted route entries; remember to rebind as needed. A created empty target is valid and may render no visible deformation until deltas and a channel route are authored. No bone/joint bind-pose transforms, source mesh editing, or facial LOD transfer in this stage.
- Actual tests: **NOT RUN**, per user request to postpone one-shot native validation. New checks in `tests/vivid/human_creator.cpp` cover named target creation, duplicate rejection, deletion, undo/redo. Native C++ compile and visual Creator behavior are not yet verified.
- Next bounded task: add symmetrical/falloff source-mesh vertex editing and a real viewport picking workflow, then a separate rig bind-pose editor and morph LOD transfer. Once all functionality is complete, run `bash tools/vivid-validate.sh --full` and record real diagnostics; never claim unexecuted tests passed.


## Stage 6X — 2026-11-19 — non-destructive source-space falloff brush

- Branch `feature/vivid-digital-humans`; `main` and MotionWeave unchanged.
- Source commits: `689ce6fcdae59984b88290a77dcde22a49b8eccb` adds `MorphAuthoring::brush(target, sourcePositions, centerVertex, radius, offset)`; `82a3295084287c92be8322845782ca02e3bbd02f` adds brush radius/Apply falloff brush to VIVID Creator and snapshots original imported source-vertex positions; `e11044fece3d4b806584f64945fe3779db869f62` explicit iterator include; `14d05f041a0791d2e68b57cae567d1fbaf99ce2e` adds unit test source assertions.
- Each stroke applies smooth spatial falloff around one selected source vertex ID, editing position offsets of nearby vertices in one validated `SparseMorphLibrary::compile` transaction and one undo history record. Original imported geometry and source vertex IDs are not rewritten. The resulting morph is displayed on existing GPU actor regeneration, and can be exported in the open JSON sidecar format.
- Input policy: finite source positions/offsets, positive radius at most 10 model-space units and per-axis offset magnitude at most 10. The UI offers a narrower 0.001–2.0 radius slider. The original imported vertex count bounds edit scope; generated model LOD vertices are not touched.
- Limitations: brush is centered by source vertex ID, not mouse picking; it does not yet use topology-aware geodesic distance, normal-direction displacement, symmetry plane or local surface tangents. It does not recalculate explicit morph normals/tangents or offer a full DCC sculpting viewport. Multi-actor GPU evaluation and full 3D artist workflow are not certified.
- Tests actually run: **NOT RUN** as requested. New `vivid_human_creator` assertions cover falloff affecting adjacent vertices, invalid radius/offset and stroke undo/redo. The entire VIVID branch still needs native C++ build, real Creator GPU capture and full AIVERSE regression once implementation is finished.
- Next: viewport ray picking and brush gizmos; non-destructive rig bind-transform authoring and authored facial LOD transfer; physical skin/eye/hair shading refinement. Preserve one handoff per stage and avoid claiming parity or test success prematurely.


## Stage 6Y — 2026-11-19 — 3D sculpt triangle picking and X symmetry

- Branch: `feature/vivid-digital-humans`. `main`, MotionWeave scheduler and original imported mesh remain unchanged.
- Source commits: `e5ea3bb0c0fbd80cd2cad95c4dde85b3692d8aff` new `engine/character/vivid/ViewportSculptPick.h` CPU ray/triangle hit test; `2e9dbbd6002ad5e6cc4afec82fe8dd1fb8068f07` Creator select-mesh + arm-click + world-space selection marker; `300cdd08e18de1ddfaefcbcf4d60c5d0df0f4935` symmetric sparse brush implementation; `81009dcbaefba69c8192eceee9993eb36c6e8725` Creator mirror toggle; regression commits `7273988c8d0b2d481d580921f5daafffac229c2b` and `f0940557f4b1d64b3b70e19dd41c32cbd1d63a2a`.
- `pickSculptVertex(model, meshId, nodeWorld, origin, dir, sourceVertexCount)` traverses indexed triangles of the selected model mesh, intersects a world-space camera ray with both triangle winding directions, selects the closest positive triangle hit, then returns its nearest **original source vertex** ID. GPU-generated LOD vertex IDs are not selectable. It does not mutate `render::Model`. Creator uses `Camera3D::screenRay`, real facial-overlay/MotionWeave node-world matrices, and only accepts a click when ImGui is not capturing the pointer. Screen-space ring marks the selected vertex.
- `MorphAuthoring::brush(...,bool mirrorX=false)` uses the source-mesh X=0 plane for optional bilateral brush symmetry; horizontal displacement flips sign on mirrored side, vertical/depth displacements keep sign. A bilateral stroke is one validated CSR transaction and one undo step; near the symmetry plane the duplicate application is suppressed. Existing call sites keep working via default argument.
- Tests written, **NOT RUN** per user instruction. New `tests/vivid/human_creator.cpp` source assertions cover front-facing triangle hit, ray miss, matching mirrored deltas and single-step undo. No C++ compiler, CTest, Metal GUI capture or macOS success is claimed for this stage.
- Known limits: UI currently selects a mesh by name, not a per-material surface mask. CPU ray picking uses undeformed bind vertex positions transformed by node matrices; it is **not exact for skinned/GPU-morphed vertices** and can disagree with displayed geometry. Mesh with many triangles uses linear traversal and may need BVH. Mouse coordinates are taken from ImGui logical space with high-DPI disabled in this standalone Creator; different display scale needs validation. Brush uses Euclidean source-space distance rather than geodesic topology distance. It does not perform normal/tangent regeneration, full sculpt tool gizmo, auto-symmetry landmark mapping, rig authoring or LOD-transfer publication.
- Next: rig local bind-transform authoring (non-destructive, versioned manifest), source-to-LOD morph transfer, followed by high-quality skin/eye/hair optics. Once functional stages are complete, perform one `bash tools/vivid-validate.sh --full` native Mac acceptance and append real compiler/visual logs.


## Stage 6Z — 2026-11-19 — non-destructive rig-pose correction and explicit morph LOD remap

- Branch `feature/vivid-digital-humans`; `main` unchanged. No new animation scheduler; MotionWeave pose is the immutable input.
- Code commits: `da08f418d679dde5d871a87e6c66b167a97de07b` new `engine/character/vivid/RigAuthoring.h`; `7d65b0da2af8d54037a63f82ac0c11628cf03628` fixes equality handling for float3 edits; `1806dcd61a61c676d3b101ce2adb834418c7f5ce` versioned JSON roundtrip; `5da316a0f7313039759ffd079c3c61a12e58b36c` Creator rig correction inspector with position XYZ, node selector, undo/redo, JSON staging and atomic rename; `e5254f169e918715cdd35ac6b62c8c5dce48b98b` regression source.
- `RigAuthoring` stores named local-space translation corrections with a 32-deep history. The `apply(model,motionPose,out)` stage copies matrices, composes a correction for a selected bone and its descendants, and preserves the input MotionWeave buffer. A singular/missing transform rejects the overlay. Creator applies the correction to the already face-composed presentation matrices. This is a **pose offset**, NOT actual editing of the imported skeleton bind-pose or inverse-bind matrices; it does not republish skin weights nor rebind the original character.
- LOD source: `3be2152433145c235616b80ecba27cb363ab98e8` new `engine/character/vivid/MorphLodTransfer.h` takes an explicitly authored `lodToSource` vertex index mapping and projects all sparse targets to a reduced vertex stream, rejecting unknown source IDs. Test source commit `d35763fe9e7af4ff06c6588ffe52acb15b37bad8` checks mapped sparse corrective and invalid ID.
- Test status: **NOT RUN** (implementation-first request). Source code and test cases have been authored but no native Mac C++ compilation, Metal Creator GUI validation, CI result or end-to-end LOD render has been verified. Historic 5J 11/11 passes do not apply to the new code.
- Remaining: true bind pose / skin rebinding and inverse-bind authoring; persisted project-owned rig corrections rather than standalone sidecar; automatically deriving accurate source→LOD correspondence; editor LOD mapping UI and GPU LOD switch; high quality skin/eye/hair shading and groom self-collision; release acceptance. The existing engine's auto-LOD generator is not a guaranteed morph-preserving source vertex map, and must not be treated as one.
- Next: implement a bounded LOD correspondence authoring utility with geometry support/strict tolerance and editor import/export; integrate correct local/world skin spaces; then one `bash tools/vivid-validate.sh --full` on Mac after features are complete, fixing the first observed compiler errors.


## Stage 6Z follow-up — strict geometric LOD correspondence

- Code commits: `4c72c0684fd4f857741e21db7f2d81816421ed3e` added `MorphLodTransfer::matchPositions(source,lod,tolerance)`; `8cc8287aec2e8c54c464147420467e9fa0229e74` caps diagnostic O(source×LOD) search to 32K vertices per side; `cea5928bb4c18b0447567d99182889b4bceb6886` added an authored exact-coordinate mapping regression.
- This is a **fail-closed reference matcher** for identical/near-identical vertex positions, not a production LOD baking/reprojection algorithm. It rejects no match, ambiguous source match, nonfinite coordinates and excessively large inputs. It can only transfer sparse facial morphs between meshes sharing actual source positions; arbitrary decimated LODs with different vertex positions need barycentric surface correspondence.
- Tests **NOT RUN** (source assertions only). Next: implement explicit surface projection correspondence and GPU pipeline validation after the larger functional roadmap.


## Stage 7A — 2026-11-19 — barycentric surface-projection morph LOD baking

- Branch: `feature/vivid-digital-humans`; `main` unchanged, MotionWeave remains the only animation owner.
- Implementation commits: `75ca966e8bde90777d49be8c2677f0d4a9c56f06` adds `engine/character/vivid/MorphLodSurface.h`; `d7ff9f8b2f07fc3f444843119bfe3817f0c3d849` caps projection work at eight million point/triangle comparisons; `783efdb994bf24ac85a8b6bab7a1e0e2c8a5c792` adds source regression assertions in `tests/vivid/human_creator.cpp`.
- API: `MorphLodSurface::project(sourcePositions,sourceTriangleIndices,lodPositions,maxDistance)` finds the closest source-triangle point and retains a source triangle index plus barycentric coordinates for each independently decimated LOD vertex. Rejects malformed indices, degenerate triangles, non-finite data, oversized inputs and LOD vertices outside strict model-space tolerance. Tie resolution is stable by first smallest-distance triangle. `MorphLodSurface::transfer(sourceSparseMorphs,sourceIndices,mapping)` interpolates position/normal/tangent sparse deltas from all three triangle corners per target, preserving target names, and recompiles a new LOD-owned `SparseMorphLibrary`. Existing exact vertex transfer remains supported.
- This supports a **new LOD vertex inside or near an original triangle** and does not require equal vertex positions. It is a portable CPU reference authoring tool, with a strict 8M triangle-check budget. It does not automatically author a new LOD mesh or insert LOD-target switching into Renderer3D.
- Tests **NOT RUN** as requested. New assertions check barycentric weights and interpolated corrective displacement for a point inside a triangle, plus tolerance rejection. No CMake/CTest, Metal GPU or visual LOD switching has been executed for Stage 7A.
- Limitations: current authoring projection is brute-force bounded, not a BVH. It can map vertices across adjacent but semantically incorrect surfaces if those surfaces overlap spatially; facial region/material constraints and triangle normal alignment are needed for production. Retargeted morph tangents/normals are numerically blended but not yet renormalized or reconstructed from a LOD surface differential. Scene transforms/skinning and duplicated UV seams need explicit consistent local-space authoring.
- Next: efficient BVH projection and region-specific matching; persist source triangle/barycentric maps as versioned LOD sidecars; integrate Creator selection and generated CSR per authored LOD; verify full functionality once through `bash tools/vivid-validate.sh --full` on macOS.


## Stage 7B — 2026-11-19 — versioned facial LOD correspondence sidecar

- Branch `feature/vivid-digital-humans`; `main` and MotionWeave unchanged.
- Source commit: `7c4d2a2e03f22853d5dbad70366cf8e020c678bc` adds `engine/character/vivid/MorphLodMapping.h` with versioned `MorphLodMapping`, `toJson()/fromJson()` and `transfer()`. Test source `e50a3ca0dd9a8772087dcdc016d892f73a693c62` extends `tests/vivid/human_creator.cpp`.
- The sidecar preserves source vertex count, triangle count and per-LOD barycentric source triangle/sample records. Data is strictly bounded, checked for finite barycentric weights/nonnegative distances, canonicalized by JSON serialization, and checked for source dimensions before reusing it with a `SparseMorphLibrary`. Stored projection avoids rerunning Stage 7A's costly geometric correspondence search on every import.
- Source authored; **no tests executed** (user explicitly deferred build/CTest). Native C++ compile, macOS GPU preview and asset-based reimport verification are still pending. Do not count source checks as passing CTest.
- Limitations: this format binds only to source *counts*, not a topology/content fingerprint, so two different meshes of identical size could be confused. Production loading must add an immutable mesh hash or authoring asset ID; stage 7B is a reference implementation and should not silently accept unrelated meshes. Projection authoring is currently bounded CPU brute force; no accelerated BVH, no adaptive LOD switch/render integration and no full photoreal asset benchmark.
- Next: add source topology fingerprint and safe sidecar load/save in Creator, source surface/material region constraints, accelerated triangle closest-point BVH, and actual per-LOD Actor/GPU deformation publication. Then one final `bash tools/vivid-validate.sh --full` Mac run after the full feature scope is implemented.


## Stage 7B follow-up — source-geometry fingerprint correctness gate

- Source commits `1c6040e26cd23d47c37de9b274d373d41013ed08` (`MorphLodMapping.h`) and `38293a18ae38378f509cc6d4295b174f65933c76` (`tests/vivid/human_creator.cpp`).
- Mapping now carries a required 16-character lowercase hex FNV-1a 64-bit fingerprint over exact source vertex float bits and triangle vertex IDs, along with source mesh counts. The same fingerprint must be recomputed from original positions/indices on reuse. `MorphLodMapping::transfer` now requires sourcePositions as well as CSR and source indices and rejects a stale mapping even when mesh counts still match. Malformed/missing fingerprint rejected on JSON import. Test authored for original geometry parity and same-size modified geometry rejection.
- Fingerprint is for accidental asset mismatch detection, **not cryptographic authenticity/security**; a deliberate collision is theoretically possible. Float-bit exactness also means harmless source reserialization changes require a new bake. The format is v1 during this development branch; earlier un-fingerprinted draft sidecars are intentionally incompatible.
- No tests or macOS builds have run for this follow-up, per implementation-first instruction. Next: add GPU LOD publishing and Creator mapping import/export; benchmark accelerated projection, then one native all-feature CTest/GUI acceptance.


## Stage 7C — 2026-11-19 — accelerated BVH facial LOD baking and Creator export

- Branch `feature/vivid-digital-humans`; `main` unchanged. VIVID continues to use MotionWeave as its only skeleton scheduler.
- Implementation commits: `6c06d5a3cd8ed5dd49d7bbbf4d86ee51dd340f50` adds median-split triangle BVH accelerated nearest-surface projector `MorphLodSurface::projectAccelerated`; `fe626a6d12b717ffcf9ac947b5b17affb20d360d` reuses traversal stack across target vertices; `fa3842d4246235949873549a95f8b4ce64e92843` exposes Creator source/LOD mesh selectors, projection tolerance, bake and two versioned JSON exports; `a80e1fc99755585c41ec94219bb041d27005690a` extends `MorphLodMapping` with stable sorted model-relative target vertex IDs; `72696836e02608a0a24a2f7c46a5de2e9786c2f1` sets those IDs from Creator.
- Test source commits: `a4b37a8731c6423da2c680a2cd288a5d1818c48d` asserts BVH/scalar closest triangle and barycentric mapping parity on simple source faces; `d56178ce771ad74b2be8ff2e76fc5433d3ab306f` checks model-relative target ID roundtrip. **Tests actually run: NOT RUN** (user postponed all functional validation). No compiler pass, benchmark, Metal GUI acceptance or GPU LOD-switch result is claimed.
- BVH implementation: bounded source vertices (1M), triangles (2M), target vertices (1M); validates finite geometry/indices/degenerate faces, builds deterministic median splits with small leaves, traverses AABB nearest distance and resolves closest original triangle using existing barycentric `closest`. Bakes `LodSurfaceSample` usable by Stage 7B fingerprinted sidecar and `MorphLodSurface::transfer`. This removes the old quadratic hard 8M-comparison cap from the **new accelerated path** while preserving the simple reference path for parity.
- Creator: `Facial LOD surface bake` inspector lets artist select separate source face mesh / target LOD mesh, provides tolerance slider and runs BVH projection for selected triangle sets. Target IDs are unique sorted model-relative vertex indices. It writes `vivid-facial-lod.mapping.json` (source dimensions, fingerprint, barycentric samples, target IDs) and `vivid-facial-lod.morphs.json` (versioned CSR converted to source-independent sparse morph JSON). CPU asset bake and sidecar publication only; not yet an integrated GPU LOD execution/switching mechanism.
- Limitations: model-space coordinates must match across the selected source/target meshes. The mapping does not yet carry target model topology fingerprint/mesh ID; target ID arrays are only valid against a retained identical model. Creator's current two-file save is not a multi-file atomic transaction: a failure on the second rename can leave one new file and one old file. CPU BVH build/search needs actual performance acceptance; no surface region/material/normal filtering, no seam-safe projection and no final renderer integration. Target files use fixed names in package directory (multiple LODs need distinct names).
- Next: make sidecar output transactional as a publishable package with target source identity; add numbered multi-LOD authoring and GPU actor stream selection/LOD policies, safe synchronization, and verify end-to-end skin/morph correspondence. True bind-pose rebinding, realistic optics and groom self-collision remain distinct unfinished stages. Finish feature implementation before the user's requested single `bash tools/vivid-validate.sh --full` native Mac run.


## Stage 7D — 2026-11-19 — authored facial LOD morphs in full-model GPU actor stream

- Branch: `feature/vivid-digital-humans`; `main` unchanged. No new animation scheduler, no dynamic per-frame GPU actor teardown and no shared Renderer3D LOD rewrite.
- Implementation commits: `4cf8f7d8f0014804e18b154ae1a0dc536c9918c8` introduces `engine/character/vivid/MorphLodGpuAuthoring.h`, `d0405065ac82d9afe860ccdbd0deb4a508c4911c` integrates baked facial LOD morphs into Creator's prepared `GpuActorStream` cache/initialization path, `b0b7070c5759c06a53706a64ef13d5805affcee5` fixes stable source-prefix acceptance when model upload preparation appends LOD vertices, `df0a37bbe1948e9039e82fb498f35e8e61dd7566` adds validated LOD mapping sidecar reimport, `5331f4a1aab27b205e0937ff0aeb9c7e245dda1e` accelerates overlap detection. Regression source `da7333ee0b39c0826b44a0b18559be8dafb7ac3b`.
- `MorphLodGpuAuthoring::combine(fullCSR,compactLodCSR,sortedModelTargetIds)`: requires identical semantic morph target names/order; maps each compact LOD delta into the existing full-model vertex address space, appends original model deltas, and recompiles CSR with *the same target count*. Fails closed for out-of-range IDs, unsorted mappings, name changes, or overlapping nonzero source/LOD morph deltas. A shared semantic weight now deforms both original and LOD vertices in one GPU dispatch. This exploits existing Renderer3D model LOD selection for authored meshes; **there is no new per-frame GPU buffer hot-swap**.
- Creator: after explicit Stage 7C bake, keeps the fingerprinted `MorphLodMapping` plus indexed source face triangles, marks `Publish LOD morphs to GPU actor` enabled and schedules a normal scene rebuild at the safe frame boundary. The rebuild re-evaluates current source sparse morphs, validates original geometry fingerprint, transfers corrections onto compact target-LOD stream, merges full-model CSR, recompiles the existing ARKit `FaceShapeBinding`, then acquires a revision-specific GPU asset from `GpuMorphAssetCache` and initializes one `GpuActorStream`. On-disk `Load baked LOD mapping` checks original fingerprint and exact target vertex ID membership before enabling. The inspector can disable publication with another rebuild. Active changes are never swapped mid-render.
- Existing `HumanImportedAsset::sourceVertexCount` is used to hash only the imported stable original prefix; source sparse CSR may legitimately cover appended prepared vertices and is accepted as long as its vertex count is at least the original count. The mapping captures sorted target vertex IDs, not merely a triangle ID range.
- **Tests actually run: NOT RUN**, as requested (feature work first). New regression source verifies combined CSR has both original and LOD deltas and rejects an actual overlap. No native C++ build, Metal GPU execution, real animated imported character or authoring GUI acceptance is claimed.
- Limitations: supports **one selected authored LOD mapping per VIVID Creator character** at present, not a complete many-LOD chain; automatic Renderer3D LOD behavior is reused and must be verified with real authored `MeshDef::lods` metadata and model vertex-override compatibility. If LOD uses source vertex IDs with nonzero morph offsets on both surfaces, combination rejects overlap rather than silently using the wrong deformation. The selected target mesh is still an existing part of the Model; no mesh decimation or per-LOD skin palette generation is performed here. Stage 7C output is two separate atomic renames, not one multi-file transaction. Cache lifecycle relies on the existing GPU idle barrier on Creator rare rebuild, and must be profiled/validated on Metal. Actual runtime Vulkan/GLES VIVID GPU morph parity is not implemented by this Metal-only actor path.
- Next tasks: multi-LOD chain authoring with unique file identities and two-phase publication; actual Renderer3D LOD visual/GPU regression; cross-backend VIVID compute portability; rig bind-pose skin rebinding, hair self-collision and measured skin/eye/hair optics. When all functional milestones are complete, run exactly the user's requested native Mac `bash tools/vivid-validate.sh --full` and report first observed diagnostics.


## Stage 7E — 2026-11-19 — multi-LOD facial GPU CSR authoring

- Branch `feature/vivid-digital-humans`, `main` untouched. MotionWeave remains sole animation scheduler.
- Changes: `8b95901e2762ac55aa24a5c2f7765ca4465f635e` adds `MorphLodGpuAuthoring::combineMany(fullCSR, span<Layer>)`, validating up to eight compact LOD mappings and creating one full-model CSR across every authored LOD with common semantic target ordering and shared GPU weights; `e58287d025cb8428682867b4260cbfeb6495af9d` makes Creator keep multiple authored LOD maps and combine them at its normal safe GPU idle rebuild; `ed11d4e412ad4c229f78eb51ae8bf44f1df147f8` publishes distinct `vivid-facial-lod-{targetMesh}.mapping.json` and `.morphs.json` files, reimports by selected target mesh and provides a Remove level control; `23a11cb8f8dada232f42e7a2202660fb95e6db67` adds regression assertions for two LODs and overlapping target delta rejection.
- The existing `GpuActorStream` continues one compute dispatch across a model-wide vertex buffer. No GPU output buffer is swapped or destroyed during rendering; Renderer3D's preexisting authored `MeshDef::lods` still determines visible mesh and its vertex ranges.
- The authored chain is limited to eight LOD layers and requires each selected target mesh to contain a disjoint set of model-relative vertices for nonzero morph deltas. The authoring library rejects any duplicate destination vertex for the same target, incompatible semantic target ordering/names and out-of-range/unsorted compact maps.
- **Tests actually run: NOT RUN** (user requested complete functional work before one native validation). No Metal image parity, LOD switching footage, compiler pass, GPU performance timing or Vulkan/GLES backend confirmation is claimed.
- Gaps: Creator still saves mapping and morph sidecars in separate atomic renames rather than a manifest-level multi-file transaction; external asset import does not automatically discover all LOD sidecars (user selects meshes and loads each), and the selected model must already have a valid LOD chain in Renderer3D. Need target-geometry fingerprint, per-mesh region constraints, explicit LOD switch debug view and visual parity. Existing GPU actor compute remains Metal-only; real Vulkan/GLES parity outstanding. Retargeting, hair self-collision and advanced optics still unfinished.
- Next: add manifest-indexed multi-LOD package discovery and two-phase atomic manifest publication, then exercise genuine ModelDraw viewport LOD selection and cross-backend compile tests at final QA. Do not mark this as production-ready until native validation.


## Stage 7F — 2026-11-20 — manifest-indexed multi-LOD package discovery and fail-closed publication

- Branch: `feature/vivid-digital-humans`; `main` unchanged. MotionWeave and shared Renderer3D untouched.
- Source commits: `c61d8fee1a162e5d21180ebb831ce04700111d10` adds `HumanLodEntry` (`meshName`, `sourceMeshName`, `mappingPath`) and up-to-eight `facialLods` entries to `HumanImportManifest`; `7adf91ebd2fdf4a031e60c9b1adf03760a021f21` adds strict import JSON roundtrip and duplicate/path/name/limit validation; `5702eadb511c0f0a000fa8b0af7995005ed737f7` adds automatic indexed LOD sidecar discovery and complete cross-layer GPU CSR preflight in Creator; `e9903ac04979c26a97b0bebae4aa27b5270a48f5` stages original positions rather than mutating live state before preflight; `06ff7610d6d5900da2908c0e57f4725bf3d2d9df` also defers the Creator DNA/session update until package LOD validation passes.
- Publication commits: `735ceb236b95dd51325ee87501ead0b81dfa136b` creates uniquely named content-addressed immutable `vivid-facial-lod-{meshId}-{digest}.mapping.json` and `.morphs.json` sidecars, then publishes an indexed manifest **last** by staged rename. Existing sidecar paths are never overwritten with different contents. A failure before the final manifest rename leaves the previous indexed package readable and may leave unreferenced files for later cleanup. Requires source morph changes exported before publishing. `a6be3002874fe937c573aeaf681c966033759376` makes Remove level also rewrite the manifest last and manual reimport select the indexed mapping filename instead of an old fixed filename.
- Creator auto-loader: after importing the original Model and morph shapes, follows each `facialLods` manifest entry, locates named source/target meshes, validates original source triangles, target vertex IDs, mapping fingerprint, and all cross-LOD combined sparse CSR destinations. Either every indexed LOD is staged, or import raises an explicit error; it does not partially enable the GPU actor. Base/LOD semantic target layout remains common and GPU retirement/rebuild behavior is unchanged.
- Regression source `30e560a1a0b2eef3f2ff57d77bdf45dcf36036f8` adds manifest roundtrip, duplicate entries, unsafe mapping paths and more-than-eight-level rejection to `tests/vivid/human_import.cpp`.
- **Tests/build actually run: NOT RUN**, as requested. Native Mac C++ compile, GPU Metal visual LOD switching, Creator import/reimport interaction, Vulkan/GLES compute parity, and performance are not verified for this stage. No pass claim should be made.
- Constraints: manifest index is published by one atomic rename on a local file system, **not** a fully atomic multi-file filesystem transaction; immutable sidecars are prerequisites. Generated filenames use a non-cryptographic 64-bit content digest; collisions are detected by comparing existing bytes. The package can accumulate orphaned old sidecars after new versions or Remove level. Target geometry still needs its own content fingerprint beyond sorted vertex IDs to protect against same-index geometry edits. Native Renderer3D already supports LOD selection, but exact source/LOD shader and skin parity have not been visually tested.
- Next: source/target mesh geometry identity and reusable sidecar garbage collection, actual GPU LOD visual parity on Mac, Vulkan/GLES GPU morph execution instead of Metal-only actor, true bind-pose skin rebinding, advanced optical skin/hair/eyes and groom self-collision. User requested one full `bash tools/vivid-validate.sh --full` only once feature scope has been completed.


## Stage 7G — 2026-11-20 — remove Metal-only GPU actor backend gate

- Branch `feature/vivid-digital-humans`; `main` untouched. Code commit `2c783d141ebd94c4653e62dff5fb8e9dcadab32a`: `engine/character/vivid/GpuActorStream.cpp` no longer rejects all non-Metal devices based only on backend enum. Instead it validates that the immutable GPU shared asset belongs to the current gfx::Device, that the device reports `supportsCompute()`, is healthy, and has a usable frame serial. Existing frame-safe, single-dispatch, two-pass GPU stream packing and retirement are unchanged.
- Existing `VividComputeShaders.h` and `GpuMorphMeshShaders.h` already offer GLSL 4.30 compute along with Metal shader strings; `GpuMorphSharedAsset` uses the standard `device.createCompute({msl,glsl,...})` interface. This stage removes VIVID's own artificial gate, **not** a missing Vulkan backend implementation. It only activates on actual RHI devices that advertise functional compute, frame serial and resource barriers. Vulkan shader compilation, bindings, storage/vertex synchronization and readback have not been tested. Do not claim Vulkan parity.
- Actual tests: **NOT RUN** at user request. No native Vulkan/GLES build or hardware GPU validation. Next: verify Vulkan's `supportsCompute` and `createCompute` implementation or add them to core RHI in an isolated backend workstream; add real Vulkan parity tests before declaring GPU support.

## Stage 7H — 2026-11-20 — no-compute CPU morph fallback for GLES3/WebGL2

- Branch `feature/vivid-digital-humans`; no gameplay ECS or Renderer3D genre APIs modified.
- Code commit `c1e46c8cb4e338230d4da87070f1351ee22d993b`: new header `engine/character/vivid/CpuActorStream.h` with a full-model, per-actor CPU `SparseMorphLibrary::evaluateGather` and `FaceShapeBinding` route evaluation, preserving complete 60B `render::ModelVertex` positions, normals, tangents, UVs, material data, weights and joints. Outputs are uploaded to a dynamic `BufferType::Vertex`, `snapshotUpdates=true` and attached via the existing `modelVertexOverrideStatus` gate. No compute shaders or new RHI interface needed. CPU resources are retired only after `waitForResourceIdle`.
- Integration commit `7b4683ec7826b69f6ee1f8f441f92170a8c65fc0`: `tools/vivid-creator/main.cpp` now selects existing GPU actor only for `supportsCompute() && frameSerial()`; otherwise it creates `CpuActorStream`, dispatches and attaches it through the same ModelDraw path, and releases either at a safe idle barrier. Source `8ae596d048302c3f461b297e9a58f80ff6beef37` removes an accidental requirement for nonzero frame serial on GLES3. `GpuActorStream` still provides the GPU compute path for capable backends.
- Test source `122d0e62762b26a4cc150b9959264565bfa8565f` adds `tests/vivid/cpu_actor.cpp` with a GLES3 fake RHI backend, zero frame serial, semantic jawOpen morph, full vertex override and idle shutdown. CMake registration `a34eb0335e440446cb2263a419d11fe2858478a4` adds `vivid_cpu_actor` to CTest. **Tests/build actually run: NOT RUN**. No Android/WebGL device measurement or visual parity is claimed.
- Known constraints: this fallback is **CPU evaluation and CPU→GPU dynamic vertex upload**, not GPU compute on GLES3/WebGL2 (which lack compute in baseline API). Mobile crowd performance will differ substantially from Metal compute; benchmark active morph vertices, upload bandwidth and frame-time before committing crowd targets. The ordinary gfx::Device dynamic upload/vertex override contracts must be validated on each backend. CPU actor must be called once per presentation frame by the host when the backend cannot expose frame serial. Explicit per-frame GPU barrier/snapshot synchronization depends on the current renderer/backend implementation.
- Next: improve sparse update/upload bandwidth and platform-specific CPU fallback scheduling; implement missing Vulkan RHI compute support (if truly absent), add backend parity regression on real graphics devices; continue rig bind-pose/skin rebinding, hair self-collision and physical skin/eye/hair optics. Once all features are implemented, run user's requested one-shot native Mac `bash tools/vivid-validate.sh --full`, fix compiler issues and log results.


## Stage 7I — 2026-11-20 — merge universal ECS animation pipeline into VIVID

- Source: `feature/universal-ecs-animation-pipeline` at `6bf45b99d88f60badf8ba39ac9c077e53facd2fa`. Destination: `feature/vivid-digital-humans` previously `6354ea68a37069c08d77685f7e7cc58c702ff78c`. Merge base `548f75f0ce824db6f468f6577da95387d388ea0b`.
- Git operation: true two-parent merge commit on the VIVID branch; retain both histories. Source branch and `main` are untouched. This handoff is included in the merge tree itself.
- Integration scope: 75 changed paths relative to the common base. 73 non-overlapping animation paths retain the exact animation-branch blobs, including MotionWeave source epochs, model cache/read leases, safe GPU retirement and the editor animation/attachments pipeline.
- Conflicts manually reconciled: `AGENTS.md` appends BOTH the animation stages 127–154 directives and VIVID universal digital-human ownership rules. `engine/render/Model.cpp` preserves VIVID's `modelFromParts` primitive `firstVertex/vertexCount` initialization and the animation branch's model GPU/texture lifetime changes. No VIVID source files elsewhere were replaced by animation source.
- Tests/build actually run for merged tree: **NOT RUN**. The animation branch's previous test claims predate this merge; no integrated native build, CTest, editor launch or Metal GPU validation is claimed. User requested final batch testing after implementation.
- Known risks: animation branch changes common `engine/render`, editor, view, MotionWeave and world integration. CMake/build compatibility and functional behavior with VIVID GPU morph LOD, source-model epochs and model lifetime leases need an actual merged-tree acceptance run. The animation branch may lack later `main` changes; no `main` integration was attempted.
- Next: inspect API compatibility, especially the VIVID Creator `ModelDraw` and MotionWeave pose lifetime, then finish pending VIVID functionality. Perform all previously deferred tests together; log exact command, result, SHA and warnings in a new handoff.


## Stage 7J — 2026-11-20 — VIVID / universal MotionWeave merged lifetime compatibility

- Branch `feature/vivid-digital-humans`; merge ancestor `8c9048fb6f0407ad87cc62d112b53fa467d43f78` combines universal animation pipeline `6bf45b99d88f60badf8ba39ac9c077e53facd2fa` with VIVID `6354ea68a37069c08d77685f7e7cc58c702ff78c`. No change to `main` or the source animation branch.
- Code commit `709fb26a916bf843c98fe982402dc4f02677b026`: `tools/vivid-creator/main.cpp` detaches/clears the MotionWeave ECS World and compiled Graph pointers/pose scratch *after* the actor's successful GPU idle barrier but **before** releasing `HumanPreviewAsset::head` / `hair` Model resources. The previous order called `releaseModel()` while ECS `AnimationAssetComponent::model` and `AnimationGraph` still borrowed the Model. This now follows the newly merged source-publication/lifetime policy and prevents dangling model pointers while destroying scene resources.
- Code commit `3e06dada7df041f3eced6cff0988771fca920718`: `engine/character/vivid/HumanRig.cpp` validates every incoming MotionWeave node-world matrix component for finiteness *before* copying or facial composing; a bad source cannot contaminate skinning/groom output or partially alter the last-good presentation matrices.
- Regression source commit `0fc4d93665577b364a487b0f381c3c8e3e3f76eb` adds a nonfinite-source pose rejection and last-good output check in `tests/vivid/human_rig.cpp`.
- API compatibility: Creator continues to own a private single-use Model+World, not a `ModelResourceCache` resident Model; it must **not** fabricate cache `ModelReadLease` for models it does not cache-own. The new `MotionWeaveSourceBridge` epoch publication API is relevant to shared hot-reload hosts, but the standalone Creator regenerates the World/AnimationGraph on each model replacement and does not need a second source-identity scheduler. MotionWeave remains owner of animation graph evaluation.
- Actual testing: **NOT RUN**, per user request to finish implementation before combined testing. No native integrated CMake compilation, CTest, GUI frame capture or resource-retirement behavior has yet been verified on this merged tree. The stage adds regression source but no observed pass/fail result.
- Known limitations: full Creator regenerations still use a synchronous GPU idle barrier and can stall the editor; mid-frame enqueue is not implemented. Failed GPU upload after tearing down old model can leave a partially prepared replacement rather than retaining last-good visible model, which requires separate transactional GPU publication work. Animation's newer model-source epochs are not automatically imported into standalone VIVID projects beyond their private owner lifecycle.
- Next: stage GPU uploads of a replacement character before publication, carry safe old model retirement through a completion token, then implement remaining skin rebinding/groom self-collision/material optics and finally run `bash tools/vivid-validate.sh --full` once, logging exact build/compiler/CTest evidence.


## Stage 7K — 2026-11-20 — two-phase Creator character GPU publication

- Branch: `feature/vivid-digital-humans`, base includes animation merge `8c9048fb6f0407ad87cc62d112b53fa467d43f78`. `main` unchanged.
- Source commit: `5cd520bffbc889fb43f8d4aa4dba03e715bf0cc9`, `tools/vivid-creator/main.cpp` — reorganized `CreatorApp::regenerate` into CPU preflight, GPU-candidate preparation, then publish.
- Before: Creator called `releaseHuman()` before `uploadModel(head)`, groom GPU buffers, shared GPU morph asset, per-actor stream buffers and new ECS `World` existed. Any allocation failure destroyed last-good visible human.
- After: CPU prepared `HumanPreviewAsset` and `AnimationGraph` remain local; replacement has independent `World` with `AnimationAssetComponent`, `AnimationPlayerComponent`, `AnimationPoseComponent`, independent model and hair GPU buffers, and either `GpuActorStream` or `CpuActorStream`. Only after all of these initialize successfully does `releaseHuman()` drain/retire previous actor, detach old MotionWeave ECS references and release old `Model`; then ownership of the proposed model/streams/world/graph/pose/scalp scratch moves into live Creator members. The `unique_ptr<render::Model>` address remains stable under ownership move, keeping compiled graph and Actor Stream model pointer valid.
- Candidate failure cleanup: an RHI idle barrier is requested before destroying any candidate GPU handles and before detaching its independent World/Graph. On confirmed barrier, candidate Actor Stream buffers are retired and candidate Model head/hair GPU resources released; prior character is preserved. On **unconfirmed idle barrier**, candidate resource owners are intentionally leaked to avoid freeing possibly in-flight native handles. This is a **fail-closed process lifetime leak, NOT a completed GPU device-loss/recovery workflow**. Host must not continue normal operation on a truly lost device; proper device-loss quarantine/retirement is pending.
- **Tests actually run: NOT RUN.** The implementation was checked by source inspection and committed to GitHub, without native compile/CTest/Metal Creator preview or failpoint-driven GPU allocation tests. Do not describe the change as tested or production ready.
- Known limits: `releaseHuman()` currently drains GPU via synchronous device-wide barrier and may visibly stall the editor; old actor shutdown can be partially complete if a later independent CPU actor barrier fails, though Creator only uses one active actor mode per rebuild. Candidate graph/runtime model lifetime is staged but old GPU model resources still require safe retirement; a nonblocking completion-token swap is a separate stage. Existing `GpuMorphSharedAsset` and actor failure semantics should be verified with injected `createBuffer` failures on real devices. Failed publish does not roll back an authored `HumanCreatorSession` DNA revision, only preserves the last-good **visible** character.
- Next: implement explicit failure-injection resource tests and nonblocking retirement/quarantine based on `ResourceRetirementStatus`, then finish skin rebinding, groom self-collision and advanced material optics. At the user's chosen end-of-features milestone run a single `bash tools/vivid-validate.sh --full` and record real results.


## Stage 7L — 2026-11-20 — nonblocking whole-human GPU completion-token retirement

- Branch `feature/vivid-digital-humans`, after universal animation merge. `main` and source animation branch are untouched.
- Source commits: `7f72151523c83c5135c0365d2828c01e9e2a01ae` grants the whole-human retirement owner explicit access to `GpuActorStream::releaseAfterGpuCompletion()`; `f9cccd68abb0e54cc0424524b029a079853a3ea4` allows `CpuActorStream` buffer release after a verified completion token; `aed9070cb922d2b9f73705231eb6bb6a693164e7` adds `engine/character/vivid/HumanRetirementQueue.h`, reusing genre-neutral `render::AsyncResourceRetirementQueue`; `a38cda660860f29ac942a051b8515c2aaf5614a4` swaps complete old Creator scene into queue during replacement without a synchronous GPU-wide barrier. The queue's `Resident` retains old head/hair `HumanPreviewAsset`, GPU/CPU actor, ECS `World` and AnimationGraph until retirement.
- Submission-boundary correction: `608307e1f7cc266f9e2977b333c86a7be4f0625a` adds opt-in `platform::AppCallbacks::afterFrame(gfx::Device&)` called after endFrame; `9448d9515bf4cef1ee857b72f468b624437b576d`, `de2ee1bf3a60bd98dec400f4d8f223c2f513461a`, `7519f55cdd70956b7ef06eeee71a0fe4628008fc`, `0e64a08f25af237cf3b777dc52fabf46cfdd50b0`, `a27e9481fab8b58aefd09a5dca0372ac6c8786da`, `081e224a7ec8318773fa1eb436dcf8386b17be60` add the hook to macOS, Linux, Windows, Web, Android, iOS; web indent follow-up `4f2644ada4bd107761e5b42b980a9af0e6b7a7c3`. `004bcdbd15def8d04a0d5c382a9895526ceea746` moves Creator fence polling out of `frame()`, which runs **inside** beginFrame/endFrame, into `afterFrame`. All existing callback users remain source-compatible.
- GPU safety: queue first allocates and enqueues an empty retirement slot before moving any live objects. On successful candidate preparation, old ECS World/Graph, Model, GPU/CPU morph stream ownership moves into that slot and new human is published immediately. After last draw submission `afterFrame` starts a native GPU retirement token. Pending or failed tokens never release buffers. Only completion of that token or a successful explicit `waitForResourceIdle()` allows actor buffer destruction followed by head/hair `releaseModel`; ECS/Graph references are detached before model destruction. Queue uses the existing failed-fence diagnostics and successful idle fallback, not elapsed frame counters. If `beginResourceRetirement()` is unsupported, generic queue may block in its explicit idle fallback; the nonblocking promise applies to token-supporting devices.
- Test source `9b58c20c8689c453b8434ed3132b8f0429373ecd` adds `tests/vivid/human_retirement.cpp`: fake RHI verifies no marker before endFrame, retained model buffers on Pending/Failed, rejection of an unsuccessful idle drain, and release on successful drain. CMake registration `91096bcca91301c6b9155954506194978e134195` provides `vivid_human_retirement`.
- **Actual tests/build: NOT RUN** per user's implementation-first requirement. No compile, CTest, macOS Metal fence tracing, visual regression, Vulkan/GLES backend parity, or performance measurement was performed for Stage 7L.
- Remaining risks: the existing two-phase candidate-preparation rollback can retain/leak native owners if GPU idle cannot be proven (explicit fail-closed policy). Device-loss recovery and safe queue destruction on failed drain are not implemented; Creator currently logs failure and retains the pending queue owner until process termination rather than freeing in-flight buffers. The optional post-frame callback touches common platform sources and requires native build across macOS/Linux/Windows/iOS/Android/Web. An unsupported completion-token backend still stalls in the queue's explicit idle fallback. Backpressure (limit on pending retired human models) and measuring peak dual-resident GPU memory need a separate stage.
- Next: implement pending-residency/VRAM backpressure, failure injection and per-backend post-frame contract tests; finish remaining VIVID bind-pose/skin rebinding, groom self-collision and skin/eye/hair materials. After full feature closure, run the deferred one-pass `bash tools/vivid-validate.sh --full` and record actual commands/results.


## Stage 7M — 2026-11-20 — GPU residency backpressure during frequent Creator rebuilds

- Branch `feature/vivid-digital-humans`; `main` untouched. Builds and tests remain deferred by user request.
- Source commits: `2ac79af5d6b9a5e25ace7ad18e1ce328865214b1` added `HumanRetirementQueue::Admission` and read-only `Residency`, estimated dedicated GPU bytes, count/byte budgets, high-watermark/blocked-publication counters and `canPublish(nextRetiredBytes)`; `79517be909588a408fe77be31ef05d14fb7476b2` exposed consistent `estimatedCurrentBytes(human,actor,cpuActor)` for a currently visible character; `eda35da98287eb6d267c739f0fb45b04073613d5` integrated early admission into `tools/vivid-creator/main.cpp` and diagnostic UI; `dcc2d52180545f80335dc4924f4b5efadbb20d97` added headless fake-fence regression source assertions in `tests/vivid/human_retirement.cpp`.
- The queue's default conservative limits: **at most three pending retired characters** and **512 MiB of estimated pending dedicated resources**, including the next active character to retire. If exceeding either budget, Creator keeps `pendingRebuild=true`, leaves the visible last-good character and its World/Graph/actor untouched, and retries at the next frame after asynchronous completion. The check is done **before** beginning a new CPU/GPU candidate build, so repeated edits cannot allocate unbounded replacement GPU streams while the retirement backlog is saturated. No forced release or implicit success of pending/failed fences is permitted.
- Estimates reuse the existing `render::estimateModelResident()` (model dedicated vertex/index streams, known owned textures and baked textures) plus VIVID per-actor morph/output buffers; shared device texture caches, unknown driver padding/compression, opaque morph textures and some CPU-mode buffer sizes are not authoritative VRAM totals. The reported count and MiB are **admission estimates, not GPU memory telemetry**. In particular, the live new candidate's peak transient GPU allocation can exceed this retired-resource budget; follow-up native profiling must measure peak two-character residency.
- New test *source*, **NOT RUN**: fake RHI exercises admission for first/second queued humans, blocks third by pending-count budget, holds blocking while token is unresolved, resumes after completion and verifies byte-limit rejection and clean idle drain.
- Limitations: an unsupported token backend can still stall in explicit fallback, a failed fence may indefinitely defer rebuilds rather than risk deleting in-flight resources, and real device loss still needs explicit recovery. There is not yet a strict total-device VRAM budget or backend-specific memory counter. Creator's UI advertises estimated retired residency only; the runtime currently offers fixed default admission limits with an API for configuration.
- Next: optional display of GPU residency estimates by actor and source, scoped GPU allocation failpoints, native Vulkan/GLES and Metal acceptance, then full bind-pose/skin rebinding, groom self-collision, and realistic skin/eye/hair optics. Run all final validation together once feature development is complete; record actual compiler and CTest results, not inferred outcomes.


## Stage 7N — 2026-11-20 — opt-in native skeleton bind-pose / inverse-bind rebinding

- Branch `feature/vivid-digital-humans`; base includes the animation merge; `main` unchanged.
- Code commits: `3c96f159810b7230777d995ad3f192c0e7be3621` added `engine/character/vivid/BindPoseRebind.h`; `1a1ed6fd1b8b05fa3249448ab1448109daa20508` rejects no-skin models in rebind mode; `c223d4db23abe10b3385d5dab842b33e4a54f634` adds an opt-in Creator rebind checkbox and applies edits to a private CPU candidate **before** its AnimationGraph compilation and GPU upload; `f060656fc979a727b91556a39ee361fe5650d1d3` preserves the original imported binding frame when calculating new inverse-bind matrices; `395b80c5b655925f0c8e42c9d333d8c9442c3630` and `4c998423f82380dad0cc94f1384efb87837818cd` add headless source regression assertions.
- API: `BindPoseRebind::apply(render::Model&,const RigAuthoring&)` requires a CPU-owned, pre-upload Model with at least one Skin, computes a validated hierarchy from immutable input node parent links and authored local bone translation edits, validates both original and proposed finite/nonsingular transforms, checks skin joint/inverseBind cardinality and each affected primitive's skinned palette/influences, then publishes newly authored node translations, proposed `Model::bindWorld` and new `Skin::inverseBind` only after validation. This is not an ECS animator or GPU pass.
- Core math: for joint `j`, `newInverseBind[j]=inverse(newWorld[j])*oldWorld[j]*oldInverseBind[j]`. This conserves the original skinned mesh bind frame, including imported non-identity mesh/root bind offsets, so rest-pose skin `newWorld * newInverseBind` equals `oldWorld * oldInverseBind`. Morph CSR vertex IDs and the original source mesh vertex positions remain unchanged, while subsequent animation deformation is based on the edited skeleton and corrected inverses.
- Creator no longer double-applies the authored pose correction when opt-in rebinding is active. Edits and undo/redo/sidecar load schedule a normal staged candidate rebuild using the existing Stage 7K/7L transactional resource publication; failures preserve the visible last-good character.
- New regression source in `tests/vivid/human_rig.cpp` covers changed bone local translation, inverse-bind inverse correction, nonidentity imported mesh bind offset, neutral-skin preservation and invalid palette rejection without partial publication.
- **Tests actually run: NOT RUN** per user instruction to defer all validation. No C++ compiler, CTest, GPU skinning parity, Metal renderer preview or Vulkan/GLES backend run has been performed for Stage 7N. Do not equate authored source tests with observed PASS.
- Deliberate scope: this stage implements skeleton/inverse-bind rebinding, **not automatic joint-weight painting, mesh rest-shape sculpting, dual quaternion skinning, retargeting clip keyframes to a new rest pose, or exporting a modified FBX/glTF rig**. Imported multi-skin aliases and shared vertices are validated for palette bounds, but advanced seam harmonization/weight painting and animation-clip baseline retargeting require separate authoring stages. The new bind frame may change how absolute translation animation keys behave; verify in real asset tests.
- Next: native skin-weight authoring (four-influence constrained palette edits, automatic renormalization, seam-aware checks), persist rebind recipe within VIVID imported package and add new rig/mesh GPU parity tests. Then complete groom self-collision and realistic materials before one final `bash tools/vivid-validate.sh --full` on Mac.


## Stage 7O — 2026-11-20 — named-joint skin-weight editing on staged imported model

- Branch: `feature/vivid-digital-humans`; main/other agent branches untouched.
- Source commits: `4b72c97b665188d91984f78c9f495241f9b48733` new `engine/character/vivid/SkinWeightAuthoring.h` with `set(vertexId,jointName,weight)`, `erase`, `toJson`/`fromJson`, `apply(Model&)`; `49774832270f1cf81dea528f4f48d5754aed71ef` adds Creator Skin weight authoring panel and save/load JSON, calls `SkinWeightAuthoring::apply` only on the *private pre-upload* `HumanPreviewAsset` candidate during Stage 7K's transactional rebuild; `4841a6f2f4c81bdfeb3c117ce553a0f94fb15cc1` keeps the newly selected bone in the top-four authoring subset when five influences compete.
- Validation: each edited source Model vertex must belong to a skinned primitive with a valid node skin, valid primitive index range, named joint present in that skin's `Primitive::jointPalette`, and valid original palette slots. Shared vertices must be compatible across every referenced skinned primitive (same skin index AND same palette mapping); otherwise the edit fails with explicit **split seam first** error. All requested vertex bytes are staged before any mutation, and publication changes **only `ModelVertex.joints[4]` and `ModelVertex.weights[4]`**, preserving original rest geometry, normals, UVs, tangents, morph CSR IDs and the immutable input source asset. Bone influences are normalized to a total of 255 using deterministic floor+largest-remainder quantization, with at most four nonzero slots. No effect on ordinary rigid or material-palette vertices.
- Editing workflow: Creator selects a global model-relative vertex index, named bone node, weight in [0..1]. A source-authoring request is staged, the character rebuilds with staged inverse-bind changes if enabled, then existing safe GPU publication/retirement publishes the new 60-byte ModelVertex stream only after candidate initialization. JSON sidecar uses schema `{version:1,edits:[{vertex,joint,weight}]}`. Current authoring supports **one last-written named-bone edit per global vertex**; multiple individual vertices may be edited. This is not yet a painted brush with distance falloff, mirror symmetry, automatic skin assignment, or full four-bone vector authoring.
- Regression source commits `3136b260afc6ff59ca0f12497bf9d764be6cd406` and `00dc9814effc0d200db1bf7bb3346be2e2571105` update `tests/vivid/human_rig.cpp` with weighted mesh fixture, exact U8 sum, JSON roundtrip, invalid palette rollback and same-global-vertex contradictory primitive palettes.
- **Actually run:** NOT RUN. GitHub source commits only; no native C++ compiler, CTest, creator GUI smoke, import/export or Metal/Vulkan/GLES skinning parity was executed. Follow-up must check true unsigned-joint palette handling in all RHI backends and the imported skin’s existing quantization conventions. Behavior when requested small share rounds to zero is not guaranteed to preserve a rendered influence.
- Remaining: reversible weight-paint history and brush-based vertex selection, seam splitting/harmonization, automatic joint assignment, skinned morph-LOD weight transfer, canonical skin-weight sidecar path in HumanImportManifest, real native GPU image parity, hair self collision, optics. Do not declare full MetaHuman skin authoring complete. Next stage: implement brush and mirrored seam-aware skinning, then final one-shot `bash tools/vivid-validate.sh --full` after remaining features.


## Stage 7P — 2026-11-20 — viewport skin weight painting, mirror, undo/redo and heatmap

- Branch `feature/vivid-digital-humans`; `main` untouched. Prior universal animation merge remains in branch.
- Code `92d97264975c9923c6d36767e343c671afc0ded7` extends `engine/character/vivid/SkinWeightAuthoring.h`: `brush(const Model&, meshIndex, centerGlobalVertex, namedBone, targetWeight, localRadius, strength, mirrorX)`, smoothstep-like radial falloff, bilateral X mirror with suppression of double centre stroke, one transactional update per completed stroke, bounded history `undo/redo`, `weightAt` for exact U8 source-palette weight lookup. Staged changes remain model-global vertex IDs plus stable bone names, and `apply(Model&)` still commits only after validating every skin/palette. Edit storage currently allows one named-bone adjustment per vertex, with the last edit replacing earlier edits for that vertex.
- Code `3b9ddde7de64924c901a32821e4ac70431ea11c3` integrates creator UI: skinned mesh choice, brush radius, strength, selected bone weight, mirror-X, arm-and-click viewport brush using existing `pickSculptVertex` ray triangle picking and MotionWeave face pose, undo/redo buttons, and bounded screen-space diagnostic weight heatmap. Successful stroke sets `pendingRebuild` and reuses full staged GPU/CPU actor replacement. Painting never directly overwrites the source Model's uploaded buffer during the active render pass.
- Code `a799ea91d7257a8245a86b7b62186fce3cafd8b7` caps an individual weight stroke at 4096 affected candidate vertices to avoid unbounded brush history/GPU staging costs in massive imported meshes. Exceeding the limit returns false and preserves the prior edits. This is a provisional scalability guard; per-primitive skin palette lookup in `weightAt` is still a linear scan and needs an indexed cache/BVH for large production characters.
- Test source `39e559ad173fa9e1ca33b11a72e49022d8b6f257` adds `tests/vivid/human_rig.cpp` fixture with skinned source mesh, symmetric X vertices, falloff/radius isolation, whole-stroke undo/redo, invalid brush rejection preserving JSON, and authored U8 weight publication.
- **Tests actually run: NOT RUN** by explicit user preference (finish all functionality, then one native acceptance sweep). No C++ compile, Creator click interaction, Vulkan/GLES/Metal GPU parity, high-poly profiler or screenshot was executed. These are implemented source changes, not validated functionality.
- Important limitations: the heatmap is a **bind-space screen overlay**, projected using the selected mesh owner node transform, not a true per-vertex GPU-skinned/morphed position visualization; expect misalignment when a mesh bends through skeleton joints or facial morphs. Ray picking uses source static triangles and a node-world matrix, not the final GPU-skinned mesh surface. Real GPU skinning-aware ray pick and heatmap require CPU-skinned or GPU-picked presentation geometry. Current UI arms a *single click* per stroke; continuous drag painting and spatial acceleration are future work. Mirroring assumes a character local X=0 plane, not an automatically detected symmetry skeleton. Multi-owner skinned mesh or conflicting primitive palette is rejected fail-closed; shared seams are not auto-split. No new gameplay ECS animation scheduler or genre dependence was introduced.
- Next: production-grade skinned presentation picking and GPU-accurate skin weight heatmap, drag strokes with cached candidate sets/spatial index, full palette-aware seam splitting and multi-bone blending. Then hair self-collision and advanced skin/eye/hair optics before end-of-features single `bash tools/vivid-validate.sh --full` run.


## Stage 7Q — 2026-11-20 — skin-aware weight picking and continuous drag transactions

- Branch `feature/vivid-digital-humans`; `main` untouched. Animation pipeline from the 7I two-parent merge remains the authoritative evaluator.
- Source commits: `c5f97a5172980d8c671084e1b0f20488aee1775c` new `engine/character/vivid/SkinnedWeightPick.h`; `c8528fdadc72b433728d305e0bea8d30ca27a02d` adds one-undo-unit `SkinWeightAuthoring::beginStroke/strokeBrush/endStroke`; `79a84e985ad253e765bb742479b5373049593d28` provides edit equality for cancelling unchanged strokes; `94c7113a56484f39983c5d1cb7fbccf86fb30e6d` integrates creator press/drag/release painting, skinned raycast and skin-aware heatmap vertex projection; `1518644f9f606f420a87a731ab8584b0a5359220` removes quadratic per-entry history copies while parsing large skin-weight JSON; `a87c95c67372497f6b30187561f08be0d2376e35` fixes the deferred-history argument to the actual brush implementation.
- `prepareSkinnedWeightSurface(Model,meshIndex,nodeWorld,out)` validates a single mesh owner, source topology and primitive palette compatibility, and stages a per-vertex world-space skin position with up to four U8 influences using `sum(weight * nodeWorld[joint] * inverseBind[skinJoint] * vertexPosition)`; rigid surfaces use the owning node transform. The temporary positions are not GPU data and cannot alter the MotionWeave pose. On invalid input the previous caller output remains unchanged. `pickSkinnedWeightVertex` tests the camera ray against the deformed triangles, selecting the nearest vertex on the closest positive intersection. Editor heatmap projects sampled skinned positions rather than only the owning node transform.
- Continuous gesture: mouse-down begins one stroke snapshot, dragging applies multiple source-vertex brush dabs to an in-memory authoring map, mouse-up commits ONE undo record and schedules a new character GPU/CPU actor rebuild. Invalid stroke cancels/rolls back the whole pending gesture. Undo/redo of the full gesture do not trigger per-frame model upload. The earlier `SkinWeightAuthoring::brush` remains available as a single-click API with its normal history semantics.
- Test source `dc34a408ecd5b10547e63e9d0ffbbdf6f9b2f364` extends `tests/vivid/human_rig.cpp` with a translated joint and skinned raycast, fail-closed invalid pose, two-dab one-step undo/redo and cancellation rollback.
- **Tests/build actually run: NOT RUN**, as requested. GitHub source inspection confirmed the intended final method signatures but no C++ compile/CTest, Creator mouse interaction, GPU skinning parity or benchmark exists yet for Stage 7Q.
- Important limitations: this CPU reference reproduces matrix skinning, **not** facial GPU morph displacement, cloth secondary deformation, baked/alternative GPU skinning modes or real GPU depth occlusion; visual/pick parity still needs a native hardware test. Scratch positions and per-vertex palette vectors are rebuilt when painting or heatmap is enabled, and each brush dab scans the selected mesh; production high-poly painting needs a persistent per-model topology cache/BVH and true GPU-position parity. Single-mesh owner requirement rejects ambiguous instances; the paint API still authors a single bone weight per source vertex rather than a full four-bone vector. Edits are published only after mouse-up through the existing two-phase Creator rebuild, with no in-frame GPU mutation.
- Next: real GPU-morphed/skinned picking parity and high-poly acceleration, automatic incompatible seam splitting and skin-weight LOD transfer; hair self-collision, skin/eye/hair optics; then the user's deferred single native `bash tools/vivid-validate.sh --full` and explicit documentation of any compile failures.


## Stage 7R — 2026-11-20 — barycentric skin-weight LOD transfer and seam diagnosis

- Branch: `feature/vivid-digital-humans`. Source history includes merged universal MotionWeave. `main` unchanged.
- Code commits: `1be1626d91981967e524bbd47a9e1430158cd369` creates `engine/character/vivid/SkinWeightLodTransfer.h`; `f12a70310ba9bf54b26094d0d163d176879398b3` adds source-geometry fingerprint and exact target-vertex-set validation; `92e2ebae0d1beefa8f8badca50fd789e923b782b` integrates LOD weight transfer into private pre-upload model rebuilding in `tools/vivid-creator/main.cpp` after source skin-weight edits; `af5e82fcbdeb4b87b9c7a1257ca61115165d543e` creates read-only shared-skin seam diagnosis in `SkinSeamAnalysis.h`.
- Skin LOD algorithm: uses existing `MorphLodMapping` barycentric triangle correspondence, validates original source position/index fingerprint and full sorted target-vertex set, resolves four U8 source influences through each primitive's joint palette to canonical node IDs, barycentrically blends weights, remaps to the target LOD's explicit skin palette, chooses up to four heaviest influences, normalizes deterministically to 255 bytes, and stages ALL updated target `ModelVertex` values before applying them. No geometry/UV/morph-CSR indices are changed. Missing bones, conflicting palettes, unsafe ranges or topology fail before publication, preserving previous active character. Does not infer skeletal weights for unindexed generated LODs.
- `SkinSeamAnalysis::detect(Model)` detects identical global vertex IDs referenced through inconsistent resolved primitive skin joint palettes, returning locations for a later geometry split tool. It is read-only and does not fabricate new vertex IDs. **Automated seam splitting is not complete**: naïve vertex duplication would require coordinated index ranges, model GPU vertex counts, original morph CSR target indices, authored LOD maps and skin-owner semantics. This stage deliberately prevents silent corruption and isolates the exact conflicts that future split authoring must resolve.
- Test source: `f6eb41cea06fde0d98e2686d332661178f347cf3` adds barycentric LOD weight transfer with reversed destination palette, sum-255 invariant and missing-target-joint rollback; `077dfd9cac636f77ca9d6bd85b44d2f95c58ba2d` adds detection of shared vertices across incompatible primitive palettes. The tests are in `tests/vivid/human_rig.cpp`.
- **Tests/build actually run: NOT RUN**, per user requirement to finish features before a single final native pass. This source is not compilation-verified. Real Metal/Vulkan/GLES skinning parity, GPU morph+LOD composite, imported skinned GLB and editor preview are still pending.
- Next: implement a safe seam-split geometry remap transaction including morph CSR expansion, primitive-range regeneration and indexed LOD sidecar updates; then hair self-collision and high-end skin/eye/hair optics. Run final `bash tools/vivid-validate.sh --full` only at completed feature scope, documenting actual results and fixes.


## Stage 7S — 2026-11-20 — safe skinned seam duplication with sparse facial morph replication

- Feature branch: `feature/vivid-digital-humans`. Universal MotionWeave merge preserved; `main` untouched.
- Source commits:
  - `0dbf99db481961cabd0f961b67a52130685a4a96`: new `engine/character/vivid/SkinSeamSplit.h` stages `Model::vertices`, `indices`, `meshes` and the full `SparseMorphLibrary` for incompatible skinned primitive palettes, preserving original global vertex IDs and appending complete disjoint contiguous primitive vertex intervals. Every clone inherits the original position, normal, tangent, UV, U8 influences and facial CSR morph delta. Applies only to CPU-only pre-upload model.
  - `1f7856b04775f3432caa61ac0901637dd593d6a5`: reject primitives that already contain legacy `morphTargets`/`morphDeltas`, baked shadow batches or material batches, because rewriting those additional vertex/index ownership structures without rebaking could silently corrupt rendering.
  - `244dcb50674f42b5c4d5fafdcbc4dda842ec780c`: `SkinWeightAuthoring::withSplitClones(clones)` derives candidate-only authored weight adjustments for every cloned original vertex. The source JSON/Undo history retains the original immutable imported global vertex IDs.
  - `975e5fdc749cc7124470078710ec580abcd693c6`: Creator calls `SkinSeamSplit::apply` for fresh private candidate before applying edited skin weights and before skin LOD transfer, then applies the expanded candidate edit map. Because original IDs and original source mesh/index prefix survive, existing source correspondences remain meaningful; target-mesh correspondence that cannot cover added clones fails closed.
  - `753e80f15c57fffc79dd9017b20a3aaccf6dbc2c`: detect and reject multi-owner skinned meshes and primitive index ranges shared with any other primitive, since editing a shared index interval could change another mesh silently.
- Algorithm: `SkinSeamAnalysis::detect` identifies conflicting primitive usages of a global vertex. The second/conflicting primitive is cloned into newly appended, sorted, contiguous `ModelVertex` interval, and only the cloned primitive's index range is redirected to those new IDs. The corresponding `Primitive.firstVertex/vertexCount` are updated. All original vertices remain in place. `SparseMorphLibrary` is unpacked to named `MorphTargetInput` deltas, exact nonzero source delta records are duplicated to each new cloned ID, and CSR is recompiled to the new full `ModelVertex` count. Preparation validates clone budgets, topology, palette owner uniqueness and unsafe legacy draw structures; only successful staging swaps the model's vectors and compiled facial morph library into the candidate. This avoids remapping existing source IDs or changing the original externally authored asset.
- **Important limitation**: currently this is a constrained automatic split, not a universal asset re-topologizer. The tool conservatively rejects imported primitives with legacy eight-target renderer morph atlases, precompiled material/shadow batches, overlapping index intervals, multi-owner skinned meshes, or LOD mappings that cannot cover all newly appended target vertices. Sidecar `MorphLodMapping` remains unchanged because old IDs remain valid; freshly appended cloned target vertices are NOT automatically added to an authored LOD mapping. This requires follow-up coordinated sidecar sample replication and rebake for affected target LODs. For non-rejected models, the new extended VIVID facial CSR covers the clones.
- Regression source: `be796270978a4d67652a7ff90ca38942efa18d68` adds a multi-primitive same-source vertex fixture to `tests/vivid/human_rig.cpp`. It verifies three seam conflicts, one cloned primitive, source index stability, cloned contiguous IDs, copied sparse facial deltas, original authoring unchanged, cloned weight edits, and normalized U8 weights after editing.
- **Tests/build actually run: NOT RUN.** No C++ compile, CTest, imported GLB real asset, Metal/Vulkan/GLES renderer parity, hair physics test or GPU profile has been performed. Current claims are source changes committed, not successful runtime validation.
- Next: support legacy morph atlas and batched primitive rebaking or reject with surfaced diagnostics; clone target LOD mapping samples/sidecar fingerprint safely for all affected target meshes; fully exercise real imported skinned characters and GPU 60B vertex stream. Then proceed to hair self-collision and skin/eye/hair optics, finally the deferred one-shot `bash tools/vivid-validate.sh --full` with actual test logs.


## Stage 7T — 2026-11-20 — target LOD mapping replication for seam-clone vertices

- Branch: `feature/vivid-digital-humans`. Do not merge into main at this stage.
- Source `0566cf98a101dfff7ef2a265ec3e75c376a67547`: new `engine/character/vivid/SkinLodSeamRemap.h`, with `SkinLodSeamRemap::expandTarget(original,clones,targetMesh)` producing a **candidate-only** `MorphLodMapping` that copies the original target source triangle/barycentric sample for each appended seam-clone global vertex ID. Preserves all original IDs and samples, sorts by target ID, revalidates the expanded mapping and rejects missing source sample/duplicate target IDs without altering the sidecar.
- Source `5e7dd351014fa4dbdd5337fa90fe1afb796367b1`: VIVID Creator captures the `SkinSeamSplit::Result` across candidate operations; after cloning incompatible skin palettes and replicating extended sparse facial morph deltas, it uses the expanded LOD map in `SkinWeightLodTransfer::apply`. Thus the target LOD's cloned vertices receive interpolated skin influences and their original authored source triangle correspondence remains available.
- Source test `eb3b2d0cf3a6ab403dae1691a9fe987d008c9e59`: expanded target mapping preserves old IDs, associates clone ID with identical barycentric coordinates, leaves original mapping unchanged, and rejects an incomplete authored target mapping with no partial mutation. Test source is in `tests/vivid/human_rig.cpp`.
- Crucial distinction: expanded mapping is **runtime candidate metadata**, not a persisted replacement `MorphLodMapping` JSON file. Geometry/source fingerprint remains that of the original indexed source mesh. This is correct only when source positions/triangles used by the map remain unchanged (current seam splitting appends clones without renumbering the original source vertices). If the source mesh itself contains cloned source vertices, the saved source correspondence still references the original indexed triangles and remains valid for sampling, but it does not reconstruct a new independent authored source map for the clones.
- **Legacy renderer status:** reviewed `Model::Primitive.morphDeltas`, `morphTargets`, `MeshDef.shadowBatch`, `materialBatches` dependencies. Automatic rebuilding of the legacy morph texture atlas, shadow batch and material batches is **NOT implemented** in this stage. The earlier Stage 7S fail-closed checks remain active. Do not claim universal seam splitting on these assets; a follow-up must choose asset rebake with renderer parity or an explicit batch invalidation/rebuild API.
- **Actual validation: NOT RUN.** No native build/compiler, CTest, GPU screenshot, Metal/Vulkan/GLES functional test or high-poly benchmark executed. The new test assertions exist in source only.
- Next: implement coordinated legacy primitive morph atlas / shadow material batch rebuilding with explicit renderer contracts and tests; inspect the Creator candidate graph/morph source ownership and existing Vulkan compute implementation; complete hair physics and realistic materials before single final Mac acceptance. Record actual compiler diagnostics in the next handoff if tested.


## Stage 7U — 2026-11-20 — legacy morph and batch rebuild on split skinned seams

- Branch: `feature/vivid-digital-humans`. Existing MotionWeave integration remains intact. No edits to `main`.
- Reviewed **actual render contracts**: `engine/render/Model.cpp::uploadModel` packs `Primitive::morphDeltas` into the legacy `RGBA32F` morph atlas on first successful upload and assigns fresh `Primitive::morphTexel`. `prepareModelUpload` calls existing engine `prepareShadowBatches` and `prepareMaterialBatches`; their engine implementations deliberately avoid skinned models. Renderer3D also disables those batch paths when a custom vertex override is active. Do not add a second morph-atlas creator or use external reimport of model data.
- Commit `69befab713b4c4f780b1836a21ceb865a2b67b2e`: `engine/character/vivid/SkinSeamSplit.h` accepts **unuploaded CPU-owned legacy primitive morph data**, validates `[target][source local vertex][pos xyz,normal xyz]` length of `morphTargets*vertexCount*6`, source interval and non-resident morph texture, and stages an exact local-index repack for the new sorted, contiguous vertex interval. It resets stale `morphTexel` on the cloned primitive; normal `uploadModel` builds the new GPU atlas. Before publication, the now-invalid per-mesh batch descriptors are invalidated so primitive drawing remains authoritative. Extended VIVID SparseMorph CSR still duplicates its own deltas to cloned vertices as before.
- Commit `5e79c7378ee525f8718ae409ace97385ac57a80a`: scope batch-descriptor invalidation only to meshes that actually contain seam-cloned primitives, leaving unrelated meshes' batches unchanged.
- Test source `f419eff1c44a7ecd1b6d3bf7cca66101a7d16010` adds old `morphTargets=1`, `morphTexel`, 18-float legacy morph data and populated material/shadow batch fixtures; checks repacked data, reset atlas offset and removed stale descriptors after splitting. `8e9d6c1183239d3758999d5aad19bac7ca5a97e1` adds negative malformed-morph-stride rejection with invariant that the source CPU Model and extended SparseMorph stay unchanged.
- **Actual tests and builds: NOT RUN** by explicit request to finish features before batch acceptance. Source added; no native compiler, CTest, Metal/Vulkan/GLES GPU atlas parity, sample Creator rendering or performance benchmark has been performed. Do not claim tests passed.
- Remaining limitations: this is re-packing CPU legacy `morphDeltas` plus **invalidating** affected stale material/shadow batch descriptors, not reconstructing optimized precompiled batches. Since the model is skinned, normal batching is ordinarily inapplicable. Does not handle a resident GPU `morphTex`, geometry-pool pages, multi-owner skinned meshes, overlapping primitive index ranges or invalid source local ranges. Those cases continue to fail closed. A production acceptance pass must verify per-target legacy morph atlas sampling against the selected vertex override and all LODs before claiming cross-RHI graphics parity.
- Next: finish VIVID groom hair self-collision and optical skin/eye/hair refinements. Before final feature freeze, audit inter-stage invariants around `SkinSeamSplit` cloning, source morph binding, skinned LOD correspondence and model upload; run the user's deferred combined `bash tools/vivid-validate.sh --full` with actual native log and fix compiler/runtime issues.


## Stage 7V — 2026-11-20 — bounded inter-guide hair self-collision

- Target `feature/vivid-digital-humans`; `main` unchanged. MotionWeave remains sole skeleton animator. All work is within the VIVID presentation-only groom runtime/Creator.
- Source commits: `c10bf48abf02bd2fc9d64459a01b733e7ef12c90` adds `engine/character/vivid/GroomSelfCollision.h` for deterministic 3-D uniform-grid candidate lookup and Jacobi inter-guide collision projection; `c98f204763fce19220a20a823f5fc3e753f953a1` adds `GroomSelfCollisionConfig/Stats`, `HumanGroomRuntime::setSelfCollision`, read-only settings/stats; `cb8977044b217a590bdeb3be87aa177907dcff32` unifies public settings definitions in `HumanGroom.h`; `163460f30d2098112cb6c78437719e922f0fad34` integrates inter-guide contacts into each existing bounded Verlet substep and reprojects post-contact positions outside the current scalp proxy/ellipsoid; `ca7241508c02bbfe8600fa8434835247d1f566b5` adds Creator collision enable/separation/response settings and pair/contacts/limit diagnostics.
- Algorithm: only **different guide** particle nodes with `segment>=1` can repel, so each strand's root stays pinned and same-guide adjacent particles are excluded. A uniform-grid bin of width `separation` checks 27 adjacent cells; every unordered inter-guide pair is evaluated at most once. Resolve overlaps with averaged two-sided Jacobi corrections and update both position/previous positions together to avoid imparting artificial Verlet velocity. After correction, the existing scalp collider is reapplied. Upper bounds: <=131,072 dynamic particles, configurable default max 250,000 candidate pair checks per substep and maximum 2,000,000 via config validation; if a budget is exhausted, remaining pairs are skipped and flagged in diagnostics, never freed/invalidated. Default self-collision is disabled in the library for existing clients, enabled in the VIVID Creator preview. API is genre-free and operates entirely on groom-local CPU data.
- Tests source: `9e068398eb3202f730197e27f0a7a4a45f16d126` extends `tests/vivid/human_appearance.cpp` for deterministic repeated simulation, zero root drift, finite positions, rejected NaN parameters and bounded pair counting; `89ea7710ecd5fb4701c1c148c55aa9d0538ef913` adds an explicit two-overlapping-guides contact fixture verifying separation with unchanged anchors. Existing `vivid_human_appearance` CTest owns these assertions.
- **Actual tests/build: NOT RUN.** These are new source changes committed to GitHub; no C++20 compile, CTest, native macOS Metal Creator launch, visual/animated groom comparison, Vulkan/GLES or performance profiling has been performed. Do not mark them passed.
- Limitations: point-particle guide collision, not continuous segment-segment or card-card collision. No per-triangle collision response, friction, tangling, thickness from licensed real groom assets or GPU compute version; intersection between long segments may remain despite nonoverlapping sample nodes. The broadphase builds grid/scratch storage per substep and needs persistent capacity/spatial hashing plus profiler optimization for 8192-guide production grooms. After inter-guide correction, a full additional length-constraint solve is not performed; segment stretching in very dense clumps should be checked under high-wind stress. The library config initially defaults to off for backwards compatibility. The packed hair-card geometry remains dependent on the existing CPU `updateCardVertices` / dynamic RHI stream and can be slower on mobile.
- Next: optimize guide grid allocation/reuse, add segment-segment narrowphase and friction/clump preservation, then validate real Creator capture on Mac. Continue realistic skin/eyes/hair optical authoring and the remaining Vulkan compute backend verification. Per user instruction do the full native build/test sweep only once functionality is finished, capturing compiler and runtime failures accurately.


## Stage 7W — 2026-11-20 — segment-level hair collision, friction and curl preservation

- Branch: `feature/vivid-digital-humans`. The universal MotionWeave merge is still the authoritative skeleton animation path. No changes to `main` or peer branches.
- Source commits:
  - `3e27331bd1461c66df5e60b4d99961d1d6de00a5` adds `engine/character/vivid/GroomSegmentCollision.h`: a deterministic optional CPU segment/capsule narrowphase across *different* hair guides. It hashes inflated segment AABBs into ordered uniform-grid cells, deduplicates pair IDs, computes closest segment points with bounded parameters, and projects contact corrections onto endpoint particles with a Jacobi scratch accumulator. Anchor segment endpoints (index zero) remain pinned. Both Verlet position/previous state are updated, attenuating residual motion by the configured friction factor.
  - `4e5d553934df57ee029a4873d47b3d932afbee89`, `1162592a3ae7338e0170b80f4c840a4ec89497fe` extend/validate `GroomSelfCollisionConfig` with `segmentEnabled`, `friction` and `bendPreservation`, all bounded to [0,1]. Existing callers retain default segment collision **off** for compatibility.
  - `8d560c70741ccf803ae00fbb4bfe959b2c7a68a6`: `HumanGroomRuntime::step` evaluates capsule segment collision after the existing guide-particle pass, accumulates pair/contact diagnostics, then reprojects local rest curvature and segment length (two passes) before the existing scalp/ellipsoid correction. MotionWeave source matrices and gameplay ECS are untouched.
  - `fac21eb6f711e3fbba19f8b1d202cc66c650bb62`: VIVID Creator provides explicit segment-collision enable, strand friction and curl-shape preservation sliders; Creator opts into the segment mode by default.
  - `6a51bb37d072134f273731358cd9ff3bf9c6ecbb`: caps CPU segment broadphase at 16,384 segments and 524,288 cell memberships per substep. A larger groom reports `budgetHit` and skips only the optional segment pass without touching current points or failing the parent groom step; the existing particle-level collision and scalp handling remain active.
  - `1b4d5359cd15cd8a05da409c5cbda5783a0b9c61`: avoids redundant bend/length follow-up if segment narrowphase found no contacts.
- Regression source commits `9701eeb8f6062f3a10ac3b58fb5f6802efc2205b` and `e756639d54fefe2aa0d155e9cd6ba4c3b2a45b5f` extend existing `tests/vivid/human_appearance.cpp`: two crossing segments with non-overlapping endpoint positions, finite movement/friction, pinned roots, invalid friction rejection, candidate-pair cap and no-modification safe skip when segment count exceeds memory budget.
- API: `HumanGroomRuntime::setSelfCollision(GroomSelfCollisionConfig)` is unchanged. New optional fields: `segmentEnabled=false`, `friction=0.2f`, `bendPreservation=0.25f`; Creator settings default segment collision ON. Existing `GroomSelfCollisionStats` aggregates both point and segment passes (so pair budget applies to each pass separately). Collision remains genre-independent, operating exclusively on groom-local guide positions.
- **ACTUAL BUILD/TEST EVIDENCE: NOT RUN** — GitHub source commits only. No native C++20 compilation, CTest pass/fail, GPU/Metal or Vulkan/GLES hair preview, photo comparison or profiling. User requested one consolidated test run after functional work is complete.
- Limitations: this is closest-distance capsule collision at the current substep position, not swept continuous collision detection across time. Endpoint Jacobi corrections and two curvature/length passes are approximate and do not guarantee absolutely nonpenetrating final cards in very dense hair, especially when the scalp pass pushes guides together. Grid and scratch allocations occur per substep; high-quality grooms exceeding the cap skip the segment pass. Segment-based friction damps Verlet residual velocity, not physically calibrated anisotropic fiber friction, static clumping, tangling or wet hair material behavior. Not yet GPU-accelerated. A later optimization should replace per-step `std::map` and `std::set` storage with persistent spatial hashing and benchmark dense 8192-guide high-LOD assets; add swept CCD if fast motion tunnelling is observed.
- Next: persistent/reusable segment broadphase storage and measured budgets, optional clump/group-aware collision and stability tests; then realistic skin/eye/hair optics, native Metal/Vulkan/GLES integration audit, finally the explicitly deferred `bash tools/vivid-validate.sh --full` on macOS and fix all observed failures.


## Stage 7X — 2026-11-20 — reuse CPU segment broadphase scratch across groom frames

- Branch: `feature/vivid-digital-humans`; `main` untouched. Builds/tests deferred by user's implementation-first instruction. Universal MotionWeave remains the only skeletal evaluator.
- Source `28b85087f5e53a0f02d2c8dcb1ba7def86da791f`: replaced per-substep ordered `std::map<Cell,vector<SegmentId>>` and `std::set<pair>` in `GroomSegmentCollision` with a contiguous sortable `CellEntry` list, sorted/unique unordered segment-pair IDs, flat Jacobi offset/correction buffers, and 1M pre-dedup raw pair guard. The sort produces deterministic ID order and ensures a segment pair spanning many cells only gets one narrowphase evaluation. An overfull cell aborts the optional narrowphase before applying any corrections.
- Source `2824522af5e97767a20149fe1e1c9448efeab272`: added `GroomSegmentWorkspace` to `HumanGroom.h`, with reusable cells, pairs, flat vertex offsets and accumulators. Each `HumanGroomRuntime` owns one workspace, so per-frame capacities are retained without a global cache or cross-world data races.
- Source `e75ea3957aaf2f2724e1d743a3290378cce1fd01`: refactored `GroomSegmentCollision` to accept the groom-owned workspace, preserving a three-argument scratch-local overload for existing tests/consumers.
- Source `4a97a54c649d328574630391e40292d760f8bdf4`: passes the owner workspace from `HumanGroomRuntime::step`, clears its active entries on generate and each solve, and reuses allocated capacity across Verlet substeps.
- Source `bb6c02e7528d4d1f0e5fe3edde6e6e851926744d`: completed the flattened readback of Jacobi corrections in the application pass, replacing obsolete nested per-guide `weights/delta` variables.
- Test source `ad8a063be0e1eba65390db118dce3961c2ca11f8` extends `tests/vivid/human_appearance.cpp` with reusable workspace tests asserting stable capacities and identical pair counts/contact results for repeated identical input, plus reset without capacity loss. Previous direct crossing/overload and dense-budget scenarios remain present.
- **Actually run: NOT RUN.** No native CMake, C++ compile/link, CTest, high-density benchmark, GPU/Metal/Vulkan/GLES runtime test, frame capture or sanitizer was executed on this merged branch. The refactor should be considered unverified until the final native acceptance pass.
- Safety behavior: max 16,384 hair segments and max 524,288 spatial memberships remain. A 1M raw pair guard prevents pathological within-cell quadratic allocation; if exceeded, the additional segment collision pass reports `budgetHit`, returns with unchanged model-guide positions, and leaves the parent particle/scalp response active. Candidate sorting/deduplication is deterministic. The final distance checks remain capped to `maxPairChecks`.
- Remaining limits: `segments` itself and the old point-level `GroomSelfCollision` grid are still allocated each substep, and sorting up to 1M raw pairs remains potentially expensive; real high-LOD profiling is necessary. This is CPU current-position capsule narrowphase, not swept CCD, GPU hair or real anisotropic fiber-friction simulation. Realtime peak allocations and wall-time were **not measured**.
- Next: profile on an actual Mac with 8k guides, investigate spatial pruning/coarser grid and allocator-backed persistent segment storage; refine PBR skin/eye/hair optics and real backend GPU parity, then run one consolidated `bash tools/vivid-validate.sh --full` as requested and fix actual compile/runtime regressions before merge readiness.


## Stage 7Y — 2026-11-20 — retain groom segment snapshot capacity across substeps

- Branch: `feature/vivid-digital-humans`; no `main` or peer branch modification intended. MotionWeave remains the sole skeletal evaluator and groom collision remains presentation-only, genre-independent.
- Source implementation commits: `071136f7a0792b6d5150cb7330bdda0bd1e39c25` adds `GroomSegmentWorkspace::Segment` and an owner-local `segments` vector in `engine/character/vivid/HumanGroom.h`, cleared without freeing capacity by `reset()`; `e40285cf2696b0c8f2874d029228c5fe24b9a126` changes `GroomSegmentCollision::resolve` to use `workspace.segments` instead of a new vector every substep, with a bounded reserve of min(16384, guides*8). Convenience overload still creates a transient workspace for standalone users. API signatures unchanged.
- Test-source commit: `def785623f65ccc8389354aee9a528c54b87e02a` extends `tests/vivid/human_appearance.cpp` to assert segment snapshot count/capacity, same capacity across repeated identical contact solves, and reset clearing active entries without releasing storage. Existing deterministic result and root-pinning assertions remain.
- **Actual compiler/build/CTest/GPU/benchmark: NOT RUN.** This stage was authored through remote GitHub commits, without access to the user's local Mac build or captured runtime output. No claimed speed improvement or green result. Follow the user's preference to finish features and run one consolidated acceptance sweep.
- Compatibility: workspace gains an internal scratch vector; existing public collision function signatures, DNA/JSON, save format, GPU shader, ECS and model asset IDs are unchanged. Earlier deterministic pair ordering remains unchanged because the segment vector is rebuilt in guide-index order on every solve; only its capacity persists.
- Known limits: the other particle-level broadphase still allocates per step; the sorted candidate membership/pair buffers can consume significant CPU under dense clumps. Segment scratch remains CPU-only and bounded at 16,384 segments for optional narrowphase. No high-density allocation trace, macOS graphics preview, Vulkan/GLES parity or compiler verification was performed. Beware concurrent writers to the same groom instance: scratch is explicitly owner-thread-local.
- Next actionable stage: audit and reduce per-substep allocation in `GroomSelfCollision.h`; add repeatability/limit tests for its scratch refactor, optionally instrument separate CPU cell entries, deduplicated pair counts and segment pass time. Then finish material optics/Creator validation; only after feature completion run `bash tools/vivid-validate.sh --full` on macOS and fix observed build/test failures. Append a separate committed handoff at every completed stage.


## Stage 7Z — 2026-11-20 — reusable sorted particle collision broadphase

- Branch `feature/vivid-digital-humans` (do not merge to `main` without consolidated acceptance). Existing MotionWeave skeleton evaluator, DNA formats and gameplay ECS remain unchanged.
- Implementation commits: `21c3aa7d19b8e714a03f8643b16373991748e855` adds `GroomParticleWorkspace` to `engine/character/vivid/HumanGroom.h` with owner-local particle snapshots, sortable cell membership entries, Jacobi correction/count arrays and reset-preserved vector capacity. `80ca06f5d9af6d8d54d65ad90fe3a99d5666ddc0` removes per-substep `std::map<Cell, std::vector<particleId>>` in `GroomSelfCollision.h`; a flat sorted `Entry` vector and binary search over 27 neighbors replace heap-allocated buckets. Global loop remains ordered by particle index and neighboring cell offsets, with ascending particle IDs inside each cell, preserving existing unique-pair visitation and deterministic correction order. Adds four-argument `resolve(guides,cfg,stats,workspace)` while retaining the original three-argument overload with temporary scratch. `e44faeabad37bb6e481714aa53e3bcf84c3d0528` wires owner-local workspace into `HumanGroomRuntime::step` and resets active scratch on generation.
- Regression-source commit `fadc7030088761a96e72400bd68aa54d08d8bafd` extends `tests/vivid/human_appearance.cpp` with standalone-versus-workspace collision counts, corrected positions, pinned roots, stable capacities over multiple identical solves, and reset-without-capacity-release.
- **Actual build/CTest/Metal/Vulkan/GLES runtime/benchmark: NOT RUN.** Changes are remote GitHub source edits. Do not infer compiled correctness or performance improvement. Existing 131,072 movable-particle limit, pair budget, per-contact uint16 count saturation, and rejection of invalid coordinates remain. Workspace is NOT thread safe for simultaneous solves on a single groom; continue render-owner-thread-only access.
- Performance expectation (not measured): one vector for membership entries and one sort per substep, rather than repeated map node and cell bucket allocations. `lower_bound` per queried neighboring cell may still dominate for 8k guides; benchmark on Mac against prior baseline, inspect allocations, time and contact parity, and consider a persistent open-addressing grid only if profiling justifies.
- Compatibility: original `GroomSelfCollision::resolve(guides,cfg,stats)` remains valid. New overload and workspace type are additive, no JSON/asset change. Runtime's scratch fields affect only render-local CPU presentation.
- Next: native compiler check and consolidated `bash tools/vivid-validate.sh --full` after remaining optical/Creator feature work as requested. Add 8,192-guide stress scenario with bounded timing/allocations and skip behavior, then inspect skin/eyes/hair material parity and Creator UI. Preserve separate stage handoff commits for further changes.


## Stage 7AA — 2026-11-20 — 8192-guide CPU collision stress and budget instrumentation

- Target branch: `feature/vivid-digital-humans`. No changes to `main`, MotionWeave, ECS simulation, DNA serialization or rendering backend. Follows Stage 7Z's persistent owner-local particle and segment scratch optimizations.
- Regression source commit: `e10d0c229d26a17f9d16eda3ff0a733eae10b496` extends `tests/vivid/human_appearance.cpp` with an 8,192-guide/8,192-segment sparse workload using two identical consecutive particle and segment solves; inspects pair counts, contact counts, finite positions, anchored roots and capacity retention of particle entries, segment cells and segment snapshots. An independent 8,192-guide densely overlapping workload verifies the segment pass's transactional raw-candidate budget skip (zero applied contact updates) and the point-level pass's configured 1,024 candidate-check limit, without moving anchored roots. A `std::chrono::steady_clock` diagnostic prints aggregate two-solve wall time plus member-entry counts for native profiling, not a synthetic pass/fail performance threshold. Follow-up source commit `39113af09781109352ab13effd16275792a08535` corrects the diagnostic newline.
- API: no public interfaces or save formats changed. Original three-argument collision entry points and persistent four-argument workspace overloads are exercised from the existing `vivid_human_appearance` test executable. Both CPU solvers retain current 131,072 particle / 16,384 segment / 524,288 cell entry and 1,000,000 raw segment pair safety policies, including the per-call `cfg.maxPairChecks` cap.
- **Actual build, CTest, renderer screenshot, sanitizer, hardware benchmark: NOT RUN.** Source changes were committed using GitHub; no executable was compiled or run in this step. Test assertions are NOT verified passes. A diagnostic wall-clock value is only emitted once the native executable is actually run on a Mac.
- Cautions: sparse case has exactly one short segment per guide and probes allocators/search work but is not representative of an 8,192-guide, eight-segment production groom; that higher count is beyond the optional segment narrowphase's 16,384-segment cap and deliberately skips it. The existing dense sample covers all-overlapping cells and intentionally does not apply the optional segment pass. Reference performance varies with hardware, so do not assert fixed millisecond pass criteria until a baseline and budgets are agreed.
- Next: inspect actual macOS build output during the deferred consolidated `bash tools/vivid-validate.sh --full` sweep, then correct any compiler failures and compare measured sparse CPU time and memory allocations to Stage 7X/7Z baselines. Finish realistic skin/eye/hair optical materials, native Vulkan/GLES path validation and Creator UX before marking the overall VIVID system production-ready. Continue committing separate handoffs for each stage.


## Stage 7AB — 2026-11-20 — incremental optical layering in human material graphs

- Branch: `feature/vivid-digital-humans`, `main` untouched. Implementation commit `4e87ff50cae16de0b83e77b7f054876baa26b9b5` modifies `engine/character/vivid/HumanMaterials.cpp`. Preserve existing render::ShadingModel::{Skin,Eye,Hair}, imported material texture bindings and dual GLSL/MSL ShaderGraph compilation; no new RHI specialization or independent shader path.
- Skin: keeps earlier pore/wrinkle, melanin and Fresnel-linked hemoglobin tint; adds a small bounded shallow warm contribution scaled by DNA `scatteringMm`, clamped in the existing color graph. This approximates a surface-layer contribution only; **not** thickness-aware, multi-scatter, screen-space or random-walk subsurface scattering.
- Iris: adds centered radial fiber noise and wider crypt variation to the existing pupil / limbus / sclera blend; continues to use existing UV material graph ops and independent corneal shell material. Does **not** implement refraction through a finite cornea, anatomical iris displacement or actual eye wetline.
- Hair: adds low-frequency anisotropic-looking longitudinal noise modulation to existing groom card Fresnel intensity; dedicated render Hair shading model remains the authoritative lighting path. This is not a multi-lobe Marschner/Chiang fiber-scattering model or geometry-level strand shading.
- Regression-source commit `5054a996da2a05b181c04b02bf389d0a7e439668` extends `tests/vivid/human_appearance.cpp` with low/high skin scatter depth, variable iris radius and differing hair melanin assertions that require both compiled graph GLSL and MSL sources to differ with DNA.
- Actual build / native CTest / real Metal Creator capture / GLES / Vulkan shader compilation / visual A/B / profiling: **NOT RUN**. Source-only commits are not evidence of runtime parity or aesthetic improvement. There are no API, save/JSON, ECS, MotionWeave or asset-format changes. CPU material graph compilation tests remain unverified until a real native run.
- Risks: extra gradientNoise nodes may cost fragment instructions on mobile and high-density groom cards; human BRDF parameters remain coarse and not physically calibrated. Compare GPU appearance and compile permutations across backend implementations before declaring optics production-ready.
- Next: inspect exact ModelShaders human BRDF implementations, add controlled optical reference captures and quality-scaled cost budgets, then fully verify native Metal/Vulkan/GLES shader paths where executable hardware/tools exist; finish Creator visual/authoring usability and run the requested consolidated `bash tools/vivid-validate.sh --full` on macOS. For every future stage record implementation SHA, actual test evidence and separate handoff commit.


## Stage 7AC — 2026-11-20 — corneal Fresnel and dual hair specular lobes

- Branch `feature/vivid-digital-humans`; `main` and peer agents untouched. Implementation commit `dd2e25de80b32db1b745addf75f50768182ee1fd` modifies `engine/render/ModelShaders.h` in the existing GLSL ES / MSL model fragment programs. Keep material dispatch through the packed `m_uv1` / `m.uv1` shading model and `humanProfile`; no separate renderer path, no new public API, no save format change.
- Eye: the previous fixed-`iorF0` analytic corneal glint now evaluates Schlick `coatFresnel = iorF0 + (1 - iorF0) * pow(1 - clamp(ndv,0,1),5)` in both languages. Maintains the prior finite-cost Blinn-Phong-like highlight, corneal-IOR clamp and tear-film controls; no physical corneal refraction, path tracing or energy conservation proof.
- Hair: replaces the one longitudinal ring lobe with two bounded power lobes. The primary uses the existing roughness-driven exponent range 16..82 and coefficient 0.72, while a broader secondary uses exponent 5..22 and coefficient 0.18. Keeps existing tangent input, profile strength and sun/shadow multiplication. This approximates a secondary surface highlight, NOT a spectral multiple-scattering Marschner/Chiang strand BSDF or GPU strand simulation.
- Regression-source commit `6aacd287ac303d673085135bcdb01940e432b633` extends `tests/vivid/human_appearance.cpp` with GLSL/MSL source contract assertions for `coatFresnel`, `primaryLobe`, `secondaryLobe`, shared lobe coefficients and packed human-material profile usage. These source-contract assertions alone do not prove pixel parity.
- **Actually executed native C++ compilation / CTest / GPU capture / Vulkan GLSL compilation / GLES shader compilation / Metal runtime: NOT RUN.** Remote GitHub source writes only. Numeric luminance, image and mobile fragment cost remain unmeasured; do not claim physically accurate appearance or tested parity.
- Compatibility: `HumanDNA::kVersion`, material graphs, mesh format, public C++ interfaces, MorphWeave/MotionWeave and ECS simulation state unchanged. Normal non-human PBR materials retain their previous model-shader dispatch. User requested deferred consolidated acceptance after feature completion.
- Next: add an explicit GPU A/B material fixture for varying corneal IOR/roughness and hair highlight angle, then run actual Metal frame capture and numerical GLSL/MSL comparisons; validate Vulkan/GLES with real shader toolchains/devices where available. Profile mobile cost and revisit physically calibrated skin profile / scatter approximation. Run the final `bash tools/vivid-validate.sh --full` on macOS and remediate any observed failures before production-ready/merge claims. Every next stage needs a separate committed handoff.


## Stage 7AD — 2026-11-20 — quantitative native Creator optical A/B capture reporting

- Branch `feature/vivid-digital-humans`; no `main` merge. Existing Creator neutral/warm PPM capture and independent `compare_ppm.py` pixel threshold remain the actual native visual verification path. No changes to render::Model, material API, MotionWeave, DNA version, or authoritative ECS.
- Implementation commit `bbed93f47def0540a28b67e79a725a57a978e2b1`: new `tools/vivid-creator/optical_ab.py` parses strictly bounded P6/255 screenshots, checks dimensions and full RGB payload, crops to the image's right-side 3D viewport (excluding the left ImGui inspector), computes changed pixel fraction, per-channel absolute RGB differences, per-channel changed counts and max RGB difference. It can write a machine-readable JSON report and enforces a configurable minimum change threshold. The report only records image differences and must NOT be interpreted as isolated corneal optics, hair lobe or physically accurate skin.
- Integration commit `c8bd40d423ccf1427ae83d520a311805610d84a9`: `tools/vivid-validate.sh` retains existing GUI Creator neutral/warm capture and `compare_ppm.py`, then runs `optical_ab.py` against the same captured images, writing `$LOGDIR/creator-optical-ab.json` alongside normal logs. This runs only when GUI capture is enabled. The normal terminal summary contains an additional diagnostics line; success is asserted only if the full script executes.
- Regression source commit `203c5f11d8d76db1ae553c31ac24c1de3ac3eda7`: `tools/vivid-creator/test_optical_ab.py` uses synthetic temporary P6 images to verify the exact cropped pixel count, no-difference case and malformed payload rejection. This is a script-level test; production GPU optics still require native captures.
- **Actual execution status: NOT RUN** for Python test, full CMake, CTest, Creator GPU capture, Metal, Vulkan and GLES. GitHub source commits alone do not establish that these checks pass. When acceptance is authorized, first run `python3 -m unittest discover -s tools/vivid-creator -p 'test_optical_ab.py'`, then `bash tools/vivid-validate.sh --full` on a supported macOS machine, capturing exit codes and generated JSON.
- Limits: neutral/warm presets change multiple DNA properties, so this A/B comparison is a general material-response smoke test, not a controlled one-parameter BRDF experiment. Timing, render nondeterminism, shadow movement and tonemapping may influence differences. No isolated IOR, skin scatter thickness or hair tangent sweep fixture is implemented at this stage. The Python reader assumes the Creator's existing three-line P6 header and explicit fixed viewport crop and deliberately bounds maximum resolution.
- Next: implement dedicated one-variable Creator capture controls (cornea IOR, roughness, hair tangent/light direction and skin scattering) without changing the default presets, record the selected controls in each screenshot manifest, and define stable named 3D-only ROIs. Add optional optical quality tiers for mobile and real Metal/Vulkan/GLES compile/runtime validation. Do not mark the optical models production ready until hardware captures and tests have actually passed. Continue one separate handoff commit per stage.


## Stage 7AE — 2026-11-20 — isolated optical DNA GPU capture scenarios

- Branch: `feature/vivid-digital-humans`; `main` unchanged. All optical changes remain within VIVID Creator capture tooling and appearance regression sources. MotionWeave is still the only skeleton evaluator; no changes to authoritative ECS simulation, DNA serialization/version or runtime material APIs.
- C++ case table source commit `02642c74ed7c136d279e016125fba7c29024c07f`: `tools/vivid-creator/OpticalCaptureCases.h` defines seven valid named cases, including a `reference` neutral and low/high pairs for precisely one `HumanDNA` scalar each: `skin.scatteringMm` 0.10/4.0, `eyes.corneaIor` 1.25/1.45, `groom.roughness` 0.08/0.90. No other DNA fields are intentionally modified. No imported assets or proprietary character DNA.
- Creator integration source `a84d4856df3891bb1eb995a6451d552bc62ff97d` and newline fix `5392d751b43bc2cbdfb290d2c4fe1df73c7094dd`: `tools/vivid-creator/main.cpp` recognizes `--optical-case CASE` only with `--smoke`, rejecting combinations with `--preset` or `--asset`. It starts from a neutral human, applies its named case through validated `HumanCreatorSession::edit`, and freezes MotionWeave, guide simulation and wind, retaining the Creator's original fixed starting camera, lighting, capture path and 3D viewport render path. Unknown case reports a command-line error before GPU startup. Existing normal Creator and `--smoke --preset warm` behavior is unchanged.
- C++ regression-source `fcc2a8ee0a8e04617932608489325738ba338a52` adds isolated-case validation, precise target values, untouched independent DNA fields and invalid-case no-mutation checks in `tests/vivid/human_appearance.cpp`.
- ROI/diagnostic source `cad3ef5a805888adb486bb7a08edcf1baf3e51f8` updates `tools/vivid-creator/optical_ab.py` to accept normalized `--roi x0,y0,x1,y1` **relative to the existing UI-excluded viewport crop** and `--label`. `b37d6b8b7254206b54f3a585b5988bdddcc5830e` adds `--parameter`, `--reference-value`, `--candidate-value` fields to the JSON report, making the numerical input delta explicit. Python regression source `8a3ea452b0e23c72bc06eb5d36aa286201c74f16` adds ROI crop/count assertions and invalid/NaN input rejection to `test_optical_ab.py`.
- Integration source `2c4e27b0dfceb7279a86860fe7479f06c3242e4d` extends `tools/vivid-validate.sh` GUI smoke acceptance with seven sequential native Creator capture runs (`reference` and low/high pairs), nonempty PPM and `pass=1` checks, then separate scene-cropped RGB JSON A/B comparisons for skin scatter, corneal IOR and hair roughness. Subsequent commit `e6575144f6bf459eac2e503f9b11cfcda346c954` adds deferred `test_optical_ab.py` to the same final acceptance script and embeds the exact DNA parameter/value metadata in JSON output. Named reports under `$LOGDIR`: `optical-skin-scattering.json`, `optical-cornea-ior.json`, `optical-hair-roughness.json`. Threshold is currently a low smoke gate (`--threshold 4 --min-changed 1`), **not** artistic or physically based acceptance.
- Human Creator documentation commit `935a6104168a21af72ee9c59616a3116d2798568` adds a command example, case table, reports and caveats to `tools/vivid-creator/README.md`.
- **ACTUAL TESTS / BUILD / CAPTURE: NOT RUN.** No C++20 compiler, Python unit test invocation, CMake/CTest, native macOS Creator session, captured PPM, GPU shader compile, image/pixel validation, Metal/Vulkan/GLES execution, or timing/performance benchmark occurred in this remote GitHub source stage. Do not claim checks passed. Source assertions alone are unverified until executed.
- Risks: ROI rectangles are *provisional*, broad normalized viewport rectangles selected without hardware images; they are not segmentation masks for the anatomical skin/cornea/hair materials. Fix/check against actual GUI screenshots before tightening region thresholds. Different presentation-frame durations may still affect ImGui transient state (left inspector is cropped), though animation and wind are frozen. Corneal Fresnel effects may be very small and require calibrated exposure or a better isolated analytic lighting fixture. The new seven sequential GUI captures add execution time to full visual validation. All P6 screenshots and JSON are diagnostic, not physical BRDF validation. Vulkan and GLES not tested; a GPU source-level GLSL/MSL comparison is not equivalent to backend compilation and pixel parity.
- Next actionable stage: obtain a real macOS Metal run and inspect `$LOGDIR/optical-*.ppm` and JSONs for actual coverage, then refine fixed target ROIs and minimum meaningful pixel thresholds; implement configurable mobile/desktop optical quality tiers with bounded ShaderGraph instruction cost. Continue evaluating physically calibrated skin thickness and corneal refraction separately. After feature completion, run `bash tools/vivid-validate.sh --full` once and address all observed compiler/CTest/GPU discrepancies before merge to `main`. Each future stage requires its own implementation and committed handoff.


## Stage 7AF — 2026-11-20 — explicit Low / Medium / High human material quality profiles

- Branch `feature/vivid-digital-humans` only; `main` unchanged. Same standard AIVERSE ShaderGraph compiler, Renderer3D, character geometry, MotionWeave skeleton and presentation ECS. No new RHI fork, genre dependency, HumanDNA field or JSON/save version.
- Public API implementation `263be133c2b3abd32ab02f89ad028722e2b7dd10`: `engine/character/vivid/HumanMaterials.h` adds `enum class HumanMaterialQuality {Low,Medium,High}`, `humanMaterialBudget(quality)` and overloads of `humanMaterial`, `humanMaterials`, `humanMaterialFromSource`. Old argument lists/symbols remain present and choose **High**. Current graph limits: Low <=24 nodes/0 graph texture samples, Medium <=48/0, High <=128/0. These are ShaderGraph DAG budgets, not guaranteed compiled GPU instruction/total texture costs.
- Core material implementation `269448555620b2055534c017d5e64850398a588b`: `HumanMaterials.cpp` adds low-complexity graphs and medium partial-detail graphs. Low uses fixed skin pigment, pupil/iris masks and simple hair melanin color with no procedural noise and generic `ShadingModel::Pbr` for skin, iris, cornea, hair and eyelashes. Medium retains Skin/Eye/Hair specialized renderer BRDFs but uses a simpler skin Fresnel/hemoglobin tint, single iris grain and one simple groom variation; High preserves the previous full graph paths and specialized BRDF selection. `enforceGraphBudget` rejects generated graphs beyond chosen ShaderGraph node/sample limits. All tiers preserve imported material texture slots, alpha-masked hair, cornea transparency and PBR material factors. The existing ShaderGraph compiler still emits paired GLSL/MSL.
- Preview/groom integration commits: `cd9b6a24475d3fdb57f72121a3034693917d7249` and `c3a5959e69304099722cb15a33d1e11c461ce3bb` add a new `HumanGroomRuntime::makeCardModel(dna,quality)` while retaining legacy `makeCardModel(dna)` defaulting to High; `834577c28527b6d065795bca836dfdca8ac2e0b7` and `3c12ec29aa4e0d4b8d82aec7bd22884d44b6135e` add `generateHumanPreview(dna,groomQuality,materialQuality)` with the legacy two-argument API preserved and Low/Medium/High selection propagated to skin, eyes and separate groom card model.
- Creator integration: `e58475296656e5d4a25ad11a9586609fd0951cdd` adds a standalone material quality toggle for the procedural reference and imported material replacement, `--material-quality low|medium|high` CLI and rebuild on UI change in `tools/vivid-creator/main.cpp`; `c247376f5e84c02295b2cf16668114a8927f037a` fixes CLI error-text newline. Default remains High, including pre-existing optical A/B cases, normal interactive authoring and old smoke flows. The material tier is a render setting, not serialized into HumanDNA.
- Regression-source commit `2ad32b65b209de2a4252fc0b3db2efd217d053b2`: `tests/vivid/human_appearance.cpp` checks all tiers compile nonempty paired GLSL/MSL programs within declared node/sample budgets, strict low < medium < high graph nodes for skin/iris/hair, default-versus-explicit-High shader source equality, correct PBR vs dedicated human shading selection, preserved source texture slots, low-tier procedural groom material and unchanged DNA JSON.
- Acceptance-script implementation `a72a63bfdd5531a096fe06ebd145cb7faed03c46` adds sequential Low/Medium Creator GPU smoke captures to `tools/vivid-validate.sh`, `23541e41c9897db0b891c561a699c7bdda69624b` corrects a concatenated terminal summary line and the new failure printf format. Creator will write `vivid-material-low.ppm` and `vivid-material-medium.ppm` in the usual `$LOGDIR` when the final suite actually runs.
- Public usage documented in `tools/vivid-creator/README.md` at commit `4bba650530471fdb3d68aca7abbb195d80c09102`: quality profiles, overload samples, smoke CLI and realistic limits.
- **Actual C++ compilation, Python tests, CTest, Metal GUI runtime, Vulkan/GLES shader compilation, native device capture, GPU frame timings, material visual comparison: NOT RUN** in this source-only remote GitHub stage. No tested-performance or measured shader cost claims. The user deferred a full acceptance run until features are complete. This source-level implementation has NOT been verified as buildable on a local macOS checkout.
- Known limitations/risks: graph node/sample cap is an authoring budget, not a measured cost of the full ModelShaders PBR fragment pipeline or imported albedo/normal maps. Medium still executes dedicated human BRDFs; Low intentionally loses specialized skin/eye/hair appearance and may require color recalibration. Only GLSL/MSL generation is wired through existing shaders; real Vulkan/GLES shader dialect/compiler parity remains unverified. Runtime per-material tier switching causes model material recompilation and GPU-safe Creator candidate publication, not per-frame automatic dynamic scaling. It is a presentation setting without save persistence; imported files and animation are unchanged. A production readiness decision needs actual GPU screenshots, functional compiler tests and performance measurements on representative devices.
- **Next actionable stage (7AG):** add a separately controllable quality preference in project/renderer presentation settings, verify no duplicate owner of quality policy, collect real render graph instruction/profile timings for Low/Medium/High on Metal and GLES/Vulkan where available, and adjust thresholds/visual deltas. Before final merge, run `bash tools/vivid-validate.sh --full` on the user's Mac with a clean worktree, record actual process exit codes and fix any native compiler, CTest, UI, GPU or parity defects; maintain a separate chronological handoff commit for each stage.


## Stage 7AG-Interop — 2026-11-20 — Texture RHI cross-agent audit / dependency gate

- Branch `feature/vivid-digital-humans`; **documentation-only** source checkpoint `4d99568dd232bf05d5c66e73aa4f3de2b3a7383f` adds `docs/agent-handoffs/vivid-texture-rhi-integration-2026-11-20.md`. No actual engine source, `main`, peer texture branch or GPU ABI was changed.
- User requested inspection of the parallel texture agent's work during planned VIVID Stage 7AG. Reviewed `feature/texture-rhi-unification` files `TextureAsset.h`, `TextureStreaming.h`, `TextureResidentStreamer.h`, `GpuMaterialResourceTable.h`, `ModelVirtualTextureSampling.h`, `VirtualTextureDeformedFeedback.h` and `ModelVirtualTextureFallback.h`, plus texture handoffs and branch divergence.
- Important outcome: reuse the canonical model texture import, container compression, sampler/mips/streamer, material resource identity and GPU retirement code when branches can be integrated. **Do not wholesale-import the texture branch now**; it and VIVID modify overlapping Model/Renderer3D/ShaderGraph CMake and shaders and there are unresolved suite/Metal validation gates. VIVID already has model-local compressed image storage; do not duplicate it in human DNA or Creator.
- **Critical incompatibility:** texture feature's coarse VT fallback refuses many shaderGraph, blended/alpha, shared non-base usages. High/Medium VIVID skin and groom shaderGraph, transparent cornea and hair card coverage cannot be blindly virtualized. Handle VT via per-material eligibility/failure reason in shared renderer after integration, not at VIVID authoring layer.
- Additional limitation: `GpuMaterialResourceTable` is CPU staging/identity only, not yet heterogeneous descriptor/ICB GPU material binding. Validate old texture leases + GPU completion before publishing replacement materials; do not bypass `HumanRetirementQueue`.
- Tests actually run for VIVID or this integration: **NOT RUN**. Texture handoff reports an earlier user-provided Mac Studio 132/133 full-engine run with `aiverse_tests` SegFault and an unverified later Metal gradient compiler fix; those observations do not establish latest branch PASS. No local CMake/CTest, shader compiler, texture GPU or VIVID Creator run occurred in this interaction.
- Next exact step: resume VIVID Stage 7AG as a bounded, independent project presentation settings implementation (material tier + future streaming budgets) using currently compiled public headers only. Then arrange a dedicated VIVID×Texture RHI integration branch after the texture owner publishes an API freeze and actual acceptance evidence; preserve both feature branches untouched, resolve shared renderer conflicts semantically, run the final macOS CMake/CTest/GPU and Vulkan/GLES checks as available, append separate handoff before any main merge.



## Stage 7AG — 2026-11-20 — centralized project VIVID presentation settings

- **Branch:** `feature/vivid-digital-humans`; `main`, `feature/texture-rhi-unification` and other agents' branches unchanged. Followed the user's request to review the texture agent while continuing quality-settings implementation. Preliminary cross-agent read-only audit and dedicated contract doc: `docs/agent-handoffs/vivid-texture-rhi-integration-2026-11-20.md` source commit `4d99568dd232bf05d5c66e73aa4f3de2b3a7383f`, separately appended Stage 7AG-Interop handoff at `2e80285248d55306ffc95794f3ee28456d303f88`.
- **Implementation:** `1760f98308080bda91b91c18564cdc9759a5a7f8` adds `engine/character/vivid/VividPresentationSettings.h`, a reusable genre-neutral `VividPresentationSettings` value with `materialQuality` and `groomQuality`, both High by default, strict versioned JSON schema (version 1, exactly two tier keys), explicit parse/string helpers and bounded file load/atomic staged save. It contains **no Texture RHI header**, GPU handles, streaming state or serialization inside HumanDNA.
- **Creator migration:** `93e2d1c5c05007ec642c96439b577f6cff199abf` replaces separate Creator material/groom fields with one presentation profile value and adds GUI file Save/Load plus CLI `--presentation-config FILE`, `--groom-quality low|medium|high` (retaining `--material-quality`). Profile is read explicitly; CLI flags apply in order. Interactive edits still trigger GPU-safe character candidate rebuild/retirement. Existing workflow, `--preset`, optical captures, DNA Save/Load remain unchanged.
- **Single-profile API:** `32fdfbd789ac4863fb056f0c00a2e6b797ce3c90`, `0480b102265dcbdb3a934fcf99057b0b2c0ce44d` add `generateHumanPreview(dna,const VividPresentationSettings&)`, mapping one profile to existing groom/material tier overloads. `c8c801c346a1716ff2fdbdf2aba217bd66be7447` makes the Creator procedural path consume the unified profile. Existing `generateHumanPreview(dna,GroomQuality)` still defaults to High materials; no binary save/palette/mesh format changes.
- **Sample config:** `80ada8138362dc9b9a444ec1de3d865e37899ee5` adds `tools/vivid-creator/vivid-presentation.example.json` (High/High); `2e1c6fbf548821e10acf1fa5880af6f1f0cbccc2` adds `tools/vivid-creator/vivid-presentation.mobile.json` (Low/Low). `72c1caea8cabd8b456efdd28467a607057e343db` changes the existing Low Creator smoke capture in `tools/vivid-validate.sh` to use that file, leaving Medium's explicit CLI smoke test intact.
- **Regression sources:** `a85f3ff6cb0ee8244d80a16d6819b0e7032660f0` and `6c04112ad929d4ace1be9baf21c296b20a286ca7` extend `tests/vivid/human_appearance.cpp` with all nine Low/Medium/High JSON profile combinations, strict unknown-version/type/tier/property rejection, unchanged HumanDNA, and the unified preview overload's actual low-tier hair material selection.
- **Documentation:** `9f15d91aebd4872d1d1b4af5c9382d6b2329064d` updates `tools/vivid-creator/README.md` with project-level config schema, API, examples, override behavior, texture interop boundary and explicit limitations.
- **Actual runs:** source GitHub create/update and source inspections succeeded. **C++ compilation, Python tests, CMake/CTest, native macOS Metal/GLES/Vulkan GPU captures, ShaderGraph compiler or gameplay build: NOT RUN**. Do not claim the new header and CLI are compiled or GPU-tested; user deferred full test sweep until features complete. No texture agent code was imported or merged.
- **Architecture/compatibility:** material tier selection still occurs at VIVID model preparation time and renderer sees its output Material; this is not a universal global Renderer3D flag and unrelated games must opt into the new profile API. No gameplay ECS authority, MotionWeave pose, DNA v1 JSON, imported texture IDs, or renderer texture ABI changed. Added profile schema is independent of actor assets and can be extended later under a new version.
- **Risks / texture coordination:** The texture branch still owns TextureViews, samplers, upload budgets, compressed container RHI, mip residency, optional VT and model resource identity. Do not expose unimplemented streaming toggles in the VIVID profile. Texture's `ModelVirtualTextureFallback` rejects many shaderGraph / blended / alpha-sensitive paths; VIVID skin/high-detail groom/transparent cornea must not be blindly virtualized. `GpuMaterialResourceTable` is CPU staging, not complete per-command GPU ICB. Texture handoff reports a prior user-supplied 132/133 full-suite failure and an unverified Metal-gradient shader fix; no current full green acceptance. Shared renderer and CMake files are divergent.
- **Next exact action (Stage 7AH):** inspect project-wide presentation ownership and decide how game hosts should opt into this profile; keep Material quality and future Texture Residency separate. When Texture RHI feature API and actual native acceptance stabilize, use a dedicated integration branch and reconcile `Model.h`, renderer/shader graph and CMake one file at a time, add explicit per-material VT eligibility, preserve `HumanRetirementQueue` completion fencing, and test imported scanned skin/normal/MR/eye/hair assets. On macOS run `bash tools/vivid-validate.sh --full` and selected Texture RHI full-engine/GLES/Vulkan gates on an exact merged SHA. Append the next separate handoff with real results and limitations. Do not merge either feature to `main` before review.



## Stage 7AH — 2026-11-20 — opt-in game-host quality bridge to the existing Renderer3D policy

- Branch: `feature/vivid-digital-humans`, `main` and parallel `feature/texture-rhi-unification` unchanged. This stage follows the Stage 7AG central VIVID presentation JSON policy. Read `docs/agent-handoffs/vivid-texture-rhi-integration-2026-11-20.md` for the unresolved texture merge gates.
- Discovery: `engine/render/Renderer3D.h` already declares the canonical `render::MaterialQuality {Low,Medium,High}`, `Renderer3D::setMaterialQuality`, `materialQuality()` and `defaultMaterialQualityFor(device,mobile)`; the existing game client chooses the default renderer tier from platform capabilities. Do not add a duplicate engine-level quality owner or interpret Render3D material quality as Texture RHI streaming quality.
- Implementation SHA `832b87f93630f49bb2cd6c7a31500bf656c84eac`: new header-only `engine/character/vivid/VividRendererQuality.h`. Explicitly maps `HumanMaterialQuality` to/from `render::MaterialQuality`. `resolveVividPresentation(projectRequested, rendererTier)` returns a new value with the material tier capped to the renderer's existing capability setting; it rejects an invalid project policy. It deliberately retains the independent groom CPU/geometry tier. `applyVividMaterialQuality(Renderer3D&,projectSettings)` is an opt-in Creator/host helper that calls the canonical renderer setter and rejects invalid settings. No new texture ownership, CMake library target, persisted format or game-system dependency.
- Creator implementation SHA `b9869f2da9a1378368fbf81fbf19430869514c31`: `tools/vivid-creator/main.cpp` includes the bridge and applies `presentation.materialQuality` to Renderer3D at initialization and when the visual profile changes, before the next candidate regeneration/render. Renderer3D's existing `setMaterialQuality` retires previous pipeline programs; the existing `HumanRetirementQueue` still governs old character/texture resource retirement. This opt-in synchronization keeps the Creator's actual render-material tier aligned with its authored VIVID shader-graph tier.
- Regression-source SHA `06f91d2d2ffd432b72467dbbc4651e2a0eb0366f`: `tests/vivid/human_appearance.cpp` adds bidirectional tier-mapping assertions, renderer-cap-to-effective-value checks, no accidental quality escalation, preservation of independent hair-guide tier, bounded actual skin ShaderGraph nodes at resolved tier, fail-closed invalid VIVID settings, and unchanged HumanDNA recipe. These are **unexecuted** regression assertions.
- Documentation SHA `4bbd466044df659c5eb292a13d4c97c01681f768` updates `tools/vivid-creator/README.md` with native game/project host integration, single-owner quality contract, frame-boundary GPU lifetime rules and Texture RHI boundary.
- API/asset compatibility: old VIVID material / preview / groom overloads and DNA v1 remain High by default; game hosts can opt in without changing RTS/FPS/general renderer or View. No new ECS components, gameplay or network data, no renderer ABI or ShaderGraph compiler modifications. The project settings file remains explicit version 1 `materialQuality` + `groomQuality`. `resolveVividPresentation` is a pure non-mutating snapshot operation. The Creator is the only existing host wired to apply the renderer tier; other hosts must opt in and rebuild materials safely themselves.
- **Actual verification this stage: NOT RUN** — inspected GitHub source and completed remote GitHub source/documentation commits only; did not execute compiler, CMake, CTest, Python scripts, a native Metal Creator GPU run, Vulkan/GLES shader compiler or frame capture. No performance or material visual parity claim. User requests feature completion before consolidated native acceptance. The new source is not certified buildable until compiled.
- Limitations / risks: changing Renderer3D global material quality affects all materials drawn by the Creator; general games should choose their render tier first and use `resolveVividPresentation` for the effective human quality rather than changing a shared renderer to suit one human. Groom CPU simulation/LOD quality is independent, so on very weak devices the host must separately cap `groomQuality`. Per-tier ShaderGraph node counts do not measure GPU fragment instruction cost or texture residency. Renderer material program retirement and old human texture lifetime need live GPU validation. Parallel Texture RHI currently has unresolved full-suite/Metal gates and incompatible alpha-sensitive VT fallback; no cherry-pick or merge performed.
- **Next actionable stage 7AI:** finish the VIVID functional scope audit and any remaining Creator authoring gaps; add a source-only CI script or manifest that checks VIVID/Texture shared-file conflict surfaces at exact branch heads without modifying the texture branch. After Texture RHI API freeze and required acceptance, create a separate integration branch, resolve `Model.cpp/.h`, `ModelShaders.h`, `Renderer3D*.cpp/.h`, ShaderGraph and CMake conflicts semantically. Then run the user's deferred `bash tools/vivid-validate.sh --full` on macOS, collect actual process exit codes and captured images, and perform available Vulkan/GLES parity checks. Record handoff after each stage; do not mark latest source PASS before evidence.


## Stage 7AI — 2026-11-20 — Creator feature-completeness audit and read-only Texture RHI overlap inventory

- Branch `feature/vivid-digital-humans`; `main` and `feature/texture-rhi-unification` untouched. User requested continued VIVID development and safe preparation for eventual texture integration. This stage makes **no production renderer, gameplay, DNA, GPU ABI, shader, or networking changes**.
- Feature inventory: inspected `tools/vivid-creator/main.cpp`'s actual ImGui controls and core VIVID headers. There are source paths for procedural DNA editing, imported glTF/FBX/OBJ/manifest assets, rig corrections, skin-weight authoring, morph/LOD authoring, CPU groom and material quality tiers. Outstanding release-scale functionality remains topology-aware body/face sculpt, exact final skinned+morphed GPU surface picking, comprehensive artist rig tooling, advanced hair rendering/simulation, physically calibrated optics and target-device GPU verification. A full prioritized audit with ownership and limitations is committed as `docs/agent-handoffs/vivid-functional-audit-2026-11-20.md`, source SHA `29d8059afe4289e76ee4497803fe490efbed0395`.
- Implementation `01304d0a55782eb0fe3c8a1a694b7b3faae0f5fb`: new `tools/vivid-creator/check_texture_rhi_overlap.py` inspects fetched VIVID/Texture branch refs with `git merge-base`, then evaluates each HEAD's changes independently against that common ancestor via NUL-delimited `git diff --name-only`. It emits deterministic JSON (exact ref SHAs, common SHA, source-only/texture-only/overlapping path lists, critical Renderer3D/Model/ShaderGraph/CMake paths). All operations are read-only by default: **no checkout, merge, fetch, commit, worktree, remote update or file write**. An optional `--output` writes just the report; `--fail-on-overlap` makes a CI gate. Path overlap is a **risk inventory, not an exact textual merge conflict oracle or compatibility proof**.
- Fixture source `80e689feb833e1543db102c874010fe6daeac882`: new `tools/vivid-creator/test_texture_rhi_overlap.py` checks deterministic overlap classification, branch merge-base, changed-only path sets, and that running inventory on a scratch independent Git history does not move the active checkout or alter its index/HEAD. `bfa6ea641a40103d09dbc60302fcdf14bd2d0b4f` registers this fixture in the existing `tools/vivid-validate.sh` one-pass Python fixture section, preserving the user-requested deferred consolidated testing policy.
- API/asset compatibility: no new C++ APIs, no public ABI additions, no DNA/project settings schema changes, no GPU shader or render Model changes; no branch merge. The overlap utility is Python 3 stdlib plus system Git, remains outside CMake runtime. It is only an integration-audit tool. The exact full overlap manifest has **not** been computed against a local complete AIVERSE checkout; a truncated GitHub compare result is not a substitute.
- **Actual verification status:** GitHub source reads, source commits and documentation updates succeeded. Python unittest **NOT RUN**, overlap script against local Git refs **NOT RUN**, CMake/native C++ **NOT RUN**, VIVID/engine CTest **NOT RUN**, Metal/Vulkan/GLES runtime or screenshot **NOT RUN**. Test code being added must not be misreported as test pass. Texture agent's previously reported 132/133 full-engine test result has an unresolved `aiverse_tests` SegFault on another SHA; causality/repair is unverified and unrelated to this audit until a stack trace demonstrates otherwise.
- Known limitations: the tool identifies changed-file overlap only; semantic ABI conflicts may exist without common changed paths, and multiple independent renames may require manual assessment. Dependencies on unmerged Texture RHI cannot be compiled from VIVID alone; no automatic VT enable for human skin/cornea/alpha hair. The feature inventory reflects visible implementation paths, **not** full artist usability, GPU pass status or production readiness.
- **Next exact step:** in a separate worktree/checked-out repository where both refs have been fetched, run `python3 tools/vivid-creator/check_texture_rhi_overlap.py --output /tmp/vivid-texture-merge-risk.json`, inspect source HEAD SHAs and critical paths, and separately run the synthetic Python fixture when performing the user's consolidated acceptance. For new functionality, Stage 7AJ should take one bounded authoring gap (for example exact deformed-surface click picking or non-destructive morph brush improvements), write implementation and separate handoff, then continue. When Texture RHI publishes stable API and actual native checks, create an isolated integration branch and resolve overlapping renderer/shader/CMake semantics one file at a time. Do not modify `main` or claim integration readiness beforehand.


## Stage 7AJ — 2026-11-20 — morph-first, skin-aware source-vertex viewport sculpt picking

- Branch: `feature/vivid-digital-humans`. Other agents' branches, main, Texture RHI and MotionWeave core untouched. Scope: CPU editing/selection reference only, no authoritative gameplay ECS, DNA schema, generated vertex IDs, model buffer ABI, GPU shader or RHI changes.
- Implementation SHA `6796e36af2652a0d457d17b2ee6b30176d143de2` extends `engine/character/vivid/SkinnedWeightPick.h`: `prepareSkinnedWeightSurface(...,out,optional deformedBindPositions)` may receive a full-size morph-deformed **bind-space** vertex array, used instead of ModelVertex::pos before the established bone palette and MotionWeave inverse-bind transform. Existing no-extra-argument callers still behave as before. `pickSkinnedWeightVertex(...,origin,direction,optional sourceVertexCount)` rejects generated/LOD triangles while keeping the original, unlimited overload behavior.
- Implementation SHA `2116235801b3870a82f0f6eee166e88f63ca7d04` creates header-only `engine/character/vivid/DeformedSculptPick.h`. It reads the **published** SparseMorphLibrary vertex-major CSR offsets/links/deltas and per-frame FaceShapeBinding-evaluated weights; accumulates morph position offsets in original bind space; then uses `prepareSkinnedWeightSurface` to apply the actual skin pose before ray-triangle selection. Picking only happens on the user's click, not every frame. Original model is immutable; it rejects invalid/mismatched vertex IDs, morph weights, nonfinite positions, node matrices, palette mismatches or a missing ray intersection. Source-authorable IDs remain bounded by `sourceVertexCount`. It caps CPU reconstruction to 1,000,000 source/model vertices.
- Creator SHA `83a8ee09eeaa742dd958632ca2f3f3a4c8a7a604` replaces previously incorrect rigid `pickSculptVertex(model,mesh,facePose,ray)` with `pickDeformedSculptVertex(model,mesh,facePose,publishedFacialShapes,facialBinding,controller.frame(),ray,sourceVertexCount)` in `tools/vivid-creator/main.cpp`. The selection marker now samples the deformed world-space hit position instead of projecting a base-pose vertex. To avoid displaying a stale location while animation advances, the sampled marker is cleared before each animated frame; paused previews may retain it if the chosen source ID and mesh remain the same. Missing or invalid pose/morph paths **fail closed**, not silently using bind-pose geometry. Skin-weight brush/heatmap path remains unchanged except optional extension point.
- Regression-source SHA `5e0902740818ad211941584081168af021bb0deb` extends `tests/vivid/human_creator.cpp` with a 3-vertex triangle, single-palette LBS joint translated +2 Z, one `JawOpen` target adding +0.4 X at source vertex 0, and checks deformed ray selection returns source ID 0 and the expected world position. Checks neutral morph/skin result, missing-ray case, `sourceVertexCount` excluding a triangle, invalid MotionWeave matrix, invalid skin palette and invalid morph library transactional scratch preservation.
- User-facing documentation SHA `eaecfae9fee9182172abc9a3830470d5375903e6` adds Stage 7AJ to `docs/vivid.md`.
- API/compatibility: all existing C++ VIVID Scene, Morph, Skin, HumanPreview and Creator APIs remain source compatible; the skin-aware picking helper adds optional arguments, and the new DeformedSculptPick header is opt-in. No CMake source additions required: both are header-only. GPU actor's published morph is transformed before native skinning, matching the VIVID GPU mesh-stream ordering. This is a **CPU reference**, not a certified 1:1 GPU raster oracle.
- **Actual verification:** remote GitHub reads and implementation/docs writes succeeded. Native compilation, CTest, macOS Creator scene, Metal shader compile/GPU readbacks, Vulkan/GLES real shader parity and performance benchmarks: **NOT RUN** (user requested finishing features before one consolidated validation sweep). New source tests are not PASS claims.
- Limitations: matching the final pixel's visibility, inter-mesh occlusion, GPU shader float precision, cloth/other post-skin deformations, topology-aware/geodesic brush and animated marker tracking remain open. Current algorithm reconstructs full matching source vertex stream on a user click (bounded 1M), no per-frame CPU animation pass. It rejects a non-matching published morph vertex count rather than guessing across topology LOD or seam changes. The old frozen source-vertex marker behavior was replaced by a one-frame animated snapshot, not live GPU pixel IDs.
- **Next exact step (7AK):** review and fix any publication mismatch between imported model vertex count and SparseMorphLibrary CSR count under seam-split clones and authored LOD; if performance demands, add reusable per-editor selection workspace/cached palette precomputation and a GPU ID/depth picking oracle. Then advance full topology-aware sculpt/rig features. Before claiming this stage validated, actually compile the `aiverse_vivid_human_creator_tests` and run `ctest -R '^vivid_human_creator$'` on macOS, plus native Creator Metal capture. Once all remaining functional stages are complete, run `bash tools/vivid-validate.sh --full` as requested. Do not merge main or Texture RHI without separate integration validation.


## Stage 7AK — 2026-11-20 — stable original source IDs after skinned palette seam clones and LODs

- **Branch**: `feature/vivid-digital-humans`; `main` and parallel Texture RHI/animation branches unchanged. Source-only authoring improvements. No gameplay ECS, MotionWeave implementation, Renderer3D shader, texture ABI, HumanDNA or project presentation schema changes.
- **Discovered real gap**: Stage 7AJ morph-before-skin ray picking rejected every triangle containing a seam clone because cloned model vertex IDs append after the original `sourceVertexCount`. `SkinSeamSplit::apply` correctly clones vertices and expands the GPU sparse morph CSR, but the editor still needs to cast against the actually drawn cloned triangle and return the *original* author vertex ID, never an authored/generated LOD vertex ID.
- **Core picking changes**: implementation `79b4c84834bbf7e8c14773615657e6da3a7b9a43` updates `engine/character/vivid/SkinnedWeightPick.h` with optional `authorVertexIds` mapping on `pickSkinnedWeightVertex`. Ray intersection uses the deformed global vertex positions but selection returns a mapped original author ID; generated/LOD sentinel IDs fail closed. Follow-up safety fix `e1f70ce372ed3df04762d6ad5eefc01bbb893ec1` ensures global index bounds are validated **before** dereferencing this map. Existing no-map and original skin painting API remain source compatible.
- **Candidate map**: implementation `3b75d614d28a46df75b56f20d51411e6c1eda366` extends `DeformedSculptPick.h` with transactional `buildSculptSourceVertexMap(modelVertexCount,authorSourceCount,clones,out)`: original prefix maps to itself; per-primitive seam clone targets map to original source IDs; other appended/generated LOD vertices map to `UINT32_MAX`. It rejects repeated/invalid target mappings and returns without touching old output on failure. The morphed+skinned picking overload consumes this map, preserving actual selected *clone* world position. Correction `f2b832d796ddab51e7a561a8980af9cda01f2279` explicitly supports seam-cloned **LOD input** vertices as excluded IDs (they must not block otherwise valid imported candidate publication) and keeps duplicate-target detection separate from the sentinel value.
- **Creator publication**: implementation `826511622e4994ff6d3a9adae0c7602817bfed2c` in `tools/vivid-creator/main.cpp` stages the source-ID map after `SkinSeamSplit` and authored LOD/weight transfer, validates the current full Model and `SparseMorphLibrary` vertex counts, and moves the map into active Creator state **only after** a successfully published GPU-ready character candidate. This preserves previous active mapping when a candidate fails. Sculpt clicking supplies that same published map to Stage 7AJ's morph/skinned raycast, which returns the original author ID, retaining the real clone world-space marker. Source edits still address original vertices and are later replicated through seam splitting on regeneration. An imported model above the per-click 1,000,000 vertex budget still renders but intentionally has no source map or sculpt picking. UX diagnostic commit `e968d882647225526e4c7bde622f33af3c32bc05` reports disabled picking explicitly instead of silently ignoring clicks.
- **Regression source**: `13ab9fe3be848b061f179f891b2f932f4be5ad37` extends `tests/vivid/human_creator.cpp` with a skinned triangle referencing cloned global vertex 3, a +0.4 X published morph delta replicated from source 0 to clone 3, an inverse-bind joint translated by +2 Z and a raycast that returns source ID 0 from the *clone's* actual world-space hit. The test also switches to an appended generated-LOD triangle to assert it cannot be selected, verifies malformed/duplicate source clones leave the last-good map untouched, and rejects an incorrectly sized published ID map. Additional fixture SHA `acbdbaf2002a41885121aebe05dad3ceecc039f4` checks a valid seam clone originating at a nonauthorable LOD vertex is itself still excluded.
- **Documentation**: `9c4595f99b882773937844f95cba0d1878dc7cbd` extends `tools/vivid-creator/README.md` with the new source ID contract, sample mapping behavior and limits.
- **Actual verification on this branch**: remote GitHub source fetch/update commits succeeded; **C++ compilation NOT RUN, CMake/CTest NOT RUN, Creator GUI GPU capture NOT RUN, Metal/Vulkan/GLES GPU parity NOT RUN, large-mesh performance benchmarks NOT RUN**. Source-only assertions are unverified test definitions, not passes. Previous test evidence (if any) predates these commits.
- **Compatibility and performance**: authoring does not mutate source Model geometry, so original IDs survive and can continue to use the existing morph and skin weight JSON formats. Existing picker APIs retain defaults for callers not using seam mappings. Source-ID map uses one uint32 per published model vertex and a short temporary duplicate-target mask, built once per successful candidate rather than on each click. The Stage 7AJ CPU morph/skin reconstruction still scans the full model, up to 1,000,000 vertices, and palette preparation still allocates temporary scratch; no validated asymptotic picking-time improvement claimed. The editor uses a single mesh owner requirement in `prepareSkinnedWeightSurface`, not true GPU ID-buffer visibility; unrelated occluding meshes can still differ from the CPU pick result.
- **Next action (7AL)**: profile and address on-click CPU workload with a reusable per-character `DeformedSculptWorkspace` / local mesh BVH or GPU ID+depth readback as appropriate, checking actual source-ID correspondence after morph+skin+LOD. Test cases should cover nonuniform bone transforms, multiple skin palettes, hidden/occluding body meshes and a realistic imported character with at least hundreds of thousands of vertices. After all functional stages, run actual native `aiverse_vivid_human_creator_tests` then the user's deferred `bash tools/vivid-validate.sh --full` on a clean macOS worktree, record exact tested SHA, logs, GPU captures, limitations. Keep Texture RHI integration isolated until its GPU lifecycle and ShaderGraph/Renderer3D changes are certified.


## Stage 7AL — 2026-11-20 — reusable CPU morph/skin workspace and refitted sculpt ray BVH

- **Branch**: `feature/vivid-digital-humans`; `main` and texture/animation peers unchanged. Scope is presentation-only imported Creator CPU picking: no gameplay ECS/replication, MotionWeave scheduler, versioned DNA, existing shader source, new RHI API or model vertex ABI change. User requested feature closure before one consolidated native test sweep.
- **Skinned surface scratch implementation**: `2a33086aa18ba1a0eebb87bf716ceb99e2fc7135` updates `engine/character/vivid/SkinnedWeightPick.h`. New optional `SkinnedWeightScratch*` retains a staged `SkinnedWeightSurface` and a per-vertex primitive identity vector. Each included vertex references its first primitive's joint palette rather than allocating one `std::vector<uint16_t>` palette per model vertex; subsequent shared primitive uses must have identical palettes. Validation and transformation stage the whole new world surface before swapping it into the published output; a failed pose/palette leaves the previous output intact. Existing call signature (without scratch) is preserved through default parameters.
- **Morph scratch**: `e571b63dfb530b4c44a4cdb04d6c34fde43faa54` extends `prepareSculptMorphPositions` in `DeformedSculptPick.h` with an optional distinct reusable scratch vector. On successful validation, staged output and old output swap; failed weights/CSR/non-finite source positions do not overwrite last-good output. Existing no-scratch calls are unchanged.
- **Topology acceleration**: `1b66f45dab10eca6814cfebf92150c46b2fa4b06` adds `engine/character/vivid/SculptPickBvh.h`: read-only per-selected-mesh triangle index set, deterministic median partition by *bind-pose* centroids and triangle ordinal, bounded to 1M mesh triangles. On each click `refit` computes world-space leaf bounds from the currently morphed and skinned positions, then parent unions; `pick` traverses ray/AABB intersections and only tests surviving eligible triangles. Ties at equal ray distance explicitly prefer the original source triangle ordinal to match the brute-force reference. It honors Stage 7AK seam clone → original source ID map and excludes generated/LOD vertices. `SculptPickBvhStats` records total triangle count, visited AABB nodes, narrowphase triangle tests; these are **not** actual GPU or wall-time benchmarks.
- **Owner-local orchestration**: `064b440138342576dedef38d56015dbdde1530a3` adds `engine/character/vivid/DeformedSculptWorkspace.h`, with a persistent weight buffer, pair of morph bind-position buffers, staged skinned-surface buffers, primitive identities and refittable BVH. Its `pick` evaluates the published `FaceShapeBinding`/SparseMorphLibrary and MotionWeave nodeWorld, refits the same mesh BVH and returns an original author ID (with actual deformed world position). Rebuilds topology when model identity or selected mesh/vertex count changes; the caller MUST `reset()` when the same Model object is edited in place. All old single-call `pickDeformedSculptVertex` paths remain available as a brute-force oracle.
- **Creator integration**: `4f41a92941c7ac2dc09b967eeab6a7d1fd9f460b` updates `tools/vivid-creator/main.cpp` to own `DeformedSculptWorkspace`, use `sculptPicker.pick` on a user click, show `testedTriangles/triangles` in selection status and call `reset()` only after the new imported/procedural model is successfully published. The existing source vertex mapping, one-frame animated marker, skin-weight brush, renderer and GPU-retirement machinery are unchanged.
- **Regression source**: `e0a0214de051606cab96c77dc45250ded0d9a5be` extends `tests/vivid/human_creator.cpp` with a 41×41 rigid grid, one active facial sparse morph and multiple ray clicks in neutral/animated expression poses. New BVH hits are compared numerically with the old full-triangle CPU oracle (source ID, ray distance and world vertex), with a narrow-ray candidate-count assertion and buffer-capacity retention after repeated warm clicks. It rejects a nonfinite pose and verifies recovery and topology reset. `6c6cee720212bf01fb6bc373155356a8675deed5` adds rollback checks for reusable morph and skin scratch on invalid weights and node matrices. `03cd4994d5c8adbb03f9c14e83ca9a65501e72da` covers accelerated seam-clone original-source picking and explicit BVH reset after in-place LOD index modification. Tests remain **source-only and unexecuted**.
- **Documentation**: `e3c9b1c9e49b5c7da399761b89514b84c258d064` updates `tools/vivid-creator/README.md` with the workspace API, cache invalidation rule and cost limit; `31acbb7f3a8cb75711735c94b510f2f7a32c0bab` extends `docs/vivid.md`.
- **Performance model**: BVH partition build once per model/mesh is O(triangles log triangles); each click's morph/skin position reconstruction and BVH bounds refit remain O(full model vertices + selected mesh triangles). Only ray candidate narrowphase traversal is accelerated, generally O(log triangles + hits). This should reduce allocations and triangle intersection work, but **no actual wall-time, RSS, GPU execution time or speedup was measured**. Retained buffers are per Creator/owner, never global statics or user-asset save data. Selected mesh triangles and the model vertex stream have independent 1M caps; giant imported assets still render with picking disabled.
- **Actually verified**: GitHub source fetch/create/update operations succeeded. **Native C++ build: NOT RUN; `ctest -R '^vivid_human_creator$'`: NOT RUN; other CTests: NOT RUN; Metal GUI screenshot: NOT RUN; Vulkan/GLES shaders or GPU parity: NOT RUN; CPU benchmark: NOT RUN.** Do not call the new test assertions passing, claim numerically validated parity or present invented performance figures.
- **Remaining limitations**: no GPU ID+depth buffer, no exact per-pixel inter-mesh occlusion, no cloth or unmodeled deformers, no complete topology-aware sculpt, no automatic GPU readback. BVH refit still touches every selected mesh triangle on each click; the persistent build assumes immutable topology until reset. An explicit owner/editor reset after candidate publication is wired. Device-backed compiler/runtime and texture-agent integration are separate acceptance gates.
- **Next actionable Stage 7AM**: consider a real optional GPU source-ID/depth picking pass, using canonical Renderer3D deformation/visibility and safe GPU readback semantics when the Texture RHI/renderer branch integration is stable; otherwise add a cached per-mesh skin palette plan and refit/drag interaction instrumentation, profile 10k/100k/1M vertex imported heads against the legacy linear picking oracle. Keep per-stage separate handoff commits. For final acceptance fetch exact branch SHA on user's Mac, compile `aiverse_vivid_human_creator_tests`, run targeted CTest and the deferred full `bash tools/vivid-validate.sh --full`, then record actual outcomes and GPU screenshots before any production or merge claim.


## Stage 7AM — 2026-11-20 — cached palette/influence CPU picking and reproducible 10k/100k/1m benchmark

- **Branch**: `feature/vivid-digital-humans`, main and all parallel Texture RHI/animation feature branches unchanged. Scope is imported Human Creator sculpt picking on the CPU: no changes to the authoritative ECS, MotionWeave scheduler, Renderer3D RHI/ShaderGraph source, ModelVertex layout, HumanDNA, network saves, GPU resource lifetime or texture system.
- **Problem**: Stage 7AL accelerates ray/triangle intersection using a deformed BVH and retains scratch arrays, but `prepareSkinnedWeightSurface` still rescans the selected mesh's complete triangle index stream to discover the per-vertex palette and evaluates `jointWorld * inverseBind` separately inside each nonzero vertex influence every click. These static mappings should be cached while the imported model is immutable.
- **Implementation `0496d92b75608e85c46aebc2cdb3843168eb8735`**: new header-only `engine/character/vivid/SculptSkinningPlan.h`. `build(model,meshId)` validates a single mesh owner, valid skin joint count/IDs/inverse binds, triangle index ranges, strict equivalence of any shared vertex's primitive palette, up to four U8 weights and their normalized sum; it stores each referenced vertex's resolved skin-joint index and normalized influence weight. For subsequent `evaluate(model,nodeWorld,bindPositions,out,staging)`, it computes the combined skinning matrices only once per skin joint, applies them to the per-click morph-deformed bind positions and swaps a completely validated staged `SkinnedWeightSurface` into output. Rigid meshes use the owning node transform. The new plan is local to the editor owner, holds no GPU handles and does not touch the original model. Invalid input returns false without publishing a partial surface or replacing the last valid influence plan.
- **Workspace integration `b24f6693248dca3c68deca2ccb936279fbe1ab06`**: `engine/character/vivid/DeformedSculptWorkspace.h` now keeps a `SculptSkinningPlan` and builds it on the same first click after a published candidate/selected mesh change as `SculptPickBvh`, reusing the Stage 7AL retained world-position scratch for transactional evaluation. `reset()` invalidates both BVH and plan; the existing Creator publication hook already invokes reset after a new model/skin candidate is accepted, not before. Any host editing model topology, skin weights, inverse binds or palettes in-place must explicitly reset before the next pick.
- **Regression sources**: `2a74b2ed72217d7984ca8d44cf1c9e55e37efc0f` extends `tests/vivid/human_creator.cpp` with a skinned triangle driven by two unequal raw U8 weights (128 and 64), two palette joints, multiple positive/negative joint translations, numerical comparison to the original `prepareSkinnedWeightSurface` CPU reference, nonfinite-pose rejection and retained last-good world positions. `85462e02a50d1b92aaa025f323e57416bd2613bd` adds failed new-plan-build rollback and explicit-reset checks. All fixture assertions are **NOT RUN** until a real native compiler/CTest.
- **Opt-in measured benchmark**: `9f3b3ec21974c8852fa15be38220873e41a16813` adds `tools/vivid-creator/sculpt_pick_bench.cpp`; `2022139372e6798eb9ce169065c4ad69e3c4f8d0` registers `aiverse_vivid_sculpt_pick_bench` behind CMake `-DAIVERSE_VIVID_SCULPT_BENCH=ON` (OFF by default, never auto-run as CTest). The CLI prints JSON containing actual cold BVH build+pick wall time, median warm click wall time, one brute CPU reference time, total/visited/tested triangles, source-ID/ray-distance parity and a nonzero exit when valid hit parity fails. Three synthetic rigid checkerboard model sizes: side 100 (10,000 vertices), 317 (100,489) and 1000 (1,000,000 vertices), with selected mesh <=1M triangles. Fix `3c7abe2b0ff2b72352e6db3d7b97966ff00b3ff6` ensures the timed ray hits a known existing quad and does not report parity PASS from two matching misses. This does **not** benchmark real imported skinned heads, GPU, iOS, Vulkan or GLES.
- **On-click diagnostics `357cd6e3aba849faf6f2989d27242cda638bc49b`**: `tools/vivid-creator/main.cpp` measures CPU sculpt picker wall time via `std::chrono::steady_clock` only when the user actually clicks, and displays that elapsed time and candidate/total BVH triangle counts in its status. It provides no synthetic speedup claim.
- **Documentation**: `e1c4406832073fab42205317a0bfd8c7e5e3bdf0` describes opt-in CMake/benchmark invocation and quality invariants in `tools/vivid-creator/README.md`. `d17fd726be0bfcecd626d597b6ecf767ebc0f762` extends `docs/vivid.md` with Stage 7AM.
- **Actual verification**: GitHub source read/update/create operations **SUCCESS**. **NOT RUN**: native C++ compilation or CMake generation, `ctest -R '^vivid_human_creator$'`, full `bash tools/vivid-validate.sh --full`, benchmark executable, native Mac GUI Creator, Metal/GL/GLES/Vulkan GPU operations, source-ID pixel oracle, wall-time/RSS baseline. Source-file existence is not test evidence. All performance figures remain unknown until an actual benchmark executes on a known host/branch SHA.
- **Exact benchmark command after the user authorizes the consolidated native sweep**, from the real Mac worktree at this SHA or newer:
  ```bash
  cmake -S . -B build/vivid-onepass -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_VIVID_SCULPT_BENCH=ON
  cmake --build build/vivid-onepass --target aiverse_vivid_human_creator_tests aiverse_vivid_sculpt_pick_bench --parallel 4
  ctest --test-dir build/vivid-onepass --output-on-failure --no-tests=error -R '^vivid_human_creator$'
  build/vivid-onepass/aiverse_vivid_sculpt_pick_bench 3 > build/vivid-onepass/sculpt-pick-benchmark.json
  ```
  `--full` remains the final full-engine suite; do not conflate the optional measured benchmark with a registered CTest or accidentally present the JSON schema as real timing results.
- **Known costs/limits**: per-click CPU morph gather and skin vertex traversal remain O(full model vertices), BVH refit remains O(selected mesh triangles), but palette topology scanning is now amortized at first click and per-influence matrix construction is eliminated in favor of per-joint transforms. Cold-cache build uses temporary per-vertex metadata and may be expensive, so warm and cold results must both be captured. Reusing a plan after changing Model topology/skin data in place without an explicit reset is unsafe. Geometry/LOD/source ID budgets from Stage 7AK/7AL remain 1M model vertices and 1M triangles. No GPU ID/depth picker, GPU pick readback or screen-space occlusion handling is implemented.
- **Next exact step Stage 7AN**: actual optional CPU benchmark and targeted native compile, then inspect cold/warm cost distributions on 10k/100k/1M before deciding between further CPU acceleration and a canonical Renderer3D GPU ID/depth selection pass. The user requested completing features before consolidated tests; do not claim this code tested or benchmarked and do not merge `main` or Texture RHI until cross-branch CMake/Renderer3D/ModelShaders ABI and RHI lifetime checks have actual evidence. Continue one separate handoff commit per future stage.


## Stage 7AN — 2026-11-20 — GPU triangle-ID/depth preparation, guarded readback tokens, paired GLSL/MSL

- **Branch**: `feature/vivid-digital-humans`; `main`, Texture RHI and parallel animation/renderer branches untouched. This bounded feature stage extends source-level GPU selection preparation, not the canonical gfx::Device interface. All CPU BVH, morph/skin, source-seam-map, and Creator presentation paths remain unchanged; the Creator still uses CPU picking.
- **Architecture discovery**: The VIVID branch's `engine/gfx/Gfx.h` supports offscreen `gfx::PassDesc` with color/depth targets, `gfx::Device::captureNextFrame` on a *presented screen*, asynchronous GPU `readBufferAsync` and real `Renderer3D::drawModel` with per-instance morph vertex override. It does **not** contain a cross-backend async **offscreen texture pixel** readback suitable for an ID/depth image. Installing a new Renderer3D/gfx API while the separate `feature/texture-rhi-unification` is unsettled would risk duplicated ownership and shader/resource ABI conflicts. No working offscreen ID renderer or GPU callback has been claimed.
- **GPU ID and async token protocol**: implementation `3405b7f4fbb36dcf75a359add18be97d15d48dc6` adds `engine/character/vivid/SculptGpuPickContract.h`. `packSculptTriangleId`/`unpackSculptTriangleId` encode mesh-local ordinal+1 into four little-endian linear RGBA8 channels (0 = clear/unselectable; 1M-triangle hard cap). `SculptGpuPickProtocol` tracks exactly one outstanding readback `SculptGpuPickRequest`, including generation, immutable model publication serial, submitted frame serial, mesh ID, viewport dimensions and top-left pixel. New clicks and `cancel()` invalidate old callbacks; `consume` verifies full token, actor publication, frame completion ordering, unchanged viewport and optional [0,1] finite depth. Invalid/stale replies fail closed without overriding a new request. A valid miss completes a pending click without creating a sculpt target.
- **CPU reconciliation**: same header exposes `resolveSculptGpuTriangle(model, SkinnedWeightSurface, triangleOrdinal, ray, sourceVertexCount, authorMap)`. After **external** GPU depth selection, it finds only the selected mesh-local triangle in the original draw ordering, intersects the matching morphed/skinned CPU pose, returns an original authorable vertex ID with the true world vertex position, and rejects generated/LOD IDs, missing triangles, ray mismatch, inconsistent indices and invalid pose positions. Safety fix `ef4537b9dc1b437d445feb2b42ce053629a928fc` validates source vertex count and all selected triangle vertex components before a hit is allowed.
- **Paired shader + source stream**: implementation `a8ee4a3df3ca4da0e20181e9f6978371d5b8baac` adds `engine/character/vivid/SculptGpuPickShaders.h`. `SculptGpuIdVertex` packs `float3 world + UNORM UByte4 RGBA triangle ID` into an exact 16-byte vertex; a bounded transactional `buildSculptGpuIdVertices` expands every selected mesh triangle into three vertices with an **identical** ID to avoid flat-provoking-vertex ambiguity. Nonauthorable triangles have zero ID but still must write depth, so an invisible LOD cannot become an editable fake face. Paired GLSL ES300 `kSculptGpuIdVS/kSculptGpuIdFS` and Metal `kSculptGpuIdMSL` output that ID unchanged with clip position from the host's `viewProj`; actual projection depth range is backend-specific and must be supplied by the host. `27bd925c197ae7275d526f7e292b547e93b0ec8e` adds canonical `gfx::ShaderDesc`/`gfx::PipelineDesc` descriptor helpers with attribute 0 Float3@0, attribute 1 UByte4N@12, 16-byte stride, RGBA8 linear target, blend off, depth test/write on. No ShaderDesc/RT/pipeline is allocated and **no draw call** is issued by this stage.
- **Frame-safe workspace bridge**: implementation `2485612a1c0fbfd3264ae3683e7e20aa9d5dfbd4` adds `DeformedSculptWorkspace::resolveGpuTriangle(...)`. Additional implementation `2aefcdda76b11110f44a0b4b07a93c0c70c937d6` requires matching nonzero **captured frame serial** and **model publication serial** when preparing the CPU surface and later resolving GPU ID readback; a normal unstamped CPU click cannot be silently interpreted as a matching GPU frame. `reset()` clears both stamps on any successfully published new character, preserving the Stage 7AK original source ID contract. Future async host must retain a matching immutable frame snapshot or reject responses after a subsequent successful pose preparation.
- **Regression-source changes**: `67efee1ae55495fa48532d95ee31570f4baee130` extends `tests/vivid/human_creator.cpp` with ID packing bounds, clear/overflow, two-click generation replacement, wrong actor revision/frame/viewport rejection, NaN depth validation, valid token consumption, cancel-on-republish, off-viewport request rejection, targeted original-ID seam-clone triangle resolution and invalid/LOD/missing ray rejection. `9005fc37b02ee867211a34793576f95573baabcb` checks 16-byte expanded ID stream, stable per-triangle colors, zero-ID LOD depth occluder and GLSL/MSL flat-ID source contract; `c025c5b1b719461d07389fee48f6930029b1ce3a` checks the canonical RHI descriptor attributes and linear depth-tested pipeline. `22fe2f703ebd5e5f310866634b9d072210ef2b5b` exercises workspace GPU triangle reconciliation against CPU BVH source/position, and `20209901ba7953af249b1bbc74fb6f25238647eb` adds exact frame/model publication mismatches and reset rejection.
- **Documentation**: `5fe2dce98cf05c89208c7436a0415e2d65fa06ee` updates `tools/vivid-creator/README.md` with ABI, timeline/viewport/ownership rules, missing offscreen pixel readback and exact remaining tasks; `f42a6b2b2d128bf85a8f59ab3b34be33f629c3f2` adds the Stage 7AN overview to `docs/vivid.md`.
- **Actual tests**: GitHub source files inspected and writes committed. **Native C++ compilation: NOT RUN. `vivid_human_creator` CTest: NOT RUN. Metal/GLSL shader compilation: NOT RUN. GPU ID/depth offscreen draw: NOT RUN. Async pixel readback: NOT IMPLEMENTED/NOT RUN. Vulkan/GLES shader/runtime/depth parity: NOT RUN. Creator GUI GPU picking: NOT IMPLEMENTED/NOT RUN. Full `bash tools/vivid-validate.sh --full`: NOT RUN.** Source assertions being present do not certify compilation or success.
- **Known limits & compliance**: stage is a complete *host-side contract*, not an operational GPU ID picking feature. Triangle ID sampling must use an RGBA8 **linear**, no-MSAA/no-blend/no-dither/no-postprocess target, hardware depth test/write and matching visibility/cull/alpha. The ID pass must draw unrelated scene occluders into the same depth target with **zero** selectable ID. Skinned and facial morphed GPU vertex override must share the real draw-time pose/LOD and be retained until readback completion. The expanded temporary 3-vertex-per-triangle CPU/world-space stream may be costly for production 1M-triangle scenes; no perf measurements. Frame-scoped CPU workspace must be derived from the **exact** GPU-captured pose. A screenshot or present-frame capture must not be mistaken for arbitrary offscreen pixel readback.
- **Next exact step (7AO)**: once safe common Renderer3D/Texture RHI ownership allows it, implement an opt-in offscreen linear RGBA8 + depth ID pass and asynchronous single-pixel readback on the shared gfx::Device (or a read-buffer staging path with correct fences and backend support). Use the *real* Renderer3D morph/skin vertex stream or a documented CPU-fed fallback, draw all relevant occluders, validate shader pipeline on native Metal/GLSL ES3/Vulkan. In Creator, begin one token per actual click, correlate returned pixel with a published actor/frame/viewport and matching workspace pose, fall back to existing CPU BVH where unsupported. Add GPU reference and stale-response oracles and real performance/timeline logs. Only then label GPU ID/depth picking operational. Continue one implementation + separate handoff commit per stage; do not merge `main` or Texture RHI without semantically resolved conflicts and the user's deferred macOS one-pass acceptance.


## Stage 7AO — 2026-11-20 — opt-in Metal offscreen ID/depth draw and actual asynchronous one-pixel RHI API

- **Branch:** `feature/vivid-digital-humans`; `main`, Texture RHI and other agents' worktrees untouched. Scope: experimental on-click selected-mesh GPU ID/depth confirmation in imported VIVID Creator. The default active CPU morph+skin+BVH picker from Stages 7AJ–7AM remains unchanged, including non-Metal platforms and no-GPU mode. No MotionWeave/ECS gameplay scheduling, model/vertex ABI, versioned DNA, networking or resource-streaming policy changes.
- **RHI public API source `e3047e6105ef54f6fc1ecdd680cea7ee54629b87`:** `engine/gfx/Gfx.h` adds virtual `Device::readTexturePixelAsync(TextureHandle,int topLeftX,int topLeftY,PixelReadFn)`, a 4-byte canonical linear RGBA8 callback on a later device-owner frame or empty data on GPU failure. Base/default is **unsupported false**. It must be issued after `endPass()` and before `endFrame()`; unlike `captureNextFrame`, it is readback from an explicit offscreen texture. Vulkan, GLES3 and GL33 do not override the method at this stage and therefore safely retain CPU fallback.
- **Real Metal backend `d734aa9f0fb7dca18384dc46b6b1c672de8f78ad`, safety follow-up `3fc862fd55546515993808319691c76342090ddd`:** `engine/gfx/metal/GfxMetal.mm` overrides `readTexturePixelAsync`. Under an active owner-frame command buffer, with no render/compute encoder, it validates a 1-sample linear RGBA8 render-target texture and top-left pixel bounds, limits the concurrent callback queue to 32, blits just one pixel into a 256-byte-pitch shared MTLBuffer, waits **asynchronously** for the *recorded command buffer* completion, and delivers exactly 4 RGBA bytes on a later owner `beginFrame` through the pre-existing BufferRead completion queue. A Metal BGRA8Unorm render-target byte sequence is explicitly swizzled B/G/R/A → canonical R/G/B/A; a native RGBA8Unorm source retains channel order. Error status produces an empty buffer. No per-click synchronous `getBytes`, `waitUntilCompleted`, full-frame capture or raw GPU driver access exists in the VIVID module.
- **Actual GPU ID owner `9d4787d57e19604f6cbd5ab444e9ee9d23f8d5a5`:** new header-only `engine/character/vivid/SculptGpuIdPass.h`. Uses the Stage 7AN paired GLSL ES300/MSL shader and standard gfx::ShaderDesc/PipelineDesc, creates a dedicated linear RGBA8 colour RT + Depth RT, allocates a triangle-expanded `SculptGpuIdVertex` buffer (three world-space vertices with identical per-triangle IDs), records a depth-tested offscreen draw with the **exact click pixel 1×1 scissor** and the Creator's actual same-frame `Renderer3D::viewProj`, then enqueues a native Metal one-pixel async readback. It supports only Metal for now; an explicit backend guard returns false elsewhere. The lifecycle retains vertex buffers until GPU callback completion and destroys them only when that completion is reported on the owner thread. A failed GPU read after a draw retains the in-flight vertex allocation until a verified owner-thread `waitForResourceIdle` on shutdown (bounded max eight such failures); colour/depth texture resize also requires an idle barrier before destroying old RTs. Follow-up `728ef4d11205f24c2fc57c3ca048d38f3d1ef3b4` caps failed readback retention. Follow-up `539f4ebf3d7c18bd6209ae737c6ac997acb0a1fe` uses no culling, aligning this experimental selected mesh pass with the existing CPU BVH two-sided triangle-ray semantics.
- **Creator integration `c54292745d1637283ec08a2b19fd7bc8e5389de8`, `e3650300536470484b06f844ca4d13ad89e54b23`:** `DeformedSculptWorkspace` now exposes its already validated per-click morphed/skinned world surface read-only. `tools/vivid-creator/main.cpp` owns one optional `SculptGpuIdPass` and the Stage 7AN `SculptGpuPickProtocol`. An **Experimental Metal GPU ID check** checkbox in imported sculpt authoring is OFF by default and does not affect normal CPU BVH picking, screenshots or the one-pass smoke workflow. On a user click, CPU BVH is still used immediately (with frame/publication stamps when GPU eligible). A valid offscreen request is drawn after `Renderer3D::beginScene`, before the main Renderer3D pass. On a later owner frame, a completed hardware pixel is decoded, validated against the same click, model publication, frame serial and viewport; if its selected triangle can be resolved against the exact stamped CPU world pose and author-source map, Creator reports/uses the GPU-confirmed vertex. Missing/invalid/mismatched/unsupported readback retains the CPU BVH result. The new pass never captures a raw CreatorApp pointer in a GPU callback.
- **Stale controls and lifetime fixes:** `3f33d9be66b00e446c1b134b055eae4530ea7620` cancels in-flight GPU ID tokens on every new CPU click, including unsupported/fallback clicks; `bf1eb01596c943bd6ce8912b6272b191ab2688b2` cancels tokens and resets cached pose on chosen sculpt mesh change; Creator publication increments a separate, nonzero GPU model publication serial, cancels stale tokens and resets the old BVH/skin CPU surfaces, then publishes only a fully constructed character. `SculptGpuIdPass::shutdownAfterIdle` releases any pending GPU buffers/RTs/programs only after successful GPU idle proof. No extra GPU passes are recorded with the experimental checkbox off.
- **GPU test source and CMake:** `dcb592589f2199730a72871396358da05a402a05` adds `tests/vivid/metal_sculpt_id.mm`, which uses the real production `gfx::createMetalDevice` RHI, creates two overlapping CCW triangles in identity projection (far ordinal 0, near ordinal 1), records an actual offscreen GPU ID/depth render, polls a real asynchronous owner-frame pixel completion, expects canonical RGBA `(2,0,0,0)` at centre due to depth test, and expects RGBA `(0,0,0,0)` at a background pixel, then shuts down after verified GPU idle. `f9c26a3d3198ef6926d2f0f350e3facb6e9dd439` adds native `aiverse_vivid_metal_sculpt_id_tests` / CTest `vivid_metal_sculpt_id` only on macOS and `6ef4267e0751a3ff2810844a59ed17af5907e840` adds that target to the final focused `tools/vivid-validate.sh` build; full-engine build/CTest picks it up automatically.
- **Documentation:** `9b6ef437ff767a622e1c111488f698a4580dba41` updates `tools/vivid-creator/README.md` with the live opt-in path, RHI method, known limited occlusion, async lifetimes and portability limits; `7a1a7f168ddb74d094004541ab0c25f59b797a61` updates `docs/vivid.md`.
- **Actual verification performed in this session:** GitHub connected source inspection and GitHub source commits succeeded. **NATIVE C++/OBJ-C++ BUILD NOT RUN; CMAKE/CTEST NOT RUN; Metal GPU shader, hardware ID/depth raster and async readback NOT RUN; Vulkan/GLES/GL runtime NOT RUN; real Creator GUI user interaction and screenshots NOT RUN; GPU frame time/memory benchmarks NOT RUN.** The new test is a **registered test case**, not a passing test. Metal functionality is an implemented source path whose actual hardware correctness remains unverified. No invented performance improvement or GPU result.
- **Crucial limitations still outstanding (do NOT declare full GPU picking):** (1) The optional ID pass renders only the selected mesh, not every other visible mesh, floor, groom, alpha-masked strand card or translucent cornea as proper depth-only/zero-ID occluders; it therefore does NOT match full Renderer3D screen visibility and may confirm a hidden triangle. (2) It expands the *CPU-reference morph+skin positions* to a separate GPU triangle stream, rather than using the real main renderer's GPU actor vertex-override path/LOD culling; hardware pixel selection can differ under actual GPU deformation, alpha or advanced shaders. (3) Only Metal currently implements async offscreen pixel readback; Vulkan/GLES3/GL33 fail closed. (4) The full-resolution RGBA8+depth target and three-vertices-per-triangle staging are expensive, capped at 4096×4096 framebuffer, up to 1M model-source/mesh triangles and 8 retained failed callbacks. (5) Shader code is uncompiled on Metal/GLSL/Vulkan at this SHA. The explicit test for two overlapping CCW faces does not prove real-scene visibility or multisurface parity.
- **Next exact stage 7AP:** add complete-scene depth/occluder submission with alpha policy matching Renderer3D, and if appropriate switch ID pass to the actual GPU deformed/skin vertex stream rather than CPU triangle expansion. Integrate shared Texture RHI ownership before building new readback paths for Vulkan and GLES/WebGL2; preserve current unsupported+CPU fallback until validated. Build the new `aiverse_vivid_metal_sculpt_id_tests` executable on the target Mac and run `ctest --test-dir build/vivid-onepass --output-on-failure -R '^vivid_metal_sculpt_id$'` only in the user's consolidated native acceptance phase, review actual pixels/compilation and fix observed failures before merging. Full `bash tools/vivid-validate.sh --full` remains the final acceptance, with real exact SHA and logs. Continue one separate handoff commit per implementation stage.


## Stage 7AP — 2026-11-20 — conservative scene-wide Metal ID/depth occluders for VIVID Creator

- **Branch / scope:** `feature/vivid-digital-humans` only. `main`, `feature/texture-rhi-unification`, MotionWeave and all parallel agents untouched. This extends Stage 7AO's opt-in Metal ID/depth check from selected-mesh-only hardware depth to the geometries drawn by the current VIVID Creator scene. Genre-independent VIVID core remains independent of RTS/FPS and gameplay ECS. No additional RHI entry points were added in this stage; it uses the existing `gfx::Device::readTexturePixelAsync` added at Stage 7AO. CPU BVH stays the immediate/default picker and fallback.
- **Problem / true limitation:** Stage 7AO drew only the selectable mesh, meaning a head/body mesh, hair card or other visible geometry could overlap the selected triangle while the ID pass incorrectly returned a selectable RGBA ID. A complete renderer depth parity solution also needs alpha discard, transparent depth ordering, actual GPU skin/morph vertex-override and LOD. Those deeper shared Renderer3D/Texture RHI changes are **still not implemented** in this stage.
- **Material eligibility:** commit `7a6dfd8b135bc92e6222f0f90bb407bc0bedee91` updates `engine/character/vivid/SculptGpuPickShaders.h`. `buildSculptGpuIdVertices` now encodes an editable triangle ID only if the selected primitive's `Material` is fully opaque (`alpha==Opaque`, `transparent==false`, `baseColor.w>=0.999`). Selected masked hair, blended cornea and translucent materials still contribute conservative whole-polygon depth but carry ID 0, so they cannot cause false positive GPU selection. Standalone unit fixture `Model` with no material table continues to use default opaque behavior. Invalid primitive material IDs reject the GPU candidate.
- **Occluder generator:** commit `ca4a6b437e641d354128fa233b321b419ee93d7d` adds `engine/character/vivid/SculptGpuSceneOccluders.h`, with `appendSculptGpuMeshOccluder` for a `Model` and an already reconstructed `SkinnedWeightSurface`, plus `appendSculptGpuWorldOccluder` overloads accepting indexed `float3` or indexed `ModelVertex` positions and the actual submitted world transform. All generated triangles have RGBA ID 0, so they only write depth; their indices, world positions, included masks, per-call total triangle budget and finite transforms are validated. The caller owns a throwaway scene vector and discards it on any failure; a failed partial append is never passed to the GPU. Transparent/masked occluders are conservatively treated as solid geometry (false-negative GPU confirmation through texture holes), rather than silently skipped.
- **Shared CPU pose:** commit `92f424c897ca94ebd89a0d237252498232bb2dab` adds read-only `DeformedSculptWorkspace::morphedBindPositions()` to share the exact published facial morph deformation used by the selected mesh. Other imported meshes use the existing `prepareSkinnedWeightSurface` with their own skin owner and `facePose` rather than sharing an incorrect single mesh skin palette.
- **One hardware depth pass:** commit `49e27dee1f85ffd23498d9f52deb69a707143f09` extends `SculptGpuIdPass::record(...,request,optional std::span<const SculptGpuIdVertex> occluders)`. It validates every extra vertex is finite and **all four ID bytes are zero**, enforces the same combined 1M-triangle/3M-vertex cap, appends occluders to the selectable triangle stream and sends one depth-tested offscreen draw. A missing/invalid occluder never produces a selectable nonzero ID. The existing one-pixel Metal async callback and frame/publication token contract remain unchanged.
- **Actual Creator scene wiring:** commit `dcf6d7436ad6745cf13e41a2edb391b5e8cfc611` updates `tools/vivid-creator/main.cpp`. The `MeshBuilder` floor vertices and triangle indices are retained during the initial floor upload. Only for an opted-in on-click GPU validation, Creator enumerates the other node-attached meshes of the currently displayed imported head/body model (not unattached authored LOD alternatives), reconstructs each mesh with its own skin owner using current `facePose` + the published morph bind positions, and appends their zero-ID depth triangles. It also appends the static floor at identity world transform and the current per-frame CPU-simulated procedural groom cards with the same `rigidHeadMatrix` used by Renderer3D. Any invalid mesh owner, palette, finite value, index or budget fails the optional GPU pass and retains the CPU BVH selection. The offscreen ID draw still runs before the regular Renderer3D colour pass, outside other render encoders. Normal CPU-only authoring incurs none of the new occluder work.
- **GPU callback UX:** commit `77e210431ed6715e4e5c14db0a6cccbb976bb800` fixes the previously unhandled successful zero-ID readback. A completed but zero-ID frame (background, unselectable alpha material or separate occluder) clears the pending token and reports a conservative GPU blocker/no-ID rather than leaving `GPU ID pending` indefinitely. No stale, canceled or different-model GPU callback is permitted to overwrite a newer CPU pick status.
- **Test source:** `2b8a18d7b4dd911e30fed5c6540bde46a6f5867d` extends `tests/vivid/human_creator.cpp` with separate rigid mesh zero-ID occlusion, selected material Mask/Blend/transparent suppression, floor and groom world-space zero-ID coverage and malformed index rejection. `d520bcd8db33fb7b7faad5dab5cb9db19fa60870` extends the existing actual native Metal `tests/vivid/metal_sculpt_id.mm` fixture to send independent zero-ID scene triangles on top of and behind the previously selectable face. The test now expects a *closer* blocker to produce RGBA `(0,0,0,0)` and a *farther* blocker to preserve the nearer face's `(2,0,0,0)` ID. It uses the real gfx Metal backend and async pixel-readback API when eventually executed. CMake registration `vivid_metal_sculpt_id` and the final `tools/vivid-validate.sh` inclusion were already introduced in 7AO; no CMake changes needed for the new header-only helpers.
- **Docs:** `e93099a5e539d5bf41e335eb3104e00458caea2c` updates `tools/vivid-creator/README.md` with the scene geometry coverage, conservative alpha/transparent semantics, CPU fallback and remaining renderer parity limits. `666e1830c7d524042d31a99e61bccbf5ca856cca` appends Stage 7AP to `docs/vivid.md`.
- **Actually performed:** GitHub source inspection, file creates and commits. **NOT RUN:** CMake configure/build, native C++/Objective-C++ compile, any Python or C++ test, CTest, native Metal ID/depth raster, asynchronous hardware pixel, Vulkan/GLES shader/runtime or performance test. All test assertions are source-only and unverified until the user-requested consolidated Mac sweep. Do not claim measured pixel parity or that the new source is certified buildable.
- **Explicit outstanding limitations:** (1) GPU ID stage is still only experimental and Metal-specific; Vulkan/GLES/GL33 implement no offscreen async single-pixel readback and continue CPU fallback. (2) Alpha-masked hair card/cornea polygons are treated as solid zero-ID depth blockers, not true texture alpha holes; GPU confirmation may be suppressed incorrectly, whereas CPU selection remains. (3) Imported mesh and groom positions come from CPU morph+skin/groom reference, not the actual Renderer3D GPU actor vertex override. (4) Renderer3D may choose different authored/generated LOD, culling or custom shader deformation; current helper enumerates node-attached meshes and does not fully mirror all colour-pass variants. (5) Arbitrary geometry not present in this standalone Creator scene must be added by its host; the scene helper is not a general full-engine occlusion/render graph replacement. (6) Large 1M-triangle expanded streams and full-size 4096x4096 colour/depth RT are expensive; timing and VRAM not measured.
- **Next actionable Stage 7AQ:** implement exact material alpha-mask/depth-after semantics or an opt-in capability probe that validates colour-pass parity; investigate reuse of Renderer3D's real GPU vertex override and LOD-culling stream without duplicating RHI ownership. Coordinate the texture agent's stable sampler/mip/runtime contracts, then add true async offscreen one-pixel readback for Vulkan and GLES/WebGL2 with correct readback ordering and CPU fallback where unsupported. Run `aiverse_vivid_human_creator_tests` and `aiverse_vivid_metal_sculpt_id_tests` on the exact Mac checkout in the deferred full acceptance sweep and record real compile errors, pixel values, RSS, GPU frame timings. Keep implementation and a separate handoff commit per new stage; do not merge to `main` or Texture RHI without semantic integration and tested code.


## Stage 7AQ — 2026-11-20 — sampled GPU alpha masking, explicit LOD incompatibility, native GLES3 async readback

- **Branch:** `feature/vivid-digital-humans`; `main`, `feature/texture-rhi-unification` and other agents' branches untouched. Detailed, standalone agent handoff: `docs/agent-handoffs/vivid-7AQ-alpha-mask-gles-2026-11-20.md` (source commit `aa4c190496bd82a6a58ef6059219890888b4269f`). It includes source API, design constraints, code changes/SHAs, testing evidence classification, integration ownership and exact next action.
- **Shader/geometry implementation:** `SculptGpuAlphaMask.h` adds paired Metal/GLSL ES300 hardware alpha-tested ID/depth shaders, typed 32-byte vertex / 48-byte uniform contract, standard texture+UV/cutoff material eligibility and per-primitive masked batches. Relevant commits `b80554591112752067f9d1fc6056641477c953f2`, `bbc51ff1b51782e827d20550df74e707e7630a37`. Unknown ShaderGraph/team/unsupported masked materials remain conservative zero-ID full-depth blockers.
- **Pass integration:** `SculptGpuIdPass.h` now creates both opaque and mask shader pipelines, stages selected and other-mesh masks in the same linear RGBA8+depth pass, binds original GPU source texture, discards transparent fragments, and retains ALL mask and opaque vertex buffers until the matching async callback or verified idle. Commits `2860ec88150d51d049ec51712b0cc8a2e358dbd9`, `3b60d8485e684dbeff2759eaa3f922c4d77f022a`, `9386be9c0a8ae07d6104da1f43a3b7b0a273b0c3`, `5f1a1fd8a552920452a561e566872b6d2a827dec`. `tools/vivid-creator/main.cpp` forwards other imported masked mesh batches in `d490c3efbf9a7784c634b7260c5e4b35af024d50`. Floor/procedural hair without bound alpha textures remain conservative blockers.
- **LOD safety:** `SculptGpuPickEligibility.h` commit `68143569b45e6f7b74cc17cc8b2b0325e92876d9` validates selected mesh ownership and rejects any attached authored LOD chain rather than falsely confirming CPU base mesh as Renderer3D's selected render LOD; Creator gate commit `cd03e6d80a53cddcf6af98ac55592b13f8ad5abe`. CPU BVH still works. Exact visible render LOD / GPU deformed vertex stream parity NOT implemented.
- **Native GLES3 pixel readback (not yet operational GLES ID picking):** `engine/gfx/gl/GfxGL.cpp` commits `f08eafcdf393251363de1d86122bb2a31a92d932`, `f79848fd4abf75bf91416dc52d0bd4d5184792e0`, `1bb90e9aca7ef232741d37db12812c3786919b26` implement 4-byte PBO + fence + nonblocking later beginFrame callback, top-left coordinate conversion, framebuffer state restoration and in-pass rejection on native GLES3/GL3. WebGL2/Emscripten and custom Windows GL loader return false; Vulkan still unsupported. GLES3 Device::frameSerial() deliberately remains zero under its current completion contract, so `SculptGpuIdPass` is still Metal-only and CPU BVH remains fallback.
- **Regression fixtures added (not executed):** `tests/vivid/human_creator.cpp` covers standard material alpha batching, live GPU image binding, cutoff/UV transforms, source/ShaderGraph fail-closed policy and LOD eligibility `3c085f75a505aa08242c90283fef7b059687c674`, `95b1aafeed59b12d9c981f20ca4a3ea118b57dcb`. The native `tests/vivid/metal_sculpt_id.mm` fixture adds a real transparent vs opaque texture texel case `8cbb2f12737d2514f80bac3c84d6aaf478438fa4`. Same existing CMake `vivid_metal_sculpt_id` registration from 7AO.
- **Docs commits:** `tools/vivid-creator/README.md` `19b3363dc2d405b0564a4dfc06aad623c4096522` and `docs/vivid.md` `244d725f3547cc29571cd024572fab5a433a2c09`. Review detailed document before branching or adjusting RHI ownership.
- **Actual verification:** Remote GitHub source fetch/create/update succeeded. **C++ / ObjC++ build NOT RUN; CMake/CTest NOT RUN; real Metal mask shader and readback NOT RUN; native GLES3 PBO/fence test NOT RUN; Vulkan/WebGL2/Windows GL test NOT RUN; 1M-triangle performance benchmarks NOT RUN.** The changes are source implementation only, not completed GPU validation. Never say tests passed merely because fixtures were added.
- **Remaining / Stage 7AR:** renderer-owned actual GPU morph/skinning vertex-override stream, exact visible LOD and culling list, ShaderGraph custom alpha/Blend cornea and alpha cards need true parity. GLES3 GPU ID needs an explicit safe frame serial or different request timeline; Vulkan needs async offscreen pixel readback; WebGL2 a tested browser-safe buffer delivery path. Texture RHI integration must be semantic, on a separate branch, retaining both agents' shader/texture lifetime and sampler contracts. Finally compile and run `vivid_human_creator`, `vivid_metal_sculpt_id` and deferred `bash tools/vivid-validate.sh --full` on actual Mac hardware, capture test logs/pixels/SHA before merging to main.


## Stage 7AR — 2026-11-20 — source-implemented Vulkan/GLES3 ID/depth and fenced pixel-readback path

- **Branch:** `feature/vivid-digital-humans`. No merge to `main`, no change to Texture RHI or other agents' branches. A full separate stage handoff with exact APIs, file and commit IDs, build commands and limitations is in `docs/agent-handoffs/vivid-7AR-native-rhi-readback-2026-11-20.md` (commit `aca3c607d036bf38c606ac4522ca6295a63fb793`).
- **Vulkan RHI changes:** `engine/gfx/vulkan/VkDevice.h` `b094f3d8aad4a4ec6401f1e12c11ebaeba4acb8e`; `engine/gfx/vulkan/GfxVulkan.cpp` `9e10630ce817b8cf907ceee77db810078a9baf20`; fence correction `2e88b8fb7b792483e4d6bb566ae13a6687c53826`. `gfx::Device::readTexturePixelAsync` now has a Vulkan offscreen RGBA8 source path: one-pixel `vkCmdCopyImageToBuffer` into 4-byte host-coherent VkBuffer, correct transfer image layouts/barriers, top-left→bottom-left storage coordinate conversion, and later callback on the gfx owner thread only after the **actual successful frame-slot fence** advances `completed_`. Pending staging is released after completion or device-idle teardown. `frameSerial()` is a nonzero active recording identity, never a proxy for GPU completion; device loss produces empty/invalid results, never invented RGBA.
- **Native GL frame contract:** `engine/gfx/gl/GfxGL.cpp` commit `0b18c3a9ed6c99a4417274137e85fb533fa20e44` adds monotonic active-frame `frameSerial()` for native GL/GLES, pairing with Stage 7AQ's existing nonblocking one-pixel PBO+`glFenceSync` readback. Emscripten/WebGL2 and Windows GL custom loader still fail closed; their frame serial/readback remain unsupported and CPU picking is used.
- **Shared ID pass integration:** `engine/character/vivid/SculptGpuIdPass.h` commit `acc3832f77093a477d77469767cf9068ec4e204b` accepts Metal, Vulkan and native GLES3/GL33 as opt-in hardware backends and stops further GPU submissions after a post-draw readback failure until the old GPU allocations can be retired at idle. Commit `da50f996884bc054def0c333a95d39cf3cae534d` permits shader/pipeline creation *before* a frame; nonzero matching frame serial is mandatory **at actual record()**.
- **Creator CPU authority:** `tools/vivid-creator/main.cpp` commit `b230637c670b43a3672fbd5435464236453a5f2a` renames checkbox to `Experimental GPU ID check` (still OFF by default), allows native GPU backends with valid current frame serial, saves the expected CPU BVH original source vertex ID and treats any matching GPU result as diagnostic CPU-fed scene corroboration **without replacing the selected vertex**. Any stale, different, occluded or CPU/GPU-disagreeing result retains the prior CPU vertex and shows a diagnostic status. The ID pass is **not** rendering the same vertex override/visible LOD records as Renderer3D; do not claim visual pixel parity.
- **Native regression fixtures (source only):** `tests/vivid/vulkan_sculpt_id.cpp` commit `5778d784cc0d954c6e2dc4c11cb97aea2174d0e5`, registered in root `CMakeLists.txt` commit `50bb0ae552b30fbd225c20c4728eb0eb17e4c5db`: target `aiverse_vivid_vulkan_sculpt_id_tests`, CTest `vivid_vulkan_sculpt_id`. It creates headless native Vulkan (Mesa lavapipe permitted), verifies near/far ID+hardware depth, zero background and closer zero-ID occluder with real async one-pixel callback. `tests/vivid/gles_sculpt_id.cpp` commit `ef4bae9d59e14f077f9c33806a4f5e6101330d0b`, registered in root `CMakeLists.txt` commit `1735f853a1f6feae3dddf569ea7d0aadd64b0f06`: Linux-only target `aiverse_vivid_gles_sculpt_id_tests`, CTest `vivid_gles_sculpt_id`; creates actual EGL GLES3 pbuffer and checks the same ID+depth+PBO/fence readback. Both tests return **77 CTest SKIP only when device/context absent**; an actual wrong pixel is FAIL, not SKIP.
- **Docs:** `tools/vivid-creator/README.md` commit `5fe08b73c9302ee1e67c5e84536e2bd188c657f8`, `docs/vivid.md` commit `6acca990d287bf306781056c004bf3e4aab874f5`.
- **Verification evidence:** Remote GitHub source inspection and update/create writes succeeded. **NOT RUN:** CMake, native C++ compiler, Objective-C++ compiler, CTest, Metal shaders/readback, Vulkan shader compilation/pixels, GLES3 EGL/GPU/PBO callback, Android/iOS/WebGL build, screenshots, performance benchmarks or full `bash tools/vivid-validate.sh --full`. All new targets are *test definitions only*, not tests passing. Linux GLES and Vulkan acceptance cannot be inferred from running macOS/Metal-only validation.
- **Next exact Stage 7AS:** implement a Renderer3D-owned immutable visible-draw/active-LOD and true GPU `ModelDraw::vertexOverride` provenance contract for ID picking, or safely keep the current opt-in CPU-fed GPU diagnostics until such provenance is available. Coordinate RHI resource lifetime and shader/texture semantics with the parallel Texture RHI branch, then run the newly registered Vulkan/GLES CTests on native drivers, and the deferred consolidated macOS build/CTest/Metal suite with actual logs and exact SHA. Retain CPU fallback on unsupported backends and do not merge main until semantic conflict resolution and native acceptance.


## Stage 7AS — 2026-11-20 — renderer-owned source/LOD/override provenance for safe GPU ID confirmation

- **Repo/branch:** `chromealex/AIVERSE`, `feature/vivid-digital-humans`. No changes to `main` or `feature/texture-rhi-unification`, no merge to sibling rendering/animation agents. Genre-independent and no gameplay ECS/animation scheduler mutation. The **separate full handoff** is `docs/agent-handoffs/vivid-7AS-renderer-provenance-2026-11-20.md`, commit `9a0cc7ce13e047b6b25e22fe016b21e90d9e4ab3`, containing detailed API, source SHAs, tests, limits and exact next step.
- **Renderer3D owner API:** `engine/render/Renderer3D.h` source commit `dafe7c78b8d131b612dee187eca370726a6394c4` adds `Renderer3D::ModelViewPrimitive`, `ModelViewProvenance` and `modelViewProvenance(const Model&)`; `engine/render/Renderer3DModels.cpp` commit `c958d57d30a14f19e3c21d093848b79db1f64f33` stamps the renderer's **actual per-node selected ModelItem** with original node/mesh/primitive source index, material/batch/LOD status and exact current `ModelDraw::vertexOverride` handle/count, then provides a read-only current-scene query filtered to colour candidates. Source decisions are reused, not recomputed by a VIVID-specific renderer. The view is a **pre-render staging snapshot**, not a final pixel/GPU draw guarantee.
- **Actual LOD policy:** `93ad294e0440289b59d9443c3611108c4f04b8b1`/`b5014ab05d0f75abd7eaed8b745b94c7857b6cbb` explicitly mark when Renderer3D selected an alternate mesh LOD, as distinct from simply having authored LOD assets. VIVID helper `SculptGpuRendererView.h` commit `49805fd86c8dd07a7e250d543f92230ddb597e5f` and strict LOD validation `8387c998b33afd15f8fa7b65518d1cd795e97de1` reject actual alternative LOD/fade, GPU-conditional LOD, material batches, missing/incomplete source primitive index ranges, baked/ShaderGraph vertex modifiers and invalid source owners, but accept *actual renderer-selected unchanged LOD0* even if the model also has an authored LOD chain.
- **Distinct ModelDraw instancing:** `Renderer3D.h` `da60bc6c0331da415204e03bcc4a228169fe79f9`, `Renderer3D.cpp` `40e496b59be95f8dd476abe9fe7ebf06643de0cc` and `Renderer3DModels.cpp` `679113eac5022e43fccb18d635b5fdf25ebd6460` add scene-scoped `drawInvocation` identity per ModelDraw and per selected primitive (reset at `beginScene`). VIVID `323f8d80880e6c09d6d4f75ddf4a1415949fde6a` rejects multiple Renderer3D draws of the same Model/mesh, even when internal node IDs happen to be identical, because a single CPU world-space picker cannot represent multiple transforms. Source regression `a67a44707d5ed26c68dc3e215b68f544153d63ee`, fixture correction `a7df20bea15e4fe52b6864dcf588cfe192a1b26a`.
- **Creator integration:** `tools/vivid-creator/main.cpp` `bbfbea7b4c921d11372c288a6ce1314c3d7d8b05` moves the existing normal Renderer3D floor/head/hair *staging calls* before the optional ID pass. VIVID then checks the exact `renderer.modelViewProvenance(*human.head)` for its current captured frame and selected source mesh. Other zero-ID imported mesh occluders are now selected from `check.otherVisibleMeshes`, the renderer's actual visible source set rather than an independent loop over all nodes. `0a2efa6a10dee5dcbafa2a57892ba1c1767eed00` removes blanket upfront authored-LOD rejection specifically because the real later provenance determines whether Renderer3D actually selected LOD0 or something else.
- **CPU selection stays authoritative:** the ID pass still renders CPU-morphed/skinned world triangles, **not the actual GPU `vertexOverride`**. `SculptGpuRendererViewCheck::gpuVertexOverridePresent` makes that fact explicit. The Creator commit `fcea49f3ab6fa0e9f02c3cfa4f78b64e19789a90` reports CPU-snapshot corroboration and when GPU override parity remains unverified, without ever silently changing original source vertex. `SculptGpuPickProtocol::pendingIs` source `7126baa7b9bbb28c12a21d2a3be1b4e300bbca21`, Creator invalid-token fix `b6910e731f627e7e09ca84a0dd7532a38fab5d54` and regression `9bf2c15a758f8490b0186f05d2b2a6fff1acef79` prevent stale/cancelled async GPU readbacks (even empty error responses) overwriting the latest CPU picker status.
- **Tests added, NOT run:** pure source fixtures in `tests/vivid/human_creator.cpp` `771d102bedb39beeabddff2f04210fac4878b2fb`, `1cb75bcb981c903fb6c5dc77d664bd5380ae8526`, `a67a44707d5ed26c68dc3e215b68f544153d63ee`, `a7df20bea15e4fe52b6864dcf588cfe192a1b26a` cover renderer-selected source index ranges, frame identity, LOD0 allowed, actual alternative LOD, batch/fade/vertex-offset rejection, multi-owner/instance rejection and positive `vertexOverride` provenance. Native Metal `tests/vivid/renderer_preview.cpp` `ab12efff1d5a7c5661b0a6ccb5e6856611c32d95` now queries actual Renderer3D staging during one-actor playback and asserts that its current GPU actor override buffer appears in the source view. Shader/pixel tests on Metal/Vulkan/GLES from earlier stages remain unexecuted.
- **Documentation:** `tools/vivid-creator/README.md` `373d84818347fb14516ec48e796b8ed635d40637` explains host API, strict provenance contract, frame-only pointer/handle lifetime, and limit of CPU-fed GPU ID source. `docs/vivid.md` `a1f6a0cb0fe088ca27b2b331476f50f6976cde4e` records the architecture.
- **Actual verification record:** remote GitHub file reads/creates/updates succeeded. **CMake/configure/build: NOT RUN. `vivid_human_creator`, Metal `vivid_renderer_preview`, `vivid_metal_sculpt_id`, Vulkan `vivid_vulkan_sculpt_id`, GLES `vivid_gles_sculpt_id`: NOT RUN. GLSL/MSL compile, actual screen pixels, callback timings, large-model metrics, full `bash tools/vivid-validate.sh --full`: NOT RUN.** Source implementation does NOT establish successful native tests. No performance delta or GPU source-vertex parity claim.
- **Remaining limitations / next exact Stage 7AT:** capture the same *actual GPU-vertex-override ModelVertex stream and joint/primitive drawing data* used by Renderer3D, without allocating a duplicate RHI stream or violating GPU fence ownership. Then use a verified GPU ID pass (or a carefully fenced native source-vertex readback) with model/LOD/mat graph semantics of real colour draw. The current renderer provenance is pre-submit, so late GPU ICB/MDI culling, transparency/depth-after, shader variants and final material visibility still require genuine renderer-owned draw submission/GPU-pixel parity checks. Texture RHI has overlapping shared `Renderer3D.h/.cpp/Models.cpp` and `gfx` code; merge only in a dedicated integration worktree after semantically reconciling the agents. Keep CPU BVH fallback and the user-requested deferred consolidated Mac test pass; record exact tested SHA/logs when native execution happens. Produce a new separate handoff after the next stage.


## Stage 7AT — 2026-11-20 — Metal GPU ID compute consumes the actor's actual 60B ModelVertex source

- **Repo/branch:** `chromealex/AIVERSE`, `feature/vivid-digital-humans`. `main` and parallel Texture RHI/Renderer3D feature branches were NOT changed or merged. The full dedicated handoff is `docs/agent-handoffs/vivid-7AT-actual-gpu-vertex-source-2026-11-20.md`, created at `4dd1784c223f8fb7515e48faab49a7e3e1e69cb5`; it contains exact APIs, ownership, code and test commits, limitations and next steps.
- **Problem:** Stage 7AS knew the exact Renderer3D current-frame `ModelDraw::vertexOverride` GPU source handle and selected primitive ranges but VIVID's ID pass still uploaded a separate CPU-morphed/skinned selected-triangle geometry stream. This could make GPU picking apparently confirm a vertex that the real colour pass displaced elsewhere.
- **New Metal GPU-source expansion:** `engine/character/vivid/SculptGpuVertexSource.h` commit `dbf39e5252d7c4b78fbb438d81bf83d45e705b06` (+ include fix `1fba918548ed80c78ccc3f2e63d4946e037e5edc`) defines 8-byte `SculptGpuSourceIndex`, 32-byte `SculptGpuSkinInfluence`, 80-byte `SculptGpuSourceParams`, `SculptGpuVertexSource` and `prepareSculptGpuVertexSource`. It builds only original source triangle indices/IDs, seam clone→source mapping, the owning skin palette's four normalized U8 influences and current MotionWeave `jointWorld * inverseBind` matrices, but never uploads a CPU-expanded source vertex position stream. Metal `kSculptGpuSourceExpandMSL` gathers x/y/z directly from the **same current-frame RHI GPU 60-byte `ModelVertex`** used by Renderer3D at 15×uint stride, applies rigid model/world or skin palette transform and writes a GPU 16-byte `SculptGpuIdVertex` with correct mesh-local RGBA8 Triangle ID.
- **ID pass integration:** `SculptGpuIdPass.h` commit `65c9488be606e160e66a0eecffaf830d380dc66c` creates optional Metal compute pipeline and tracks new temporary GPU buffers throughout the actual asynchronous pixel completion; `01d86893a680de1ded8979aaf31badb6398a6055` adds optional `record(...,const SculptGpuVertexSource*)` and excludes previously duplicated CPU selected face positions; `e3f812fab8cbe595f5e1b0e521fad1206dceaa0f` dispatches the same-frame ordered compute gather before existing RGBA8+depth ID pass, draws its GPU-generated source triangles and the existing CPU zero-ID scene occluders and GPU alpha-mask batches. `7e293b0a39069b4843b7155dc9b8223b82b2ead7` fixes C++20 byte-size arithmetic; `7dfba9772f8009d0213e8a6d9bf10d66eb532210` validates exact indexed triangle source ordering and finite skin inputs. Completed readback returns `SculptGpuIdPass::Completed::actualOverride=true` only if the actual RHI actor GPU buffer was used, rather than assuming a GPU raster means GPU source fidelity.
- **Creator:** `tools/vivid-creator/main.cpp` commit `fbf1510f3f7c8a07c882b52caddcc34ea27458c2` uses the Stage 7AS Renderer3D current-frame source provenance, requires exact same GPU vertex handle/count as the prepared `ModelDraw`, and prepares valid Metal GPU-source index+skin parameters using its current MotionWeave `facePose` and actual renderer-selected source world transform. Missing compute, inconsistent materials, unsupported alpha/ShaderGraph, invalid skin palette, wrong LOD or source mismatch rejects only the optional GPU ID confirmation and retains the already selected CPU BVH original vertex. Native Vulkan/GLES continue their prior CPU-fed diagnostic path; WebGL2/Windows GL loader CPU fallback remains.
- **Source-only regression tests:** `tests/vivid/human_creator.cpp` commit `e0995e48d7430be20a23d38f8e1e3e7338c4be3e` adds checks for clone IDs, original source mesh index order and nonauthorable LOD zero ID, rigid source world transform, skinned two-joint U8 normalization, MotionWeave transforms, wrong palette/override vertex count transactional failure and the GPU MSL source addressing contract. **New real hardware fixture** `tests/vivid/metal_sculpt_id.mm` commit `a7f3400e63a7de02a9639e61ef1a1ab3ca76a08d` uploads an actual 60B Metal vertex buffer shifted off-screen while CPU-pick world surface remains unchanged; expects the GPU ID to disappear in the centre (proving GPU instead of CPU positions), then checks the original stream and a GPU-skin-palette joint translation moving source out of the pixel, plus `actualOverride` completion flag. Both fixture sources are **unexecuted**.
- **Documentation:** `tools/vivid-creator/README.md` `8f8ade4b28af8362c6af0d84631e0458001e4a24` explains explicit Metal GPU-source semantics and compatibility gates; `docs/vivid.md` `058d0135fb9cd7f04caa47f9b2d4222b9421e4b0` records the implementation.
- **Actual verification:** GitHub file reads/create/update operations succeeded. **C++/ObjC++ native compilation, CMake, CTest, Metal shader compile and GPU pixels, Vulkan/GLES drivers, runtime/VRAM/performance benchmarks and full `bash tools/vivid-validate.sh --full`: NOT RUN.** Tests are source-only additions; no claimed measured GPU parity or pass result.
- **Remaining blockers / Stage 7AU:** The actual 60B GPU actor morph position now drives selected Metal triangle geometry, but its **separate GPU compute skinning** still needs real parity measurements against Renderer3D's native per-primitive skin shader. Masked selected faces need GPU UV+alpha ID expansion; non-selected occluders still rely on CPU world positions; ShaderGraph vertex displacement, transparent/depth-after cornea, GPU ICB/MDI culling, active LOD and final screen depth are not proven pixel-equal to the colour pass. Vulkan/GLES3 GPU source expansion is absent (only CPU-fed ID and async pixel readback exist), WebGL2/Windows loader remain CPU fallback. Coordinate overlapping `Renderer3D*`, `Gfx*`, Model, ShaderGraph and Texture RHI changes on a separate semantic integration branch. Continue staged feature closure with one separate handoff per stage; afterward perform actual macOS native `vivid_human_creator`, `vivid_metal_sculpt_id`, renderer preview and `bash tools/vivid-validate.sh --full`, plus independent Vulkan/GLES hardware tests on Linux/Android; report exact tested SHA/logs.


### Stage 7AT safety addendum — transparency (2026-11-20)

- Renderer3D view provenance now exposes whether each current selected ModelItem belongs to the actual colour-pass translucent class (instance tint alpha, material Blend/transparent flag or transparent DrawFlags). `engine/render/Renderer3D.h` `b692e2dcbc97a0d15960711d55a4d79e6b791e18`, `engine/render/Renderer3DModels.cpp` `6eb5917d9dbe3d4a682d8d22877a672ca5c1db19`; `engine/character/vivid/SculptGpuRendererView.h` `094b3afecaeaa0c5e164d06d16cb5612dfa1d5e9` explicitly rejects such views instead of falsely confirming an opaque GPU ID. Unexecuted regression fixture in `tests/vivid/human_creator.cpp` `2bb360b24b7f90977b1713737dd2b40a9bbe26e1`. CPU BVH remains unchanged. The dedicated Stage 7AT handoff has also been updated at `5582b7e75c133cbe13c4394e261dfce48fa4ee4f`. **Native build and GPU tests NOT RUN.**


## Stage 7AU — 2026-11-20 — selected alpha-masked GPU actor source and real GPU UV/depth ID

- **Branch and scope:** `feature/vivid-digital-humans`, `chromealex/AIVERSE`. No update/merge into `main`, Texture RHI, MotionWeave or other agents' branches. Full separate handoff: `docs/agent-handoffs/vivid-7AU-gpu-mask-2026-11-20.md` (creation commit `971c4a4fe84ef5ba943e7ce976f8dcf3fd4c8ab1`).
- **GPU selected Mask source:** `engine/character/vivid/SculptGpuVertexSource.h` implementation `d5de637cddbf3cfb9183407d0a70d10f6566cbdc` adds homogeneous material eligibility, sampled Mask texture/UV/cutoff data, 16B input per original vertex and `kSculptGpuSourceMaskExpandMSL`: actual borrowed 60-byte GPU ModelVertex position + current MotionWeave/rigid matrices + immutable source UV/alpha → 32B `SculptGpuMaskVertex` GPU output. Opaque selected path remains 16B. Mixed opaque/Mask or different masks, custom/Blend, missing GPU texture, invalid UV/palette fail closed with transactional output preservation.
- **ID pass:** `engine/character/vivid/SculptGpuIdPass.h` `6cec08562902e05706cd5bec02527f9a6327b5d8` adds native `expandMaskCompute_`, `supportsActualGpuMaskedStream()`, exact source vertex/ordinal ID and texture/uniform validation, fifth mask-input buffer, GPU expanded 32B selected mask draw through the existing sampled fragment shader and native depth buffer. Selected CPU geometry is suppressed only for validated GPU source; other CPU zero-ID/alpha occluders are retained. The borrowed original actor GPU source is not retired by the ID pass; scratch buffers stay pinned through actual async pixel completion or verified GPU idle.
- **Creator:** `tools/vivid-creator/main.cpp` `7e4132414d00142a7862c0e517b495a8bd95c11c` checks that the mask compute shader is actually supported. The GPU ID reply still cannot change a CPU BVH-authored original source vertex.
- **Tests added but NOT RUN:** `tests/vivid/human_creator.cpp` `f1e49e080db54ccad5dd0f456b33cc4e914cfebe` covers UV/vertex alpha/IDs, invalid UV or mixed/custom masks with rollback and 16/32B shader contracts; `tests/vivid/metal_sculpt_id.mm` `0b4650bb2c255603debc40f251d1ed5d5983da4e` adds actual GPU actor displacement under sampled Mask and opaque-versus-transparent GPU alpha texel readback assertions. These are test **source definitions**, not reported pass results.
- **Documents:** `docs/vivid.md` `1bfd4f552a410addd197fce6e7af5ce90bc233c6`, `tools/vivid-creator/README.md` `1dee4519ab4b1f25d17acc6d3a46fa8cbb4f58dc`. No public VIVID DNA/face-52 channel/asset/ECS/AnimationGraph API break or genre dependencies introduced.
- **Actually verified:** remote GitHub file writes/readbacks plus source-only review of the new GPU shader inputs, byte ABI guards, ID order, gate, Creator fallback and fixture presence. **NOT RUN:** C++/ObjC++ compilation, MSL shaders, CMake, CTest, Metal/Vulkan/GLES real GPU pixels, full `bash tools/vivid-validate.sh --full`, benchmarks, final pixel parity and full AIVERSE regression. The source-only checks are not native acceptance.
- **Remaining / next Stage 7AV:** carry out a real GPU renderer-colour vs sculpt-ID matrix/alpha/depth parity assessment; after deferred consolidated native Mac acceptance, resolve any reported compiler or shader errors. Native Vulkan GPU actor expansion and GLES3 GPU-source evaluation remain absent; CPU-fed ID on those backends is not GPU morph-vertex parity. Texture RHI and Renderer3D ownership overlap requires separate semantic integration before main. ShaderGraph displacement, Blend/cornea, alt LOD, GPU culling and GPU-deformed other occluders remain unsupported exact parity; broader MetaHuman-grade digital human authoring still incomplete. Maintain one bounded source commit plus a separate committed handoff on the same branch after each subsequent stage.


## Stage 7AV — 2026-11-20 — Renderer3D 3×4 skin-row parity for Metal GPU actor ID

- **Branch/ownership:** `feature/vivid-digital-humans`. No main/Texture RHI/MotionWeave merges or updates. Separate full dedicated handoff: `docs/agent-handoffs/vivid-7AV-renderer-skin-parity-2026-11-20.md` created at `e39fb7e0a571f16c4d6da084845cfc50bd7e9b1e`.
- **Renderer alignment:** `SculptGpuVertexSource.h` commit `bf029cb76160df479d7301d35c300cfb5ab5aeae` adds 48B `SculptGpuSkinRows` and `sculptGpuRendererSkinRows()`, uses the same original `ModelShaders.h` row-blended GPU weights and three row–position dot products for both opaque and sampled-mask Metal compute, and applies `params.rigidWorld` **after** model-space skinning. Original weights are UNORM8-to-float and normalized on the GPU like `Renderer3D`; rejects primitive palettes exceeding native 80×3×float4 U3 limit. Source RGB/UV/asset schema unchanged, borrowed 60B current-frame actor buffer retained.
- **ID pass:** `SculptGpuIdPass.h` `7ea9d930df98291f44d60273c2c7479ec5f638e7` packs and uploads 48B rows rather than full 64B joint matrices. `f16902b248a1707ed71e3717225e24cd7fb61b7` verifies selected sampled GPU mask's indexed original source UV and vertex alpha match the selected texture input; `6eef1294e503a0afec104f23e396d6b8b29b8eb4` bounds-checks the original vertex index before dereference. Async GPU scratch lifetime/retirement and CPU BVH authority stay unchanged.
- **Creator:** `tools/vivid-creator/main.cpp` commit `34c86578923b0c4fb183de175d01a3780f230651` requires every selected renderer primitive to have one identical world matrix, 60B override and skinning mode. Unsupported source remains CPU-only and does not fabricate a GPU-confirmed original vertex.
- **Tests added, NOT RUN:** `tests/vivid/human_creator.cpp` `51b95df0bea96dba133b6d47279af328b4c90e97` now checks weighted two-joint matrix algebra, 48B layout, renderer shader source contract, root world order and over-80 palette rollback. `tests/vivid/metal_sculpt_id.mm` `85800430f7f49184fa19ce4cb8bc0441df3bdb1a`, `0b859b6ba05fc4ea335876e44aad1e009cd5fa5d` adds native GPU weighted-joint / compensating world pixel oracle and a stale UV rejection test. Their contents exist but have **not** executed.
- **Documentation:** `docs/vivid.md` `5b51e0045fcb65ba3b3ba668f19e0a5441ec41ae`, `tools/vivid-creator/README.md` `b0c30dcf68c603d102e5025de934a7a77c27f323`. No JSON/asset 52-face-channel, genre, ECS or MotionWeave API changes.
- **Verification actually done:** GitHub source reads/commits and 14-condition **static source review** of both compute kernels, row-buffer size, source palette limit, masked UV gate, Creator matching and fixture presence. **Native CMake/compiler/CTest, Metal GPU shader compilation or real colour/ID pixels, Vulkan/GLES devices, full `bash tools/vivid-validate.sh --full` and performance: NOT RUN.** This is arithmetic/source-order alignment, not a proven renderer-colour pixel parity result.
- **Next Stage 7AW:** capture Renderer3D's *accepted main-colour draw* primitive/override/material provenance after actual submit and compare a physically rendered scene colour/depth pixel with the VIVID ID target using a fenced async readback. Verify dynamic skinned selected actor plus nearer sampled alpha occluder; retain strict rejection for GPU ICB/MDI, late depth-after, ShaderGraph displacement and alternative LOD until supported. Vulkan/GLES3 GPU source expansion, scan/hair and broader MetaHuman-level authoring remain incomplete. Coordinate renderer changes with peer owners on isolated semantic integration branch and run deferred Mac/other backend hardware tests before declaring parity.


## Stage 7AW — 2026-11-20 — actual encoded Renderer3D main-colour submission provenance

- **Branch:** `feature/vivid-digital-humans`, based on previous 7AV HEAD `7f0a0026c6b8da0ea5029d6fb155886011092ce0`. `main`, MotionWeave/animation and Texture RHI branches were NOT modified/merged. Full separate handoff: `docs/agent-handoffs/vivid-7AW-accepted-draw-provenance-2026-11-20.md` created at `2c07118b02a79f3c105dcc14c1e79e099686e4c3`.
- **Renderer3D accepted-command API:** `engine/render/Renderer3D.h` `97f5de199fc51e991ae3e6875496f0e9aa63bf55`, `5c95606810cf86b1f3a0a5d57926739386515731`, `d0486d79abc4976ce9cbe5ef5f198ee6be55a115` adds opt-in one-model `trackModelSubmissions`, `modelSubmissionProvenance`, `ModelSubmittedPrimitive` and `ModelSubmissionKind` (single Direct vs DirectInstanced/MDI/GPU indirect/ICB), frame-scoped source primitive/draw/pipeline/vertexOverride fields and late-opt-in rejection. `Renderer3DModels.cpp` `7e5564d3f4b5245abd17b8b1a908f561f9969a0b`/`3e2e2553c665a8fb42da078faaf764aafdfd97e7` logs **actual encoded** direct indexed and successfully accepted indirect/ICB draw branches, not warmup preflight; `Renderer3D.cpp` `3810b1be902a224369c71368bd71b1ca768bee66` resets at beginScene and closes main-colour submission epoch after `renderModels(1)`.
- **VIVID strict post-submit check:** `engine/character/vivid/SculptGpuSubmittedView.h` `e12c1f0a1c980a0250cc86980e49c83c1c510915` compares staged source view against encoded full original main opaque source draw, frame serial and GPU override, with compiled pipeline and identical node/mesh/invocation/range/world/skin/LOD. Rejects missing/late/changed/indirect/instanced source. This proves *RHI command encoding*, not GPU completion or final pixel visibility. No parallel VIVID renderer database or gameplay ECS mutation.
- **Creator pass order:** `tools/vivid-creator/main.cpp` `d8d0679a474b7056742a2054fbd565d95480978c` records GPU ID only after actual `Renderer3D::render()`; temporarily closes its main UI pass, records optional GPU compute+ID pass, resumes without clearing existing colour/depth, restores full viewport/scissor. Rejections retain CPU BVH original vertex; `d6da6b952bdbf41bb0ce910fada667dfd49ec63b` clarifies status for unsupported GPU source vs accepted renderer draw.
- **New test sources, NOT RUN:** `tests/vivid/human_creator.cpp` `0ffdb27242722294731fe3951cc0ad2b49f4a3fa` validates missing/invalid accepted draw, stale frame, wrong index/world and ICB/pipeline/over-limit failure. `tests/vivid/renderer_preview.cpp` `c21c2758eec9f3a8e57a428d9671dcd073f646d5`, `11502d6ef9e8de2fe3329f653ff7f3a3c00f1214`, `db6989bc77181c50e6658af631771e2e3c500896` adds unexecuted real Metal preview assertions: main-colour actor GPU direct draw submitted and exactly matches staged source once pipeline warmed.
- **Documentation:** `docs/vivid.md` `948a6b8fac5c9b83d40335fd3eaabcf166e39272`, `tools/vivid-creator/README.md` `77bb6ae0ef28af6c95641febc51dd837949fc287`. No ModelVertex 60B, HumanDNA JSON, 52 facial-channel ABI, MotionWeave ECS scheduling, genre or existing GPU ID public source signature changed.
- **Actual checks:** GitHub connector read/write/commit operations plus **16 static source-contract inspections PASS** (recording at real direct/indirect/ICB branches, frame reset, only accepted draw, strict VIVID source/frame check, Creator post-render ordering/pass restoration, fixture presence). **CMake/native C++/ObjC++ compilation, CTest, MSL pixel execution, renderer colour/ID depth comparisons, performance/VRAM, Vulkan/GLES hardware and full `bash tools/vivid-validate.sh --full`: NOT RUN.** Never interpret the 16 source checks as green native tests.
- **Next 7AX:** Implement and actually verify a real Metal framebuffer colour/depth ↔ GPU ID pixel oracle with one dynamically skinned actor and a nearer alpha-masked overlapping occluder, using RHI async completion and correct screen-pass suspend/resume. Resolve remaining shader/compiler results when consolidated Mac tests are run. GPU ICB/MDI, ShaderGraph, cornea/depth-after, active LOD and other GPU-deformed blockers still lack full GPU colour parity; Vulkan/GLES lack true GPU actor source expansion. Semantically integrate overlapping `Renderer3D*` changes with batching/Texture RHI agent branches before any main merge, keeping per-stage handoffs and no false hardware PASS claims.


## Stage 7AX — 2026-11-20 — actual Metal Renderer3D screen-colour ↔ GPU actor ID pixel coverage

- **Branch/base:** `feature/vivid-digital-humans`, starting at Stage 7AW HEAD `3b23f0fce1fe20d4246878d89b2b67f166bc0f24`. `main`, MotionWeave, Texture RHI and concurrent renderer/batching branches were NOT modified or merged. Full separate handoff: `docs/agent-handoffs/vivid-7AX-metal-screen-id-pixel-coverage-2026-11-20.md`, created at `76d4c2dc16817dad9394fad3b811fccde6220d3c`.
- **True same-frame pixel fixture:** `tests/vivid/renderer_preview.cpp` `83d662f3113cf858dc115e506dbd52d0521bac0e`, `93d18a7cd4ff5c7b772fe84f317066ef853899b3`, `ddd9ecd134e45e7c293d9f79dbe1ae01c8747803` extend existing one-actor real Metal Renderer3D preview. During GPU morph and MotionWeave motion screenshots, accept exact renderer source/encoded draw, project a head-centre top-left screen pixel, request the **presented normal colour** frame and an ID pixel from the **actual borrowed 60B GPU actor vertex override** after ending the main pass. Correlate both independently asynchronous callbacks by request stage/frame/pixel; reject stale, missing, empty or CPU-fed results; serialize one in-flight query; retire ID resources only after genuine GPU idle. Do not interrupt 12-actor crowd preview. Existing 3 screenshot A/B metric remains mandatory.
- **Portable colour coverage classification:** New `engine/character/vivid/SculptGpuColorPixelOracle.h` `ebb7dd3e854ae49f824276601e05351cc485750a` defines Covered, Mismatch, ClearBackground and Inconclusive. It compares normal framebuffer RGB with the known clear colour **only for visible coverage** while treating ID=0 with unrelated visible colour as inconclusive. It does not claim raw RGB or numeric GPU depth equality, nor alter face/skin/JSON/ECS ABI. `tests/vivid/human_creator.cpp` `741b7a889129663e46023f653ec48e446f54e3de`/`cd469962997186a4647838749cd55b5e1ed3040a` adds **UNEXECUTED** fake colour/ID classification regression.
- **Real Metal negative alpha/depth ID fixture:** `tests/vivid/metal_sculpt_id.mm` `007bb2e9597d0dfb7ae6e17d2fa7dbd3910c306d` adds an unrelated nearer GPU-sampled Mask zero-ID depth occluder: transparent texel leaves source ID, opaque texel blocks it. The occluder is still CPU-geometry-staged and tested in the independent ID pass; it is NOT claimed equivalent to a separately GPU-deformed renderer-colour blocker.
- **Acceptance runner:** `tools/vivid-validate.sh` `d1bfba67085f97c908901c0705f6f96b3d2724e0` requires actual one-actor screenshot/ID coverage logs for stages 1 (morph) and 2 (MotionWeave), in addition to prior GUI and CTest checks. The user-requested consolidated validation remains deferred; a grep assertion existing in a script is not a test pass.
- **Docs:** `docs/vivid.md` `77d03063e0a4b304a1434c22419c4308ca4039c6`, `tools/vivid-creator/README.md` `35fc217c9a5371463d1ac6e576364b74207f04c2`. No actual Renderer3D, texture RHI, MotionWeave, source 60B vertex format, HumanDNA schema, 52 face-channel ordering or genre modules changed in this stage.
- **Actual validation:** GitHub connector read/write/commit and **17 source-only contract inspections matched** (accepted source, real colour capture, ID GPU handle/frame token, callback pairing, fail-closed statuses, native Metal source and masked depth fixture, shutdown lifetime and acceptance runner). **NOT RUN:** C++/ObjC++ compile, CMake, CTest, GUI preview, Metal shader compilation/pixels, actual depth readback, full `bash tools/vivid-validate.sh --full`, Vulkan/GLES drivers, performance and VRAM. Do not claim real GPU pixel PASS.
- **Next 7AY:** Use Renderer3D-owned final colour and depth targets with a *separate dynamically GPU-deformed alpha-masked occluder* to test real framebuffer colour/depth↔ID parity. `gfx::Device::readTexturePixelAsync` exposes RGBA8, not native depth; numerical depth testing requires an explicit backend-safe depth encoding/shared attachment instead of CPU guesses. Resolve potential sample orientation/gamma/Metal shader errors during consolidated native Mac acceptance. Remaining ICB/MDI, alt LOD, ShaderGraph displacement, Blend/cornea, GPU-deformed other occluders and Vulkan/GLES3 actor source expansion are not complete; preserve CPU BVH fallback and integrate peer renderer/Texture RHI edits only on an isolated semantic merge branch.


## Stage 7AY — 2026-11-20 — true Metal Depth32Float parity and independent GPU-source Mask depth blocker

- **Branch/source:** `chromealex/AIVERSE`, `feature/vivid-digital-humans`, based on Stage 7AX HEAD `32785d29d97a949904c4bdf673b4b195114afb4b`. No changes, rebase or merge into `main` or other agent branches. **Dedicated full handoff:** `docs/agent-handoffs/vivid-7AY-native-depth-gpu-mask-2026-11-20.md`, created in separate documentation commit `c127b5b95159fd979dee2c2b410ff5be3686ac82`. It records all exact implementation SHAs, API/ABI, tests, blockers and next-stage actions.
- **Actual native-depth RHI:** `engine/gfx/Gfx.h` `3123e30ff6e24d19ca1be91b93659228acb5940d` adds optional `gfx::Device::readTextureDepthPixelAsync` with unsupported default on other backends; `engine/gfx/metal/GfxMetal.mm` `3fca10e38099b13f7d4b757711d14e01148e6b25` adds owner-thread async 1px 256-byte-row-aligned hardware Depth32Float blit from explicit offscreen or current screen depth. Rejects invalid/missing source or GPU failure; never substitutes CPU-projected depth.
- **GPU ID source + two callbacks:** `engine/character/vivid/SculptGpuIdPass.h` `7d60eb5616d4b4aeb6124e50aade905b31bcab8d` adds opt-in `record(..., captureDepth=false, gpuMaskOccluder=nullptr)` and `Completed::{depthValid,depth01}`, gated on both real colour/depth readbacks; real GPU scratch pinning and verified-idle retirement retained. `engine/character/vivid/SculptGpuColorPixelOracle.h` `c51175a25d9b05f46c172600dda446cbe1798b23` defines true GPU depth comparison with finite/nonclear/range/tolerance gates.
- **Exact-frame renderer scene colour/depth:** `tests/vivid/renderer_preview.cpp` `3da6cc91ac0f8b328bd2e96f694aab3daac1eea8`, `a511479a933518fb0728f84cbc22055f7bb1b185`, `6e34effe3e39db75bfcaeb97d23f207c7dbfaed6` capture screen colour using current-frame `copyColor` + RGBA8 async pixel read (not lagging screenshot callback), and true current-screen Depth32Float versus ID target depth at the identical top-left pixel and frame/actor token. Provisional native depth delta max 0.003; existing full screenshots still serve A/B visual regression.
- **Independent GPU Mask depth source:** `SculptGpuIdPass.h` `053cf98a57e5769e6dc6c72ddde3f7d73f840ad9` builds a second borrowed GPU vertex stream and Mask compute source with all-zero selectable triangle IDs, real GPU position/depth and original sampled mask UV/cutoff; resources live through async completion. Pure source check `engine/character/vivid/SculptGpuVertexSource.h` `86da013f7b6edf140535f39954ede5d8847ab938`, `a8915f980089e649c455c959bef987ae542ea303`; `SculptGpuIdPass.h` `b8e89c9d5f4df19df6017da66657bc5a5995b651` shares preflight. The host must still verify the other actor's actual renderer-colour provenance; it is not automatically enabled for every Creator scene.
- **Existing selected GPU Mask fixes:** `SculptGpuIdPass.h` `647ae3be56e04e6b58e55d67ff24908f628ef340` compares exact original indexed source count against total selected triangles (not opaque-only); `a0e6f24219bea4eefc2b90da0d87a9a848121425` accounts for GPU-only masked geometry in nonempty/triangle budget. Ordering comment corrected `ac5382d052abb5a4f7d59cd35acc100cc4c9de1b`.
- **New test sources, not run:** `tests/vivid/metal_sculpt_id.mm` `0312aeef83327509a705648784de7b2095aca1ab` reads native depth for near/blocked/masked alpha-hole and solid pixels. `c343ab61a9f0d0176a52e978ca9183eccddec0f5` adds two independent GPU-resident 60B sources and moved masked occluder/hole/solid depth/ID oracle. Source buffers were CPU-uploaded (not shown GPU morph-compute-written); this fixture is **not** normal Renderer3D-colour parity of the second actor. `tests/vivid/human_creator.cpp` `cbf7e4011934cb0144fecaaeac1562c22c785d04`, `c6cf82d72c4a240c8826d318bf2fae51ee47a548` add malformed occluder/UV/index and finite/clear/NaN depth fixture sources. `tools/vivid-validate.sh` `930f6bdcacf621ff9a572f453329ca6c9438b5fa` now requires actual renderer depth scene/ID deltas in the single-actor native preview log.
- **Docs:** `docs/vivid.md` `5252f5182f6c1eaedb4032394a29e748eca0957d`, `tools/vivid-creator/README.md` `c7ffcdfcfafda5485d5bcc4e27f44e9243d3e1b3`. HumanDNA JSON, 52 ARKit face-channel order, 60B `ModelVertex`, ECS gameplay authority and MotionWeave animation scheduler unchanged. RHI virtual method optional/fail-closed on Vulkan/GLES until implemented.
- **Actual verification:** GitHub source reads/updates and **22 source-only static inspections matched** expected RHI/Metal/callback/input/depth/fixture contracts. **NOT RUN:** native CMake, C++/ObjC++ compiler, Metal shader or pixel tests, `vivid_human_creator`, `vivid_metal_sculpt_id`, GUI preview, Vulkan/GLES GPU parity, VRAM/timing and `bash tools/vivid-validate.sh --full`. Do not claim hardware depth parity PASS on this basis.
- **Next Stage 7AZ:** render the same second dynamically GPU-deformed alpha-masked object in actual Renderer3D *main colour/depth* and VIVID ID, track/verify both accepted main-colour draw sources, compare truly native colour/depth and ID at transparent/solid texels, and prove GPU-compute-written source positions. Continue to fail closed for unsupported ShaderGraph, GPU ICB/MDI, alternate LOD, Blend/cornea and complex GPU occluders. Hardware test Stage 7AY on macOS and semantic reconciliation with Texture RHI/batching agent edits on isolated integration worktree are required before main merge; Vulkan/GLES actor source and depth readbacks remain separate milestones.


## Stage 7AZ — 2026-11-20 — second GPU-compute-written Mask actor in real Renderer3D colour and VIVID ID

- **Branch/base:** `feature/vivid-digital-humans`, `chromealex/AIVERSE`, starting HEAD `dffad02577e5b235b05ddde2fb7637b948281a9b`. No writes or merges to `main`, MotionWeave, Texture RHI, Prisma or batching agent branches. Separate full handoff: `docs/agent-handoffs/vivid-7AZ-second-actor-mask-parity-2026-11-20.md`, created at `38e2326804a45ac5c0a09ddf767a184c9853a89c`. It carries the complete individual code SHA table, API, caveats and native commands.
- **Renderer3D paired provenance:** `engine/render/Renderer3D.h` `3f29e386809ff42d52cd63c9eeb4e50c627f1d3b`, `Renderer3D.cpp` `36550347e4e7df6b8cf5da360080c5c0aaa4f892`, `Renderer3DModels.cpp` `a9fc10425fa4f768c254304e2eaa291a89196752`. Opt-in `trackModelSubmissions(selected, occluder=nullptr)` records two current-frame Models without breaking prior one-Model calls, clears both on beginScene, rejects late opt-in and returns only the queried Model's actual accepted colour commands. The records are still bounded and are **encoded submissions**, not proof of final pixels.
- **Paired source invariant:** `engine/character/vivid/SculptGpuSubmittedView.h` `0cc8598e9c73cb7eeeed38c049a960625776bc11` adds `sculptGpuPairedRendererViewsAccepted`: two independently accepted normal main-colour direct draw sources, one exact frame, two distinct borrowed 60B GPU buffers and matching original primitive/LOD/pipeline. The helper is used by the native preview in `3697290e06f51b86aa268ec9d4c22395256256fe`.
- **Native Metal two-model fixture:** `tests/vivid/renderer_preview.cpp` commits `7bf6e98dce6ef7ca20fd2033c00a693618f635f0`, `c9914c1b57203db32300c995d162b10fccec1f95`, `1664d47a262488220b1654a896560251a9938e5d`, `c0587820a9688a42d9ad4b1f9c2738711f1ed12b`, `2bab4a8c2777948938706464494d5effa59abcdf`: optional `AIVERSE_VIVID_MASK_PARITY=1` creates a second double-sided sampled `AlphaMode::Mask` Model and two immutable alpha-hole/solid textures; a **real Metal compute producer** copies all 15×uint of its 60B source vertex and writes an independent GPU position displacement. This exact GPU buffer is rendered by the normal `Renderer3D::drawModel` colour/depth and by VIVID's zero-ID GPU Mask occluder path, with both original sources validated before ID submission. Stage1 alpha hole demands selected human ID; Stage2 solid demands zero selected ID and normal Renderer3D foreground colour plus matching actual native GPU depth. The real Morph+MotionWeave screenshot A/B remains intact. Extra GPU resources are retired only after a verified gfx queue-idle barrier.
- **Test and runner source:** `tests/vivid/human_creator.cpp` `436d097e05c363d0b07a0f48a5c33508a2492bf5` adds UNEXECUTED paired owner, stale frame, missing draw and changed primitive regression. `tools/vivid-validate.sh` `8ea5876c0397d03ee80bb7c7db8226bf3b3e6bd6` now runs separate `renderer-mask-parity` real Metal GUI invocation and requires 3 screenshots and exact `mask=alpha-hole` and `mask=solid-occlusion` stage PASS log markers. Old single actor and 12 actor modes unchanged.
- **Documents:** `docs/vivid.md` `9869faecc0e070144ae16d0658dd216fd3965ff9`, `tools/vivid-creator/README.md` `df8fc048d10d7194fea60944c8f09db570b0bd99`. ModelVertex 60B ABI, VIVID HumanDNA recipe, 52 face channels, MotionWeave/AnimationGraph and gameplay ECS unchanged. No automatic scene-wide GPU occluder gathering in normal Creator.
- **Actual verification:** GitHub connector source reads/commits and **23 static code-contract checks matched**, covering two filtered renderer sources, actual native second compute producer, normal renderer draw, same GPU buffer in ID, alpha-hole vs solid expectations, real depth callbacks, resource lifetime, unit fixture source and GUI runner gates. **CMake/C++/ObjC++ build, Metal GPU shader compilation, native CTests, real pixel/depth capture, GUI smoke/crowd/mask modes, `bash tools/vivid-validate.sh --full`, Vulkan/GLES, VRAM/benchmarks NOT RUN**. No GPU PASS claims.
- **Next Stage 7BA:** native Mac compile and shader/pixel/depth acceptance first, then optional renderer-owned bounded multiple-GPU-occluder collection for Creator and wider materials, while preserving CPU BVH authority and rejecting unproven ShaderGraph, Blend/cornea, ICB/MDI and LOD. Vulkan/GLES genuine GPU-source expansion remains open. Integrate any overlapping `Renderer3D*` files with Texture RHI/batching agents only on an isolated semantic merge worktree; do not merge VIVID directly to main before real acceptance and conflict audit.


## Stage 7BA — 2026-11-20 — bounded multiple GPU Mask occluders / renderer-verified material and depth

- **Branch/base:** `chromealex/AIVERSE` `feature/vivid-digital-humans`, previous Stage 7AZ HEAD `38c8e63854b9bd7e5f537050cecc2684170b633b`. No write/merge to `main`, MotionWeave, batching, Prisma or Texture RHI branches. **Separate full handoff** `docs/agent-handoffs/vivid-7BA-multi-gpu-mask-2026-11-20.md` created at `46549e2e1d807d27341be42edd0a19c603de76be`. This dedicated file includes the complete code commit/SHA table, interface and limitations.
- **Renderer3D 1+4 current-frame sources:** `engine/render/Renderer3D.h` `af76a5c2344e424190884e4e8cdabe09053f52d7`, `Renderer3D.cpp` `6fc61ae1408dda54c4e4d689d80078d95bf262e5`, `Renderer3DModels.cpp` `72564c3dd639cd46c9c720af46e4de9562bffad7`, `09c494a9a32df45c71fed816f10b7893813ef56d`. New opt-in `trackModelSubmissionsMany(span<Model* const>)` tracks max 5 unique source Models in the current camera while preserving single/pair compatibility and frame reset. Each query returns only actual accepted direct/indirect/etc commands belonging to that Model; duplicated, untracked, late, invalid or over-capacity Model registration fails closed.
- **Exact renderer Mask material:** `Renderer3D.h` `2392b1c8f569d351df4c8e2848bf6256e9e96d3b`, `Renderer3DModels.cpp` `a41d6bf0e169b21d2e641e089f93c68bda9351ce`, `engine/character/vivid/SculptGpuSubmittedView.h` `b93012628cca546dcb226a62850f08e507ac355f`. Both staged and accepted source records capture real alpha-mask texture handle, alpha factor/cutoff, UV offset/scale/rotation and vertexColors option. Exact normal-colour-vs-GPU-ID texture/material source matching rejects stale/republished alpha state.
- **Transactional GPU scene collector:** new `engine/character/vivid/SculptGpuRendererMaskSources.h` `acd2ab544f4704e69195dc53460ec28ffec30b5f`, `cd451c9ef0172b924c1c41e268700e9a826a0a11`, `6491e7ec8302d0c6adbde83e96fb53734706ada5` accepts bounded *host-known* current visible candidate models/poses and returns up to four native GPU Mask source records derived solely from real Renderer3D staged+accepted colour draws, world/LOD/current frame and exact 60B vertexOverride. Invalid candidate aborts the entire collection (no skipped hidden blocker). No all-scene discovery or extra gameplay ECS.
- **Multi-source Metal ID/depth:** `engine/character/vivid/SculptGpuIdPass.h` `5489c31ff87a434ab75e786de54cde2d09037749`, `4e0412bc714148ecf6459f6099bc55123fab692c`; `engine/character/vivid/SculptGpuVertexSource.h` `012ea9e30a65ef1ba987a08bc1c773336b214d65`. Retains the existing trailing single GPU Mask pointer and adds optional span for additional sources, total max 4; checks independent handles and each bounded input. Each independently GPU-source-expanded 32B Mask draw has its own original source-index/UV/skin and sampled base texture/cutoff, **all IDs forced zero**, combined <=3M vertices and shared true hardware depth. All intermediate buffers stay pinned until async colour/depth callback or real idle.
- **Tests added, NOT RUN:** pure `tests/vivid/human_creator.cpp` `f139cac5ba4db6bc42e38851a42fabc8db17c8ba`, `49ad63cd5f1bb8bcb56a0adcead14b9a03b3cf56`, `eb1626bd0bfeae59c5cb8b71855fb47b87bae54b`, `6064542ccc02a921967e4900de0e79ec4984dec2` covers 4 cap, duplicates/invalid last mask, renderer accepted snapshot and stale/mutated mask material, plus C++ fixture namespace correction. Real `tests/vivid/metal_sculpt_id.mm` `327920cea76751d405c01b3e8b57faf0e10ad872` adds third distinct 60B GPU source and alpha solid/hole native Depth32Float oracle (depth .15 vs .2). `tests/vivid/renderer_preview.cpp` `5a7980efed7c0e4c73bbdc9b24d83d1106d1bc1c`, `25d4b4947ef2f00c6a5e6d9d9907f20514438f6d`, `8df27e5ba3d52039ce1b56b8f7442e678029fb50`, `df2b5de3573e3236a0394e0afe1b78ff8f29520d` adds opt-in `AIVERSE_VIVID_MASK_PARITY=2` real 3-actor Renderer3D main-colour (selected human + two separately Metal-compute-written GPU Mask Models), collector and same-frame ID/depth, with verified-idle retirement. `tools/vivid-validate.sh` `b5bbd3b319f67758ee0bb61c9061a78f99c00c25` requires actual three screenshots and alpha-hole/solid ID/depth results for a new GUI mode while retaining old single/one-mask/crowd modes.
- **Docs:** `docs/vivid.md` `698d699286f183eb0e871ac27f182070c5c742c1`, `tools/vivid-creator/README.md` `cc49143c654d63d68f3355f6769b4c05f3794ad0`. No HumanDNA JSON, face 52 channel ordinal, GPU ModelVertex 60B ABI, MotionWeave ECS or gameplay changes; API additive only.
- **Actual verification performed:** GitHub connector source reads/updates/commit confirmations + **28 static source-contract inspections matched** expected source logic (bounded registered sources, actual colour provenance, exact sampled material, independent GPU multi-mask compute, per-mask hardware depth, GPU scratch lifetime, test fixture/runner). **NOT RUN:** native CMake/C++/ObjC++ build, CTest, Metal shader compile, real GPU colour/ID/depth pixels, GUI one/two-mask/crowd acceptance, `bash tools/vivid-validate.sh --full`, Vulkan/GLES, performance/VRAM or CI on this HEAD. Static inspection does **not** establish PASS on GPU.
- **Next 7BB:** first run and fix consolidated native macOS/Metal tests, especially `AIVERSE_VIVID_MASK_PARITY=2`, original colour/depth vs two sampled Mask occluders and borrowed-resource retirement. Then connect the bounded collector to genuine Creator/editor visible scene candidates where available, add exact GPU-sourced opaque blockers (currently conservative CPU), and expand Vulkan/GLES source ownership. Continue fail-closed on ShaderGraph, Blend/cornea, alternate LOD, MDI/ICB and unknown occluders. Integrate shared Renderer3D edits only on a dedicated semantic worktree with other Texture RHI/batching agents, not a blind main merge.

## 2026-11-20 — combined MotionWeave Studio integration

User-authorized merge `1d8c76e99c924f0373067ba413fac7b5d4803018` preserves VIVID and Studio source histories. See [joint handoff](motionweave-vivid-integration-2026-11-20.md). Native build has GameView API failures; no native/GPU PASS yet. Builds and logs on KINGSTON only.

## 2026-11-20 combined native build checkpoint

Code `4f7d6afd60c962b7a6594aea97f8eef01acc0ce0` on `integration/motionweave-studio-facial-stage184`: Creator and Studio compile; combined runner 12/17 passes with five failures under investigation. Full commands/logs and next steps: [integration handoff](motionweave-vivid-integration-2026-11-20.md). All builds on KINGSTON; no complete acceptance claim.

## 2026-11-20 combined Metal acceptance

Code `647b7bc2e07ab1f99ca567022ac94cfed0d80dec`: fixed reserved MSL tokens and pre-frame actor initialization. 26 distinct focused native tests PASS; real single-actor Renderer3D/MotionWeave colour/ID-depth preview PASS. Detailed commands, limits, logs and next steps: [joint handoff](motionweave-vivid-integration-2026-11-20.md). All artifacts on KINGSTON.

## 2026-11-20 full palette and mask parity

Code `1cd6b732a100b8a01c302f2749b1d0bce2b4ee5e`: all 52 semantic channels + last MotionWeave palette slot tested; real 1/2-mask scene/ID-depth probes, 12-actor scene and Creator smoke PASS on Metal. See [joint handoff](motionweave-vivid-integration-2026-11-20.md) for logs and limits.


## Visual acceptance repairs — 2026-11-20

Implementation: `a16fbb965ad88e6d120592f6c0db5840d12d675b` on
`integration/motionweave-studio-facial-stage184`; starting HEAD
`9340e8c7ffa79db9d47e6a550653f3925a76bc73`.

**Correction of earlier status:** old Creator smoke PASS was insufficient visual
acceptance. Inspection of its actual screenshot showed only hair: CPU actor
borrowed a sparse library from a candidate subsequently moved into the live asset.
Floor/hair draw counts let that failure pass. This entry supersedes earlier claims
of visible Creator head acceptance, but not independent renderer parity fixtures.

Changes: CpuActorStream owns an immutable sparse-library snapshot, bounded with
its fixed vertex payload; source replacement is covered by the CPU actor test.
Creator can select compute before frame 1; `--cpu-morph` explicitly exercises the
fallback on Metal. Smoke requires head attachment AND actual tracked head commands,
logs real draw counts. Procedural facial parenting preserves world TRS rather than
applying head scale twice. Iris is ahead of sclera, inside cornea. High/compact iris
graphs use XYZ length: UV W=1 previously made all pupil/iris masks empty. Camera,
floor and a frontal authoring key reveal the bust and eye glints. Existing MSL
source-contract tests now match vertexIndex/threadIndex from the earlier compiler
fix. Validation script builds CPU actor tests and captures CPU fallback.

Paths: engine/character/vivid/{CpuActorStream.h,HumanPreview.cpp,HumanMaterials.cpp},
tests/vivid/{cpu_actor.cpp,human_appearance.cpp,human_creator.cpp},
tools/vivid-creator/{main.cpp,README.md}, tools/vivid-validate.sh, docs/vivid.md.
Public call signatures, DNA/save format, 52 semantic channels and 64 Model palette
unchanged; CPU actor now owns its morph snapshot (initialization-only copy). No
second scheduler, authoritative ECS edits, renderer shader changes or default
experimental batching/ICB changes.

Actually executed (all build/log/image artifacts under
`/Volumes/KINGSTON/AIVERSE-builds/motionweave-vivid`):
- CMake builds Creator, CPU actor, human appearance and human creator tests: PASS.
  Final logs build-iris-radius.log, build-creator-contract.log, build-creator-light.log.
- Rebuilt aiverse_vivid_renderer_preview and aiverse_studio: PASS,
  build-visual-deliverables.log.
- `TMPDIR=.../tmp bash run-tests.sh --no-build --build-dir .../native --filter
  '^vivid_(cpu_actor|human_appearance|human_rig|human_creator|motionweave_bridge)$'
  --ctest-jobs 1`: **5/5 PASS**, tests-visual-accepted.log,
  native/test-reports/20261120-145600-39306. CPU lifetime test uses fake GLES device;
  real fallback capture below runs on Metal, not native GLES hardware.
- Final Creator `--smoke`: GPU, --cpu-morph, imported OBJ manifest, seven frozen
  optical cases, mobile low config and medium materials: **12/12 PASS**.
  Actual head command counts are 24 procedural / 1 imported. Files/logs in
  visual-acceptance-final/. creator-gpu.png visually inspected: head, facial parts,
  coloured iris and pupils, torso and shadow present; crude procedural test art.
- optical_ab.py with original ROIs, threshold=4, min-changed=1: **3/3 PASS**.
  Skin .10/4.0: 56233 changed pixels; eye IOR 1.25/1.45: 44; hair .08/.90: 2313.
  JSON/logs in visual-acceptance-final/{skin-scatter,eye-ior,hair-rough}.*.
  These prove parameter response in this scene, not physical accuracy.
- Real Metal MotionWeave/VIVID preview: capture-pass=1; morph 81989 changed pixels,
  MotionWeave 63241; both same-frame colour/ID/native depth probes PASS, delta=0.
  visual-acceptance-final/motionweave.log and three PPM captures; animated PNG
  inspected (deliberately simple geometry fixture, not a realistic head).
- bash -n tools/vivid-validate.sh and git diff --check: PASS. Entire branch-fetching
  vivid-validate.sh NOT RUN because it switches to the separate upstream branch.

Recorded failed attempts: build-face-transforms.log initially lacked Animation.h
in appearance test, then fixed. tests-visual-fixes.log and
 tests-visual-fixes-final.log each failed human_creator's stale MSL string checks;
all corrected and rerun above. Original top-lit eye IOR A/B failed (max RGB delta1,
no pixels above threshold), including full viewport diagnostic; final frontal key
passes WITHOUT lowering threshold. Older images remain under visual-acceptance/
for provenance; use visual-acceptance-final/ for final lighting/material captures.

CI NOT DISPATCHED (no URL); no push or main checkout edits. Full engine suite,
Vulkan/GLES hardware, production artwork and Studio WKWebView interaction/screens
NOT VERIFIED. Native Mac UI was locked; user was asked to unlock, no reply received.
CLI framebuffer captures succeeded independently. No synthetic UI screenshot.
Next action: after Mac unlock, inspect Studio Facial/animation windows via CUA,
exercise controls and take UI screenshots; then cross-backend acceptance on real
hosts and production character assets. Do not label the whole VIVID roadmap done.


## Real MetaHuman DNA sample import — 2026-11-20

User explicitly requested a ready internet DNA character and screenshots, then
selected **actual MetaHuman .dna, add compatible import first**, rather than a
substitute glTF character. Code: `af940c85dbfa7967a836e6b300522e34f261b9c3` on
`integration/motionweave-studio-facial-stage184`; parent
`b6c17725d3fd97d24e005c960a5e60977f2bd8f1`.

Source: official Epic MetaHuman-DNA-Calibration revision
`8297ff1bd6f1692a6e221aa8395a66486e5a53c9`, data/dna_files/Ada.dna.
SHA256 `e764cfab938283231bcb3e5af1c7bcbec5154bab4804aafa1b5b7dc6e400ca5e`.
Internal DNA descriptor is "Archtype" (filename is Ada). SDK, original sample,
converted assets, builds and captures live ONLY under
`/Volumes/KINGSTON/AIVERSE-builds/motionweave-vivid/metahuman/`; not vendored.
SOURCE.json records source URL/revision/hash and local SDK license path.
The official repository supports legacy UE <=5.5 DNA, not 5.6+; current general
MetaHuman licensing and the older Calibration license are separate documents.
No rights to redistribute the SDK or sample with AIVERSE are asserted.

Changes: tools/vivid-dna-import/{CMakeLists.txt,decode.cpp,package.py,README.md,
test_package.py} implement a separate authoring adapter linked to an external
user-supplied official DNA reader. Decoder exports selected LOD geometry, all
source target names and optionally one exact target's deltas. Package creates
glTF + VIVID JSON/manifest, expands sparse deltas across UV/normal seams, records
preview center/scale and optional baked weight. No SDK/game runtime coupling.
Creator `--smoke --still` freezes MotionWeave and snaps facial weights for controlled
captures. docs/vivid.md and Creator README document partial support.

Actual native/sample checks:
- External SDK CMake + Apple Silicon Release reader/adapter: PASS,
  import-configure.log, import-build.log, import-build-final.log. SDK has ordinary
  compiler warnings and ranlib empty platform translation units; build succeeded.
- Creator rebuild: PASS, build-still.log. No new Studio build required; adapter is
  offline and --still is standalone Creator-only.
- Real Ada decode LOD0: **9 meshes, 34625 layout vertices**, head lists 681 source
  blendshape targets. Selected exact source target `brow_raiseIn_L` exported.
  Final rebuilt decoder output byte-compares equal to prior successful output.
- `PYTHONDONTWRITEBYTECODE=1 TMPDIR=.../tmp python3 -m unittest discover
  -s tools/vivid-dna-import -p 'test_*.py'`: **4/4 PASS**, package-tests-final.log.
  Cases: UV seam morph expansion, invalid layout rejected without publishing,
  baked+live double deformation rejected, existing output preserved.
- Reader negative tests: invalid binary signature, out-of-range LOD 99 and `0junk`
  all rejected without successful output files (bad-signature.log, bad-lod.log,
  partial-lod.log). Input capped at 512MiB and LOD layout vertices at 1M; this does
  not claim hardened adversarial parsing of the external SDK.
- Real Metal `aiverse_vivid_creator --smoke --still --asset <manifest>`:
  ada-neutral-final, ada-three-quarter, ada-brow-live and ada-brow-cpu
  (`--cpu-morph`) **4/4 capture PASS**; each has .log/.ppm, PNGs of first three.
  Actual accepted head provenance commands=9, opaque/shadow counts=5 after hiding
  untextured helper surfaces. Front and 3/4 PNGs visually inspected: recognizable
  human source geometry, not procedural spheres, no generated replacement wig.
- Explicit diagnostic live route: VIVID browInnerUp -> source brow_raiseIn_L, gain1;
  expressionRest[2]=1. This maps ONE left source target for test, not a faithful
  bilateral ARKit/RigLogic mapping. Neutral vs live: 200 changed viewport pixels
  at RGB-total threshold4. GPU vs CPU: one changed pixel of 586500 (max RGB sum8).
  **Strict bit-exact image equality FAILED**; bounded visual similarity only.
  Both actor paths rendered correctly. morph-parity.json preserves all counts.
- git diff --check PASS. Full engine/Studio/web suites NOT RUN (unchanged runtime
  systems outside Creator capture option); no CI/push, no main checkout changes.

Failed discovery commands: initial unquoted GitHub tree URL hit zsh glob expansion;
quoted retry succeeded. media.githubusercontent.com Ada URL returned 404 (file is
ordinary Git blob); raw.githubusercontent.com pinned URL succeeded. No fabricated
sample was substituted. Earlier first Ada render had opaque helper eye shells and
Creator-generated hair; final package keeps helper geometry but explicitly hides
untextured eyelash/eye-edge/tear/saliva surfaces and tags authored eyelashes so
Creator does not add its procedural wig. Final screenshots are untextured/clay
materials; they do not show original MetaHuman skin/iris textures or groom.

| Subsystem | Status | Remaining |
|---|---|---|
| Legacy binary DNA reader + geometry package | implemented in source; Ada Metal tested | Other DNA versions/axes, broader assets |
| One exact source morph, GPU/CPU routes | partial; Ada tested | Full corrective combinations, normals/tangents, semantic mapping |
| RigLogic, original skeleton and skin weights | not implemented in this adapter | Behavior/joint solver and full expression parity |
| Original textures, eye shading and groom | not implemented in this adapter | Obtain companion licensed assets; DNA alone lacks them |
| Newer UE DNA, Vulkan/GLES, full Studio UI | not tested | Matching SDK and backend/UI hosts |

No VIVID save schema, canonical 52-channel ordering, authoritative ECS state,
shared animation scheduler or GPU ABI changed. Next actionable step: expand the
explicit adapter to original skeleton/skin weights and RigLogic behavior with
real reference-pose tests; obtain companion texture/groom assets separately.
Do not call this complete MetaHuman runtime support or a production character.


## Full Ada companion-character capture — 2026-11-20

User requested a full character because the preceding DNA sample showed only the
head. Code `3699a445d1383fe7048f9fa8afe6443575b77272` on
`integration/motionweave-studio-facial-stage184`, parent
`25d60660c4f17089d8b5c68e57f8338fb7ca29fa`.

Discovery: Epic's old Ada.dna contains no full-body mesh. Its public data/body
folder contains skeleton Maya files only. Found separately published full Ada
export by CreateGameCharacters:
https://creategamecharacters.github.io/metahuman-to-glb/characters/ada/ada.glb
Repository https://github.com/creategamecharacters/metahuman-to-glb
SHA256 `70ad5d190e3cdfb54e88d405b75d7b4731547f2bd93d5cbad0ce4720946d99de`.
This is a separate UE 5.6 companion GLB, NOT a body generated from the previous
legacy DNA file. The user was told this distinction before and after import.

Changes: new tools/vivid-dna-import/prepare_full_character.cjs decodes Draco via
external pinned tooling (@gltf-transform/core + extensions 4.2.1, draco3dgltf 1.5.7),
preserves mesh/skin/texture data, assigns unique duplicate garment bone names and
selects the actual face skin head. A parent frame normalizes the whole figure,
with optional yaw. Published mh_materials.json provides uniform cloth colours,
roughness/metallic fallback because the GLB base-colour input is a UE RGB region
mask. This is not complete original UE cloth-detail shader reconstruction.
Authored hair tags prevent Creator from adding procedural hair. Documentation
updated in adapter README and docs/vivid.md. Creator's demo idle translation now
uses bind + bob instead of absolute zero, preserving nonzero imported head offsets.
No new scheduler, authoritative gameplay, DNA schema or shared renderer edits.

Actual checks (all artifacts in
/Volumes/KINGSTON/AIVERSE-builds/motionweave-vivid/full-character/):
- `cmake --build .../native --target aiverse_vivid_creator -j 8`: PASS, build.log.
- Node preparation front/quarter: PASS, prepare-final.log, prepare-quarter.log.
  node --check script and git diff --check PASS.
- Input/output asset structural checks: preserved **7 meshes, 15 primitives,
  5 skins with unchanged joint-count multiset, 16 texture images**, removed Draco
  requirement. Source has 119916 vertices; native skin-palette splitting gives
  125302 displayed vertices. Garments include shirt, slacks, flats, full body,
  face, eyebrow cards and hair cards.
- `aiverse_vivid_creator --smoke --still --asset <manifest>` final front and ¾
  GPU captures PASS; front with --cpu-morph PASS. Logs/images:
  ada-front-final.{log,ppm,png}, ada-quarter-final.{log,ppm,png},
  ada-cpu-final.{log,ppm}. Actual tracked head/model commands=47,
  shadow=41, colour=41, pass=1, pipelineFailures=0.
- Both final GPU PNGs visually inspected: full head-to-shoe character framed,
  hands/legs/garments/hair visible, body/face remain attached in rest pose.
  Earlier ada-front.png showed RGB clothing masks as red/green/purple colours;
  final uniform author-colour fallback fixes those diagnostic mask colours.
- SOURCE.json records URL, hash and companion-export distinction. SDK/sample and
  npm package-lock/dependencies remain external on KINGSTON, none vendored.

A read attempt at sparse checkout UnrealFest.zip failed because sparse checkout
excluded it; no asset depended on that archive. Full source and final snapshots
are retained externally for reproducibility. No source code from the external
pipeline scripts was executed; only downloaded data and standard pinned decoder
libraries were used by our preparation script.

| Subsystem | Status | Remaining |
|---|---|---|
| Full Ada companion export in VIVID | implemented in source; real Metal captures passed | Broader asset/backend parity |
| Imported head bind translation | implemented in source; full rest-pose capture verified | Comprehensive moving full-rig tests |
| Original UE materials / RigLogic | partial / not implemented | Cloth mask-detail, exact eye/hair shader and facial solver parity |
| Full body animation / other backends | not tested | Actual clips, skinning/retarget reference tests |

Full engine tests, Studio UI screenshots, Vulkan/GLES and complete MetaHuman
animation parity NOT RUN. No CI/push/main checkout changes. Next step: bring
actual body clips and validated facial routing into this full asset, then check
pose/retarget and material fidelity independently. Do not conflate this successful
full-body rest-pose import with full binary body-DNA/RigLogic support.


## Ada companion textures and hair alpha — 2026-11-20

User: "И текстуры". Implementation `e1dc2a63fe65f8519505ff62a4a3163492b24f86`,
branch `integration/motionweave-studio-facial-stage184`, parent
`6aa5aff8037a5a6e231eb2c6aca9346bb56d20ee`.

New bake_materials.cjs + test_bake_materials.cjs under tools/vivid-dna-import;
prepare_full_character.cjs accepts optional source-map directory after material
JSON. Companion textures were obtained from the same published Ada directory:
https://creategamecharacters.github.io/metahuman-to-glb/characters/ada/textures/
Read the published assets/viewer.js as a material-channel reference, did not
execute it. Existing 16 embedded images were copied directly where applicable;
extra maps downloaded via curl with normal certificate checking. Python urllib
attempts failed with local CERTIFICATE_VERIFY_FAILED; no TLS verification bypass.
External sharp@0.34.5 added to the KINGSTON tooling/package-lock.

Offline conversion: cloth RGB region masks now blend authored colours, available
micro/macro detail and AO, with linear-to-sRGB output and a mip-like prefilter for
highly tiled micro detail. Hair/brow/lash source red coverage becomes alpha in
neutral RGB images. Eye R/B iris masks + UV pole/radial ramps become ordinary
colour textures; original atlas transform is removed to avoid double zoom. Skin
cavity and companion teeth colour/normal/mouth-occlusion are attached. Final
package has 29 images and 13 conversion/attachment provenance records. All 16
original image payloads verified byte-identical in output. Source-map SHA256s in
full-character/texture-source-hashes.json. No image/model asset vendored.

HumanMaterials.cpp high/compact hair graphs now use sampled input alpha via
setAlpha rather than overwriting coverage with procedural RGB's constant alpha.
This applies to existing colour and shadow graph evaluation; no renderer edits.
Creator adds --portrait for close inspection of the normalized full-body asset.
README and docs/vivid.md updated. No DNA/save/API/channel ordering, gameplay,
shared scheduler, renderer GPU ABI or default experimental settings changed.

Actually executed, all under /Volumes/KINGSTON/AIVERSE-builds/motionweave-vivid:
- Native Creator + human appearance target build PASS: full-character/build-textures.log.
- Studio rebuild PASS: full-character/build-studio-textures.log.
- Unified run-tests.sh --no-build --build-dir .../native --filter
  '^vivid_human_appearance$' --ctest-jobs 1: 1/1 PASS, including material quality
  budgets. Log full-character/test-textures.log; report
  native/test-reports/20261120-152153-72698.
- node test_bake_materials.cjs <tooling> <KINGSTON tmp>: PASS,
  full-character/test-bake-final.log. Synthetic fixtures deliberately separate R
  coverage from source A; check correct alpha, authored cloth colour/sRGB values,
  colour roundtrip and path traversal rejection. Syntax checks and diff --check PASS.
- Final prepare-textured-filtered.log PASS. Final asset is
  full-character/ada-textured-filtered/manifest.json. Earlier ada-textured-final
  has unfiltered microdetail and is NOT the preferred delivery.
- Real Metal --smoke --still --asset ... with default and --portrait:
  **2/2 PASS**, head/model commands=47, shadow=44, colour=44, pipelineFailures=0.
  full-character/ada-textures-final-{full,portrait}.{log,ppm,png}.
  Both final PNGs viewed: whole figure and close skin/eyes/hair visible, cloth
  moire removed. Intermediate unfiltered captures also succeeded technically but
  showed an aliased large check pattern; prefilter was added and recaptured.

Limitations: offline baked cloth at finite resolution, nearest source sampling;
no view-dependent iris refraction, procedural sclera veins, tangent-atlas hair
anisotropy or full UE skin/scattering parity. Specular glints/card edges remain
visible and are not presented as final production fidelity. Full engine suite,
Vulkan/GLES, complete facial/body rig animation and Studio UI visual parity NOT
RUN. No CI/push/main mutation. Source worktree only intentional untracked web
symlinks remain after commits.

Next: validate original UE reference lighting/materials against native results,
then integrate actual full-body clips and RigLogic-compatible facial routing.
Texture wiring is verified in native Metal captures; photoreal/UE parity is partial.


## Soft hair/lash coverage correction — 2026-11-20

User reported missing alpha on hair/eyelashes after the texture checkpoint.
Implementation: `1f8b8b2852328864aafe8f1a453c3be5defa0be4`, branch
`integration/motionweave-studio-facial-stage184`, parent `f7e932bfb2860c2ad2a7e9f7a6d05a04733c9421`.
Affected: tools/vivid-dna-import/{bake_materials.cjs,prepare_full_character.cjs,
test_bake_materials.cjs,README.md}, docs/vivid.md.

Correction to prior acceptance: PNG coverage packing and successful render
submission did NOT establish correct soft edges. The previous baker forced
MASK at .35 on both hair and lashes, discarding fractional coverage. Re-read
external viewer-reference.js: original uses blended lashes and blended hair
fringe with a depth-writing masked core. Baker now emits BLEND lashes (coverage
texture preferred over albedo), BLEND hair/brows and cloned MASK .5 core
primitives sharing glTF accessors/textures. Invalid coverage channels reject.
Preview groom roughness .55 follows reference viewer floor and reduces pin glints.
No native renderer, DNA schema, API/save/channel ordering, gameplay or scheduler
changes. Additional primitives increase native imported vertices 125302 ->159396;
interchange sharing does NOT currently imply zero additional native GPU memory.

All assets/logs/captures remain external under
`/Volumes/KINGSTON/AIVERSE-builds/motionweave-vivid/full-character/`.
Preferred asset supersedes ada-textured-filtered:
`ada-soft-alpha/manifest.json` (diagnostic-*.json are opaque controls, NOT delivery).
Actual verification:
- node --check baker and test: PASS; git diff --check: PASS.
- node tools/vivid-dna-import/test_bake_materials.cjs <external tooling> <KINGSTON tmp>:
  PASS, test-alpha.log. Fractional R=128 versus A=0 preserved, transparent zero,
  BLEND lash path, shared core accessors/textures + MASK .5, cloth conversion,
  unsafe path rejection. Synthetic test updated to exercise fractional coverage.
- prepare_full_character.cjs <tooling> ada-source.glb ada-soft-alpha 0
  mh_materials.json source-maps: PASS, prepare-soft-alpha.log.
- Existing native Creator --smoke --still --asset ada-soft-alpha/manifest.json,
  with/without --portrait: 2/2 PASS, commands49 shadow43 colour43,
  pipelineFailures0. ada-soft-alpha-{portrait,full}.{log,ppm,png}; both PNGs viewed.
- Separate all-hair/lash OPAQUE control and lash-only OPAQUE control: 2/2 PASS,
  ada-{alpha,lashes}-control-opaque.{log,ppm,png}. Both viewed. Fixed-camera PPM
  RGB comparison (>3/255 max-channel difference): all-opaque hair region
  [620,300,880,480) 24851 changed pixels; lash-only eye region
  [660,475,825,520) 335. Demonstrates coverage affects actual rendered pixels,
  NOT a UE visual parity metric. Opaque control exposes solid card strips.
- Native rebuild/CTest NOT RUN this stage: only offline JS/asset preparation
  changed, existing e1dc2a63 native binary used for actual Metal captures.
  Full suite, moving camera/animation sorting, Vulkan/GLES and Studio UI NOT RUN.
  No CI URL/run, push or main mutation.

Limits: primitive-sorted alpha blending is not order-independent transparency;
intersecting strand/pose sorting still needs multi-angle/animated validation.
Hair tangent-atlas anisotropy and original UE skin/eye lighting parity remain
unfinished; this is a soft-coverage correction, not a photorealism completion.
Next action: validate full-body clips and moving-camera/animated hair layers,
then refine original skin/eye and strand-lighting fidelity. Keep all builds and
artifacts on KINGSTON. Untracked web dist/node_modules symlinks left unchanged.


## Imported appearance profiles and matte correction — 2026-11-20

Implementation fdf3c8d22cede2ca3ff9a382c7f18fdcb081a76a on
`integration/motionweave-studio-facial-stage184`, parent d113d8a5d.
User requested matte clothes, adjustable skin/hair/eyes/textures/tones and body.
Root cause: shirt authored roughness .30; packed skin roughness can approach
zero; old skin/eye DNA controls were not bound to Ada materials.

HumanAppearance.h adds a separate version-1 bounded 72-scalar recipe with
explicit role mapping. HumanMaterials.cpp builds portable <=128-node graphs
over source colour/coverage with explicit roughness; no extra fixed human
highlight lobes. Skin tone/saturation/contrast/exposure/normal/cavity/procedural
freckles/pores; hair melanin/redness/dye/gray/root/ombre/highlights/coverage;
eyes radial iris/pupil/limbus/sclera/detail; cloth roughness/normal/colour;
10 Y-up skin-joint body proportions. Material application is transactional on
fresh models. Body deformation is post-animation and includes garment joints,
never canonical physics/skeleton data. Bounds are bind-pose; animated extremes
and clipping remain unverified. Hair uses sRGB-encoded root/seed in baked R/G
and coverage A, not raw data interpreted as albedo. Existing old companion
packages need regeneration to enable new hair colour controls correctly.

HumanImport manifest accepts optional relative appearance path (old v1 readers
reject this new key). VIVID DNA/52-channel order unchanged. Creator auto-loads
profile and supports --appearance override.json. No new ImGui UI. Next stage
is Studio UI with real native apply/save/load, not yet present in this commit.
Touched engine/character/vivid/{HumanAppearance.h,HumanMaterials.cpp,HumanImport.*},
tools/vivid-creator/main.cpp, tools/vivid-dna-import scripts/README, tests/vivid/
human_appearance.cpp and docs/vivid.md.

Executed on KINGSTON under AIVERSE-builds/motionweave-vivid/full-character:
- build-look-final.log: Creator, appearance/import tests build PASS.
- test-look-final.log: run-tests.sh --no-build --filter
  '^vivid_human_(appearance|import)$' --ctest-jobs 1, 2/2 PASS. Tests cover
  profile roundtrip/range rejection, graph budget/texture+alpha preservation,
  cloth roughness, changed skin roughness, identity body and skin-joint-only
  height change. Earlier compile attempts failed due to an overbroad text
  replacement and test API typos, corrected before final PASS.
- test-look-bake.log: node material bake tests PASS, including root/seed packing.
- prepare-look.log: ada-look-controls/manifest.json and appearance.json produced.
- ada-matte-portrait.{log,ppm,png}: real Metal --smoke --still --portrait PASS,
  commands49/shadow43/colour43/pipelineFailures0. Viewed: matte shirt and softer
  face lighting. Capture predates addition of neutral cloth tint controls only.
- git diff --check PASS. Full suite/CI/push/Vulkan/GLES/animated shape QA NOT RUN.

References read: Epic docs /metahuman/hair-overrides and /skin-material-tools;
external companion viewer-reference.js. This is an implemented portable subset,
not all MH controls: no original texture-index library, authored accent masks,
geometry hair grooming, corneal refraction or complete UE scattering parity.
Next: Studio native profile UI, final contrasting look/body frames and persistence
checks. All build outputs/assets/logs/screens remain on KINGSTON.


## Studio appearance panel and rigid-groom body follow — 2026-11-20

Implementation `994e7c7b41b24916121ceeaefe65f25ea4626cef`, branch
`integration/motionweave-studio-facial-stage184`, parent handoff `77c66ef8e`;
preceding native appearance implementation `fdf3c8d22cede2ca3ff9a382c7f18fdcb081a76a`.

Implemented a dockable Studio UI v1 VIVID Appearance panel (generated through
new_panel.py), 72 native-schema controls: skin19, hair20, lashes2, eyes14, cloth7,
body10. Draft undo/redo/reset, native apply, role assignments, reading a local
recipe, loading sibling appearance.json by checked model GUID, native named
save/load in project/human-looks. Explicit typed methods humanAppearanceInfo,
Apply, Save, Load, List, FromAsset; no arbitrary filesystem/shell bridge and no
browser project-file writes. Native errors/readiness shown separately from drafts.
Pure web validator rejects malformed/out-of-range profile input before rendering
number controls. The native validator remains authoritative.

StudioFacialPreviewBinding keeps a same-model MotionWeave rig while applying
appearance. A new appearance-only mode works without a fabricated facial rig.
StudioFacialGpuPreview applies materials before publishPrepared; fresh generation
ownership, read lease and GPU retirement preserved. Body field runs on the
post-animation presentation matrices. Still one Animator; no simulation, save-
state, GPU ABI, shared Renderer3D shader or batching default changes. CMake links
Studio/GPU test to existing vivid_human. A separate native session integration
fixture checks actual dispatch/load/apply/save/read/stale and symlink rejection.
Project/asset switch clears unrelated appearance. Profile schema/DNA unchanged.

Visual regression discovered and fixed: skin joints followed changed proportions,
but rigid hair/brows did not. applyHumanBody now also warps unskinned mesh-owner
matrices about each mesh's transformed centre (not its often foot-level origin).
Skinned mesh owner nodes remain untouched to avoid double deformation. Added a
rigid attachment numerical regression. Invalid body bounds are caught by both
native hosts instead of escaping the frame loop. Intermediate
ada-look-variant-full is BAD (bald/displaced groom); use fixed-full below.

Affected files: apps/studio-native/{CMakeLists.txt,StudioSession.cpp,
StudioFacialPreviewBinding.h,StudioHumanLookDocument.h,mac/StudioFacialGpuPreview.*,
tests/StudioFacialGpuTests.mm,tests/StudioFacialPreviewBindingTests.cpp,
tests/StudioHumanLookSessionTests.cpp}; apps/studio-web package, registry/types,
generated/HumanAppearancePanel TSX/CSS, humanLookSchema and tests; VIVID
HumanMaterials.cpp, appearance test, Creator main; docs/vivid.md and new
vivid-appearance-controls.md. See git show for exact full paths.

Verification, all builds/caches/assets/logs on KINGSTON at
`/Volumes/KINGSTON/AIVERSE-builds/motionweave-vivid/`:
- Final native app/Creator/GPU target build PASS: full-character/
  build-look-publish-final.log. Previous build-studio-look-final.log also built
  the app and new session target; build-look-session-test.log final fixture.
- Unified run-tests.sh --no-build --build-dir .../native --filter
  '^studio_facial_(preview_binding|gpu)$' --ctest-jobs 1: 2/2 PASS,
  test-studio-look.log. Binding tests include named persistence, path rejection,
  stale controls, same-model rig preservation and appearance-only binding.
- '--filter ^(vivid_human_appearance|studio_facial_gpu)$': 2/2 PASS after rigid
  attachment fix, test-look-rigid-fix.log, report 20261120-154720-98436.
- '--filter ^studio_human_appearance_session$': 1/1 PASS,
  test-look-session-final.log, report 20261120-155139-9875. Initial fixture failed
  because empty gameplay rules had no types; corrected fixture, not production.
- Final '--filter ^studio_facial_gpu$': 1/1 PASS after frame error handling,
  test-look-publish-final.log, report 20261120-155407-14665. Real Metal shader
  submission tests use a small fixture, not a claim of full Ada Studio UI QA.
- npm run typecheck PASS; npm run test:web 75/75 PASS (including 2 profile import
  tests). web-look-checks.log records passing tests plus an initial failed
  `tsc -b --tsBuildInfoFile` attempt (TS5094). Corrected to `npx tsc --incremental
  --tsBuildInfoFile <KINGSTON>/web-look.tsbuildinfo` and `npx vite build`: PASS.
  Final web-look-final-build.log PASS; bundle copied into native app and verified
  present. Existing dist symlink targets KINGSTON. First npm attempt used repo
  root and failed ENOENT; corrected explicit apps/studio-web workdir. Dock tests
  initially exposed generator not adding the default right-stack entry, then
  old two-tab assumptions; registration and fixtures updated, all pass.
- Python UI contracts: 29/29 PASS, test-studio-contracts-look-final.log, using
  PYTHONPYCACHEPREFIX on KINGSTON. git diff --check PASS.
- Real Creator Metal --smoke --still --asset ada-look-controls/manifest.json:
  ada-matte-final-portrait.{log,ppm,png} and --appearance look-variation.json
  ada-look-variant-fixed-full.{log,ppm,png}, ada-look-variant-portrait.*.
  All PASS and viewed, pipelineFailures0; matte cloth/skin, colour changes,
  changed body and following groom observed. Pixel frames precede only the
  final error-catching wrappers, which do not change valid rendering.
- UI tool reports Mac locked. User explicitly replied 'Пока проверяй без
  интерфейса'. WKWebView desktop/240px interaction/screenshots NOT RUN; no claim
  of manual UI acceptance. verify_mac.sh not run literally because it hardcodes
  a repo-local build directory; relevant builds/tests performed at KINGSTON.
  Full engine suite, animation-wide body/clothing/culling checks and Vulkan/GLES
  NOT RUN. No CI/push/main mutation.

Ready external sample: KINGSTON root `VIVID-Look-Lab/content/Ada` with source
companion, `human-looks/Ada-Matte.json`, `Ada-Variation.json`, and launcher
`Open-VIVID-Look-Lab.command`. Project was prepared, not opened interactively.
Choose Panels > VIVID Appearance, select Ada/model.gltf, Load model profile,
Apply to preview. The profile can also be passed to Creator --appearance.
Source assets remain external and are not committed or published.

Limitations/next: perform unlocked Studio interaction and 240px layout check;
validate multiple poses/angles and extreme garment deformations. Body warp is a
bounded Y-up presentation approximation, not anatomical morphs/physics. Hair
colour controls require packed root/seed attributes and do not regenerate hair
geometry. No MetaHuman face texture-index library/region masks, tangent-atlas
anisotropy or full cornea/SSS parity. Control reference documents this explicitly.
Only pre-existing dist/node_modules symlinks should remain untracked after commit.

## Wardrobe prerequisite: preserve facial export — 2026-11-20

Implementation `4a7f0a3da4c54c210ac75450413ed6f165a54b2a`, branch `integration/motionweave-studio-facial-stage184`.
Updated tools/modeling/Export.cpp and tests/modeling/modeling.cpp: GLB now exports
position/normal morph deltas, semantic targetNames, mesh/node weights and SCALAR
weight-animation channels. Previously morphs were silently lost and weight clips
were serialized as rotation. CPU deltas must exist; missing data fails clearly.
No save schema/runtime/renderer change. Wardrobe, body masks, baking and Studio
assembly button are still outstanding; this is a prerequisite, not completed UI.
Verified cmake build target aiverse_modeling_tests and run-tests.sh --no-build
--filter '^modeling_authoring$' --ctest-jobs 1: 1/1 PASS including skinned morph
roundtrip. All builds/logs on KINGSTON: motionweave-vivid/full-character/
{build,test}-wardrobe-export.log; report native/test-reports/20261120-155942-27803.
git diff --check PASS. Full regression/UI/other platforms NOT RUN. No CI/push.
Next: native wardrobe recipe, masking and bake pipeline, then typed Studio UI.

## Wardrobe CPU assembly and bake — 2026-11-20

Code `3bad4c000730c7bbc51aedbe2664462c6132a639`, branch integration/motionweave-studio-facial-stage184.
HumanWardrobe v1 composes imported CPU models: selectable source meshes, one
enabled item per slot, rigid bone sockets/TRS, skinned clothes with exact bone-name
and bind-world checks. Per-item UV0 red-channel visibility masks multiply base
alpha; black hides. Masks remain texture cutouts (no unsafe triangle deletion).
Optional cloth finish uses existing appearance controls; source finish preserves
jewelry PBR. Body deformation is baked in bind-space vertices/morph vectors with
translated joint pivots/inverse binds and offset translation animation keys. It is
an approximation for upright characters, not pose-wide anatomical retargeting.
HumanAppearanceBake executes the same private DAG, bakes linear colour/alpha to
sRGB textures (16..4096 max edge), stores roughness/normal/AO factors, removes live
graphs. CPU/GPU procedural-noise floating-point differences possible. Export also
preserves COLOR_0 and base KHR_texture_transform. Final package removes unused
meshes/materials/images, keeps separate skin/material primitives in one asset.
No automatic clothing fitting or physics; external skinned clothes must match rig.
No UI/button or filesystem publication yet. Next stage: safe native async build,
Studio garment/mask controls, actual Ada package roundtrip and native captures.

Verification: KINGSTON full-character/build-wardrobe-core-final.log and
test-wardrobe-core.log: modeling_authoring + vivid_human_wardrobe +
vivid_human_appearance 3/3 PASS (report 20261120-160550-42160). Final core changes
(baseline body bounds, idempotent hides, optional cloth finish) rebuilt and
vivid_human_wardrobe 1/1 PASS, build/test-wardrobe-core-last.log, report
20261120-160628-42693. New fixture covers mask alpha, socket attachment, hidden
mesh removal, facial/skinning export/reimport, duplicate IDs/missing socket,
cloth roughness/alpha. Initial make invocation could not select newly generated
target in same command after CMake regeneration; next invocation passed.
Full suite/multi-pose body and actual Ada/UI NOT RUN yet. No CI/push.

## Studio wardrobe and safe character publication — 2026-11-20

Implementation `9ade6c85d3f5411bf8bf1fc1f04977598118dda6`, branch `integration/motionweave-studio-facial-stage184`;
prerequisites 4a7f0a3da (morph export) and 3bad4c000 (CPU wardrobe/bake).

Added Studio UI v1 wardrobe controls within VIVID Appearance: real source mesh
visibility, model GUID pickers, slots, enabled items, cloth/source PBR finish,
bone sockets and local TRS, per-item source mesh hiding and body mask texture
GUIDs/material names. Draft undo, native sibling wardrobe.json preset loading,
resolution/name controls, Russian Собрать персонажа button, async status/error,
and loading the completed GLB through existing checked preview binding. There
is NO live per-item wardrobe preview; display the assembled output. No JS project
file writes or new ImGui controls. StudioField labels get actual input IDs.

Native typed methods: humanWardrobeInfo, humanWardrobeFromAsset,
humanCharacterBuild, humanCharacterBuildStatus. A worker captures validated
source/model/mask selections; native FileReader restricts reads to nonsymlink
content paths. Output is content/Characters/name/{model.glb,source.json,
appearance.json}. Exclusive staging, fsync and macOS RENAME_EXCL publish avoid
source/previous-output replacement. Reimport and mesh/skin/morph-name checks
precede publication. The live output appearance is empty to avoid double baking;
source.json retains the editable request. 128 MiB output/import cap; the standalone
Creator cap increased from 64 to 128 MiB to match Studio. aiverse_vivid_assemble
uses the SAME publisher for headless acceptance (content paths instead of GUIDs).
Wardrobe presets normalize optional TRS/finish defaults before returning to TS.

Important body regression found by actual Ada screenshots, despite unit tests:
packed UNORM skin weights sum to 254/256 on some vertices; GPU normalizes by
actual sum. Dividing by 255 during bake produced up to .01556 model-unit error
and severe face/hand artifacts. Fixed by normalized weighted bind/warped matrices,
inverting each vertex's weighted bind, and matching GPU linear skin normals.
Source vertices are snapshotted and only referenced primitive vertices are baked,
preventing repeated/shared-range deformation. Public bakeHumanBody plus tests
cover shared primitives and non-255 weight sums. Body translation keys/pivots
are adjusted; pose-wide anatomical retargeting remains unverified.

Baked Creator manifests optionally support `authoredHair:true` (v1 additive,
false by default), suppressing an extra generated wig WITHOUT replacing baked
PBR. Strict boolean parsing and roundtrip test added. Earlier captures with empty
material roles generated a second wig. Native Studio raw GLB never used that
Creator-only generation path. No core renderer/Metal batching changes.

Affected paths: apps/studio-native CMake/Session/header/StudioHumanCharacterBuild
and session tests; studio-web types and HumanWardrobeControls/appearance panel;
HumanImport, HumanWardrobe, their tests; tools/vivid-creator main/assemble and
wardrobe_fixture.py; docs/vivid.md and vivid-character-assembly.md. No external
MetaHuman/companion data is committed. Procedural fixture accessories are original.

Verification: ALL builds, assets, logs, packages and captures on KINGSTON under
/Volumes/KINGSTON/AIVERSE-builds/motionweave-vivid/. Logs below are full-character/.
- cmake --build native targets aiverse_studio, aiverse_vivid_creator,
  aiverse_studio_human_appearance_session_tests, aiverse_vivid_human_wardrobe_tests,
  aiverse_vivid_human_import_tests, aiverse_vivid_assemble: PASS across scoped builds;
  final build-wardrobe-complete.log. Final app contains built web dist.
- Unified run-tests.sh --no-build --build-dir <KINGSTON>/native --ctest-jobs 1
  filter '^(studio_human_appearance_session|vivid_human_import|vivid_human_wardrobe|
  vivid_human_appearance|modeling_authoring)$': 5/5 PASS, test-wardrobe-final.log,
  report 20261120-161832-58357 (before final body normalization).
- After body/preset fixes, filter '^(studio_human_appearance_session|
  vivid_human_wardrobe|vivid_human_import)$': 3/3 PASS, test-wardrobe-complete.log,
  report 20261120-163045-80190. Earlier shared-vertex and weight regressions also
  PASS in test-body-shared-regression.log and test-body-weight-normalization.log.
  Session tests exercise actual async dispatch, imported defaults/GUID resolution,
  output/recipe creation, duplicate-name failure without partial output, bad GUID.
- npm run typecheck PASS; npm run test:web 75/75 PASS; npx tsc --incremental
  --tsBuildInfoFile <KINGSTON>/web-wardrobe.tsbuildinfo && npx vite build PASS
  (tool output, bundle index-BHaoxO61.js). Initial typecheck caught missing
  StudioField htmlFor and 'texture' instead of AssetKind 'image'; corrected.
- PYTHONPYCACHEPREFIX=<KINGSTON>/python-cache python3 -m unittest discover
  -s tools/studio -p 'test_*.py' -v: 29/29 PASS, test-wardrobe-ui-contracts.log.
- Actual Ada builds: Characters/Ada-Wardrobe-Ready/model.glb (160971 vertices,
  9 meshes,22 materials,89316788 bytes); Characters/Ada-Baked-Body-Normalized/
  model.glb (159396 vertices,7 meshes,17 materials,88986864 bytes). Companion
  preset includes separate BlueShirt, SilverWatchBand/dial, GoldChain and
  BodyVisibility.png. Imported garment is a same-rig shirt variant, not proof
  of automatic fitting. Bodies/masks/accessories are all in the final GLB.
- --compare-body PROJECT Ada/model.gltf Characters/Ada-Baked-Body-Normalized/
  model.glb look-variation.json: PASS, compare-ada-body-normalized.log;
  225900 samples, maxPositionError=2.38814e-06, RMS=4.21594e-07,
  minNormalDot=.999165 against live applyHumanBody with GPU-normalized weights.
- Real Metal Creator --smoke --still captures inspected: ada-wardrobe-ready.png
  (shadow48/colour48, pipelineFailures0) and ada-baked-body-normalized.png
  (shadow43/colour43, pipelineFailures0). These are Creator framebuffer captures
  of assembled GLBs, NOT screenshots of the new Studio controls.
- BAD intermediates: Ada-Baked-Variation, Ada-Baked-Body-Fixed (marked DO-NOT-USE),
  ada-baked-variation*.png and ada-baked-body-fixed.png retain dents from the
  pre-normalization implementation. Do not deliver as passing. Initial 93 MiB
  Creator load failed at its old 64 MiB limit. A diagnostic AIVERSE_SHADOWS=off
  did not override Creator's explicit Medium setting (shadow43): NOT a valid
  no-shadow comparison. Temporary override code was removed. Final normal
  shadows are enabled; no rendering defect was hidden by disabling shadows.
- git diff --check PASS. No CI/push/main mutation. Full engine/Vulkan/GLES,
  manual WKWebView 1440/240px and multi-pose wardrobe acceptance NOT RUN. User
  explicitly authorized checking without UI while Mac is locked; no repeated
  unlock/approval request. verify_mac.sh not run verbatim because it hardcodes
  repo-local build output; equivalent scoped builds use KINGSTON.

Ready launch: KINGSTON Open-VIVID-Look-Lab.command. In VIVID Appearance select
Ada/model.gltf, Load model profile, Загрузить гардероб модели, choose a NEW name,
Собрать персонажа, then Показать собранного персонажа. START-HERE.txt updated.
Only existing untracked dist/node_modules symlinks should remain in worktree.
Next: unlocked Studio interaction/narrow dock checks, animation/morph-pose
wardrobe and body tests; live garment preview, LOD/atlas optimization and
automatic fitting are separate unfinished features. Preserve all source assets.

## Face identity deformation core — 2026-11-20

Code `be6bfcb2e5a837d073c056cb13416d1c07cac782`, branch integration/motionweave-studio-facial-stage184.
HumanFaceShape defines strict v1 selected-mesh identity recipe, 18 bounded
region controls and <=256 normalized XYZ landmark displacements. CPU candidate
geometry uses GPU-normalized bind skin weights; UVs/topology/skin ownership stay
unchanged. Morph direction vectors follow the deformation Jacobian. It changes
identity shape, not the fixed 52 expression semantics or authoritative ECS.
A coarse aligned-scan nearest-vertex correspondence helper produces <=128
landmarks; it is NOT automatic registration, dense surface reconstruction or
MetaHuman RigLogic. Front-photo XY landmark displacement preserves template
depth. No photographic texture extraction, landmark detector or UI implemented
yet. Face bones are not anatomically refitted. Models must be Y-up, +Z forward.

User explicitly expanded request to include scan/photo creation, not only
separate head/body import. Clarification answered 'Также создание по скану/фото'.
Explained planned template fitting and preservation of unseen photo geometry.
Official references researched: Epic metahuman-creator-from-dna-tool-in-unreal-engine,
metahuman-creator-import-tools-in-unreal-engine, head-controls. These describe
separate DNA/template/Identity flows; do not claim full Unreal feature parity.
Current DNA adapter still legacy head diagnostic only; its limitations remain.

Verification: cmake builds aiverse_vivid_human and aiverse_vivid_face_shape_tests
PASS. Unified run-tests.sh --no-build --build-dir <KINGSTON>/native --filter
'^vivid_face_shape$' --ctest-jobs 1 PASS. Logs full-character/build-face-shape-
{core,test}.log and test-face-shape-core.log on KINGSTON. Fixture verifies
recipe validation, neutral identity, width, unchanged topology, photo depth,
scan correspondences. No UI/actual Ada visual/animated-pose acceptance yet.
No CI/push. Next: Studio native preview/build persistence, photo landmark UI,
scan fitting, and separate head/body import with rig-safe attachment.


## 2026-11-20 — Studio facial identity authoring, scan/photo reference and parts import

Code: `ff1f660e60742cfc67c7a724c73e892daf0a1e27`, branch
`integration/motionweave-studio-facial-stage184`; preceding core `be6bfcb2e`.
Affected: Studio native binding/session/GPU/build helper, Studio shared-UI
HumanFaceControls and appearance/wardrobe integration, HumanWardrobe independent
rig merge, assembly CLI, native fixtures, docs/vivid-face-authoring.md.

Implemented 18 facial controls, manual local reference-photo points (front XY /
profile ZY), aligned-scan coarse nearest-vertex fit, independent head/body model
merge preserving separate rigs/clips/morphs/materials. Socket inverse TRS chain
retains imported world bind frame while following body. Face applies to fresh
native preview CPU geometry and final assembly before body deformation; source.json
persists recipe. Read-only file import restores recipe against original source.
Native methods humanFaceFitScan and humanHeadBodyImport share existing single
async build status. Optional face field is additive, old appearance/wardrobe callers
remain valid; wardrobe independentRig defaults false. No renderer core changes,
no automatic binary DNA compatibility expansion, no browser project writes.

All outputs on `/Volumes/KINGSTON/AIVERSE-builds/motionweave-vivid`.
Executed verification:
- CMake targets aiverse_studio_human_appearance_session_tests,
  aiverse_vivid_human_wardrobe_tests, aiverse_vivid_assemble, aiverse_studio,
  aiverse_studio_facial_gpu_tests, aiverse_studio_facial_preview_binding_tests PASS.
- Initial make target AiverseStudioHTML5 failed (bundle name, not target); reran
  correct aiverse_studio. Initial binding link failed missing vivid_human; fixed
  target dependency. Initial wardrobe test fixture declared morph name without
  channels; fixed fixture. Logs build-face-integration.log/build-face-studio.log
  retain these failures, later build-face-final-tests.log PASS.
- run-tests.sh --no-build --build-dir <KINGSTON>/native --filter
  '^(vivid_face_shape|vivid_human_wardrobe|studio_human_appearance_session|studio_facial_preview_binding|studio_facial_gpu)$'
  --ctest-jobs 1 PASS 5/5; report 20261120-164640-3967.
  Additional translated/nonuniform-scaled socket fixture PASS, report
  20261120-164734-4623. Session tests execute real async face bake/scan/parts import.
- npm run typecheck, npm run test:web (75 tests), npx tsc --incremental
  --tsBuildInfoFile <KINGSTON>/web-face.tsbuildinfo, npx vite build PASS.
  After final recipe restoration UI, typecheck + vite repeated PASS.
  One initial npm invocation at repo root failed ENOENT; reran in studio-web.
- PYTHONPYCACHEPREFIX=<KINGSTON>/python-cache python3 -m unittest discover
  -s tools/studio -p 'test_*.py' -v PASS 29/29.
- Actual Ada build using optional assembly CLI FACE_JSON: Ada-Face-Edited,
  159396 vertices, 7 meshes, 17 materials, 95831620 bytes, self-contained GLB.
  Recipe full-character/face-ada-test.json (jawWidth .12, cheekWidth .07,
  chinForward .03, noseWidth -.08). Native Creator --smoke --still --portrait
  capture PASS, Metal pipelineFailures=0, pass=1. Inspected
  full-character/ada-face-edited.png and control ada-face-baseline.png.
  Eye/eyelash edge artifacts exist in BOTH old undeformed wardrobe and new face
  preview; NOT clean portrait visual acceptance. No shadows disabled to hide them.
- Latest web dist copied into KINGSTON AiverseStudioHTML5.app Resources/studio-web.
  git diff --check PASS. No CI/push. User authorized tests without locked Mac UI;
  manual WKWebView/narrow panel interaction NOT RUN. Creator image is NOT a Studio
  panel screenshot. Full animated-pose/foreign head asset acceptance NOT RUN.

Limits: photo tool is MANUAL template guidance, not automatic reconstruction,
photogrammetry, detected landmarks or texture extraction. Scan requires prior
alignment/units; coarse nearest vertex, not ICP. No anatomical bone refit, neck
weld, automatic retarget or independent-clip synchronization. Existing MetaHuman
binary DNA importer remains legacy head diagnostic, not full modern body/RigLogic.
No user-specific scan/photo supplied. New tools implement a bounded stage of the
broader request, not full Mesh to MetaHuman parity. Source recipes must NOT be
applied to already baked assets. Photo reference itself is not persisted.
Next: fix existing close-up eye/eyelash rendering; unlocked panel QA; validate
real independent head/body animated assets; automatic scan registration and
photo reconstruction remain separate unfinished work. Keep original source data.


## 2026-11-20 — Eight studio lighting rigs and curved cyclorama

Code commit `77e23959c1dcf7157f635c5d9b432c8ed65d7bd6`, branch
`integration/motionweave-studio-facial-stage184`. User requested 8 Unreal-style
lighting presets, multiple sources, curved backdrop. Shared presentation header
`engine/character/vivid/HumanStudioLighting.h` defines eight original rigs:
neutral, portrait, warm, cool, dramatic, rim, gel, catalog. Four sources each:
shadowed directional key plus three High-tier point sources, hemisphere ambient.
64-segment smooth quarter-circle joins floor and wall (134 vertices/396 indices).
Cyclorama and source layout scale/translate to character world bind bounds.
No engine renderer/shader/batching policy changes or experimental flags enabled.

Studio native facial binding owns preview-only settings; validated typed
`studio.humanStudioLighting` gets catalog/settings and optionally sets the whole
settings object atomically. Invalid presets/ranges/types reject without publication.
Light changes do not increment binding revision or reload character GPU assets.
Native Metal Scene owns/releases one immutable unit cyclorama mesh. Facial mode
uses Medium sun shadows and High local lights; map/material modes retain previous
Off shadows/Medium local tier. No map light document or baked texture mutation.
Shared Studio UI component exposes 8 choices, rig yaw [-180,180], gain [.25,2],
backdrop toggle and reset. Settings remain until project/binding reset, not on disk.
Creator CLI `--studio-lighting ID` uses the SAME rigs/geometry for captures;
without option its legacy optical lighting/floor is retained. No new ImGui controls.

Verification actually executed, all outputs on KINGSTON:
- CMake targets aiverse_studio, aiverse_vivid_creator,
  aiverse_studio_human_appearance_session_tests PASS. Initial aggregate braces
  error in new header failed compile; fixed. Original build-studio-lighting.log
  retains failure; build-studio-lighting-retry.log final success.
- run-tests.sh --no-build --build-dir <KINGSTON>/native --filter
  '^studio_human_appearance_session$' --ctest-jobs 1 PASS. Report
  native/test-reports/20261120-165318-10617; test-studio-lighting.log.
  Checks 8 catalog entries, selection, invalid selection atomicity, cyclorama
  vertex/index counts and every triangle winding against smooth normals.
- npm run typecheck, npm run test:web (75), npx vite build PASS.
  dist is KINGSTON symlink; latest dist copied into native app Resources/studio-web.
- PYTHONPYCACHEPREFIX=<KINGSTON>/python-cache python3 -m unittest discover
  -s tools/studio -p 'test_*.py' -v PASS29; test-lighting-ui-contracts.log.
- Actual native Creator --smoke --still --studio-lighting <ID> with the SAME
  external Ada-Face-Edited asset/camera/pose for all8: PASS8/8, each log has
  pipelineFailures=0 and pass=1. All eight PNGs visually inspected: visible
  distinct light tint/contrast, curved continuous ground/background and sun shadow.
  full-character/light-{neutral,portrait,warm,cool,dramatic,rim,gel,catalog}.{png,log,ppm}
  under /Volumes/KINGSTON/AIVERSE-builds/motionweave-vivid.
  full-character/lighting-gallery.html links all actual PNGs; no generated/fake
  character images. Left overlay is legacy Creator, NOT new Studio control UI.
- git diff --check PASS. No CI/push. Manual WKWebView/narrow dock screenshots
  NOT RUN: user explicitly asked to keep testing without locked desktop UI.
  Vulkan/GLES and dynamic eight-preset switching visual acceptance NOT RUN.

Limits: original AIVERSE rigs, not copied Epic assets or UE render parity.
Current point lights have no individual shadow maps; no area lights/Lumen/ray
tracing added. Existing eye/eyelash and occasional shadow-edge artifacts remain;
lighting stage does not repair imported material anatomy. Cyclorama is oriented
behind +Z-facing character and fixed in world; rig yaw rotates sources only.
No scene export or persistent project light-preset file. Next: unlocked Studio
controls QA, orbit/rotation acceptance and existing close-up eye material fix.
Build/launch remains KINGSTON/Open-VIVID-Look-Lab.command. No source assets staged.


## 2026-11-20 — Visual QA: hair and eyelashes FAIL

Report commit `23df5d50b490d6926d5a0e0228827c12d17c475c` on
`integration/motionweave-studio-facial-stage184`; no product/source asset changes.
Read docs/qa/vivid-hair-lashes-2026-11-20.md before further appearance work.
Actual existing Creator binary hash/version and asset hash captured in
KINGSTON/full-character/hair-lashes-qa-20261120/environment.json.
Three new actual Metal portraits: catalog, neutral, rim; all renderer logs
pipelineFailures=0/pass=1, but eyelashes visually FAIL3/3 (dark jagged bands),
hair silhouette FAIL especially against light background (hard card fragments).
Critical new evidence: source Ada/baseColor_8.png AND assembled GLB eyelash
baseColor texture each contain1048576 alpha255 pixels, zero transparent/partial.
BLEND mode survives, but meaningful lash coverage is ALREADY LOST IN SOURCE
companion texture. Do not claim alpha correctness from material mode alone.
Hair alpha DOES survive:676354 alpha0,306724 partial,65498 alpha255 (1024²)
for both BLEND fringe and MASK core. Hair defects therefore need separate
coverage/filter/sorting investigation; not established as alpha loss.
Eye shell/edge grey artifacts also seen; origin not isolated.

Executed:3 CLI captures + sips PNG conversion + read-only GLB/PNG alpha analysis
using standard-library PNG unfilter/zlib. No build/tests/CI/push. Pillow missing
in default Python3.8 and3.12; no dependencies installed. Report2 PASS technical
checks,3 FAIL,3 NOT RUN. Animation/blink/orbit/Studio UI NOT RUN (user authorized
without locked UI). No retouching, UI simulation or fabricated screenshots.
Evidence/report under /Volumes/KINGSTON/AIVERSE-builds/motionweave-vivid/
full-character/hair-lashes-qa-20261120/. Next: investigate companion eyelash
opacity extraction first; restore texture coverage, then isolate eyeshell/edge
and hair core/fringe quality with matched A/B and actual blink/orbit validation.


## 2026-11-20 — Default facial idle and four preview poses

Code `8c71909c5f9e5f69b3b92fae4ad3dddbf7a6458c`, branch
`integration/motionweave-studio-facial-stage184`. User requested default facial
idle and poses applied. HumanPreviewAnimation.h generates preview-only six-second
clips: relaxed(default), A-pose, T-pose, portrait × face on/off. Semantic upperarm/
lowerarm targeting converts world direction into parent bind rotation, updating
matching garment/body skeletons; numeric suffixes/namespaces are recognized.
Small neck/spine motions, blink peaks1.7/4.4s, subtle smile/brow morphs. Uses current
native64 morph capacity, not outdated8-channel restrictions in older AGENTS text.
No import/source edits, rig schema/channel reordering or renderer shader changes.
One existing Studio Animator remains owner; default play/speed1/face enabled,
0.25s pose fade, immediate change while paused. Existing manual facial layer runs
afterwards. Source clips retained but default preview selects generated clips.
Native validated studio.humanPreviewAnimation settings are session-only; no export
or baked animation mutation. Shared Studio UI exposes pose, face toggle, speed,
pause and reset. See docs/vivid-preview-animation.md.

Creator opt-in --preview-pose uses its existing MotionWeave graph. Diagnostic
--preview-time samples a separate pose copy without modifying canonical ECS pose.
IMPORTANT: initial screenshots face-open.png/face-blink.png DID NOT close eyes:
Creator's sparse GPU actor override suppresses the imported Model morph texture.
Fixed preview-pose mode to use ModelDraw.pose through the same standard morph
renderer path as Studio, without actor vertex override. Legacy actor modes remain.
Final capture logs identify actor=MODEL_MORPH. Do NOT cite the early snapshots as
blink acceptance. No second animation scheduler added.

Also corrected prior Studio light/cyclorama positioning to match character preview
normalization (max bind extent→2), rather than raw asset units. No viewport layering
or camera/UI geometry changes. Runtime source model/wardrobe assets unchanged.

Actually executed, outputs under /Volumes/KINGSTON/AIVERSE-builds/motionweave-vivid:
- CMake aiverse_studio, aiverse_vivid_creator,
  aiverse_studio_human_appearance_session_tests, aiverse_studio_facial_gpu_tests PASS.
  build-idle-poses.log, build-idle-checks.log, build-idle-capture-fix.log.
  One Python edit invoked from web directory failed FileNotFoundError before any
  write; rerun at explicit worktree root succeeded. No failed native compile.
- run-tests.sh --no-build --build-dir <KINGSTON>/native --filter
  '^(studio_human_appearance_session|studio_facial_gpu)$' --ctest-jobs 1 PASS2/2;
  native/test-reports/20261120-170617-19019, full-character/test-idle-poses.log.
  Fixture tests default enabled state, invalid settings atomicity, paired blink,
  relaxed/T target directions, source immutability, loop endpoint and face-off.
  Existing real Metal GPU fixture passes with default preview clip integration.
- npm run typecheck && npm run test:web (75) && npx vite build PASS. After final
  clothing-intersection hint, typecheck/vite repeated PASS; latest dist copied
  into native app Resources/studio-web on KINGSTON.
- Python UI contracts using KINGSTON PYTHONPYCACHEPREFIX PASS29/29,
  full-character/test-idle-ui.log. git diff --check PASS.
- Actual Creator fixed-time native renders visually inspected:
  full-character/idle-poses/{relaxed-native,a_pose-native,t_pose-native,
  face-open-native,face-blink-native}.png. All five pipelineFailures=0,
  pass=1, actor=MODEL_MORPH. Open t0 vs blink t1.7 clearly shows closed eyelids.
  Default relaxed/A-pose garment placement acceptable in these static front views.
  T-pose exposes shoulder skin through shirt: NOT clean wardrobe visual acceptance.
  Portrait inherited opaque lashes/eye-edge artifacts remain. Gallery.html links
  all final snapshots (lowercase filename gallery.html). These are native Creator
  scene snapshots, NOT screenshots of Studio UI or a real-time video.
- UI interaction, continuous playback pacing, other models/rig conventions,
  cross-platform GPU, full arbitrary animation/clothing collision NOT RUN.
  User still authorizes testing without locked desktop UI. No CI/push/main change.

Known limits: semantic matching is not full retarget/foot IK/anatomical correctives.
Unknown bones/morphs are skipped; original assets have no supplied animation clips.
Default clips are procedural presentation helpers, not licensed mocap assets.
Imported source eye alpha/hair problems from QA remain (do not claim fixed).
Next: fix companion eyelash alpha, shoulder mask/corrective deformation for T-pose,
then unlocked panel switching/pause/idle and continuous blink/orbit validation.


## 2026-11-20 — Unreal preview reference and animation source discovery

Investigation commit `e3f59a8d064b1cd33089535071e9fd6ff3899cfc`, branch
`integration/motionweave-studio-facial-stage184`. See
`docs/qa/vivid-unreal-animation-source-2026-11-20.md`. User asks for Unreal-like
preview. Local MetaHumanSample 5.7 contains MM_Idle and f/m sample AnimSequence
packages, but no portable animation exports. Standard Mac editor executable
paths are absent; launcher UE_5.7 directory contains plugin remnants. Custom
unindexed engine locations remain possible. Official Creator documentation
confirms separate head/body animation assignment. No asset export/import, code,
public API, save format, runtime behavior or screenshot changes in this stage.
Executed read-only filesystem/launcher discovery and official documentation
lookup. Builds/tests NOT RUN (investigation only); CI NOT RUN; no push.
Next: locate usable Unreal installation or skeletal-animation export, then
validate retargeting with garment rigs and independent face/body clips. Keep
all project copies/builds/caches/captures on KINGSTON. Current procedural idle
is not MetaHuman-quality parity; eyelash alpha and T-pose shirt defects remain.


## 2026-11-20 — Idle phase, motion recordings and actual eyelash alpha fix

Implementation `cdf6db13da6e401ec3bbbdc1402192f4c94e6ca7`, branch integration/motionweave-studio-facial-stage184.
Paths: HumanPreviewAnimation.h, render/Animation.h/.cpp, StudioFacialGpuPreview,
StudioHumanLookSessionTests, vivid-creator, vivid-dna-import baker/test/docs;
Studio stage90 and docs/qa/vivid-motion-review-2026-11-20.md contain evidence.
Three-spine breathing, gentle resting arms, named conjugate gaze and smooth
asymmetric blink envelope. Existing four poses; 0.65s transitions preserve phase.
New optional Animator::Play.startTime defaults0, no existing caller behavior or
save/bridge format change. IMPORTANT first attempt used seek after play, which
clears the fade; test caught it. Final uses initial time during play, retaining
outgoing track. No new production scheduler; Creator offline sequence pauses
MotionWeave and samples a diagnostic pose via the shared Animator helper.
--preview-sequence N writes 24fps fixed-step frames (1..720), path is directory;
requires --smoke --preview-pose, no --preview-time. Not FPS/UI parity evidence.

Actual source eyelash PNG has RGB entirely255 and meaningful alpha:935962 zero,
76 full, remainder fractional. Baker now infers A only for white-RGB meaningful-A
lash exports; explicit metadata wins; hair retains R. Node fixture covers both.
New external Ada-Motion-Review GLB has repaired lashes, original face-edited GLB
unchanged. Working content/Ada/baseColor_8.png repaired with original backed up
under full-character/motion-review/source-texture-backup. Other baked packages
still need rebake. No third-party assets added to Git.

All builds/artifacts on /Volumes/KINGSTON/AIVERSE-builds/motionweave-vivid.
CMake aiverse_studio, aiverse_vivid_creator, aiverse_studio_human_appearance_session_tests,
aiverse_studio_facial_gpu_tests PASS (build-motion-phase-fix.log).
First tests FAIL1/2 due seek cancelling fade (test-motion-review.log), corrected
run PASS2/2 (test-motion-phase-fix.log), report native/test-reports/20261120-174042-42412.
Node test_bake_materials.cjs PASS; git diff --check PASS. Web NOT RUN (no frontend
changes); Studio manual UI, other platforms, real-time pacing/complete orbit NOT RUN.
No CI/push. Existing compiler warnings. First body/ sequence is pre-fix, not final.

Final native Metal body-final/:360 distinct frames→body-poses.mp4 15s, face/:168
frames→face-idle.mp4 7s, both1500x920@24fps. AVFoundation encoding successful.
full-character/motion-review/gallery.html includes videos and inspected native
PNGs. Source frames at open/two blinks, relaxed endpoints/intermediate/A/T inspected.
No pipelineFailures, actor=MODEL_MORPH. Hair remains attached in inspected views
but coarse card silhouette FAIL; lashes now transparent and blink with eyelids,
fine edges still imperfect; gray eye artifacts FAIL; T shoulder skin poke-through
FAIL, relaxed/A samples acceptable. Not full MetaHuman Creator quality parity.

Next: author genuine shirt visibility masks/correctives for shoulder skin, isolate
iris/eye-shell shading and hair coverage/filtering; then orbit/continuous UI checks.
Do not hide entire body or mark existing synthetic BodyVisibility test patch as
artist-approved clothing coverage. Root/legs stay bind: no weight-transfer/foot IK,
cloth collision or physically simulated hair was added. Preserve source backups.


## 2026-11-20 — Named sockets and independent SKM attachments

Code `9e9f43bb32c46314046548c8afc7c5b870c4d740`, integration/motionweave-studio-facial-stage184.
HumanWardrobe.cpp/.h adds optional sockets and item.socket/attachmentRule to
version1 recipes. Existing recipes retain semantics; old strict readers reject
new fields. Rules keep_world and snap require independentRig, preserving imported
child joints/skins/morphs/clips rather than name-remapping. Shared-skeleton clothing
still validates matching bind frames. Sockets are ordinary socket/<name> nodes
compatible with generic Model/View traversal and GLB export, not a new scheduler.
Keep-world exact inverse TRS chain follows rotated/nonuniform-scaled parents;
explicit offset is socket-local, legacy offset order unchanged. Duplicate/missing/
ambiguous parents and invalid transforms reject. Multiple attachments on a bone
use distinct arbitrary slot names. Limits128 sockets/32 assets remain explicit.

HumanWardrobeControls/types expose socket name/bone/local TRS editing, separate
skeleton mode, rules, slot input and draft undo via shared Studio UI. StudioSession
normalizes omitted socket TRS on loading. Existing native async assemble/show
flow publishes changes, not live gizmos. No new bridge allowlist or browser writes.
Docs: vivid-skm-attachments.md, Studio stage91; vivid.md and motion QA explicitly
WITHDRAW the previous hair-attachment conclusion after user reported hair detached.
This stage does NOT claim Ada hair is repaired; no new character assets changed.

Actual checks: all builds/caches/output KINGSTON. Native aiverse_studio,
aiverse_vivid_human_wardrobe_tests, aiverse_studio_human_appearance_session_tests
PASS (build-skm-final.log). CTest2/2 PASS, final report
native/test-reports/20261120-175028-57514, test-skm-final.log; earlier scoped run
also2/2. Added real snap/local-offset/keep-world, parent rotation+scale, independent
skin and exported GLB socket roundtrip, duplicate/missing socket rejection checks.
npm typecheck + existing75 web tests + incremental tsc(KINGSTON cache) + vite PASS;
final typecheck/vite repeated after selector adjustment; dist copied into KINGSTON
native app. Python UI29 PASS(test-skm-ui.log); diff --check PASS.
verify_mac.sh NOT RUN because it hardcodes internal build output contrary to user;
used scoped KINGSTON checks instead. Native UI/screenshots and visual SKM motion
NOT RUN. No CI/push/main change. No imported Unreal .uasset parsing or full parity.

Next: fix Ada's actual hair attachment by locating/extracting its mesh/rig and
using an explicit head attachment, then exaggerate head tilt/turn in native video.
Do not infer attached hair from a few near-static frontal frames. Existing shared
submesh reweighting, vertex-picking/gizmo, cloth collision and independent child
animation playback remain outside this bounded assembly stage.


## 2026-11-20 — Modular head shoulder pose synchronization

Code `eb4cc813bf48e782232600ca4a8b5a82517e0e94`, integration/motionweave-studio-facial-stage184.
HumanPreviewAnimation resolves missing lowerarm directions from complete matching
upperarm bind-world frames only (relative1e-3, reject conflicting donors). Ada head
upperarms1052/1061 were skipped before; head includes exposed shoulder geometry.
No mask or vertex removal. StudioHumanLookSessionTests asserts truncated joint
follows complete arm and distant same-name joint does not. Builds aiverse_studio,
aiverse_vivid_creator, session tests PASS on KINGSTON (build-shoulder-sync.log);
scoped studio_human_appearance_session PASS1/1 report20261120-191116-6147.
Actual native Metal shoulder-sync/t-pose.png inspected: large shoulder patches gone;
minor collar-edge skin still visible. pass1/pipelineFailures0. No full outfit QA,
UI/CI NOT RUN. No save/API changes. docs/vivid-preview-animation.md updated.
Next: hair2264 and brows2263 are unskinned children of vivid_full_root2265, not
vivid_face_head1848. Fix their bind-preserving hierarchy and test exaggerated
head motion; lashes already belong to skinned face1915. New user question answered
honestly: those grooms were NOT yet attached. Do not claim this shoulder fix does it.


## 2026-11-20 — Actual Ada groom binding and capture lifetime fix

Code `440a57d56b63b700d626b04675a95cc482f88328`, integration/motionweave-studio-facial-stage184.
New attach_grooms.cjs bind-preserves unskinned cards using inverse TRS chains;
prepare_full_character applies it to Hair_/Eyebrows_ nodes after framing.
Working KINGSTON Ada/model.gltf hierarchy corrected with backup; new assembled
Ada-Groom-Bound used for testing. Both cards now descend from vivid_face_head;
brows46 morph targets retained, eyelashes remain skinned face. Actual bind error
<4.4e-7. No copied animation, shaders, vertex edits, binary Git assets or save API.
Generic skinned meshes must use independent SKM attachment, not this rigid helper.

Node test PASS (bind, rotate/scale, morph ownership, idempotence, cycle/skin reject,
GLB roundtrip). Full original companion preparation PASS to groom-binding/prepared-pipeline.
Creator builds PASS (build-groom-binding.log, build-groom-capture-lifetime.log).
Diagnostic --preview-head-sweep gives ±0.45rad yaw/±0.20rad pitch on pose copy.
Initial7s capture succeeded; initial body recording crashed after238frames.
System report capture-crash.ips points to Animator::advance reading retired model;
failed-last.png shows revision2/headscale0.964 during interactive edit. Fixed
sequenceAnimator lifetime on publication/release; offline sequences omit controls
and reject unexpected model rebuilds. LLDB launch stalled under Rosetta and was
stopped, system report provided stack. No unrelated processes stopped.
Final head-final168 and poses-final360 native frames completed successfully;
encoded7s/15s videos on KINGSTON, pipelineFailures0. Inspected turn/tilt/blinks/T:
hair follows head; large shoulder protrusions gone; small collar intersections,
coarse hair silhouette and GRAY EYES still fail. Not universal groom/UE parity.
See docs/qa/vivid-head-binding-2026-11-20.md and groom-binding/gallery.html.
No frontend/GUI/CI/push; true strand physics, all rigs and realtime pacing NOT RUN.

Next USER STEERING: fix gray colorless eyes. Read actual eye baked PNG: brown
iris, black pupil present (centerRGB10), so investigate overlay layers/UV/render
binding. Source eyeShell blend alpha.35, eyeEdge alpha.25. Latest files extracted
to full-character/eye-review. Do not silently paint over screenshot or claim
fix until matched native captures; retain completed shoulder/groom work.


## 2026-11-20 — Gray eye glare repair and native comparison

Code `8f76aa33aefc796791361bc54eb52d0eddef74d5`, branch
integration/motionweave-studio-facial-stage184. HumanAppearance eye roughness
default .6; importer uses same portable single-layer PBR baseline, occlusion
alpha .12 / wet edge .04. Creator --closeup adds actual native camera close-up.
Existing explicit appearance values and save schema preserved; no renderer
shader edits, no final unlit material, no repainted iris pixels.

Source brown atlas/pupil were valid. Native no-shell diagnostic remained gray;
unlit/matte and .45/.65 roughness pair recovered contrast. Matched catalog
before-closeup.png/final-closeup.png confirms brown iris/dark pupil restored.
Neutral-light168frame head sweep completed, inspected turn36/blink41; eyes
close, hair follows, contrast retained. Jagged lash/eyelid edges, bright sclera,
coarse iris detail and no refractive cornea still limit quality. No UE parity.

All artifacts full-character/eye-review on KINGSTON. New Ada-Eyes-Reviewed
assembled model; working content/Ada factors/default updated with source-backup.
Historical assembled assets unchanged. Native Studio/Creator/session targets
built PASS. Node test_bake_materials.cjs PASS incl overlay regression. Scoped
studio_human_appearance_session PASS1/1 report20261120-193153-13311.
Native captures pipelineFailures0; GUI/CI NOT RUN; not realtime performance.
See docs/qa/vivid-eyes-2026-11-20.md. No push this stage.
Next: correct eyelid/lash edge coverage/deformation and improve iris detail plus
separate optical cornea response, retaining matched native before/after evidence.


## 2026-11-20 — Layered eye optics, material/assembly integration

Code585e6b4d2e34eb5f43a24d53eff766bb98775e66, integration/motionweave-studio-facial-stage184.
Material.eyeOptics appends UV centre/radius/depth to U2, equality/hash/merge;
GLSL/MSL refracted UV plus wet coat, glTF extras.aiverseEye v1 import/export,
assembly accepts baked Eye with portable PBR. Appearance adds3 fields(schema75).
Baker uses original iris fibers, radial limbus and restrained sclera; cartilage
strip material fixed white jagged edges. No geometry shrinking/deletion, lashes
alpha and51 morphs retained. Working Ada source updated with JSON backups; new
Ada-Eye-Optics asset, all external on KINGSTON. Creator --eye-detail .85 camera.

Native builds PASS; Node baker PASS;4/4 CTests PASS report20261120-200027-32474.
GLSL12/12 stages syntaxPASS ES300/330,Q0/1/2; initial harness missing ColorSpace
prefix fixed. Initial schema count72 and new fixture missing hiddenMeshes[]
failed; corrected tests, retained logs. Native Metal motion168frames7s24fps
completed pipelineFailures0. Eye/source reload logs show correct shading3 and
optics. GUI/mobile/Vulkan/OpenGL GPU/CI NOT RUN; no push.

CRITICAL latest visual finding: final-catalog.png after3.125 normalization
reveals excess pale glare again. dynLight ds includes broad environment/grid
response, not only source glints; multiplying the whole ds was insufficient.
Do NOT call MetaHuman parity or final quality achieved. Next: isolate sharp
reflections of actual point-light directions from broad ambient response; rerun
matched catalog capture and motion before delivering final media. Existing
final-motion.mp4/gallery.html are pre-refinement artifacts. Full analysis/tests
and known skin-normal/card-hair limits in docs/qa/vivid-eye-optics-2026-11-20.md.


## 2026-11-20 — Local corneal glints; final reviewed delivery

Code7773c40b0a141fa76907d828c776500d75086d3e, same integration branch.
Replaces broad dynLight ds scaling with a bounded up-to8 actual point/spot-light
reflection loop (distance/cone/normalized sharp lobe). Both GLSL/MSL changed;
low keeps sun/environment coat and omits point glints/parallax. All ordinary
PBR behavior and prior optical data/API preserved. No new asset/UV/exposure
change during this refinement.

Native Creator/Studio/appearance tests built PASS (build-local-glints.log).
Appearance CTest PASS1/1 report20261120-200337-34885 following integration4/4
report20261120-200027-32474. GLSL syntax12/12 PASS ES300/330 Q0/1/2, LDR
skinned+morph+normal permutations (glsl-local-glints.log). Vulkan/OpenGL GPU,
mobile, GUI, CI and realtime performance NOT RUN. No push this stage.

Native Metal catalog comparison local-catalog.png restores brown iris/dark
pupil with small sharp highlights; previous final-catalog.png was pale and is
DIAGNOSTIC ONLY. Final local-motion168 frames completed, inspected turn36,
blink41,tilt110; glints move without erasing iris; cartilage white edges gone.
pipelineFailures0. Encoded eyes-reviewed.mp4:168frames1500x92024fps (7s).
All artifacts /Volumes/KINGSTON/AIVERSE-builds/motionweave-vivid/full-character/eye-optics/.
gallery.html points at reviewed images/video, previous takes retained as history.
Source content/Ada and assembled Ada-Eye-Optics retain previous stage's changes.

Readiness: iris/material/optics/export/reassembly implemented+scoped verified;
eyelid white-fleck fix and lash alpha/morph retention verified on this asset.
MetaHuman reference parity is NOT established: no volumetric ray tracing, source
skin-normal/card-hair artifacts persist; no all-rig/blendshape coverage claim.
Next if requested: controlled Unreal reference comparison and broader facial
corrective/eyelid deformation QA; do not re-enable broad grid specular as coat.


## 2026-11-20 — User-rejected glass eyes; blink normal repair

Implementation a1b0870f43f336d809e6613d32ac8f7ba7de6ac2 on
integration/motionweave-studio-facial-stage184. Prior eye-optics delivery was
explicitly rejected as glass-like. Softer/reduced cornea glints (GLSL/MSL), lower
sclera/fiber intensity, UV-faded lid occlusion; defaults pupil .048/sclera .40.
No schema/ABI/save changes; saved values override new defaults.

User then reported black blink stripes. Color-coded accessories, unlit skin,
normal-texture-off, shadow-off and morph-normal-off captures isolated malformed
imported blink NORMAL deltas on head skin. New repair_morph_normals.cjs derives
normal changes from deformed triangles, welds smooth coincident vertices and
repairs only eyeBlinkLeft/Right on known companion head material in
prepare_full_character.cjs. No position/UV/skin/morph-position changes. Creator
adds --shadow-quality off|medium|high diagnostic flag; default medium preserved.
Smoke gate permits zero shadow submissions only for explicit off.

PASS: Creator/Studio/appearance build (build-final.log), final CLI Creator build
(build-final-cli.log), CTest vivid_human_appearance 1/1, Node baker + folded-plane
normal repair/idempotence tests, full Draco companion import, GLSL syntax12/12
ES300/330 Q0/1/2. Initial nonexistent /Volumes/KINGSTON/tmp baker path failed;
rerun existing directory PASS. Initial no-shadow smoke gate failed, corrected.
All logs/media at /Volumes/KINGSTON/AIVERSE-builds/motionweave-vivid/full-character/eye-natural/.
No GUI/Vulkan/OpenGL GPU/CI runs; no push.

Native Metal final-motion168frames/7s24fps captured; open, turn and both blinks
reviewed. Broad black upper-lid stripe removed with full textures+shadows intact.
eyes-blink-fixed.mp4 encoded168frames1500x920. Working content/Ada source
updated with backed-up JSON/appearance, appended normal buffer/accessors and
new eye/shell textures; old geometry/node/accessor IDs retained. Source reload
42frames confirmed repaired blink (source-blink.png). Candidate
content/Characters/Ada-Eyes-Repaired retains original groom/wardrobe bindings.
Prior Ada-Eye-Optics remains an older comparison; do not deliver as latest.

Known limits: small shadow/skin speckles around lids remain, card-hair edges, no
full RigLogic corrective parity or MetaHuman visual acceptance. Current repair
is deliberately scoped to known companion smooth head, not arbitrary hard-edge
SKMs. QA details docs/qa/vivid-eye-optics-2026-11-20.md. Next: diagnose remaining
shadow flecks and evaluate other expressions under matched reference lighting.


## 2026-11-21 — Adaptive character-stage shadows and soft portrait key

Code864bd2d8c589960bac9d9e46b60e67a1f80a3fe1; branch
integration/motionweave-studio-facial-stage184. User approved adapting shadows
to screen size after reporting stepped neck shadows and crawling nose edges.
Generic engine/render/AdaptiveShadowResolution.h chooses1024/2048/4096 from
focus-plane screen density, with15% upshift margin,40% downshift threshold,
60 quiet frames and120-frame cooldown. Host cap4096; per-view state; no ECS or
asset/save schema changes. VIVID lighting bounds fit foot/height with arm/floor
margin. Creator and Studio facial view opt in; Studio clears bounds on exit.
GPU target retirement remains existing Renderer3D behavior. No shader changes.

First fitted4096 still produced a hard nose patch. Scoped studio soft PCF
(.012*height) replaces its tiny-angle PCSS key; ordinary game lighting remains.
Source Ada geometry/materials untouched. Creator --camera-distance .5..120
and shadow diagnostics allow reproducible acceptance; default High now adaptive.
This is a single-map policy, NOT VSM/cascades/per-pixel demand/temporal denoising.

PASS Creator/Studio builds on KINGSTON (build-distance.log/build-studio-final.log);
standalone policy test and CTest adaptive_shadow_resolution1/1 with assertions
active in Release. First combined make regenerated CMake but missed the new
second target; initial CTest NOT RUN. Separate build + tests-assertions.log PASS.
Native captures: close1.05=4096, distant12=2048; default distance6.4 still4096
for this asset. soft-motion168frames1500x92024fps, pipelineFailures0; reviewed
open/turn/blink/opposite turn. Broad nose patch removed and neck softer.
Final adaptive-soft-shadows.mp4 encoded168frames. Earlier adaptive-shadows.mp4
is pre-softness diagnostic, do not deliver as latest. All artifacts/logs:
/Volumes/KINGSTON/AIVERSE-builds/motionweave-vivid/full-character/shadow-review/.
QA docs/qa/vivid-adaptive-shadows-2026-11-21.md records exact commands/limits.

No GUI/live zoom-tier-boundary visual check, Vulkan/mobile GPU, CI or frame-time
benchmark; no push. Policy hysteresis tested, not a zero-pop or zero-noise claim.
Background outside character fit/oversized attachments need broader checks.
Next: interactive zoom transitions and all8light rigs; quantify remaining
filtered shadow noise before temporal filtering changes. Preserve head/groom
bindings and previous blink-normal repair.

## 2026-11-21 — native FXAA and neck albedo diagnostic checkpoint

Implementation: `3d267f847a5a6500884418f9d34ec78d61ca9237`; branch
`integration/motionweave-studio-facial-stage184`. Native Studio/Creator now
resolve LDR FXAA through existing PostFx; retirement/shutdown included. Creator
--no-aa supports comparison. No temporal AA, save format or ECS changes.
Opt-in importer neck_albedo_smoothing filters only known lower MetaHuman atlas;
face/normal maps retained. External Ada source updated with backup on KINGSTON.

PASS Creator+Studio builds (build-restored.log), filter test, existing baker
test, optional real-import comparison; native168frame video and source reload
AA enabled=1 passes=2 pipelineFailures=0. Exact artifacts/commands and limitations:
docs/qa/vivid-neck-aa-2026-11-21.md. PostFx CTest discovery found zero tests.
No GUI, Vulkan/mobile, performance, CI or push. Skin texture cleanup is cosmetic.
Residual hard neck shadow at blink frame41 UNRESOLVED; unoffset receiver-plane
derivative experiment failed visually and was reverted (background seams).
Next: test user-requested elderly wrinkled model, then isolate neck shadow edge.
All builds/media: /Volumes/KINGSTON/AIVERSE-builds/motionweave-vivid/.

## 2026-11-21 — older male skin QA (visual acceptance failed)

Code/QA commit `a8414da4bca3ff8f2dc14e376696692feea6de26`, same branch
`integration/motionweave-studio-facial-stage184`. Added only fixture preparation
tool and QA report; no renderer/schema changes. External Patchbay MakeHuman older
male, not MetaHuman DNA or a scan. Provenance/license/SHA and exact paths in
docs/qa/vivid-elder-skin-2026-11-21.md. Asset/media on KINGSTON elder-skin/.

Initial native import FAILED:67 named morph targets exceed64. Tool now removes
speech visemes retaining52 ARKit shapes, remaps weights, decodes meshopt/WebP,
adds framing wrapper and VIVID skin mapping. No Ada neck filter, no synthetic
normal maps. Native catalog48frames, dramatic144frames, shadow-off1frame PASS;
AA1/passes2/map4096/pipelineFailures0. PNG/morph/frame-count validation PASS.
144frame24fps video encoded and selected front/turn/blink/opposite stills inspected.
Severe triangle shadow patches reproduce independently of Ada, disappear with
shadows off. Eyebrow/lash edges coarse; source2K diffuse lacks normal/roughness
maps and cannot prove skin microdetail. Visual quality FAIL, no MH parity.
No GUI/Vulkan/mobile/CI/performance check; no push. Latest native binary is
restored-shader FXAA build from preceding stage. Next: isolate shadow bias/receiver
plane discontinuity against both fixtures, then acquire higher-detail elder scan.

## 2026-11-21 — authored LOD preparation, partial stage

Implementation `7b9646b83d751d23d822006c5dbb189577ee0bec`, branch
`integration/motionweave-studio-facial-stage184`. New configure_lods.cjs links
authored meshes through existing aiverseLods metadata; rejects skin/morph/cycle
mismatches and detaches alternative leaf nodes from visible scene. Creator
--lod auto|highest added. No renderer/Studio UI/schema changes.
PASS JS policy and synthetic GLB roundtrip tests; Creator KINGSTON build; CTest
auto_lod_runtime1/1. Transition executable missing, direct command FAILED;
transition test NOT RUN (corrected QA's initial inaccurate statement).
See docs/qa/vivid-lod-2026-11-21.md for external logs and reproduction.
Incomplete: wardrobe assembly clears chains; no synchronized character policy,
arbitrary manual level UI, authored real character visual acceptance or CI.
Next preserve/remap wardrobe chains then real skinned/morphed character LOD
near/far native capture and editor controls. All outputs on KINGSTON; no push.

## 2026-11-21 — character LOD assembly + Studio controls

Implementation `95bae785b3893ca75f4d5135165f6478254df97f`, branch
`integration/motionweave-studio-facial-stage184`. Wardrobe keeps/remaps reachable
authored LOD chains for base and added parts; body/face deformation includes
alternatives. Native Studio existing animation bridge accepts optional validated
automaticLod (defaulttrue for old calls). Shared UI offers auto/LOD0, native facial
renderer consumes it. Parts keep individual thresholds; no equal-level controller.
Creator logs actual level counts; GLB chain tool registers standard extensions.

PASS Creator/Studio KINGSTON builds; CTest4/4 auto_lod_runtime, vivid_human_wardrobe,
vivid_face_shape, studio_human_appearance_session. Initial session test compile
failed Json::number(3) ambiguous, fixed3.; no remaining build failure. Standalone
ModelLodTransition assertions built with -UNDEBUG and passed (configured target
was absent). Typecheck/web tests/build PASS; Python UI29/29 PASS. Native elder
auto48frames LOD1=5 vs highest1frame LOD0=6; full Ada auto48frames LOD1=5,chains6,
blocked0,pipelineFailures0. Selected PNG inspected; video48frames24fps encoded.
Fixtures use diagnostic index simplification, retain original vertex/morph arrays,
threshold5000 deliberately exercises coarse levels. No VRAM/performance claim.
See docs/qa/vivid-lod-completion-2026-11-21.md for exact logs/limits and paths.

No GUI/live zoom-boundary/Vulkan/mobile/CI/push. Previous authorization: verify
without interface. No production source asset replacement. Shadow artifacts
remain unresolved. Next: production-authored LOD threshold tuning and live zoom
visual QA, followed by separate renderer-shadow investigation. No save/ECS change.

## 2026-11-21 — full older Bruce companion and planted body idle

Implementation `063c0a2a76595361618dd2a51eee88861a69725c`, branch
`integration/motionweave-studio-facial-stage184`. More visible spine/shoulder/arm/
elbow/hand breathing and settling; optional complete-leg pelvis weight shift
baked with shared solveTwoBoneIK and planted ankle targets/orientations. Canonical
spine_01/neck_01 suffix bug fixed, MakeHuman upper-body aliases supported. Default
Creator uses same full-body preview instead of head-only bob. Diagnostic
--preview-fixed-pose prevents automatic pose cycling in pure-idle capture.
No gameplay ECS/save change; no full motion-capture/MetaHuman quality claim.

Bruce published companion GLB downloaded from creategamecharacters/metahuman-to-glb
docs/5.7/characters/bruce; actual full body/outfits/grooms/face. Generic companion
preparation accepts FaceMesh names, attaches beard/mustache. Separate upstream
coverage maps + metadata converted by new prepare_companion_coverage.cjs. Explicit
external grey colour blend, source skin pixels retained, eye shell mask disabled
for layered eye. Final alpha BLEND; early MASK was too sparse. Source/license/SHA/
adaptations recorded externally, no binary vendoring. Repository code MIT not
a claim about Epic character content licensing. Python download certificate failed;
curl retry succeeded. Source content Ada untouched.

PASS Creator/Studio KINGSTON builds, extended native wardrobe test (initial test
found canonical spine name bug, then PASS), shared groom and morph-normal tests;
Studio appearance session PASS. Synthetic complete-leg test samples5phases and
checks ankle drift<.001 plus moving pelvis; loop endpoint/body channel tests PASS.
Final bruce-continuous-idle144frames/24fps fixed relaxed pose; pipelineFailures0,
57commands. Video bruce-continuous-body-idle.mp4 encoded144frames1500x920. Reviewed
fullbody36/108 and closeup0/41 blink. Earlier bruce-body-idle.mp4 switches poses,
NOT final pure idle. All files on KINGSTON full-character/elder-skin/.

QA: docs/qa/vivid-bruce-body-idle-2026-11-21.md. Known visual FAIL remains on
triangular collar/cloth patches and shoulder intersections; do not claim polished
clothing or complete MH Creator parity. No GUI/Vulkan/mobile/CI/performance/push.
Next: isolate clothing faceting/shadow vs deformation and mask head shoulders
against outfit; validate idle on additional body rigs and actual ankle targets.

## 2026-11-21 — Bruce clothing deformation/visibility and fixed-PCF repair

Implementation `93fb42cbd79a4c3877bf6f460331d9733d907e33`; branch
`integration/motionweave-studio-facial-stage184`. Explicit offline jacket weight
smoothing (final8iterations/.8 strength,top4normalized) and closed-outfit skin
index mask (3302triangles removed,neck ancestry+hands retained) repair Bruce's
spikes/skin protrusions. Geometry/UV/normals and texture contents unchanged.
Original source remains model-final.glb; corrected model-clothing-fixed.glb and
manifest published under KINGSTON content/Characters/Bruce-Full. Manifest backup
manifest.before-clothing-fix.json; clothing-repair.json + clothing-visibility-mask.json
record reversible parameters. Source binaries not vendored. Mask is outfit-specific;
rebuild from original for other clothes. Imported vertex numbering can change,
so external CSR/LOD sidecars must not be reused blindly (this fixture has none).

Common preview now synchronizes missing-leg pelvis only with complete rigs sharing
the same bind-world frame; resolves modular face detachment during weight shift.
Native regression rejects a distant donor. Ada42frame before/after inspected: broad
shoulder patch disappears without source edits. Creator --preview-bind-pose added.

GLSL/MSL fixed-PCF SKINNED/BAKED path omits triangle receiver-plane extrapolation
and bounds normal offset to1..4 existing texel offsets by filter footprint. Rigid
receivers/cyclorama and PCSS unchanged. No RHI/batching/ECS/save changes.
Unconditional shadow derivative call experiment did not help; global removal of
receiver-plane bias leaked background, rejected. First4/.65 weight/mask variant
still exposed neck skin in T pose; final8/.8+neck ancestry resolved inspected case.

PASS Creator/Studio/native session builds; CTest wardrobe/face/session3/3;
modular-pelvis wardrobe regression additionally rebuilt and PASS. JS smoothing
test PASS; actual-asset geometry/normal/UV/weight/index subset validation PASS
(validation8.log). GLSL ES300/GL330 Q0/1/2 skinned+morph+normal12/12 glslang PASS.
Native Metal approved-motion144frames24fps + torso37frames + Tpose1frame inspected;
55headcommands,50colour/shadow,pipelineFailures0. Final video
bruce-clothing-approved.mp4 encoded144frames1500x920. Earlier reviewed/fixed videos
are intermediate variants; final still approved-torso.png, t-pose8.png. All under
/Volumes/KINGSTON/AIVERSE-builds/motionweave-vivid/full-character/cloth-review/.
QA docs/qa/vivid-bruce-clothing-2026-11-21.md. No GUI/Vulkan/mobile/CI/perf/push.
Next: broader outfits/pose/material-quality checks and authored corrective poses.
No claim of full Unreal cloth physics or universal zero-shadow-artifact behavior.

## 2026-11-21 — eight-light wardrobe/pose regression and backdrop fix

Implementation `ba0f89312774dbd21ecafa52cc12f87f88ca578a`; branch
`integration/motionweave-studio-facial-stage184`. Human studio cyclorama now uses
existing kDrawNoCastShadow in Creator and native Studio facial view: it receives
character shadows but no longer casts onto itself. Found thin backdrop seam in
cool/portrait light; matching-phase cool before/after confirms removal while
character silhouette remains. No shared shader/RHI/batching/save/asset change.
Creator --preview-light-sweep cycles eight presets every48 offline sequence frames;
invalid prerequisites reject exit2. Offline sequence logs ankle/ball bone origin
max drift/sample count; no per-frame sampling in normal interactive mode.

PASS Creator+Studio KINGSTON builds (build-metrics.log,
build-receiver-background.log), invalid CLI test, final frame counts/log checks.
Native macOS15.6 Apple M3 Ultra Metal: final-transitions360frames/15s and
final-lights384frames/16s,24fps,1500x920; pipelineFailures0. Selected A/T/return
checkpoints and all8final lighting PNGs inspected. No old shoulder skin/needle
folds observed in these views. Numeric endpoints: transitions max0.00050682
model units/2880samples; idle0.00000933/3072samples (~0.013% of3.8-unit height
for transitions). This is bone-origin characterization, NOT physical sole
contact/cloth simulation or locomotion acceptance.

Videos encoded: bruce-pose-transitions.mp4 and bruce-eight-lights.mp4. All
artifacts, binary/asset/source hashes, detailed report and logs:
/Volumes/KINGSTON/AIVERSE-builds/motionweave-vivid/full-character/wardrobe-regression/.
QA docs/qa/vivid-wardrobe-regression-2026-11-21.md. Initial light-sweep images
retain found seam; use final-lights/final-*.png for delivery. No new unit suite
or unchanged-suite repetition; actual native captures cover this host fix.
No GUI (standing user instruction: verify without interface), other outfits,
Vulkan/mobile, CI, performance benchmark or push. Next: additional outfits and
camera angles, production LOD tuning and interactive editor parity.
## Stage 6G — 2026-11-19 — atomic human Creator hot reload integration

- **Branch:** `feature/vivid-human-safe-publication`, created from live VIVID branch commit `d02943682c0e287b9ee5043ded5da528c7c57a10`, rather than carrying an old divergent branch with potential conflicts. This stage updates only the original `tools/vivid-creator/main.cpp` and its headless transaction test, plus macOS CI branch routing; no duplicate GUI, renderer, skinning or simulation.
- **Commits and paths:**
  - `bb55053a60191952b725859660bacb85c922f6a3`: `tools/vivid-creator/main.cpp` implements all-or-nothing GPU character reload. Builds a new DNA-driven CPU head/groom, preserves imported source texture maps via `humanMaterialFromSource`, compiles MotionWeave animation graph, stages complete GPU models and a new GPU actor, constructs the new World and pose, and only then retires and replaces the previous visible human. Failed candidate stage is retired only after an explicit successful device idle barrier; the old character remains on screen and usable.
  - `6a343e5b3d7a62019ae9f74bf96e02f00257d2d6`: `tests/vivid/human_creator.cpp` adds a protected existing-target regression that verifies a failed DNA atomic save never removes its existing sentinel/directory. Current base already implements the safe atomic rename failure behavior, so it was not reimplemented.
  - `a0685e6c802d7968c25310135303190d57242298`: `.github/workflows/vivid-validation.yml` adds the safe-publication integration branch to macOS 16-CTest/Creator build CI while preserving the main VIVID workflow.
- **Architecture:** stage/candidate CPU model, hair model and DNA-driven skin/eye shader graphs before retiring old GPU resources. New rig graph, cached immutable morph asset, own writable actor stream, MotionWeave World/pose all exist before publication. Existing creator controls, glTF/FBX/OBJ import, texture preservation, 52 facial semantics, skin/eye/hair surfaces and GUI smoke capture are unchanged. One GPU idle boundary is allowed on committed edit, never per animation frame. On failure, staged resources are freed behind a confirmed barrier and previous presentation remains valid.
- **Tests actually run on this new branch:** **NOT RUN**. GitHub source was fetched and structurally inspected, but no actual Mac compiler/Metal GPU or screenshot evidence has been received for Stage 6G. Prior Stage 5J user Mac 11/11 and 2 GUI previews on `fa5504a9` predates Stage 6 human code. The newly authored 16 CTests plus procedural/imported Creator GUI smoke should be run in one pass, followed by full engine regression.
- **Known constraints and risks:** the Creator remains standalone ImGui instead of an integrated editor tab. An explicit idle barrier blocks rare DNA edit commits; a failed barrier prevents publication and may require manual retry. Production photorealistic skin diffusion/eye optics/groom, bespoke licensed head scans, artist DNA calibration, streaming/LOD and Vulkan parity are still separate visual quality gates. This is an integration hardening change, not full MetaHuman-quality certification.
- **Next:** user Mac: fetch this branch into a separate clean VIVID worktree, `AIVERSE_VIVID_REF=feature/vivid-human-safe-publication bash tools/vivid-validate.sh`. Inspect 16 CTests, single and 12-actor GPU previews, procedural and imported Creator GUI PPMs. Only when green should the PR be rebased/merged to the updated VIVID feature branch; do not merge to main until full AIVERSE tests also pass.

### Stage 6G follow-up — selection-safe one-pass runner

- Commit `6447bb1c374d7d31517a76609c17c267694941eb` updates `tools/vivid-validate.sh` to read `AIVERSE_VIVID_REF`, defaulting to the existing `feature/vivid-digital-humans` for backward compatibility. The isolated safety-fix branch must be tested with `AIVERSE_VIVID_REF=feature/vivid-human-safe-publication bash tools/vivid-validate.sh`; otherwise the script would deliberately switch to the base VIVID branch and NOT test this patch. This clarification is critical for reliable test provenance.
- Tests after this runner adjustment: **NOT RUN**; Mac CMake, CTest, and four GUI previews still require the actual user/CI execution.
## Stage 6C — 2026-11-19 — Creator last-good transactional publication

- **Dedicated integration branch:** `feature/vivid-human-creator-ui`, branched from `feature/vivid-digital-humans` at `0b5a94d789258a1c6d3a1a3cc898656e4f249a27`. Avoid overwriting concurrent improvements to the original VIVID branch. No `main` merge.
- **Discovery:** the source branch already included a working full ImGui editor at `tools/vivid-creator/main.cpp` with DNA editing, procedural/manifest import, GPU facial morphs, MotionWeave, dynamic hair, and GUI capture smoke mode. A provisional duplicate `apps/vivid_creator/main.cpp` and duplicate `aiverse_vivid_creator` CMake target were *discarded* in commits `ddd4fe94b0dd2aac616485688c89895d445b8242`, `9f9ddb929514ee338149eb24f43701f3caed71e0`. Production continues using the original Creator, not a second editor/scheduler.
- **Implementation commit:** `4cd4bd1713e0d3b647ad2bc26678e8096ae3856d` — `tools/vivid-creator/main.cpp`: Creator regeneration is transactional rather than destroying existing head/hair/actor before GPU creation of the replacement. It first compiles the DNA-driven candidate on CPU, builds MotionWeave graph, reserves GPU head and dynamic groom buffers, creates the shared morph GPU asset and per-character actor, allocates new presentation World and face matrices, and publishes only when all stages succeed. Any failure leaves the original character's model and rig intact. Failed candidate uploads retire staged GPU resources behind a full queue idle barrier rather than leaking or drawing partial data. Existing `releaseHuman` owns the old actor's explicit-safe retirement.
- **Compatibility:** no serialized DNA schema changes, no authoritative simulation, no `gfx::Device` or `ModelVertex` ABI changes, and no duplicate animation implementation. Uses existing VIVID HumanCreatorSession/Preview/Import/GpuActorStream and MotionWeave APIs. GUI, CTest target names and manifest format remain unchanged.
- **Verification:** source review only. No native macOS compilation, ShaderGraph warm-up or screenshots were run for this commit. Previous Stage 5J 11/11 CTests and one/twelve-actor GUI passes were on `fa5504a9` and do not validate Stage 6 human code. Stage 6A/6B human tests remain unverified until a Mac run.
- **Risks:** asset publication requires GPU idle only when a DNA edit commits, not per frame. A failed GPU idle barrier prevents retiring last-good or staged buffer resources and must abort safely. Existing Creator may still need stronger device-loss handling, asset migration/undo policy and real human visual regression. Transaction is an editor-local integration, not generic automatic hot reload of all model assets.
- **Next:** audit Creator smoke and headless HumanDNA/HumanRig/Materials/Groom/Import tests in the current branch; fix first Mac compiler/ShaderGraph errors, then run a single comprehensive macOS validation including the real GUI Creator capture. Keep separate handoff for each stage.

## Stage 6F — 2026-11-19 — Creator DNA last-good save and isolated macOS CI

- **Branch:** `feature/vivid-human-creator-ui`, forked at `0b5a94d789258a1c6d3a1a3cc898656e4f249a27` from `feature/vivid-digital-humans`. Base VIVID branch has continued to receive parallel updates, so this is an isolated integration/review branch, **not merged to VIVID/main**.
- **Implementation commits and exact files:** `2931fdb77d67f6075673c2a4fd28fd376bdea55b` — `tools/vivid-validate.sh` now selects `AIVERSE_VIVID_REF` or defaults to isolated branch for this branch's one-shot Mac acceptance; `0c739b4f831c258a0807d94503d76cda01aa52dd` — new `.github/workflows/vivid-human-creator-validation.yml`, macOS-15 focused build for Creator and VIVID/MotionWeave 16-case CTest; `a701f303f639ea1caa4965b7323469f17844557f` — `engine/character/vivid/HumanCreatorSession.cpp` removes unsafe delete-existing-asset fallback after atomic save rename failure; `793c1a0a04ddd39a5d4475760fa6412383a206e4` — `tests/vivid/human_creator.cpp` asserts failed save cannot delete existing directory and sentinel; `2eef4c1dcc3fd892159baf4bcd50339714c23323` — `docs/vivid.md` details safety and provenance.
- **Creator integration context:** Stage 6C commit `4cd4bd1713e0d3b647ad2bc26678e8096ae3856d` changed existing `tools/vivid-creator/main.cpp::regenerate` to stage CPU/GPU human, morph actor, MotionWeave World and presentation matrices before retiring the previously displayed model. No duplicate editor, scene scheduler or simulation ownership was created.
- **Behavior:** `saveFile` never removes the last-good DNA JSON on failed rename. On Windows/filesystems where atomic replacement of an existing file is not available, the method now fails conservatively (leaving `.vivid.tmp`) instead of deleting the original. Headless CTest covers this on every supported platform using a target directory containing sentinel data.
- **Actual tests after Stage 6C/6F:** **NOT RUN** from this assistant session. GitHub Actions run for the isolated branch was pending at last inspection. The earlier user-provided 11/11 CTest and single/12-actor GUI pass were at Stage 5J SHA `fa5504a9f2e1f64087ef73139525e5bde31976b4`, before Stage 6 human code and this stage; they are not evidence for current branch.
- **Risks:** the Creator's GPU transaction still requires an explicit idle barrier on editing (never per frame) and remains a standalone GUI, not yet embedded as a tab in the editor. Asset licensing, true human scans, multi-layer skin refraction/diffusion, realistic strand hair and MetaHuman binary DNA interchange are not solved by this PR. The source VIVID branch advanced independently and may conflict in `tools/vivid-creator/main.cpp` when integrated; reconcile before merge.
- **Next step:** obtain full Mac test log for `bash tools/vivid-validate.sh` in an isolated worktree, including 16 CTests, GPU single/crowd previews, procedural and imported human GUI smoke (8 PPMs); only after successful validation integrate as a reviewed PR into the updated VIVID branch. Then run full AIVERSE regression before merge into main.

## Studio Human Creator window + human_* MCP — 2026-11-21

Branch `feature/vivid-digital-humans`. Before it: main (`43ef79e6b`, shared engine roads), vivid-human-safe-publication
(`7aef6fe98`), vivid-human-creator-ui (`13f1121d0`) and the studio-html5 editor commits (Scene camera, orientation
gizmo, component Inspector: `5d106b453`..`d32a720d4`, cherry-picked) were merged in. Not taken from studio-html5:
`607f7b958` "shadow-depth transmission for backlit SSS" + its docs — its `sssThickness` and main's vegetation
`thinTransmission` both claim PBR U2 `light.w`; the studio-html5 owner must pick a layout when merging main.

Implementation: `ef3ac46eb` (see docs/vivid.md § Studio Human Creator).

Actually run (Linux VM, g++ 11.4, no CMake):
- `apps/studio-native/tests/StudioHumanCreatorTests.cpp` standalone g++ build + run with the vivid human sources,
  Json, Model, ModelMerge, MeshBuilder, ShaderGraph (other model loaders left unresolved, never called):
  `studio human creator PASS (25 nodes, 5 targets, 11932 verts)`. `-Wall -Wextra` clean for the new headers.
- `g++ -fsyntax-only` OK: StudioSession.cpp, mac/StudioFacialGpuPreview.cpp, apps/editor/core/HumanCreatorOps.cpp,
  Workspace.cpp. MCP input schemas parsed with json.loads (7/7).
- Studio web: `npx tsc --noEmit` OK; all 16 node test files PASS (new humanCreatorSchema, updated nested-dock).
- `python3 -m unittest discover -s tools/studio` 41 OK (1 skipped); `tools/readme/check.py` 0 problems;
  `tools/skills/catalog.py check` OK; `tools/skills/audit.py --strict` 1 error, pre-existing (studio-ui NN-* path).

NOT RUN (KINGSTON Mac): CMake configure/build/link, ctest `studio_human_creator` and the extended
`studio_human_appearance_session`, Metal: Creator publication in StudioFacialGpuPreview (head + hair keys,
retirement on mode switch), rig overlay at submit, hair draw; aiversemcp link with aiverse_vivid_human; Studio UI
screenshot of the window and the viewport.

Known limits: static Studio preview (no groom simulation, hair uses head framing); brush weight painting, scalp
collision mesh and imported packages stay in tools/vivid-creator; no MCP tool captures the Studio viewport.
Next: build on KINGSTON, run `ctest -R '^studio_human_(creator|appearance_session)$'`, screenshot Human Creator with
"Show in viewport" for neutral/warm presets and a jawOpen rest value; then decide the SSS `light.w` layout with the
studio-html5 owner.

## 2026-11-21 — Character Creator: MetaHuman-style editor features (branch feature/vivid-digital-humans)

Done (code): drag sculpt + brush circle, 1024 landmarks; live slider preview; unified look/face/sculpt history with
⌘Z; preset thumbnails (`studio.humanLookCapture`, `studio.humanAppearanceThumbnails`); before/after
(`studio.humanCompare`); test expressions (`HumanPreviewAnimation.expression`); per-part face blend picked in the
viewport (`studio.humanFacePick`); card-style pose & light tabs (8 existing light rigs); live wardrobe try-on
(`studio.humanTryOn`), clothing `tint`, LOD budget (`lodTriangles`), "В игру" (`studio.humanPlaceInGame`); random
character with locks; Russian description -> draft + agent prompt; MCP `human_look_schema|list|get|save`.
Fixes: no map flash when another character imports; sculpt auto-picks head meshes on a newly shown model; asset picker
portal over the viewport; Russian status texts; Studio reopens the last project.

Tests actually run here (Linux VM): `tsc --noEmit` clean; `node --test tests/*.mjs` 115/115 pass (incl. new
humanCreatorSchema cases); `g++ -fsyntax-only` of StudioSession.cpp, StudioFacialGpuPreview.cpp, StudioMetalScene.cpp,
HumanCreatorOps.cpp, HumanWardrobe.cpp, human_wardrobe.cpp, StudioHumanLookSessionTests.cpp. NOT yet run: the Mac build,
ctest (studio_human_appearance_session, vivid_human_wardrobe, ...) and visual checks in Studio — next step.
Known gaps: try-on assembly cost on big outfits is unmeasured; brush circle size is an estimate (face camera);
thumbnails need the character shown in the viewport.

## 2026-11-21 (later) — Character Creator second pass + main merge

Merged origin/main (85 commits; Studio workspaces) — added a "Character" workspace (⌘7) and kept main's async project
open (last project saved there). Added: MCP `studio_character_snapshot` (file handshake with the running Studio),
makeup layers (`makeup.*`), hairstyle library + shared outfit try-on, age control, crowd variants, model animation
clips in "Поза и свет", index-only LODs for skinned characters at build (`HumanLod.cpp`), reference photo overlay,
`.aivchar` packages, height in cm + ruler.

Tests actually run (Linux VM): tsc clean; web `node --test` 137/137; `tests/vivid/human_lod.cpp` compiled with
meshoptimizer and RUN — passes; g++ -fsyntax-only of StudioSession.cpp, StudioMetalScene.cpp, StudioFacialGpuPreview.cpp,
HumanMaterials.cpp, HumanCreatorOps.cpp, StudioHumanLookSessionTests.cpp. Not run: Mac build, ctest, Studio visuals.
Known gaps: makeup positions are UV estimates (artist aligns once per model); hairstyles need models skinned to the
same head bone; no retargeting of external animation files (only clips inside the character glTF).
Follow-up (same day): makeup auto-placement from face geometry, hairstyle rig detection (shared skeleton vs head-bone
attach), external animation clips via `<model>.clips.json`. Verified: tsc clean, g++ -fsyntax-only of the touched C++.
Still needs the Mac build + ctest + visual check. External clips need the same bone names as the character.

## 2026-11-21 (night) — MetaHuman adapter

`vivid::adaptMetaHuman` + Studio card + MCP `human_metahuman_check`. Mac build green; ctest 9/9 PASS incl.
`vivid_metahuman_adapter` against the real local Ada and Bruce-Full exports. Not supported (stated in the report):
RigLogic correctives, FACIAL joint animation, strand groom, .dna at runtime, Draco GLB.

## 2026-11-23 — Ada generated walk preview adapter

Implementation commit `f1244cec3` on
`feature/motionweave-walk-generator-current` (based on `7351e960b`, not the
VIVID feature branch). `HumanWalkPreview.h` is a presentation-only mapping
from modular human skin rigs to the shared MotionWeave walk generator; it
does not own facial animation, simulation state or a second pose writer.
Creator's `--preview-walk` offline capture path evaluates that clip on Ada.

Actual checks: AppleClang 17 native Creator build PASS; generated-walk CTest
1/1 PASS; 48/48 real Metal frames from the external corrected
`Ada-Groom-Bound` manifest, `actor=MODEL_MORPH`, no pipeline failures,
encoded as a 2-second 1500x920 H.264 MP4 with AVFoundation. Inspected frames
6 and 36. Artifacts and exact limits are in
`docs/agent-handoffs/animation-walk-generator-2026-11-23.md`. The earlier
`Ada-Motion-Review` capture had detached groom and is rejected. No Ada
binary was committed or changed. Full VIVID validation, interactive Studio
and other GPU backends were not run. Next: improve foot contact and repeat
visual/trajectory acceptance before integrating with catalog assets.

## 2026-11-23 — Ada A-pose and head motion in generated walk

Source commit `0865842d6` on `feature/motionweave-walk-generator-current`.
The shared walk generator classifies the bind shoulder-to-elbow direction as
Relaxed/A/T/Unknown, lowers A/T arms and writes separate neck/head yaw/pitch
channels. `HumanWalkPreview.h` maps these joints across modular skins and
reports classifications. The native `Ada-Groom-Bound` diagnostic found four
complete rigs and eight A-pose arms, no T/relaxed/unknown arms, plus nine
head/neck channels. The external asset was not changed.

Actually run on macOS: native Creator and focused MotionWeave test built;
CTest 1/1 PASS; Metal Creator captured 48/48 frames with no pipeline failures;
AVFoundation encoded a 2-second video. Frames 0 and 6 were inspected and show
lowered arms in the walking capture. Artifacts, commands, and remaining gait
limitations are recorded in the MotionWeave walk handoff above. The head
motion is intentionally small; gaze tracking and planted feet are not done.

