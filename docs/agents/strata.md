# Agent rules: STRATA virtualized geometry: stages 12-17.4, skill and MCP authoring

Moved verbatim from AGENTS.md on 2026-11-21 (AGENTS.md keeps the cross-cutting rules; this file is
mandatory reading before working in this area). Newest entries are at the end.

## Strata virtualized geometry work (2026-11-19)

Strata development is isolated on `feature/strata-virtual-geometry`.
Before editing mesh/LOD/geometry import, GeometryScene, GPU scene, RHI or
indirect batching, read `docs/strata.md` and
`docs/agent-handoffs/strata-virtual-geometry.md`, plus the latest
`docs/metal-batching-agent-handoff.md`. Stages 2–8 build CPU meshlet/LOD assets, opt-in static
`GeometryScene` staging, per-pass CPU reference selection, auxiliary
source-attached `GlobalGeometryPool` indices and version-2 GPU metadata.
Stage 8 adds **diagnostic Metal compute culling** through
`StrataGpuCulling.h` and `GeometryScene::dispatchStrataGpuView`, default
OFF. It is **NOT a GPU render submission path**: `Renderer3D` is still
the only draw authority. Never use GPU selection buffers to draw until
real Metal shader execution + CPU oracle selection parity, overflow
checks and full screenshot parity are verified.
`GpuPassConfig::viewProjection` is VIEW PROJECTION only; each
`GpuSceneInstance::objectToWorld` already includes the source node bind
transform. `cpuReferenceView` composes the two. Treat GPU output order
as nondeterministic and any overflow/invalid flag as non-drawable.
`GpuPrimitive::gpuFirstIndex` (version 2 only) is the absolute shared
GPU index base for a primitive; `GpuNode::sourceFirstIndex` is only
primitive-local and never directly a `Model::gpuFirstIndex`.
Do not fork the underlying GPU geometry pool, ECS scene or batching
submission. GPU selection slots are explicit frame-serial scoped and
cannot be recycled until prior GPU completion. Keep all Strata staging,
GPU metadata and compute diagnostics default-OFF, including
`AIVERSE_BUILD_STRATA=OFF` portable fallback.
Coordinate with the batching agent: share a single instance/material/indirect
grouping authority, do not duplicate rendered primitives or transforms.
Stage 8.2 adds `GpuClusterCulling::requestReadback` (Metal async counters
plus <=2048 selected cluster records) and `compareGpuSelection` CPU oracle.
Output is diagnostic and must never be submitted to `Renderer3D`. Any
invalid/overflow/readback-failed status is not drawable. The real macOS
`aiverse_strata_metal_parity` fixture is manual and opt-in
(`AIVERSE_STRATA_METAL_GPU_TESTS=ON`) and requires genuine Metal shader
execution before GPU parity can be claimed. The owner-confirmed Mac Studio
run for **previous Stage 8 code** passed 5/5 CPU/mocked CTests in 1.19s;
that does not prove the newer shader or readback behavior.
Stage 9 adds `StrataRenderRouting.h/.cpp` and the additive
`GeometryScene::prepareStrataRenderCandidate` diagnostic preflight.
Batching PR #34 has merged into main; Strata synchronized its updates at
merge commit `a77cd830` without overwriting batching ownership.
`RenderRouteGate` defaults to Legacy and requires independent proof of
CPU/GPU selection parity, raster parity, source safety, residency and
Renderer3D submission ownership. Even a complete gate only prepares
commands; **no live renderer suppresses legacy ModelDraw or executes
Strata commands yet**. Do not turn it on until the actual Metal parity
runner, material and shadow screenshots, and exclusive per-primitive
ownership are verified. `encodeRendererCommands` revalidates the
current Model pointer, geometry/material revisions and live
`GpuMetadataScene` sidecar lease before preparing shared-RHI indirect
command bytes. Never use a stale candidate after source unload.
Stage 10 adds a **dry-run only** bridge into real `Renderer3D` ModelDraw
passes. `StrataRendererPreview.h/.cpp` supports pure preflight of each
rigid node/primitive in independent main-camera and sun-shadow views;
`GeometryScene::setStrataRenderPreviewEnabled(true)` is an additional
scene opt-in (in addition to CPU/GPU staging and the separate GPU culling
flag), default OFF. It invokes
`renderer.previewStrataModel(...)` synchronously and ALWAYS follows with
the original `renderer.drawModel(d)` exactly once. No Strata GPU command
or indexed draw is submitted by preview. Renderer3D counters are
diagnostic only and reset at beginScene. Never infer GPU/raster parity
from a preview-ready count; actual Metal validation and exclusive route
ownership remain mandatory before legacy suppression can be added.
Stage 11 adds opt-in `aiverse_strata_metal_raster_parity`: a
real Metal/CAMetalLayer A/B visual fixture using **the ordinary
Renderer3D model material and shadow passes**, with exclusive
per-frame Legacy or Strata source alias. The aliases borrow the same
source GPU VB and existing pooled Strata sidecar IB — no extra GPU
geometry pool and no second GPU draw path. A blank baseline must
differ from Legacy, fine/coarse raster screenshots must match Legacy
byte-for-byte for a deliberately **lossless** two-triangle hierarchy,
and only one ModelDraw is submitted per candidate frame. It is NOT
yet a production Strata routing path or a proof of real simplified
LOD crack-free visuals, water reflection parity or GPU-authored
selected-command rendering. Stage 11 has **not been run** until
native output is returned; do not conflate with the previously
confirmed real Metal compute 5/5 or focused CPU 6/6.
The Strata-focused workflow's previously malformed regex was
replaced; CI compiles both Metal fixtures on macOS, but intentionally
does not run GUI tests headlessly.
Stage 11.1 owner Mac Studio real Metal raster attempt FAILED at
`Legacy and empty screenshots identical` while GPU selection remained 5/5
PASS. Do not attribute that to geometry blindly: the test requested
`captureNextFrame` on the final frame of each mode while Metal may
delay capture until the next mode (framebufferOnly drawable).
`bcc471e9` fixes capture scheduling with `setScreenReadable(true)`
and mid-mode snapshots (4/15/26/37), retaining strict pixel equality
and nonblank baseline. This fix requires native rerun and cannot
be marked PASS from its mere GitHub commit. Confirm callback-frame
and main/shadow draw diagnostics if failing. No production renderer
or Strata draw path was enabled.
Update from owner Mac Studio after `bcc471e9`: **Stage 11.1
native Metal raster A/B PASSED**, `Legacy/Fine
changed_channels=0`, `Legacy/Coarse changed_channels=0`,
`status=PASSED frames=44`. The screenshots' actual capture
callback frames were Empty 6, Legacy 17, Fine 28, Coarse 39
(requested 4,15,26,37); the renderer recorded source shadow/opaque
submissions Legacy 1/1, Fine 2/2, Coarse 1/1, Empty 0/0, with
`pipelines=16 pipelineFailures=0`. Previous blank-baseline
failure is superseded for this fixed native fixture; do NOT
extrapolate to independent shadow-map depth, reflection,
mixed materials or lossy LOD visual parity.
Production Strata draw route remains opt-in diagnostic only.
On each bounded Strata step: implementation commit first; append and commit the
handoff separately, recording unrun tests honestly.

