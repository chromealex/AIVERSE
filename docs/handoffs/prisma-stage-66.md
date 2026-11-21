# PRISMA Stage 66 — native validation run initiated (results pending)

Date: 2026-11-19
Repo: chromealex/AIVERSE
Branch: feature/prisma-postfx
Draft PR: https://github.com/chromealex/AIVERSE/pull/26
PR base: feature/universal-entity-inspector-fps
Previous: docs/handoffs/prisma-stage-65.md
Validation workflow: .github/workflows/prisma-final-validation.yml
First accepted run (obsolete, cancelled): https://github.com/chromealex/AIVERSE/actions/runs/44595169973
Current run: https://github.com/chromealex/AIVERSE/actions/runs/44595377866
Current tested SHA: 96da88d098307f10d7745af6b2f880e35d918f9e
Latest docs/skill commit before handoff: add7583c773f9ae7affa7b5eb48202f1fa30d034
main untouched. No merges or force-pushes.

## 1. What was actually implemented in Stage 66

1. `bf54aae08fd1f3c1c8404999ab872029b076e021` —
   **new** `.github/workflows/prisma-final-validation.yml`,
   GitHub Actions workflow triggered by PRISMA source/workflow
   pushes to `feature/prisma-postfx`, or workflow_dispatch.
   Least-privilege contents:read. Two independent jobs:
   `ubuntu-24.04` (X11/EGL/GLES/Vulkan/Mesa/Xvfb installed)
   and `macos-15`. Each checks out source+submodules,
   configures desktop editor/tests/HDR scene Release,
   builds ALL default native targets once, then runs ONE
   full CTest invocation and attempts the available
   GL/HDR fixture with a 240-second timeout.
   The runner always uploads configure/build/CTest/GPU logs,
   possible HDR PPM image and CTest LastTest log.
   Existing source, project simulation and test APIs
   were not modified to create this workflow.
2. `96da88d098307f10d7745af6b2f880e35d918f9e` —
   `tests/render/prisma_volume.cpp`, extend the existing
   GPU-free Prisma volume/settings test to check
   nonnumeric RGB data, strict parser throwing and
   non-strict default preservation, NaN/Inf native
   authored values and truncated vector recovery,
   huge finite native value clamping, extra parameter
   truncation and native bool 0/1 normalization.
   Reuses existing CMake/CTest `prisma_volume`
   registration; no new target or runtime owner.
3. `c9123d9ef65452e13263c2694aaaf6f2dd723595` —
   `docs/prisma.md`, Stage-66 workflow invocation,
   test details, queued status and honest limitations.
4. `add7583c773f9ae7affa7b5eb48202f1fa30d034` —
   `skills/visuals/aiverse-prisma/SKILL.md`, pending
   CI/runner log investigation contract, accurate
   reporting and next validation responsibilities.

This is the handoff-only commit following source/test,
architecture and skill updates. Read latest feature
branch SHA before starting a new stage.

## 2. Actual CI invocation and observed status

- A GitHub Actions `push` event created run
  `44595169973` for workflow commit bf54aae.
- Adding the actual GPU-free numeric tests caused
  `concurrency.cancel-in-progress` to cancel that
  old run; it is NOT an executed test pass.
- GitHub Actions accepted current run
  `44595377866` for commit `96da88d09830`.
  The two jobs are:
  `135812125320` PRISMA full CMake + CTest (ubuntu-24.04)
  `135812125653` PRISMA full CMake + CTest (macos-15).
- **Observed status:** both jobs `queued`, no
  `started_at` or `conclusion` yet at the
  last API check. The run is not PASS/FAIL.
- Attempted native `git ls-remote` in the assistant
  Linux sandbox, but its network cannot resolve
  github.com (DNS failure). No local AIVERSE
  checkout or physical Mac Studio GPU is mounted
  in the assistant's container. Therefore no
  direct native build was attempted successfully.

**NOT RUN / NOT YET OBSERVED:** CMake configure/build,
clang/Obj-C++/MSL/GLSL compilation, CTest, GPU HDR
fixture, pixel capture/parity, resource lifetime,
perf and GPU fence checks. A queued job is ONLY
evidence that GitHub accepted the work request.

## 3. Next agent must read the REAL workflow results

1. Fetch `https://github.com/chromealex/AIVERSE/actions/runs/44595377866`
   / GitHub REST
   `GET /repos/chromealex/AIVERSE/actions/runs/44595377866`.
2. List jobs of run ID `44595377866`. If `completed`,
   fetch its jobs/steps and precise job logs and artifacts.
   Distinguish `success`, `failure`, `cancelled`,
   `skipped` and `queued` without guessing.
3. Report actual CMake command/exit status, compile
   diagnostics, total CTest passed/failed, HDR fixture
   invocation/return code and whether an HDR PPM
   artifact was captured. macOS GitHub OpenGL is
   NOT physical Apple Silicon Metal validation.
4. Fix actual failures on `feature/prisma-postfx`
   without changing the original base/main and
   preserve separate agent handoff after each
   bounded fix. Source file pushes retrigger the
   single workflow automatically; avoid CI runs
   caused only by documentation changes.
5. If GitHub runners remain queued, do not claim
   testing succeeded. Possible causes include
   insufficient runner capacity, GitHub Actions
   settings or billing; investigate from the
   GitHub UI rather than inventing a conclusion.
6. As soon as CI has real output, UPDATE this
   Stage-66 handoff with actual PASS/FAIL and
   any fixed blockers. Keep all backend-specific
   visual gaps marked unverified until tested.

