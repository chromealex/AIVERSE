# VIVID Stage 7AY — real Metal Depth32Float parity and a second GPU-sourced Mask occluder

Date: 2026-11-20
Repository: `chromealex/AIVERSE`
Working branch: `feature/vivid-digital-humans`
Pre-stage HEAD: `32785d29d97a949904c4bdf673b4b195114afb4b`
Peer branches (`main`, Texture RHI, batching/STRATA, MotionWeave) were **not merged, rebased, force-pushed or modified** by this stage.

## Bounded implementation

Stage 7AX could corroborate only *screen RGB versus ID pixel coverage* for the same actor, not numeric depth. Normal full-frame `captureNextFrame` can arrive a frame later and is not a sufficiently strict source for same-GPU-frame comparison. Stage 7AY addresses these correctness gaps with true native Metal GPU depth sampling, exact-frame colour pixel copy, and an opt-in second independent GPU-resident masked actor source in the VIVID ID depth pass.

### 1. Native GPU depth through the canonical RHI

`gfx::Device::readTextureDepthPixelAsync(depth,x,y,callback)` is an optional virtual API, with a conservative `false` default on unsupported backends. A valid handle must refer to a Depth render target; an invalid handle explicitly means the current screen depth. Metal's implementation checks that the native texture is `MTLPixelFormatDepth32Float`, single-sample, within bounds and outside all render/compute/ICB encoders; it issues an ordered texture->shared-buffer 1×1 blit with 256-byte row alignment, then dispatches a `callback(bool valid,float depth01)` through the existing owner-thread `deliverBufferReads()` after actual command completion. A GPU error, invalid float or unsupported texture is not reported as a valid depth sample. This is **not CPU-projected depth**, and no Vulkan/GLES parity is claimed.

`SculptGpuIdPass::record` adds trailing optional `captureDepth=false` and `gpuMaskOccluder=nullptr` arguments without changing existing call sites. If enabled, the ID pass reads its own Depth32Float target as well as RGBA8 and `Completed::{depthValid,depth01}` become usable only after both callbacks have completed. The pass pins compute/index/mask/GPU output buffers until callback retirement; failed post-draw readback keeps buffers until proven owner-thread GPU idle and blocks additional submissions.

The native one-actor `aiverse_vivid_renderer_preview` now uses `setScreenReadable(true)`, an exact-frame screen `copyColor` to an owned RGBA8 render target and `readTexturePixelAsync` for the colour sample. Delayed `captureNextFrame` remains only for full-image A/B screenshots. In the *same* frame and (top-left) head-centre pixel it reads native screen depth and the separately rendered GPU ID depth. Source frame serial, model draw provenance, GPU vertexOverride, pixel coordinates and the two ID readbacks are validated before classifying a result. `SculptGpuColorPixelOracle.h::compareSculptGpuDepthPixels` rejects nonfinite, clear or out-of-range values and requires absolute difference <= 0.003 (provisional native tolerance). No PASS is claimed before actual hardware logs.

### 2. Second GPU-resident Mask occluder

An optional `const SculptGpuVertexSource* gpuMaskOccluder` lets the Metal ID pass use a **distinct** current-frame 60B `ModelVertex` buffer for an unrelated sampled alpha-masked actor. The second compute reads that actor's own GPU positions, joint palette and original source UV/alpha (no CPU reconstructed blocker positions), writes the normal 32B Mask ID vertex format, and **forces all triangle IDs to zero** before dispatch. Draws happen in the same ordered RHI frame and actual RGBA8+depth pass, after the selected actor. Opaque sampled texels occlude the source via native depth; alpha holes discard and reveal it.

The host must establish the other actor's real Renderer3D current-frame model/mesh/LOD/handle/material provenance before calling this optional API. `sculptGpuMaskedOccluderInputsValid` checks bounded, finite source inputs, distinct GPU handle, triangle count, U8-weight rows/palette, UV and mask factors but **does not claim to query a second renderer's final colour submission**. Creator does not automatically enable the external GPU occluder until it can prove that provenance. The Metal fixture uses two independent **GPU-resident** 60B buffers with intentionally different GPU positions; the test input buffer bytes are uploaded from CPU, not demonstrated to originate from a real GPU morph-compute dispatch. The isolated ID pass proves GPU position gathering and sampled-depth behaviour when run; actual normal-renderer occluder-colour identity is a later gate.

### 3. Existing GPU Mask correctness fixes

Two earlier Stage 7AU source defects were found by the Stage 7AY review:

