# Hybrid Dynamic GI - living agent handoff

Branch: feature/hybrid-dynamic-gi (based on main d71c6d47e532b25fb816a8bb1f96c41e5275a165, 2026-11-19).
Scope: shared genre-independent renderer presentation, not authoritative ECS gameplay state.
Before any continuation read docs/probe-gi.md, docs/lightbake.md, docs/sponza-handoff.md, and docs/metal-batching-agent-handoff.md.
MANDATORY: After each bounded implementation stage commit code, then append a documented handoff in a SEPARATE commit to the same branch. Keep all previous entries.

## Existing GI inventory

- engine/bake, BakedLighting, Renderer3DBaked: offline CPU path-traced lightmaps plus a compact L1 probe grid; shader variants LIGHTMAP/PROBES.
- StaticDiffuseField, StaticLightGrid, LightSystem: a separate fixed-scene sunlight/sky transport probe cache; .avgic format AVGIC005; RGBA32F shader atlas.
- ScreenLighting: screen-space color gather and temporal pass, not geometric dynamic GI.
- Moving occluders and arbitrary moving emissive lights do not currently update global transport. gfx::Device compute is optional, present in Metal but not guaranteed on GLES/Vulkan.
- Historical M3 Ultra Sponza Metal 720p full-scene medians from docs/probe-gi.md: GI off 8.41ms, Mobile 13.18ms, High 14.49ms. Not measured in this stage; no Android performance proof.

## Roadmap

1. Unify indirect GI ownership/contracts, static/dynamic composition and sky handling; preserve compatibility and avoid double counting.
2. Add generation-safe dirty bounds, bounded near-camera updates and fair scheduling.
3. Add real dynamic ray-based probe transport (light/geometry proxy, relocation/classification, visibility moments, temporal convergence) that consumes tickets, without rebuilding all probes each frame.
4. Connect dirty events from ECS changes, TerraWeave digging, falling vegetation, destruction, moving lights, and streamed world chunks. ECS publishes events after structural barriers; renderer owns transient floating-point data.
5. Add clipmap/cascade volumes, compact cache after quality tests, quality tiers and fallback, optional SSGI detail.
6. Verify Release/Debug CPU, GL/Metal GPU images, native mobile timings, stability and source-hash invalidation. Never unconditionally rebake a large world during gameplay.

## Stage 1 - 2026-11-19 - deterministic GI invalidation scheduler (source committed, NOT VERIFIED)

Source code commits:
- a1626416cd499566ed4f7d048946b2eed6dad925 - engine/render/GiUpdateScheduler.h
- 87d400c9fd6a762f2ba226694ef39a94d2e04539 - tests/render/gi_update_scheduler.cpp
- a3c484817165272f6e5ce724f9f8990a36465a3a - CMakeLists.txt, CTest registration for gi_update_scheduler in default ALL build
- 4428762ae5601fbf0360bd2b74ee028840abb10e - AGENTS.md, GI ownership and handoff convention

Public API: aiverse::render::GiUpdateScheduler in GiUpdateScheduler.h is header-only and GPU-free. configure(origin,spacing,nx,ny,nz) allocates up to 20,000 probe slots; resetClean() marks an imported valid cache up-to-date; invalidate(bounds, reason, influenceRadius) and invalidateAll(reason) produce dirty regions. claim(budget, focus) returns deterministic GiProbeTicket batches; complete(ticket) only acknowledges the same probe revision; stale completion cannot erase newer edits. abort(ticket) releases work for retry. pending() and inFlight() report backlog. Three focus-priority batches alternate with one oldest-first fairness batch.

Compatibility: Existing lightmaps, GI shaders, .avgic assets, renderer bindings, ECS saves, networking and gameplay have not changed. No runtime GI quality tier is enabled. THIS STAGE IMPLEMENTS ONLY SCHEDULING, NOT ACTUAL DYNAMIC TRANSPORT. No production GPU work has been performed.

Validation ACTUALLY executed: inspected upstream GitHub source and confirmed GitHub source commits. NOT RUN: native CMake configure, C++ build, CTest, full ./run-tests.sh, GL/MSL shader compilation, GPU images, hardware profiling, Android/iOS. Current execution container cannot resolve github.com for a clone and has no mounted AIVERSE repo. New tests are registered and use tests/AlwaysCheck.h, not Release-stripped assert; they are not claimed to pass.

Next precise action:
1. Pull feature/hybrid-dynamic-gi. Configure with cmake -S . -B build/gi -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_EDITOR=OFF -DAIVERSE_BUILD_CLIENT=OFF. Build cmake --build build/gi --target aiverse_gi_update_scheduler_test -j8, then ctest --test-dir build/gi --output-on-failure -R '^gi_update_scheduler$'. Fix any compile or runtime issues and record the tested SHA and result here, in a separate handoff commit.
2. Implement a CPU-reference dynamic indirect probe transport layer that consumes dirty tickets, projects bounce into the existing L1 directional format but stores an independent dynamic-only delta (never overwriting the baked base). Use a bounded light budget, immutable geometry reference and no per-frame full-scene recook. Add tests that a moving colored light changes indirect while direct light stays independent and invalidation during an in-flight job triggers reprocessing.
3. Integrate with LightSystem/Renderer3D only after explicit composition strategy and baseline camera/GL/Metal parity; use optional compute only when supported. Geometry mutation needs an updatable BVH or SDF/proxy acceleration structure; relighting static probes alone cannot handle holes, doors or falling trees.

No merge into main was requested. Experimental Metal ICB flags remain disabled.


## Stage 2 - 2026-11-19 - CPU reference for dynamic one-bounce diffuse GI (source committed, NOT VERIFIED)

Implementation commits:
- 5e3c94be248cd02dcde17b3a6a790da343207aeb - engine/render/GiDynamicProbeReference.h
- 4b9a4183525f3ea0c790ca30d73cc4393c9dfe62 - tests/render/gi_dynamic_probe_reference.cpp
- a41a391526470a21af0bb1bfa55155961e35daf2 - CMakeLists.txt, registers gi_dynamic_probe_reference in the default ALL/CTest test runner.

New API: aiverse::render::GiMovingLight has stable key, world position, linear color, influence radius and intensity. GiDynamicProbeReference::configure(origin,spacing,dims,samples,traceDistance) creates an independent dynamic-only L1 probe coefficient layer. setLights(sorted-by-key internally) compares per-light inputs and invalidates old/new influence bounds, including an indirect transport margin. setGeometry(const StaticShadowGeometry*, revision) forces all probes dirty when the geometry generation changes; setting null clears the overlay. update(probeBudget,camera) consumes a bounded number of scheduler tickets and traces deterministic directions from each chosen probe to static BVH surfaces; incident moving-light radiance is visibility checked, modulated by surface albedo and projected to L1 directional coefficients. coefficients() and coefficient(index) return **only bounced indirect light**: they must be added once by a future shader, never mistaken for direct point light.

Current intentional limits:
- CPU single-threaded reference for validation/small volumes; NOT a GPU compute implementation or real mobile performance path.
- Only one bounced-light order, no dynamic-geometry tracing unless host republishes an up-to-date BVH revision, no skinned meshes, no reflection probes, no lightmap/renderer binding yet.
- Still no production GI activation; current baked GI assets and shaders are untouched. No ECS/serialization/network schema changes.
- Indirect influence uses an expanded AABB, not a precise per-ray dependency graph. Large lights may mark an entire volume dirty, but update budgets prevent all probes from processing at once.
- No stale-cache content hash defined yet for moving light / geometry snapshots. Host must supply correct revisions.
- CPU reference produces a dynamic L1 **delta**. Temporal convergence, visibility moments, relocation and GPU texture layout are upcoming work.

Tests ACTUALLY executed: GitHub fetch/write operations only. **NOT RUN**: C++ compilation, CTest, full run-tests.sh, GPU tests, native device timings. Added Release-safe regression asserts using AIVERSE_TEST_CHECK for colored indirect bounce, light on/off, no-op unchanged inputs, geometry generation changes, small update budgets and invalid light IDs. These are source tests; do not label PASS without native execution.

Required next steps:
1. Verify both GI targets in a local cloned worktree:
   cmake -S . -B build/gi -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_EDITOR=OFF -DAIVERSE_BUILD_CLIENT=OFF
   cmake --build build/gi --target aiverse_gi_update_scheduler_test aiverse_gi_dynamic_probe_reference_test -j8
   ctest --test-dir build/gi --output-on-failure -R '^(gi_update_scheduler|gi_dynamic_probe_reference)$'
   Record exact tested SHA and any compiler warnings/failing inputs in the NEXT handoff; run ./run-tests.sh --quick when feasible.
2. Build a shared explicit GI composition contract, including baked base versus dynamic delta and sky occlusion, so static lightmaps and dynamic transport never double-count point lights, sun or ambient. Add test-only CPU compose fixtures before hooking it to material shaders.
3. Implement GPU update/upload via a capability-checked backend, beginning on Metal. Need independent dynamic probe texture, per-frame budget and asynchronous-safe publish; do NOT upload the entire RGBA32F static atlas each frame. Add device-free RHI fake tests, then GL/MSL visual parity. Only after that integrate renderer, ECS dirty events, TerraWeave and cascade clipmaps.


## Stage 3 - 2026-11-19 - sparse GPU upload of dynamic-only probe coefficients (committed, NOT VERIFIED)

Implementation commits:
- 0bd0a8722a514eef2534df2e2bcc6967f33eb2bc - engine/render/GiDynamicProbeTexture.h
- 51b4ec1d5faeb70524f569babb6507e9ce99a0a9 - engine/render/GiDynamicProbeReference.h, optional list of successfully published probe indices
- bc199b50ff180b6288177d47d4f963ac23a07222 - tests/render/gi_dynamic_probe_texture.cpp, FakeDevice-based upload boundary cases
- d532068a0a82a2edcad27c6fb02b30054fb8155b - CMakeLists.txt, registers gi_dynamic_probe_texture as default CTest target

API/behavior: GiDynamicProbeTexture::initialize(gfx::Device&, probeCount) allocates a separate RGBA32F nearest data texture with 384 texels per row (128 probes, 3 float4 RGB L1 coefficient rows per probe). This data is DYNAMIC INDIRECT ONLY, never the existing StaticLightGrid atlas. GiDynamicProbeReference::update(budget,focus,&publishedIndices) appends only successfully published probe IDs. GiDynamicProbeTexture::upload(reference, ids) sorts and deduplicates, merges adjacent ids within a row into one Device::updateTexture call, and reports probes / regions / bytes; uploadAll() rehydrates after device recreation. The maximum volume remains 20,000 probes (~1 MiB raw texture storage); no full-atlas upload on normal incremental updates. Release destroys the GPU texture.

Compatibility: no change to old lightmap or AVGIC005 storage, shader permutations, render quality default, ECS save format or direct light. This only provides upload-ready data. The renderer DOES NOT YET SAMPLE the dynamic texture. It does not implement GPU ray tracing or compute, and the current probe reference still uses bounded CPU BVH rays.

Actual validation: GitHub write/fetch operations succeeded. **NOT RUN:** native C++ build/CTest, GLES/Metal texture update verification, shaders, GPU performance, ./run-tests.sh. The CTest target uses existing tests/cloth/FakeDevice.h to inspect packed coefficients and adjacent-row grouping; it must be compiled and checked in Debug and Release.

Next agent actions:
1. From this branch run cmake --build build/gi --target aiverse_gi_update_scheduler_test aiverse_gi_dynamic_probe_reference_test aiverse_gi_dynamic_probe_texture_test -j8 after configuring with AIVERSE_BUILD_TESTS=ON. Then ctest --test-dir build/gi --output-on-failure -R '^gi_(update_scheduler|dynamic_probe_reference|dynamic_probe_texture)$'. Fix all warnings, failures and native portability issues; append exact tested commit SHA and report in separate handoff commit.
2. Design and test the SINGLE indirect composition point in lit shaders for baked static base (lightmap OR legacy probes OR scalable field), plus dynamic-only L1 texture and optional SSGI near-field term. Explicitly allocate a safe sampler slot for GLSL/MSL, validate Metal 16 sampler slots and shader-graph material variants. Ensure static point direct lighting never leaks into the bounced delta, and no double-addition under the prior LightSystem static grid. Do not silently enable across all materials.
3. Once shaders are verified, publish dynamic source / geometry change events from read-only ECS presentation snapshots and implement capability-backed GPU probe tracing instead of this CPU reference for large-volume 30/60 FPS operation; then hierarchical clipmaps, visibility/relocation, and real Android/iOS profiling.


## Stage 4 - 2026-11-19 - single-owner GI composition contract (source only; NOT TESTED)

Source commits:
- be48911eb40a7ecced5d26f8c3695ceb8bbf6ff0 - engine/render/GiLightingComposition.h
- 68dd085c4446c66642c6f4e0f239c8ba85ad43e2 - tests/render/gi_lighting_composition.cpp
- 945611e8f9db6e069d9425b2e7f893617253e5f7 - CMakeLists.txt: gi_lighting_composition test in ALL/CTest.

Semantics: GiBaseSource selects exactly one of Hemisphere, BakedLightmap, BakedProbes, ScalableProbes; other base values must not be summed. GiIndirectLayers accepts independent dynamicBounce and optional screenBounce as *incident*, linear, nonnegative indirect RGB and indirectOcclusion [0..1]. composeIndirectGi chooses the one owner, adds dynamic and screen delta exactly once, applies AO once. The direct-sun/point path stays independent and must not be sent into this compositor. Dynamic sources present in an immutable baked base must be explicitly excluded from the dynamic delta, or the bake must be regenerated; this API cannot magically subtract historical baked lighting.

