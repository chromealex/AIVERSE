# Dynamic indirect GI

The lighting branch provides an explicit, genre-independent, one-bounce diffuse
probe overlay. It composes with existing direct lighting. It does not implicitly
enable itself, replace direct-light ownership, or write simulation/ECS state.

## Native integration

Use `engine/render/GiMainCpuBridge.h` for budgeted CPU tracing and
`engine/render/GiMainGpuBridge.h` for bounded Metal/Vulkan compute. Both consume
immutable `GiChunkedGeometrySnapshot`, stable geometry revisions, linear RGB
`GiMovingLight` sources and optional indirect-only `GiEnvironmentLight`.
`apps/renderbench/DynamicGiHost.h` is a working native host example.

1. Before CPU mesh arrays are released, build immutable opaque caster chunks with
   material diffuse albedo. The benchmark retains CPU geometry for opted-in
   animated assets and republishes posed opaque caster chunks automatically.
2. Initialize a fixed probe lattice. GPU supports up to 8192 triangles, 16 lights,
   32 probes per dispatch and 8–64 rays per probe. CPU supports 8–128 rays and uses
   an adaptive probe budget. Lattices are capped at 20000 probes. A CPU budget is
   a scheduling target, not a guaranteed frame-time bound for an individual ray.
3. On the render owner thread, publish geometry/light/environment snapshots and
   update before graphics passes. GPU publication requires an active Device frame.
   Retry deferred publications after prior compute **and graphics reader** fences
   complete. Rejected source data must not be presented as current data.
4. Call `renderer.beginScene(...)`, then `renderer.setDynamicGiOverlay(bridge)`
   for every view. `beginScene` clears previous attachment. The bridge must outlive
   all submitted draws. Textured Shader Graph receivers reserve tex13 for GI and
   move graph samplers to tex14+. GPU-instance/ICB uses ordinary draws while GI
   is attached. PBR receivers support High/Low and multiply GI by material AO.
5. Terrain/custom pipelines bind after selecting their shader: built-in full
   terrain uses U6/tex21; compact tier0 terrain uses tex8 only without baked,
   detail or trace features. Generic custom/model samplers use tex13.
6. Detach receivers, establish real GPU idle, then call `shutdownAfterGpuIdle()`
   before destroying the Device. Never substitute CPU frame age for completion.

GPU scheduler completion is fence-backed. `computeDispatchSucceeded()` means
commands were encoded, not that the GPU completed them. Partial checked uploads
keep the result hidden and force coherent geometry recovery. Source rejection
states are independent; updating the environment cannot restore rejected lights.

## Renderbench recipe

Add an opt-in `dynamicGI` object to a native renderbench scene:

```json
"dynamicGI": {
  "backend": "auto",
  "origin": [-12, 0, -6],
  "dimensions": [8, 4, 4],
  "spacing": 3,
  "samples": 32,
  "distance": 20,
  "cascades": 1,
  "cpuBudgetMs": 2,
  "sunDirection": [0.35, 1, 0.55],
  "sunColor": [1, 0.96, 0.9],
  "sunIntensity": 3,
  "skyIntensity": 0
}
```

`auto` uses compute only when device and scene limits allow it, otherwise retaining
all captured geometry on CPU. `gpu` fails explicitly if unsupported; `cpu` forces
the portable path. Sources are not silently truncated. Recipe point lights are
converted from the renderer's stored sRGB representation back to linear transport.
Environment colors above are already linear and are indirect-only; no implicit
copy of direct sun/sky settings occurs. Geometry is split into spatial chunks of
about one `distance`; the GPU path keeps resident only chunks a GI ray can reach
(`GiGpuResidencyPlanner`), so a world above the 8192-triangle GPU budget stays on
the GPU while the reachable part fits. `cascades` (1..3, GPU only) adds nested
lattices around the same center, cascade k with `spacing`·2^k and `distance`·2^k;
all share the one GI atlas (texture 13, U6 nine float4), a point uses the finest
cascade that contains it and fades into the next one at its edge. The benchmark waits for initial convergence before timed frames and reports
`dynamicGiBackend`, `dynamicGiFallback`, `dynamicGiTriangles` and
`dynamicGiConvergenceMs`. It reattaches GI on every camera view.

The benchmark rejects simultaneous `probeGI`/`staticPointBounce` because those
recipes reuse the same sources. Its screen-space diffuse bounce is disabled while
dynamicGI is active. Direct-light rendering and post-processing remain independent.
Animated assets share one sampled pose between renderer, indirect GI and Luma
shadows. The verification host holds that pose until its GI converges; it does
not claim real-time animated transport latency. Submitted-frame timings exclude
convergence-only updates, as recorded in `lightingCadence`. Editor integration
is a separate stage.

## Verification and limits