1. `sourceOrdinal` for GPU-selected `AlphaMode::Mask` was compared against `selectedOpaqueCount` instead of the total selected vertex count, so valid fully masked primitives failed even with the exact original source indices.
2. After intentionally removing selected Mask CPU geometry (`grouped.clear()`), the source count was not charged to the ID pass's scene budget/non-empty gate. A valid GPU-only Mask selection without unrelated CPU blockers could therefore be rejected as empty.

Both checks now use the real GPU-selected Mask triangle count; the source/palette/mask input still fails closed on unsupported materials.

## Exact implementation and test commits

| SHA | Path(s) | Result |
|---|---|---|
| `3123e30ff6e24d19ca1be91b93659228acb5940d` | `engine/gfx/Gfx.h` | Optional `readTextureDepthPixelAsync` with fail-closed portable default and owner-thread callback contract. |
| `3fca10e38099b13f7d4b757711d14e01148e6b25` | `engine/gfx/metal/GfxMetal.mm` | Metal Depth32Float async blit from explicit/screen depth, actual RHI frame/fence and float validation. |
| `7d60eb5616d4b4aeb6124e50aade905b31bcab8d` | `engine/character/vivid/SculptGpuIdPass.h` | Optional native depth readback, callback coordination and new fields in `Completed`. |
| `3da6cc91ac0f8b328bd2e96f694aab3daac1eea8` | `tests/vivid/renderer_preview.cpp` | Native scene-screen depth and independent ID depth at same frame/pixel; provisional 0.003 tolerance. |
| `a511479a933518fb0728f84cbc22055f7bb1b185` | `tests/vivid/renderer_preview.cpp` | Exact same-GPU-frame screen `copyColor` + offscreen RGBA8 pixel read; delayed screenshots no longer parity evidence. |
| `0312aeef83327509a705648784de7b2095aca1ab` | `tests/vivid/metal_sculpt_id.mm` | **UNEXECUTED** real native depth z assertions for selected, nearer opaque and alpha Mask hole/solid ID pixels. |
| `647ae3be56e04e6b58e55d67ff24908f628ef340` | `engine/character/vivid/SculptGpuIdPass.h` | Selected Mask original source index count fix. |
| `053cf98a57e5769e6dc6c72ddde3f7d73f840ad9` | `engine/character/vivid/SculptGpuIdPass.h` | Second GPU-sourced sampled Mask depth blocker with transient all-zero IDs, additional compute dispatch and pinned scratch. |
| `c343ab61a9f0d0176a52e978ca9183eccddec0f5` | `tests/vivid/metal_sculpt_id.mm` | **UNEXECUTED** two actual independent GPU-resident ModelVertex streams: moved mask source, solid Mask depth blocking, sampled alpha hole visibility and native GPU depth. |
| `930f6bdcacf621ff9a572f453329ca6c9438b5fa` | `tools/vivid-validate.sh` | GUI acceptance now requires both sampled real `depth_scene`, `depth_id` and delta values in addition to colour/ID coverage. |
| `a0e6f24219bea4eefc2b90da0d87a9a848121425` | `engine/character/vivid/SculptGpuIdPass.h` | Prevents GPU-only selected Mask geometry from failing the empty-scene/triangle budget gate. |
| `86da013f7b6edf140535f39954ede5d8847ab938`, `a8915f980089e649c455c959bef987ae542ea303` | `engine/character/vivid/SculptGpuVertexSource.h` | Pure, finite, bounded Mask occluder source validation, including distinct GPU handles, indices, skin weights, alpha factors and cutoff. |
| `b8e89c9d5f4df19df6017da66657bc5a5995b651` | `engine/character/vivid/SculptGpuIdPass.h` | Reuses the pure input validation before any GPU occluder compute. |
| `cbf7e4011934cb0144fecaaeac1562c22c785d04` | `tests/vivid/human_creator.cpp` | **UNEXECUTED** source preflight rejection fixtures for duplicate GPU owner, invalid indexed source, NaN UV and wrong count. |
| `c51175a25d9b05f46c172600dda446cbe1798b23` | `engine/character/vivid/SculptGpuColorPixelOracle.h` | Pure actual-GPU-depth comparison with finite/range/clear validation. |
| `6e34effe3e39db75bfcaeb97d23f207c7dbfaed6` | `tests/vivid/renderer_preview.cpp` | Uses the shared depth comparison helper. |
| `c6cf82d72c4a240c8826d318bf2fae51ee47a548` | `tests/vivid/human_creator.cpp` | **UNEXECUTED** finite/close/mismatch/clear/NaN depth reference tests. |
| `ac5382d052abb5a4f7d59cd35acc100cc4c9de1b` | `engine/character/vivid/SculptGpuIdPass.h` | Clarifies post-Renderer3D same-frame Metal command ordering semantics. |
| `5252f5182f6c1eaedb4032394a29e748eca0957d` | `docs/vivid.md` | Status, RHI contracts, exact coverage vs numeric depth boundary. |
| `c7ffcdfcfafda5485d5bcc4e27f44e9243d3e1b3` | `tools/vivid-creator/README.md` | Creator-facing optional API, source validation and device-specific limitations. |

