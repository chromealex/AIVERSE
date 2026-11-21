# VIVID Stage 7AU — selected sampled Mask from actual renderer GPU actor source

Date: 2026-11-20
Repository: `chromealex/AIVERSE`
Branch: `feature/vivid-digital-humans`
Stage scope: selected ordinary `AlphaMode::Mask` GPU source/depth ID only.
Peer branches `main`, Texture RHI, MotionWeave/animation and other rendering agents were NOT merged or modified.

## What changed

Stage 7AT consumes the actual 60-byte GPU `ModelVertex` override on Metal but rejected selected alpha-tested materials because it output a 16B untextured ID vertex. Stage 7AU adds a **separate opt-in Metal compute kernel** for homogeneous standard sampled Mask materials. Its position is read from the exact borrowed current-frame actor GPU override; immutable UV and vertex alpha are read from the source `Model`. It writes 32B `SculptGpuMaskVertex`-compatible positions/IDs/UV and draws that GPU output through the existing alpha texture/cutoff ID/depth fragment shader. The selected CPU-expanded masked geometry is omitted only after all source, texture, UV, triangle order and encoded original-source IDs validate.

All CPU BVH sculpt selection and authoring stay authoritative. The GPU ID callback may corroborate the original source hit, never replace it. Existing current-frame Renderer3D provenance and in-flight GPU buffer ownership checks are still required.

## Implementation commits, files and APIs

| Implementation SHA | Path | Change |
|---|---|---|
| `d5de637cddbf3cfb9183407d0a70d10f6566cbdc` | `engine/character/vivid/SculptGpuVertexSource.h` | Adds 16B `SculptGpuSourceMaskInput`, `SculptGpuVertexSource::{maskedSelection,maskBaseTexture,maskUniform,maskInputs}`, homogeneous mask material/texture/finite UV guard and transactional preparation; new `kSculptGpuSourceMaskExpandMSL` reads real GPU 60B source at 15 uint stride, applies current rigid/skin transform, writes 32B output matching mask draw pipeline. |
| `6cec08562902e05706cd5bec02527f9a6327b5d8` | `engine/character/vivid/SculptGpuIdPass.h` | Adds optional `expandMaskCompute_` and `supportsActualGpuMaskedStream()`, exact masked triangle IDs/texture/UV uniform validation, separate fifth source UV input buffer, 32B GPU scratch output and actual sampled Mask draw. Keeps CPU zero-ID occluders, async completion and idle-retirement behavior. |
| `7e4132414d00142a7862c0e517b495a8bd95c11c` | `tools/vivid-creator/main.cpp` | Creator checks native mask GPU compute capability and fails closed without overwriting the CPU picked authoring vertex. |
| `f1e49e080db54ccad5dd0f456b33cc4e914cfebe` | `tests/vivid/human_creator.cpp` | UNEXECUTED tests for sampled masked UV/vertex-alpha/triangle IDs, missing/custom/mixed material or nonfinite UV transactional failure, 16B input/32B output source shader contract. |
| `0b4650bb2c255603debc40f251d1ed5d5983da4e` | `tests/vivid/metal_sculpt_id.mm` | UNEXECUTED Metal fixture: source actor buffer displaced off click pixel but CPU pose unchanged under sampled Mask; then original GPU source tested against alpha hole and solid texture pixel. |
| `1bfd4f552a410addd197fce6e7af5ce90bc233c6` | `docs/vivid.md` | Feature status, limitations and next milestones. |
| `1dee4519ab4b1f25d17acc6d3a46fa8cbb4f58dc` | `tools/vivid-creator/README.md` | Public API usage and conditional GPU mask behavior. |

No public asset, JSON, ECS, AnimationGraph, MotionWeave scheduling, 52-channel face ordering or game genre dependency changed. The existing `SculptGpuIdPass::record(..., const SculptGpuVertexSource*)` signature is unchanged; `SculptGpuIdPass::Completed::actualOverride` still identifies a true 60B GPU actor source (not guaranteed final scene pixel parity).

