# AIVERSE GI + LumaWeave — Stage 13: Vulkan direct GPU publication owner (2026-11-20)

## Branch/source lineage and exact commits

- Repo: `chromealex/AIVERSE`.
- Current feature: `feature/gi-lumaweave-vulkan-direct-owner-stage13-20261120`, created from `feature/gi-lumaweave-vulkan-compute-rhi-stage12-20261120` at `b8f84dc2c64c7a48bf381f9620785c5bdc7478e3`. This branch includes Stage11 shader/SPIR-V and Stage12 Vulkan compute RHI changes; it does **not** mutate `main`.
- Source commits:
  - `13bc144779aecd0c52f3848bbc69aaf49c7d5285` — selective integration of `engine/render/GiGpuBvhValidator.h`, exact blob SHA `6ed861e98063e45d281b751459c85cdce569ef53` from separate Stage62 GI precision branch. Enforces root-reachable BVH, stack/triangle/child bounds, one-float32-ULP containment tolerance, finite albedo and no corrupt GPU traversal inputs.
  - `b9c90d741286ee78fc4e547b36076387e710e312` — new `engine/render/LumaWeaveVulkanDirect.h`: explicit Vulkan compute owner for independent DIRECT radiance, source/BVH publication and limited receiver dispatch, RHI resource lifetime, actual frame-serial fence retirement.
  - `fd153be068e5b8e6662518e302b4b1d5da802e66`, `6137c099136663d34b42f9935c6453087c3426de` — prevent two different source registries or BVH packets with equal revision from aliasing an old world, via verified `lumaSameLight` snapshot and immutable packed-BVH content fingerprint. Includes explicit `<utility>`.
  - `7390f38e52507c2de5f65b58daee73cf60dac3f3` and `1fdcee3dc727e502986753166df4d733001bbe8e` — `tests/render/lumaweave_vulkan_direct_owner.cpp`: FakeDevice contract for frameSerial vs completion, no premature resource retirement, source/geometry validation, duplicate revision alias prevention, 4096-light overload rejection, invalid receivers, GPU backend gating and full shutdown resource cleanup.
  - `fa9982b6c4d979fd14f7380e57aa291c662b1ce8` — `CMakeLists.txt` registers platform-independent `aiverse_lumaweave_vulkan_direct_owner_test`, CTest name `lumaweave_vulkan_direct_owner`.

## What has actually been implemented

`LumaWeaveVulkanDirect` is a **new opt-in native C++ presenter API** rather than an automatically enabled renderer route:
- `initialize(gfx::Device&)` requires Vulkan, actual `supportsCompute()` and RGBA16F sampled/storage capabilities, and an initially healthy device. Creates the Stage11 GLSL compute kernel, persistent independent RGBA16F output texture, 4 Vulkan storage buffers: light packets, dynamic receiver data, shared GI BVH triangles and nodes.
- `setSources(const LumaWeaveRegistry&)` validates/serializes the **entire** sorted stable-key source list into existing 80-byte `LumaGpuLight` structs. Source revision is monotonic; equal revision is a no-op only when the complete immutable light snapshot matches, preventing cross-world aliasing. Uploads are transactional; no old GPU handle is discarded until replacements all exist.
- `setGeometry(const GiGpuBvh&,uint64_t)` requires nonzero monotonic scene revision, strict Stage62 GPU BVH preflight, transactional triangle/node buffer replacement and equal-revision content fingerprint check. No 8192-triangle truncation, no malformed shader input, and no dynamic-world stale geometry silently presented.
- `dispatch(span<LumaGpuReceiver>,samples,seed)` requires 1..256 explicit receivers, 1..8 shadow-ray samples, valid finite world positions/normals, no pending GPU frame, safe buffer-update point, nonempty initialized scene/source packet and `receivers*(samples+1)*ALL_registered_sources<=524288`. Rejects excessive work **without removing sources or pretending energy is preserved**. Publishes 80-byte uniform contract, four buffers and storage texture on the Vulkan compute RHI; it marks pending only when Vulkan RHI reports an actual accepted dispatch.
- `encodedOutput(exactFrameSerial)` is permitted ONLY for a same-command-buffer consumer after `endCompute` has restored the shader-read layout, not for cross-frame access. `poll()` advances from pending only after an actual `completedFrameSerial()>=pendingFrameSerial` **and** healthy device; `completedOutput()` is invalid otherwise. `shutdown()` refuses to destroy in-flight storage.
- The output is incident **DIRECT** diffuse light (linear, cosine-weighted), not a final material-shaded GGX fragment, nor bounced GI, nor a lightmap. There are no changed ECS components/saves/network IDs, Metal/GLES paths or default rendering features.

## Test status — evidence vs authored sources

- **Performed:** source reads/updates through connected GitHub; independent CMake owner target verified in source; new source commits acknowledged; existing Stage11 compiled-source target and Stage12 Vulkan device target preserved.
- **NOT RUN:** native C++ compilation of header/tests, CMake/CTest, glslang GLSL-to-SPIR-V, actual Vulkan compute dispatch or pixel readback, Vulkan validation layers, desktop/mobile GPU ms, Metal MSL and GLES3/WebGL2 visual/regression parity, full engine test suite.
- FakeDevice does NOT simulate actual GPU shading or GPU fences; it only simulates delivery of a monotonic completion serial and checks the host's rejection/retirement policy. The Stage12 `vulkan_backend` source contains a real-device red-light versus wall-shadow pixel test, but has likewise **NOT RUN**.
- Do not claim 4096 fully shadowed lights at 60FPS: GPU work cap rejects high source+receiver combinations; the native host must choose meaningful screen coverage and quality policy instead of silently dropping energy.

## Critical remaining feature work

1. Production native scene integration: provide current-view opaque receivers from actual depth+normal/material capture, not fabricated CPU world points; attach the Vulkan owner to explicit `LumaWeaveNativeDriver` route selection with **exclusive** legacy direct light disabling on successful GPU route only. Avoid adding two direct sources in one view, preserve baked indirect and separately owned moving GI.
2. HDR composition: Vulkan scene-depth matched additive direct pass with verified material base/metallic/roughness and GGX specular semantics. A result that is already material-shaded must NOT get Lambert multiplied twice. Temporal/spatial denoising, dynamic resolution and camera-history invalidation need true GPU source/geometry/camera revision ownership.
3. Real GPU verification: configure/build native Vulkan, run `ctest -R '^(lumaweave_vulkan_direct_shader|lumaweave_vulkan_direct_owner|vulkan_backend)$'` (plus Stage62 BVH validation), with Vulkan validation and actual red/black pixels. Investigate any real GCC/Clang failures from logs, don't suppress errors. Include hardware and CPU/GPU timings for 100/500/1000 lights, overflow rejection and no flicker.
4. GLES3/WebGL2 source and GPU capture validation; continue bounded existing raster LightSystem direct + CPU L1 GI, do not imply GLES compute is supported. Conflict-aware reconciliation of newest main/parallel GI/lighting/shader branches after feature freeze.
5. After every subsequent code stage, create and commit separate handoff with exact code SHAs, public APIs, actual test status, risks, next actionable step. User requested one final consolidated native test pass after functional completeness. Keep all PRs DRAFT until evidence exists.
