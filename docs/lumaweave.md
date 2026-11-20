# LumaWeave — experimental massive direct lighting

Branch: feature/lumaweave-massive-lighting. Detailed stage handoff: docs/agent-handoffs/lumaweave-massive-lighting.md.

## Ownership

LumaWeave is DIRECT point/spot/approximated-area lighting and stochastic BVH shadows, NOT indirect GI. Hybrid Dynamic GI owns the separate bounced-light source/atlas, reusing the SAME GiGpuBvh. A renderer must choose EITHER the legacy LightSystem direct-light map OR LumaWeave Metal direct for one view. No effect is automatically enabled.

## Native API

- Stable and transactionally revised source registry: LumaWeaveRegistry, LumaLight
- Actual FX identity: ParticleSystem::collectLightsStable, LumaWeaveParticleBridge; unique host systemNamespace per ParticleSystem
- Multiple FX systems, authored lights, generic effect sources, area emitter samples: LumaWeaveNativeCoordinator::collectScene
- Point/spot candidate culling: LumaWeaveClusters, overflow forces complete source registry scan
- Standalone 1..8 sample Metal direct + shared GiGpuBvh shadows: LumaWeaveMetalDirect
- Depth-reconstructed fullscreen Metal direct + spatial/temporal denoise: LumaWeaveScreenMetal
- Genuine PBR model material receiver (linear base RGB, metallic alpha) and separate depth-matched HDR composition: LumaWeavePostProcess
- Native frame orchestrator (host still owns beginFrame/endFrame): LumaWeaveNativeDriver
- Optional point bounce bridge: LumaWeaveNativeCoordinator::syncGi
- Low-end mobile raster map fallback: LumaWeaveMobileFallback with dropped-source diagnostics
- CPU stress inputs: LumaWeaveStress, aiverse_lumaweave_bench (CPU planning only)

## Example frame order

Create Renderer3D and a working linear HDR PostProcess first. Construct LumaWeaveNativeDriver(device, renderer, existingPost), call initialize() (false on unsupported Metal/mobile is an acceptable fallback). The native game controls its ECS tick and one Device::beginFrame / endFrame. Before LumaWeaveNativeDriver::frame, publish immutable GiGpuBvh geometry with revision, gather authored LumaLight, stable FX system snapshots, and sampled area emitter sources; call ParticleSystem::update after the ECS presentation barrier. Driver.frame(viewInput,screenPass,callback) gathers lights, invokes renderer.beginScene, chooses an exclusive Metal/legacy route, invokes callback(Renderer3D&) so the game submits its opaque/translucent models, then renderer.render and renderer.resolvePostFx. Draw UI afterwards, then device.endFrame. Call luma.gpuPost().pollGpu() on later frames only after Metal beginFrame delivered completed callbacks. Keep Device alive until shutdown() succeeds; shutdown() returns false while GPU work is in flight. The wrapper can be manually used without the driver if the game owns its own frame ordering.

## Correctness limitations

Metal path uses the actual HDR scene depth, reconstructs a geometric normal, then outputs linear DIRECT incident light. The material pass records *actual* PBR baseColor and metallic for OPAQUE model surfaces and a separate depth attachment. Compositor multiplies by diffuse material / pi and rejects receivers whose depth differs from the actual scene depth. Custom terrain, sprites, transparent water and FX geometry have to provide their own material/depth buffer to receive Luma direct; no fake material values are inferred from illuminated scene color. High/Ultra experimental fullscreen GGX captures material base/metallic and surface world-normal/roughness (including valid normal maps); lower tiers use diffuse incident light, and custom terrain/foliage must provide matching receiver callbacks. PBR/normal/specular GPU parity and real device timing remain UNVERIFIED for the current branch. Disk/rectangle/sphere sources are finite point/spot approximations, not analytic soft-area BSDFs. Mobile continues to use old raster source map with explicit lost-source diagnostics. This is NOT an automatically enabled hardware RT/ReSTIR-equivalent Unreal MegaLights replacement.