Scope/compatibility: header-only CPU/reference contract and always-on regression assertions. There is no renderer or shader binding yet; old baked/AVGIC formats, gameplay networking, editor and save formats are unchanged. The next shader stage must mirror these semantics; it must preserve the existing default when dynamic GI is unbound.

Actually executed: read existing model shader and RHI source via GitHub and successfully committed the above files. **NOT RUN:** native compile, CTest, GPU shader validation, ./run-tests.sh, device measurement. This remains unverified until owner or an agent can run native commands in a working clone.

Next exact action: implement opt-in model shader sampling of the independent dynamic delta with correct volume mapping and AO exactly once. CAUTION: model fragment textures 13+ are currently shader-graph textures (Renderer3DModels.cpp: kGraphTextureSlot=13), and Metal has sampler limits. Do not collide with graph samplers or corrupt GPU ICB material bindings. A safe first stage is to enable dynamic GI only for models without graph textures while retaining the old path for all others. Reserve an unused uniform slot carefully, bind dynamic data only for color phases, and never force shader permutation changes in the default GI-off configuration. Add always-on sampling contract and shader-variant checks before broadening to shader graphs / terrain / water.


## Stage 5 - 2026-11-19 - opt-in GLSL/Metal dynamic probe sampling for PBR models (source committed; NOT VERIFIED)

Implementation commits:
- 10e14e62763b02438d8923271ae6739dce9c45a5 - engine/render/GiDynamicShaders.h
- 3b34d758299d44abe7eae3b7c5bd109dba3a83ab - engine/render/ModelShaders.h, dynamic overlay in indirect diffuse path (GLSL + Metal)
- c4d937b7da680b3f75ad5efc02cf849963372184, 731f0a4be5b3e6c3f452cd840ca03ba3af6398a9 - engine/render/Renderer3D.h, borrowed overlay API and shared eligibility gate
- b329f25f6ae2b5c40b53d581d7041f2e941d7619, fac7e8744bfa8a17991e9889ba2e63b8364a0dd6 - engine/render/Renderer3DModels.cpp, shader variant/binding and HDR/Medium+/device eligibility
- 8839e554fa8f1f5747c035cd5350a91ae8336a1e - engine/render/GiDynamicSampling.h, CPU-reference sample math
- d04e94a422073945ef8818b7db698cb141e953b4 - tests/render/gi_dynamic_sampling.cpp
- d482ae8e04b323e0c6c210b7b8d4739723887862 - CMakeLists.txt, gi_dynamic_sampling test
- 7bf5852d07a3855a5680710f8c28264d99272aab - engine/render/GiDynamicProbeTexture.h, exposing the owning gfx::Device to check safe borrowed textures

Public API: Renderer3D::setDynamicGiOverlay(const GiDynamicProbeTexture* overlay, float3 origin, float spacing, int nx,int ny,int nz) validates atlas readiness, matching device, grid dimensions and finite parameters; clearDynamicGiOverlay() disconnects it. Caller MUST retain GiDynamicProbeTexture, keep it on the same RHI, and supply an atlas with coefficients containing only indirect radiance from sources not already counted in the selected static base. This is opt-in; existing no-overlay models are unchanged.

Integration: GLSL/Metal material shader DYNAMIC_GI variant uses tex13 and U6 (Metal buffer(7)), samples the independent 384-wide 3-texels-per-probe atlas by four tetrahedral neighbors, fades at the volume border, adds the dynamic-only indirect result into ambient PBR prior to material AO. The existing scalable-field indirect term is suppressed only in DYNAMIC_GI variants that already use baked lightmaps or baked probes, while direct local lights and sunlight remain. The old GI-disabled shaders remain unchanged. Renderer only generates this variant in color phases 1/2, in HDR at Medium/High material quality, for lit models without active Shader Graph textures, and without GPU ICB instance or instance palette mode. Shader Graph textures begin at slot 13; this gate prevents a sampler collision. Terrain/basic meshes, water, GPU authored draw variants, low/LDR and models with graph texture inputs are NOT integrated yet.

Test source: gi_dynamic_sampling compares lattice-centre evaluation and tetra interpolation against GiDynamicProbeReference, checks zero outside and at fade boundary, and checks that both model shader strings have dynamic GI guards/expected bindings. NOTE string checks are NOT a replacement for compiling GLSL/MSL variants on actual devices.

Compatibility: no change to .avgic/lightmap version, active default quality policies, ECS persistence or networking; old rendering stays active when overlay pointer is null. Current dynamic probe reference still traces on CPU, so this does NOT establish mobile-rate dynamic GI. No built-in owner currently creates/configures a GiDynamicProbeTexture or calls setDynamicGiOverlay from game/editor; the hook is intentionally opt-in.

Actual validation: GitHub source read/write succeeded; source-only review of shader slot occupancy and CMake registration. **NOT RUN:** native build, CTest, GPU GLSL/MSL compilation and captures, mobile GPU profiling, ./run-tests.sh --quick. Do not declare compile/visual parity without evidence.

Next precise action:
1. Pull branch. Run native:
   cmake -S . -B build/gi -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_EDITOR=OFF -DAIVERSE_BUILD_CLIENT=OFF
   cmake --build build/gi --target aiverse_gi_update_scheduler_test aiverse_gi_dynamic_probe_reference_test aiverse_gi_dynamic_probe_texture_test aiverse_gi_lighting_composition_test aiverse_gi_dynamic_sampling_test -j8
   ctest --test-dir build/gi --output-on-failure -R '^gi_'
   Run ./run-tests.sh --quick when feasible. Fix every compile warning/assertion and record exact SHA. Critically compile real GLSL and MSL opt-in variants with texture slots 13, uniform 6, HDR PBR and LIGHTMAP/PROBES.
2. Add lightweight host-side runtime ownership so a game/editor can allocate/reference the volume, apply changed lights and geometry revisions, update <= budget probes/frame and upload only returned dirty IDs; then call setDynamicGiOverlay() before drawModel. Instrument light-to-indirect latency and upload bytes. Make ownership and teardown explicit.
3. Add GPU compute tracing on Metal behind capabilities and a conservative fallback; integrate terrain, Shader Graph texture-slot remapping, and dynamic occluder BVH/SDF. Never silently enable GPU ICB experimental flags. Full 30 FPS Android claim requires actual device measurements.


## Stage 6 - 2026-11-19 - opt-in dynamic GI host runtime/controller, invalidated geometry clears ghosting (source committed; NOT VERIFIED)

Implementation commits:
- bf654fec4ffa40e34d18ad3a7357b4385ae72755 - engine/render/GiDynamicProbeReference.h clears stale dynamic light coefficients whenever the geometry snapshot/revision changes, instead of retaining transport through a removed obstacle.
- 29feae08ed84953dd2d96f19b0577a7441c77734 - engine/render/GiDynamicRuntime.h, owner of CPU reference and independent GPU probe atlas.
- 197735cfe05bb581ef133e1a658b32c962ad73e5 - tests/render/gi_dynamic_runtime.cpp, FakeDevice test for update budget, geometry reset, upload size and shutdown.
- 3e1198dd9db814864705b0f8d1e9dd87c3c20664 - CMakeLists.txt, gi_dynamic_runtime registered in default ALL / CTest.

Public API:
- GiDynamicRuntime::initialize(gfx::Device&, origin, spacing, nx,ny,nz, raysPerProbe, bounceDistance) configures a reference GI volume and allocates its own independent RGBA32F texture.
- setGeometry(const StaticShadowGeometry*, revision) changes the immutable traceable geometry snapshot; resets stale CPU coefficients and marks the full atlas for zeroing before the next update (even budget=0). Host must rebuild a geometry snapshot when triangles change; incrementing revision of an unchanged BVH alone cannot detect new obstacles. setLights(vector<GiMovingLight>) performs source-key stable dirty-region invalidation. invalidate(bounds,reason,radius) supports explicit terrain/foliage/world-change notifications.
- update(budget,camera) refreshes no more than budget probes using the CPU reference, updates only published probe texels after an optional one-time geometry clear, and reports GiRuntimeStats (probe count, rays, upload bytes/regions, backlog/inflight).
- bind(Renderer3D&) calls setDynamicGiOverlay; unbind(Renderer3D&) clears the borrowed pointer. Caller MUST unbind before runtime/renderer destruction or device replacement. shutdown() releases texture and CPU state. No GUI/automatic ticking or gameplay activation is added.

Safety/compatibility:
- The runtime is *opt in* and presentation-side, not authoritative ECS state. Baked/AVGIC005 formats and existing default renderer paths are untouched.
- Geometry change currently clears the WHOLE dynamic atlas once (avoid ghost illumination through destroyed walls); later stages should support sparse invalidation per clipmap brick and incremental zero upload instead of whole atlas.
- CPU reference remains a small-scene correctness fallback, not a mobile-performance solver; no GPU ray tracing compute or SDF geometry acceleration yet.
- Dynamic lighting for shader-graph textures, basic lit meshes, water and TerraWeave terrain still requires dedicated integration. Merely providing invalidate() does not establish an ECS bridge.

Actual verification: Read source, GitHub writes reported success; **NOT RUN**: native build, CTest, shader compile, visual screenshots, hardware timings, ./run-tests.sh. No tests have passed in this execution context.

Next:
1. Pull branch and configure/build all GI tests including gi_dynamic_runtime, run ctest -R '^gi_' and full ./run-tests.sh --quick; compare CPU references to GPU frames on Metal/GLES; fix any warnings or assertion failures. Record results and exact SHA in the next handoff.
2. Create an optional simulation-to-render read-only world-change subscription with stable event types (light old/new bounds, destructible changes, terrain, vegetation). The presentation side calls runtime.invalidate with conservative transport influence and republishes a rebuilt geometry snapshot when blockers change. Do not add genre-specific state to GI.
3. Replace high-cost CPU tracing with bounded GPU compute on supported backends and a robust fallback. Progressive update/clipmap/relocation and material system integration are still needed before claiming production dynamic GI.


## Stage 7 - 2026-11-19 - bounded, genre-independent world-change dirty-region bridge (source committed; NOT VERIFIED)

Implementation commits:
- 42aa7fc2047f9a0c14e440e42c2365135a33692a - engine/render/GiWorldDirtyAccumulator.h
- 077a5e7fbb17a2b3d33cbc5909496c973f27996d - engine/render/GiDynamicRuntime.h, applyWorldChanges accumulator flush
- 81188175152aa8dc1eab49689cd522ef2365a63e - tests/render/gi_world_dirty_accumulator.cpp
- 586b75b57cc536e3a2846b790003e10b6068e292 - CMakeLists.txt, registers gi_world_dirty_accumulator in default ALL and CTest

Public API: GiWorldChange carries a generic GiDirtyReason, current and optional previous world bounds, and conservative transportRadius. GiWorldDirtyAccumulator::submit validates events, expands influence bounds, merges intersecting regions (including transitive overlaps) and retains reason categories. At most 128 independent regions are stored. If the batch exceeds that limit (e.g. thousands of grass changes), it switches to a full-volume dirty mark; no unbounded per-grass memory growth. flush(GiUpdateScheduler&) sorts resulting regions for deterministic invalidation order, marks the right probe cells, clears queued events and reports events, coalesced region count, probe marks and saturation. GiDynamicRuntime::applyWorldChanges(accumulator) is an opt-in convenience bridge; host owns the ECS source and event ordering.

Invariants/limitations: This accumulates visual invalidation events; it DOES NOT update traceable world geometry, create a new BVH, identify occlusion changes, or supply an ECS subscription itself. After digging terrain, opening a door or falling trees, the producer must publish new immutable traceable geometry and revision using setGeometry(), then apply any other affected material/light edits. This currently triggers a full dynamic-atlas clear on geometry revision; later chunked/proxy-BVH updates are necessary for big worlds. Moved objects should submit BOTH previous and current bounds rather than a long connecting AABB. The 128-region overflow degrades conservatively to a whole-volume update, which may take several frames under a bounded per-frame probe budget.

Compatibility: no ECS runtime or save schema changes, no genre-specific logic, no legacy/AVGIC005/cache format changes, no new default renderer quality cost.

Verification ACTUALLY executed: connected GitHub file create/update returned successful commit SHA. **NOT RUN:** compilation, CTest, ./run-tests.sh, GPU captures, mobile timing. New test source covers moved-object old+new bounds, merging categories, saturation under 129 changes and invalid reason/region rejection; it is not a PASS until executed.

Next exact steps:
1. Pull feature/hybrid-dynamic-gi and run:
   cmake -S . -B build/gi -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_EDITOR=OFF -DAIVERSE_BUILD_CLIENT=OFF
   cmake --build build/gi --target aiverse_gi_update_scheduler_test aiverse_gi_dynamic_probe_reference_test aiverse_gi_dynamic_probe_texture_test aiverse_gi_lighting_composition_test aiverse_gi_dynamic_sampling_test aiverse_gi_dynamic_runtime_test aiverse_gi_world_dirty_accumulator_test -j8
   ctest --test-dir build/gi --output-on-failure -R '^gi_'
   Run ./run-tests.sh --quick and GPU shader variant checks if possible. Record actual exact tested source SHA and results in another handoff stage.
