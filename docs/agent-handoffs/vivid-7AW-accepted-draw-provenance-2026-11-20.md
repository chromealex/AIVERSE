# VIVID Stage 7AW — accepted Renderer3D colour-draw provenance before GPU-ID corroboration

Date: 2026-11-20
Repository: `chromealex/AIVERSE`
Working branch: `feature/vivid-digital-humans`
Base before stage: `7f0a0026c6b8da0ea5029d6fb155886011092ce0`

No merge or write to `main`, Texture RHI or parallel MotionWeave/renderer branches. Cross-cutting renderer changes were made on VIVID's feature branch only. These must be reconciled semantically on an isolated integration worktree with other renderer owners before main.

## Problem and bounded implementation

Earlier VIVID 7AS–7AV validated renderer-owned **staged ModelItems**, selected original mesh/LOD, the actual actor 60B GPU vertex override and native Metal skin/Mask arithmetic. But the renderer could still reject, defer or alter the eventual main-colour command because of asynchronous pipeline compilation, GPU ICB/MDI, geometry culling and other late decisions. VIVID could therefore say a GPU ID corroborated a source never actually submitted by the normal colour renderer.

Stage 7AW adds **opt-in Renderer3D accepted-command provenance**, recorded from the actual direct indexed submission, successful CPU MDI/Metal GPU-indirect and GPU ICB submission branches. The record is published only for one host-specified source Model during the current scene, containing the actual encoded main-colour phase, compiled pipeline handle, source node/mesh/primitive/invocation, original index range, borrowed GPU vertex override/count, model matrix, material transparency and skinning/LOD flags. No persistent scene database, duplicated VIVID renderer or gameplay ECS mutation.

The new `SculptGpuSubmittedView.h` compares staged `modelViewProvenance` with accepted `modelSubmissionProvenance` one-to-one in the **same frame**, requiring a single directly encoded full original-primitive main opaque draw with an actually valid pipeline. Missing or deferred pipeline, incomplete snapshot, late opt-in, oversized tracked scene, changed world/index/vertex override, material batch, active GPU LOD, instancing, ICB/MDI and non-main phase fail closed. `gfx::Device::drawIndexed` is void; the record means the **native RHI indexed draw was encoded**, not that GPU hardware executed and survived depth/fragment/postprocessing. Do not call it a final framebuffer oracle.

Creator now collects the accepted snapshot **after** `renderer.render(screenPass)`, not before rendering. The renderer normally leaves the main pass open for UI, so Creator temporarily ends the screen pass for an eligible GPU diagnostic, records the existing optional compute + RGBA8 ID/depth pass, then resumes screenPass without clearing colour or depth and restores the full-screen scissor and viewport. Failed preflight never touches the main pass. CPU BVH original-vertex selection remains authoritative, regardless of GPU ID callback result. Only the selected human Model opts in; others do not incur tracking.

## Exact code and test changes

| SHA | Path | Purpose |
|---|---|---|
| `97f5de199fc51e991ae3e6875496f0e9aa63bf55`, `5c95606810cf86b1f3a0a5d57926739386515731`, `d0486d79abc4976ce9cbe5ef5f198ee6be55a115` | `engine/render/Renderer3D.h` | Public frame-local `ModelSubmissionKind`, `ModelSubmittedPrimitive`, `ModelSubmissionProvenance`, `trackModelSubmissions` and `modelSubmissionProvenance`; captures only one model and rejects late opt-in or clipped tracking. Distinguishes single direct from direct-instanced draws. |
| `7e5564d3f4b5245abd17b8b1a908f561f9969a0b`, `3e2e2553c665a8fb42da078faaf764aafdfd97e7` | `engine/render/Renderer3DModels.cpp` | Implements immutable renderer-owned submitted source snapshots on direct `drawIndexed`, accepted `drawIndexedIndirect`, accepted `drawIndexedGpuIndirect`, and actually executed `executeGpuEncodedIndexedIcb`; excludes preflight-only pipeline warm. Direct instancing is explicitly recorded as a separate unsupported kind. |
| `3810b1be902a224369c71368bd71b1ca768bee66` | `engine/render/Renderer3D.cpp` | Resets the selected model, submitted commands and main-colour complete marker at `beginScene`; stamps the accepted main-camera colour pass only after `renderModels(1)` returns. |
| `e12c1f0a1c980a0250cc86980e49c83c1c510915` | `engine/character/vivid/SculptGpuSubmittedView.h` | Portable strict source/frame/pipeline/direct-index comparison; explanatory failure codes, no GPU ownership. |
| `d8d0679a474b7056742a2054fbd565d95480978c`, `d6da6b952bdbf41bb0ce910fada667dfd49ec63b` | `tools/vivid-creator/main.cpp` | Post-render accepted command gate, safe resume of non-clearing main UI pass after ID pass and complete rejection reason. |
| `0ffdb27242722294731fe3951cc0ad2b49f4a3fa` | `tests/vivid/human_creator.cpp` | **UNEXECUTED** synthetic fixture for actual vs staged source, missing main pass/pipeline, wrong frame, changed indexed range/world, ICB rejection, truncated entries and no true submit. |
| `c21c2758eec9f3a8e57a428d9671dcd073f646d5`, `11502d6ef9e8de2fe3329f653ff7f3a3c00f1214`, `db6989bc77181c50e6658af631771e2e3c500896` | `tests/vivid/renderer_preview.cpp` | **UNEXECUTED** real Metal model-preview fixture: single actor opts into tracking, queries accepted main direct GPU source and verifies one-to-one match with staged view once model pipelines are warmed. Other crowd previews do not opt in. |
| `948a6b8fac5c9b83d40335fd3eaabcf166e39272` | `docs/vivid.md` | Stage 7AW architecture, status and unresolved pixel/hardware parity. |
| `77bb6ae0ef28af6c95641febc51dd837949fc287` | `tools/vivid-creator/README.md` | Public Creator integration and manual same-frame pass-resume constraints. |

