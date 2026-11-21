# VIVID Stage 7BA — bounded multi-actor GPU Mask scene provenance and depth tests

Date: 2026-11-20
Repository: `chromealex/AIVERSE`
Branch: `feature/vivid-digital-humans`
Pre-stage HEAD: `38c8e63854b9bd7e5f537050cecc2684170b633b`

No changes or merges to `main`, MotionWeave, Texture RHI, Prisma, batching/STRATA or other agents' branches. Shared Renderer3D edits live only in this feature branch and require semantic integration with concurrent renderer agents.

## Precisely implemented

Stage 7AZ had one selected GPU human plus **one** independent, GPU-deformed sampled Mask colour/depth occluder. Stage 7BA implements a **bounded set of at most four distinct GPU Mask blockers**, in the same RHI/GPU-ID depth pass, with Renderer3D-owned frame-local colour submission evidence and a transactional current-frame collector.

### Renderer3D accepted source tracking (5 Models total)

`Renderer3D::trackModelSubmissionsMany(std::span<const Model* const>)` is an opt-in frame-local API accepting one selected source Model plus up to four additional Models. Unique live pointers are borrowed only during the frame; null, duplicate, oversize and late registration sets fail closed instead of silently missing occluders. The prior `trackModelSubmissions(model)` and `trackModelSubmissions(model,occluder)` APIs are preserved by a backwards-compatible wrapper. All pointers and accepted-source records clear at `beginScene`. Renderer3D captures only encoded main-colour draws for these Models and `modelSubmissionProvenance(model)` returns exactly that Model's accepted primitive source/transform/LOD/GPU override/pipeline, never records from another Model. Tracking is bounded; it does **not** create an all-scene registry.

The renderer additionally freezes sampled `AlphaMode::Mask` material provenance for each selected primitive and accepted draw: actual model base texture handle, base alpha factor, cutoff, UV offset/scale/rotation and `vertexColors` setting. The staged/encoded source gate compares these byte-for-byte; the independent ID source must use the same texture and alpha/UV inputs. Material hot-reload or changed texture between normal scene submission and GPU ID no longer passes an undocumented assumption.

### Renderer-derived GPU scene collector

New header `engine/character/vivid/SculptGpuRendererMaskSources.h` provides `SculptGpuMaskSceneCandidate { model, mesh, nodeWorld }`, `prepareSculptGpuRendererMaskOccluder(...)` and `collectSculptGpuRendererMaskOccluders(renderer,candidates,frame,selectedGpuBuffer,output)`. A host gives the known current-scene candidate models and current MotionWeave pose, but never a supposed 'visible GPU buffer' of its choosing. The collector reads the real Renderer3D staged/accepted main-colour ModelViews, checks original primitive indices, direct pipeline, frame, LOD/skin/world, actual 60B borrowed GPU vertexOverride, and exact material mask. Only homogeneous standard sampled masks with a living texture and valid alpha/UV are eligible. Any candidate with stale/unsupported/duplicate model or duplicate 60B GPU buffer rejects the **entire** transaction, preserving the caller's previous published output. Unknown visible scene objects must not simply be skipped: retain CPU BVH as the authoring truth when not representable.

### GPU ID / native Metal

`SculptGpuIdPass::record` adds trailing optional
`std::span<const SculptGpuVertexSource> gpuMaskOccluders={}`, retaining its old single pointer `gpuMaskOccluder=nullptr` and all earlier call signatures. Legacy pointer and span together may contain at most four independent GPU sampled Mask actors; they cannot alias the selected stream or each other. One bounded, finite, atomic input validator `sculptGpuMaskSourceSetValid` is shared between pure regressions and ID submission. The total indexed/opaque/mask scene budget remains capped at 3,000,000 vertices.

For every blocker, the ID pass creates immutable GPU source-index/zero-ID, source-skin, native 3×4 joint rows, original UV/vertex alpha and 32-byte expanded vertex buffers. It then dispatches the native Metal mask compute for each source on the same ordered queue, and draws the result under its own sampled texture/cutoff and hardware depth. **Every blocked source triangle ID is forcibly zero**. The GPU source positions come from the matching current-frame 60-byte Renderer3D actor buffer, not the CPU reconstruction. All scratch is retained until independent GPU ID/depth readback completes, or until an explicit owner-thread GPU idle barrier proves safe retirement on failure.

The normal Creator continues to preserve CPU BVH and existing conservative opaque/hair/floor occlusion. This stage exports the host-side collector API but does **not** fabricate automatic all-scene discovery where the current Creator has no GPU-morphed additional Model candidate. Clients must register their known scene Models before `drawModel`, then collect after actual main-colour render. CPU-fed zero-ID opaque blockers and unsupported custom-material fail-closed paths remain intact; generic independently GPU-deformed **opaque** blockers are not yet promoted to exact GPU expansion.

