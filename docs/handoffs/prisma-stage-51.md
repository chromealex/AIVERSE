# PRISMA Stage 51 — remaining GPU owners, capability diagnostics, and custom-camera lifecycle

Date: 2026-11-19
Repository: chromealex/AIVERSE
Branch: feature/prisma-postfx
Draft PR: https://github.com/chromealex/AIVERSE/pull/26
Base: feature/universal-entity-inspector-fps
Prior stage: docs/handoffs/prisma-stage-50.md
Main: unchanged; do not merge until explicit user approval.
User-requested development order: FINISH ALL FUNCTIONAL FEATURES first; only then native build, CTest, Metal/GL/GLES/Vulkan GPU run, profiling, screenshots and fixes.

## Completed source commits and changed files

1. ba05addc1fe266f1f9aee7a945fdb959ba9666b3 — engine/render/SpriteBatch.h. Public read-only ownedGpuMemoryStats() for the sprite batch's own VB/IB/caster buffers; it does not claim textures or caller-owned static SpriteMesh buffers.
2. f08d615659cdd3df0538b2e1833e9b4574326ec6 — modules/rts/view/RtsFog.h. Report only the owner's fog sheet VB/IB/texture.
3. 0c43e05dcc89fb61251412ecbd5fc5f68c6c7182 — modules/rts/view/RtsFieldView.h. Report field sheet buffers and per-field texture array.
4. 556a64bc8e3110ab883bd240e9e8bd7f787ee39a — modules/rts/view/RtsOverlays.h. Report own overlay vertex buffer, white placeholder and private TextureCache.
5. e7ac69e0d9f9a37350e89d41fcccccd83894e7f1 — modules/rts/view/RtsGroundFx.h. Report unique chunk/lanes mesh GPU buffers, white texture and its private texture cache.
6. 9d5f987c94e16eaa6b22a4d3ea212a615765bc9e — modules/rts/view/Rts2DView.h. Count SpriteBatch buffers, owned static tile mesh and sprite cache images.
7. b318d4d12f353b4b4bf382bc8b2a614ea805db34 — modules/rts/view/RtsRoadView.h. Count resident unique road/ghost/prop model resources, all kit-owned image handles once, decal/wet film/puddle mesh VBs, SpriteBatch and paint TextureCache. Chunk models' borrowed kit image references are deliberately excluded.
8. 7c7f7779f7f476afd57488e9fb366aa96753b9cf — modules/rts/view/RtsShoreView.h. Count unique resident shoreline/foam/prop model assets and their kit-owned images once; chunk models borrow kit images.
9. a6c6b662fb678152cd951c690171b733e15882b9 — modules/rts/view/RtsPlanetGroundFx.h. Report planet ground overlay tile and lane mesh allocations per body, with no borrowed terrain texture ownership.
10. c2b9c51a26d9480d602ec24ea572d0c8ff4fdfa6 — modules/rts/view/RtsGroundCanvas.h. Report canvas SpriteBatch and independent texture cache GPU bytes.
11. befc6eb7f669101d8ea1cf4f44fae3990f186fd1 — modules/rts/view/RtsPaintLayers.h. Report canvas RenderTexture targets, coast GPU textures and layer mesh buffers.
12. a30d8065d67590b916e7b2bac738d2827de62ae2 — modules/rts/view/RtsView.h. Aggregate all these disjoint owners into the generic presentation GPU owner report alongside the Stage 49-50 model/terrain/planet assets and projectile mesh, even when assets remain resident but are not drawn that exact frame.
13. 30e94325a73ba4c80c7f69c3b1b5a1fc5be44e38 — engine/render/Prisma.h. Public GPU-free read-only RuntimeCapabilities structure and canonicalTemporalRequested() interface; requested vs prepared vs last-frame effects.
14. 0994a4b738f4843837d4785ccf6e6a0333ef97a3 — engine/render/Prisma.cpp. Implement requested HDR/Temporal/Jitter/Upscale/Adaptive report, device RGBA16F support, relevant pipeline-handle preparedness, recent reactive data, and specific known fallback reasons. This method never allocates GPU resources or compiles shader programs.
15. d4f91fbf42e2f05c3f328cf673999f8f142af665, 1c192f2272296991c8640a5ad0bb8ab0f1fffd4a — Prisma.h and Prisma.cpp. Track last-frame valid reactive input in PrismaStats.
16. 74bcea9ecbb4edc42894871cf50c64b75d0bbb18 — engine/render/PrismaScreenLighting.cpp. When canonical Temporal Resolve is authored and requested, do not run older PrismaScreenLighting history resolver simultaneously; delegate jitter to Prisma, reset legacy history and suppress legacy scene temporal passes. This temporarily also skips legacy bounce/volumetric contributions; those features need separated ownership in Stage 52.
17. b3ed9baf04cf0ff3015106b69cf36411fbbb0c52 — Prisma.cpp. Canonical temporal owner precedence depends on requested HDR/High temporal settings, not lazy shader compilation state; no unexpected fallback to a competing temporal owner just because the canonical shader is not yet prepared.
18. aedde374441dedfcde4d98a8b9119533b5e01527 — apps/editor/windows/PrismaWindow.cpp. Expose requested and effective temporal/HDR/jitter/upscale information, last-resolve execution and fallback reason.
19. a5d14b185211acb4eb8ee96ad420a211035b78ad, 6f5774c9576f216ec857d28d5a0a0162a514b50e — Prisma.h and Prisma.cpp. Public retireUnregisteredViews(activeStableViewIds), union of all per-view exposure, motion matrices, temporal histories, quality/policy, timing and size reference maps. Uses existing GPU-fenced retireView; never force-destroys in-flight targets or removes size-shared caches while still referenced.
20. fc3c1349064916cf915a38fe10ce1883917b0009 — apps/editor/Editor.cpp. At the end of viewport submission, keep the existing hard-coded Scene/Game motion-retirement closure while also pruning orphaned custom Prisma view state through the generic live-ID registry. Keep retireUnreferencedSizeCaches as a separate size-cache lifetime policy.
21. 8403c34af48ccfb2247c38fe4e4a4bdca37f255d, 391ca4fe37a18eb7deebe31226caafa8e62f68fb, f87f3bbbf53b60d18ae34abba48cf2bf3def577d — Prisma.h, Prisma.cpp and PrismaWindow.cpp. Explicitly distinguish authored HDR intent from currently usable HDR (driver format + quality + settings); reject stale per-view GPU timing samples older than eight confirmed frame fences from the capability report; update editor labels.
22. 8462a9f99172cf40f0ccc15a0f435a2758d0d498 — skills/visuals/aiverse-prisma/SKILL.md. Stage 51 owner APIs, GPU capabilities, custom view contract, temporal conflict and feature-first handoff requirements.
23. 745a66d478d807ebf3c379b89820e44494efe9d3 — docs/prisma.md. Stage 51 design, memory ownership, actual vs requested status, known limitations and no-native-test claim.
24. This file — docs/handoffs/prisma-stage-51.md. Separate last commit after code/docs and structural readback; get the resulting branch SHA from PR #26.

