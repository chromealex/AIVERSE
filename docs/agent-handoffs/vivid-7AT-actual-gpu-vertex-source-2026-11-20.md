# VIVID Stage 7AT — GPU ID read of the actual current-frame 60B Renderer3D actor source

Date: 2026-11-20  
Repository: `chromealex/AIVERSE`  
Working branch: `feature/vivid-digital-humans`  
Other agents: `main`, `feature/texture-rhi-unification`, MotionWeave/animation and rendering peers were not modified/merged.  
**Test evidence: new source and regression fixtures only. No native compilation, CTest, Metal hardware shader/pixel test or performance benchmark has been run.**

## Problem solved / exact boundary

Stage 7AS added a renderer-owned frame-scoped `ModelViewProvenance` source mesh/LOD/override handle check, but the experimental GPU ID pass still uploaded a CPU-expanded world-position triangle stream. That stream could differ from the real `GpuActorStream` current-frame 60B `ModelDraw::vertexOverride` used in the normal Renderer3D colour pass.

Stage 7AT implements an opt-in **Metal compute expansion of the same 60-byte GPU ModelVertex source** into the ID pass. It samples the real GPU-written morph position from the same RHI buffer handle, then applies the exact CPU-provided current MotionWeave skin-joint transforms on GPU to construct ID triangles. GPU-generated triangle vertex positions are not read back to CPU, and the actor-owned GPU vertexOverride buffer is **borrowed, not copied or destroyed**. This is not yet full final-colour-pixel parity: the native renderer's own vertex shader/LOD, material, blending, postfx and actual submitted culling must still be matched. The immediate CPU BVH authoring selection remains authoritative; a GPU ID callback may only corroborate its original-source vertex.

## Files / commits / current APIs

| SHA | File | Actual source change |
|---|---|---|
| `dbf39e5252d7c4b78fbb438d81bf83d45e705b06` | `engine/character/vivid/SculptGpuVertexSource.h` (new) | `SculptGpuSourceIndex` (8B), `SculptGpuSkinInfluence` (32B), `SculptGpuSourceParams` (80B), transactional `SculptGpuVertexSource` and `prepareSculptGpuVertexSource(model,mesh,nodeWorld,selectedWorld,currentGpuVertexOverride,overrideVertexCount,authorIds,sourceVertexCount,out)`. Builds GPU triangle *indices and IDs*, strict source model/skin palette and current joint matrix data, not a GPU duplicate of morphed positions. `kSculptGpuSourceExpandMSL` uses the same 15 uint/60B source stride as the established `GpuMorphMeshStream` to read x/y/z position and emits 16B `SculptGpuIdVertex` after rigid or four-weight current skinning. Original-source seam clones map to their original author ID; nonauthorable LOD triangles write ID zero, still contributing depth. |
| `1fba918548ed80c78ccc3f2e63d4946e037e5edc` | same new header | Include C++ utility for transactional move. |
| `65c9488be606e160e66a0eecffaf830d380dc66c` | `engine/character/vivid/SculptGpuIdPass.h` | Own optional Metal GPU expansion ComputeHandle; add `supportsActualGpuVertexStream()`, extra async-in-flight scratch resource ownership and `SculptGpuIdPass::Completed::actualOverride` so callbacks report their actual vertex-source path. If MSL compute compile fails, old CPU-fed diagnostic and CPU BVH remain available; no false claim of GPU source. |
| `01d86893a680de1ded8979aaf31badb6398a6055` | same | Add `record(..., const SculptGpuVertexSource* actualGpuSource=nullptr)` optional API. Reject non-Metal/native-no-compute, unexpected source vertex count, malformed triangle IDs, missing/alpha-selected mask, malformed skin input or mismatch with the CPU-validated selected mesh. When real GPU source is used, exclude selected CPU positions from uploaded geometry, retaining only CPU-fed nonselected scene zero-ID blockers/mask occluders. |
| `e3f812fab8cbe595f5e1b0e521fad1206dceaa0f` | same | Allocate four same-frame GPU buffers (indexed requests, skin weights, current joint transforms and 16B GPU output), dispatch ordered Metal compute reading `source.actualVertexOverride`, then draw the computed vertex buffer with the existing linear RGBA8/depth ID pipeline. A separate GPU buffer still draws CPU-fed scene occluders and normal mask batches in the same depth pass. All temporary buffers remain owned through the true async pixel callback; failed post-encoded readback retains them until an actual GPU-idle retirement barrier and disables repeated GPU jobs. |
| `7e293b0a39069b4843b7155dc9b8223b82b2ead7`, `7dfba9772f8009d0213e8a6d9bf10d66eb532210` | same | Fix C++20 vector byte count, validate **exact original Renderer3D primitive index ordering**, 32-bit IDs and all external joint matrices/weights before GPU dispatch; no silent incorrect source triangle confirmation. |
| `fbf1510f3f7c8a07c882b52caddcc34ea27458c2` | `tools/vivid-creator/main.cpp` | The existing normal Renderer3D colour draw attaches the current GPU actor/CPU actor `ModelDraw::vertexOverride` and calls `drawModel` before the diagnostic; its Stage 7AS actual `modelViewProvenance` identifies the selected GPU source handle/count. When Metal compute is available, Creator verifies exactly that same handle/count, calls `prepareSculptGpuVertexSource` using current `facePose` and selected world's renderer transform, and passes the result to `SculptGpuIdPass::record`. Unsupported or incompatible GPU source fails closed and leaves immediate CPU BVH selection intact. The reply distinguishes actual GPU-source ID confirmation from previous CPU-fed diagnostic; it never overwrites the selected original vertex. |
| `e0995e48d7430be20a23d38f8e1e3e7338c4be3e` | `tests/vivid/human_creator.cpp` | New **unexecuted** regression source covers exact clone source ID and triangle-index ordering, no-ID LOD triangles, 60B source size guard, rigid world transform, two-joint unequal U8-normalized weights, current joint matrices, invalid skin palette rollback and the real 60B GPU MSL source-addressing contract. |
| `a7f3400e63a7de02a9639e61ef1a1ab3ca76a08d` | `tests/vivid/metal_sculpt_id.mm` | New **unexecuted** native Metal GPU fixture: upload a 60B GPU actor source with positions deliberately moved completely outside the ray while leaving the old CPU surface unchanged; center ID must become zero when the GPU source is genuinely read, then source positions restored must produce the near ID. A separate skinned source with translated MotionWeave joint matrix must also move ID coverage. The fixture checks callback `Completed::actualOverride` and actual RGBA bytes. |
| `8f8ade4b28af8362c6af0d84631e0458001e4a24`, `058d0135fb9cd7f04caa47f9b2d4222b9421e4b0` | `tools/vivid-creator/README.md`, `docs/vivid.md` | Public API, GPU/CPU source ownership, same-frame compute/colour dependency, strict feature limitations, deferred native acceptance. |

