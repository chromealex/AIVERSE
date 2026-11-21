# PRISMA Stage 32 — SpriteBatch water reactive, live RHI metadata and GPU retirement

Date: 2026-11-19.
Branch: `feature/prisma-postfx`.
Draft PR: https://github.com/chromealex/AIVERSE/pull/26.
Previous stage: `docs/handoffs/prisma-stage-31.md`.
Main was NOT changed and no branches from other agents were imported.

## Implementation history (exact commits and affected files)

1. `07e1c8c136b615ab8d3ce4a3a340a2e7b9d7015f` — **new** `engine/render/PrismaMotionTextureInput.h`: non-owning `MotionTextureInput` (borrowed texture handle, optional declared dimensions and format, no GPU readback).
2. `5581aae9eba044edff7bd70040c503ab7726cce6` — `engine/render/Renderer3D.h`: typed `PostProcess` overloads accepting `MotionTextureInput`, with backwards-compatible fallback to existing bare-handle virtual methods.
3. `e7501e2a9a910bd8a1515a9eb0fb61ad97b8569a` — `engine/render/Prisma.h`: per-camera state stores typed borrowed velocity and reactive inputs, adds typed native setters and PostProcess overrides.
4. `155cb579e6a35ccbc9bee900210aec4d97452afc` — `engine/render/Prisma.cpp`: declared-parameter validation and conservative fallback for mismatched velocity/reactive inputs (object motion falls back to camera reprojection; reactive mask uses black).
5. `2bb50846edee2314c39126555e68da20ef00e3f1` — `engine/render/Renderer3D.cpp`: the renderer now submits typed full-resolution RGBA16F object velocity and RGBA8 reactive mask inputs.
6. `f101bdf52e26f8799d8ba21120d0d0e096648e75` — **new** `engine/render/SpriteReactiveShaders.h`: GLSL/MSL SpriteWater reactive shader samples base water-sheet alpha and vertex alpha and rejects occluded fragments against read-only scene depth.
7. `4bf837ba8a2a6178bf7e4be0ece076a5557685bb` — `engine/render/SpriteBatch.h`: optional current-frame water draw capture, `SpriteBatch::registerWaterReactive(Renderer3D&)`, `hasWaterReactive()`, `renderWaterReactive(device,sceneDepth,w,h,mesh,waterTexture,viewProj)`, and owned shader/pipeline handles.
8. `dcf9471eeec66ad93b34e4c24f3c1f1343b3013f` — `engine/render/SpriteBatch.cpp`: record real `drawWater` mesh/textures including fallback to lit decal; register per-draw callbacks with generic Renderer3D; render source mesh into already-open reactive pass with Screen blending and depth/alpha sampling. Clean up dedicated GPU resources at shutdown.
9. `59b7957c7b47d3d949c086e1e587bbfb4625667a` — `engine/render/Renderer3D.h`: generic per-view GPU-retirement APIs, opt-in backend last-submitted fence, completed-fence collector and unfenced target quarantine using Stage 18 queue.
10. `a06b77880927811163c0d1f196196baa445971d0` — `engine/render/Renderer3D.cpp`: move obsolete object-motion targets into the deferred retirement queue on viewport resize; if no valid fence is available, retain in quarantine until explicit confirmed GPU idle. `retireObjectMotionView`, `collectCompletedObjectMotion`, `drainObjectMotionAfterGpuIdle` implemented; Renderer3D shutdown drains old allocations under existing host GPU-idle teardown assumption.
11. `ef9049986fcc53e784d3f7063424421540390382` — `engine/gfx/Gfx.h`: `gfx::TextureInfo` and optional `Device::textureInfo(TextureHandle,TextureInfo&)`, CPU-only metadata lookup (false if unsupported).
12. `f5df83909b462c5c1b88f6a672efed27874f5d36` — `engine/gfx/gl/GfxGL.cpp`: live GL texture metadata lookup, plus const Pool::get.
13. `2d4173c22cd0fe8a9acda8efc384843bda27e2d9` — `engine/gfx/metal/GfxMetal.mm`: live Metal texture metadata lookup from MTLTexture dimensions and existing PixelFormat, plus const Pool::get.
14. `756f9e4ae29fd9605c02ddc72eeda6b901fdad3e` — `engine/gfx/vulkan/VkDevice.h`: Vulkan texture metadata override and const Pool::get.
15. `e7a63e2305892415a6cf61c72cb4022550f8cfb3` — `engine/gfx/vulkan/VkResources.cpp`: Vulkan live texture width/height/format lookup.
16. `4906d889245a34094942984fdb8ca0bd50173f90` — `engine/render/Prisma.cpp`: compare actual RHI metadata to current viewport and expected formats before using motion inputs; fall back if mismatched or invalid.
17. `674de9737fbbc32e0dddace124f521ef7c009346` — `docs/prisma.md`: Stage 32 architecture, limits and native API documentation.
18. This Stage 32 handoff is committed separately here.

## API and intended usage

### Specialized SpriteBatch water