2. Integrate concrete read-only scene events from TerraWeave/NEREID/vegetation/building systems after their respective commits land in main; avoid guessing proprietary unpublished APIs. Surface changes must rebuild a geometry proxy for visibility.
3. Add partial geometry-region update, probe relocation/classification, GPU tracing on Metal and portable graphics fallback, then real performance and visual acceptance. No work was merged into main.


## Stage 8 - 2026-11-19 - incremental geometry-revision invalidation and sparse GPU clearing (source committed; NOT VERIFIED)

Code commits:
- 7732a17614d0da005e71041d2aebde124bd436a9 - engine/render/GiUpdateScheduler.h: optional affectedIndices output in invalidate()
- d3c101156d898e52971cbca94e92b6fafe457938 - engine/render/GiDynamicProbeReference.h: local geometry snapshot revisions, conservative dirty radii and immediate CPU coefficient clearing
- 1f7d43663170c70151bee209c4aae1c4e74380ef - engine/render/GiDynamicRuntime.h: setGeometryRegion(), pending regional zero uploads, dedup across overlapping revisions
- 8271dce623e21b48da4841dc0da467fba1a14f82 - tests/render/gi_regional_geometry.cpp
- 2c76aa59d4bdd8e2bf5533836810036f98de890b - CMakeLists.txt: GI regional geometry added to default ALL and CTest

API:
1. GiUpdateScheduler::invalidate(bounds, reason, radius, std::vector<uint32_t>* affectedIndices = nullptr) retains existing overload behavior while optionally reporting every invalidated cell.
2. GiDynamicProbeReference::setGeometryRegion(const StaticShadowGeometry* rebuiltImmutableSnapshot, uint64_t revision, GiBounds changed, float influenceRadius = 0, std::vector<uint32_t>* cleared = nullptr) requires **an existing and a new non-null** geometry snapshot, validates the region, then atomically switches the reference to the new scene geometry, resets only affected CPU L1 coefficients and schedules those probe indices. Effective radius is conservatively at least traceDistance + maximum source radius: an edited blocker can change both probe-to-surface transport and the subsequent surface-to-light shadow ray.
3. GiDynamicRuntime::setGeometryRegion(geometry, revision, bounds, radius) forwards the updated immutable BVH snapshot, accumulates zeroed probe indices until the next frame, and returns affected probe count. update(budget, camera) uploads *only those cleared texels* before tracing, even with budget=0; then uploads only successfully retraced probes. Multiple changes before update are deduplicated by GiDynamicProbeTexture::upload. A full setGeometry still performs a full invalidation/zero upload and supersedes pending regional clears.

Compatibility: no changes to baked lightmaps, AVGIC005, shader layouts, ECS save/networking, or existing default GI. Calls are opt-in. Geometry reference snapshots are borrowed; owner must ensure lifetime. Merely bumping a revision without rebuilding the BVH does not make moved geometry traceable; this stage optimizes invalidation, **not** the BVH rebuild itself.

Performance intent and limitations: Local changes avoid full-volume CPU coefficient reset and GPU upload, but the geometry BVH is still rebuilt by the host; CPU reference tracing remains bounded and slow compared with a GPU solver. If traceDistance and light radii cover the whole world, the conservative affected region will still include all probes, which is correct. This is a fixed dense probe volume (not yet clipmaps). Each region is axis-aligned in world metres. Revision changes while old tickets are in flight invalidate them; CPU reference update is currently synchronous single-threaded.

Regressions added (source only): gi_regional_geometry checks GPU texture row preservation for a distant independently lit region, affected count < total, zero upload with no tracing, incremental retracing bounded by budget, coalesced overlapping regional edits, invalid range rejection, and rejecting partial edits without an initial geometry snapshot. It uses always-on AIVERSE_TEST_CHECK.

Validation ACTUALLY performed: GitHub fetch, source edits and successful commit acknowledgements only. **NOT RUN:** compilation, CTest, shaders, device metrics, ./run-tests.sh. No PASS assertions about the regression or Android/Metal performance are justified yet.

Next bounded stage:
1. Verify the exact branch HEAD in a native worktree, compile the now **eight** gi_* targets and run CTest:
   git fetch origin && git switch feature/hybrid-dynamic-gi && git pull --ff-only origin feature/hybrid-dynamic-gi
   cmake -S . -B build/gi -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_EDITOR=OFF -DAIVERSE_BUILD_CLIENT=OFF
   cmake --build build/gi --target aiverse_gi_regional_geometry_test -j8
   ctest --test-dir build/gi --output-on-failure -R '^gi_'
   ./run-tests.sh --quick
   Preserve logs and exact tested SHA; fix any compiler/test failures before claiming tested.
2. Add generation-aware geometry chunk sourcing (terrain tiles, vegetation tree proxies, buildings) with a deterministic index and host BVH rebuild/publishing. Only geometry that is actually in a rebuilt BVH affects ray visibility.
3. Add scrolling/clipmap probe volumes for worlds larger than a single dense <=20,000-probe lattice, then a true GPU compute GI backend (Metal first, Vulkan/other backend fallbacks) plus per-frame GPU timing checks.


## Stage 9 - 2026-11-19 - immutable chunked geometry BVH snapshots (source committed; NOT VERIFIED)

Code commits:
- 19ca1b3a4be81a987d84d8aed0e4becc6e46278b - engine/render/GiChunkedGeometry.h
- e979be0dd7192f9f794f646b32caf0bcfea4de14 - tests/render/gi_chunked_geometry.cpp
- 21fd0d3f631566ce74a7261b0abc8a6b2b6d7511 - CMakeLists.txt, gi_chunked_geometry in the standard ALL / CTest suite.

API and architecture:
- GiChunkedGeometryBuilder::replace(uint64_t stableId, vector<StaticShadowGeometry::Triangle> worldTriangles, vector<float3> albedos) builds a NEW StaticShadowGeometry BVH for that chunk only, rejecting nonfinite geometry, invalid surface IDs and unreasonable triangle counts before replacing the previous chunk. remove(id) removes the chunk; publish() builds a lightweight top-level AABB BVH over all active chunks.
- GiChunkedGeometrySnapshot is immutable, owns std::shared_ptr<const StaticShadowGeometry> per chunk, provides distance(origin,direction,limit,normal,albedo,optional QueryStats) with closest-hit semantics identical to the existing StaticShadowGeometry reference, findChunk(id), chunkCount() and triangleCount(). Unchanged chunks retain the same triangle-BVH allocation across published snapshots. Old snapshots remain usable while an updated world snapshot is being published; there is no shared mutable BVH.
- Top-level AABB hierarchy is rebuilt each publish (O(chunk count log chunk count), not an incremental top-level tree). This avoids re-BVHing all triangles on edits, but is not yet a sparse GPU proxy or SDF/voxel cache.

Compatibility: This is a new optional presentation-only module. Existing StaticShadowGeometry, CPU GI, legacy baked lightmaps, AVGIC005, model shaders, ECS saves/network and renderer defaults remain unchanged. The GiDynamicProbeReference/Runtime still accept StaticShadowGeometry, not this new snapshot. **Do not claim chunked geometry is already integrated into the dynamic GI tracer**; that adapter is the next stage.

Test added (source only): gi_chunked_geometry creates two overlapping hit candidates with different albedo, verifies nearest hit, per-chunk BVH pointer sharing after replacement, unchanged old snapshot results, deletion and bounded broad-phase chunk tests with forty irrelevant chunks, plus rejection of nonfinite vertices and negative colors without mutation. It uses always-on AIVERSE_TEST_CHECK.

Validation ACTUALLY executed: source reads + successful GitHub file/commit operations only. **NOT RUN:** native C++ build, CTest, full ./run-tests.sh, GPU shader compilation, hardware timings. No compiler or runtime PASS may be inferred.

Next exact step:
1. Compile/run gi_chunked_geometry + gi_regional_geometry and all other gi_* CTests in native Debug and Release, repair any compile errors or logic failures, and record tested SHA + logs.
2. Introduce a narrowly scoped ray-query adapter so GiDynamicProbeReference can consume GiChunkedGeometrySnapshot without changing the public static-geometry call sites. GiDynamicRuntime must retain snapshot ownership or mandate borrowed snapshot lifetime. Demonstrate a door/wall opening changed only one chunk BVH; unaffected probe texture values remain unchanged while local visibility/radiance updates.
3. Only after regression/performance checks: incremental top-level tree refinements, clipmaps, and GPU tracing/compute with capability checks (Metal first). Keep O(chunks) CPU allocations and mesh ownership out of authoritative ECS simulation.


## Stage 10 - 2026-11-19 - chunked geometry ray-query adapter and dynamic GI runtime wiring (source committed; NOT VERIFIED)

Code commits:
- c0d732985f2baafd08aa1a60412e4f3e5fe714e2 - engine/render/GiTraceGeometry.h, non-owning ray-query view
- 7064426b59526876adcce5cf3fa670ec4dac1c50 - engine/render/GiChunkedGeometry.h, immutable chunk snapshot traceView() with callback
- a57ca895784c791c9c36efd13e68f15004318243 - engine/render/GiDynamicProbeReference.h, common ray-query callbacks for both monolithic and chunked scenes
- 4e7152f02dce19bd161ca4d715d66d2cfb9243b7 - engine/render/GiDynamicRuntime.h, setTraceGeometry / setTraceGeometryRegion
- ef92890b23038ce822c98caa6ac11b98f3f409b9 - tests/render/gi_chunked_runtime.cpp
- cf5e49fe257cfd75c72433c4fe318416d46a413e - CMakeLists.txt, new gi_chunked_runtime regression in ALL/CTest

Architecture/API: GiTraceGeometryView is a value-type, borrowed {source pointer, ray query callback} wrapping StaticShadowGeometry::distance() or GiChunkedGeometrySnapshot::distance() with the same normal/albedo closest-hit contract. Existing GiDynamicProbeReference::setGeometry and ::setGeometryRegion keep their prior concrete signatures, forwarding to setTraceGeometry and setTraceGeometryRegion; GiDynamicRuntime does likewise. A new chunk snapshot exposes traceView(), which is accepted directly by GiDynamicRuntime and uses the same budgeted dynamic bounce solver. There are no virtual allocations per ray, only one indirect function-pointer call. A null view clears the dynamic layer through the existing full-reset path.

Lifetime/ordering contract: GiTraceGeometryView is borrowed. Store a published GiChunkedGeometrySnapshot in owner-controlled stable memory, keep it alive during trace/update, and only release older snapshots after no readers depend on them. Replacing a chunk uses builder.replace/remove, then builder.publish, then runtime.setTraceGeometryRegion(newSnapshot.traceView(), incrementedGeometryRevision, worldDirtyBounds); update(0) clears affected GPU texels before rendering, update(budget>0) retraces a bounded subset. Publishing a snapshot is not a GPU upload and does not automatically notify ECS/renderer. Never call setTraceGeometryRegion with an unbuilt empty previous view.

Test source: gi_chunked_runtime builds two distant wall chunks / separately colored moving lights, then removes one chunk, checks BVH pointer sharing, asserts affected probes < full volume, verifies sparse zero upload with no rays, tests that only near bounce is removed and far blue bounce is preserved, and checks both old/new immutable snapshots can be queried. All assertions are always-on. No runtime compilation or test execution has been performed here.

Compatibility: no modification to save/network, old baker, AVGIC005, default renderer quality, shader bindings, TerraWeave/NEREID ownership or native ECS state. The CPU dynamic reference can now use a chunked world but remains single-threaded and DOES NOT provide GPU compute ray tracing; a global top-level chunk BVH is rebalanced when publish() is called.

Verification ACTUALLY performed: GitHub file read/commit acknowledgements only. **NOT RUN:** CMake/Clang/GCC compilation, CTest, full ./run-tests.sh, real Metal/GLES shader validation, GPU benchmarks or mobile profiling.

Next agent steps:
1. Fetch this branch and compile/run all TEN gi_* CTest targets, especially gi_chunked_geometry, gi_chunked_runtime and gi_regional_geometry. On failure, repair actual code, record exact SHA and log; do not claim unverified source tests passed.
2. Create a host-facing lifetime-safe geometry owner wrapping GiChunkedGeometryBuilder/published snapshots with staged per-chunk edits and generation tracking, without calling the editor or gameplay systems in the GI module. Hook opt-in terrain/foliage/destruction adapters after verifying concrete interfaces in current main.
3. Extend probe volumes with scrolling clipmap/cascades and prioritize camera-adjacent dirty probes. Build a capability-gated GPU compute path for relighting/tracing and compare against CPU reference. Test visual parity for wall-open/closed, camera motion and stale in-flight jobs.


## Stage 11 - 2026-11-19 - host-owned immutable GI geometry scene with snapshot retirement (source committed; NOT VERIFIED)

Implementation commits:
- f201d67283ca9ce7c0455dd80c5ee003d42b5d4c - engine/render/GiGeometryScene.h
- 1d4b37b5e00b2c3ad30eda7a073d3510d9ddd0a6 - tests/render/gi_geometry_scene.cpp
- e27eeccee342cd72dd999174ff69a43852fa8532 - CMakeLists.txt, gi_geometry_scene test included in default ALL/CTest.