No CMake edits required in 7AT: `SculptGpuVertexSource.h` and expansion shader are header-only; the existing `aiverse_vivid_human_creator_tests` and native `aiverse_vivid_metal_sculpt_id_tests` targets already include modified fixture sources.

## Source and GPU lifetime contract

1. `Renderer3D::modelViewProvenance(*character.model)` after `drawModel` establishes exact same-frame selected mesh, original primitive index ranges, chosen LOD and **borrowed** `vertexOverride` handle/count, as implemented at Stage 7AS. A distinct `ModelDraw` invocation or a different model/mesh/frame cannot pass validation. This is pre-render provenance, not a final draw/GPU-pixel oracle.
2. For an eligible Metal character with an actual VIVID-owned 60B GPU morph stream, `prepareSculptGpuVertexSource` prepares immutable vertex index/gather and bone metadata only. The skin input maps the local primitive palette to the owning skin's indices using the exact original model's U8 joints/weights; GPU source actor streams from VIVID preserve those immutable metadata while replacing morph positions. Global skin matrices combine the renderer-selected model transform, current MotionWeave `nodeWorld[joint]`, and source `inverseBind[joint]`.
3. The ID compute kernel reads the **same current-frame source GPU buffer** that the normal colour pipeline will consume. Metal command encoders are ordered: VIVID actor morph/pack compute, selected ID compute, ID colour+depth pass and subsequent Renderer3D colour draw all share one gfx-owned frame/command queue.
4. Source vertex override `gfx::BufferHandle` remains owned by `GpuActorStream`/`CpuActorStream` and their existing completed-fence resource-retirement owner; the ID pass does **not** destroy it. Its four temporary compute buffers plus optional CPU occluder buffers live through the async pixel callback; no CPU waits on vertex readback.
5. Only eligible fully opaque selected primitives use the GPU-source expansion: sampled alpha-mask selection, custom ShaderGraph displacement, source/LOD ambiguity and other unsupported shader paths fail closed rather than faking GPU-stream equivalence. Other Creator floor/hair/head depth occluders still use existing CPU-derived snapshot until they also have verified renderer-owned deformed GPU streams.
6. The GPU callback still carries the exact model publication/frame/viewport/pending-click token, resolves triangle ID against the original authoring source mapping and **cannot override** the CPU BVH-selected author ID. A successful `actualOverride=true` says only that the selected ID pass consumed the same actor GPU 60B vertex source, **not** that its separate skinning kernel/material/depth differs by 0 pixels from final Renderer3D.