## Strata Stage 12 — real Metal shadow Depth32Float + two independent views (2026-11-19)

Implementing `Strata` must remain fully genre-independent: no
FPS/RTS-specific camera or renderer routing assumptions. There is
still only one production `Renderer3D` and one
`GlobalGeometryPool` source geometry owner.

The new **diagnostic-only** `gfx::Device::readDepthTextureAsync`
has a default-unsupported generic RHI implementation and a bounded
Metal `Depth32Float` GPU blit implementation. It does not stall the
GPU; Metal delivers packed 32-bit float depth bytes on the main
device thread at a later beginFrame. The renderer's
`requestSunShadowDepthReadback` method delegates readback of its
own sun shadow map after normal passes finish, without leaking
ownership of the texture. Never use readback as a runtime
render decision or allocate a separate Strata GPU pool.

`tests/render/strata_metal_depth_multiview_parity.cpp` is a separate
macOS GUI test target `aiverse_strata_metal_depth_multiview_parity`
built with `AIVERSE_STRATA_METAL_GPU_TESTS=ON`.
It renders Empty, Legacy, Strata Fine and Strata Coarse
lossless geometry in separate frames for **two distinct cameras**,
and reads back both real 800x600 RGBA and 512x512 shadow depth.
Success requires real nonblank color/depth, finite depth, exact
Legacy-vs-Fine/Coarse equality of both pixel buffers in each view,
distinct camera images, correct main+sun-shadow draws and
one ModelDraw per frame. There is NO production Strata draw
switch, no GPU-selected indirect draw and no claim that lossy
LOD or reflection parity has been verified.

**Current truth:** Stage 11 owner-verified Metal main-color raster
A/B is PASS with zero changed channels; real Metal compute
selection is PASS 5/5; six focused native Strata CPU/mock tests
were PASS. This **new Stage 12 native C++ compilation and GPU
runtime verification have NOT been performed** by the agent.
CI compile-only is not runtime parity. Run the new fixture on
Mac Studio, record exact output and commit separate handoff.
No merge to main until shadow-depth, reflection and advanced
material/LOD gates pass.