The GPU producer fences via Metal readBufferAsync after compute and only publishes completed output on a verified GPU callback. No external callback or endFrame call may be mistaken for completion. GPU source/geometry changes while busy are deferred, not silently overwritten. Separate camera/geometry/light revisions invalidate temporal history. Strict work caps shrink low resolution / sample count rather than drop lights on Metal; outside/overflow clusters scan the complete source list. The old LightSystem can still render sun/sky/baked/legacy when Luma is disabled.

## ONE consolidated acceptance pass after code freeze

User explicitly requested implementing all features before running tests. These are commands for a later native Mac Studio run, not observed results:

```sh
git fetch origin
git switch feature/lumaweave-massive-lighting
git pull --ff-only origin feature/lumaweave-massive-lighting
git rev-parse HEAD
cmake -S . -B build/lumaweave-final -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_TOOLS=ON -DAIVERSE_BUILD_EDITOR=OFF -DAIVERSE_BUILD_CLIENT=OFF
cmake --build build/lumaweave-final -j8
ctest --test-dir build/lumaweave-final --output-on-failure
./build/lumaweave-final/aiverse_lumaweave_bench 50
./run-tests.sh --quick
```

Additionally validate all MSL shaders on an actual Metal GPU, render moving 100/500/1000 point/spot lights with PBR and scene-depth correctness (including material receiver and denoise), use Metal API Validation, record ACTUAL GPU timings and memory. The CLI benchmark reports CPU timing ONLY and prints GPU time NOT_MEASURED. Never claim it proves GPU FPS. Record tested git SHA, compiler, timing setup, tests/pass-fail logs and hardware in a new handoff before merging main.

## Experimental combined indirect GI + direct-light integration

Branch: `feature/gi-lumaweave-frame-integration-20261119`. The opt-in native frame driver can consume a host-owned `GiGpuCascadeRuntime` as a separate INDIRECT bounced term while it routes LumaWeave DIRECT (Metal) or legacy LightSystem raster (mobile/non-Metal) exclusively. A direct ray BVH is **optional** on the raster-only path (`sceneGeometry=nullptr`, `geometryRevision=0`); a nonzero revision requires an actual BVH.

`LumaNativeViewInput.indirectGi` is optional; the caller owns GI runtime geometry updates, device frame and actual GPU completion. `giProbeBudget` bounds the total cascade probe jobs, `synchronizeGiPointSources` explicitly publishes GI-eligible point emitters from the same light registry, rejecting unsupported spot bounce and >16 mobile GI sources rather than hiding energy loss. When bounce source publication is deferred by an outstanding GPU fence, the driver does **not** bind stale GI. A per-view RAII guard clears borrowed overlay textures before/after render. `giFrame` and `giPollFrame` keep new submissions and prior completion observations separate; `giBound` indicates whether GI was actually presented.

When changing the GI runtime pointer, the driver resets its accepted source revision. For reinitialization at the same address, explicitly call `driver.resetGiSourceSync()`. Do not share a single presentation driver across unrelated worlds (use independent `LumaWeaveWorlds` entries). This is NOT a finished product feature until it is rebased against main (the source branch diverged by hundreds of commits) and built/tested/visually accepted on native targets.

One-shot native acceptance can now be invoked from the integration branch with `bash tools/lumaweave_acceptance.sh --run`, from a clean macOS checkout with Xcode Metal. It runs Debug+Release build/CTest/assembled MSL shader compile and CPU stress profile; physical Metal GPU frame capture, temporal ghosting and measured GPU time still require a separate on-device visual run. This section documents source-level integration only, not executed tests.

## Vulkan and GLES3 / WebGL2 compatibility (experimental)