## Fail-closed/material and GPU resource requirements

1. Renderer3D provenance must identify the actual selected mesh/frame, matching GPU override handle/count, original primitive/source indices and supported LOD. No ambiguous ModelDraw instance ownership.
2. Selected primitives must be all conventional opaque as previously, **or all share one ordinary sampled `AlphaMode::Mask` material** with a resident GPU base texture. Mixed Mask/Opaque, distinct Mask materials, Blend/translucent, ShaderGraph, unsupported vertex occlusion or invalid UV/cutoff are rejected. No inferred shader material alpha on CPU.
3. The position source remains the borrowed renderer actor GPU buffer; the mask's immutable UV/alpha input is separately uploaded for the click. The GPU output is 32B and owns temporary memory only through the asynchronous callback, or after a real GPU-idle barrier if readback submission fails.
4. The sampled material uses the existing `SculptGpuMaskMSL` fragment shader and its texture, alphaCutoff and UV parameters. Other scene occluders still use the conservative CPU-derived depth/masked batches, not fictitious GPU deformed positions.
5. The feature requires Metal + supported compute pipeline. Vulkan/GLES3 still have the CPU-fed ID path, and browser/WebGL2 is CPU fallback.

## Actual verification

- **Performed:** GitHub connector reads and source file writes/commits, followed by a source-level contract review of shader presence, include chain, 16B/32B ABI declarations, mask input binding, true source gather, opaque/masked branch gating, Creator fallback and newly inserted test-fixture source paths. These are static inspections, **not C++ compilation or GPU passes**.
- **NOT RUN:** CMake configure, C++/ObjC++ compile, actual shader compile/Metal hardware validation, `vivid_human_creator` and `vivid_metal_sculpt_id` CTests, pixel parity, frame/VRAM benchmarks, Vulkan/GLES3 tests, and full `bash tools/vivid-validate.sh --full`. There is **no green/PASS claim** for Stage 7AU. The remote GitHub connector has no macOS GPU or native project build host.

Deferred verification on an Apple Silicon host, after functionality closure:
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
Linux/Android Vulkan and native GLES3 driver fixture runs are separate and are not certified by the Metal suite.

## Known limitations and immediate next stage 7AV

- **Native hardware acceptance and fixes come first at the final consolidated test gate.** The source/fixture implementation may still reveal compiler errors, shader-layout differences or unsupported pipeline requirements when actually run.
- **Stage 7AV**: make actual GPU deform/skin visible-pixel correspondence measurable against Renderer3D's native vertex shaders, capturing current-frame renderer-submitted primitive/depth provenance (not merely pre-submit ModelItems). Add a targeted mask pixel/depth parity frame with simultaneous selected GPU actor and unrelated visible alpha occluder.
- Integrate the same 60B actor source into Vulkan (compute or native vertex fetch) using gfx-owned fences, then assess GLES3 vertex fetching without assuming writable compute. Preserve WebGL2/Windows CPU fallback until proven.
- Still unsupported for exact ID vs colour parity: arbitrary ShaderGraph vertex displacement, Blend/cornea depth-after, final GPU culling/ICB/MDI, alt LOD/LOD fade, moving groom GPU occluders and material/texture residency edge cases. Shared Renderer3D/Model/Gfx changes overlap Texture RHI and other agents: use a dedicated integration worktree, never overwrite their work or merge to main prematurely.
- Wider MetaHuman-level requirements (licensed source content, realistic skin/eyes/hair, topology-aware sculpt/scan authoring, end-to-end retargeting) are not complete. Keep feature status explicit.

Next agent: read `AGENTS.md`, `engine/character/vivid/AGENTS.md`, `docs/vivid.md`, `docs/agent-handoffs/vivid.md`, and this handoff; use `feature/vivid-digital-humans` and preserve other agents' branches. Commit implementation first and the next stage handoff separately.