Docs: `docs/strata.md`,
`docs/agent-handoffs/strata-virtual-geometry.md`.


## Strata Stage 12 verification — Mac Studio real Metal PASS (2026-11-19)

The owner **actually built and ran** the new Stage 12
`aiverse_strata_metal_depth_multiview_parity` GUI fixture on Mac Studio.
All eight RGBA and eight genuine `Depth32Float` GPU captures
completed across two independent cameras and four mutually exclusive
Empty/Legacy/Fine/Coarse model modes each. The real GPU readbacks
showed **ZERO changed colour channels and ZERO changed depth bytes**
between Legacy/Fine and Legacy/Coarse in **both views**.
The shadow maps were nonconstant: 73,179 non-clear samples in
view 0 and 34,765 in view 1. The new fixture ended
`status=PASSED views=2 modes=4 frames=89`, with 16 Metal pipelines
and 0 pipeline failures. The owner's same output reconfirmed
the earlier real GPU cluster selection **5/5 PASS** and the
Stage 11 real Metal raster A/B **PASS**.

Do not accidentally reassert the historical 'Stage 12 NOT RUN':
it refers to the state before this owner-provided Mac Studio run.
The precise tested checkout SHA was not printed in the terminal
log. No production renderer switch, lossy LOD fidelity, reflection
RTT, mixed materials, Vulkan, streaming or performance was tested.
Stage 12's strict equality is **only for intentionally lossless
test geometry**. Preserve single Renderer3D submission authority,
the shared GlobalGeometryPool source, opt-in diagnostics and
conservative unsupported-case fallback.

Next independent gate: true planar reflection RTT readback and
material/UV-boundary parity with multiple cameras, followed by
lossy LOD seam and GPU-selected drawing tests. Keep PR #35 Draft.
Stage handoff: `docs/agent-handoffs/strata-virtual-geometry.md`.


## Strata Stage 13 — reflection RTT, 2 material groups and UV seams (2026-11-19)

