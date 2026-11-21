# PRISMA Stage 49 — owned Model/Material GPU accounting and camera-priority temporal admission

**Date:** 2026-11-19.
**Repository:** `chromealex/AIVERSE`.
**Working branch:** `feature/prisma-postfx`.
**PR:** https://github.com/chromealex/AIVERSE/pull/26 — Draft, base `feature/universal-entity-inspector-fps`, not merged.
**Prior handoff:** `docs/handoffs/prisma-stage-48.md`.
**Code/design HEAD immediately before this handoff:** `3561fb19fda7c47b9a910687b9c29a278b83e21f`.
**Main:** not changed. Continue the *features-first* sequence; user specifically requests finishing functional implementation before native tests. Keep each next stage's own handoff.

## Actual implemented commits and files

1. `4fb0460b568ce43645b780a8f35a98826c6be0f8` — `engine/render/ModelTextureCache.h`: add `appendOwnedGpuMemory(GpuOwnedMemoryCounter&)`, counting shared cached model/material GPU textures from the **cache owner**, not a model's borrowed copy.
2. `2e327f3a84e8be8d1fc5db7f305f15bca8022014` — `engine/render/Model.h`: declare `appendModelOwnedGpuMemory(const Model&,GpuOwnedMemoryCounter&)` and depend on generic Stage 48 GPU owner memory primitives.
3. `3798ed14d6aa698fc75ece01888f9cfbb3497668` and `b426def84fd8ce62b4d99284c51bec7501634a6e` — `engine/render/Model.cpp`: count **dedicated** Model GPU VBO/IBO only when `geometryPoolPage` is empty; do not duplicate shared GeometryPool page buffers. Count nonborrowed model images using the same `Image.borrowedTexture` semantics as `releaseModel`, plus owned morph and baked animation clip textures.
4. `2655a18dad610c8df79d56dc83b0d52d02178e58` — `modules/rts/view/RtsView.h`: add `ownedModelGpuMemoryStats(const gfx::Device&)`, enumerating each unique cached model and the single shared ModelTextureCache, deduplicating typed GPU handles instead of counting draw instances. The named RtsView is a presentation asset owner, not an RTS gameplay dependency inside PRISMA.
5. `d952afc820adffd4de7eca208767df42b858f1f2` — `engine/render/TextureCache.h`: read-only `ownedGpuMemoryStats()` for atlas pages, generated/standalone sprites, deduplicating references/aliases to the same GPU atlas texture. Retired old textures remain in the separate GPU retirement owner.
6. `ded64b75d0b8c28c660500e0b82b0f921939f463` — `engine/render/Prisma.h`: public `ViewQualityPolicy {uint8_t priority=128; uint32_t maxTemporalHistoryMiB=0;}`, `setViewQualityPolicy(viewId,policy)`, `clearViewQualityPolicy(viewId)`, per-camera policies/admission state; frame diagnostics `adaptiveHistoryAdmissionRejected` and `adaptiveViewPriority`.
7. `d638ce69417df802d84357d980cdaee85bc6be98` — `engine/render/PrismaAdaptiveResolution.h`: distinct memory-pressure-only hysteresis thresholds by view priority — low 6, normal 12, high 30 *confirmed* GPU-completion observations before decreasing scale. A genuinely slow GPU still downgrades any view after the standard 12 observations. Existing 36-sample fast recovery and 48-sample cooldown remain; no estimated CPU FPS used.
8. `68f4a41b6a707ecbb08a82fc458d6b70e7ed1701` and `44e85b76457e502384955389d9bcf37af64d116b` — `engine/render/Prisma.cpp`: per-view two-history allocation admission using `outputWidth*outputHeight*16` bytes (RGBA16F × two). When over the camera's limit, return full-resolution render size and fence-retire prior histories; propagate refusal through **both** `jitteredCamera` and `resolve` so no denied history is allocated after preflight. Close view cleans up its policy and refused-admission marker.
9. `b0edca3447d1955f81630116a347f0f9e137a7a5`, `f277c48341d89705d86731498248d65f934a21b1` — `engine/render/Prisma.h`: generic host API `setExternalPresentationAssetsGpuMemory(knownBytes)` (the initial model-only name was broadened to include disjoint sprite/render asset owners). Exclude ModelCache borrowings, renderer-owned handles, Prisma textures and shared geometry pages.
10. `8bae51f0d613c45a2011b18ac8415ff0a03887bf`, `26af24843fc487195ca8349ecd47c7e294445663` — `engine/render/Prisma.cpp`: add external presentation assets into the existing saturating known-memory sum and clear them on shutdown; preserves the serialized `temporalResolve.maxPrismaMemoryMiB` key and the previous global resource/geometry budgets.
11. `473e0444236c83225218289b6eb03fe4ba2e1882`, `b41250e7d8ec80c9fbb7326cffd15b25aba83b9e` — `apps/editor/Editor.cpp`: before the Scene/Game `renderSizeForOutput` call, compute owner-only RtsView model/material assets plus the independent editor `fxTextures_` owned sprite/atlas texture cache. Supply the saturated nonoverlapping total. Scene priority **64** with history quota **128 MiB**; Game priority **224** with history quota **256 MiB**.
12. `d457aeb24bb0731c8669d00bbf69f04b473868ce`, `c95dc3d9de320c2cde81772e34d3f65d2eb71e80` — `apps/client/GameClient.cpp`: submit owned RtsView model/material asset bytes and prioritize running game (priority **224**, history quota **256 MiB**) before choosing render size. Unrelated host-owned FX textures may be added when that subsystem has a unique resource owner report.
13. `2c391da27b726344858ddc565493fa6d57358c67` — `apps/editor/windows/StatsWindow.cpp`: show owner-only live model/material texture and dedicated VBO/IBO known bytes and unknown counts, separate from GeometryPool pages and PRISMA/Renderer3D.
14. `85dd78d89640058ff641ee621510d4df69b480ef` — `apps/editor/windows/PrismaWindow.cpp`: report last-frame view priority and HDR-history admission status; label adaptive memory as Prisma + renderer + geometry + presentation assets, *known minimum*, not total VRAM.
15. `094d085a37b7d8338a18089e8d433c1e9a75e3a9` — `skills/visuals/aiverse-prisma/SKILL.md`: Stage 49 integration, ownership, limits, source checks, next-agent guidance and explicit no-test-yet sequencing.
16. `3561fb19fda7c47b9a910687b9c29a278b83e21f` — `docs/prisma.md`: Stage 49 design, public API, memory owner boundaries and documented limitations.
17. **This file** — `docs/handoffs/prisma-stage-49.md`, created in a separate commit after all sources and documentation.