All builds and compiler TMPDIR for this work live on KINGSTON, under
`/Volumes/KINGSTON/AIVERSE/Builds/.temp/lighting-completion-20261120`.
Run registered checks with `./run-tests.sh --no-build --build-dir <root>/build
--filter '^lighting_'` after building the selected targets. Enable
`AIVERSE_BUILD_HDR_TESTS`, `AIVERSE_BUILD_RENDERBENCH`, `AIVERSE_BUILD_TESTS`;
`AIVERSE_VULKAN` additionally builds real SPIR-V compilation tests.

Actual Metal/desktop GL pixel tests cover model and terrain receivers, per-view
reset, light removal/restoration and CPU/compute parity. Native host acceptance
loads real glTF, verifies JSON completion and compares captured RGB, so native
window exit code zero cannot hide a fixture failure. Vulkan backend and GI SPIR-V
compile locally; Vulkan GPU execution is not verified on this Mac (no loader).
Compact shader testing on desktop GL is not a GLES device performance claim.

This is bounded first-bounce L1 interpolation, not multi-bounce path tracing,
reflection GI, infinite-world streaming, probe relocation or a proof against
all thin-wall light leaks. Performance and mobile/Vulkan hardware acceptance are
separate gates; no production quality defaults were changed.

## Posed geometry snapshots

`giModelGeometry(model, objectTransform, &pose)` in `GiModelGeometry.h` captures
opaque casters with rigid node animation, morph positions and normalized byte
skin weights. It reuses `worldMatrices` / `paletteMatrices`; skin joint transforms
are model-space, so the mesh node transform is not applied twice. Morph precedes
skinning. Retain CPU vertices, indices, morph deltas and image samples through
`uploadModel(..., true)` for models whose snapshots will be rebuilt after upload.
Missing pose/morph data rejects instead of silently freezing the bind pose.

Publish the result as a replacement chunk in `GiChunkedGeometryBuilder`; old
snapshots retain their previous geometry for in-flight tracing. This adapter
provides the snapshot, not scheduling or automatic host tracking. Alpha masks,
transparent surfaces and Shader Graph vertex displacement are not represented by
this opaque triangle approximation. Texture albedo uses bounded average sampling.


The native `AnimatedLightingGeometry` host shares unchanged static chunks and
rebuilds only animated caster chunks. It publishes monotonic revisions to both
CPU/compute GI and Luma. Reports expose animatedLightingCasters,
lightingGeometryRevision and lightingSnapshotTriangles. Geometry capacity errors
reject the view; actors are never silently omitted to fit the GPU budget.
`bake:false` preserves the asset's explicit caster exclusion. This fixed-workload
host holds the selected pose while compute/convergence is pending, then renders
that same pose; a game host must choose and measure its own update cadence.

## Native Studio authoring
The optional map `renderLighting` v1 block is validated by RenderLightingSettings.
Studio Lighting controls support native save/undo/redo. RenderLightingRuntime uses
the shared DynamicGiHost and LumaHost owners; the viewport renders its existing
terrain/document proxies as PBR geometry. This is not full game-asset preview
parity. The panel reports actual backend, convergence and fallback.

## Experimental Metal ICB
Dynamic GI texture13 is part of the Metal ICB fragment argument buffer; U6 is
snapshotted with the other per-submission uniforms. Both CPU and GPU GI providers
pass analytic pixel tests with actual GPU-encoded command submissions. This does
not enable experimental ICB by default or imply whole-engine ICB performance.

## Reproducible Release timing
`tools/renderbench/lighting_release_acceptance.py --binary <arm64-renderbench>
--scene <GI-scene.json> --output <KINGSTON-artifact-directory>` records an
OFF/ON/OFF sequence with 30 warmup and 120 measured frames, fixed camera/pose.
CPU encode, CPU frame and GPU execution are reported separately from loading and
GI convergence. It rejects non-Release/non-arm64/incomplete reports; window
pacing must not be presented as FPS. Use --feature luma for direct lighting.

## Main integration resource coexistence (2026-11-20)

Models without virtual materials retain GI texture13/U6. When the material
uses virtual sampling, its existing atlas/page-table and U6 remain unchanged;
GI uses U7 and the first texture after the table (after alpha coverage when
present). Shader Graph sources remain at 13+ for virtual materials and move to
14+ for nonvirtual GI materials. Resource overflow rejects the pipeline with
an explicit diagnostic. Virtual-material + GI and graph + GI use ordinary CPU
submission; experimental ICB retains its previously verified plain-PBR GI ABI.
These fallbacks preserve the same GPU shading, not a second lighting solver.

Vulkan GI uses the shared TextureResources compute ABI: buffers 12..15,
storage image 0 and std140 uniform block 16, through bindComputeStorageTexture
and dispatchChecked. It no longer defines a competing Vulkan compute backend.
Terrain GLSL emits literal sampler names for the Vulkan binding translator.
Prepared LENS terrain frames stage GI pipeline variants before publication;
callbacks retain immutable texture bindings instead of capturing mutable owners.
