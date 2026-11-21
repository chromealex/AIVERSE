# PRISMA Stage 31 — real water and particle reactive producers, linear-depth motion blur

**Date:** 2026-11-19.
**Branch:** `feature/prisma-postfx`
**Draft PR:** https://github.com/chromealex/AIVERSE/pull/26
**Previous:** `docs/handoffs/prisma-stage-30.md`.
**Main:** unchanged. No merge from other agents' feature branches.

## Implemented (exact commit and file)

1. `9e2e668d7ea055b008889694fa2ac2680d8a7923` — `engine/render/Renderer3DReactiveShaders.h`: repair Stage 30 GLSL reactive fragment U0 uniform declaration to include `lod`, matching its own vertex shader and CPU dither uniforms. Source correction, **not** a GPU shader test.
2. `d17f848c4f3aec8446f5592cf5ce826f2a78826d` — `engine/render/Renderer3D.h`: per-view mesh reactive pipeline handle and `ObjectMotionStats::reactiveMeshDraws`.
3. `8c3e6b187b6578ebdbc4b51672b9520aba9a5026` — `engine/render/Renderer3D.cpp`: automatic reactive mask for actually queued translucent `DrawItem`/`MeshVertex` submissions, including RTS `TerrainRenderer` water sheets submitted via `r.drawMesh(water_,...,waterColor)`. Lazy dedicated MeshVertex GPU pipeline reuses Stage 29 sampled-scene-depth reactive shader, two-sided for water, Screen-union blending, coverage based on color alpha, and shutdown cleanup. This path is not tied to RTS gameplay.
4. `7cb079fb65b22fc2f1bef2d5e6805b2dd9e7424b` — new `engine/fx/FxReactiveShaders.h`: GLSL and Metal reactive shader (particle pos/UV/vertex alpha, actual alpha texture and sampled scene depth). No old effect code modified.
5. `8d0345c9345c74679ce8a35a2f27decda95145b0` — `engine/fx/FxInternal.h`: per-frame `ReactiveRun` capture (texture, base vertex, count, quad vs mesh) and shader/pipeline ownership within the FX renderer.
6. `394a465b23d3a58ece0da5df1514cd46308ce964` — `engine/fx/ParticleSystem.h`: `hasReactiveGeometry()` and `renderReactive(gfx::Device&,sceneDepth,width,height)` native API for calling **inside** an open mask pass.
7. `37d454b28a7ba35f09f5ba3bd2257e289260f066` — `engine/fx/FxRender.cpp`: capture non-opaque particle runs while the normal render submits its real billboards, trails, ribbons and triangle mesh particles; reuse the already-uploaded vertex buffer for reactive render with matching texture alpha, per-vertex alpha and scene-depth rejection. No re-simulation, sorting, expansion or extra vertex upload. Quad runs are emitted in chunks using the established UInt16 index buffer; triangle-mesh runs use nonindexed draw. Lazily create per-geometry-type Screen-blended reactive pipelines and do not open/end the caller's pass.
8. `6c87e0cde623a458a0c5cc9db72bed8e8336f7a9` — `engine/fx/ParticleSystem.cpp`: destroy newly owned reactive pipelines and shader on FX shutdown.
9. `d4da170c36067cd33897634722d899f7a777fa5a` — `apps/client/GameClient.cpp`: after `fx_.render` and before `r3d_.resolvePrisma`, register a per-scene reactive callback when non-opaque geometry was actually submitted. Source is `fx_.particles()`, no gameplay/ECS changes.
10. `dcac327c1b53bb966726660bb3a2a4ec18eb284e` — `apps/editor/Editor.cpp`: same particle callback registration for Scene and Game viewport previews after `gameFx_.render`.
11. `33b1c8036c8442c5149799e506b31cf06d66e7e8` — `engine/render/Renderer3D.h`: append perspective/orthographic near-far metadata to `PostFrame` without changing existing members or their aggregate-init compatibility.
12. `65d98da9391bbc31f6c78d68d044d659b146ade8` — `engine/render/Renderer3D.cpp`: populate new projection metadata from `RTSCamera` including orthographic projection's actual -200..1000 bounds.
13. `8f86756b5d47f5cdfa5f404be78aeb06beb20acb` — `engine/render/PrismaData.cpp`: `motionBlur.linearDepth` (false default) and `depthToleranceMeters` (0.35 default, 0.01..10 range); old `depthThreshold` retains device-depth semantics.
14. `c5936ff55f3aeee1ab014107d0fab2bb380a0bb6` — `engine/render/PrismaShaders.h`: optional camera-projection-based linear-depth comparison for velocity dilation and color gathers, supporting perspective and orthographic window depth; fallback reproduces original nonlinear device-depth comparison when `linearDepth` disabled.
15. `838d297c9b33ac2a77f37175761f35fc50ce0988` — `engine/render/Prisma.cpp`: expanded `MotionBlurU` shader uniform and `kMotionBlurFields`, wiring camera projection depth and metric threshold through fullscreen raster pipeline. Config gate checks projection metadata validity.
16. `2e3ff56139e2263fdc1d8e9efa177e3fc2a768ef` — `docs/prisma.md`: Stage 31 producer ownership, integration, opt-in, projection and risk.