What changed: GiGeometryScene is a presentation-only owner wrapping GiChunkedGeometryBuilder, stable chunk ID to world bounds bookkeeping, a current immutable GiChunkedGeometrySnapshot, and a deque of retired snapshots that remain alive until the host explicitly calls releaseRetiredThrough(completedRevision). The host can replaceChunk(id, worldTriangles, albedos), removeChunk(id) and publish(GiDynamicRuntime&) after the ECS/world structural mutation barrier. Initial publication and batches exceeding 128 changed bounds call full setTraceGeometry; later local edits use setTraceGeometryRegion for previous and current bounds, creating new incremental geometry revision tokens per region. No full triangle BVH rebuild occurs for unchanged chunks. GiGeometryPublishStats reports published/fullReset/revision/chunks/changedRegions/invalidatedProbes. An unmodified scene is not needlessly republished.

Ray-query view lifetime: The owner retains current and retired published snapshots; unchanged triangle BVHs are shared_ptr-managed by GiChunkedGeometrySnapshot. Do not call releaseRetiredThrough until all CPU readers and, once introduced, GPU compute jobs referencing the retired views have completed. GiDynamicRuntime currently performs synchronous CPU reference updates, but any future asynchronous renderer must use a real fence. Caller must unbind GiDynamicRuntime from Renderer3D before the owning runtime/scene is destroyed, and must not keep the current geometry view after owner shutdown.

Limitations: GiGeometryScene still requires the host to generate world-space triangle proxy geometry per chunk. It is not yet an automatic subscriber to TerraWeave/NEREID/vegetation ECS events and is not a GPU SDF/voxel solver. An edit to a large mesh chunk rebuilds that chunk BVH on CPU; top-level BVH rebuilds each publish. Changed bounds are conservative and may be invalidated twice if overlapping, reported invalidatedProbes counts marks, not necessarily distinct probe IDs. Over 128 changed regions fall back to full-volume clearing. Renderer defaults, baked GI caches and authoritative ECS saves remain unaffected.

Verification source fixture: tests/render/gi_geometry_scene.cpp creates two opaque GI wall chunks and two moving colored lights; first publish sets all probes, deletion of one chunk republishes only near affected probes, untouched far bounce stays identical, the old snapshot remains valid until releaseRetiredThrough, no-op publish skips changes and invalid triangle input does not corrupt canonical state. Registered in regular CTest via a CMake ALL target. Test source uses AIVERSE_TEST_CHECK, not Release-stripped assert.

Validation actually executed: GitHub source inspection and file/commit operations only. **NOT RUN**: build, CTest, run-tests.sh, actual GL/Metal model-shader compilation, GPU screenshots and performance measurements. No CI success has been established. Do not claim this code is production ready until validation.

