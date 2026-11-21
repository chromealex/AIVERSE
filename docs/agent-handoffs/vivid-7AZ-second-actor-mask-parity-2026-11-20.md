# VIVID Stage 7AZ — two independently GPU-produced actors in Renderer3D and the VIVID Mask/ID depth pass

Date: 2026-11-20
Repository: `chromealex/AIVERSE`
Branch: `feature/vivid-digital-humans`
Base HEAD at the start: `dffad02577e5b235b05ddde2fb7637b948281a9b`
`main`, Texture RHI, batching/STRATA, Prisma and MotionWeave peer branches were not merged or modified.

## Exact bounded feature

Stage 7AY introduced actual native Metal scene-vs-ID Depth32Float single-pixel readback and a second independent GPU Mask occluder for the VIVID ID pass. It did **not** prove that the same occluder participated in a real main-camera Renderer3D colour/depth pass, or that a GPU *compute producer* had written its 60-byte ModelVertex positions. 7AZ adds an opt-in **true two-model native Metal acceptance scenario** to the existing `aiverse_vivid_renderer_preview`, without a fork of Renderer3D and without mutating the normal Creator, face DNA or animation runtime.

### Renderer-owned submitted-source provenance

`Renderer3D::trackModelSubmissions(const Model* selected,const Model* occluder=nullptr)` supports two borrowed Model pointers in the same current-frame scene, while the old single-model invocation remains valid. The bounded main-colour accepted-command list records both. `modelSubmissionProvenance(model)` filters returned commands to the requested source Model; it does **not** return another model's records by mistake. Both pointers reset at `beginScene` and late opt-in still fails closed. No persistent global scene database or per-frame tracking of unrelated game models was introduced.

`sculptGpuPairedRendererViewsAccepted` checks each Model against its own exact staged/source ModelView + accepted-direct-main-colour command, then checks that both have live, distinct GPU vertexOverride handles in the same frame. Any missing direct submission, conditional LOD, indirect/instanced draw, stale token, duplicated actor GPU source, unknown shader offset or altered index range fails closed before an ID pass claims parity. This source check is independent of actual per-pixel alpha/depth results.

### Real GPU-written second actor, colour and depth

The new preview mode `AIVERSE_VIVID_MASK_PARITY=1` runs on macOS Metal only with one selected VIVID actor and a second, separate model:

1. The original selected human continues using its real VIVID GPU-morphed `ModelDraw::vertexOverride`, with MotionWeave pose in stage 2.
2. The second model is a small front-facing triangle in the current camera view with a conventional double-sided `AlphaMode::Mask`, alpha cutoff 0.5, UV (0.5, 0.5) and two independent immutable 1×1 base textures (identical RGB, alpha=0 or alpha=255). Its CPU bind mesh and texture resources remain immutable while GPU draws are in-flight.
3. A new Metal compute shader reads all 15 uint32 words of each original 60-byte `ModelVertex`, copies them into an independent storage buffer and **writes the position bytes on the GPU**. Offset +0.075 in stage 1 and -0.065 in stage 2 ensure the Renderer3D colour pass consumes an actual GPU-deformed 60B source, not a CPU-updated bind-pose surrogate. Both buffer handles are distinct from the selected human.
4. The normal **`Renderer3D::drawModel(maskDraw)` colour/depth pass** receives this exact second compute output as `maskDraw.vertexOverride`, and both submitted source Models are queried after the normal scene colour pass. The ID pass then prepares a `SculptGpuVertexSource` for the **same** masked GPU buffer, with original material, UV and cutoff, and draws the second actor through Stage 7AY's zero-ID GPU Mask occluder compute path.
5. **Stage 1: alpha hole**. The solid selected human should be visible. Both actual renderer colour/depth and the separate ID/depth readbacks should agree at the single projected head-centre pixel; the selected original-source triangle ID must be nonzero.
6. **Stage 2: solid mask**. The independent GPU Mask triangle should hide the selected human centre. Normal renderer colour is visible but the selected ID is exactly zero. The independently rendered ID target must report the mask's nearer actual depth within the provisional 0.003 tolerance of normal Renderer3D GPU Depth32Float at the same frame/pixel. The generic colour coverage helper returns Inconclusive for colour+ID-zero; this bounded fixture treats it as expected occlusion only after both accepted submissions, known sampled alpha and measured matching GPU depths.
7. Scratch, GPU-produced actor output, compute pipeline, sampled textures and Models are retired **after a real gfx owner-thread GPU idle barrier**, not merely after frame serial change.

The standard single-actor three-screenshot A/B and 12-actor crowd preview modes remain intact. `tools/vivid-validate.sh` now invokes a separate `renderer-mask-parity` GUI mode, requires its three screenshots and actual `stage=1 mask=alpha-hole PASS` / `stage=2 mask=solid-occlusion PASS` logs. These strings are **test conditions**, not actual pass results.

### Source/test changes and exact implementation SHAs