## Architecture and API

Each owner enumerates only its DIRECT RHI buffers/textures via GpuOwnedMemoryCounter. It never maps, reads back, waits for GPU, force-destroys, or adopts another module's resources. SpriteBatch does not count caller-owned SpriteMeshes, borrowed textures or TextureCache atlas references; Roads/Shores count shared kit images once, and their derived Model::images are borrowed. GeometryPool shared pages, ModelTextureCache and Renderer3D/LightSystem-owned allocations remain in their original independent owner buckets. Missing RHI information remains unknown; output is known lower bound, not total VRAM.

RtsView::ownedPresentationGpuMemoryStats(device) merges model/material cache + terrain/planet owners (Stage 50) with GroundFx, PlanetGroundFx, Fog, FieldView, Overlays, GroundCanvas, PaintLayers, Rts2DView, Roads, Shores and projectile mesh. The existing Editor Scene/Game and native GameClient already submit this report as one disjoint presentation-owner input before renderSizeForOutput, in addition to their separate Renderer3D/GeometryPool inputs.

Prisma::RuntimeCapabilities report = prisma.runtimeCapabilities();
This reports deviceHealthy, hdrFormatSupported, hdrAuthoredRequested, hdrRequested (effective), hdrEffectiveLastFrame, temporalRequested, temporalPipelinePrepared, temporalResolvedLastFrame, jitterRequested, jitterAppliedLastFrame, upscaleRequested, upscaleAppliedLastFrame, adaptiveRequested, scopedGpuSampleAvailable, velocityPipelinePrepared, reactiveLastFrame and fallbackReason. Requested is not equivalent to real GPU support, prepared pipeline is not real GPU execution, and last-frame values are the latest Prisma resolve rather than simultaneous per-view snapshots. The method does not allocate/compile GPU programs.