The new \`gfx::Device::readColorTextureAsync(TextureHandle,ColorReadFn)\`
is a diagnostics-only virtual API with default-unsupported
behavior on non-Metal backends. Metal implements bounded,
non-blocking GPU blit of an **actual offscreen LDR RGBA8 render
target** into owner-thread callback RGBA8 bytes, with
BGRA swizzle and row-stride repacking. It must NOT fake a
reflection result by taking a screenshot of the presented
main camera. It does not change rendering or geometry
allocation when unused.

\`LightSystem::requestPlanarReflectionColorReadback\` and
\`Renderer3D::requestPlanarReflectionColorReadback\` provide
read-only diagnostic access to the existing half-resolution
LDR reflection RTT after the production reflection pass,
with the actual renderer and selected water plane.
\`strata::RenderPass::PlanarReflection\` is a pass-specific
CPU selection contract, not permission to submit GPU draws.

The manual macOS \`aiverse_strata_metal_reflection_material_parity\`
test uses one shared source VB/IB, one Strata auxiliary index
lease, two adjacent opaque materials with **duplicated seam
vertices, independent UV transforms and separate GPU checker
textures**. Parent geometry is deliberately LOSSLESS, so
Legacy/Fine/Coarse should be pixel-identical. It exercises
two independent cameras and four mutually exclusive modes
each. The test obtains the actual main image, sun shadow
Depth32Float map, and half-resolution reflection GPU RTT:
**24 total GPU captures**. It requires Legacy != Empty for
all three targets, exact source-vs-cluster parity, distinct
camera results, separate material/primitive submission
counts and no doubled ModelDraw. Reflections use the
existing \`Renderer3D\` water path; there is no second
raster implementation or Strata GPU allocator.

This test has **NOT YET been compiled or run natively**.
Mac Studio Stage 12, Stage 11 and cluster-culling
results were owner-verified PASS in prior steps, but those
DO NOT prove this newer reflection/multi-material fixture.
The test is compiled but NOT executed by the focused macOS
CI workflow. Keep PR #35 Draft and do not change \`main\`.
After the user runs Stage 13, record every real result and
failure with exact source commit (if available) in
\`docs/agent-handoffs/strata-virtual-geometry.md\`.
Then proceed to genuine lossy cluster LOD seam quality,
GPU-selected draw submission and guarded Legacy fallback.


## Strata Stage 13.1 — native Mac Studio reflection RTT parity PASS (2026-11-19)

The owner independently built and ran the real Metal
`aiverse_strata_metal_reflection_material_parity` fixture in
the clean isolated `AIVERSE-strata-stage13` worktree.
**Both cameras passed with exactly ZERO differences** for
Legacy vs Strata Fine/Coarse in all three actual GPU outputs:
main 800x600 RGBA, sun shadow 512x512 Depth32Float, and
offscreen planar reflection 400x300 RTT RGBA.
The fixture completed 24 real GPU captures, asserted that
the actual RTT differed from Empty, confirmed two independent
opaque PBR materials with separate UV/textures and a duplicated
UV seam, and verified no dual ModelDraw source submissions.
Observed final `[strata-reflect] status=PASSED views=2 modes=4 frames=89`;
Metal reported 16 pipelines, 0 failures.

This supersedes the historical Stage 13 pre-execution note.
The owner's output did not print the checkout commit SHA;
do not make an SHA-specific hardware validation claim.
Scope remains intentionally **lossless** cluster triangles:
real simplified-LOD seam quality, GPU-selected raster commands,
streaming, Vulkan, HDR reflection and performance remain unverified.
The branch has diverged from main; this PASS cannot be
extrapolated to an untested integrated main+Strata tree.
Keep the PR Draft, one GPU geometry pool and one production
Renderer3D draw owner. Record and test a separate main
integration before any merge. Handoff in
`docs/agent-handoffs/strata-virtual-geometry.md`.


## STRATA Stage 15.3 — async disk page handoff (2026-11-20)

Current work is on `feature/strata-virtual-geometry`, not main.
Full details, exact implementation commits, API changes, authored
but **unrun** tests and next steps are in
`docs/agent-handoffs/strata-virtual-geometry.md`, Stage 15.3.

- `aiverse_strata_disk` is a separate reusable offline/runtime
  archive library: `SceneStaging::writeDiskArchives(directory)`
  produces verified `.strataidx` files per source key.
- Metal/Vulkan `IndexResidency` uses bounded background archive
  reads for child cluster indices, with roots checked synchronously
  during scene load and parent fallback until complete siblings
  are resident and GPU-uploaded.
- `GeometryScene` opts in with `setStrataDiskPageDirectory` and
  `setStrataStreamingFrameUploadBudgetBytes`, after enabling portable
  plus incremental streaming and setting a positive index budget.
  GLES3/WebGL2 continue the old CPU-selected index fallback.
- Do not claim disk mode reduces the original CPU `ModelAsset`
  index memory: full CPU indices remain required by current planners.
  FNV checksums detect accidental corruption, not malicious assets.
- Do not replace the source vertex buffer, bypass Renderer3D, or
  merge this divergent feature branch into main until a clean native
  build, focused CTest and actual Metal/Vulkan/GLES parity run pass.
  No Stage 15.3 tests have been executed yet.

Next: Stage 15.4 global multi-world CPU/GPU streaming coordination,
then Vulkan compute GPU selection and crack-free lossy LOD. Append a
new handoff after each bounded implementation stage.


## STRATA Stage 15.4 — multi-world streaming continuation (2026-11-20)

Read `docs/agent-handoffs/strata-virtual-geometry.md` Stage 15.4
before editing indexed geometry, IO, GPU residency, scene load/unload
or RHI buffer lifetime. Current branch:
`feature/strata-virtual-geometry` (do NOT merge into main yet).

- `StrataStreamingCoordinator.h/.cpp`: one Device-wide reserved
  STRATA INDEX budget, a per-RHI-frame checked GPU transfer cap,
  process-wide bounded shared native disk IO workers and verified
  ready-page CPU cache. Settings/leases must be shared across ECS
  worlds using the same gfx::Device; no FPS/RTS dependencies.
- `DiskIndexPages` no longer starts one std::thread per archive.
  Worker jobs reference-count their own archive State, never a
  GeometryScene/ModelAsset; source close cancels pending jobs and
  releases global cache budget.
- `GeometryScene`: default-OFF, optional release of duplicate
  staged `Asset::indices` only after archive fingerprint and pinned
  GPU root admission, with all conflicting diagnostic/metadata paths
  disabled. `planView` uses immutable logical index counts.
- The shared index cap is NOT complete physical GPU VRAM including
  Vulkan versions/in-flight buffers, and CPU release does NOT delete
  original source Model geometry. Never claim those objectives met.
- New/extended tests are authored but **NOT RUN**. Finish all
  functionality before the owner's final native CMake/CTest and GPU
  acceptance pass; record exact failures and tested commit SHA.

Next functional target: Stage 16 actual Vulkan compute selected cluster
visibility, followed by Stage 17 crack-free lossy LOD. Preserve one
Renderer3D draw owner and Metal/Vulkan/GLES3 fallback semantics.


## STRATA Stage 16.1–16.2 — Vulkan GPU compute + indirect candidates (2026-11-20)

Read `docs/agent-handoffs/strata-virtual-geometry.md`, Stage
16.1 and 16.2, **before** editing `VkCompute`, Strata GPU selection,
Renderer3D indexed draw submission or model metadata. Branch:
`feature/strata-virtual-geometry`, NOT merged to `main`.

- `VkCompute.cpp`: Vulkan compute RHI, standalone GLSL450 SPIR-V,
  storage descriptors, checked 64-thread dispatch, GPU barriers and
  async fence-completed readback. Preserve legacy Vulkan graphics
  indexing and Storage vertex+index pooled buffer usages.
- `StrataVulkanCullingShaders.h`: version-2 metadata shared with
  Metal; GLSL GPU hierarchy selection + GPU indexed-indirect command
  clear/select/safety. Overflow/invalid counters disable partial
  commands on GPU, optionally replacing them with one original indexed
  primitive only when strict one-material fallback fields are set.
- `StrataVulkanIndirectContract.h/.cpp`: eligible static opaque,
  single-primitive/material/node pooled source; no animation, author
  LOD, shader graph, shadows/reflections or multi-instance alias.
  Checks model revisions, handles and frame serial. Strict rejection
  keeps the authoritative CPU/Legacy renderer route.
- `GpuClusterCulling::dispatch` now emits Vulkan
  `GpuSelectionSubmission::indirect` in addition to diagnostic ranges.
  **DO NOT interpret `gpuIndirectPrepared()` as permission to
  submit a production Renderer3D draw.** Vulkan RHI has an explicit
  `drawIndexedGpuIndirect` method but no exclusive Renderer3D
  production bridge is enabled yet. Next Stage 16.3 must own the
  full renderer state and establish actual pixel parity before opt-in.
- Stage16 authored CMake targets / tests are **NOT EXECUTED**:
  `strata_vulkan_kernel` (real Vulkan+SPIR-V when available),
  `strata_gpu_culling`, `strata_render_routing`. Do not claim
  GPU parity from older Stage13 Metal lossless fixture.
- Metal selector remains diagnostic, GLES3 portable CPU path
  remains untouched. Avoid merging the divergent feature branch
  to main until the full final native acceptance run.


## STRATA Stage 16.3 — exclusive Vulkan GPU raster handoff (2026-11-20)

Read `docs/agent-handoffs/strata-virtual-geometry.md` Stage 16.3
before changing `GeometryScene`, `Renderer3DModels.cpp` or
Vulkan `GpuSelectionSubmission`. This work remains isolated on
`feature/strata-virtual-geometry` and is NOT merged into main.

- GeometryScene now prepares a Vulkan GPU-authored indexed command
  candidate only when staging, GPU metadata, GPU compute and
  `setStrataVulkanGpuIndirectEnabled(true)` are enabled **AND BOTH**
  explicit CPU/GPU-selection and actual-raster-parity acceptance
  flags are true. Defaults remain OFF; flags are not proof of PASS.
- A matching Vulkan GPU selection is copied into the SAME source
  `ModelDraw::gpuIndexed`; no source sidecar/VB or second draw is
  submitted. The renderer's own main-opaque pass binds existing
  material, lighting and instance uniforms, then may consume the
  GPU-authored indirect array INSTEAD OF the normal indexed draw.
  Any rejection runs ordinary CPU/Legacy as before.
- Strictly one source instance, one rigid opaque mesh/primitive/
  material/node, no author LOD/deform/alpha/auxiliary passes,
  no active sun shadows or planar reflections. GLES3/Metal never
  take this Vulkan-only route; shadow/reflection scenes still
  render fully through Legacy/portable CPU.
- File/commit list and Stage 16.3 progress table are in the
  dedicated handoff. Authored tests have **NOT BEEN RUN**.
  Do not assert Vulkan real GPU pixel parity or modify defaults.
- Next: add real Vulkan Renderer3D main-view pixel parity fixture,
  then extend multi-material/multi-instance support while retaining
  exclusive pass ownership, before tackling crack-free LOD.


## STRATA Stage 16.4 — multi-material, instance and camera GPU routing (2026-11-20)

Read Stage16.4a, 16.4b and 16.4c of
`docs/agent-handoffs/strata-virtual-geometry.md` before editing
Vulkan GPU selection, `GeometryScene` or `Renderer3D`. The
complete implementation remains on
`feature/strata-virtual-geometry`; do NOT merge directly to main.

- Stage16.4a supports up to 16 opaque primitives/materials and up
  to 16 source instances, with independent GPU indexed packets;
  Renderer3D binds each original PBR/material/U0 state and owns the
  ONLY actual draw. CPU material/shadow proxy batching is disabled
  for selected GPU packet sources, not for Legacy sources.
- Stage16.4b prepares a separate sun-camera selection before
  the shadow render pass, with strict pass/projection/resolution
  identity checks and same-model Legacy fallback.
- Stage16.4c prepares **spot and planar reflection** GPU selection
  only after their actual camera matrices are known in
  `Renderer3DReflect.cpp`, **before opening render encoders**.
  A separate `GpuIndexedSelectionPacket` is owned by each
  Renderer3D ModelItem for every camera. Never reuse a main-view
  command buffer in sun, spot or mirrored reflection.
- EXPLICIT acceptance flags are all default FALSE:
  `setStrataVulkanGpuParityGates(selection,mainRaster)` and
  `setStrataVulkanGpuAuxiliaryParityGates(sun,spot,reflection)`.
  The user must establish separate real Vulkan main RGBA and
  sun/spot DEPTH, planar reflection RTT pixel parity for the
  exact source SHA before accepting each pass. A manual flag is
  not a test result.
- `GpuClusterCulling` is borrowed from GeometryScene by ModelItems
  only until `Renderer3D::render` completes; scene Model/metadata
  cannot be destroyed between draw and render.
- All 16.4 source + recording fixtures are committed but **NONE
  WERE COMPILED OR EXECUTED**. Next stage: Stage17 crack-free lossy
  cluster LOD boundaries, then Stage18 performance, then one full
  native CMake/CTest and real hardware acceptance run. Preserve
  genre independence and Metal/Vulkan/GLES3 fallback behavior.


## STRATA Stage 17.1–17.2 — welded and cross-material crack-resistant LOD (2026-11-20)

Current branch: `feature/strata-virtual-geometry`. For the full
architecture, exact commit list, test plan, known limitations and
next steps, read `docs/agent-handoffs/strata-virtual-geometry.md`
Stage 17.1 and Stage 17.2. Do NOT merge to main without
the user's final combined native validation run.

- `StrataSeamTopology.h/.cpp`: immutable CPU-only oriented-edge
  contour verifier for a single source material. Coarse parent
  triangles MUST preserve the EXACT welded global-ID border of
  the combined children, even when adjacent groups render at
  different depths. Nonmanifold/degenerate/altered contours
  reject the parent and preserve the original fine children.
  `StrataBuilder` independently audits all final parent links
  and marks `Asset::seamCertified` only after success.
- `StrataSourceSeams.h/.cpp`: source-wide UV/material/mesh physical
  seam preflight. Handles duplicated vertex IDs at equal original
  coordinates and different rigid `Model::bindWorld` transforms.
  Uses a bounded BVH to reject an original cross-material T-junction
  or near-identical but different source seam endpoints. Source
  input is NEVER repaired or welded silently; unsafe sources
  stay on the original Renderer3D Legacy path.
- `StrataModelAdapter::buildModelAsset` now requires the second
  positional audit before assigning
  `ModelAsset::sourceSeamsCertified`. `SceneStaging` and
  `GeometryScene` check per-partition AND whole-source
  certification before portable Metal/Vulkan/GLES and GPU indirect
  use. Both checks occur on CPU import/load, not in the render loop.
- Tests `strata_seams` and `strata_source_seams` added to CMake:
  authored but **NEVER EXECUTED**. Verify with `strata_builder`,
  `strata_model_adapter`, `strata_view`, streaming/GPU route
  tests and actual Metal/Vulkan/GLES3 mixed-LOD pixel/depth QA
  during the user's final combined validation. Don't claim that
  authored assertions prove GPU success.
- **Limits:** no source geometry T-junction repair, no cross-Model,
  cross-world or separate terrain chunk seam stitching, no
  morph/skin vertex changes, no geometric geomorph or frame-stable
  hysteresis, no guarantee of exact lossy-LOD interior shading.
  Imported open surfaces are allowed; suspicious near-overlap
  declines STRATA. Next Stage17.3: temporal LOD stability,
  then Stage18 performance profiling and final Stage19 acceptance.

All work stays genre-independent. Renderer3D remains the only draw
authority; Metal/Vulkan/GLES and Legacy fallbacks must keep working.


**Stage17.2 intra-material UV island note:** use
`StrataSourceSeams` edge-connected triangle components, not just
material IDs, when auditing spatial seams. Different UV islands
inside the SAME primitive may have duplicated IDs and physically
matching or mismatched boundary segments. See latest Stage17.2
follow-up in the STRATA handoff; corresponding CTest source was
written but not executed. Preserve Legacy on ambiguous source.


## STRATA Stage17.3 — CPU/portable temporal LOD hysteresis (2026-11-20)

Read `docs/agent-handoffs/strata-virtual-geometry.md`,
Stage17.3, before changing projected LOD error, per-pass view
selection or scene-resident index page demand. Current branch is
`feature/strata-virtual-geometry`, NOT merged to main.

- `StrataGeometry.h` and `StrataView`: opt-in validated
  `hysteresisFraction` (0..0.45), prior per-node refined-state
  mask and successful view's actual refined-state vector. A
  previously coarse node needs error > threshold*(1+h) to refine,
  a previously refined node stays fine until below
  threshold*(1-h). Cold start and default h=0 retain the
  original immediate threshold. Missing resident children,
  malformed input, max-selected-node budgets and prior Stages17.1
  and 17.2 seam admission always override hysteresis safely.
- `PortableLodHistory`: a value history keyed by original
  source+revision, instance, primitive, original source node and
  camera/pass slot. It is only published after the complete
  indexed source alias has been successfully prepared by
  `PortableSource::prepareModel`; a failed GPU upload never
  changes it. Force-finest late planar reflection resets the
  history after successful preparation. Changing the scene
  hysteresis policy also cold-resets all instance histories.
- `PortableSource::collectResidencyDemands` must use the SAME
  history/fraction as `prepareModel`. Otherwise Stage15 GPU
  residency might evict child pages that a warmed LOD frontier
  still requires. GeometryScene owns temporal state; no extra
  Renderer3D submission path or duplicate vertex allocator.
- All Stage17.3 new tests were AUTHORED, NOT RUN:
  `strata_view` (cold/warm thresholds, invalid masks,
  missing-residency and budget fallback) and
  `strata_portable` (Metal/Vulkan/GLES3 recording backend,
  isolated instance/camera histories, GPU-error transactional
  safety, streamed demand alignment).
- This is CPU-selected portable raster stability, **NOT**
  Vulkan/Metal GPU-compute temporal history, continuous
  geomorph or guaranteed invisible lossless shading.
  Next Stage17.4 is GPU-compute per-pass history with actual
  CPU/GPU temporal parity, then Stage18 benchmarks, Stage19
  final build/CTest and hardware validation. Parity gates stay
  OFF until an exact source SHA has real GPU evidence.


## STRATA Stage17.4 — Metal/Vulkan GPU temporal LOD history handoff (2026-11-20)

Current feature branch `feature/strata-virtual-geometry`, NOT
merged to main. Read `docs/agent-handoffs/strata-virtual-geometry.md`,
Stage17.4, before editing GPU cluster culling, `GeometryScene`,
`Renderer3DModels`, temporal mask lifetimes or Vulkan/Metal shaders.

- `GpuPassConfig` adds `hysteresisFraction` (0..0.45),
  `temporalPassId` (main=1, sun=2, spot=3, reflection=4),
  `temporalNodeCount` and `temporalRevision`. Requires one
  enabled GPU scene instance per temporal dispatch; h=0 is the
  existing stateless selector, unchanged by default.
- `GpuClusterCulling` owns bounded GPU-only per-source/
  instance/primitive/pass histories: 3 frame-serial slots,
  never overwrites a previous still-in-flight mask; no
  synchronous CPU readback. Reset every next mask, select using
  previous mask's coarse/fine hysteresis band, then GPU-safety
  rollback on invalid metadata/overflow. Distinct Metal/Vulkan
  shader kernels share 112-byte uniform layout and GPU metadata
  node indexing. Caps: 262144 nodes/key, 256 histories,
  32 MiB total logical Storage masks/selector owner. Expire
  inactive histories after GPU completion; GPU allocation or
  fence pressure declines to portable CPU/Legacy safely.
- `GeometryScene::setStrataLodHysteresisFraction()` controls
  the CPU (Stage17.3) AND GPU (17.4) band; source/material/
  policy revision, camera slot and pass viewport identity
  reset warm history. NEW
  `setStrataGpuTemporalParityVerified(bool)` remains FALSE
  until real GPU warm-state selection/raster A/B passes.
  Old stateless GPU parity acceptance flags do not imply
  temporal parity. Never enable this gate from source review
  or mock tests alone.
- Stage17.4 tests AUTHORED, NOT EXECUTED: recording RHI
  `strata_gpu_culling` tests frame fences, independent cameras,
  revisions, 32 MiB memory caps and cold/warm uniforms.
  Real `strata_vulkan_kernel` offscreen GPU test queues
  four consecutive frames and asynchronously compares
  selected ranges AND refined mask with CPU temporal oracle.
  New native `aiverse_strata_metal_temporal` is opt-in on
  macOS under `AIVERSE_STRATA_METAL_GPU_TESTS=ON`, with
  three consecutive frames. Both await the owner's combined
  final native build and real device validation.
- Never claim full physical VRAM accounting, smooth geomorphing,
  zero flicker, CPU/GPU pixel parity or measured GPU time
  from this stage. Cross-model/terrain seams and shading
  continuity remain outside the static Stage17 proof.
  Renderer3D still owns every actual graphics draw.
- Next: Stage18 profiling and GPU workload optimization,
  then Stage19 clean worktree and full CMake/CTest, actual
  Metal/Vulkan/GLES3 raster/depth/RTT parity, reconciling
  concurrent main without force merge. Every stage needs a
  handoff with exact changed files, real tests and commit SHA.

### Animation/View Stage 115 — main integration and GPU program lifetime

- Merge `01538817a68ac869b1e9ca12bc48ef9d9f4d56c7`
  integrated main's NEREID/SpectraFX work with the animation
  feature branch, preserving both parent histories. The six
  shared files were reconciled without dropping unrelated
  water/postprocessing or animation GPU-epoch changes.
  **This Git merge is NOT a C++ compilation or test result.**
- `Renderer3D` now owns `GpuProgramRetirement`: a material
  quality/instance-limit change must enqueue obsolete shader
  and pipeline handles for GPU-fenced retirement instead of
  destroying potentially in-flight programs. Editor, FPS and
  client hosts call `collectRetiredPrograms` only at true
  frame boundaries, while using the same host GPU epoch.
- When an edited AnimationGraph's GPU shader preflight is
  `Pending`, poll native jobs through
  `Renderer3D::pollPendingModelPipelineJobs()`. Do not
  repeatedly decode/upload scratch Models merely to check
  the same unfinished shader job. Preserve the old
  AnimationGraph and presentation clocks on `Failed`.
- A leased `PostFx` owner now queues old color/depth and LUT
  texture handles into GPU-fenced `OffscreenTargetRetirement`
  on size eviction/loader replacement. Frame-boundary
  collection and successful idle-drain at shutdown are required
  before retiring the host Device. Legacy unleased tools
  retain immediate cleanup for compatibility. Do not erase
  NEREID underwater/depth compositing changes.
- `uploadModel` may report false when an indexed GPU vertex
  or index allocation failed; scratch callers must use
  fence-backed rollback, not direct immediate destruction.
  The native `render_gpu_program_retirement` CTest is
  registered but **NOT RUN**. PR #22 remains Draft until
  build, GPU/backend, cross-genre animation and sanitizer
  suites pass. See the separate
  `docs/agent-handoffs/animation-stage115-main-gpu-retirement-2026-11-19.md`
  for exact commits, unresolved risks and the Stage 116 work plan.

### Animation/View Stage 116 — fence-backed shadows/trace and HDR warmup

- Continue using one renderer-host GPU generation. `Renderer3D`
  now owns `OffscreenTargetRetirement` for its shadow/SSR/trace,
  selection, light-map, spot-shadow and reflection RTTs.
  `LightSystem::setTargetRetirement` borrows this queue; do
  not create a separate Device epoch or let the owner die first.
- Resize/quality changes must allocate replacements *before*
  releasing prior targets and retire old handles using GPU
  completion tokens; Pending/Failed fences never permit
  destruction. `Renderer3D::shutdown` requires a confirmed
  full-device idle before freeing active buffers and shaders.
- Editor, GameClient and FPS must poll renderer targets through
  `pendingTargetRetirements/collectRetiredTargets` at owner
  frame boundaries. Renderer-target orphan/queue failure
  diagnostics and `gpuWarmStats` are visible in editor Stats.
- Native HDR `warmPipelineColor` can return false without a
  ShaderJob. `pendingColorWarms_` now tracks these handles,
  and `pollPendingModelPipelineJobs` advances them without
  duplicate scratch Model imports. Boolean false cannot
  distinguish a true backend failure from Pending; do not
  claim this case is fully resolved.
- The existing `render_model_image_transaction` test has
  new deferred shadow/SSR/reflection/partial allocation and
  failed indexed geometry scenarios. **NO CMake, CTest,
  GPU or sanitizer tests have been run for Stage 116.**
  Consult the separate
  `docs/agent-handoffs/animation-stage116-render-targets-2026-11-19.md`
  before any next step. Keep PR #22 Draft until full native
  validation passes.

## STRATA engine skill and MCP authoring (2026-11-20)

Use `skills/visuals/aiverse-strata/SKILL.md` for STRATA asset authoring,
MCP analysis and native verification. It is registered in the engine skill
catalog. The shared editor/MCP operations `strata_capabilities` and
`strata_model_analyze` provide read-only compiled-support and CPU import results;
they never enable GPU parity gates or certify measured rendering performance.
See `docs/strata.md` and the Stage 23 tools handoff for limits and verification.