Historical note (Stage 9): the earlier integration branch did **not** expose Vulkan compute through `gfx::Device`. Experimental Stages 12–16 on `feature/gi-lumaweave-vulkan-hdr-stage16-20261120` now add an opt-in Vulkan compute RHI, GGX shader and postprocess path; **native compilation/GPU validation are still NOT RUN**. GLES3/WebGL2 still do not expose this compute ABI. Do not initialize `LumaWeaveScreenMetal` on these backends or label their raster lighting as 4096 shadow-traced direct lights. LumaWeave's explicit `MobileRasterFallback` selects up to the existing `LightSystem::kMaxLights` (1024) source map contributors, with deterministic importance and `LumaNativeFrameStats.mobile.dropped` / `sourceLoss` diagnostics. A light registry may contain up to 4096, but omitted sources remain **reported**, not invisibly shaded.

For optional portable dynamic indirect GI, instantiate `GiDynamicRuntime` (RGBA32F CPU-updated L1 probe atlas), publish an immutable scene BVH through `setTraceGeometry`, then pass `LumaNativeViewInput.indirectCpuGi=&runtime`. This runs the actual `GiDynamicProbeReference` ray tracer on CPU and samples directional L1 coefficients through the standard `DYNAMIC_GI` GLSL model shader on Vulkan/GL/GLES3; it is not the Metal toroidal GPU cascade path. CPU work is deliberately bounded per frame by `LumaWeaveBackendPolicy` (GLES3 2 probes, Vulkan 16, GL33/Metal CPU 8), with actual cap exposed in `LumaNativeViewResult.cpuGiProbeBudgetAdmitted` and any reduction in `cpuGiBudgetClamped`. L1 static source/geometry coverage and HDR Medium+ material eligibility are still host-controlled.

A host targeting Metal and another platform may supply both `indirectGi` (Metal GPU cascades) and `indirectCpuGi` (portable CPU atlas) to the same driver. Ready Metal cascades win on Metal; other backends use ready CPU probes when supported. Both remain separately owned and require proper geometry and source revision updates. The GI source sync flag publishes point sources with up to 16 GPU vs 64 CPU bounce sources, refusing unsupported spot-to-omni substitutions. Invalidated CPU irradiance is cleared and uploaded before further tracing, even when the new probe work budget is zero, preventing old-bounce ghosting.

Available one-shot acceptance entrypoints **after source freeze**, not executed yet:
- macOS + full Xcode Metal: `bash tools/lumaweave_acceptance.sh --run` (Debug+Release CTest, assembled Metal shader compilation, CPU stress)
- Linux with Vulkan ICD (lavapipe accepted), EGL GLES3 and Xvfb: `bash tools/lumaweave_portable_acceptance.sh --run` (Debug+Release CTest, Vulkan GI GLSL-to-SPIR-V, GL/GLES3 and Vulkan screenshots)

Neither general scene script auto-activates LumaWeave's native driver; dedicated native GI+direct visual captures, GLES3 actual shader-driver acceptance, Android/WebGL2 targets and real GPU frame times are still required. Avoid claiming supported Vulkan *compute* / hardware ray tracing or complete platform parity without implementing and testing those backends.

## Experimental Vulkan screen Direct + HDR composition (Stages 11–16, 2026-11-20)

Active isolated integration path: `feature/gi-lumaweave-vulkan-hdr-stage16-20261120`.
Detailed per-stage handoffs under `docs/agent-handoffs/gi-lumaweave-stage11-*` through `gi-lumaweave-stage16-*`.

Vulkan now has **source-level** optional compute RHI supporting four immutable/read-only SSBOs, one RGBA16F sampled+storage output, 80-byte push constants, and five independently sampled textures. The original source/geometry BVH owner uses strict Stage62 one-ULP bounds preflight, transactional source publication and actual completed-frame serial checks. A Vulkan screen kernel reconstructs world positions from real postprocess depth, uses current-view model/custom material RGB+metallic and world normal+roughness plus two independent receiver depth buffers, importance-samples all declared light sources with the SAME BVH shadow query, and outputs already-PBR-reflected GGX+diffuse DIRECT light. Its GLSL shader and HDR upsampler are distinct from the old GLES CPU probe path. It does NOT render indirect/baked radiance.

