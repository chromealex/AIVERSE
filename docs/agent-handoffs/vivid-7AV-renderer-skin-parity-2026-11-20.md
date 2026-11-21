# VIVID Stage 7AV — Renderer3D skinning arithmetic parity for actual Metal actor GPU source

Date: 2026-11-20
Repository: `chromealex/AIVERSE`
Working branch: `feature/vivid-digital-humans`
Pre-stage HEAD: `472ac4384b3e8e9c2f72083ca80c28eccb741d82`
The parallel `main`, Texture RHI and MotionWeave/animation branches were NOT modified or merged.

## Precise scope and outcome

VIVID Stage 7AT and 7AU consumed the actual current-frame renderer-owned 60-byte GPU `ModelVertex` actor morph source, but their *separate Metal compute skin* applied the model/world transform inside each bone matrix, accumulated `matrix * position * weight` and CPU-normalized original UNORM8 skin weights. The native `Renderer3D` shaders instead apply the joint matrices in **model space**, accumulate three 3×4 matrix rows from original per-vertex weights normalized on GPU, form the dot-product position, **then** multiply by the source `I.model` world transform. Although mostly mathematically equivalent, the different floating-point rounding and palette limits could lead to a false claimed GPU-pixel corroboration.

Stage 7AV aligns VIVID's native Metal compute **operation order and ABI** to the existing `engine/render/ModelShaders.h` `SKINNED` vertex shader. For both opaque selected sources and homogeneous standard sampled alpha-masked selected sources, new 48-byte `SculptGpuSkinRows` are packed from `nodeWorld[joint] * inverseBind[joint]`; the GPU uses each original UNORM8 weight converted to float and normalized by a single dot denominator, blends three rows, transforms the original borrowed GPU vertex source by row dot products, and finally applies one current Renderer3D model/world matrix. The output remains 16B ID or 32B Mask ID/UV, without staging CPU morph positions.

A per-primitive palette exceeding `kMaxPaletteJoints = 80` now fails closed: Renderer3D's original U3 shader upload has a fixed 80×3×float4 (=3840B) capacity, so allowing more joints on the GPU picker would no longer match its actual main draw. The model's complete skin can have more global joints; only the renderer-visible primitive's palette must be representable. The existing source-owner/primitive index and per-vertex local palette consistency checks remain.

The Creator requires all currently selected Renderer3D primitive submissions to use **one exact renderer model-world matrix and skinning mode**, rather than just the same 60B GPU actor buffer. Sampled Mask GPU selection also validates every original-source UV and vertex-alpha value against the indexed selected CPU geometry *before* encoding GPU commands; a stale or manipulated source may not report `actualOverride=true`.

This is **a source-level arithmetic-parity implementation, not verified final colour-pixel parity**. No renderer-owned post-culling/submitted-command ID/depth attachment has been implemented here. Do not claim all final Renderer3D pixels are measured, or that native CMake/Metal tests passed.

## Implementation SHAs and files

| SHA | File | Change |
|---|---|---|
| `bf029cb76160df479d7301d35c300cfb5ab5aeae` | `engine/character/vivid/SculptGpuVertexSource.h` | New 48B `SculptGpuSkinRows` and `sculptGpuRendererSkinRows()`. Exact renderer palette limit for each primitive; original UNORM8 weights; model-space joint matrices; both `kSculptGpuSourceExpandMSL` and `kSculptGpuSourceMaskExpandMSL` compute kernels use identical native renderer row/dot skin and post-skin `params.rigidWorld`. |
| `7ea9d930df98291f44d60273c2c7479ec5f638e7` | `engine/character/vivid/SculptGpuIdPass.h` | Uploads the 48B 3×4 row array to the existing joint compute binding rather than 64B full joint matrices, with the unchanged async pinned scratch/resource-retirement path. |
| `51b95df0bea96dba133b6d47279af328b4c90e97` | `tests/vivid/human_creator.cpp` | **NOT RUN** source fixtures: verify 128/255 + 64/255 GPU raw weight data, two-joint row blending, root world applied after skin, ModelShaders.h native MSL string parity, and over-limit primitive palette rollback. |
| `85800430f7f49184fa19ce4cb8bc0441df3bdb1a` | `tests/vivid/metal_sculpt_id.mm` | **NOT RUN** real-Metal pixel fixture: two unequal skin influences shift source out of the click; compensating renderer-world transform returns the original near triangle ID. Test actor GPU buffer includes original skin joint/weight metadata, not stale CPU-only edits. |
| `f16902b248a1707ed71e3717225e24cd7fb61b7` | `engine/character/vivid/SculptGpuIdPass.h` | Requires selected GPU mask's indexed original source UV and vertex-alpha attributes to match the existing CPU-selected primitive mask input exactly. |
| `0b859b6ba05fc4ea335876e44aad1e009cd5fa5d` | `tests/vivid/metal_sculpt_id.mm` | **NOT RUN** native test rejects stale or mutated selected GPU-mask UV metadata before GPU draw encoding. |
| `6eef1294e503a0afec104f23e396d6b8b29b8eb4` | `engine/character/vivid/SculptGpuIdPass.h` | Bounds-checks original source GPU index before mask UV dereference to avoid malformed-input UB. |
| `34c86578923b0c4fb183de175d01a3780f230651` | `tools/vivid-creator/main.cpp` | Validates consistent selected current-renderer primitive matrices and skinned mode before using one GPU source ID compute draw. |
| `5b51e0045fcb65ba3b3ba668f19e0a5441ec41ae` | `docs/vivid.md` | Stage 7AV architecture, fallback, limitations. |
| `b0c30dcf68c603d102e5025de934a7a77c27f323` | `tools/vivid-creator/README.md` | Creator and public API contract, native test status and next stage. |

