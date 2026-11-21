# VIVID handoff — Stage 7AQ: sampled GPU mask alpha, LOD-safe confirmation, native GLES3 async pixel RHI

Date: 2026-11-20.
Branch: `feature/vivid-digital-humans`.
Integration constraint: `main` and `feature/texture-rhi-unification` have NOT been changed or merged.
**Evidence classification: source changes committed, native compiler / CTest / GPU NOT RUN.**

## Intent / architecture

Stage 7AP's GPU selection depth pass conservatively treated every alpha-masked polygon as a solid zero-ID blocker. This sometimes prevented GPU confirmation behind genuinely transparent holes. Stage 7AQ adds **material-limited, hardware per-fragment alpha masking** without taking ownership of textures or streaming from Texture RHI. GLES3 now has an independently useful async offscreen pixel readback method, but native GLES VIVID GPU picking is NOT yet activated: its device still lacks the safe frame publication serial used by the async VIVID token contract.

The default CPU morph-before-skin BVH picker remains operational on Metal, GLES3, GL33, Vulkan and WebGL2. Stage 7AQ is opt-in GPU confirmation only; no gameplay ECS, MotionWeave tick/scheduler or character DNA/saves modified.

## Implemented source / commit references

1. `engine/character/vivid/SculptGpuAlphaMask.h`:
   - Added in `b80554591112752067f9d1fc6056641477c953f2`, materialless fixture fix `bbc51ff1b51782e827d20550df74e707e7630a37`.
   - Standard mask eligibility: `alpha==Mask`, real `model.images[baseTex].tex`, no ShaderGraph or team override, finite UV/alpha, no vertex AO/back-occlusion semantics. Missing texture, custom shader alpha and unsupported inputs remain conservative opaque zero-ID blockers.
   - `SculptGpuMaskVertex`: 32 bytes = world float3 at 0, UNORM UByte4 triangle ID at 12, UV+per-vertex alpha float4 at 16. `SculptGpuMaskUniform`: 48-byte std140/MSL three vec4 slots carrying base alpha, cutoff, UV offset/scale and UV rotation.
   - `kSculptGpuMaskVS/FS/MSL` samples the original GPU alpha texture and discards pixels below `baseColor.w * tex.a * vertexAlpha < alphaCutoff`, with the exact GLSL/MSL sign convention for UV rotation from `ModelShaders.h`. The transparent sample writes neither depth nor ID; a solid sample writes ID for the selected mesh or ID=0 depth for other occluders.
   - `appendSculptGpuMaterialGeometry` prepares normal opaque/zero-ID geometry plus per-material masked batches from the SAME skinned source world positions. Local staging, total 1M-triangle bound and 256-mask-batch bound. It retains the original selected mesh-local triangle ordinal used by `resolveSculptGpuTriangle` for ID reconciliation.
2. `engine/character/vivid/SculptGpuIdPass.h`:
   - `2860ec88150d51d049ec51712b0cc8a2e358dbd9`: own a second standard gfx mask shader/pipeline and readback GPU vertex buffers until GPU completion.
   - `3b60d8485e684dbeff2759eaa3f922c4d77f022a`: partition selected model into opaque and sampled masked batches, append external occluder masks, validate total budget and zero-ID restrictions.
   - `9386be9c0a8ae07d6104da1f43a3b7b0a273b0c3`: draw opaque base plus all texture-sampled alpha batches through the same linear RGBA8/depth offscreen pass. Mask batches bind `gfx::TextureHandle` and pass their own UV/alpha uniforms, with depth test/write on, blending off, alpha discard in shaders. Pending GPU buffers remain valid until asynchronous pixel completion; failed readback retains them until verified GPU-idle shutdown.
   - `5f1a1fd8a552920452a561e566872b6d2a827dec`: reject external masked occluders with any nonfinite position, UV or material uniform.
3. `tools/vivid-creator/main.cpp`:
   - `d490c3efbf9a7784c634b7260c5e4b35af024d50`: create masked occluder batches from every other visible imported mesh in the same face/morph/skinned pose, and pass them to the existing Metal ID pass. Floor and procedural hair remain zero-ID polygon occluders when GPU alpha texture semantics are unavailable.
   - `cd03e6d80a53cddcf6af98ac55592b13f8ad5abe`: opt-in GPU ID requires `sculptGpuPickEligibility` pass; if authored LOD chains or ambiguous selected mesh ownership exist, do not claim the base-mesh ID pass matches visible Renderer3D LOD and retain CPU BVH.
4. `engine/character/vivid/SculptGpuPickEligibility.h`:
   - `68143569b45e6f7b74cc17cc8b2b0325e92876d9`; immutable, genre-neutral compatibility probe rejecting wrong mesh index, unreferenced/multiowner mesh, invalid node mesh and authored LOD in any node-attached mesh. No silent fallback to guessing the selected GPU LOD.
