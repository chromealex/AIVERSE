// Fog of war settings of the open game: mode (full / explored / none), colour, transparency,
// style (smooth, cells, grid, dithered pattern), soft edges, reveal fade, cloudy noise, cell grid.
// Stored in rules.json "fogOfWar" through rules_merge (validated, undoable like any rules edit);
// the play-test in the Game window shows the result immediately.
#include <algorithm>
#include <cmath>
#include "../Editor.h"
#include "../EditorUtil.h"

namespace aiverse::editor {

class FogWindow : public EditorWindow {
public:
    const char* id() const override { return "fog"; }
    const char* title() const override { return "Туман войны"; }
    const char* group() const override { return "world"; }
    const char* dockArea() const override { return "right-bottom"; }
    int order() const override { return 6; }
    bool openByDefault() const override { return false; }

    void draw(Editor& ed) override {
        auto& ws = ed.ws();
        if (ws.project.dir.empty()) { ui::emptyStateHere(ICON_FOG, "Игра не открыта", "Файл → Открыть проект"); return; }
        const Json& f = ws.project.rulesJson["fogOfWar"];
        ui::section(ICON_EYE, "Режим (влияет на игру)");
        static const char* modes[] = {"full", "explored", "none"};
        static const char* modeNames[] = {"Полный", "Разведано", "Без тумана"};
        static const char* modeHelp[] = {"Чёрная карта, разведанное темнее", "Карта открыта, враги — только в зоне видимости", "Все видят всё"};
        std::string mode = f.getStr("mode", "full");
        int mi = mode == "explored" ? 1 : mode == "none" || mode == "off" ? 2 : 0;
        if (ui::segmented("##mode", mi, modeNames, 3)) set(ed, "mode", Json::string(modes[mi]));
        ui::hint(modeHelp[mi]);
        bool share = f.getBool("shareAllies", true);
        if (ui::checkbox("Союзники видят общее", &share)) set(ed, "shareAllies", Json::fromBool(share));
        bool los = f.getBool("lineOfSight", false);
        if (ui::checkbox("3D: линия видимости по высотам", &los)) set(ed, "lineOfSight", Json::fromBool(los));
        ui::help("Видимость считается лучом от глаз юнита: холмы, уровни скал, стены (тип \"vision\": {\"blocks\": h}), "
                 "дым (delivery.blocksVision) и нераскопанная порода закрывают то, что за ними. Вышки видят дальше "
                 "(\"vision\": {\"eye\": h}), летающие и \"seesOver\" — поверх всего.");
        if (los && ui::beginProps("##los")) {
            float rock = float(f["rockHeight"].toDouble(2));
            ui::prop("Высота породы");
            ImGui::SliderFloat("##rock", &rock, 0, 6, "%.1f");
            if (ImGui::IsItemDeactivatedAfterEdit()) set(ed, "rockHeight", Json::number(double(rock), 1));
            ui::endProps();
        }

        ui::section(ICON_PALETTE, "Вид");
        bool show = f.getBool("show", true);
        if (ui::checkbox("Рисовать туман", &show)) set(ed, "show", Json::fromBool(show));
        ui::help("Выключено: туман не рисуется, но враги вне видимости всё равно скрыты (режим выше).");
        if (ui::beginProps("##look")) {
            float col[3] = {8 / 255.0f, 10 / 255.0f, 14 / 255.0f};
            readColor(f["color"], col);
            ui::prop("Цвет");
            if (colorField("##col", col, 0)) set(ed, "color", rgb(col));
            slider(ed, f, "unexplored", "Неразведанное", 0.92f, 0, 1);
            slider(ed, f, "explored", "Разведанное", 0.47f, 0, 1);
            static const char* styles[] = {"smooth", "cells", "grid", "pattern"};
            static const char* styleNames[] = {"Мягкие края", "Клетки", "Клетки с сеткой", "Узор (классика)"};
            std::string st = f.getStr("style", "smooth");
            int si = st == "cells" ? 1 : st == "grid" ? 2 : st == "pattern" ? 3 : 0;
            ui::prop("Стиль");
            if (ImGui::Combo("##style", &si, styleNames, 4)) set(ed, "style", Json::string(styles[si]));
            int soft = int(f["softness"].integer(2));
            ui::prop("Мягкость", "Ширина размытого края, клеток");
            ImGui::SliderInt("##soft", &soft, 0, 6, "%d клет.");
            if (ImGui::IsItemDeactivatedAfterEdit()) set(ed, "softness", Json::number(int64_t(soft)));
            slider(ed, f, "fade", "Проявление, с", 0.3f, 0, 3);
            slider(ed, f, "noise", "Облачные края", 0.0f, 0, 1);
            slider(ed, f, "height", "Высота слоя", 1.3f, 0, 4, "Насколько высоко над землёй лежит туман: закрывает юнитов и низкие здания в неразведанных клетках.");
            ui::endProps();
        }

        ui::section(ICON_GRID, "Сетка клеток");
        const Json& g = f["grid"];
        static const char* gshow[] = {"none", "fog", "all"};
        static const char* gnames[] = {"Нет", "В тумане", "Везде"};
        std::string gs = g.isObject() ? g.getStr("show", "fog") : "none";
        if (g.isObject() && g["alpha"].toDouble(0.12) <= 0) gs = "none";
        int gi = gs == "fog" ? 1 : gs == "all" || gs == "always" ? 2 : 0;
        if (ui::segmented("##grid", gi, gnames, 3)) {
            Json o = g.isObject() ? g : Json::object();
            o.set("show", Json::string(gshow[gi]));
            if (!o.has("alpha")) o.set("alpha", Json::number(0.15, 2));
            set(ed, "grid", o);
        }
        if (gi > 0 && ui::beginProps("##gridp")) {
            float ga = float(g["alpha"].toDouble(0.15));
            ui::prop("Яркость линий");
            ImGui::SliderFloat("##ga", &ga, 0, 1, "%.2f");
            if (ImGui::IsItemDeactivatedAfterEdit()) { Json o = g; o.set("alpha", Json::number(double(ga), 2)); set(ed, "grid", o); }
            float gc[3] = {1, 1, 1};
            readColor(g["color"], gc);
            ui::prop("Цвет линий");
            if (colorField("##gc", gc, 1)) { Json o = g; o.set("color", rgb(gc)); set(ed, "grid", o); }
            ui::endProps();
        }
        ImGui::Spacing();
        const int n = ed.playing() ? 1 : 2;
        if (ui::wideButton(ICON_UNDO, "Сбросить к стандарту", 0, n)) {
            Json p = Json::object();
            p.set("path", Json::string("fogOfWar"));
            ed.ws().call("rules_delete", p);
            ed.reloadRules();
        }
        if (!ed.playing() && ui::wideButton(ICON_PLAY, "Посмотреть в игре", 1, n, true)) { ed.startPlay(1); ed.focusWindow("game"); }
        if (!message_.empty()) ui::callout(ui::Tone::Error, message_);
        ui::hint("rules.json → fogOfWar (docs/game-format.md)");
    }

private:
    static void readColor(const Json& c, float (&out)[3]) {
        if (c.size() < 3) return;
        for (int i = 0; i < 3; ++i) { double v = c[size_t(i)].toDouble(); out[i] = float(v > 1 ? v / 255 : v); }
    }
    static Json rgb(const float* c) {
        Json a = Json::array();
        for (int i = 0; i < 3; ++i) a.push(Json::number(int64_t(std::lround(std::clamp(c[i], 0.0f, 1.0f) * 255))));
        return a;
    }
    // A row of the props grid.
    void slider(Editor& ed, const Json& f, const char* key, const char* label, float def, float lo, float hi, const char* tip = nullptr) {
        float v = float(f[key].toDouble(def));
        ui::prop(label, tip);
        ImGui::SliderFloat((std::string("##") + key).c_str(), &v, lo, hi, "%.2f");
        if (ImGui::IsItemDeactivatedAfterEdit()) set(ed, key, Json::number(double(v), 2));
    }
    void set(Editor& ed, const char* key, const Json& value) {
        Json v = Json::object();
        v.set(key, value);
        Json p = Json::object();
        p.set("path", Json::string("fogOfWar"));
        p.set("value", v);
        Json r = ed.ws().call("rules_merge", p);
        message_ = r.has("error") ? r.getStr("error") : "";
        if (!r.has("error")) ed.reloadRules();
    }
    // Colour button whose picker edits a local copy; the rules are written once, when the picker closes
    // (writing on every drag step would reload the rules each frame).
    bool colorField(const char* label, float (&c)[3], int slot) {
        float* tmp = tmp_[slot];
        if (!editing_[slot]) { tmp[0] = c[0]; tmp[1] = c[1]; tmp[2] = c[2]; }
        if (ImGui::ColorEdit3(label, tmp, ImGuiColorEditFlags_NoInputs)) editing_[slot] = true;
        if (editing_[slot] && !ImGui::IsItemActive() && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) {
            editing_[slot] = false;
            c[0] = tmp[0]; c[1] = tmp[1]; c[2] = tmp[2];
            return true;
        }
        return false;
    }
    std::string message_;
    bool editing_[2] = {false, false};
    float tmp_[2][3] = {};
};

AIVERSE_EDITOR_WINDOW(FogWindow);

} // namespace aiverse::editor