`LumaNativeViewInput.enableVulkanDirect` is OFF by default and requires `vulkanReceiverCoverageConfirmed=true` from the host. That flag means the entire intended opaque visible surface set (not just ordinary ModelDraw) has current-view material, normal and depth receiver callbacks. An immutable BVH and nonzero generation must be provided and the down-stream PostProcess must be active/linear HDR; a pending Vulkan GPU fence, incompatible device, rejected workload or publication failure selects the existing raster direct fallback. Do not enable the route for custom terrain, water, sprites or FX unless corresponding receiver passes are authored and coverage is verified.

Native usage (after the owner/editor has installed GPU backend and completed its presentation source barrier):
```cpp
LumaNativeViewInput view;
view.camera = camera;
view.width = width;
view.height = height;
view.sceneGeometry = &immutableBvh;
view.geometryRevision = sceneRevision;  // must be monotonic, nonzero
view.authored = authoredDirectLights;
view.enableVulkanDirect = true;
view.vulkanReceiverCoverageConfirmed = allOpaqueReceiversMatched;
view.quality = LumaWeaveQuality::Balanced;
auto result = driver.frame(view, outputPass, [&](Renderer3D& renderer) {
    // drawModel, custom opaque terrain, AND matching material/surface callbacks.
});
```
The driver still does NOT own `Device::beginFrame/endFrame`. It records `LumaDirectRoute::VulkanGpu` only after publishing correct source/BVH packets. The Vulkan PostProcess requests material/surface capture passes, runs compute after the main HDR pass, makes the written image shader-readable, then performs depth-matched *additive* HDR composition before downstream tonemap. Its output is material-reflected, so the compositor must NOT multiply base color again. On post-resolution refusal it reports `vulkanFrameComposited=false` rather than silently declaring success.

**Verification:** the Vulkan `vulkan_backend` test source now contains point/spot/BVH shadow, sampled input, and screen-material GGX pixel checks; the CPU FakeDevice CTest sources cover publication, quality budgets, fence ownership, receiver pass/compose lifecycle and runtime API fields. These are newly AUTHORED TESTS, **NOT EXECUTED** on this source branch: no native CMake, real GLSL/SPIR-V compiler results, Vulkan validation layers, Metal/GLES device parity, GPU FPS or full AIVERSE suite. Do not merge to main, claim hardware shadow parity, or enable by default until the user's requested final consolidated acceptance is run.

## Stage 17 Vulkan temporal direct history (experimental)

Branch: `feature/gi-lumaweave-vulkan-denoise-stage17-20261120`, handoff:
`docs/agent-handoffs/gi-lumaweave-stage17-vulkan-temporal-spatial-2026-11-20.md`.

When Vulkan Direct is explicitly selected, a second compute pass can perform
3×3 depth/normal-aware bilateral spatial filtering of current PBR GGX DIRECT
radiance. A matching prior filtered RGBA16F image may be reprojected and
history-clamped only after the real Vulkan completed frame fence and only
for the same immutable source/BVH revisions and stable camera+world identity.
The history is two low-resolution filtered textures; their alpha channel
stores the corresponding frame's scene depth, never baked light or GI.

Native view configuration:

```cpp
view.enableVulkanDirect = true;
view.vulkanReceiverCoverageConfirmed = allOpaqueReceiversMatched;
view.vulkanHistoryKey = uniqueStableCameraAndWorldId; // 0 = spatial only
view.vulkanHistoryEpoch = cutOrTeleportEpoch;           // increment on hard cut
view.vulkanDenoise.enabled = true;
view.vulkanDenoise.historyWeight = 0.84f;
```

The view result reports `vulkanDenoiseEncoded`,
`vulkanTemporalHistoryUsed`, and `vulkanHistoryReason`. Changing
camera/world identity, cut epoch, source/BVH generation or render resolution
invalidates temporal accumulation. Each Vulkan GPU completion is associated
with a captured history generation; a late completion cannot restore
invalidated history. If the current GPU output cannot be encoded, the existing
raster direct route or current raw direct is used; **a stale temporal image is
never substituted**. Two Vulkan frames in flight can temporarily select
raster while the Direct owner is busy; history may be kept across only this
strictly matching identity/generation case and is still inaccessible until
the real GPU fence.