| Commit SHA | File | Summary |
|---|---|---|
| `3f29e386809ff42d52cd63c9eeb4e50c627f1d3b` | `engine/render/Renderer3D.h` | Optional second Model pointer in source-colour submission tracking, backwards-compatible single Model signature. |
| `36550347e4e7df6b8cf5da360080c5c0aaa4f892` | `engine/render/Renderer3D.cpp` | Reset second source pointer at `beginScene`; frame-scoped ownership. |
| `a9fc10425fa4f768c254304e2eaa291a89196752` | `engine/render/Renderer3DModels.cpp` | Capture either tracked Model's actual encoded main-colour commands; filter query to one original Model. |
| `7bf6e98dce6ef7ca20fd2033c00a693618f635f0` | `tests/vivid/renderer_preview.cpp` | Add optional Mask Model, two sampled alpha textures, model upload and new **real** Metal 15-word/60-byte compute vertex producer. |
| `c9914c1b57203db32300c995d162b10fccec1f95` | same | Dispatch Mask vertex compute before normal renderer; draw with its GPU-written vertexOverride in normal main-camera colour/depth. |
| `1664d47a262488220b1654a896560251a9938e5d` | same | Validate second current-frame renderer main-colour source, then reuse the identical GPU mask buffer in the independent ID/depth pass. |
| `c0587820a9688a42d9ad4b1f9c2738711f1ed12b` | same | Require nonzero source ID for alpha hole and zero selected ID under the solid Mask plus actual same-frame GPU scene/ID depth comparison. |
| `2bab4a8c2777948938706464494d5effa59abcdf` | same | Retire Mask Model, source/output buffers, native compute and textures only after verified queue idle. |
| `8ea5876c0397d03ee80bb7c7db8226bf3b3e6bd6` | `tools/vivid-validate.sh` | Separate Metal two-model GUI acceptance invocation and exact hole/solid result gates. |
| `0cc8598e9c73cb7eeeed38c049a960625776bc11` | `engine/character/vivid/SculptGpuSubmittedView.h` | Pure two-actor accepted-source gate requiring correct current-frame direct commands and distinct GPU handles. |
| `3697290e06f51b86aa268ec9d4c22395256256fe` | `tests/vivid/renderer_preview.cpp` | Use shared paired GPU-source verification before ID pass. |
| `436d097e05c363d0b07a0f48a5c33508a2492bf5` | `tests/vivid/human_creator.cpp` | **UNEXECUTED** pure fixture checks shared, stale, missing or changed second GPU source/renderer command rejection. |
| `9869faecc0e070144ae16d0658dd216fd3965ff9` | `docs/vivid.md` | Feature status and exact remaining scope. |
| `df8fc048d10d7194fea60944c8f09db570b0bd99` | `tools/vivid-creator/README.md` | Native preview usage and current API limitations. |

No changes to HumanDNA v1 JSON, 52 ARKit-style facial channel ordering, the shared 60B ModelVertex ABI, gameplay ECS authority, or MotionWeave graph scheduling. New Renderer3D two-target tracking remains fully opt-in. Stage 7AY's Metal Depth32Float and exact-frame colour-copy RHI APIs are reused unchanged.

## Real validation, honest status

**Performed:** GitHub connector read/write/readback and a 23-condition static source-contract inspection of the new renderer tracking and filtered query, GPU compute producer, normal Renderer3D Mask draw, two independent source handles, source gate, actual frame/pixel depth correlation, hole/solid assertions, resource retirement, unit fixture presence and validation runner. All 23 textual inspections matched expected code paths.

**NOT RUN:** native macOS CMake configure; C++20 and ObjC++ compilation; Metal shader compilation; `vivid_human_creator` and `vivid_metal_sculpt_id` CTests; GUI `aiverse_vivid_renderer_preview` one-actor/crowd/mask modes; any actual Metal colour/ID/depth pixel comparison, screenshots, GPU queue timing/fence sampling, full `bash tools/vivid-validate.sh --full`; Vulkan/GLES tests; VRAM and performance measurements. There are no real hardware PASS logs. The code may require native compiler/shader/driver corrections and empirical tolerances.

### Deferred native Mac acceptance

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
Record exact checked SHA and actual native renderer/Metal draw, shader, pixel and depth logs. A source review is not a successful build.

## Known limitations and next Stage 7BA

1. First run the deferred native build and dedicated `renderer-mask-parity` GUI check when a real Metal Mac build host is available. Verify correct alpha-holes vs solid cutoff with the actual GPU-deformed second vertex stream, current-frame depth differences, screenshot probe orientation and callback ordering, and fix any compiler, shader or numerical errors. Do not merge to main until native evidence exists.
2. **Stage 7BA:** make optional multi-object occluder collection available to Creator/editor through renderer-owned visible and accepted-source filtering. Add multiple GPU-deformed masks, mixed opaque/material occluders and nested ownership with a bounded count and strict GPU lifetime. The current 7AZ case explicitly validates only one independent sampled Mask triangle and one selected human.
3. Keep current fallbacks for transparency/Blend/cornea depth-after, ShaderGraph vertex offsets, conditional LOD/fade, material batching, MDI/GPU ICB and late compositing, since the exact colour/ID visibility path remains unproven. The new test is not an automatic all-scene GPU picking replacement.
4. Implement real Vulkan and GLES3 GPU actor source support with backend-owned ordering and safe resource retirement; generic RHI GPU Depth32Float readback remains optional and unsupported outside Metal. Verify native driver results independently.
5. Renderer3D shared source files overlap with batching/STRATA, Texture RHI and Prisma agents. Semantically reconcile on a dedicated integration worktree; do not overwrite parallel features or force merge to main. Human realism/MetaHuman-grade skin/hair/scan and artist validation are separate open project milestones.
6. At each future stage update both this dedicated handoff lineage and `docs/agent-handoffs/vivid.md` with exact code SHAs, actual executed tests, flags and next concrete action. Read `AGENTS.md`, `engine/character/vivid/AGENTS.md`, `docs/vivid.md` first.
