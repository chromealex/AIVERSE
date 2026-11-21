# VIVID Stage 7AS — Renderer3D-owned Model view provenance for experimental GPU ID picking

**Date:** 2026-11-20  
**Repository:** chromealex/AIVERSE  
**Branch:** `feature/vivid-digital-humans`  
**Other branches:** `main` and `feature/texture-rhi-unification` were NOT changed or merged.  
**Actual native tests on this source:** NOT RUN.

## Goal and safety decision

Stage 7AR established cross-platform source paths for Vulkan/Metal/native GLES3 ID/depth readback, but the ID pass still draws a CPU-reconstructed morph+skin triangle stream; it neither consumes the actual Renderer3D GPU actor 60B `vertexOverride` nor its actual source LOD/mat-batch selection. The previous VIVID implementation derived occluding meshes separately from node graph and **preemptively disabled any asset with authored LODs**, even if the renderer really chose source LOD0.

Stage 7AS introduces an opt-in **read-only, current-scene query in the existing Renderer3D** to get exact render-source mesh and primitive decisions without implementing a parallel view renderer or guessing from `Model::lods`. This is intentional **pre-submit provenance**, NOT final raster visibility and NOT proof of GPU-deformed vertex-buffer equality. The CPU BVH selection stays authoritative.

## Changes, files and exact commits

| Commit | File | Behavior |
|---|---|---|
| `dafe7c78b8d131b612dee187eca370726a6394c4` | `engine/render/Renderer3D.h` | Add public `ModelViewPrimitive`, `ModelViewProvenance`, `modelViewProvenance(const Model&)`, and render ModelItem source node/mesh/primitive/override metadata |
| `c958d57d30a14f19e3c21d093848b79db1f64f33` | `engine/render/Renderer3DModels.cpp` | Stamp real `drawModel()` node/mesh/primitive/60B override decisions in ModelItems and implement source-view extraction filtered to the main colour view, with explicit LOD/fade/batch/shader/baked unsupported flags |
| `49805fd86c8dd07a7e250d543f92230ddb597e5f` | `engine/character/vivid/SculptGpuRendererView.h` | Match a renderer-selected snapshot to picked original source mesh, frame, selected node, primitive index ranges and visible other meshes; fail closed on mismatches rather than confirm wrong LOD |
| `bbfbea7b4c921d11372c288a6ce1314c3d7d8b05` | `tools/vivid-creator/main.cpp` | Stage floor/head/hair `Renderer3D::drawModel` before optional ID pass, call `modelViewProvenance`, reject unsupported source geometry, only submit zero-ID occluders for model meshes that Renderer3D actually made colour candidates |
| `93ad294e0440289b59d9443c3611108c4f04b8b1`, `b5014ab05d0f75abd7eaed8b745b94c7857b6cbb`, `8387c998b33afd15f8fa7b65518d1cd795e97de1` | Renderer3D source model / VIVID view validator | Distinguish selected **alternate** authored LOD mesh from valid base source LOD0. Selected LOD0 can be diagnosed even when the model contains a chain; alternate LOD, fade and conditional GPU indirect LOD cannot |
| `0a2efa6a10dee5dcbafa2a57892ba1c1767eed00` | Creator | Remove static blanket authored-LOD rejection **only when** real renderer provenance will be checked before the draw. No ungated GPU ID pass |
| `771d102bedb39beeabddff2f04210fac4878b2fb`, `1cb75bcb981c903fb6c5dc77d664bd5380ae8526` | `tests/vivid/human_creator.cpp` | Synthetic exact frame/node/source range test, GPU override metadata, base LOD0 accepted, alternate/fade/batch/offset rejected, invalid source owner failure |
| `ab12efff1d5a7c5661b0a6ccb5e6856611c32d95` | `tests/vivid/renderer_preview.cpp` | Real Metal single-character scene now checks the current Renderer3D view contains source prims and the actual `GpuActorStream` vertexOverride and count, without claiming pixel parity |
| `7126baa7b9bbb28c12a21d2a3be1b4e300bbca21`, `fcea49f3ab6fa0e9f02c3cfa4f78b64e19789a90`, `b6910e731f627e7e09ca84a0dd7532a38fab5d54`, `9bf2c15a758f8490b0186f05d2b2a6fff1acef79` | GPU request protocol / Creator / tests | `SculptGpuPickProtocol::pendingIs()` prevents an obsolete async callback (including empty error replies) from changing the current CPU source vertex or even the latest status; report whether Renderer3D actually used a GPU vertex override, which still lacks exact ID-pass stream parity |
| `da60bc6c0331da415204e03bcc4a228169fe79f9`, `40e496b59be95f8dd476abe9fe7ebf06643de0cc`, `679113eac5022e43fccb18d635b5fdf25ebd6460` | Renderer3D | Add scene-scoped, monotonically incrementing `drawModel` invocation ID to each selected primitive, resetting on `beginScene()`. Different instances of one Model may share node/mesh IDs and must not be collapsed |
| `323f8d80880e6c09d6d4f75ddf4a1415949fde6a`, `a67a44707d5ed26c68dc3e215b68f544153d63ee`, `a7df20bea15e4fe52b6864dcf588cfe192a1b26a` | VIVID validation / regression | Reject one source mesh submitted by multiple distinct Renderer3D draw invocations or node owners; preserve correct fixture isolation |
| `373d84818347fb14516ec48e796b8ed635d40637`, `a1f6a0cb0fe088ca27b2b331476f50f6976cde4e` | Creator README / `docs/vivid.md` | Public API usage, provenance limits, stepwise validation and explicit outstanding GPU stream/RHI parity work |