No HumanDNA/skin recipe JSON changes, no renumbering of 52 facial channels, no genre-specific ECS or second MotionWeave scheduler, no changes to the 60B ModelVertex ABI. The RHI virtual method is **optional** and will return unsupported on Vulkan, GLES3, GL33 and WebGL2 until implemented and verified separately. `SculptGpuIdPass::record` only adds trailing optional parameters.

## Actual validation (no fabricated native tests)

**Actually performed:** GitHub connector reads, file updates/commits and read-back static review of nine affected files. A 22-condition source inspection matched the expected contracts (Depth32Float RHI API, Metal async owner callback, true source GPU buffers, selected/occluder source safety, zero IDs, numeric depth helper, current-frame colour copy, two native fixture cases, documented runner log fields). These are static textual audits only, **not compiler tests**.

**NOT RUN:** macOS native CMake configure; C++ or ObjC++ compilation; Metal compute or render pipeline compilation; native `vivid_human_creator` and `vivid_metal_sculpt_id` CTests; actual Depth32Float blit/colour/ID readbacks; GUI image capture; Metal GPU fence lifetime tests; Vulkan/GLES hardware; `bash tools/vivid-validate.sh --full`; full engine regression, VRAM and frame benchmarks. There are no actual hardware PASS or shader-colour/depth-parity measurements for 7AY. Source additions may need corrections once compiled on the Mac.

### Deferred macOS commands

```sh
git fetch origin
git switch feature/vivid-digital-humans
git pull --ff-only
cmake -S . -B build/vivid-onepass -DAIVERSE_BUILD_TESTS=ON
cmake --build build/vivid-onepass --target \
  aiverse_vivid_human_creator_tests \
  aiverse_vivid_metal_sculpt_id_tests \
  aiverse_vivid_renderer_preview --parallel 4
ctest --test-dir build/vivid-onepass --output-on-failure \
  -R '^(vivid_human_creator|vivid_metal_sculpt_id)$'
bash tools/vivid-validate.sh --full
```
Capture the exact tested SHA, actual RHI lastError, native Metal command-buffer and RHI depth logs, real scene/ID depths and their tolerance. Do not merge to main until semantic shared Renderer3D/Texture RHI reconciliation and the native gates are satisfied.

## Remaining blockers and next exact Stage 7AZ

1. **Prove independent occluder normal colour identity.** Render the other animated sampled alpha-mask GPU actor through `Renderer3D::drawModel` and the same current camera, track the accepted draw source for both selected + occluder, then compare actual normal Renderer3D colour and depth at both transparent and opaque mask texels against the ID pass using the same two GPU buffers. The current mask fixture only proves separate GPU actor vertex bytes are used by the independent Metal ID path, not that the other GPU actor was actually submitted to the main colour pass. Add a producer compute deformation before both draws to prove GPU-**written** positions.
2. **Full GPU-parity limitations:** renderer colour/ID pixel equality under arbitrary ShaderGraph vertex offset, blended cornea/depth-after, GPU ICB/MDI culling, alternative LOD/LOD fades, dynamic groom and unrelated non-mask opaque GPU actors are still unsupported, with CPU BVH authoritative as fail-closed authoring fallback.
3. **Cross-backend:** Vulkan/GLES do not yet implement true GPU 60B source expansion nor native depth readback. Add backend-owned shared vertex-buffer/queue/retirement semantics; avoid treating CPU-fed ID as GPU parity.
4. **Mac acceptance:** test actual Metal depth32->buffer blit support and 0.003 depth tolerance, offscreen/screen colour-space and top-left sampling, independent scene attachments, async cancellation and shutdown. Address compiler/runtime failures before claiming green.
5. **Integration:** `engine/gfx/Gfx.h` and `engine/gfx/metal/GfxMetal.mm` now overlap Texture RHI/batching and potentially other renderer agent changes. Reconcile with current `main` on an isolated semantic integration branch; never blindly replace concurrent shader/material/RHI lifetime work.
6. Keep handoff: implementation commit(s) first, separate dedicated + canonical living handoff commits on the same VIVID branch with exact actual test evidence and unfinished tasks.

Next agent reads `AGENTS.md`, `engine/character/vivid/AGENTS.md`, `docs/vivid.md`, `docs/agent-handoffs/vivid.md` and this dedicated Stage 7AY handoff. Do not infer success from source presence or string checks.