### Native fixture expansion

The existing `vivid_metal_sculpt_id` Objective-C++ fixture source now creates a third independent 60-byte GPU Mask vertex source at depth 0.15 behind a first 0.1 GPU Mask, but ahead of the selected source at 0.2. It tests one mask's hole + the second mask's opaque sampled texel (zero ID and true 0.15 depth), then both holes (selected triangle ID and true 0.2 depth). Each uses distinct GPU buffers, textures and hardware alpha/depth (not CPU-position substitution).

The real macOS `aiverse_vivid_renderer_preview` now also has opt-in `AIVERSE_VIVID_MASK_PARITY=2`: one selected VIVID GPU human plus **two distinct** independently GPU-compute-written alpha Mask models, all rendered through normal Renderer3D main colour/depth in one frame. Each Mask uses the same original 15×uint32/60B `ModelVertex` format, a separate compute-written output and actual current-frame accepted source identity. Both sources are collected and fed to the independent GPU-ID/Depth32Float pass, which compares same-frame colour/ID and native depth during alpha-hole and solid-occlusion cases. The original `=1` one-mask preview, standard single-human preview and 12-actor crowd preview are unchanged. Resources are freed only behind the verified GPU idle barrier. `tools/vivid-validate.sh` adds a separate multi-mask GUI process with three screenshot existence checks, two real hardware pixel/depth assertions and `GPU mask sources=2` log requirement.

## Exact code commit SHAs and files

| Code SHA(s) | Paths | Changes |
|---|---|---|
| `af76a5c2344e424190884e4e8cdabe09053f52d7` | `engine/render/Renderer3D.h` | Fixed `kMaxTrackedSubmissionModels=5`, `trackModelSubmissionsMany`, legacy tracking wrapper retained. |
| `6fc61ae1408dda54c4e4d689d80078d95bf262e5` | `engine/render/Renderer3D.cpp` | Clear all tracked Model pointers on beginScene. |
| `72564c3dd639cd46c9c720af46e4de9562bffad7`, `09c494a9a32df45c71fed816f10b7893813ef56d` | `engine/render/Renderer3DModels.cpp` | Per-model bounded accepted draw tracking and model-filtered provenance query. |
| `5489c31ff87a434ab75e786de54cde2d09037749`, `4e0412bc714148ecf6459f6099bc55123fab692c` | `engine/character/vivid/SculptGpuIdPass.h` | Multi-mask trailing `span`, per-mask GPU scratch/compute/depth, 4-mask cap and shared validator. |
| `acd2ab544f4704e69195dc53460ec28ffec30b5f`, `cd451c9ef0172b924c1c41e268700e9a826a0a11`, `6491e7ec8302d0c6adbde83e96fb53734706ada5` | `engine/character/vivid/SculptGpuRendererMaskSources.h` (new) | Renderer-owned bounded collector, shared cap and exact accepted Mask material matching. |
| `012ea9e30a65ef1ba987a08bc1c773336b214d65` | `engine/character/vivid/SculptGpuVertexSource.h` | Pure 4-mask source set cap, independent stream/validity contract. |
| `2392b1c8f569d351df4c8e2848bf6256e9e96d3b` | `engine/render/Renderer3D.h` | Frame-local original Mask material/texture/UV/alpha fields. |
| `a41d6bf0e169b21d2e641e089f93c68bda9351ce` | `engine/render/Renderer3DModels.cpp` | Freeze same Mask material state at staged and accepted colour draw points. |
| `b93012628cca546dcb226a62850f08e507ac355f` | `engine/character/vivid/SculptGpuSubmittedView.h` | Reject mismatched submitted Mask texture/UV/alpha state. |
| `f139cac5ba4db6bc42e38851a42fabc8db17c8ba`, `49ad63cd5f1bb8bcb56a0adcead14b9a03b3cf56`, `eb1626bd0bfeae59c5cb8b71855fb47b87bae54b`, `6064542ccc02a921967e4900de0e79ec4984dec2` | `tests/vivid/human_creator.cpp` | **UNEXECUTED** 4-limit, duplicate/invalid last source, stale or missing accepted draw and changed Mask cutoff, plus namespace fix. |
| `327920cea76751d405c01b3e8b57faf0e10ad872` | `tests/vivid/metal_sculpt_id.mm` | **UNEXECUTED** actual Metal third GPU stream, alpha holes/solid and 0.15 depth tests. |
| `5a7980efed7c0e4c73bbdc9b24d83d1106d1bc1c`, `25d4b4947ef2f00c6a5e6d9d9907f20514438f6d`, `8df27e5ba3d52039ce1b56b8f7442e678029fb50`, `df2b5de3573e3236a0394e0afe1b78ff8f29520d` | `tests/vivid/renderer_preview.cpp` | **UNEXECUTED** collector use, third GPU-written Mask/normal Renderer3D model, both sources sent to ID, safe retirement. |
| `b5bbd3b319f67758ee0bb61c9061a78f99c00c25` | `tools/vivid-validate.sh` | New native GUI `AIVERSE_VIVID_MASK_PARITY=2` and exact three screenshots + two pixel/depth log gates. |
| `698d699286f183eb0e871ac27f182070c5c742c1` | `docs/vivid.md` | Stage 7BA architecture, scope and unfinished paths. |
| `cc49143c654d63d68f3355f6769b4c05f3794ad0` | `tools/vivid-creator/README.md` | Creator-facing opt-in API contract, run modes and limitations. |