Next steps for the next agent:
1. Pull this branch, run cmake configure and build ALL 11 GI targets (not just tests' headers), ctest --test-dir build/gi --output-on-failure -R '^gi_' and ./run-tests.sh --quick. In particular check gi_chunked_geometry, gi_chunked_runtime, gi_geometry_scene and gi_regional_geometry on real Clang/GCC Debug + Release. Append a new handoff with actual SHA, logs, compiler warnings, and any fixes. No false-green Release assertions.
2. Implement a real publisher bridging native procedural terrain chunks / foliage instances and destructible geometry from the relevant shared ECS/WorldScene presentation snapshot into GiGeometryScene. Read actual current main ownership before editing; do not duplicate genre systems. Geometry vertex positions and albedo must be in world metres and local chunk rebuild must be generation-bound.
3. Implement GPU compute relighting/transport: capability-check Metal first; use independently owned uploaded BVH/SDF/geometry proxy, bounded rays/probes per frame, publication fences, and a no-compute fallback. Only then add camera-following GI clipmaps, automatic quality budgets, material-graph/terrain/water integration and mobile/perf gates.

## Stage 12 - 2026-11-19 - bounded Metal GPU first-bounce compute prototype and texture-RHI compatibility review (SOURCE ONLY; NOT VERIFIED)

Source commits:
- f69015707fabb441911f729911bb0f9f62d2fd26 - engine/render/GiDynamicProbeTexture.h computeWritable opt-in
- eb4da7badac7328cac5d7af51e539ef963fc6c6a - engine/render/GiChunkedGeometry.h immutable world-space triangle/albedo export
- bfc5f28e5dc575c199a76b7e8055c25383f63321 - engine/render/GiDynamicProbeTexture.h computeWritable chooses RGBA16F, legacy CPU RGBA32F path unchanged; CPU float4 uploads to half-float storage rejected
- 97601a769567da510af85751cfe39005763fa7a6 - engine/render/GiGpuSmallSceneShaders.h Metal cs_main kernel
- 401bf3302fc95f59f6a52edec39ef38d4be637ab - engine/render/GiGpuSmallScene.h opt-in bounded GPU dispatcher
- 9d992e513ecce5a859736055af82164eb860fa0f - tests/render/gi_gpu_small_scene.cpp FakeDevice regression source
- 958503a13ab0bf39a176f4de291994cf0daed3ae - CMakeLists.txt registers gi_gpu_small_scene in standard CTest

Cross-branch review: User authorized importing relevant work from feature/texture-rhi-unification. Comparison against main showed texture branch 696 commits ahead and 409 behind with an older merge base. It extensively changes Gfx.h, GL/Metal/Vulkan, ModelShaders, Renderer3D and texture streaming; wholesale merging would risk overwriting concurrent GI/NEREID/batching. NO MERGE OR CHERRY-PICK WAS PERFORMED. Its Metal supportsStorageTexture advertises RGBA8/RGBA16F, not RGBA32F; Vulkan Compute uses another descriptor layout. GI compute therefore uses RGBA16F to align with the eventual texture contract while leaving old GI RHI/CPU path intact.

New API: GiGpuSmallScene::initialize(device,probeCount) explicitly requires Metal, supportsCompute and RGBA16F. It allocates a computeWritable RGBA16F 384-wide atlas and compiles a cs_main MSL kernel. setGeometry(GiChunkedGeometrySnapshot,revision) flattens and uploads a MAXIMUM of 256 scene triangles only when revision changes, retaining unchanged scene data. setLights takes at most 16 moving point lights, validates and stably sorts by key and uploads linear RGB/intensity. dispatch(tickets,samples,distance) accepts up to 32 unique probe tickets and 8..64 rays/probe inside a valid frame, OUTSIDE render passes. One thread per probe traces closest triangles, shades bounced point-light diffuse, checks occluders, and writes directional RGB L1 into three half-float RGBA texels. DispatchResult reports submission and theoretical worst-case work, NOT measured GPU time.

Safety and fallback: The new GPU path is a STANDALONE OPT-IN PROTOTYPE. It does NOT automatically connect to GiDynamicRuntime or the game/client. Existing CPU reference and baked-lightmap defaults remain intact. No material/asset/save format changes. There is no reliable GPU completion fence or scheduler acknowledgement, so dispatch deliberately DOES NOT complete GiUpdateScheduler tickets. Buffers/snapshots must remain alive until GPU execution is complete. Current CPU coefficients are not mirrored to GPU output. Other RHI backends fall back to the existing CPU path; do not imply Vulkan or Android GPU GI works.

Major limitations: O(max 256 triangles * rays * max 16 lights) shader traversal, not suitable for large worlds; no GPU BVH/SDF, no clipmaps, moving dynamic meshes, temporal accumulation, relocation or real performance budgets. The input source is an immutable already-built chunk snapshot; the GPU prototype does not accelerate chunk updates beyond the CPU flatten-on-revision staging. GPU/CPU parity and real shader compile are not verified.

ACTUAL verification: GitHub branch comparisons and code inspection, source create/update success, CTest registration. NOT RUN: native C++ compilation, CTest, Metal shader compilation, GPU execution/readback/captures, timing, ./run-tests.sh, Android/Vulkan. FakeDevice test source only checks capability gating, rejected invalid jobs/IDs, dispatch count and shader marker ABI. It cannot prove GPU result correctness.

Next steps: (1) Pull this branch and run all 12 GI test targets, CTest -R '^gi_', ./run-tests.sh --quick and actual Metal shader compilation/API validation; fix all issues, record source SHA + logs. (2) Implement an RHI GPU completion/fence mechanism or conservative safe ring, generation-aware scheduler publication and ABI/pixel-parity tests versus CPU reference before wiring the backend into GiDynamicRuntime. (3) Replace flat-triangle O(T) loops with uploaded GPU BVH/SDF, dirty chunk uploads and scrolling GI volumes, then integrate TerraWeave/foliage/water and benchmark on real mobile GPU. Avoid merging the divergent texture branch until it is rebased or isolated prerequisites can be tested. Do not touch main.

## Stage 13 - 2026-11-19 - deterministic GPU BVH ray traversal (source committed; NOT RUN)

Code commits:
- 7a807d8b5fabe74d110fa50c0ab5b22848be0931 - engine/render/GiGpuBvh.h
- bfb4f6a791e756c2b1935fb45a4d4cb2dff99299 - engine/render/GiGpuSmallSceneShaders.h
- 60fe05738345eb2f73a0c58012cb0972f6a4ecb2 - engine/render/GiGpuSmallScene.h
- 4194b184bcb5338c22a66c85f01f785df8a33ab2 - tests/render/gi_gpu_bvh.cpp
- fb0e6c9465a3c02adad9f6affa704df0a454e3f0 - CMakeLists.txt, gi_gpu_bvh in default ALL / CTest.

Implementation: GiGpuBvh::build(snapshot) copies up to 8192 world-space triangles from immutable GiChunkedGeometrySnapshot, rejects invalid coordinates/albedos, uses deterministic median centroid splits to emit packed C++/MSL 64-byte triangles and 48-byte nodes. Leaf size is <=4; CPU GiGpuBvh::trace provides nearest-hit reference. Metal GiGpuSmallSceneShaders.h uses a 64-entry explicit stack for GPU BVH traversal of primary and secondary shadow rays, replacing O(all triangles) loops. Compute buffer(3) supplies nodes; 32-byte uniform ABI includes nodeCount; existing RGB L1 output format stays unchanged. GiGpuSmallScene uploads triangle and node buffers transactionally on scene revision, updates metadata only after both allocations succeed, and reports nodes count in dispatch diagnostics.

Compatibility: unchanged existing lightmap/baker, GI output texture (3 float4 / probe), ECS save/network and default renderer. GPU option remains Metal-only, off by default and standalone; no shader-graph, terrain, water or Vulkan path enabled. CPU BVH still flattens/rebuilds a full <=8192 triangle scene per geometry revision; NOT yet incremental per-chunk GPU streaming. GPU geometry resource lifetime and fences remain unresolved; do not release or reuse resources from in-flight compute until a GPU completion barrier.

Validation performed: GitHub source fetch/commits + CTest registration only. NOT RUN: native C++ compilation, actual tests, Metal shader compilation, GPU readback, GPU perf, ./run-tests.sh. The new gi_gpu_bvh fixture source compares CPU BVH nearest hit against chunk snapshot, broad-phase pruning and fake Metal dispatch; it is not proof that MSL compiled or ran.

Next: Pull branch and build all 13 GI targets, run ctest --test-dir build/gi --output-on-failure -R '^gi_', and ./run-tests.sh --quick; compile MSL on actual Metal and compare GPU readback against GiDynamicProbeReference with explicit tolerance. Fix any errors and append exact SHA / observed logs. Add generation-aware GPU-fence completion acknowledgements before wiring scheduler to GPU; dispatch is encoder submission, not completion. Then implement per-chunk GPU BVH updates/SDF, clipmaps and real device profiling.

## Stage 14 - 2026-11-19 - GPU completion-aware probe ticket queue (source committed; NOT RUN)

Code commits:
- 4447e02e61393767a67a66da330024e5a419bd3a - engine/render/GiGpuTicketQueue.h
- c0169148521e533fd5f6242859d5aa2ae6be6624 - tests/render/gi_gpu_ticket_queue.cpp
- 5ad8c4866face44342b9e6b5dc21ef1bb6b75b02 - CMakeLists.txt: GI ticket queue test in default ALL / CTest

API and scheduling: GiGpuTicketQueue::initialize(gfx::Device&) allocates a 4-byte immutable GPU storage buffer with a known marker. submit(backend,scheduler,probeBudget,focus,samples,traceDistance) claims at most GiGpuSmallScene::kMaxJobs per batch (and at most 4 outstanding batches), dispatches on Metal, then calls gfx::Device::readBufferAsync(marker). Metal implements readBufferAsync via a blit AFTER prior compute commands in the current command buffer, with the callback only delivered on the owner thread once that GPU command buffer completed. The callback captures a shared state packet, never this queue or a scheduler pointer. poll(scheduler) acknowledges tickets only on positive completed callback, rejects stale probe revisions and aborts on GPU error. Failed dispatch aborts tickets without acknowledgement. If readBufferAsync cannot queue after a successful dispatch, the batch remains pending until the host obtains an independent real GPU-finished signal and explicitly calls confirmExternalCompletion(serial, success). Do not pass true without a real GPU completion fence. GiGpuTicketQueue::setGeometry/setLights guard against swapping/uploading resources while the queue has outstanding submissions. shutdown() refuses with pending work. safeToSample(scheduler) is deliberately conservative: it returns false while ANY probe is dirty or in flight and while batches are outstanding, so stale GI cannot be presented through the opt-in overlay.

Critical boundaries: The queue is currently a separate optional orchestration class, not automatically integrated with GiDynamicRuntime or game/editor render hooks. Host must keep GiGpuSmallScene/Device alive during pending work, drive callbacks/poll on device thread, always consult safeToSample before binding GPU output, and avoid bypassing the queue's geometry/light mutation guards through direct backend calls. The queue protects CPU scheduling and borrowed resource lifetime only when the host obeys this contract; it cannot stop an application from drawing the texture incorrectly or destroying Device first. Metal readBufferAsync callback may be delayed to the next beginFrame. Do not mark a dispatch complete immediately after endFrame. The GPU writes atlas in place; until per-brick double buffering is developed, use conservative full-volume sampling suppression during any dirty or pending work.

Test source: gi_gpu_ticket_queue.cpp uses a FakeDevice with a manually controllable async callback, checks in-flight ticket counts, post-invalidation stale rejection, GPU error abort/retry, delayed success, blocked geometry mutation while outstanding, mandatory explicit confirmation when readBufferAsync is unsupported, shutdown refusal during unconfirmed GPU work, and final resource cleanup. It is a CPU/FakeDevice fixture, NOT actual Metal API validation.

ACTUAL validation performed: inspected Gfx.h and Metal GfxMetal.mm readBufferAsync semantics; GitHub source writes and CTest registration reported success. NOT RUN: native build, CTest, actual callback timing, MSL compile, Metal/GLES screenshots, GPU timing, ./run-tests.sh. No tests have been claimed passing.

Next: Native compile/build all 14 gi_* CTest targets, run CTest and actual Metal shader/API validation with GPU output readback. Fix failures. Next feature stages should (1) expose a cohesive GPU runtime owner that chooses CPU/GPU fallback but cannot accidentally bind partial/dirty output; (2) refit/stream per-chunk GPU BVH with generation ownership; (3) camera-scrolling clipmaps, probe relocation and sparse per-brick validity rather than full-volume hiding; (4) include texture-RHI branch prerequisites only when rebased/isolated and tested. Keep main untouched and append next separate handoff.

## Stage 15 - 2026-11-19 - guarded opt-in Metal GI runtime (source committed; NOT RUN)

Code commits:
- 726c279f358c33ae4650b539da3156cbcda76996 - engine/render/GiGpuDynamicRuntime.h
- 86821f8a58d1bbded0525a132e6917c8b5696c64 - tests/render/gi_gpu_dynamic_runtime.cpp
- c015eb4d9dd516404d2fd11a6a459e8af4c64623 - CMakeLists.txt, gi_gpu_dynamic_runtime in default ALL/CTest

Architecture/API: GiGpuDynamicRuntime is a separate Metal-only opt-in host facade, composing GiGpuSmallScene (GPU BVH/Metal L1 transport), GiGpuTicketQueue (async GPU completion fence) and GiUpdateScheduler. initialize(device,origin,spacing,nx,ny,nz,samples,maxTraceDistance) and shutdown() own resources; only scenes without in-flight GPU work can update/release geometry or lights. setGeometry(shared_ptr<const GiChunkedGeometrySnapshot>, revision) owns the immutable scene snapshot for all pending jobs and invalidates all probes. setGeometryRegion(scene,revision,changedWorldAabb,extraRadius) preserves unaffected probe output, expands dirty bounds conservatively by trace distance and moving light radius, and rejects changes while jobs are pending. setLights(vector<GiMovingLight>) validates and compares stable-key old/new moving lights and invalidates only their influence zones. updateFrame(budget,camera) polls RHI-confirmed callbacks, claims bounded work and encodes asynchronous compute; results report accepted/stale/aborted and currently dirty probes. bind(Renderer3D&) ONLY enables the dynamic atlas if no scheduler probe is dirty or in flight and no GPU fence is pending; otherwise it clears the borrowed overlay to avoid presenting partially updated GI. unbind() clears the borrowed binding. No frame rendering or ECS source is modified automatically.

Fallback: GiGpuDynamicRuntime initializes only on Metal with supported compute and RGBA16F storage format; other backends still use the previously existing GiDynamicRuntime CPU reference and baked lighting. This is NOT a global runtime auto-selector. No source nor shader-graph/terrain/water integration is enabled by default. A caller must create an appropriate renderer and choose which runtime to use.

Key safety contract: All updateFrame calls run after Device::beginFrame and outside render passes, as Metal readBufferAsync queues a blit after compute in the same command buffer and invokes callback on the device thread after that command buffer completed. A GPU dispatch is never considered complete from submission alone. setGeometry/setLights cannot replace GPU buffers during pending work through the facade. Immutable geometry snapshots are held via shared_ptr; callbacks do not capture runtime pointers. The caller must keep this runtime, Renderer3D and gfx::Device alive in correct order; invoke unbind before shutdown, and drive updateFrame/queue polling until pending work finishes. shutdown returns false when jobs remain outstanding; do not destroy the runtime in that state. A missing RHI async fence requires an independently verified external completion token. The GPU output remains single buffered, so safeToSample is conservative; eventually per-brick validity and double-buffering are needed.

New test fixture: tests/render/gi_gpu_dynamic_runtime.cpp uses FakeDevice with manually controlled GPU completion and a 4-probe volume. It verifies initial dirtiness, dispatch, refusal to mutate geometry/lights while jobs are pending, no presentation before completion, successful clean presentation afterward, regional scene update triggering recalculation, invalid AABB rejection and cleanup. Tests are registered but were not run.

Actual validation: GitHub source read/create/update, RHI Metal readBufferAsync source inspection and GitHub commit acknowledgement. NOT RUN: native C++ build, CTest, Metal shader compile, GPU render/readback/timing, texture-RHI compatibility, full run-tests.sh. Do not claim production readiness. Model shader opt-in binding uses HDR medium+ and borrowed atlas; it has not received a real GPU visual acceptance test.

Next stage: run native configure/build (all 15 gi_* targets), focused CTest and full ./run-tests.sh --quick, then real MSL compilation/Metal validation. Implement bounded per-clipmap-brick GPU buffers and validity masks for streaming worlds (currently one dense <=20000 probe volume and entire volume hidden while dirty). Add robust GPU geometry retirement/fences and a safer dual atlas for async commits; once verified, wire to actual TerraWeave/PHYSIS/NEREID ECS presentation events. Avoid touching main or merging the divergent texture-RHI branch without a compatibility gate.

## Stage 16 - 2026-11-19 - toroidal camera-following GI probe volume (SOURCE COMMITTED; NOT RUN)

Code commits:
- 0450f815da9d3b1850fc97a8f3872d27e6d5ee48 - engine/render/GiClipmapVolume.h
- 9cb02529da267a1557131d2b03d79a8ae83359f6 - tests/render/gi_clipmap_volume.cpp
- 390964467c98257a33a34cffb99fd6a5bfc123bc - CMakeLists.txt, gi_clipmap_volume registered in default ALL/CTest.

API: GiClipmapVolume::configure(worldAnchor,spacing,nx,ny,nz,cameraFocus) allocates a fixed <=20K probe lattice. Physical atlas indices are positive-modulo of stable absolute integer world-cell coordinates, not their linear logical index. originCell(), originWorld(), ringOffset(), cell(id), physicalIndex(cell), contains(cell), position(id) expose the stable mapping for a future renderer uniform and GPU jobs. scroll(focus,&recycledIndices) snaps origin to global-grid integer cell boundaries, retaining overlapped physical slots and marking only entering slabs dirty; scroll refuses (blocked=true) when any claimed probe is still in flight, preventing a stale GPU write to a recycled atlas slot. invalidate/ invalidateAll, claim(budget,focus), complete(ticket), abort(ticket), pending/inFlight match the existing scheduler ticket API; revisions stop in-flight superseded jobs from incorrectly clearing dirtiness. complete() marks valid only for the matching current revision, intended for GPU fence acknowledgement.

Constraints: This is a new presentation-only scheduling abstraction. It is NOT yet wired to GiGpuDynamicRuntime/Renderer3D, not a multi-cascade shader, and not a live GPU clipmap. Existing legacy GI/bake paths and shader texture slots are unchanged. No new GPU allocations on scroll; this scheduler does NOT own the atlas or automatically clear recycled texels (it returns recycled IDs). The renderer must suppress sampling until recycled/dirty slots are produced, or maintain per-slot GPU valid bits. Ring addressing uses an absolute world grid and includes correct negative-coordinate floor-mod; GPU interpolation needs matching U6 ring offsets in a subsequent stage. Fast camera teleport can recycle all cells (expected).

Test source (unexecuted): gi_clipmap_volume verifies logical->physical negative world-cell mapping, near-camera priority and completion, single-cell camera move retaining 12/16 slots and changing four, no-op same-cell move, blocking scroll while tickets are in-flight, full recycle on teleport, stale ticket rejection, negative nonzero-anchor mapping, input rejection. Always-on AIVERSE_TEST_CHECK is used.

ACTUAL validation: GitHub fetch, source create, and CTest CMake registration commits succeeded. NOT RUN: C++ native compilation, focused tests, full run-tests.sh, GPU model shader compilation/render and perf benchmarks. Don't mark this stage tested or ready to merge.

Next exact step: Extend opt-in GLSL/MSL dynamic GI sampling to accept x/y/z ring offsets without changing the non-scrolling default, and add overload to Renderer3D setDynamicGiOverlay that accepts ring offset. Then make GiGpuTicketQueue::submit/poll/safeToSample accept a scheduler satisfying the same GiProbeTicket interface (or use a separate safe adapter). Implement an opt-in camera-scrolling GPU host runtime using GiClipmapVolume and the existing Metal backend, keep all defaults off and avoid stale atlas sampling. After code commit append its own handoff before continuing. Later add multi-level cascade weighting and GPU BVH per-chunk updates.

## Stage 17 - 2026-11-19 - opt-in scrolling Metal GI runtime and toroidal GLSL/MSL sampling (SOURCE COMMITTED; NOT RUN)

Implementation commits:
- e23576d32b538bb8a00661f263a23be0dc5a0f1e - engine/render/GiGpuTicketQueue.h, templated scheduler acceptance and strictly one in-flight batch to avoid unsafe GPU job-buffer reuse
- 5927ac7f96ba58eff0e358edf1d2415560fbd246 - engine/render/GiDynamicShaders.h, third U6 float4 ring offset and toroidal probe index in GLSL and MSL
- e6bcae9751e97e7fb575d151c583a8dbd4b94120 - engine/render/Renderer3D.h, optional ringX/Y/Z args (default 0) to existing setDynamicGiOverlay
- d080a5a5019d757be5803ee72094756ada5e70f6 - engine/render/Renderer3DModels.cpp, 12-float U6 binding, validate ring indices, select RGBA16F for compute-writable atlas and RGBA32F for old CPU atlas
- b3cdf75a9deded2dd8a44b5fc77de9920e1679ad - engine/render/GiGpuClipmapRuntime.h
- 4cfe7267380d3dc48c302ada70cf32863fc9ae06 - tests/render/gi_gpu_clipmap_runtime.cpp
- eeb1bad80302d8de4417406350361eaa01b417b9 - CMakeLists.txt: new gi_gpu_clipmap_runtime test in ALL/CTest.

Architecture: The opted-in Metal GiGpuClipmapRuntime composes GiClipmapVolume (toroidal resident slots), GiGpuSmallScene (Metal BVH transport), GiGpuTicketQueue (real RHI async completion acknowledgement), and the existing Renderer3D dynamic indirect overlay hook. initialize(device,worldAnchor,spacing,nx,ny,nz,initialFocus,samples,traceDistance) allocates exactly one fixed-size RGBA16F GPU atlas and probe volume. The host publishes an immutable GiChunkedGeometrySnapshot with setGeometry(shared_ptr,revision); setGeometryRegion invalidates local influence; setLights invalidates old and new moving-light influence conservatively. updateFrame(budget,focus) polls completed GPU fences then attempts camera snap/recycle, deliberately postponing a scroll while any batch remains pending; it dispatches only newly dirty physical slots. bind() checks all dirty/inflight states and the GPU fence queue, then passes logical world origin + 3 positive-modulo ring offsets into Renderer3D U6; the shader converts logical tetrahedral neighboring coords to stable physical atlas slots. Old fixed-grid call sites use default ring offsets of zero. A full teleport recycles all slots but reallocates no atlas.

Render safety: To avoid the single job-buffer/atlas overwrite race, GiGpuTicketQueue permits only ONE outstanding batch per backend. No dirty or incomplete texture is sampled: bind() clears the dynamic overlay when the GPU is pending and only enables it when the full active clipmap is clean. This can visually temporarily remove dynamic indirect during fast camera movement; per-brick valid flags/double buffering are future work. Geometry and lights cannot change during in-flight jobs via the runtime APIs. Old scene snapshots are shared_ptr-owned. Host must call unbind before destroying renderer/runtime and ensure shutdown() returns true; no GPU/Metal dispatch from inside a render pass. This is a single scrolling volume, NOT yet the full 2-4 cascaded GI hierarchy.

Tests added (SOURCE ONLY): gi_gpu_clipmap_runtime uses manually completed FakeDevice Metal readback, verifies initial 16 probes, 1-cell camera movement preserving 12/16 slots, blocked scrolling while GPU work is pending, resume after fence and only 4 new jobs, ring offset shader macros, teleport recycling all 16, and cleanup. Existing test suite must also be run because Renderer3D GLSL/MSL and U6 ABI changed for all opt-in dynamic GI variants.

Compatibility: CPU GiDynamicRuntime and old baked lighting defaults are preserved. GLSL/MSL dynamic GI U6 grew from 8 to 12 floats, with last float4 defaulting to zero when no ring mode requested; no persistent cache format changed, no gameplay/ECS state ownership. Metal Compute path remains off unless explicitly constructed; unsupported RHI has the existing CPU path.

ACTUAL validation: GitHub fetch/write/commit acknowledgements and static API inspection only. NOT RUN: C++ compilation, CTest, Metal GLSL/MSL shader compilation, actual device GPU readback/visual parity, ./run-tests.sh, FPS/perf measurements. Do NOT report test PASS merely because a fixture exists.

Next: (1) Pull branch and run all 17 gi_* targets plus full ./run-tests.sh --quick and real Metal shader validation. Fix source/compiler errors and record exact tested SHA/logs. (2) Implement multiple nested clipmap volumes with explicit total probe budget and deterministic center/far selection; each is independently bounded, memory and per-frame work cap. (3) Add chunk-resident GPU BVH memory updates/refits without flattening every chunk on each edit. (4) Add per-brick atlas validity, GPU double-buffering and real GPU completion/performance gates, TerraWeave/PHYSIS/NEREID/foliage producer integration; never auto-enable unverified GI or merge into main.

## Stage 18 - 2026-11-19 - reused per-chunk BLAS + packed TLAS GPU packet (SOURCE COMMITTED; NOT RUN)

Implementation commits:
- 76bf7905b3af2ee0c47917113e91ba62ea477748 - engine/render/GiChunkedGeometry.h, read-only forEachChunk stable-ID enumeration
- 50445b6501c04af1327461514748ffc3fc980fba - engine/render/GiGpuBvh.h, fromPackedTriangles and CPU BVH single-child traversal
- 8fe6cdd50e14083019197ffe49f7d64f10bf7456 - engine/render/GiGpuChunkBvhCache.h, reusable immutable per-chunk BLAS and TLAS packing
- cc50df82360849c2f474f4f843040a73da8fc140 - engine/render/GiGpuSmallSceneShaders.h, Metal TLAS leaf with UINT32_MAX sentinel for single-child BLAS refs
- afd9b76023ad865661a97b9f3b0d408d3b8bf7b8 - engine/render/GiGpuSmallScene.h, setPackedGeometry preserving single Metal BVH ABI
- 74024ae59898764b36ef4aeb548a97847c50226f - engine/render/GiGpuTicketQueue.h, completion-fence guarded setPackedGeometry
- 01239653b886741ad4eaa3d09e6d1c903a13580d - tests/render/gi_gpu_chunk_bvh_cache.cpp
- aafbc5095e17711d71d6dfc91467c9e24ee02ea2 - CMakeLists.txt, gi_gpu_chunk_bvh_cache in default ALL/CTest.

Architecture: GiGpuChunkBvhCache::refresh(GiChunkedGeometrySnapshot) uses an ordered map of stable chunk IDs to shared_ptr-held immutable per-chunk GiGpuBvh BLAS. The source StaticShadowGeometry pointer identity is the cache key; a new per-chunk immutable BVH is built only when that source pointer changes, and removed chunk IDs are pruned. Stage is transaction-safe on source validation errors: prior map is replaced only once all chunk BLAS builds succeed. Cache::pack emits a combined GPU packet using a small deterministic top-level AABB BVH (TLAS) over chunk roots. A top-level leaf is an internal node with meta[0] pointing to its chunk BLAS root and meta[1] equal to UINT32_MAX; both CPU GiGpuBvh::trace and Metal cs_main skip the absent second child. Each cached BLAS node/triangle index is rebased into the combined node/triangle arrays. The Metal dispatch ABI remains GiGpuBvhNode=48 bytes, GiGpuBvhTri=64 bytes; the kernel's single-root-at-index-zero assumption is preserved.

API: GiChunkedGeometrySnapshot::forEachChunk(callback(id,bounds,const StaticShadowGeometry&)) exposes immutable chunks. GiGpuBvh::fromPackedTriangles(triangles) independently builds one chunk's ray BVH; GiGpuChunkBvhCache::refresh(snapshot) returns reused/rebuilt/removed counts; chunkBvh(id) permits debugging pointer reuse; pack() returns a combined GiGpuBvh. GiGpuSmallScene::setPackedGeometry(packet,revision) swaps GPU triangle/node buffers only after BOTH new allocations succeed, with a 8192-triangle cap. GiGpuTicketQueue::setPackedGeometry(backend,packet,revision) refuses while GPU jobs are in flight.

IMPORTANT NOT YET IMPLEMENTED: The GPU packet still concatenates all unchanged BLAS data and fully uploads the two GPU buffers on each geometry revision. Only expensive CPU *per-chunk BVH construction* is incremental. Do NOT claim native GPU partial buffer updates, GPU BVH refits, complete large-world residency, SDF clips, or upload budget savings. Old geometry snapshots remain shared_ptr-immutable, and callers must hold them alive. Never call setPackedGeometry on GiGpuSmallScene directly when jobs are pending: use the queue guard. Existing GPU prototype/CPU bake/default runtime remain opt-in and backward compatible.

Unexecuted regression source gi_gpu_chunk_bvh_cache creates 60 stable-ID chunks, checks 60 cold builds / 60 warm reuses / one changed BLAS rebuild and 59 reuses / chunk deletion, compares closest hits and colors of packed TLAS+BLAS against GiChunkedGeometrySnapshot, checks pruning and FakeDevice Metal packed buffer upload, validates GPU resource cleanup. Release-safe AIVERSE_TEST_CHECK is used.

ACTUAL validation: GitHub source reads and writes / CMake registration succeeded. NOT RUN: native C++ compilation, CTest, xcrun Metal shader validation, GPU BVH parity / render output / performance, full ./run-tests.sh. Preserve the source SHA and log when eventually run. Do not merge main yet.

Next bounded stage: (1) Pull branch and compile/run all 18 gi_* CTests and ./run-tests.sh --quick. (2) Implement true sparse GPU chunk residency: stable buffer arenas with per-chunk ranges, optional TLAS root buffers, Device::updateBuffer partial writes outside render encoders and fence-safe allocation/retirement; respect immutable Metal storage-buffer update semantics and Vulkan/GL capability fallback. Check GPU memory/upload bytes by moving one tree chunk. (3) Integrate chunk cache into opt-in GiGpuClipmapRuntime with a fence-guarded setter while retaining scene ownership. (4) Move toward 2-3 nested clipmap volumes with camera-priority work, per-brick atlas validity and visual acceptance.

## Stage 19 - 2026-11-19 - sparse GPU node/triangle uploads and clipmap cache adoption (SOURCE COMMITTED; NOT RUN)

Code commits:
- ff5ee9c53f6c3605565cce474ad5738b49cf0bd2 - engine/render/GiGpuSmallScene.h, delta upload of matching-layout GPU BVH packets and upload stats
- fbe91c3443be7a3f59a7db5fc999194a933df507 - tests/render/gi_gpu_bvh_sparse_upload.cpp
- 7ca678582fc2a99ebc28217e3acc8ccd135337eb - CMakeLists.txt, gi_gpu_bvh_sparse_upload CTest
- a9a801f06f516591a98f6b3393af8d6996df648a - engine/render/GiGpuClipmapRuntime.h, optional setGeometryCached/setGeometryRegionCached via GiGpuChunkBvhCache
- cac4837ba380ae3bb6bc2f74a247a579b6cca5f3 - tests/render/gi_gpu_clipmap_cache.cpp
- cb07b63dae1566c84b7b8f1d79fd47655de3b40e - CMakeLists.txt, gi_gpu_clipmap_cache CTest.

Implementation: GiGpuSmallScene::setPackedGeometry now retains the last packed CPU node/triangle vectors and reports GeometryUploadStats {full,bytes,regions}. When both buffer element counts remain unchanged, it computes runs of changed 64-byte triangles and 48-byte nodes with byte-for-byte comparison and issues Device::updateBuffer(handle, runData, runBytes, byteOffset) only for differing runs. The GPU buffer handles stay stable across this fast path. For first publish or changed buffer sizes it allocates new full buffers transactionally and reports full=true; unchanged packets with a new revision cause zero GPU writes. This is an actual partial GPU BUFFER UPDATE API path, not only a CPU cached BVH. Metal's RHI Storage buffer update uses staging/blit and queue ordering; the host MUST call only while no GI compute dispatch is pending. The GPU code still works on bounded <=8192 triangle scenes with the same 48-byte node/64-byte triangle shader ABI, 384-wide RGBA16F atlas and one in-flight batch safety policy.

Host integration: GiGpuClipmapRuntime::setGeometryCached(shared_ptr<const snapshot>,revision,GiGpuChunkBvhCache&, optional Stats*) or setGeometryRegionCached(snapshot,revision,dirtyBounds,cache,extraRadius,optional Stats*) reuses unchanged per-chunk CPU BLAS, packs the TLAS/BLAS, passes it through GiGpuTicketQueue::setPackedGeometry fence guard, retains snapshot ownership only on successful upload, and invalidates all or the affected clipmap cells. The older setGeometry()/setGeometryRegion() remain for callers that do not need a cache. The cache object is caller-owned and can be reused across frames. If GPU upload fails the cache may have advanced to the new snapshot, but caller can retry refresh/pack with the same scene.

Critical limits: Sparse GPU writes apply only when combined buffer sizes and stable ordering remain unchanged; adding/removing triangles/chunks, reordering, or a change in TLAS layout can force larger writes or a full reallocation. We still copy/repack the combined arrays on CPU for each geometry revision and perform O(total elements) comparisons. We do NOT yet own fixed per-chunk GPU offsets, free lists, persistent vertex pages or hardware traversal. No asynchronous upload job buffering across different generations, so GiGpuTicketQueue's no-pending requirement is mandatory. RHI::updateBuffer has void return and errors may be backend-specific; no Metal validation or performance proof. No automatic ECS/TerraWeave/foliage adoption.

New source-only tests: gi_gpu_bvh_sparse_upload checks a 64-chunk initial full GPU packet, then edits one same-size chunk and expects strictly smaller total GPU upload byte count and unchanged buffer handle count; a no-op revision emits zero updates; removal of a chunk triggers full-upload fallback. gi_gpu_clipmap_cache combines two chunks, scrolling GPU runtime, async FakeDevice frame callbacks, per-chunk BLAS reuse, sparse upload stats, restricted geometry mutation while GPU work is pending, and successful fence-acknowledged cleanup. Both use always-on AIVERSE_TEST_CHECK and CTest registration, but they are NOT yet executed.

ACTUAL validation: inspected live Metal RHI storage update semantics via GitHub source; GitHub source creation, CMake edits and repository commits succeeded. NOT RUN: C++ build, CTest, xcrun Metal shader compile, real GPU readback/render, ./run-tests.sh --quick, macOS/Android timing. Do not claim performance improvements without actual GPU timings.

Next agent work: (1) Pull feature/hybrid-dynamic-gi, build all 20 registered GI CTest targets, run ctest --test-dir build/gi --output-on-failure -R '^gi_' and full ./run-tests.sh --quick; report exact tested SHA, compiler warnings, results and MSL validation. (2) Implement true per-chunk GPU residency with stable offsets, partial TLAS updates, fence-safe retirement, then measure bytes/time for falling trees and terrain sculpting. (3) Add 2-3 nested scrolling clipmaps with independently capped GPU memory/updates and blend between levels; improve per-slot validity and double-buffered publication before broad runtime enablement. (4) Integrate with TerraWeave/PHYSIS/NEREID read-only ECS change snapshots and live GPU visual benchmarks. No merge into main without acceptance.

## Stage 20 - 2026-11-19 - strong ownership of cached GPU chunk BVHs (SOURCE COMMITTED; NOT RUN)

Code commits:
- c6b1391c327e09fe7e9730201512e68d69d51855 - engine/render/GiChunkedGeometry.h, forEachChunkShared stable immutable shared_ptr enumeration
- e814a0f98074125b8df06edcb7541c2b6792b484 - engine/render/GiGpuChunkBvhCache.h, retain strong source ownership and expose chunkSource diagnostic
- 10a75dfe6230028aa376b7c71d3cb830ff9c54ea - tests/render/gi_gpu_chunk_bvh_lifetime.cpp
- e01e08648359a800367c72812f1d0bcf52f7164a - CMakeLists.txt, gi_gpu_chunk_bvh_lifetime in standard ALL/CTest.

Problem fixed: the Stage 18 GPU chunk cache originally used a raw pointer to StaticShadowGeometry as its unchanged-chunk identity key. When the producing GiChunkedGeometrySnapshot was destroyed, a pointer could dangle and later coincidentally be reused for a different geometry allocation. GiChunkedGeometrySnapshot::forEachChunkShared now safely enumerates immutable shared_ptr<const StaticShadowGeometry>. The cache entry retains this strong immutable owner and compares shared_ptr source identity across generations. Unchanged chunk BLAS memory remains reused after a snapshot goes away; a removed cache entry releases the source only when other owners also release it. chunkSource(id) is for debugging/ownership checks and should not be used to mutate the geometry.

Compatibility: no change to GPU BVH ABI, shaders, texture formats, cache serialization or ECS save/network; producer-facing forEachChunk remains available, while a new strong-owner enumeration is used for cache lifetime. Still bounded to 8192 triangles and one compute batch/fence in the experimental GPU backend. Stage 19 incremental GPU writes remain opt-in and subject to stable layout constraints. Metal GPU updates and memory retirement are only safe via the ticket-queue API after completed readback.

Test source (NOT RUN): gi_gpu_chunk_bvh_lifetime creates one immutable chunk, holds a weak_ptr to its geometry, populates cache, destroys scene/builder, checks cached source stays live and traceable, then refreshes cache with an empty snapshot and checks source destruction. Uses always-on AIVERSE_TEST_CHECK.

ACTUAL validation: GitHub source inspection / updates and CTest registration success. NOT RUN: native C++ compilation, CTest, Metal shader validation, GPU execution/timing and full ./run-tests.sh. Do not claim test PASS.

Next: Pull branch and compile/run all 21 registered GI tests in Debug and Release; run ctest -R '^gi_' and ./run-tests.sh --quick, record exact tested SHA and logs. Check MSL shader compile/Metal API Validation and actual GPU result parity. Then implement cascaded clipmap manager (2-3 radii), pixel-space blending across cascade borders, GPU per-brick valid masks, robust GPU BVH allocation/reuse/retirement under async fences, integration with TerraWeave/PHYSIS/NEREID read-only ECS world change notifications and real iOS/Android performance profiling. Do not auto-enable/merge into main until acceptance.

## Stage 21 - 2026-11-19 - bounded GPU cascade manager (source committed, native NOT RUN)

Implementation commits:
- e70b668a90fb54024b6309a8880bb4b568d343bb - engine/render/GiGpuCascadeRuntime.h
- 45f841fa363b66c1dacc9aeb12a46efdc51723fd - tests/render/gi_gpu_cascades.cpp
- 4457e1e60f86fed5d82bcfb16c51189c2d5a6313 - CMakeLists.txt, gi_gpu_cascades registered in normal ALL and CTest.

API/architecture: GiCascadeConfig supplies common world anchor, finest spacing, spacingRatio (1.5..8), probe dimensions, 1..3 independent levels, maximum TOTAL resident probes (<=20000), samples and traceDistance. GiGpuCascadeRuntime::initialize(gfx::Device&, cfg, camera) constructs individually owned GiGpuClipmapRuntime levels with exponentially growing spacing; rejects unsupported Metal compute or over-budget configuration without silently enabling in default renderer. shutdown() rejects GPU work in flight. setGeometry(shared immutable snapshot,revision), setGeometryRegion(snapshot,revision,bounds,radius), and setLights(stable-id vector) propagate to all levels only while no level is busy. updateFrame(totalProbeBudget,camera) first polls all completion fences, reserves one update for each dirty eligible level, then distributes the remainder finest-first with a per-level 32-probe batch cap. Frame stats report actual submitted/remaining budgets, pending/in-flight probes and completed levels. bindFinestComplete(Renderer3D&) is explicitly a SINGLE-source fallback, not dual-cascade shading: it chooses the finest fully clean level, then the next available; no light from different levels is blended by this method. No ECS save/rollback changes; existing CPU/baked GI remains the default.

Test fixture source uses FakeDevice Metal with explicit completion callbacks and 3 cascades of 4x2x2 probes: verifies total budget=10 produces 8/1/1 submissions, no binding while first GPU commands are pending, GPU callback progress to clean state, near-level 1-cell camera movement recycles 4/16 and leaves coarser volumes intact, and rejects excessive/invalid configuration without leaking buffers. Uses Release-safe AIVERSE_TEST_CHECK. There is still NO real graphics device or shader compilation test.

ACTUAL verification: GitHub fetch/write acknowledgements, code inspection and CTest registration only. Native C++ configure/build, CTest, full ./run-tests.sh, Apple MSL compile/readback, Vulkan/Android and benchmark: NOT RUN. Do not call this production-ready.

Limitations/next step: implement real pixel-space blending at clipmap borders using two or three separately sampled cascade atlases and independent validity. Existing model shader tex13 is the one-level atlas; Shader Graph textures start at slot 13 and must remain excluded until resolved. Avoid claiming bindFinestComplete already blends. Prefer adding an independent CPU reference blend-policy test, then a guarded dual-cascade shader permutation (Metal fragment textures 13/15; extra data/mask 14/16) and explicit renderer state. Later permit 3 cascades with a controlled sampler/texture budget. The in-place GPU atlas cannot be presented with dirty probes without per-cell validity tracking; implement sparse validity next. Do not merge into main. Complete an independent handoff after each next code stage.

## Stage 22 - 2026-11-19 - CPU reference for smooth cascade blending and validity fallback (SOURCE; NOT RUN)

Source commits:
- 5412d22116eecf17c817839219a99610207f3739 - engine/render/GiCascadeBlend.h
- 1d7001bf349b7fbbbb40384867de2de8f1c2d33b - tests/render/gi_cascade_blend.cpp
- 7ffb63785cb68db086ac7ab16938b6c41addf22a - CMakeLists.txt, registered gi_cascade_blend under standard ALL/CTest.

Design: GiCascadeBlendLevel includes a world-grid origin, spacing, 3D dimensions, already evaluated linear indirect radiance, effective validFraction and fadeCells border width. giCascadeCoverage() computes smoothstep(0..1) based on the minimum world-space distance from receiver to a level's bounding volume, normalized by the desired probe-cell fade. composeCascadeGi(levels,count,receiverPosition) visits finest -> coarsest, assigns each level at most uncoveredWeight * coverage * validFraction, adds RGB only once, and reports levelWeights/uncovered. Outer absence deliberately contributes zero delta GI, leaving the separate baked/ambient base unmodified. Source light/direct sun must not be double-counted inside cascade inputs.

Tests authored but NOT RUN: center fine-only, edge mixing of fine and medium without overcounting, incomplete fine replaced by medium, 3-level fallback near outer boundary, zero outside coverage, invalid dimensions/validity. All use always-on AIVERSE_TEST_CHECK. Existing renderer still binds one complete level at a time; this CPU blend policy is NOT yet a GPU per-fragment blend and MUST NOT be presented as one.

Compatibility: header-only additive opt-in utility; no shader, GPU texture, legacy lightmap, ECS persistence or default quality changes. Bounded 1..3 level policy; it is a reference for the following shader implementation.

ACTUAL validation: GitHub source writes/CMake test registration. C++ compilation, CTest, full run-tests.sh, real Vulkan/Metal shader results and performance: NOT RUN.

Next bounded stage: implement compact GPU validity mask per level, update mask from GiClipmapVolume physical slot validity after confirmed GPU fence (and immediately invalidate mask on new dirty regions), and add a dedicated opt-in GLSL/MSL shader variant that samples two cascades, weights their incident indirect using the same smoothstep/validFraction policy, and does not steal shader-graph material texture bindings. Validate real Metal/GLES format/texture-slot limits. Do not merge to main.

## Stage 23 - 2026-11-19 - per-probe R8 validity for partially updated GPU clipmaps (SOURCE COMMITTED; NOT RUN)

Implementation commits:
- 0ee6981ccdc303ca7b9576677abb1f62a037215c, 2c58700b279a29d0a28866ba8aee2017b5ce5767 - engine/render/GiClipmapValidityTexture.h, R8 validity atlas owner and corrected validCount()
- c28015105c0e6620371844d08578d4a4ef613ae3 - engine/render/GiDynamicShaders.h, guarded tex14 sampling in GLSL and Metal
- 7e155c267b1ec6ae5a084546b351e8263b2e5720 - engine/render/Renderer3D.h optional validity owner pointer
- 336c82bffd4328b3de94c887d4dd1ce14e75aac3 - engine/render/Renderer3DModels.cpp additional DYNAMIC_GI_VALIDITY shader key, uniform/tex binding and R8 owner/device checks
- ab709ae25d35b145603275c1c6a03d013bb81a45 - engine/render/ModelShaders.h extra guarded MSL texture14 fragment argument
- dc6e6d04226b98fe68fe623b07221ec3ac42027a - engine/render/GiGpuClipmapRuntime.h validity synchronization with completed fences and partial binding
- 03f1fae510bc866984f85dd5dfe3c7918c56f528 - tests/cloth/FakeDevice.h shared FakeDevice handles R8 texture bytes instead of treating them as float4
- 4c0c2863a7fcd86a43ec58f276bcfaca8748c21a, 3b48a2e1343edd5e9a5192ccd259fa6beccad30c - update existing GI clipmap/cascade fixture expected partial-valid presentation
- 3672d4c4455ba5dc2295f61476103c9f9aaa916d - tests/render/gi_clipmap_validity.cpp
- 55be43685b932b337f21eecc731984e23ff84faf - CMakeLists.txt registers gi_clipmap_validity in standard CTest.

API/architecture: GiClipmapValidityTexture initializes a 128-texel-wide R8 data texture, one physical probe valid bit per texel. sync(GiClipmapVolume) compares current accepted slot.valid flags against uploaded bytes and batches adjacent dirty bytes by row using Device::updateTexture. Only a genuinely RHI-acknowledged successful GiProbeTicket completion makes a texel 255; dirty, recycled and in-flight slots remain zero. GiGpuClipmapRuntime owns this mask, synchronizes it after queue.poll() and camera scroll before rendering, refuses to bind after light/geometry change until the mask is resynchronized, and exposes validCount. Shader permutation DYNAMIC_GI_VALIDITY adds tex14 (Metal texture(14)) and skips individual invalid physical probe contributions when interpolating dynamic indirect from tex13; the older DYNAMIC_GI only shader path (no mask) remains supported. Renderer validates the borrowed mask belongs to the same Device and probe count. Dynamic GI can be partially shown from already-valid probes during a pending frame rather than dropping the entire volume. The atlas texture still uses a single writer/reader Metal command sequence; the mask is checked after RHI-complete callbacks only.

Important compatibility/safety: The mask occupies tex14 and is ENABLED ONLY when the validity owner pointer is supplied. Shader Graph texture slots start at tex13; the existing dynamic GI shader-graph exclusion remains mandatory. Metal sampler limits must be validated on actual hardware. Texture mask ownership is borrowed by renderer; unbind before destruction. A source change must call the runtime invalidation API before any render draw and call updateFrame to synchronize the mask. This is a single-level partial validity path, not per-pixel blending across cascades (stage22 CPU policy exists but GPU blend is still upcoming). Current per-probe weight does not renormalize missing neighbors; missing light falls back to the existing ambient/baked base, without false old bounce.

Tests written but NOT RUN: new gi_clipmap_validity checks init all-invalid, incremental valid updates, dirty-one invalidation, full 16-probe completion, one-cell scroll zeroing only the four recycled physical R8 slots, and shader ABI markers. Existing gi_gpu_clipmap_runtime and gi_gpu_cascades test fixture assertions have been updated for partial valid rendering. Fake RHI now correctly stores/uploads R8 byte format; native RHI tests remain untouched.

ACTUAL validation: GitHub source reads and commits, CTest registration and manual code review only. Native C++ compilation, full CTest and ./run-tests.sh, GLSL/Metal compilation, image parity, actual GPU fence timing and mobile performance: NOT RUN. Do not report PASS/production-ready.

Next: Implement a true dual-level GLSL/MSL DYNAMIC_GI_CASCADES shader variant (two dynamic atlases, two validity masks, distinct origin/spacing/dimensions/ring) with per-fragment smoothstep border fade and remaining-energy fallback consistent with GiCascadeBlend.h, without stealing Shader Graph texture bindings. Expose opt-in Renderer3D binding and have GiGpuCascadeRuntime use it when both cascade levels have any valid probes; keep the stage21 single-level fallback. Evaluate how to extend to a third level without exceeding texture/sampler limits. After that, run native compiler/tests and real Metal shader validation before any default enablement.

## Stage 24 - 2026-11-19 - opt-in dual-cascade GLSL/Metal PBR blending (SOURCE COMMITTED; NOT RUN)

Implementation commits:
- 9c5d7ca97fafe41a850aaa6c596b40063e13049c - engine/render/GiCascadeShaders.h, GLSL and Metal per-fragment dual clipmap mixture
- 264b8746c850aecc698d4b68fdb7e2b9b3ca4b79 - engine/render/ModelShaders.h, guarded shader inclusion and U7/tex15 MSL fragment bindings
- 87d35a74edae33d176e9d691754d8c1b11e04c98 - engine/render/Renderer3D.h, explicit optional far-cascade atlas pointer/params
- 686e44ec3cb59943c1727103de25b48453a8c598 - engine/render/Renderer3DModels.cpp, shader-key/texture-slot permutation and U6/U7 binding
- ad7f04d25540fdb3a170c5412413b0b70b9fa94a - engine/render/GiGpuClipmapRuntime.h, expose partial and fully GPU-confirmed rendering readiness
- 772162b4ccf19b96b83aeecabad7a75eadd3fe8f - engine/render/GiGpuCascadeRuntime.h, choose fine + first fully confirmed coarser level
- 52467464344b7f511c73f47f4dca70baae184ca1 - tests/render/gi_dual_cascade_shader.cpp
- 21aba638c023b98aa231be9c11d09a3eff723de2 - CMakeLists.txt, test gi_dual_cascade_shader in standard ALL/CTest.

Renderer API: first call setDynamicGiOverlay(fineAtlas,origin,spacing,nx,ny,nz,ringXYZ,fineR8Validity); optionally call setDynamicGiCascadeOverlay(farAtlas,origin,spacing,nx,ny,nz,ringXYZ) on the same Renderer3D. Only a compute-writable RGBA16F far atlas with matching device and dimension is accepted. setDynamicGiOverlay and clearDynamicGiOverlay clear the far owner to avoid stale cross-frame binding. The shader requires DYNAMIC_GI, DYNAMIC_GI_VALIDITY and DYNAMIC_GI_CASCADES together. Shader key uses a separate reserved bit; default and single-cascade shader variants stay intact. U6 contains near origin,size,ring, U7 far origin,size,ring; tex13 near L1, tex14 near R8 validity and tex15 fully-ready coarse L1. This occupies at most slots 0..15 (16 textures) and avoids Shader Graph texture conflicts by retaining the existing dynamic-GI shader graph exclusion. Metal textures are sampled with .read, so no additional sampler slot is required.

Blend algorithm: For each fragment, tri-linear-like tetrahedral neighbors are fetched from both ring-mapped volumes. The fine clipmap multiplies each probe weight by its R8 validity, and has a spatial coverage smoothstep over 1.5 probe cells at the volume boundary. The far clipmap must be FULLY RHI-confirmed and therefore needs no second mask. Both are indirect-only L1 coefficients, so the final delta is fineWeightedRGB + (1 - fineWeightedCoverage) * coarseWeightedRGB. This preserves a single energy budget, smoothly fills incomplete fine slots from the coarse GI, and does not add direct lights or duplicate baked indirect. An optional third cascade can act as the coarse fallback if level 1 is incomplete and level 2 is ready, but this shader currently samples only TWO at a time; a real three-sampler runtime still needs its own shader plan and hardware caps. GiGpuCascadeRuntime::bindCascades(Renderer3D&) chooses the fine level if it has any ready probes, then selects the finest FULLY confirmed coarser volume; otherwise falls back to bindFinestComplete() (which now means finest renderable, possibly partial R8). Must call after updateFrame before the render pass, unbind before deleting borrowed atlas owners.

Compatibility: Added only opt-in shaders/state on the GI branch. Baked GI, CPU GiDynamicRuntime, ECS replay/network, AVGIC005, Shader Graph and default material presets remain unchanged. Full shader variant requires HDR and Medium/High material quality, as in prior GI opt-in renderer guards. No automatic game/editor activation or backend selection.

Validation ACTUALLY performed: GitHub source creation and update operations with commit confirmation; registered test gi_dual_cascade_shader. Native C++ build, Debug/Release CTest, GLSL ES3 and MSL shader compilation, Metal API Validation, GPU screenshots/readback, real timing, run-tests.sh: NOT RUN. The source test checks CPU overlap/validity blending reference and shader bindings/marker strings only; it does NOT establish shader compiler or actual frame parity. The far-binding RHI texture limit and U7 uniform buffer need real device test before enabling this feature in production.

Next agent action: pull branch, configure/build all 25 GI CTest tests and run ./run-tests.sh --quick; ensure shader variants compile on Metal and GL ES 3, including LIGHTMAP/PROBES, skinned, morph, and standard PBR. Run an actual two-cascade colored-wall visual fixture and compare against GiCascadeBlend.h. Instrument GPU time and streaming artifacts under sustained camera motion. Then implement fully per-brick validity and selective coarse fallback without requiring entire far clipmap complete, and hook TerraWeave/PHYSIS/NEREID read-only dirty geometry to GiGeometryScene. Never merge without a native validation checkpoint and a separate handoff for fixes.

## Stage 25 - 2026-11-19 - Metal-only three-cascade PBR blending (SOURCE COMMITTED; NOT RUN)

Implementation commits:
- 33c0c0d3422cc2e44efdc68e4d661ed0432608ec - engine/render/GiCascadeShaders.h, Metal-only 3-level remaining-energy blend
- f595f284ab64b240d083d7c4c88b32c8438bbd83 - engine/render/ModelShaders.h, guarded third atlas/fragment uniform MSL arguments
- 1a7bae9349e6aad304d7f0b6442356f5a73bf5c5 - engine/render/Renderer3D.h, third atlas borrowed owner and explicit API
- 8578a68ce2af2309cc35cb8ce1086b4db463f3b6 - engine/render/Renderer3DModels.cpp, Metal-only shader key and tex16/U8 binding
- e19f8d098532541553824e84d26dda4893a61043 - engine/render/GiGpuCascadeRuntime.h, safe selection and binding of third completed level
- 4772a5988c22680b0432a423ecdd2a69b7ce26b9 - tests/render/gi_metal_three_cascades.cpp
- 74c27288778efd96cec13f5cbfe676d629067cbe - CMakeLists.txt registers gi_metal_three_cascades in ALL/CTest.

Architecture: A new DYNAMIC_GI_TRIPLE shader permutation is authored for **Metal only**. It reads physical clipmap L1 probe atlases tex13 (fine), tex15 (medium) and tex16 (distant), plus the fine R8 validity mask tex14. Near/mid/distant grid uniforms are U6/U7/U8 (Metal fragment buffers 7,8,9). The pixel shade uses fineRGB + (1-fineCoverage)*mediumRGB + (1-fineCoverage)*(1-mediumCoverage)*distantRGB, matching the finite energy accounting of the stage22 CPU composition. All light is indirect-only. The mid and distant sources MUST be fully RHI-completed/fence-acknowledged, so they require no additional validity textures. The fine source may be partially complete; invalid probe slots are skipped and the other two levels fill the missing coverage. At boundaries smoothstep fades over 1.5 cells. If no mid/distant complete levels, the pre-existing single or dual fallback is used.

API: Renderer3D::setDynamicGiThirdCascadeOverlay(third,origin,spacing,nx,ny,nz,ringX,ringY,ringZ) explicitly rejects non-Metal RHI, missing near+fine-mask+medium ownership, wrong atlas format, different device, invalid dimensions and out-of-range ring offsets. It retains the borrowed third volume only if validation succeeds. clearDynamicGiOverlay resets all 3 owner pointers; replacing the primary or medium atlas disconnects an existing third to prevent mismatched revisions. Renderer3DModels.cpp sets the third pipeline key only when the backend is Metal and resources are valid, and creates a 17-texture/9-uniform-slot shader descriptor. The Metal shader uses raw texture reads and **no new sampler** at texture(16). The existing GLSL shader continues with the 2-cascade path using tex13..15 and no new texture dependencies; GLES3/WebGL2 16-slot assumptions stay intact. GiGpuCascadeRuntime::bindCascades selects third only when the medium and distant levels both report fullyValid(); otherwise it leaves an existing safe 2-cascade binding. The old baked GI and CPU path remain opt-in/unmodified.

Test source: gi_metal_three_cascades checks the 3-level CPU blend, where near is half-valid and middle/distant replace uncovered portions, and asserts the Metal model shader has required tex16/U8/permutation/remaining-weight markers. It also checks GLSL never references tex16. This is NOT a GPU shader compilation test.

ACTUAL verification: GitHub fetch and source/create/update commit acknowledgements only. NOT RUN: CMake C++ build, CTest, GLSL/MSL compilation, Metal sampler/texture slot validation, real GPU screenshot/readback, FPS/benchmark and full ./run-tests.sh. 17 bound sampled textures on Metal must be checked on the actual target GPU before declaring it supported. Do not merge branch into main without native validation.

Next stage: bridge terrain and foliage/deformation changes from TerraWeave/WorldScene into GiGeometryScene using source-derived, genre-neutral read-only publication APIs. Do not invent producer field names; inspect current headers. Then build/render a stress fixture for camera movement and failing GPU completions. If a true 3-cascade mobile path is needed, consider a packed Texture2DArray/atlas after texture-RHI unification; do not silently use tex16 on GLES3. Add a separate stage handoff for all code.

## Stage 26 - 2026-11-19 - WorldScene/TerraWeave collision-to-GI presentation bridge (SOURCE COMMITTED; NOT RUN)

Implementation commits:
- 649601d2cac87f8a19550210bb7b296fd281bff0 - engine/render/GiGeometryScene.h, transactional publishGpu(Runtime&) with GPU fence refusal
- e6cec3594ff5fb8298cab458e49900c1c2f49cae - engine/world/GiWorldGeometryBridge.h
- 573776617be392e76a8cbfbaba67ca053606ac07 - tests/render/gi_world_geometry_bridge.cpp
- 007409df5929c8c6202f0d05afafba49ae9443cd - CMakeLists.txt registers gi_world_geometry_bridge in ALL/CTest.

Cross-branch source evidence: Reviewed existing engine/world/WorldAssets.h + WorldAssets.cpp and WorldScene.cpp in feature/hybrid-dynamic-gi: WorldAssets::origin(Cell) converts local collision triangles to world-space; readChunkCollision() appends terrain triangles to the end of ChunkCollision.triangles with the same diagonal as the visible WorldScene terrain. Reviewed feature/terraweave HEAD (NOT merged) engine/world/TerraWeave.h, TerraWeaveTerrainPatch.h, TerraWeaveEarthworks.h and engine/render/TerraWeaveTerrainPatchRender.h. TerraWeave TerrainPatch.collision already contains WORLD-SPACE collision triangles whose revision is TerrainPatch.sourceRevision, and TerrainPatchResident reports TerrainPatchDirtyRegion with cm RectCm bounds. TerraWeave TerrainChunk uses (resolution+1)^2 heightsCm at signed ChunkKey x/z and spacingCm. These exact public field contracts are mirrored as templates in this bridge, so NO TerraWeave files were copied, merged or cherry-picked from its actively developed branch.

New API in namespace aiverse::world: GiWorldGeometryBridge::stableChunkId(x,z,GiGeometrySource) reserves collision-free bit-packed source namespace + two signed 31-bit coordinates, rejects out-of-range keys. replaceAuthoredChunk(giGeometryScene, WorldAssets, Cell, ChunkCollision, authoredDiffuse, terrainDiffuse) converts local XZ-column collision triangles and boxes once through WorldAssets::origin(cell), distinguishes the trailing terrain triangles and uses bounded stable chunk IDs. replaceTerraWeavePatch(giGeometryScene,const TerraPatch&,albedo) is a templated adapter that directly copies WORLD-SPACE TerrainPatch.collision (NOT a second terrain mesh generator), validating the exact `(resolution-1)^2*2` authored collision triangle count. replaceTerraWeaveProcedural(giGeometryScene,const TerrainChunk&,albedo) is a separate opt-in template for unloaded cm-height-grid chunks, reproducing the same procedural two-triangle cell diagonal and positive/negative coordinate scaling; avoid using it simultaneously with the live TerraPatch for one tile unless desired as a distinct geometry source. remove(giGeometryScene,source,x,z) unloads a chunk without affecting other namespaces. submitDirtyRect(giWorldDirtyAccumulator, TerraWeave RectCm, minHeightM,maxHeightM, influenceRadiusM) converts cm XZ erosion/digging/shoreline dirty bounds into presentation invalidation events; it does NOT rebuild BVH by itself.

GiGeometryScene::publishGpu(GpuRuntime&) creates a new immutable chunk snapshot only after producer changes, then calls GPU runtime.setGeometry() on initial/overflow publication or setGeometryRegion() with the conservative UNION of old and new region AABBs for ordinary edits. The GPU runtime must accept the full replacement revision before GiGeometryScene commits its new current snapshot, increments revision and retires old snapshot. A refusal due to in-flight compute/fence does NOT alter current snapshot/revision, and preserves dirty-region records for a later retry. report.invalidatedProbes=0 means unknown/async for this templated GPU path, NOT zero actual dirty probes. GPU access must never occur before the authoritative WorldScene/TerraWeave publisher has completed its ECS/physics/mesh read barrier.

Compatibility and limits: This bridge and publishGpu are opt-in. Authoritative ECS, spatial collision/NavMesh, WorldScene/renderer default draw, legacy lightmaps and AVGIC005 caches are unchanged. The TerraWeave feature branch is separately developed and was NOT merged: templates instantiate only when an app links the actual TerraWeave headers. There is no automatic subscription to WorldScene::update(), TerraWeave TerrainPatchResident::takeDirty, falling tree physics, or NEREID water events; the host must explicitly call the adapter after the same revision is successfully published. GPU BVH still has <=8192 total triangles, and large procedural grid/forest proxies require selection, LOD and resident chunk budgets. Do not claim full-world GI works merely because this adapter exists. `replaceTerraWeavePatch` is preferred over double-generating the same surface. If terrain and authored terrain are active for the same column, the host must remove/hide the old geometry source, otherwise both could contribute GI occlusion.

Regression source tests (NOT RUN): gi_world_geometry_bridge verifies collision-free negative signed keys and source tags, local authored terrain/box conversion from a negative XZ column, world-space TerraWeave patch insertion, changed patch height + dirty region union, GPU publish denial while an asynchronous job is pending and later successful retry without losing previous snapshot, procedural cm-height grid and diagonal, cm-to-metre dirty bounds, chunk removal and invalid input rejection. Uses Release-safe AIVERSE_TEST_CHECK and a no-RHI FakeGpuScene sink.

ACTUAL verification: inspected GitHub source at the exact branches named above, committed the adapter and test, and registered CTest. NOT RUN: C++ compilation, CTest, full ./run-tests.sh, real TerraWeave branch linkage, Metal/GL shader compilation, device output/parity/perf. Do not report tests PASS.

Next stage: once TerraWeave branch lands or can be safely rebased, instantiate the adapter with actual TerraWeave::TerrainPatch / TerrainChunk in a native test and wire explicit TerrainPatchResident::takeDirty + successful collision publish to GiGeometryScene and GiGpuCascadeRuntime. Ensure old colliders and original terrain visuals are not double counted, include fallen-tree/vegetation proxy revisions and stable geometry owner lifetime. Before any merge to main, compile/build all registered GI tests, run CTest and ./run-tests.sh --quick and actual Apple Metal GPU diagnostics. Write results and exact tested SHA into the next per-stage handoff.