Public `SculptGpuIdPass::record` signature, CPU BVH authoring, 60-byte `ModelVertex`, 52 ARKit channels, HumanDNA JSON, MotionWeave graph/body scheduler and genre-independent ECS rules remain unchanged. The newly exposed Renderer3D source-query API is opt-in; it does not write to `main`.

## Actual verification and limitations

**Performed:** GitHub connector file reads/updates/commits; a static readback review of the real Renderer3D direct/MDI/ICB code paths and a 16-condition source-contract inspection confirming correct per-scene reset, accepted vs preflight command boundary, frame+source identity, fail-closed direct gating, Creator's post-render ordering/main-pass resume, and newly added fixture presence. These are source **inspections**, not native compilation or hardware verification.

**NOT RUN:** CMake configure, native C++/ObjC++ compile, Metal/MSL shader compilation, CTest (`vivid_human_creator`, `vivid_metal_sculpt_id`, renderer preview), actual colour/ID/depth framebuffer readbacks, GPU command completion fences, Vulkan/GLES driver tests, `bash tools/vivid-validate.sh --full`, full AIVERSE regression, performance/VRAM measurement or screenshots. No native PASS result exists for 7AW.

**Explicitly incomplete:** A submitted opaque draw does not prove that the final GPU pixel passed depth test, sampled the same alpha texture or survived post-processing. Other scene occluders still use independently built CPU/mask depth geometry, not renderer-owned final scene-depth. The present stage therefore establishes **encoded draw identity only**, not hardware final colour ↔ ID pixel identity. Does not prove support for custom ShaderGraph vertex offsets, cornea/depth-after blending, GPU ICB/MDI, source alternative LOD, dynamically deformed other occluders, Metal fast-math bit equality or Vulkan/GLES3 true actor stream access.

## Deferred native Mac acceptance

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

### Next Stage 7AX

1. Add a real Metal **colour+depth ↔ GPU triangle-ID pixel comparison** using Renderer3D's own opaque render target and one genuinely overlapping independently moving Mask occluder with actual GPU source. Use appropriate async readbacks and completion tokens; do not substitute only source-string comparisons or CPU staging for GPU pixels.
2. Test the actual Creator pass-suspend/restore lifecycle under normal UI capture and after a GPU ID error or missing pipeline. Verify the 1px scissor cannot clip later ImGui or screen screenshots and restored depth is intact.
3. Resolve native CMake/Metal compiler and shader errors observed during the user-requested consolidated acceptance. Do not claim green until hardware logs exist.
4. After hardware colour/depth parity, expand true GPU morph-source vertex fetching to Vulkan with explicit gfx ownership and GLES3 where backend facilities permit; keep browser/Windows CPU fallback. Support ShaderGraph vertex displacement, cornea/depth-after and other GPU-deformed occluders only with renderer-owned shared draw/depth semantics.
5. Coordinate this branch's `Renderer3D.h/.cpp/Renderer3DModels.cpp` edits with ongoing Texture RHI/render/batching agents on a dedicated semantic merge worktree, never overwriting their changes or merging straight to main.

Read `AGENTS.md`, `engine/character/vivid/AGENTS.md`, `docs/vivid.md`, and `docs/agent-handoffs/vivid.md` before the next stage. Make one bounded implementation and commit a separate next-stage handoff with actual executed tests.
