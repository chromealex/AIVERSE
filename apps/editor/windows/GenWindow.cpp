// Map generation ("Генерация"): preset + seed + players + symmetry + params -> generate into the open map
// (bake, undoable), regenerate with the next seed, or store a match-time "generate" block (a new map every
// match from the match seed). Agents use the same operations: gen_presets, map_generate (core/GenOps.cpp).
#include "../Editor.h"
#include "../EditorUtil.h"
#include "../core/GenOps.h"
#include "modules/rts/gen/MapGen.h"

namespace rts::editor {

class GenWindow : public EditorWindow {
public:
    const char* id() const override { return "generate"; }
    const char* title() const override { return "Генерация"; }
    const char* group() const override { return "world"; }
    const char* dockArea() const override { return "right-bottom"; }
    int order() const override { return 6; }
    bool openByDefault() const override { return false; }

    void draw(Editor& ed) override {
        Workspace& ws = ed.ws();
        if (ws.project.dir.empty()) { ui::emptyStateHere(ICON_DICE, "Игра не открыта", "Файл → Открыть проект"); return; }
        const Json presets = gen::presetList(ws.project.rulesJson);
        if (preset_ >= int(presets.size())) preset_ = 0;
        const Json& cur = presets[size_t(preset_)];
        const bool planet = cur["params"].getStr("shape") == "planet";
        static const char* syms[] = {"auto", "none", "point", "mirror", "mirror-y", "mirror4", "rot4", "rotational"};
        static const char* symNames[] = {"Авто", "Нет", "Центральная", "Зеркало по X", "Зеркало по Y", "Две оси", "Поворот на 90°", "Вращение"};
        if (ui::beginProps("##gen")) {
            ui::prop("Пресет");
            if (ImGui::BeginCombo("##preset", cur.getStr("title").c_str(), ImGuiComboFlags_HeightLarge)) {
                for (size_t i = 0; i < presets.size(); ++i) {
                    if (ImGui::Selectable((presets[i].getStr("title") + "##" + presets[i].getStr("id")).c_str(), int(i) == preset_)) { preset_ = int(i); params_.clear(); }
                    ImGui::SameLine(ImGui::GetFontSize() * 11);
                    ImGui::TextDisabled("%s%s", presets[i].getStr("genre").c_str(), presets[i].getStr("source") == "game" ? " · игра" : "");
                }
                ImGui::EndCombo();
            }
            if (planet) {
                ui::prop("Расстановка", "shared: все игроки на одной планете (поворотная симметрия вокруг полюсов); separate: у каждого своя одинаковая планета.");
                static const char* layouts[] = {"Одна планета", "Своя у каждого"};
                ImGui::Combo("##layout", &layout_, layouts, 2);
            }
            ui::prop("Seed", "Одинаковый seed — одинаковая карта");
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - 4);
            ImGui::InputScalar("##seed", ImGuiDataType_U64, &seed_);
            ImGui::SameLine(0, 4);
            if (ui::toolButton(ICON_DICE, "Случайный seed")) seed_ = uint64_t(ImGui::GetTime() * 1000003.0) ^ (seed_ * 6364136223846793005ull);
            ui::prop("Игроки");
            ImGui::SliderInt("##players", &players_, 1, 8);
            ui::prop("Симметрия", "Авто: 2 игрока — центральная, 4 — поворот на 90° (квадрат) или две оси, иначе — вращение. Симметрия даёт честные старты.");
            ImGui::Combo("##sym", &sym_, symNames, 8);
            ui::propWide();
            ui::checkbox("Объекты по образцу карты", &useTemplate_);
            ImGui::SetItemTooltip("Стартовый набор игрока 1 и нейтральные объекты открытой карты задают, что и сколько ставить. Иначе — по типам из rules.json.");
            ui::propWide();
            ui::checkbox("Оставить размер открытой карты", &keepSize_);
            ui::endProps();
        }
        (void)syms;
        if (planet) ui::hint("Пресет планеты строит всю карту-мир: тела из блока world открытой карты или солнце + планета + луна.");
        else {
            const std::string genre = cur.getStr("genre");
            const std::string g = genre.empty() || genre == "any" ? "Подходит любому жанру" : "Жанр: " + genre;
            ui::hint((g + (cur.getStr("source") == "game" ? " · пресет из rules.json" : "")).c_str());
        }
        if (ImGui::TreeNode("Параметры пресета (JSON)")) {
            if (params_.empty()) params_ = cur["params"].dump(-1);
            ui::inputMultiline("##params", params_, ImVec2(-1, 90));
            ui::hint("Переопределяют параметры пресета.");
            ImGui::TreePop();
        }
        if (params_.empty()) params_ = cur["params"].dump(-1);
        ImGui::Spacing();
        if (ui::wideButton(ICON_WAND, "Сгенерировать", 0, 2, true)) run(ed, "bake");
        if (ui::wideButton(ICON_DICE, "Ещё вариант", 1, 2)) { ++seed_; run(ed, "bake"); }
        if (ui::wideButton(ICON_PLAY, "Новая карта в каждом матче")) run(ed, "match");
        ImGui::SetItemTooltip("Записывает в карту блок generate с seed \"match\": каждый матч (и мультиплеер, и повтор) генерирует свою карту детерминированно.");
        if (ws.map.data.extra.has("generate")) {
            ui::callout(ui::Tone::Accent, "Карта генерируется при старте матча: " + ws.map.data.extra["generate"].dump(-1));
            if (ui::button(ICON_X, "Убрать генерацию при старте")) { ws.map.beginEdit(); ws.map.data.extra.erase("generate"); ws.map.touched(); }
        }
        if (!result_.empty()) { ImGui::Spacing(); ui::resultMessage(result_); }
        if (!detail_.empty()) ui::hint(detail_.c_str());
    }

private:
    void run(Editor& ed, const char* mode) {
        static const char* syms[] = {"auto", "none", "point", "mirror", "mirror-y", "mirror4", "rot4", "rotational"};
        const Json presets = gen::presetList(ed.ws().project.rulesJson);
        Json p = Json::object();
        p.set("preset", Json::string(presets[size_t(preset_)].getStr("id")));
        p.set("seed", Json::number(int64_t(seed_ & 0x7fffffffffffffffull)));
        p.set("players", Json::number(int64_t(players_)));
        p.set("symmetry", Json::string(syms[sym_]));
        p.set("mode", Json::string(mode));
        p.set("useTemplate", Json::fromBool(useTemplate_));
        p.set("keepSize", Json::fromBool(keepSize_));
        try { if (!params_.empty()) p.set("params", Json::parse(params_)); }
        catch (const std::exception& e) { result_ = std::string("Ошибка в параметрах: ") + e.what(); return; }
        if (presets[size_t(preset_)]["params"].getStr("shape") == "planet") {
            Json pp = p["params"].isObject() ? p["params"] : Json::object();
            pp.set("layout", Json::string(layout_ ? "separate" : "shared"));
            p.set("params", pp);
        }
        Json r = ed.ws().call("map_generate", p);
        result_ = r.has("error") ? "Ошибка: " + r.getStr("error") : r.getStr("summary");
        detail_.clear();
        const Json& g = r["generated"];
        if (g.isObject()) {
            detail_ = "симметрия " + g.getStr("symmetry") + ", пути между стартами " + g["paths"].dump(-1);
            if (g.has("objects")) detail_ += "\nобъекты " + g["objects"].dump(-1);
        }
        ed.status = result_;
    }
    int preset_ = 0, players_ = 2, sym_ = 0, layout_ = 0;
    uint64_t seed_ = 1;
    bool useTemplate_ = true, keepSize_ = false;
    std::string params_, result_, detail_;
};

RTS_EDITOR_WINDOW(GenWindow);

} // namespace rts::editor