No material DNA format, 52 ARKit face-channel ordinal, public AnimationGraph/MotionWeave scheduling, renderer asset type, genre-specific ECS state, or existing `SculptGpuIdPass::record` and completion signatures changed. This stage is entirely the shared genre-independent VIVID presentation/renderer source pipeline.

## Validation performed and exact status

- **Actually performed:** GitHub connector source reads/writes and commit operations. Read the live branch's `ModelShaders.h` native Metal/GLSL skin shader, `Renderer3DModels.cpp` paletteMatrices/drawModel implementation, `Animation.cpp` palette mapping, and VIVID GPU inputs. A 14-condition connector-side static source review passed: both compute kernels share the row/dot order and post-transform; 48B ABI appears in source; raw UNORM weighting, palette limit, GPU mask indexed UV gate, model source gate and new fixture sources are present. The static checks are *not* C++/Metal compiler validation.
- **NOT RUN:** CMake configure, C++20 and ObjC++ native compiler, CTest, Metal shader compilation, actual RGBA8 pixel capture and finite precision measurements, Vulkan/GLES/GL33 devices, GPU frame/VRAM benchmarks, complete `bash tools/vivid-validate.sh --full`, other engine tests and CI on this HEAD. No measured shader pixel-parity claim.
- The only available remote workflow was GitHub source access, not a local user Mac/macOS Metal build. Future test failures must be fixed rather than retroactively claimed green.

## Deferred macOS one-shot acceptance

```sh
git fetch origin
git switch feature/vivid-digital-humans
git pull --ff-only
cmake -S . -B build/vivid-onepass -DAIVERSE_BUILD_TESTS=ON
cmake --build build/vivid-onepass --target \
  aiverse_vivid_human_creator_tests aiverse_vivid_metal_sculpt_id_tests \
  aiverse_vivid_renderer_preview --parallel 4
ctest --test-dir build/vivid-onepass --output-on-failure \
  -R '^(vivid_human_creator|vivid_metal_sculpt_id)$'
bash tools/vivid-validate.sh --full
```
Verify the `Metal` fixture's unequal-weight + world cancellation and actual sampled-mask UV/hole results before calling parity correct. Independently run Vulkan and native GLES3 fixture tests on their own target hardware; the Metal run does not certify them.

## Known limitations / precise Stage 7AW next step

1. **Next 7AW**: Add minimal Renderer3D-owned current main-colour-pass **accepted-command provenance** for the *real submitted/encoded primitive* (including phase, per-source primitive ID, actual pipeline and GPU override, batch/indirect, material alpha and transform). Have VIVID consume it only after render submission and require an immutable matching frame token, without copying or owning the renderer ModelItem state. Merely pre-render `modelViewProvenance` is not enough to claim a final visible draw.
2. Add a native frame-level colour/depth-versus-ID pixel oracle for a dynamically skinned actor + one nearer unrelated sampled alpha occluder. The current test fixture measures the independent ID pass **only**, not the final colour framebuffer. Do not assert pixel equality without this comparison and a consistent GPU fence/readback.
3. Native Metal GPU math may show tiny numerical differences with compiler fast-math or material shader path; test actual device. Per-primitive same-vertex different palette, float-hidden occluders and conditional LOD remain conservative fail-closed cases.
4. Metal true GPU source expansion is implemented; Vulkan/GLES3 still use only CPU-fed ID diagnostic and pixel callbacks. Vulkan compute/shared vertex-fetch and GLES3 native access need backend-owned source lifetime. WebGL2/Windows fallback stays CPU until proven.
5. ShaderGraph vertex displacement, transparent/cornea depth-after, GPU ICB/MDI culling, other GPU-deformed occluders, custom merged material batches, advanced hair and MetaHuman-level scan/skin/groom realism remain open. Shared `Renderer3D*` and Texture RHI integration is owned by other parallel agents: use a dedicated semantic integration branch and do not overwrite their work.
6. Re-read the latest `AGENTS.md`, `engine/character/vivid/AGENTS.md`, `docs/vivid.md` and the full canonical `docs/agent-handoffs/vivid.md` before the next implementation. Keep every bounded feature commit followed by a separate committed handoff with true test status.

Next action: Stage 7AW renderer-accepted-command provenance plus a genuine final colour-pixel parity oracle, then continue backend support and later run the deferred consolidated Mac validation.