## API and architecture

```cpp
// On graphics owner thread, BEFORE renderSizeForOutput.
auto modelStats = view.ownedModelGpuMemoryStats(device);
auto spriteStats = effectsTextureCache.ownedGpuMemoryStats();
prisma.setExternalPresentationAssetsGpuMemory(
    GpuOwnedMemoryCounter::saturatingAdd(
        modelStats.knownBytes(),spriteStats.knownBytes()));
prisma.setExternalRendererGpuMemory(
    rendererStats.liveTextureKnownBytes+rendererStats.liveBufferKnownBytes,
    rendererStats.pendingMotionKnownBytes);
prisma.setExternalGeometryPoolGpuMemory(
    geometryPoolStats(device).allocatedBytes);
prisma.setViewQualityPolicy(viewId,{priority,maxTemporalHistoryMiB});
auto [renderW,renderH] = prisma.renderSizeForOutput(outW,outH);
auto renderCamera = prisma.jitteredCamera(originalCamera,renderW,renderH);
renderer.beginScene(renderCamera,renderW,renderH);
```

Model instances are NOT individual GPU allocation owners. `Model::vb/ib` backed by `geometryPoolPage` are counted only through GlobalGeometryPool page allocations. `Image::borrowedTexture` images are counted by the owning ModelTextureCache, not by `Model`; cache texture leases remain authoritative for GPU destruction. Unpooled model buffers, unique direct images, morph and baked clip textures are counted by `Model`. Atlas sprite pages in `TextureCache` are counted once even if many cached entries reference the same page. Stage 48 Prisma/Renderer3D/Lighting/Persistent/Geometry memory reports stay separate. This is NOT a process-wide allocation tracker.

