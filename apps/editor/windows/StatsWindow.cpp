// Statistics: editor performance (frame time, sim time, draw calls), map contents, and during a
// play-test per-player graphs of army, structures and resources over time.
#include <algorithm>
#include <cfloat>
#include "../Editor.h"
#include "../EditorUtil.h"

namespace rts::editor {

class StatsWindow : public EditorWindow {
public:
    const char* id() const override { return "stats"; }
    const char* title() const override { return "Статистика"; }
    const char* group() const override { return "logic"; }
    int order() const override { return 2; }
    const char* dockArea() const override { return "bottom"; }

    void draw(Editor& ed) override {
        std::vector<float> frame, sim;
        uint32_t draws = 0;
        for (auto& f : ed.frameHistory) { frame.push_back(f.frameMs); sim.push_back(f.simMs); draws = f.drawCalls; }
        float avg = 0, peak = 0, simAvg = 0;
        for (float v : frame) { avg += v; peak = std::max(peak, v); }
        for (float v : sim) simAvg += v;
        avg = frame.empty() ? 0 : avg / float(frame.size());
        simAvg = sim.empty() ? 0 : simAvg / float(sim.size());
        const auto& d = ed.doc().data;
        // KPI cards
        char a[32], b[32], c[32], e[32];
        std::snprintf(a, sizeof a, "%.0f", ImGui::GetIO().Framerate);
        std::snprintf(b, sizeof b, "%.2f мс", avg);
        std::snprintf(c, sizeof c, "%u", draws);
        std::snprintf(e, sizeof e, ed.playing() ? "%.2f мс" : "—", simAvg);
        const Card cards[] = {{"FPS", a, avg > 33 ? ui::Tone::Warn : ui::Tone::Normal}, {"Кадр (CPU)", b, avg > 33 ? ui::Tone::Warn : ui::Tone::Normal},
                              {"Отрисовок", c, ui::Tone::Normal}, {"Симуляция", e, simAvg > 16 ? ui::Tone::Warn : ui::Tone::Normal}};
        cardRow(cards, 4);
        if (!frame.empty()) {
            char ov[48]; std::snprintf(ov, sizeof ov, "кадр, мс (пик %.1f)", peak);
            ImGui::PlotLines("##frame", frame.data(), int(frame.size()), 0, ov, 0, std::max(33.0f, peak), ImVec2(-1, 44));
        }
        if (ed.playing() && !sim.empty()) ImGui::PlotLines("##sim", sim.data(), int(sim.size()), 0, "симуляция, мс", 0, 16, ImVec2(-1, 44));
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
            const bool open = ImGui::CollapsingHeader(head, p < 2 ? ImGuiTreeNodeFlags_DefaultOpen : 0);
            ImGui::PopStyleColor();
            if (!open) continue;
            ImGui::PushStyleColor(ImGuiCol_PlotLines, col);
            std::vector<float> units, structs;
            for (auto& h : hist) { units.push_back(float(p < h.units.size() ? h.units[p] : 0)); structs.push_back(float(p < h.structures.size() ? h.structures[p] : 0)); }
            ImGui::PlotLines(("##u" + std::to_string(p)).c_str(), units.data(), int(units.size()), 0, "армия", 0, FLT_MAX, ImVec2(-1, 36));
            ImGui::PlotLines(("##s" + std::to_string(p)).c_str(), structs.data(), int(structs.size()), 0, "здания", 0, FLT_MAX, ImVec2(-1, 30));
            for (size_t r = 0; r < ed.rules().resources.size(); ++r) {
                std::vector<float> res;
                for (auto& h : hist) res.push_back(p < h.resources.size() && r < h.resources[p].size() ? h.resources[p][r] : 0);
                ImGui::PlotLines(("##r" + std::to_string(p) + "_" + std::to_string(r)).c_str(), res.data(), int(res.size()), 0,
                                 ed.rules().resources[r].name.c_str(), 0, FLT_MAX, ImVec2(-1, 26));
            }
            ImGui::PopStyleColor();
        }
    }

private:
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

RTS_EDITOR_WINDOW(StatsWindow);

} // namespace rts::editor