5. `engine/gfx/gl/GfxGL.cpp`: 
   - `f08eafcdf393251363de1d86122bb2a31a92d932`: Native GLES3/OpenGL3 pixel readback by 4-byte `GL_PIXEL_PACK_BUFFER`, one `glReadPixels` into the PBO, GPU `glFenceSync` and later `glClientWaitSync(...,timeout=0)`; mapping and four-byte owner-thread callback only after GPU completion. The request uses top-left pixels converted to native GL bottom-left. Emscripten/WebGL2 and WGL's custom GL loader return unsupported; Vulkan also remains unsupported.
   - `f79848fd4abf75bf91416dc52d0bd4d5184792e0`: preserve read **and draw** framebuffer bindings, PBO binding, pack alignment; reject GL read errors instead of returning a fake ID.
   - `1bb90e9aca7ef232741d37db12812c3786919b26`: reject read requests while a GL render pass is active. Pending PBO/fence resources are drained with owner-context teardown. The callback queue is bounded to 32.
   - **GPU picking is NOT enabled on GLES**: GLDevice has no safe nonzero `Device::frameSerial()`, which Stage 7AN/7AO requires for pose- and publication-stamped GPU ID requests; do not invent one without an explicit completion contract. GLES PBO readback is a reusable RHI capability awaiting native testing.
6. Tests:
   - `tests/vivid/human_creator.cpp` `3c085f75a505aa08242c90283fef7b059687c674`: material alpha/UV/texture binding, 32B vertex/48B uniform, two shader descriptors, missing-image and custom ShaderGraph fail-closed geometry.
   - `95b1aafeed59b12d9c981f20ca4a3ea118b57dcb`: authored LOD and mesh owner ambiguity rejection.
   - `tests/vivid/metal_sculpt_id.mm` `8cbb2f12737d2514f80bac3c84d6aaf478438fa4`: GPU fixture with single-source 1×1 alpha=0 and alpha=255 texels, expecting GPU ID 0 and nearer triangle ID 2, in addition to Stage 7AP occluder/depth tests. Registered under existing `vivid_metal_sculpt_id` CTest, but NOT executed.
7. Documentation:
   - `tools/vivid-creator/README.md` `19b3363dc2d405b0564a4dfc06aad623c4096522`; `docs/vivid.md` `244d725f3547cc29571cd024572fab5a433a2c09`.

## Source API to continue

- `appendSculptGpuMaterialGeometry(model, skinnedSurface, sourceVertexCount, originalSourceIds, isSelectedMesh, opaque, maskBatches)` for opaque and eligible GPU mask faces. `isSelectedMesh=false` forces ID 0.
- `sculptGpuMaskEligible(model,material)` is the explicit *limited* mask shader capability; it avoids relying on stale CPU-decoded images or unknown ShaderGraph/alpha semantics.
- `SculptGpuIdPass::record(model,selectedSurface,originalVertexCount,authorIds,viewProj,request,opaqueOccluders,maskedOccluders)` combines all batches and retains the corresponding GPU vertex buffers until the exact async reply.
- `sculptGpuPickEligibility(model,meshId)` prevents arbitrary authored LOD selection from being treated as matching the base-model ID pass.
- `gfx::Device::readTexturePixelAsync` was added Stage 7AO; 7AQ extends the native GL backend only. The protocol remains opt-in with CPU fallback.

## Verification / safety gates

**Actually run:** remote GitHub source reads, creates and updates. No local worktree or GPU was used. **NOT RUN:** CMake, C++ compiler, Objective-C++ compiler, CTest, native GLES3/GL33 driver, WebGL2 build, Vulkan build, Metal mask pipeline, Metal pixel readback, frame comparison or performance benchmark. The presence of added regression assertions and source shader strings is NOT a claim that their builds/tests pass.

Recommended eventual acceptance, after finishing the remaining functional stages:
```bash
cmake -S . -B build/vivid-onepass -DAIVERSE_BUILD_TESTS=ON
cmake --build build/vivid-onepass --target \
  aiverse_vivid_human_creator_tests aiverse_vivid_metal_sculpt_id_tests --parallel 4
ctest --test-dir build/vivid-onepass --output-on-failure \
  -R '^(vivid_human_creator|vivid_metal_sculpt_id)$'
bash tools/vivid-validate.sh --full
```
Preserve exact branch SHA and copy actual log failures, Metal pixel bytes and GPU timings when available. GLES3 needs a native EGL/Android context-specific smoke; Vulkan needs a separate backend image readback implementation.

## Remaining functional work (Stage 7AR+)

- Renderer3D's exact GPU actor vertex override (48B morph -> 60B native model stream) is **not** consumed by the expanded CPU-built ID mesh. Pixel parity on dense skinned MetaHuman-like scans remains unverified. Do not claim identical GPU deformed geometry until actual GPU vertex-buffer reuse or a verified immutable GPU pose snapshot is integrated.
- Authored/generated LOD selection, fade and culling remain Renderer3D-owned. The current eligibility probe **disables** GPU ID confirmation for authored LODs rather than guessing; implement a matching draw-decision API or pull an accurate immutable visible-primitive list from Renderer3D.
- Material cuts are exact only for *eligible standard base-color alpha sampling*; custom ShaderGraph alpha, team masks, vertex back-occlusion, masked procedural hair without a bound base texture, Blend cornea and Renderer3D depth-after are conservative zero-ID blockers, not production-perfect visibility.
- GLES3 readback exists at RHI source level but is untested and GPU ID needs a frame-safe token/serial contract. Emscripten/Windows GL loader still return unsupported. Vulkan offscreen pixel readback, alignment/barriers/fences and safe completion are not implemented.
- Coordinate Texture RHI branch changes to Model, Gfx and ShaderGraph and do semantic integration on a separate branch. Do NOT merge either feature to main without GPU lifetime, editor stability, Vulkan/GLES build and the user-requested deferred native acceptance.

**Next exact step:** Stage 7AR should implement/verify the renderer-owned visible LOD/vertex stream contract and hardware readback backend completion separately, with a new chronological handoff. Document actual executed tests rather than guessing from source.