### Late safety follow-up — Renderer3D transparency source policy

- `Renderer3D.h` commit `b692e2dcbc97a0d15960711d55a4d79e6b791e18`, `Renderer3DModels.cpp` commit `6eb5917d9dbe3d4a682d8d22877a672ca5c1db19`, and `SculptGpuRendererView.h` commit `094b3afecaeaa0c5e164d06d16cb5612dfa1d5e9` extend the frame-local source provenance with the actual main colour-pass translucent classification (material Blend/transparent, instance tint alpha, DrawFlags transparency). The GPU source expansion must refuse translucent/depth-after source rather than interpret its zero-ID depth semantics as an opaque GPU-confirmed surface.
- Unexecuted regression source `tests/vivid/human_creator.cpp` commit `2bb360b24b7f90977b1713737dd2b40a9bbe26e1` verifies translucent source rejection.
- These source changes do NOT add final transparent/cornea pixels to the GPU ID path; the existing CPU BVH remains the authoring selection.

## Actual validation and exact next commands

**Actually performed**: remote GitHub source reads/writes and created commits.  
**NOT RUN**: native C++/Objective-C++ compilation; CMake; `vivid_human_creator`, `vivid_metal_sculpt_id` CTest; Metal GPU compute shader compilation; verified MSL output layout; actual GPU ID pixels, frame timings or VRAM; Vulkan/GLES build; full `bash tools/vivid-validate.sh --full`. The fixture assertions are *not PASS claims*. The source may require compile or shader fixes when the user runs their consolidated native Mac suite.

Deferred macOS commands, from the exact checkout SHA of this feature:
```sh
cmake -S . -B build/vivid-onepass -DAIVERSE_BUILD_TESTS=ON
cmake --build build/vivid-onepass --target \
  aiverse_vivid_human_creator_tests aiverse_vivid_metal_sculpt_id_tests \
  aiverse_vivid_renderer_preview --parallel 4
ctest --test-dir build/vivid-onepass --output-on-failure \
  -R '^(vivid_human_creator|vivid_metal_sculpt_id)$'
bash tools/vivid-validate.sh --full
```

Linux Vulkan/GLES hardware tests from Stage 7AR remain separate; they do not gain Metal GPU-source compute merely by having async RGBA8 readback.

## Remaining next steps (Stage 7AU+)

- **Pixel-perfect GPU parity**: verify GPU skinning matrix/numerical order and output 16B packed vertex attributes on actual hardware against the native Renderer3D skin vertex shader, then share native GPU vertex/primitive commands directly if the common RHI exposes a renderer-owned ID attachment. The current indexed Metal expansion is real GPU-source-position consumption but separately performs skinning and primitive expansion.
- **Additional materials and sources**: exact alpha-tested/skinned selected primitives need a GPU-expanded 32B UV+ID vertex stream; arbitrary Renderer3D ShaderGraph vertex displacement, animated groom GPU particles/strands, blend/cornea/depth-after and custom mesh sources remain conservative CPU/zero-ID blockers.
- **Other native backends**: the true GPU source expansion uses Metal compute only. Vulkan/GLES3 still perform the previous CPU-fed diagnostic ID pass and actual fenced readbacks, subject to native tests. Add Vulkan compute (or a shader-only vertex fetching path) with the same current RHI stream ownership; retain WebGL2 CPU fallback until browser-safe API proven.
- **Final renderer visibility/LOD**: Stage 7AS captures pre-submit ModelItem source decisions, not completed GPU culling/ICB/MDI, alpha depth-after, late pipeline compile, final postfx/cornea screen pixel. Continue to reject cases that cannot prove correspondence.
- **Integration**: changes to shared Renderer3D source/provenance may overlap the parallel Texture RHI / ShaderGraph branch; merge only on an isolated integration branch, preserving both systems and proving actual renderer + Texture RHI lifetime tests before main.
- **Broader MetaHuman level**: scan/skin/eye/groom realism and topology/geodesic brush authoring remain separate incomplete features. This implementation is not a full MetaHuman product claim.

**Next exact action**: Stage 7AU should address GPU-selected masked face UV/alpha input and/or native Vulkan GPU stream expansion, followed by eventual hardware parity and true renderer-owned ID-buffer path. Continue one separate source and handoff update per stage, retaining the user's deferred consolidated test strategy.