This is **not** universal TAA/motion-vector reconstruction: animated/skinned
objects require consistent geometry revisions and independent temporal
ghosting acceptance. No native CMake/CTest, SPIR-V shader execution, real
Vulkan pixel checks, GLES3/WebGL2/Android or Metal parity, GPU time or FPS
is verified on the Stage17 source. The source, tests and handoff exist but
are **NOT RUN**; do not merge or enable by default before the consolidated
native multi-backend acceptance requested by the owner.

## Stage 18 — Vulkan multi-frame Direct + HDR receiver lifetime (2026-11-20)

Experimental branch: `feature/gi-lumaweave-vulkan-multiframe-stage18-20261120`.
Detailed continuation for AI agents:
`docs/agent-handoffs/gi-lumaweave-stage18-vulkan-multiframe-2026-11-20.md`.

Previously the Vulkan Direct owner allowed only one uncompleted screen
submission, forcing every second frame back to the raster path on a busy GPU.
Stage18's source now supports up to **three outstanding screen frames**
with four independent low-resolution raw+filtered image slots. One slot can
hold the last physically completed denoising history while other GPU
submissions are pending. Each slot tracks its own submitting frame serial,
output texture, historical stamp and the LAST newer frame serial that still
samples it as previous history. A historical texture is not made writable
merely because a newer frame became the current history. Retirement and
new-history promotion use actual `completedFrameSerial()`, ordered by
serial then within-frame submission order; neither `dispatch` nor
`endFrame` is treated as a completion fence.

Vulkan `LumaWeavePostProcess` also maintains **four independently retired
full-resolution receiver sets** for material/metallic, normal/roughness and
their two depth images, plus a separate HDR composite depth attachment.
These are not replaced or reused until their owning GPU frame has signaled
its real fence. The maximum potential receiver-ring footprint is limited
to **512 MiB** (two RGBA16F and three depth images per slot, four slots);
views exceeding this budget choose the existing raster direct fallback
before any legacy point lights are suppressed. This is a conservative
memory safety admission, not a measured GPU memory footprint.

`LumaWeaveNativeDriver` may choose Vulkan Direct even if 1–2 older frames
are still in flight, as long as the full source registry and strict BVH
revision are unchanged, the GPU has a free output slot, the material
receiver ring can admit the view, and the original opt-in/opaque coverage,
HDR, and quality requirements hold. Frame slots full, new source geometry
while busy, memory cap refusal, or a missing resource retain the safe
raster path without silently reducing registered lights. The
`vulkanHistoryKey` / epoch and temporal disocclusion rules from Stage17
remain mandatory for any historical reprojection.

Testing is **SOURCE ONLY, NOT RUN** at this stage. The new platform-neutral
`lumaweave_vulkan_multiframe` CTest exercises 3 pending submissions,
fourth-frame refusal, history-read retirement, same-revision no-op,
source/BVH update rejection, four independent HDR receiver sets, controlled
reuse after a fake completed serial, and shutdown waiting for all fences.
The existing `vulkan_backend` test now contains an additional device-
executable three-frame concurrent compute/shadow/denoise submission
fixture. Neither is evidence of real GPU correctness until built and
executed on Vulkan (lavapipe plus validation layers/physical hardware);
Metal/GLES/WebGL2 and the full Debug/Release suite remain unverified.

Limitations: the renderer-level Vulkan device currently has a
two-frame-in-flight swapchain/command ring, so physical overlap may be
only two active GPU frames even though the Direct owner allows three.
Temporal history still lacks previous-frame motion vectors and normals
for fully arbitrary skeletal motion. The full-resolution receiver
memory footprint is significant; real VRAM and GPU performance must be
measured during consolidated platform acceptance. No automatic enablement
or merge into `main` is authorized by this experimental stage.