The host may override priority/quotas per stable view ID. `maxTemporalHistoryMiB=0` means no hard camera history quota; a positive cap applies only to two output-resolution RGBA16F histories, not scene depth, Bloom/DoF effects or total VRAM. Admission checks happen before render scene jitter. Over-cap rejection disables temporal history for this view in the subsequent `resolve`, returns native output dimensions, and retires former history textures behind GPU fences. An unjittered/native composite remains available. Under global memory pressure, low priority yields earlier (6 confirmed observations), normal priority 12, high priority 30, but GPU-over-budget still uses 12 on every view; no simulated GPU completion. Retire view clears the policy and admission state. Normal native projects without the Stage 44–46 opt-in flags remain unchanged.

**Host defaults for this stage**: editor Scene priority 64, history cap 128 MiB; editor Game/native GameClient priority 224, history cap 256 MiB. Custom hosts can tune these via the public API. No RTS/FPS or gameplay/ECS dependency is added to Prisma; model asset enumeration stays with its owning presentation layer.

## Verification actually done / not done

**Performed:** inspected native Model/Image ownership, `releaseModel()`, pooled shared geometry lifecycle, ModelTextureCache GPU leases, `RtsView` cached model ownership, TextureCache atlas-page aliases, editor/native GameClient call sites, Prisma temporal preflight/resolve, adaptive hysteresis and source docs. GitHub code and documentation commits were read back from the exact feature branch. **51/51 structural source-presence and ordering checks passed** for 14 touched source/docs paths. Verified the editor Scene/Game and native client pass owner-only values before `renderSizeForOutput`; verified admission precedes GPU preflight and `resolve` guards against rejected histories. These are **source-text inspections**, not C++ compilation or runtime tests.

**NOT RUN** under explicit user instruction to complete *all* features first: CMake, C++/ObjC++ compilation, shader GLSL/MSL compilation, CTest, Metal/OpenGL/Vulkan/GLES/WebGL device execution, memory pressure GPU simulation, scene/game image comparisons, performance benchmarks and CI. Never call these PASS, and never claim production-level VRAM precision.

## Remaining functional work before separate final validation phase (Stage 50 onwards)

1. **External presentation owners**: expose nonduplicating owner GPU telemetry from native particle/VFX, terrain/foliage, UI offscreen/material caches and other modules that own their own RHI GPU handles. The current editor provides `fxTextures_` but native GameClient `ClientFx` and other render modules do not yet all report their unique allocations. Only admit owner-reported handles; never scan borrowed `Renderer3D::DrawItem` textures and claim them.
2. **Camera quality governor**: add coherent per-view visibility and priority scheduling (hidden Scene views should not retain policy/samples indefinitely). Current `retireView` clears policy, but host-driven viewport and hosted GameClient lifecycle need inspection. Keep GPU completion and texture retirement as separate concepts.
3. **Safe complete functional fallback** across unsupported HDR, Metal counter sampling, GLSL/MSL, motion/reactive input quality, resource allocation exhaustion, resize and camera-projection changes. Do not broaden default-on scope until GPU validation is permitted.
4. Check any other missing Prisma feature coverage compared with the desired general-purpose postprocessing engine and finish functional APIs. Do not treat unsupported vendor upscalers as implemented; Stage 44 fixed-scale reconstruction is separate from vendor TSR/FSR/DLSS.
5. After functional completion, schedule one separate native verification/fix phase. The user explicitly requested **features first, then tests**. Record all actual commands and outcomes only when run.
6. Each feature stage MUST commit its own handoff with exact SHA, files, API, actual source-only checks vs native tests, limitations and next concrete step. Preserve PR #26 Draft; **do not merge main** unless user explicitly approves. Note PR metadata currently reports `mergeable=false`; investigate before eventual merge, not in this feature step.

## Mac pull commands to resume

```bash
git fetch origin
git switch feature/prisma-postfx
git pull --ff-only origin feature/prisma-postfx
```
