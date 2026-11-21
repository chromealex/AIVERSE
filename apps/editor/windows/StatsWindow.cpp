// Statistics: editor performance (frame time, sim time, draw calls), map contents, and during a
// play-test per-player graphs of army, structures and resources over time.
#include <algorithm>
#include <cfloat>
#include "../Editor.h"
#include "../EditorUtil.h"
#include "engine/render/GlobalGeometryPool.h"
#include <span>

namespace aiverse::editor {

class StatsWindow : public EditorWindow {
public:
    const char* id() const override { return "stats"; }
    const char* title() const override { return "Статистика"; }
    const char* group() const override { return "logic"; }
    int order() const override { return 2; }
    const char* dockArea() const override { return "bottom"; }

    void draw(Editor& ed) override {
        // Reuse chart buffers across frames rather than allocating every tick.
        frameScratch_.clear(); simScratch_.clear();
        frameScratch_.reserve(ed.frameHistory.size());
        simScratch_.reserve(ed.frameHistory.size());
        uint32_t draws = 0;
        uint64_t cameraTriangles = 0;
        for (auto& f : ed.frameHistory) {
            frameScratch_.push_back(f.frameMs);
            simScratch_.push_back(f.simMs);
            draws = f.drawCalls;
            cameraTriangles = f.cameraTriangles;
        }
        float avg = 0, peak = 0, simAvg = 0;
        for (float v : frameScratch_) { avg += v; peak = std::max(peak, v); }
        for (float v : simScratch_) simAvg += v;
        avg = frameScratch_.empty() ? 0 : avg / float(frameScratch_.size());
        simAvg = simScratch_.empty() ? 0 : simAvg / float(simScratch_.size());
        const auto& d = ed.doc().data;
        // KPI cards
        char a[32], b[32], c[32], e[32], tr[32];
        std::snprintf(a, sizeof a, "%.0f", ImGui::GetIO().Framerate);
        std::snprintf(b, sizeof b, "%.2f мс", avg);
        std::snprintf(c, sizeof c, "%u", draws);
        // Compact card value in the same latest-viewport scope as Draw Calls.
        // Counts submitted camera geometry (including instances), not
        // rasterized pixels; custom terrain/sprite callbacks are excluded.
        if (cameraTriangles >= 1000000)
            std::snprintf(tr, sizeof tr, "%.2f M", double(cameraTriangles) / 1000000.0);
        else if (cameraTriangles >= 10000)
            std::snprintf(tr, sizeof tr, "%.1f K", double(cameraTriangles) / 1000.0);
        else std::snprintf(tr, sizeof tr, "%llu", (unsigned long long)cameraTriangles);
        std::snprintf(e, sizeof e, ed.playing() ? "%.2f мс" : "нет теста", simAvg);
        const Card cards[] = {{"FPS", a, avg > 33 ? ui::Tone::Warn : ui::Tone::Normal},
                              {"Кадр (CPU)", b, avg > 33 ? ui::Tone::Warn : ui::Tone::Normal},
                              {"Отрисовок", c, ui::Tone::Normal},
                              {"Треугольников", tr, ui::Tone::Normal},
                              {"Симуляция", e, simAvg > 16 ? ui::Tone::Warn : ui::Tone::Normal}};
        cardRow(cards, 5);
        if (!frameScratch_.empty()) {
            // Same percentile rank as before, now selected in O(n) rather
            // than sorting the entire 240-frame history at 120 FPS.
            percentileScratch_.assign(frameScratch_.begin(), frameScratch_.end());
            const size_t rank = std::min(percentileScratch_.size() - 1,
                size_t(float(percentileScratch_.size()) * 0.95f));
            std::nth_element(percentileScratch_.begin(),
                percentileScratch_.begin() + rank, percentileScratch_.end());
            const float p95 = percentileScratch_[rank];
            char ov[64]; std::snprintf(ov, sizeof ov, "кадр, мс (95%%: %.1f, пик %.1f)", p95, peak);
            ImGui::PlotLines("##frame", frameScratch_.data(), int(frameScratch_.size()), 0, ov, 0, std::max(20.0f, p95 * 1.5f), ImVec2(-1, 44));
        }
        if (ed.playing() && !simScratch_.empty()) ImGui::PlotLines("##sim", simScratch_.data(), int(simScratch_.size()), 0, "симуляция, мс", 0, 16, ImVec2(-1, 44));
        // Resource counts are from live owners, not the number of objects
        // visible in the camera. No texture or render-target byte estimates.
        ui::section(ICON_REGIONS, "Память ассетов (учтённая)");
        const auto pool = render::geometryPoolStats(ed.device());
        // Unique page identity within this owner, resident AND waiting for
        // a GPU retirement fence. These pages are already in 'pool' above
        // and must never be added to its device-wide allocation total.
        const auto referencedModels=
            ed.view().modelResources().geometryPoolModelReferences();
        const auto ownedPages=render::geometryPoolReferencedStats(
            ed.device(),std::span<const render::Model* const>(
                referencedModels.data(),referencedModels.size()));
        const auto residency = ed.view().assetResidencyStats();
        constexpr double mib = 1024.0 * 1024.0;
        ImGui::Text("GPU геометрия (pool): %.1f / %.1f MiB  ·  страниц: %zu",
                    double(pool.occupiedBytes) / mib, double(pool.allocatedBytes) / mib, pool.pages);
        ImGui::Text("Модели в пуле: %zu  ·  кэш моделей: %zu  ·  загружено типов: %zu / %zu",
                    pool.liveModels, residency.cachedModels, residency.loadedTypes, ed.rules().types.size());
        ImGui::TextDisabled("Из них страницы, используемые этим View: %zu · %.1f MiB выделено (уже включено выше)",
                    ownedPages.uniquePages,
                    double(ownedPages.allocatedBytes)/ (1024.0*1024.0));
        ImGui::Text("CPU кэш изображений: %.1f MiB  ·  GPU текстур моделей: %zu шт.",
                    double(residency.decodedCpuBytes) / mib, residency.gpuTextures);
        const auto modelOwnerMemory=ed.view().ownedModelGpuMemoryStats(ed.device());
        ImGui::TextDisabled("Model/material GPU owned: %.2f MiB, %zu textures + %zu buffers; unknown %zu",
            double(modelOwnerMemory.knownBytes()) / mib,
            modelOwnerMemory.textures,modelOwnerMemory.buffers,
            modelOwnerMemory.unknownTextures+modelOwnerMemory.unknownBuffers);
        ImGui::TextDisabled("Dedicated non-pooled geometry + owner textures only; shared geometry pages excluded.");
        const auto presentationAssets=ed.view().ownedPresentationGpuMemoryStats(ed.device());
        const auto particleAssets=ed.gameFx().ownedGpuMemoryStats();
        ImGui::TextDisabled("Terrain/planet + model presentation: %.2f MiB; SpectraFX core: %.2f MiB",
            double(presentationAssets.knownBytes())/mib,
            double(particleAssets.knownBytes())/mib);
        ImGui::TextDisabled("SpectraFX shader/graph textures are borrowed from their texture-cache owner; VRAM remains a lower bound.");
        ImGui::TextDisabled("Учтён только asset pool; отдельные render targets и FX указаны ниже частично.");
        ImGui::Text("GPU очередь освобождения: %zu моделей · %zu партий · пик %zu",
                    residency.retiredModels,residency.retirementBatches,
                    residency.retirementPeak);
        ImGui::Text("GPU модели: резидентные ≈ %.2f MiB · ожидают освобождения ≈ %.2f MiB",
                    double(residency.residentModelKnownBytes)/mib,
                    double(residency.estimatedRetiredGpuBytes)/mib);
        ImGui::Text("GPU текстуры (владелец кэша, RGBA8 экв.): ≈ %.2f MiB · %zu уникальных",
                    double(residency.sharedTextureRgba8Equivalent)/mib,
                    residency.uniqueSharedTextures);
        ImGui::TextDisabled("Из них сжатых GPU текстур: %zu; моделей в общем geometry pool: %zu",
                    residency.compressedSharedTextures,
                    residency.sharedGeometryModels);
        if(residency.estimatedRetirementByteLimit)
            ImGui::TextDisabled("Предел оценки: %.2f MiB",
                double(residency.estimatedRetirementByteLimit)/mib);
        ImGui::Text("Порог новых загрузок: %zu моделей · ожидание fence: %s",
                    residency.retirementSoftLimit,
                    residency.publicationBackpressured ? "ограничивает новые загрузки" : "норма");
        if(residency.retirementTokenFailures || residency.retirementFallbackFailures)
            ImGui::TextDisabled("GPU retirement: token fallback %llu, неуспешный idle %llu",
                     (unsigned long long)residency.retirementTokenFailures,
                     (unsigned long long)residency.retirementFallbackFailures);
        if(residency.unresolvedFailedFences)
            ImGui::TextDisabled("GPU fence ERROR: %zu удерживаемых партий (ошибок всего %llu)",
                residency.unresolvedFailedFences,
                (unsigned long long)residency.retirementFailedFenceBatches);
        ImGui::Text("GPU откат текстур hot reload: %zu текстур ждут GPU fence",
                    residency.pendingRolledBackImages);
        if(residency.imageRollbackEnqueueFailures ||
           residency.imageRollbackOrphanBatches)
            ImGui::TextDisabled("GPU откат: ошибок очереди %llu · потерянных партий %llu",
                (unsigned long long)residency.imageRollbackEnqueueFailures,
                (unsigned long long)residency.imageRollbackOrphanBatches);
        if(residency.stagingQueueFailures || residency.stagingOrphanModels)
            ImGui::TextDisabled("GPU откат моделей: ошибок очереди %llu · потерянных моделей %llu",
                (unsigned long long)residency.stagingQueueFailures,
                (unsigned long long)residency.stagingOrphanModels);
        ImGui::TextDisabled("GPU canvas/paint: %zu ресурсов ожидают fence",
            ed.view().pendingPaintGpuRetirements());
        if(ed.view().paintGpuRetirementOrphans())
            ImGui::TextDisabled("GPU canvas/paint: сиротских ресурсов %llu",
                (unsigned long long)ed.view().paintGpuRetirementOrphans());
        const auto canvasSubmissions=ed.view().paintGpuCanvasSubmitStats();
        if(canvasSubmissions.failedBrushBuffers ||
           canvasSubmissions.failedBlitBuffers ||
           canvasSubmissions.failedUploadTextures)
            ImGui::TextDisabled(
                "GPU canvas: brush OOM %llu · blit OOM %llu · staging OOM %llu",
                (unsigned long long)canvasSubmissions.failedBrushBuffers,
                (unsigned long long)canvasSubmissions.failedBlitBuffers,
                (unsigned long long)canvasSubmissions.failedUploadTextures);
        const auto canvasRetirement=ed.view().paintGpuRetirementStats();
        if(canvasRetirement.idleFallbackFailures ||
           canvasRetirement.unresolvedFailedFences ||
           ed.view().paintGpuRetirementEnqueueFailures())
            ImGui::TextDisabled(
                "GPU canvas/paint: idle failures %llu · failed fences %zu · queue failures %llu",
                (unsigned long long)canvasRetirement.idleFallbackFailures,
                canvasRetirement.unresolvedFailedFences,
                (unsigned long long)ed.view().paintGpuRetirementEnqueueFailures());
        // Comparison mode for historical authored LODs without an
        // accepted retarget plan. Visuals are unchanged by default.
        bool strictLegacyLod=ed.view().legacyLodFallbackMode()==
            game::RtsView::LegacyLodFallbackMode::BindPose;
        if(ImGui::Checkbox("RTS LOD: bind pose для несовместимых скелетов",
                           &strictLegacyLod))
            ed.view().setLegacyLodFallbackMode(
                strictLegacyLod
                    ?game::RtsView::LegacyLodFallbackMode::BindPose
                    :game::RtsView::LegacyLodFallbackMode::PreserveWorldMatrices);
        const auto legacyLod=ed.view().legacyLodFallbackStats();
        if(legacyLod.worldMatrixFallbacks || legacyLod.bindPoseFallbacks ||
           legacyLod.rejectedCompiledPlans)
            ImGui::TextDisabled(
                "RTS LOD: legacy %llu · bind %llu · rejected compiled %llu",
                (unsigned long long)legacyLod.worldMatrixFallbacks,
                (unsigned long long)legacyLod.bindPoseFallbacks,
                (unsigned long long)legacyLod.rejectedCompiledPlans);
        ImGui::TextDisabled("MotionWeave retarget CPU scratch: %.1f / %.1f KiB",
            double(ed.view().genericRetargetScratchCapacityBytes())/1024.0,
            double(ed.view().genericRetargetScratchBudgetBytes())/1024.0);
        const auto cacheReaders=ed.view().modelReadSafetyStats();
        if(cacheReaders.activeReaders || cacheReaders.peakReaders ||
           cacheReaders.blockedMutations)
            ImGui::TextDisabled(
                "Model cache read guards: %zu active / %zu peak / %llu blocked mutations",
                cacheReaders.activeReaders,cacheReaders.peakReaders,
                (unsigned long long)cacheReaders.blockedMutations);
        ImGui::TextDisabled("GPU программы Renderer3D: %zu партий ждут fence",
            ed.renderer().pendingProgramRetirements());
        ImGui::TextDisabled("GPU RenderTargets: %zu пар ждут fence",
            ed.renderer().pendingTargetRetirements());
        if(ed.renderer().targetRetirementOrphanPairs() ||
           ed.renderer().targetRetirementEnqueueFailures())
            ImGui::TextDisabled("GPU RenderTargets: ошибок очереди %llu · сиротских пар %llu",
                (unsigned long long)ed.renderer().targetRetirementEnqueueFailures(),
                (unsigned long long)ed.renderer().targetRetirementOrphanPairs());
        if(ed.renderer().unleasedTargetIdleFailures())
            ImGui::TextDisabled("GPU RenderTargets: отсутствие idle-барьера %llu",
                (unsigned long long)ed.renderer().unleasedTargetIdleFailures());
        const auto warm=ed.renderer().gpuWarmStats();
        if(warm.shaderJobs || warm.pipelineJobs || warm.colorVariants)
            ImGui::TextDisabled(
                "GPU shader warmup: %zu shaders · %zu pipelines · %zu HDR variants",
                warm.shaderJobs,warm.pipelineJobs,warm.colorVariants);
        if(warm.colorPollAttempts || warm.colorVariantsFailed)
            ImGui::TextDisabled("GPU HDR прогрев: опросов %llu · успешно %llu · ошибок %llu",
                (unsigned long long)warm.colorPollAttempts,
                (unsigned long long)warm.colorVariantsReady,
                (unsigned long long)warm.colorVariantsFailed);
        if(ed.renderer().programRetirementOrphanBatches())
            ImGui::TextDisabled("GPU программы: потеряно партий %llu",
                (unsigned long long)ed.renderer().programRetirementOrphanBatches());
        ImGui::TextDisabled("GPU PostFx: %zu ожидающих освобождения текстур",
            ed.postFx().pendingTargetRetirements());
        if(const auto* ring=ed.gpuRetirementDiagnostics()) {
            size_t modelEvents=0,imageEvents=0,targetEvents=0;
            for(const auto& event:ring->snapshot()) {
                if(event.resource==render::RetirementResourceKind::Model)
                    ++modelEvents;
                else if(event.resource==render::RetirementResourceKind::Image)
                    ++imageEvents;
                else if(event.resource==render::RetirementResourceKind::Target)
                    ++targetEvents;
            }
            ImGui::TextDisabled("GPU retirement события: %zu/%zu · переполнений %llu",
                ring->size(),ring->capacity(),
                (unsigned long long)ring->overflowCount());
            ImGui::TextDisabled("Из них GPU модели: %zu · откат текстур: %zu · RenderTargets: %zu",
                modelEvents,imageEvents,targetEvents);
        }
        ImGui::TextDisabled("Общий geometry pool учитывается выше отдельно. Оценки без драйверных mip/padding, FX и RT.");
        ImGui::TextDisabled("Выгрузка моделей редактора: %s (период ожидания 15 с)",
                            ed.lazyEditorAssetsEnabled() ? "по требованию" : "отключена");
        ui::section(ICON_REGIONS, "PRISMA · GPU resource retirement");
        const auto post = ed.prisma().retirementStats();
        const auto memory = ed.prisma().memoryStats();
        const auto motion = ed.renderer().motionRetirementStats();
        const auto rendererOwned = ed.renderer().residentMemoryStats();
        const auto viewport = ed.viewportRetirement().stats(ed.device());
        const auto viewportLive = ed.viewportRetirement().liveStats(ed.device());
        const auto objects = ed.gpuResourceRetirement().stats(ed.device());
        ImGui::Text("GPU completed fence: %llu  ·  сцены: %zu активных + %zu ожидают",
                    (unsigned long long)post.lastGpuCompletedFence,
                    post.activeSizes, post.retiredSizes);
        ImGui::Text("Auto Exposure: %zu камер  ·  Temporal HDR: %zu камер  ·  Motion: %zu камер  ·  Model velocity: %zu видов",
                    post.exposureViews, post.temporalViews, post.motionViews, motion.liveViews);
        const auto scopedGpu=ed.prisma().viewGpuTimings();
        if (scopedGpu.empty()) {
            ImGui::TextDisabled("Per-viewport GPU timing: unavailable / opt-in Metal counters or GL33");
        } else {
            for (const auto& gpu : scopedGpu) {
                const char* name=gpu.viewId==1?"Scene":gpu.viewId==2?"Game":"Custom";
                ImGui::Text("%s GPU render region: %.3f ms (completed, %llu samples)",
                    name,gpu.lastGpuMs,
                    (unsigned long long)gpu.sampleSequence);
            }
            ImGui::TextDisabled("Per-viewport intervals may include shared shadow passes; do not sum with GPU frame time.");
        }
        ImGui::Text("PRISMA: %zu fenced + %zu unfenced textures, graph pending %zu",
                    post.fencedTextures, post.unfencedTextures,
                    post.graphRetiredTextures);
        ImGui::Text("Renderer motion: %zu fenced + %zu unfenced textures",
                    motion.fencedTextures, motion.unfencedTextures);
        ImGui::Text("Renderer3D + owned submodules: %.2f MiB textures + %.2f MiB buffers (%zu/%zu handles)",
            double(rendererOwned.liveTextureKnownBytes) / mib,
            double(rendererOwned.liveBufferKnownBytes) / mib,
            rendererOwned.liveTextures,rendererOwned.liveBuffers);
        ImGui::TextDisabled("Renderer owned details: scene capture %.2f MiB, motion %.2f MiB, pending motion %.2f MiB",
            double(rendererOwned.sceneCaptureKnownBytes) / mib,
            double(rendererOwned.objectMotionKnownBytes) / mib,
            double(rendererOwned.pendingMotionKnownBytes) / mib);
        ImGui::TextDisabled("LightSystem %.2f MiB | persistent upload %.2f MiB | culling %.2f MiB | authored draws %.2f MiB",
            double(rendererOwned.lightSystemKnownBytes) / mib,
            double(rendererOwned.persistentUploadKnownBytes) / mib,
            double(rendererOwned.persistentCullKnownBytes) / mib,
            double(rendererOwned.gpuAuthoredKnownBytes) / mib);
        const auto geometryPages=render::geometryPoolStats(ed.device());
        ImGui::TextDisabled("GlobalGeometryPool %.2f MiB (%zu shared pages, counted separately from model handles)",
            double(geometryPages.allocatedBytes) / mib,geometryPages.pages);
        if (rendererOwned.unknownTextures || rendererOwned.unknownBuffers ||
            rendererOwned.multiVersionBuffers)
            ImGui::TextDisabled("Renderer unknown: %zu textures, %zu buffers; %zu multi-version buffer(s)",
                rendererOwned.unknownTextures,rendererOwned.unknownBuffers,
                rendererOwned.multiVersionBuffers);
        ImGui::TextDisabled("Renderer direct+subowners includes LightSystem/persistent pools; external models/materials, driver and Prisma excluded.");
        ImGui::Text("Hosted GameClient ожидают GPU: %zu  ·  ShaderGraph pools: %zu активных / %zu ожидают",
                    ed.clientHost().pendingRetiredClients(),
                    post.graphLiveTextures, post.graphRetiredTextures);
        ImGui::Text("RenderGraph GPU: %.2f MiB активных, %.2f MiB ожидают, %zu не учтено",
                    double(post.graphLiveKnownBytes) / mib,
                    double(post.graphPendingKnownBytes) / mib,
                    post.graphUnknownTextures);
        ImGui::Text("PRISMA GPU resident: %.2f MiB минимум / %zu текстур (%zu без размеров)",
                    double(memory.liveKnownBytes) / mib,
                    memory.liveTextures, memory.unknownTextures);
        ImGui::Text("Кеши разрешений: %.2f MiB / %zu кешей (не используются %zu: %.2f MiB)",
                    double(memory.sharedSizeKnownBytes) / mib, memory.activeSizeCaches,
                    memory.unreferencedSizeCaches,
                    double(memory.unreferencedSizeKnownBytes) / mib);
        ImGui::Text("История камер: %.3f MiB (Temporal HDR %.3f MiB)  ·  LUT/graph input: %.2f MiB",
                    double(memory.viewHistoryKnownBytes) / mib,
                    double(memory.temporalHistoryKnownBytes) / mib,
                    double(memory.otherLiveKnownBytes) / mib);
        if (ImGui::TreeNode("PRISMA: память по камерам (без двойного подсчёта)")) {
            if (memory.views.empty())
                ImGui::TextDisabled("Сейчас нет камер с активным постпроцессингом");
            for (const auto& view : memory.views) {
                const char* name = view.viewId == 1 ? "Scene" :
                                   view.viewId == 2 ? "Game" : "Custom";
                ImGui::BulletText("%s #%llu (%d x %d)  ·  Exposure %.3f MiB + Temporal %.3f MiB (%zu targets)",
                    name, (unsigned long long)view.viewId,
                    view.width, view.height,
                    double(view.ownedHistoryKnownBytes) / mib,
                    double(view.ownedTemporalKnownBytes) / mib,
                    view.ownedTemporalTextures);
                ImGui::Indent();
                ImGui::TextDisabled("Общий кеш размера: %.2f MiB / %zu текстур, совместно с %zu камерами",
                    double(view.sharedKnownBytes) / mib,
                    view.sharedTextures, view.sharedSizeViews);
                ImGui::TextDisabled("RenderGraph: %.2f MiB live (%zu) + %.2f MiB pending (%zu); неизвестно: %zu",
                    double(view.graphLiveKnownBytes) / mib, view.graphLiveTextures,
                    double(view.graphPendingKnownBytes) / mib, view.graphPendingTextures,
                    view.unknownTextures);
                ImGui::Unindent();
            }
            ImGui::TextDisabled("Общие кеши приведены справочно и НЕ складываются по камерам.");
            ImGui::TreePop();
        }
        ImGui::Text("Viewport Scene / Game / FX: %zu fenced + %zu без fence",
                    viewport.fenced,viewport.unfenced);
        for (const auto& v : viewportLive)
            ImGui::Text("Framebuffer %s %dx%d: %.2f MiB / %zu targets, неизвестно %zu",
                        v.name.c_str(),v.width,v.height,
                        double(v.knownBytes) / mib,v.textures,v.unknownTextures);
        ImGui::Text("GPU FX/ShaderGraph: %zu pipeline, %zu shader, %zu buffers, %zu textures, %zu bundles",
                    objects.pipelines,objects.shaders,objects.buffers,
                    objects.textures,objects.bundles);
        ImGui::Text("Другие отложенные GPU: %zu fenced + %zu без fence",
                    objects.fenced,objects.unfenced);
        if (objects.bundlesWithoutByteEstimate || objects.texturesWithoutByteEstimate ||
            objects.buffersWithoutByteEstimate)
            ImGui::TextDisabled("GPU без оценки bytes: %zu Preview/Portrait bundles, %zu textures, %zu buffers",
                objects.bundlesWithoutByteEstimate,
                objects.texturesWithoutByteEstimate,
                objects.buffersWithoutByteEstimate);
        if (objects.abandonedHandles || objects.abandonedBundles)
            ImGui::TextDisabled("GPU device lost: abandoned %zu handles, %zu owner bundles (без GPU destroy)",
                objects.abandonedHandles,objects.abandonedBundles);
        ImGui::Text("FX GPU-буферы: известные отложенные bytes включены в нижнюю оценку памяти");
        const uint64_t totalRetired = post.knownPendingBytes +
            motion.knownPendingBytes + viewport.knownPendingBytes +
            objects.knownPendingBytes;
        ImGui::Text("Удерживается GPU (известный минимум): %.2f MiB / %.0f MiB",
                    double(totalRetired) / mib,double(viewport.budgetBytes) / mib);
        if (viewport.budgetBytes && totalRetired > viewport.budgetBytes)
            ImGui::TextDisabled("Превышен бюджет: на следующем кадре попытка GPU idle. WebGL может не поддерживать ожидание.");
        if (ImGui::Button("Запросить безопасную очистку GPU"))
            ed.requestGpuRetirementFlush();
        ImGui::SameLine();
        ImGui::TextDisabled("в начале следующего кадра; может задержать отрисовку");
        ImGui::TextDisabled("Pending — удерживаемая память до GPU fence; resident — активные PRISMA ресурсы, НЕ прибавляйте к pending повторно.");
        ImGui::TextDisabled("Оценки без mip, драйверной аллокации и вложенных ShaderGraph/Portrait bundle bytes; это НЕ полный VRAM.");
        if (ed.lazyEditorAssetsEnabled() && ImGui::Button("Освободить неиспользуемые модели"))
            ed.reclaimEditorAssets(true);
        ui::section(ICON_REGIONS, "Карта");
        ImGui::Text("%d×%d клеток  ·  объектов %zu  ·  дорог %zu  ·  игроков %zu", d.w, d.h, d.entities.size(), d.roads.size(), d.players.size());
        Simulation* s = ed.sim();
        if (!s) { ui::hint("Графики армии, зданий и ресурсов по игрокам появятся во время теста (F5)."); return; }
        ui::section(ICON_PLAY, "Тест");
        ImGui::Text("Тик %u  ·  %.1f с  ·  сущностей %zu  ·  снарядов %zu", s->tick(), double(s->tick()) / s->tickRate(), s->world().count<game::Unit>(),
                    s->world().count<game::Projectile>());
        const auto& hist = ed.playHistory;
        if (hist.size() < 2) return;
        for (size_t p = 0; p < hist.back().units.size(); ++p) {
            render::float4 pc = game::RtsView::playerColor(int(p + 1));
            const ImVec4 col(pc.x, pc.y, pc.z, 1);
            std::string name = p < s->players().size() ? s->players()[p].name : "?";
            const auto& last = hist.back();
            char head[160];
            std::snprintf(head, sizeof head, "%s  ·  армия %d  ·  здания %d###pl%zu", name.c_str(), p < last.units.size() ? last.units[p] : 0,
                          p < last.structures.size() ? last.structures[p] : 0, p);
            ImGui::PushStyleColor(ImGuiCol_Text, col);
            const bool open = ui::collapsing(head, p < 2 ? ImGuiTreeNodeFlags_DefaultOpen : 0);
            ImGui::PopStyleColor();
            if (!open) continue;
            ImGui::PushStyleColor(ImGuiCol_PlotLines, col);
            unitScratch_.clear(); structureScratch_.clear();
            unitScratch_.reserve(hist.size());
            structureScratch_.reserve(hist.size());
            for (auto& h : hist) {
                unitScratch_.push_back(float(p < h.units.size() ? h.units[p] : 0));
                structureScratch_.push_back(float(p < h.structures.size() ? h.structures[p] : 0));
            }
            ImGui::PlotLines(("##u" + std::to_string(p)).c_str(), unitScratch_.data(), int(unitScratch_.size()), 0, "армия", 0, FLT_MAX, ImVec2(-1, 36));
            ImGui::PlotLines(("##s" + std::to_string(p)).c_str(), structureScratch_.data(), int(structureScratch_.size()), 0, "здания", 0, FLT_MAX, ImVec2(-1, 30));
            for (size_t r = 0; r < ed.rules().resources.size(); ++r) {
                resourceScratch_.clear();
                resourceScratch_.reserve(hist.size());
                for (auto& h : hist) resourceScratch_.push_back(p < h.resources.size() && r < h.resources[p].size() ? h.resources[p][r] : 0);
                ImGui::PlotLines(("##r" + std::to_string(p) + "_" + std::to_string(r)).c_str(), resourceScratch_.data(), int(resourceScratch_.size()), 0,
                                 ed.rules().resources[r].name.c_str(), 0, FLT_MAX, ImVec2(-1, 26));
            }
            ImGui::PopStyleColor();
        }
    }

private:
    std::vector<float> frameScratch_, simScratch_, percentileScratch_;
    std::vector<float> unitScratch_, structureScratch_, resourceScratch_;
    struct Card { const char* label; const char* value; ui::Tone tone; };
    // A row of small value cards that share the width (wraps to two rows when narrow).
    static void cardRow(const Card* cards, int n) {
        const float avail = ImGui::GetContentRegionAvail().x, gap = 6;
        const int cols = avail < ImGui::GetFontSize() * 22 ? 2 : n;
        const float w = (avail - gap * float(cols - 1)) / float(cols), h = ImGui::GetFontSize() * 3.0f;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        for (int i = 0; i < n; ++i) {
            if (i % cols) ImGui::SameLine(0, gap);
            const ImVec2 p = ImGui::GetCursorScreenPos();
            dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), ImGui::GetColorU32(ImGuiCol_FrameBg), ImGui::GetStyle().FrameRounding + 2);
            dl->AddText(ImVec2(p.x + 10, p.y + 6), ImGui::GetColorU32(ImGuiCol_TextDisabled), cards[i].label);
            ImFont* hf = ui::headingFont();
            dl->AddText(hf, ImGui::GetFontSize() * 1.25f, ImVec2(p.x + 10, p.y + 6 + ImGui::GetTextLineHeight() + 2),
                        ImGui::GetColorU32(ui::toneColor(cards[i].tone)), cards[i].value);
            ImGui::Dummy(ImVec2(w, h));
        }
    }
};

AIVERSE_EDITOR_WINDOW(StatsWindow);

} // namespace aiverse::editor