## Stage 19 — object motion vectors + previous normals for Vulkan temporal GI/direct separation

Isolated source branch: `feature/gi-lumaweave-vulkan-motion-stage19-20261120`. Read
`docs/agent-handoffs/gi-lumaweave-stage19-vulkan-motion-normal-history-2026-11-20.md`.
This is LumaWeave DIRECT temporal filtering; bounced GI and baked/sun
lighting retain separate ownership.

**What changed in source:** the Vulkan denoiser now supports seven sampled
textures, including a real object-motion field and a separate previous-frame
signed world-normal + depth capture. Motion uses the same `MotionTextureInput`
metadata contract and per-pixel encoding as the MAIN branch's PRISMA
Renderer3D object motion pipeline: RG = normalized CURRENT-minus-PREVIOUS UV
displacement, B > 0.5 only where object motion is valid. Previous
filtered direct radiance/depth and previous normal/depth MUST come from
the SAME actually completed GPU history slot. The denoiser uses
`oldUv = currentUv - motion.RG` for valid moving objects, validates
old normal and depth against the current receiver and rejects mismatches.
For static camera-only motion it retains the Stage17 camera reprojection.

For a native host that has already rendered a real per-view RGBA16F motion
texture (for example, via the MAIN PRISMA renderer after conflict-aware
integration), opt in:

```cpp
view.enableVulkanDirect = true;
view.vulkanReceiverCoverageConfirmed = true;
view.vulkanHistoryKey = cameraAndWorldKey;  // nonzero unique stable identity
view.vulkanHistoryEpoch = cameraCutEpoch;   // increase on cuts/teleports
view.vulkanDynamicGeometryVisible = visibleSkinnedOrMovingMeshes;
view.vulkanObjectMotion = MotionTextureInput::declared(
    currentFrameMotion, view.width, view.height,
    gfx::PixelFormat::RGBA16F);
view.vulkanMotionFrameSerial = device.frameSerial();
view.vulkanMotionViewKey = view.vulkanHistoryKey;
view.vulkanMotionViewEpoch = view.vulkanHistoryEpoch;
```

The borrowed image must be produced for the EXACT current GPU frame and
viewport and kept alive until that frame's completed Vulkan fence. The
post-process does not create or own arbitrary motion vectors. The isolated
Stage19 feature branch **does not yet include the MAIN renderer's automatic
rigid/skinned motion producer**; it exposes an integration contract for
genuine images from an existing native producer. It never invents motion
from a character transform or camera matrix. Set
`vulkanDynamicGeometryVisible=true` for visible animated surfaces: a
pixel lacking B-valid object motion then uses CURRENT spatial lighting
ONLY, not potentially ghosted camera-only temporal history.

`LumaNativeViewResult.vulkanMotionInputBound` and
`vulkanNormalHistoryCaptured` report whether per-view motion was
accepted and whether that current filtered frame captured its GPU normal
history. When a Vulkan GPU offers fewer than seven compute sampled-image
descriptors, the denoise kernel may be unavailable, but ordinary raw
Vulkan Direct and GLES raster + CPU GI fallback are unaffected.

Source tests were authored for motion frame/view/epoch/format mismatch,
the GLSL→SPIR-V seven-sampler denoiser + normal snapshot sources,
FakeDevice motion-lifetime and multi-frame normal history, and an actual
Vulkan GPU executable for valid/invalid motion, opposing previous
normal, mismatched old depth and normal-history pixels.

**Verification: NOT RUN.** No native compilation, CTest, Vulkan/SPIR-V
hardware, GLES, Metal, WebGL2 or FPS/memory result has been observed
for this Stage19 branch. The next step is a conflict-aware integration
of the already available MAIN PRISMA rigid/skinned/deformed motion producer
into the LumaWeave native frame route, then a real moving character
ghosting capture and the user's consolidated Debug/Release multi-backend
acceptance after feature freeze. Do not merge or enable by default before
hardware/renderer validation.
