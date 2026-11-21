# AIVERSE VIVID × Texture RHI — integration checkpoint

Date: 2026-11-20
VIVID branch: `feature/vivid-digital-humans`
Parallel texture branch: `feature/texture-rhi-unification`
Status: **source review and coordination only — NO cross-branch merge, native build, CTest or GPU run**

## Why these systems meet

VIVID turns imported glTF/FBX materials into Skin/Eye/Hair shaders in
`engine/character/vivid/HumanMaterials.cpp` via
`humanMaterialFromSource(...)`. It preserves the **model-local source image indices**
(base/normal/MR/AO/emissive etc.). It does not own the GPU image objects.
The render Model, shader graph, RHI texture lifetime and sampling belong to
`engine/render` and `engine/gfx`. VIVID Creator publishes new character
candidates with a GPU retirement barrier (`HumanRetirementQueue`).

The texture agent implemented substantial opt-in infrastructure, including
`engine/render/TextureAsset.h`, `engine/gfx/TextureStreaming.h`,
`engine/gfx/TextureResidentStreamer.h`,
`engine/render/GpuMaterialResourceTable.h`,
`engine/render/ModelVirtualTextureSampling.h`,
`engine/render/VirtualTextureDeformedFeedback.h` and
`engine/render/ModelVirtualTextureFallback.h`. The latter headers currently
exist on the texture feature branch; **do not include them from VIVID yet**.

## Safe reuse plan — in dependency order

1. **Ordinary compressed imported maps** — use the existing canonical
   render::Model image/texture upload path. Both branches already have
   `Image::compressed` and KTX2-compatible source storage; do **not**
   create a parallel VIVID uploader or copy image buffers into HumanDNA.
   Texture branch's `uploadContainerTexture` adds explicit compressed and
   fallback format handling (BC/ETC2/ASTC and BC5 normal-Z policy) that should
   be reconciled in the shared renderer after integration.
   Validate sRGB for albedo versus linear normal/MR; preserve alpha-tested
   hair card coverage.
2. **Sampler, mip and streaming policy** — let the canonical RHI own
   TextureViews/Samplers and stream tickets. Mobile Low/Medium/High VIVID
   material graph tiers are not texture mip tiers. Project presentation
   policy should own both independently, with bounded upload bytes,
   staging, GPU residency and a safe coarse fallback.
3. **GPU material identity and retirement** — texture agent's
   `GpuMaterialResourceTable` is currently a **CPU staging/dedup table**;
   its comments explicitly say heterogeneous GPU descriptor/ICB binding is
   not implemented. Do NOT claim it alone implements per-command GPU
   material resource selection. A VIVID material replacement must invalidate
   stale graph/texture/atlas references and retain old texture resources
   until the renderer's in-flight submission completion.
   Reconcile with existing `GpuMaterialState.h` hashing and
   `HumanRetirementQueue` rather than introducing a second owner.
4. **Virtual texturing — opt-in only**. The texture agent's
   `configureModelVirtualFallback` rejects shared non-base maps,
   shader-graph materials, blended/transparent materials and some
   alpha-sensitive consumers. VIVID High/Medium skin uses ShaderGraph,
   groom cards use shader graph and possibly alpha masks, and cornea is
   transparent. **Do not enable VT globally on imported VIVID skins,
   corneas, or hair.** First introduce a capability/eligibility probe
   in the shared renderer that returns an explicit reason; only apply VT
   to an eligible, correctly-bound opaque base-colour material and after
   publishing its immutable archive.
5. **Skinned/morphed VT feedback** — texture branch includes
   `VirtualTextureDeformedFeedback` that uses the model's skin and
   morph deformation source. This is potentially useful for VIVID facial
   morphs, but requires shader/pose/LOD correspondence and exact GPU
   skin+MorphWeave/MotionWeave revision checks. Do not assume the dynamic
   head-local CPU groom cards satisfy that feedback path.

## Ownership boundaries

- HumanDNA: biological/appearance recipe **only**, no GPU handles, runtime
  mip residency, sampler IDs, texture offsets or streaming timestamps.
- VIVID: select material shading model and quality; preserve imported
  texture indices; author body/face/rig and supply model transforms/morphs.
- Shared Renderer3D and render::Model: texture/image binding,
  material/descriptor-state identity, shader permutations, draw dispatch,
  correct retirement fences.
- Shared gfx::Device / TextureStreamQueue: GPU allocation, storage
  ownership, uploads, mip residency, and per-backend capability rejection.
- The project/presentation configuration (future Stage 7AG): resolve
  material quality and texture-residency budgets **once** and hand a
  snapshot to VIVID. Not gameplay ECS/save authority.

## Merge / verification gates

- The branches are divergent and both modify `CMakeLists.txt`,
  `engine/render/Model.h`, `Model.cpp`, `ModelShaders.h`,
  `GpuMaterialState.h`, `Renderer3D.h/.cpp/.Models.cpp` and
  `engine/render/graph/ShaderGraph.cpp`. An automated wholesale branch
  merge risks losing VIVID's skin/eye/hair shaders and the texture agent's
  new streaming/VT resource ABI. Use a **dedicated integration branch**,
  preserve both changes with file-by-file semantic resolution and run
  existing renderer + VIVID + texture source compilation.
- Texture handoff on 2026-11-20 reports an earlier Mac Studio
  132/133 full suite with `aiverse_tests` SegFault; this is unresolved
  diagnostic evidence, **not proof that the texture changes caused it**.
  The texture Metal `gradient2d` fix was also committed with NO follow-up
  native compiler pass. Texture branch is not presently merge-certified.
- Test sources to retain and run **after** integration: VIVID
  `vivid_human_appearance`, import/creator and real Creator A/B captures
  (neutral/warm + optical Low/Medium/High), texture residency/format/mip
  CTests, GLES texture array test, Vulkan/SPIR-V compile and Metal shader/
  texture GPU oracles. Confirm exact code SHA and actual process exit codes.
  Do not confuse registering a test with passing it.
- Keep `main` and both feature branches unmodified by integration until
  the shared integration branch passes the required gates. Do not
  cherry-pick all 944 texture branch commits or 2191 VIVID-only commits
  blindly.

## Next actions

1. Complete VIVID Stage 7AG project presentation policy so it exports a
   stable render setting without importing texture-branch-only headers.
2. Ask the texture owner for its **accepted public API freeze** and
   compiler/GPU results. Re-fetch both branch heads before merge.
3. Create a separate VIVID + Texture integration worktree/branch, resolve
   shared renderer ABI, preserve lifecycle fences and establish actual
   texture eligibility/quality settings. Test a scanned head with albedo,
   BC5 normal, MR/AO, alpha hair cards, transparent cornea, layered skin.
4. Record exact integration SHA, tests (PASS/FAIL/NOT RUN), unresolved
   hardware limitations and a chronological handoff before any main merge.