## 4. Consolidated commands already in the workflow

```bash
cmake -S . -B build/prisma-final \
  -DCMAKE_BUILD_TYPE=Release \
  -DAIVERSE_BUILD_TESTS=ON \
  -DAIVERSE_BUILD_HDR_TESTS=ON \
  -DAIVERSE_BUILD_EDITOR=ON
cmake --build build/prisma-final --parallel 4
ctest --test-dir build/prisma-final --output-on-failure -j 4
```

Mac/Linux additionally attempt their supported
`aiverse_hdr_gl` / `aiverse_hdr_scene` test
executables after the full CTest. These native
fixture invocations only execute when compiled
and actually present; a missing fixture is
logged `NOT RUN`, not `PASS`.

The `docs/prisma-feature-closure-stage-65.md` matrix
remains the full inventory of 19 GPU-supported
effect pathways requiring source/visual checks.
The most important smoke gaps: Shader Graph
4-texture TEX9 vs graphless TEX8, AlphaCutout
reactive skin/morph/baked, Actor Reveal current
pose, HDR DoF and exposure/Camera Cut, per-view
GPU fenced target retirement.

## 5. Local Mac worktree fallback (only if CI cannot run)

```bash
cd ~/Projects/AIVERSE
git fetch origin
git worktree add ../AIVERSE-prisma-validation origin/feature/prisma-postfx
cd ../AIVERSE-prisma-validation
cmake -S . -B build/prisma-final \
  -DCMAKE_BUILD_TYPE=Release \
  -DAIVERSE_BUILD_TESTS=ON \
  -DAIVERSE_BUILD_HDR_TESTS=ON \
  -DAIVERSE_BUILD_EDITOR=ON
cmake --build build/prisma-final --parallel 4
ctest --test-dir build/prisma-final --output-on-failure -j 4
```

Only the host Mac can verify real Apple Silicon Metal
image/readback/fences under its actual GPU, and platform
GLES/Vulkan/WebGL must be tested when runners/devices
are available. Never claim these happened based on
a successful Linux or hosted macOS CTest.

## Stage 66 continuation — compiler-backed source regression tests

**Additional source commit:** `aa262d6b44546985429c0cda0abf0338f270c693`,
`tests/render/prisma_graph.cpp`.

Added two functions to the existing registered `prisma_graph`
CTest executable (already declared in CMake; no new build target):

- `reactiveShaderVariantContract()` invokes
  `reactive_shaders::animatedVertexGlsl` and
  `animatedMsl` with the actual unchanged
  `motion_shaders::kDeformed* / kBaked*` source
  (skinned/morph + baked × GLSL + MSL), checking
  all FOUR generated variants, current-deformed
  coordinates, UV, vertex-alpha, preserved prior
  motion output, and fail-closed behavior when
  template source anchors do not match. It also
  checks animated reactive alpha-cutout, dissolve,
  and Actor Reveal fragment source presence.
- `compositeTextureLayoutContract()` exercises
  the real C++ `PrismaCompositeTextureLayout::plan`
  for 0–4 Shader Graph inputs both with and
  without GPU Auto Exposure, including the
  TEX8/TEX9 transition, 9/10 bound-slot count
  and rejection of 5 input textures.