For a game that actually calls `SpriteBatch::drawWater`, also call `spriteBatch.registerWaterReactive(renderer)` **after** water drawing and **before** `Renderer3D::resolvePrisma()` on the same scene thread. It transfers captured draw references to per-scene callbacks; at resolve they replay the static mesh's own GPU buffers into the reactive mask using original water texture alpha, vertex tint alpha and source depth. The SpriteBatch owner must keep itself and borrowed GPU resources alive until resolve. `SpriteBatch::begin()` clears any previous pending water captures; registration consumes them.

**Scope caution:** no direct calls to `SpriteBatch::drawWater` were found in this repository branch. Thus the specialized adapter is available for native game/module users, but it is not currently exercised by existing call-sites. RTS water already flowing through `Renderer3D::drawMesh` is connected automatically by Stage 31. Neither water nor FX module is imported by PRISMA itself.

### Typed RHI input safety

Native callback producers should provide a `MotionTextureInput::declared(handle,width,height,format)`. For full resolution object velocity the format is **RGBA16F**, with signed RG normalized UV motion, B > 0.5 validity; for current-frame reactive coverage the format is **RGBA8**, R=coverage 0..1. Both are borrowed until synchronous resolve and MUST have same dimensions as scene viewport.

`PostProcess` has typed acceptance virtual overloads and keeps the old handle-only virtual API for ABI/source compatibility. Prisma validates declared dimensions and format; if `Device::textureInfo()` returns actual RHI descriptor, Prisma additionally checks the **real live texture allocation** on GL, Metal or Vulkan. A mismatch skips object-velocity override and uses camera motion, or uses no reactive mask rather than sampling the invalid resource. Backends not supporting textureInfo rely on provider declarations; legacy bare-handle inputs without declarations remain trusted when no actual descriptor is returned.

### GPU texture retirement

The renderer now uses `PrismaGpuRetirementQueue<TextureHandle>` for object-motion targets. A host with backend-issued GPU submission fence may call `setObjectMotionSubmittedFence(lastSubmittedFence)` to associate subsequent resizes with its last submission referencing those targets. Completed-fence values must only be passed to `collectCompletedObjectMotion(completedGpuFence)` **after the GPU backend has signalled completion**. One view may be explicitly retired with `retireObjectMotionView(id,submittedFence)`. A zero/unknown fence causes quarantine instead of early destruction; the owner calls `drainObjectMotionAfterGpuIdle()` after a *real GPU idle* synchronization. Existing `releaseObjectMotionViewAfterGpuIdle` is a synchronous separate API.

**This stage has NOT implemented GL/Metal/Vulkan fence production or polling.** This is a safe host-facing lifetime contract and quarantine policy, not proof that all old PRISMA caches or every resource in the renderer use safe fences. If the host never sets GPU tokens and resizes repeatedly, quarantined texture memory can grow until explicit idle. CPU `frameSerial()` is not a completed-fence signal. The existing shutdown method must be called at the host's safe device teardown point.

### Default behavior / compatibility

The full motion blur and reactive system continues to require `AIVERSE_PRISMA_MOTION_BLUR=1` and appropriate HDR/quality settings. By default, neither extra pass is enabled. No JSON migration, changes to FPS/RTS ECS gameplay, or reownership of Terra/Water/SpectraFX. User-authored PostFX config and editor/MCP names remain unchanged.

## Verification actually performed

**Performed:** source retrieval and inspection of the current branch, search for specialized SpriteBatch::drawWater call-sites, writes to GitHub connector, readback confirming all new method references and declarations in 13 changed source files. No code was executed.

**NOT RUN** per user request: CMake configure, C++ compile, CTest, Metal/GL/GLES/Vulkan shader compilers, game/editor builds, GPU tests, screenshots, framebuffer comparisons, CI, benchmarks or memory diagnostics. No claims of passing tests or GPU-ready parity.

## Technical limitations and next Stage 33

1. Implement backend-native completed GPU submission fence / timeline across Metal command-buffer completion handlers, GL sync objects and Vulkan queue fences, then call Stage 32 `collectCompletedObjectMotion` automatically on the graphics owner thread. Do not substitute CPU frame counts.
2. Apply the same fence-aware lifetime policy to the remaining `Prisma::Sized` targets, `PrismaRhiExecutor` pooled targets, exposure history and Sprite/FX resources; currently only Renderer3D object-motion resize/view textures have this new queue/quarantine behavior.
3. If actual native modules use specialized `SpriteBatch::drawWater`, wire `registerWaterReactive` at their real draw/resolve location; no current repo call-site was found. For unusual water refraction/foam or custom ShaderGraph vertex displacement, make producer-owned reactive callbacks that replay true material coverage.
4. Consider a bounded quarantine budget with explicit *GPU-idle* flushing rather than unbounded growth during frequent viewport resizes. Preserve screenshot accuracy and do not silently release in-flight resources.
5. Extend metadata validation to additional borrowed screen color/history textures if needed. Vulkan/GL/Metal RHI source changes need a compiler and runtime verification when tests are permitted.
6. When the user authorizes tests, run targeted CMake builds, CTest, shader compilation, real masked water/particle scenes, Scene/Game view resize, mismatched input rejection, GPU-completed retirement and resource leak checks, then record true PASS/FAIL rather than asserting success.
7. After Stage 33 implementation, write a separate `docs/handoffs/prisma-stage-33.md` including exact changed paths, SHAs, real verification state, limitations and concrete next action. Do not merge Stage 32/33 to main until explicitly directed.