The existing 60-byte `ModelVertex` ABI, HumanDNA v1 JSON, 52 ARKit face channel ordinal, MotionWeave ECS scheduling and gameplay authority have not changed. The RHI Depth32Float API added during 7AY remains optional, and Metal-only actual actor source expansion stays optional; Vulkan/GLES/GL33/WebGL2 still fail closed without a proven native source.

## Actually performed validation and exact gaps

**Performed:** GitHub connector reads, updates and commit responses; 28 source-only contract inspections matched: tracked-model bounds, same-frame provenance, duplicate/late rejection, independent GPU source cap and texture/alpha material matching, per-object 32B Metal compute and zero IDs, combined 3M-vertex budget, fail-closed collector transaction, async buffer ownership, headless fixture presence, new full Renderer3D three-model preview and validation runner. These are code inspections, **NOT a C++ compiler or GPU run**.

**NOT RUN:** CMake configure, native C++20/ObjC++ compilation, Metal shader creation/dispatch, native `vivid_human_creator` and `vivid_metal_sculpt_id` CTests, single/one-mask/two-mask/crowd GUI Metal previews, actual colour/ID/native Depth32Float pixels, `bash tools/vivid-validate.sh --full`, Vulkan/GLES device tests, VRAM/performance measurements or GitHub CI on the resulting HEAD. There are **no real GPU PASS values** for Stage 7BA. This development environment had GitHub connector source access but no mounted local macOS hardware build.

### Deferred native acceptance commands

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
Check actual compiler logs, exact tested branch SHA, native shader errors, same-frame alpha-hole/solid depth comparisons and independent GPU source retirement before declaring acceptance. Source presence and log-grep conditions are not evidence they passed.

## Next Stage 7BB and unresolved cases

1. **First priority: native Mac compile and targeted GPU QA.** Fix any ObjC++/MSL, C++ template/span, source ABI or readback errors. Validate the 3-model masked-depth screenshot, stage 1/2 mask alpha with nearer GPU occluder, depth tolerance, actual pixel capture timing and retirement. Avoid claims of green hardware before logs.
2. Extend Renderer3D-side *known-scene candidate* provider in Creator/editor where the host exposes actual additional animated Models. The collector exists, but Creator's current normal scene only has head/hair/floor with no independently GPU-deformed sampled Mask actor; do not invent extra scene objects to force an unnecessary GPU authoring pass. A genuinely unknown visible occluder invalidates exact GPU picking; CPU BVH remains authoritative.
3. Add non-mask **GPU opaque** occluders with exact renderer-source 60B GPU positions; keep conservative CPU opaque geometry when source parity is not demonstrable. Multiple masks are implemented but heterogeneous GPU opaque/custom/Blend source parity is not.
4. Handle renderer conditional LOD/fade, material batches, ShaderGraph vertex displacement, translucent cornea and late postfx/depth-after, GPU ICB/MDI, and independently moving groom where supported. Maintain strict fail-closed per-primitive and per-material checks.
5. Implement true GPU-source expansion/readback for Vulkan and GLES3 with backend-owned frame ordering and resource retirement; current Vulkan/GLES ID may only be CPU-fed. Test each native driver separately.
6. Semantic merge safety: Stage 7BA touches shared `Renderer3D.h/.cpp/Renderer3DModels.cpp` and bounded VIVID source files; other agents develop Texture RHI, batching and Prisma. Reconcile in isolated integration branch/worktree, no blind overwrite or direct main merge.
7. Keep mandatory per-stage distinct handoff commit (this file + append to `docs/agent-handoffs/vivid.md`) with exact code SHAs and actual test status, and follow `AGENTS.md`, `engine/character/vivid/AGENTS.md` on every stage.

Next agent: start from the verified `feature/vivid-digital-humans` branch HEAD, read the above handoff and the canonical one, run native acceptance on a real Metal Mac host before claiming feature-complete rendering.