## Architectural API / usage contract

```cpp
renderer.beginScene(camera,w,h);
renderer.drawModel(draw); // attaches existing GPU vertexOverride, if ready
auto view=renderer.modelViewProvenance(*draw.model);
auto check=vivid::validateSculptGpuRendererView(
    view,*draw.model,selectedSourceMesh,device.frameSerial());
if(check.canRenderDiagnostic) {
    // Submit selected ID triangles and zero-ID depth from
    // check.otherVisibleMeshes. This is STILL CPU-generated geometry.
}
renderer.render(pass);
```

- `ModelViewProvenance` is derived **from Renderer3D's own ModelItems**, not a second culling/LOD solution. Each primitive captures model pointer, selected node/mesh/primitive IDs, original firstIndex/indexCount, source draw invocation ID, instance transform, GPU override handle/count and fail-closed flags for batched/faded/conditional LOD, alternative LOD, baked and ShaderGraph vertex offsets.
- Snapshot is valid only in the current renderer scene; model/raw GPU handle pointers are not retained across asset republish, `beginScene`, resource retirement or async GPU ID callbacks. VIVID uses the snapshot immediately, then its old model/frame/viewport token guards later pixel readback.
- An authored LOD *asset* is not categorically rejected: when Renderer3D actually selects the source LOD0, without fades/batches, all original source primitive index ranges are retained and can be compared. An actual selected alternative LOD or conditional GPU LOD is rejected rather than falsely confirmed.
- The renderer's `ModelDraw::vertexOverride` is recorded **for diagnostic disclosure only**. A VIVID GPU match is reported as a CPU-snapshot ID confirmation with the explicit note that Renderer3D's **real GPU vertexOverride stream remains unverified**. The selected original vertex from CPU BVH never changes on any GPU callback.
- A single `Model` can have multiple world-space instances with the same internal node and mesh IDs: `drawInvocation` differentiates them. Current CPU-pick workspace is one world-space surface, so ambiguous per-mesh instances are rejected rather than silently picking the first.
- Colour candidates are still **pre-render**: this is not a guarantee that a pipeline compiled, draw was submitted after GPU culling, or fragment survived alpha/transparency/depth/postfx. The goal is eliminating duplicate source mesh/LOD inference and making remaining GPU parity explicitly visible.

## Verification classification and deferred acceptance

**Actually executed:** GitHub connector file reads/updates, commits, code-level audit.  
**NOT RUN:** CMake configure, native C++ build, `vivid_human_creator` CTest, renderer preview, Metal/Vulkan/GLES GPU shader compilation, pixel ID/depth GPU readback, large-model benchmarks, full engine CTests. Do not mark code "tested" or "production-ready".

When functionality is closed and the user requests the consolidated native run:

```sh
cmake -S . -B build/vivid-onepass -DAIVERSE_BUILD_TESTS=ON
cmake --build build/vivid-onepass --target \
  aiverse_vivid_human_creator_tests \
  aiverse_vivid_renderer_preview \
  aiverse_vivid_metal_sculpt_id_tests --parallel 4
ctest --test-dir build/vivid-onepass --output-on-failure \
  -R '^(vivid_human_creator|vivid_metal_sculpt_id)$'
bash tools/vivid-validate.sh --full
```

The Vulkan and native GLES3 hardware tests from Stage 7AR require their **separate** Linux/Android-like device contexts and cannot be certified from a macOS-only acceptance run. Log exact SHA and actual pixel outcomes; fix failures before merging to main.

## Current functional gaps / next Stage 7AT

1. **True GPU stream ID rendering**: borrow Renderer3D's actual GPU-deformed 60B `ModelVertex` stream, original index and joint palette with immutable frame/fence ownership, rather than CPU-expanded world positions. Do not duplicate actor stream/RHI ownership. Geometry, skin pose and per-primitive IDs must match one emitted GPU draw.
2. **Final visible colour submission**: downstream GPU culling/ICB/MDI, material shader compilation, alpha holes, Blend cornea, geometry overlays and final scene depth cannot be inferred solely from pre-render ModelItems. Add a Renderer3D-owned submitted-command provenance or safe view-ID attachment where supported.
3. **Texture RHI semantic integration**: `Renderer3D.h/.cpp/Models.cpp` now have new provenance fields, overlapping the texture/render agent's work. Merge only in a separate integration branch and reconcile shader/material lifetime, compressed texture residency/LOD, GPU retirement. Never overwrite peer changes.
4. **Cross-backend hardware verification**: actually compile/run `vivid_vulkan_sculpt_id`, `vivid_gles_sculpt_id` and `vivid_metal_sculpt_id`, plus the new source fixtures. WebGL2 and Windows loader remain explicit CPU fallback until tested RHI pixel delivery.
5. The wider MetaHuman-level content/authoring gaps (topology-aware sculpt, reliable scan/eye/hair realism) remain separate features, documented in earlier VIVID functional audit. Do not state the product is a complete MetaHuman replacement.

Continue one separate source+handoff commit per subsequent stage; respect the user's deferred consolidated test policy.