## Public behavior and ownership

No change to the serialized `postfx` key, the existing effect ID `motionBlur`, or `AIVERSE_PRISMA_MOTION_BLUR=1` flag. All additions are rendering-only and opt-in via the existing HDR/high-quality path. The standalone PRISMA code imports neither FX nor RTS; the owning rendering subsystems produce RGBA8 reactive masks via generic `Renderer3D::addMotionReactiveDraw` and from built-in `DrawItem` geometry.

**RTS water:** `TerrainRenderer::draw` and `drawWater` queue water via `Renderer3D::drawMesh` with a translucent `waterColor`. The generic mesh reactive path reuses their original GPU vertex/index buffers and model matrix. It does **not** sample detailed water foam/reflection alpha, so coverage is approximated by the mesh's draw alpha. Other specialized water pipelines or `SpriteBatch::drawWater` may need their own explicit native reactive provider; this stage is a concrete integration for the shared MeshVertex water path.

**Particles:** `ParticleSystem::render` builds and uploads the normal frame's particle geometry and retains only non-opaque GPU draw run metadata. After the regular main pass, `renderReactive` uses that buffer again while a mask pass is already open, shading by source alpha and particle vertex alpha and discarding fragments behind the original scene depth. Both the native GameClient and editor register this per-scene callback immediately after rendering particles and before resolve. ParticleSystem owns its shader, pipelines and run data, not PRISMA.

**Depth modes:** `motionBlur.linearDepth=false` preserves existing `depthThreshold` in device-depth units. Enabling `linearDepth=true` uses the current camera projection's near/far and orthographic flag to compare **linear view depth** for both dilation and gather; `depthToleranceMeters` controls the threshold. Project scene world units must be metres for it to be strictly metric. This changes neither core projection matrices nor simulation state.

Example:
```json
{"type":"motionBlur","intensity":0.65,"samples":8,"maxPixels":20,
 "reactiveStrength":0.85,"velocityDilation":1,
 "linearDepth":true,"depthToleranceMeters":0.35}
```

## Real verification performed / NOT performed

**Performed:** read the current repository's water `RtsTerrainRender.cpp`, `Renderer3D::drawMesh` path, particle `FxRender.cpp`, `FxInternal.h`, `ParticleSystem.cpp`, editor/client call-sites, projection math and shader generator. Wrote relevant source, new shader, APIs and documentation using connected GitHub connector. Re-fetched all 12 modified source files to verify key declarations/uses exist after commits. This is **static source inspection**, not a build.

**NOT RUN by explicit user instruction (features first, tests later):** native C++ compiler/CMake, CTest, GL/Metal/GLES shader compilation, GameClient/editor executable, screenshots, GPU visual parity, memory/profiling and CI. No passing tests or shipping readiness is claimed.

## Known limitations

- The exact GPU shader behavior still needs real Metal/GL validation. In particular, alpha sampling, depth-format conventions, screen-space Y origins and Screen-blend coverage must be checked visually.
- FX reactive runs are valid only between the same `ParticleSystem::render()` and next `render()` / `shutdown()`. Callbacks must be synchronous in the same render thread and after the main FX draw. A differently sized viewport/changed GPU device causes `renderReactive` to skip.
- Reactive mask for water is mesh-level alpha coverage, not per-pixel physically based water transparency, foam, wetness or refraction. Specialized water/Terra modules outside the existing MeshVertex route still need a provider.
- The Stage 30 generic callback remains opt-in and is invoked only with the postprocess reactive pass active; a callback cannot cause MotionBlur to run on its own.
- Particle pipeline allocates two additional optional shader variants, and extra draws can add GPU cost; no performance claim until profiler measurements. No second particle simulation or GPU vertex upload is performed.
- Camera linearization assumes standard non-reversed window depth and accurate RTSCamera projection values. Depth threshold is in positive view-depth differences, not ray-distance for orthographic views. Device-specific variations need validation.
- Existing RHI explicit GPU-completed fence/retirement remains unavailable. Resource destruction must not use CPU `frameSerial()` as GPU completion proof. Stage 31 does not solve all inter-frame resource lifetime issues.
- External user-supplied velocity/mask textures still lack generic runtime format/dimension introspection. Keep owner validation.

## Next functional stage — Stage 32

1. Add specialized reactive inputs for `SpriteBatch::drawWater`, particle graph variants and any water/terrain modules that draw through custom pipelines rather than `Renderer3D::drawMesh`. Preserve producer ownership boundaries and existing image output when MotionBlur disabled.
2. Extend `gfx::Device` with non-blocking GPU-completed resource retirement/fences in backend-specific implementations, then migrate viewport history/temporary allocations safely.
3. Add explicit native texture dimension/format metadata for object velocity and reactive bridge and reject mismatched sources at the PRISMA boundary.
4. After the user permits testing: validate native builds and CTest, Metal/GL shader compilation, actual RTS water surface and particle renderings, editor Scene/Game dual viewports, both nonlinear and linear depth paths, and GPU performance. Document real PASS/FAIL without overstatements.
5. Record a separate `docs/handoffs/prisma-stage-32.md` after the next code stage. Do not merge to main without explicit instruction.