The optional PrismaScreenLighting wrapper has its own previously independent TAA. If canonical HDR High Temporal Resolve is requested, PrismaScreenLighting::jittered delegates to Prisma::jitteredCamera and resets the old jitter/history; its scenePass skips the legacy resolver. The canonical owner wins even if its shader is not yet compiled, removing the risk of double temporal histories. NOTE: the old bounce/volumetric screen-lighting contributions also stop when canonical temporal is active. Stage 52 should split lighting-only passes from legacy TAA so users do not lose those effects.

For native project custom cameras: each host explicitly assigns a stable view ID, renders its own camera, and after rendering all viewports invokes prisma.retireUnregisteredViews(activeIds), then prisma.retireUnreferencedSizeCaches and collectGpuRetirements. The generic cleanup calls retireView for per-camera targets/policy/motion/view size references with GPU-fenced texture retirement, NOT a CPU frame guessing heuristic. The renderer owner separately calls Renderer3D::retireObjectMotionView for inactive custom IDs with backend-issued submission fences; the editor already handles Scene/Game. Never immediately release resources still in flight.

## Actual verification

Performed: inspected current GitHub feature branch and real resource owner fields for SpriteBatch, Fog, FieldView, Overlays, RtsGroundFx/RtsPlanetGroundFx, Rts2DView, RtsRoadView, RtsShoreView, RtsPaintLayers/RtsGroundCanvas, RtsView, legacy PrismaScreenLighting, Prism HUD/viewport lifecycle and RHI HDR support. Committed source changes and read back all 19 touched source/docs paths. Source-presence and call-order checks: **65 of 65 passed**, including capability report/request differentiation, canonical historical temporal suppression, per-view retirement, all RtsView owners and editor status UI. These are SOURCE ONLY, not compiled tests.

NOT RUN by explicit user direction: CMake, native C++/Obj-C++ build, clang Metal API checks, GLSL/MSL shader compilation, CTest, Metal/GL/GLES/Vulkan/WebGL GPU execution, VRAM/FPS profiling, screenshot parity, GPU-fence stress, or CI. Do not claim any of these passed or declare production readiness.

## Next FUNCTIONAL stage (Stage 52) before test phase

1. Decouple the PrismaScreenLighting diffuse bounce and volumetric light contributions from its legacy TAA/history so those image features can coexist with canonical temporal reconstruction without a second TAA owner; preserve HDR/linear tone mapping and native projection state.
2. Add explicit GPU support/fallback mode controls for selected effects, possibly a preview matrix of effect requested vs executable vs last executed per viewport (current Stage 51 reports main temporal/camera capabilities only). Keep system feature flags off by default.
3. Extend custom native host (beyond editor 1/2 and GameClient 3) view retirement integration, including Renderer3D private object-motion view retirement with actual backend fence. Never retire active view history because another view shares the same dimensions.
4. Review remaining general-purpose Prisma postprocessing feature catalog and editor UX APIs for missing effects and finish them as functionality before the separate native-build/GPU-validation phase. Do not claim vendor TSR/FSR/DLSS, full device VRAM or untested GPU effects.
5. Commit new docs/handoffs/prisma-stage-52.md after implementing its source features, listing exact branch SHA, APIs, modified files, actual source checks, tests NOT RUN and concrete next feature.
6. PR #26 stays Draft. GitHub metadata previously reported mergeable=false; investigate only when ready for the eventual merge. Do not merge to main or any base until explicitly asked.

## Mac pull commands

    cd ~/Projects/AIVERSE
    git fetch origin
    git switch feature/prisma-postfx
    git pull --ff-only origin feature/prisma-postfx
