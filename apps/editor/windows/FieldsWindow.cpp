// Grid fields (docs/fields.md): the fields of the open game (rules.json "fields" + map "gridFields"), their
// kernels / effects, and a brush that paints the map's initial values in the Scene view (fire to start with,
// water pools, fuel, creep per player). Same "external tool" convention as the Terrain window
// (ed.tool == Tool::Count). Agents use field_list / field_get / field_paint / field_set_rules (core/FieldOps.cpp).
#include "../Editor.h"
#include "../EditorUtil.h"
#include "../core/FieldOps.h"
#include <cmath>

namespace rts::editor {

class FieldsWindow : public EditorWindow {
public:
    const char* id() const override { return "fields"; }
    const char* title() const override { return "Поля"; }
    const char* group() const override { return "world"; }
    const char* dockArea() const override { return "right"; }
    int order() const override { return 6; }
    bool openByDefault() const override { return false; }

    void draw(Editor& ed) override {
        Workspace& ws = ed.ws();
        if (ws.project.dir.empty()) { ui::emptyStateHere(ICON_FLAME, "Игра не открыта", "Файл → Открыть проект"); return; }
        std::vector<game::GridFieldDef> defs;
        try { defs = mapFieldDefs(ws.project.rules, ws.map); }
        catch (const std::exception& e) { ui::callout(ui::Tone::Error, std::string("Ошибка в полях карты: ") + e.what()); return; }
        if (defs.empty()) {
            ui::emptyStateHere(ICON_FLAME, "В игре нет клеточных полей",
                               "Добавьте в rules.json блок \"fields\" (огонь, вода, крип, влияние…) или вызовите field_set_rules.");
            return;
        }
        if (sel_ >= int(defs.size())) sel_ = 0;
        const game::GridFieldDef& d = defs[size_t(sel_)];
        static const char* kk[] = {"decay", "diffuse", "spread", "flow", "consume"};
        static const char* ek[] = {"damage", "speed", "blockMove", "blockVision", "status", "yield", "reveal"};
        std::string ks, es;
        for (auto& k : d.kernels) ks += std::string(ks.empty() ? "" : ", ") + kk[int(k.kind)];
        for (auto& e : d.effects) es += std::string(es.empty() ? "" : ", ") + ek[int(e.kind)];
        auto toUnits = [&](int32_t raw) { return d.fixed ? double(raw) / 65536.0 : double(raw); };
        const double lo = d.min != INT32_MIN ? toUnits(d.min) : -1000.0, hi = d.max != INT32_MAX ? toUnits(d.max) : 1000.0;
        if (ui::beginProps("##field")) {
            ui::prop("Поле");
            if (ImGui::BeginCombo("##f", d.id.c_str())) {
                for (size_t i = 0; i < defs.size(); ++i)
                    if (ImGui::Selectable(defs[i].id.c_str(), sel_ == int(i))) sel_ = int(i);
                ImGui::EndCombo();
            }
            ui::prop("Значения");
            ImGui::TextDisabled("%s, %d клет./ячейку%s", d.fixed ? "дробные" : "целые", d.resolution, d.perPlayer ? ", по игрокам" : "");
            ui::prop("Ядра");
            ImGui::TextDisabled("%s", ks.empty() ? "нет" : ks.c_str());
            ui::prop("Эффекты");
            ImGui::TextDisabled("%s", es.empty() ? "нет" : es.c_str());
            ui::endProps();
        }
        ui::section(ICON_BRUSH, "Кисть");
        bool active = ed.tool == Tool::Count && owner_;
        if (ImGui::Checkbox("Рисовать в «Сцене»", &active)) { owner_ = active; ed.tool = active ? Tool::Count : Tool::Select; }
        static const char* modes[] = {"Задать", "Добавить", "Максимум", "Стереть"};
        ui::segmented("##mode", mode_, modes, 4);
        if (ui::beginProps("##brush")) {
            ui::prop("Значение");
            float v = float(value_);
            if (ImGui::SliderFloat("##v", &v, float(lo), float(hi), d.fixed ? "%.2f" : "%.0f")) value_ = v;
            ui::prop("Радиус");
            ImGui::SliderInt("##r", &radius_, 0, 24, "%d клет.");
            if (d.perPlayer) { ui::prop("Игрок"); ImGui::SliderInt("##pl", &player_, 1, std::max(1, int(ws.map.data.players.size()))); }
            ui::endProps();
        }
        ui::hint("Shift — стереть. Начальные значения хранятся в карте (\"gridFields\".<id>.values); ядра и эффекты — в rules.json \"fields\" (docs/fields.md).");
        if (ui::wideButton(ICON_TRASH, "Очистить поле на всей карте")) {
            Json p = Json::object();
            p.set("id", Json::string(d.id)); p.set("mode", Json::string("erase")); p.set("player", Json::number(int64_t(player_)));
            Json r = Json::array(); r.push(Json::number(int64_t(0))); r.push(Json::number(int64_t(0)));
            r.push(Json::number(int64_t(ws.map.data.w))); r.push(Json::number(int64_t(ws.map.data.h)));
            p.set("rect", r);
            Json res = ws.call("field_paint", p);
            ed.status = res.has("error") ? "Ошибка: " + res.getStr("error") : res.getStr("summary");
        }
        id_ = d.id;
    }

    void update(Editor& ed, double) override {
        if (ed.tool != Tool::Count) owner_ = false;
        if (!owner_ || ed.playing() || !ed.sceneViewport || id_.empty()) { painting_ = false; return; }
        ImGuiIO& io = ImGui::GetIO();
        Viewport& vp = *ed.sceneViewport;
        if (!io.MouseDown[0]) { painting_ = false; return; }
        if (!painting_ && !(io.MouseClicked[0] && vp.hovered)) return;
        float x, z;
        if (!ed.groundAt(vp, ed.sceneCam, io.MousePos.x, io.MousePos.y, x, z)) return;
        const double now = ed.time();
        const int cx = int(std::floor(x)), cz = int(std::floor(z));
        if (painting_ && cx == lastX_ && cz == lastZ_ && now - lastT_ < 0.1) return;
        if (!painting_) ed.doc().beginEdit();   // one undo step per stroke
        painting_ = true;
        lastX_ = cx; lastZ_ = cz; lastT_ = now;
        std::string err;
        if (paintMapField(ed.doc(), ed.rules(), id_, cx, cz, radius_, value_, io.KeyShift ? 3 : mode_, player_, &err) < 0) ed.status = err;
    }

private:
    int sel_ = 0, mode_ = 0, radius_ = 2, player_ = 1;
    double value_ = 100;
    std::string id_;
    bool owner_ = false, painting_ = false;
    int lastX_ = -1, lastZ_ = -1;
    double lastT_ = 0;
};

RTS_EDITOR_WINDOW(FieldsWindow);

} // namespace rts::editor
