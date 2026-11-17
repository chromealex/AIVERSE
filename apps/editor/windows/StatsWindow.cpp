// Statistics: editor performance (frame time, sim time, draw calls), map contents, and during a
// play-test per-player graphs of army, structures and resources over time.
#include <algorithm>
#include <cfloat>
#include "../Editor.h"
#include "../EditorUtil.h"
#include "engine/render/GlobalGeometryPool.h"

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
        const auto residency = ed.view().assetResidencyStats();
        constexpr double mib = 1024.0 * 1024.0;
        ImGui::Text("GPU геометрия (pool): %.1f / %.1f MiB  ·  страниц: %zu",
                    double(pool.occupiedBytes) / mib, double(pool.allocatedBytes) / mib, pool.pages);
        ImGui::Text("Модели в пуле: %zu  ·  кэш моделей: %zu  ·  загружено типов: %zu / %zu",
                    pool.liveModels, residency.cachedModels, residency.loadedTypes, ed.rules().types.size());
        ImGui::Text("CPU кэш изображений: %.1f MiB  ·  GPU текстур моделей: %zu шт.",
                    double(residency.decodedCpuBytes) / mib, residency.gpuTextures);
        ImGui::TextDisabled("Без dedicated-геометрии, размеров GPU текстур, render targets и FX.");
        ImGui::TextDisabled("Выгрузка моделей редактора: %s (период ожидания 15 с)",
                            ed.lazyEditorAssetsEnabled() ? "по требованию" : "отключена");
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