**Actually performed for this continuation:** GitHub
readback of the test source and **11/11 static
source-contract checks** of the committed test.
These are NOT the CTest results. GitHub accepted
a new push workflow run
[44597082684](https://github.com/chromealex/AIVERSE/actions/runs/44597082684)
for `aa262d6b44546985429c0cda0abf0338f270c693`.
The prior run `44595377866` was **cancelled**
automatically when source tests were added.
The newest run was **queued** at the last observed
check. No native C++ compile or GPU run can be
claimed PASS at this point.

The next agent MUST check the **latest** run ID,
rather than relying on the originally queued one
in section 2 above. The Linux/macOS runners have
not begun running at the time recorded. Do not
automatically retry or cancel these jobs merely
to refresh documentation; no further source pushes
are necessary until actual compiler feedback arrives.


## Source-only closure / latest docs reconciliation

Subsequent documentation-only commits after the additional
`prisma_graph` test:
- `be9fb1b272efcc64089670de44feff462a6cd8c0` —
  this Stage-66 handoff updated to reference latest queued
  CI run and regression tests.
- `19b11febe3af0e06fafd62499bb6cc2a38914d91` —
  `skills/visuals/aiverse-prisma/SKILL.md` updated the
  authoritative latest run and shader-contract guidance.
- `2bb1582110ea8b187be7cc62ee3e0693371dd992` —
  `docs/prisma.md` no longer incorrectly labels the
  earlier cancelled workflow as the current run.

GitHub returned the current source-test workflow
`44597082684` as QUEUED, with Linux job
`135818178734` and macOS job `135818179217`
both QUEUED and no native test output. No further source
writes were made after the test commit, so this
documentation-only handoff update should not supersede
the queued source-tested SHA. Do not interpret tests
in source or queued runners as a native PASS. Next
action remains: inspect exact run job statuses,
logs and artifact outputs, then fix actual blockers.

## 2026-11-20 — physical sky/fog main integration checkpoint

User-authorized integration code `1397dfb52f163bb234b10692411bfe6d3311c8aa` on
`codex/physical-sky-main` adds opt-in atmosphere/fog to canonical PRISMA, preserving
legacy shims. See [dedicated handoff](prisma-stage-physical-sky-main-2026-11-20.md)
for exact source scope, main Metal brace repair, 7/7 targeted CTests, 54/54 Metal
HDR probes and real Metal/GL captures. These local results do not assert outcomes
for the historical CI runs above or the complete repository suite.


## 2026-11-20 — lighting integration Stage 21

2026-11-20, integration/lighting-main-stage1-20261120.
Code 966963f676658ba3dc75c3ef0760e6c2eb7741a2.

Added Luma screen direct/denoise/GGX runtime, bounded quality policy, and optional
Renderer3D material/normal receiver hooks. New header APIs are additive; no save
format/default changes. Capture passes suppress profiling/ICB/debug output, use
PBR/graph material evaluation and independent depth. Legacy direct ownership
conflicts prevent composition. Fences cover compute and graphics texture readers.
Validated BVH before upload and checked actual dispatch encoding. HDR composition
uses premultiplied RGB/zero alpha, because generic Additive uses source alpha.

Build aiverse_hdr_scene/aiverse_hdr_gl on KINGSTON: PASS. Initial compilation
failed missing GiGpuBvhMetalShader, nested default initialization and assigning
C arrays; fixed. First actual pixel run found black output from alpha-zero
Additive: fixed and retested. Stage21 logs under KINGSTON build logs directory.
run-tests.sh --no-build --build-dir /Volumes/KINGSTON/AIVERSE/Builds/.temp/lighting-completion-20261120/build --filter '^lighting_(luma|graph_gi|dynamic_gi|terrain_gi|compute_gi)':
11/11 PASS, report 20261120-150547-51961. Luma nine analytic RGB cases passed:
128 sources/cluster overflow, off, deletion, metallic diffuse suppression, legacy
conflict, dielectric GGX, actual BVH shadow, geometry restoration, metallic GGX.
Existing Metal/GL GI model/terrain/graph acceptance remains PASS. diff --check PASS.
CI/push N/A. Full quick suite/editor/client, arm64 Release and mobile/Vulkan Luma
NOT RUN at this stage. No performance claim. Texture sources and normal maps
reuse the shader path but broad authored scenes need further visual acceptance.

Read docs/luma-native-lighting.md for host contract. Busy changed views can miss
Luma contribution; native host must address this before continuous lighting claims.
Luma host/editor adoption, animation GI, ICB GI, Release and platform acceptance
remain. Next: native Luma host/source ownership, followed by the remaining user
scope. Do not stop after this intermediate stage. All builds on KINGSTON.


## 2026-11-20 native Luma host

2026-11-20, integration/lighting-main-stage1-20261120; code 6839eafb7fa0589a0336ab4004b234e8985ff05f.
Added apps/renderbench/LumaHost.h, Bench integration, recipe docs and real-glTF
acceptance. Additive optional luma JSON key: backend auto/gpu, quality
balanced/high/ultra; legacy settings unchanged. One direct owner, explicit
fallback for unsupported backend/capacity, rejection of duplicate static/baked
ownership. GI independent. Source colors converted back to linear. Static
geometry captured before upload. Native host synchronizes previously committed
GPU work so moving cameras never silently omit Luma. Each view requires actual
composition or reports failure. Synchronization cost is included, not a speedup.

Built aiverse_renderbench/aiverse_renderbench_gl on KINGSTON, PASS.
run-tests.sh --no-build --build-dir /Volumes/KINGSTON/AIVERSE/Builds/.temp/lighting-completion-20261120/build --filter '^lighting_(luma_host|native_host)':
4/4 PASS, report 20261120-150855-58200. Real glTF, source on/off, moving camera,
legacy conflict rejection, Metal GPU and desktop GL fallback; existing GI host
regression PASS. git diff --check PASS. No CI/push.

Release arm64 configured successfully in KINGSTON root/release-arm64; first
renderbench/hdr_scene build RUNNING, not yet validated. Native session72016,
logs/release-arm64-build.log. Preserve this build; do not spawn a duplicate.
No editor/client/mobile/Vulkan hardware or production performance PASS implied.
Current static native host excludes animated casters; next stage: generic
animated pose geometry snapshots and automatic host publication. Then editor
settings/UI, ICB and remaining platform/performance gates. User asked finish ALL,
not just this stage; continue. All build/temp/artifacts on KINGSTON.


## 2026-11-20 animated native Luma/GI host

2026-11-20, integration/lighting-main-stage1-20261120; code 0e5e06a78a3acea2e06c7cf0f875828a8d7a9b4d.
Shared immutable AnimatedLightingGeometry snapshots retain unchanged static
chunks and replace animated opaque chunks, using the exact sampled renderer pose.
Opt-in animated assets retain CPU arrays. Both DynamicGiHost and LumaHost receive
monotonic revisions behind actual GPU fences. Capacity failures reject the view,
not silently discard actors. Original bake:false exclusions remain.

Native verification holds the current pose until GI converges. This is a
correctness-first cadence, not real-time animation performance. Report exposes
lightingCadence, lightingGeometryRevision, lightingSnapshotTriangles. Existing
submitted-frame measurements exclude convergence-only attempts. Save format
unchanged except additive report fields; existing static scenes unchanged.

KINGSTON build both renderbenches PASS. run-tests.sh --no-build --build-dir
/Volumes/KINGSTON/AIVERSE/Builds/.temp/lighting-completion-20261120/build --filter
'^lighting_(animated_host|native_host|luma_host)': 6/6 PASS, report
20261120-151917-68506. Real animated glTF caster blocks/restores Luma and CPU/GPU
GI advances revisions for live poses; desktop GL fallback/native regression PASS.
Initial narrow-shadow check (<3/255) failed at red3.3125 vs open54.75. Removing
bloom did not change this. The low-resolution denoiser softens narrow boundaries;
acceptance now explicitly requires >90% suppression and restoration. Stage21
broad-wall analytic zero test remains separate. Do not hide this quality limit.
CI/push N/A, diff --check PASS. Release animation/editor/mobile/Vulkan NOT RUN.
No active build sessions. Next: integrate existing committed Studio HTML5 into
THIS lighting branch (root checkout has unrelated dirty changes; preserve it),
then lighting settings/UI/native renderer, ICB and platform/performance checks.
User requested ALL; continue, don't stop at this intermediate stage.

# Lighting Stage 26 — persistent Studio settings

2026-11-20, integration/lighting-main-stage1-20261120.
Code 72cac2dbe3b8458ec80ff2b463679f92c5949da7.
Shared RenderLightingSettings v1 validates modes and bounded numeric values.
Native studio.renderLighting/get and Set preserve RTS extra/FPS full documents,
validate before mutation, participate in Save/Undo/Redo. Typed bridge controls
use Studio UI v1. Existing maps default off/legacy; additive optional root
renderLighting block, no gameplay authority or GPU state serialized.

Actual checks with TMPDIR/build/bundle/logs on KINGSTON:
- cmake configure and build aiverse_studio + aiverse_studio_render_lighting_tests PASS.
- ctest -R '^studio_render_lighting$' PASS: real RTS/FPS invalid-patch atomicity,
  undo/redo/save/reopen/disk serialization. Initial fixtures lacked RTS types,
  FPS rules.fps and player; fixed fixtures to satisfy real host validation.
- npm --prefix apps/studio-web run build -- --outDir external studio-web-dist PASS
  (includes tsc --noEmit); initial mock generic type corrected.
- npm --prefix apps/studio-web run test:web PASS (57 tests).
- Python unittest discover tools/studio test_*.py with external bundle: 19/19 PASS.
- git diff --check PASS; CI N/A, no push. Native screenshots NOT RUN.

This stage is authoring persistence only. Viewport consumption comes NEXT;
controls must not be represented as completed live GI/Luma yet. Continue with
shared runtime ownership and PBR preview geometry, real backend status,
native pixels/screenshots, then ICB and remaining platform/performance gates.

# Lighting Stage 27 — live Studio Metal preview

2026-11-20, integration/lighting-main-stage1-20261120.
Code 5bcc70c846e4d66910f7b16248937387d9d94712.
DynamicGiHost/LumaHost moved to engine/render; bench aliases preserve callers.
RenderLightingRuntime composes the same ownership/fence policy for native hosts.
Studio consumes renderLighting, rebuilds immutable PBR preview geometry on document
revision/project changes, binds GI, composes Luma before PRISMA HDR, and reports
actual backend/convergence/fallback. GPU resource retirement precedes release;
frame scope closes recording on exceptions. Material preview clears GI overlay.
RTS/FPS document proxy geometry remains a PREVIEW (terrain/proxies, not full game
assets/animation). Direct capacity fallback is explicit, forced GPU GI rejects.
Authored source adapter optional limit avoids truncating Luma to legacy1024;
standard LightSystem capacity unchanged. No ECS authority or save ABI changes.

All builds, temp, captures and logs under KINGSTON lighting-completion-20261120.
- Debug aiverse_studio, aiverse_renderbench/gl, aiverse_studio_metal_lighting_tests
  and authored-light test build PASS. Initial pointer API and test entrypoint
  compile/link errors fixed (Model*, aiverseMain).
- ctest '^lighting_(native_host|luma_host|animated_host)': 6/6 PASS Metal/GL.
- ctest '^studio_metal_lighting$': PASS. Actual CAMetalLayer/StudioMetalScene
  CPU GI, GPU GI, GI off with Luma, removal of sources and disabling. Captures
  cpu/gpu/off/unlit.ppm in build/apps/studio-native/metal-lighting-captures.
  Source removal changed3699pixels, total RGB delta46014; pipelines31 failures0.
  gpu.png converted/viewed: terrain grid and light gizmo visible, no blank frame.
- ctest '^studio_(render_lighting|authored_lights)$': 2/2 PASS, including1500sources.
- npm typecheck and external bundle build PASS; Python contracts19/19 PASS.
- diff --check PASS, CI N/A, no push. Native HTML controls screenshot NOT RUN:
  CUA reports Mac locked, automatic unlock failed, browser native pipe unavailable.
  Asked user asynchronously to unlock; renderer fixture runs independently.

Known scope: preview shapes only, revision rebuild synchronous; no realtime
animated Studio assets or production-performance claim. Shared animated model
GI already tested in Stage24 renderbench. GPU/CPU automatic capacity routing
retains complete geometry; low-res Luma shadow denoise quality limit remains.
Next: ICB GI ABI and actual submissions, Releasearm64 current tree performance,
full quick/editor/client gate, Vulkan/mobile hardware and UI screenshot when
available. User asked ALL; continue rather than stop at this checkpoint.

# Lighting Stage 28 — Metal dynamic GI ICB ABI

2026-11-20, integration/lighting-main-stage1-20261120.
Code 8b2d945d4a04ab6d891bd837ee7acf2616a5605d.
ICB PBR argument buffer conditionally owns dynamicGI texture13 at argument26;
GiDynamicU remains inherited uniformU6/buffer7. Metal CPU/GPU ICB owners bind
and declare the atlas as an indirect fragment reader, without requiring an
unused sampler. Remove GI blanket GPU-authored fallback; other graph/deformation/
baked-GI eligibility checks remain. Ordinary PBR/graph sampler ABI unchanged.
All experimental ICB flags stay OFF by default. No performance claim or general
Metal ICB production acceptance implied by this lighting fixture.

Actual validation on KINGSTON:
- Debug aiverse_hdr_scene/gl built PASS (initial test used wrong ModelDraw field
  transform; corrected to world).
- run-tests --no-build --build-dir <KINGSTON>/lighting-completion-20261120/build
  --filter '^lighting_(icb.*|dynamic_gi_gpu|dynamic_gi_gl|compute_gi_gpu|graph_gi_gpu|graph_gi_gl|terrain_gi_gpu|terrain_gi_gl|luma_gpu)$':
  10/10 PASS, report20261120-154538-96530.
- Both new ICB tests assert actual gpuEncodedSubmissions>0 and commands>=2,
  plus six analytic pixel cases (attach, detach, lowquality, AO, light removal,
  restoration). CPU and GPU transport providers both PASS. Ordinary Metal/GL
  PBR/graph/terrain and Luma pixels PASS. Experimental flags confined to tests.
- diff --check PASS, CI N/A/no push. Release/other hardware NOT RUN this stage.

Next: full quick native suite and editor/client builds, Releasearm64 current
source and Sponza timings, platform gates. UI screenshot remains blocked by
locked Mac; user asked asynchronously to unlock. Do not mark all complete yet.


## Lighting Stage 29 — verification checkpoint

2026-11-20; branch integration/lighting-main-stage1-20261120.
Implementation: 6ce5b19c061ec16a450f60bc475e0e9f6e6674fe.

Restored two merged legacy SpectraFX preview calls to the existing shared
ViewportGpuRetirement API (GPU Assets and Graph windows). No VFX behavior or
new ImGui UI. Repaired Studio UI skill frontmatter, escaped Markdown and editor
workflow/category registration; regenerated skills/catalog.json. Added opt-in
Release arm64 OFF/ON/OFF lighting acceptance runner and updated lighting docs
and AGENTS checkpoint. No save-format or public rendering API changes.

All builds, dependencies, temporary data and logs reside at
/Volumes/KINGSTON/AIVERSE/Builds/.temp/lighting-completion-20261120.
Actual checks:
- Release arm64 aiverse_renderbench, aiverse_hdr_scene, aiverse_studio,
  aiverse_studio_metal_lighting_tests and aiverse_client builds PASS.
- Release CTest lighting GPU/host/ICB/animated selection: 10/10 PASS (27.87s),
  logs/stage29-release-gpu.log and .xml.
- Release studio_metal_lighting: 1/1 PASS (4.95s), stage29-release-studio.log/xml.
- Debug full build including AiverseEditor PASS after fixing the two calls.
- Full ./run-tests.sh --quick --build-dir <root>/build --jobs 12 --ctest-jobs 2
  still RUNNING at handoff, report 20261120-155447-18337, session 24647.
  Main aiverse_tests passed in 840.79s. Three catalog/routing/audit tests failed
  before the metadata fix; rerun through CTest after completion is required.
- catalog.py generate/check, tests/skills/catalog_test.py, tools/skills/audit.py
  PASS after fix (227 skills, zero audit errors/warnings). Python syntax check
  of acceptance runner PASS; performance runner NOT EXECUTED yet.
- Earlier own low-parallel build processes were terminated and resumed at 12
  jobs; first resumed Debug build failed on the two Viewport calls, then fixed.
  Interrupted/failed attempts are not passes. Logs stage29-quick*.log and
  stage29-release*.log preserve evidence.
- git diff --check PASS. CI N/A, no push.

Locked Mac still blocks interactive HTML/native screenshot QA. User confirms no
Android/Vulkan hardware stand presently: hardware acceptance NOT RUN. Native
Metal capture tests work independently. Experimental ICB stays OFF by default;
Studio geometry remains terrain/proxy preview, not full game asset parity.
Next: fix FPS preview/GI bounds and camera using actual geometry rather than
MapDoc default RTS dimensions, verify actual Metal; then consolidate full-suite
results and run isolated Release Sponza OFF/ON/OFF acceptance.


# Lighting Stage 30 — FPS bounds and material status

2026-11-20, integration/lighting-main-stage1-20261120.
Code: 7f0bbd633228d4dc27248f4da82d670044a05c42.

StudioLightingPreview now derives FPS ground and model bounds from authored
proxy geometry, including negative coordinates, instead of the unrelated
MapDoc 64x48 RTS default. Empty FPS preview has an origin-centered floor.
StudioMetalScene uses that model even with GI disabled, frames its actual
bounds, excludes the RTS grid, and reports material preview GI as off.
RTS behavior and renderLighting v1 save/public API remain unchanged.
No full game-asset parity is claimed for this terrain/proxy preview.

Actually executed, all artifacts/builds on KINGSTON under
AIVERSE/Builds/.temp/lighting-completion-20261120:
- Debug aiverse_studio + aiverse_studio_metal_lighting_tests build PASS.
- Debug studio_metal_lighting/render_lighting/authored_lights/document_visuals:
  4/4 PASS, stage30-tests.log. Actual Metal test verifies negative-coordinate
  FPS GPU convergence, Luma activation, center-screen picking into authored
  scene, material-preview status and return-to-scene. Existing RTS pixels and
  CPU/GPU modes still pass.
- Release arm64 same targets build PASS; studio_metal_lighting 1/1 PASS,
  stage30-release-build.log and stage30-release-tests.log.
- Stage29 full quick completed: 313/316 PASS, three catalog tests initially
  failed. After committed metadata fix CTest skill_catalog/routing/audit 3/3
  PASS (stage29-catalog-retest.log). Main engine test PASS in 840.79s; full
  suite 998.66s. This is full baseline plus focused reruns, not one clean run.
- git diff --check PASS. CI N/A; no push.

Remaining external gates: locked Mac prevents interactive HTML/native UI
screenshots; user confirms no Android/Vulkan stand. Those are NOT RUN.
Next: isolated Release arm64 Sponza OFF/ON/OFF benchmark and final evidence
consolidation. Existing realtime-animation/convergence and preview limitations
remain; ICB experimental defaults stay OFF.


# Lighting Stage 31 — local acceptance consolidation

2026-11-20, integration/lighting-main-stage1-20261120.
Evidence/documentation commit: 31bae97ba0e57c3a4fb7829f9457251780dd132c.
Rendering code: 7f0bbd633228d4dc27248f4da82d670044a05c42.

See docs/lighting-acceptance-2026-11-20.md for the complete delivered scope,
exact build/test evidence, timing tables, artifact locations and remaining
external acceptance. No renderer/public API/save changes in this stage.

Actually executed on KINGSTON:
- tools/renderbench/lighting_release_acceptance.py --binary
  <root>/release-arm64/aiverse_renderbench --scene
  <root>/build/sponza-dynamic-gi/scene-on.json --output
  <root>/release-sponza-aba: PASS, all three complete Release arm64 reports,
  real GPU timestamps, 120 frames each. Full opaque geometry CPU fallback,
  256.140ms initial convergence. B-on.png converted with sips and inspected.
- Same runner --scene <root>/release-luma-720p.json --feature luma --output
  <root>/release-luma-aba: PASS, 128-source GPU Luma, 720p, three complete
  120-frame reports. A four-triangle fixture, not a production scene.
- root=/Volumes/KINGSTON/AIVERSE/Builds/.temp/lighting-completion-20261120.
  Logs stage31-sponza-aba.log and stage31-luma-aba.log, summary.json and
  measurement-context.json in each output directory.
- A concurrent lens-completion build was detected during measurements. Left
  untouched. Timings are exploratory, NOT isolated performance acceptance.
- git diff --check PASS; CI N/A/no push. All previous Stage29 failures resolved
  by focused reruns; final Stage30 Debug/Release tests passed.

No Android/Vulkan device exists per user's reply; hardware parity NOT RUN.
Interactive UI screenshots remain blocked by locked Mac; native Metal captures
are independent and passed. Do not re-ask for unavailable hardware. Do not
claim full gameplay asset adoption, realtime animated cadence or production
performance. Shared runtime, proxy Studio integration and documented fallbacks
are complete for the locally verified scope; experimental defaults remain OFF.

Next actionable external acceptance: unlocked Studio interaction and screenshots,
Android/Vulkan device rendering when available, isolated representative gameplay
CPU/GPU measurements before production defaults. No merge/push requested.


# Lighting Stage 32 — live Studio smoke after unlock

2026-11-20; integration/lighting-main-stage1-20261120.
Evidence update commit: ad6ba1e9d2819d27b9040ba15ed790e0bb7bd3a7.
No code/API/save-format changes. Existing Release arm64 app from Stage30 used.

User confirmed Mac unlocked. CUA successfully opened the KINGSTON Release
AiverseStudioHTML5.app and native system Open Project dialog. Opened isolated
build/apps/studio-native/render-lighting-fixture/templates/rts fixture. No user
production project edited. Browser native pipe remains unavailable, but native
app accessibility and screenshots work.

Actual UI checks PASS:
- Empty project view, disabled actions, then native project open.
- Lighting panel Auto GPU/CPU + Luma reported actual GI GPU; native terrain
  visible in the combined WKWebView/Metal window.
- Selected CPU probes: actual status became GI CPU / Direct Luma.
- Add source: count 0→1, native light gizmo visible. Undo 1→0, Redo 0→1.
- Save: disabled Save button and native saved-map console confirmation.
- GI Off: actual off status and disabled probe/ray/budget fields.
- Undo Off: CPU settings restored; transient Waiting for native viewport
  resolved to actual CPU/Luma status. Save confirmed again.
- Read-only Python disk verification: fixture map.json contains renderLighting
  v1 indirect=cpu/direct=luma and light x=4,z=4,height=1,radius=4,intensity=1.5.
- Actual 1440x928 window screenshots inspected through CUA in conversation;
  screenshots were not exported to files. Right panel scroll works; native
  viewport remains visible. No app crash or error shown.

Initial CUA keyboard typing lost characters in macOS Go To dialog; setValue
correctly entered the path. This was tool input behavior, no application fix.
No compilation or automated suites rerun for this documentation-only stage.
Prior Stage29–31 test evidence remains applicable; git diff --check PASS.
CI N/A, no push. Current fixture intentionally retains the saved UI test state.

This closes the locked-Mac blocker for the observed RTS lighting smoke, not
full editor parity, FPS interactive coverage, browser cross-platform coverage
or the exhaustive 240px/loading/error-state UI matrix. Android/Vulkan hardware
still unavailable per user; isolated production performance remains unaccepted.
Next: device acceptance and representative isolated game workload measurements
when resources become available; preserve current opt-in defaults and scope.


# Lighting Stage 33 — universal entity rule

2026-11-20; integration/lighting-main-stage1-20261120.
Code/documentation commit b77454ce4d6472a98c843f401a1f0bf54d46b614.
User explicitly requires every scene object to be an entity, including lights.
AGENTS.md now requires stable identity, component ownership and common hierarchy,
inspector, viewport selection, transform, history and persistence. Legacy adapters
must retain one canonical owner and cannot be described as a completed migration
merely because they display hierarchy rows. Presentation stays outside authority.

Same block inserted into /Users/aleksandrfeer/Projects/AIVERSE/AGENTS.md for agents
working in the main workspace; existing additional instructions preserved. Root
copy deliberately left uncommitted on its unrelated branch. Lighting branch copy
committed here. No renderer/save format changes; tests NOT RUN (instructions only).
Next: implement document light entity/component adapter with identity and common
editing, retaining existing map compatibility, then verify native/UI behavior.


# Lighting Stage 34 — Light entity and component Inspector

2026-11-20; integration/lighting-main-stage1-20261120.
Implementation: d6ddd951b521f7a10aff5ddace7ebd76032eb8c7.
User required lights to be entities, all objects to follow that rule, and then
explicitly rejected the intermediate flat Properties Inspector. Final Inspector
uses actual native Transform/Light descriptors and component-targeted writes;
name belongs to the entity. See docs/studio-light-entities.md.

Changed paths: editor EditorEntityAdapter/EditorSceneNodes, core MapDoc/LightOps
and new DocumentLightIdentity; Studio document IDs/visuals/lighting geometry,
Session and MetalScene; native render-lighting test; web App/types/LightingPanel,
StudioPanelRenderer/lightingSchema. No new ImGui UI or simulation ECS float state.

Compatibility and ownership:
- lights[] is the sole canonical legacy payload; additive entityId metadata.
  Old files load with deterministic legacy IDs; new additions get unique IDs;
  duplicate IDs are repaired. IDs persist through save/reopen and deletion of
  preceding elements. No parallel mutable light state.
- Generic document Transform/Light read/write, native selection and viewport
  pick handles. Handles excluded from GI geometry and opaque draws.
- Inspector snapshot components + entityProperties, optional componentId on
  setProperty; reject ownership mismatch. Light update/delete optionally resolve
  entityId before legacy index. Old callers retain compatibility.
- Lighting/Hierarchy/Inspector/viewport share entity selection. FPS mutations
  validate whole document through fps_set; RTS uses existing MapDoc history.
- Global all-object mandate committed in Stage33 and copied to root AGENTS.md;
  root copy remains uncommitted on its unrelated branch, other changes preserved.

Actually executed (all builds/temp/artifacts on KINGSTON root
/Volumes/KINGSTON/AIVERSE/Builds/.temp/lighting-completion-20261120):
- Debug and Release arm64 aiverse_studio, aiverse_studio_render_lighting_tests,
  aiverse_studio_metal_lighting_tests builds PASS. Final component changes rebuilt
  Studio/session/render-lighting targets. Legacy aiverse_editor final build PASS.
- Entity phase Debug studio_render_lighting + studio_metal_lighting 2/2 PASS;
  final stable-ID regression selection render_lighting/session/fps_lighting_document/
  document_visuals/scene_nodes 5/5 PASS. Release render_lighting + Metal 2/2 PASS.
- After component correction: Debug render_lighting/session 2/2 PASS and Release
  render_lighting 1/1 PASS. Tests exercise stable identity, deletion/reindexing,
  stale ordinal resolution by ID, deleted-ID rejection, canonical writes,
  validation, Undo/Redo, save/reopen, shared transform and component ownership.
- npm --prefix apps/studio-web run build -- --outDir <root>/studio-web-dist PASS
  (includes tsc --noEmit); final test:web 57/57 PASS.
- AIVERSE_STUDIO_WEB_DIST=<root>/studio-web-dist python3 -m unittest discover
  -s tools/studio -p 'test_*.py' -v: final 19/19 PASS. First invocation without
  external bundle env skipped one check; corrected invocation has no skips.
- Logs stage34-*.log, particularly components-{web,build,tests,release-build,
  release-tests,ui-contracts,web-tests}, legacy-editor-build, final-tests,
  release-tests. git diff --check PASS; no CI/push.

Actual CUA Release WKWebView verification on unlocked Mac:
- Light appears in Hierarchy and Inspector with persisted entity ID.
- Rename propagated to Hierarchy and Lighting; shared selected source tracked
  selecting/deselecting by native viewport pick. Highlight follows source.
- User flagged flat intermediate UI; final relaunched app shows Name outside
  components, TRANSFORM and LIGHT headings, no flat Properties block for light.
- Component Light intensity 7→6 and Transform X 4→6 edited successfully; Undo
  restored X4, Redo restored X6; Save disabled after native commit. Native
  viewport source moved and lighting followed. Disk JSON confirmed X6/intensity6
  with unchanged entityId. Fixture remains saved on KINGSTON, no user map edited.
- Actual screenshots inspected in CUA conversation, not exported files.

Limits: this is the authoring document compatibility adapter, not a claim that
all legacy objects/gameplay ECS or old ImGui lighting selection are migrated.
Generic component attach/remove is separate. No production performance claim;
Android/Vulkan acceptance remains unavailable per user. Current opt-in lighting
settings/defaults unchanged. Next: use this component/identity path for subsequent
scene object migrations; maintain one canonical owner and tests for each type.


# Lighting Stage 35 — engine skills and MCP authoring

2026-11-20; integration/lighting-main-stage1-20261120.
Implementation commit: 84d403ad3b115d96d3555442356855e1a69f80c5.
User explicitly clarified skills must live IN THE ENGINE. Added repository
skills/editor/aiverse-scene-entities and skills/visuals/aiverse-lighting, registered
both in tools/skills/catalog.py and generated skills/catalog.json (229 skills),
linked from AGENTS.md and documented MCP usage. No global Codex skill/config,
plugin installation or external service connection was changed.

Native Workspace/MCP registry adds seven operations:
scene_entity_list/get, scene_component_set, scene_light_create/delete,
scene_render_lighting_get/set. Common document ID formatting moved into
apps/editor/DocumentEntityIdentity.h; Studio wrappers reuse it. Component edits
reuse EditorEntityAdapter, validate ownership/instance fields, stage on a copy
and commit to native history. FPS uses whole-document fps_set validation. Old
MCP names and save formats remain compatible; entityId is the Stage34 metadata.
No new renderer behavior, GPU flags or simulation components. Sources are
apps/editor/core/SceneEntityOps.h, LightOps registration and tools/mcp/main.cpp
server instructions. Generic component attach/remove is still not implemented.

Actually executed, all artifacts/builds/temp on KINGSTON under
/Volumes/KINGSTON/AIVERSE/Builds/.temp/lighting-completion-20261120:
- Debug aiversemcp, aiverse_studio_render_lighting_tests and aiverse_editor
  built PASS (stage35-validated-build.log, stage35-editor-build.log).
- python3 tests/editor/scene_entity_mcp.py --binary <root>/build/aiversemcp
  --output <root>/stage35-mcp-final-fixture PASS. Real JSON-RPC initialize,
  tools/list and tools/call; isolated --no-editor --port 0 transport. Both RTS
  and FPS: create/get/components, invalid/wrong-component patches leave state
  unchanged, Transform + Light edit, Undo/Redo, stable-ID delete, GI validation,
  Save/reopen and persisted surviving ID. stage35-mcp-final-test.log.
- CTest studio_render_lighting 1/1 PASS (2.98s), stage35-native-regression.log.
- tools/skills/catalog.py generate/check PASS (229 skills),
  tools/skills/audit.py PASS (0 errors/0 warnings),
  tests/skills/catalog_test.py PASS (routing/ownership/dependencies).
- Host skill-creator quick_validate.py attempted for both skills but could not
  run: Python3.8 and 3.12 lack PyYAML (ModuleNotFoundError). No dependency/global
  configuration installed to work around this. Engine's own validators passed.
- git diff --check PASS. No CI or push. No new Release/GPU run needed for these
  tooling-only changes; previous Stage34 GPU evidence unchanged.

Existing running editor processes were not restarted. For connected MCP use the
updated editor and server together; an old editor can reject newly listed tools.
--no-editor only disables launching and can still connect to its configured port;
--port 0 deliberately isolates the actual transport test. Settings getters report
authored configuration, not proof of backend availability. Hardware gates remain.
Next: consume the engine skills and MCP entity/component API for new authoring
work; use map_save when persistence is intended and preserve entity ownership.


## 2026-11-20 — Lighting main integration Stage 36

Merge 9611d05d5aaf299f0892b1fe2f60b935a6c5bede on main preserves VT/LENS/Vulkan contracts and adds tested
GI coexistence. See docs/agent-handoffs/lighting-main-stage36-merge-2026-11-20.md
for exact builds/tests, initial failures and remaining platform gates.

## 2026-11-20 — structured fog validation checkpoint

Code `1fd380b6fe0ad1325162bc3c0a32e5c3eb1dab53` on
`codex/structured-volumetric-fog`; [dedicated handoff](prisma-stage-structured-fog-2026-11-20.md).
PASS 7 targeted CTests, GLSL/MSL compile, Metal forest/valley/wind/reveal fixtures
and GL forest smoke. Full suite/editor/client/Vulkan/mobile/performance NOT RUN.
This is an opt-in extension of the existing PRISMA fog pass, not a new owner.

## 2026-11-20 — fog billow/shadow validation and external build storage

`19d2d8f1a2f2357b235dc9ef6dd08da70d243cb9` adds opt-in eroded 3D fog banks and
bounded sun self-shadowing in the existing PRISMA pass. PASS 7 targeted CTests,
Metal/GL fixtures and GLSL/MSL compilers. Full details and failures/limits:
[fog billows handoff](prisma-stage-fog-billows-2026-11-20.md).
User requires **all further builds on KINGSTON**, including compiler temp/cache;
use the paths/env recorded there. No new CI/full-suite/backend parity claim.


## 2026-11-20 — lighting/fog main synchronization

Merge 73c5849b9ba7ac77404cdbdcc4b4e3b129cea800 preserves peer structured fog
and Stage36 lighting. Fresh Metal HDR 54/54 and scoped integration 10/10 PASS.
See docs/agent-handoffs/lighting-main-stage37-main-sync-2026-11-20.md.
Remote push awaits explicit authorization required by automatic approval review.


Lighting publication follow-up: explicit user push authorization received.
origin/main published at 8dcb2528df7c342626af4cb073f7743e4d0b85fc and verified
with git ls-remote; prior publication blocker is resolved. See Stage37 handoff.
