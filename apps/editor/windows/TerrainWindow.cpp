// Terrain sculpting and ground materials (heightfield maps): brushes raise / lower / smooth / flatten
// / noise / set on the corner heights, painting the material layer, 16-bit PNG heightmap import /
// export. Strokes happen in the Scene view while a brush here is active (the same "external tool"
// convention as the Tiles window: ed.tool == Tool::Count). Agents use the same operations:
// terrain_sculpt, terrain_material, terrain_heights, heightmap_import / heightmap_export
// (core/TerrainOps.cpp).
#include "../Editor.h"
#include "../EditorUtil.h"
#include "../core/PlanetOps.h"
#include "../core/TerrainOps.h"

namespace rts::editor {

class TerrainWindow : public EditorWindow {
public:
    const char* id() const override { return "terrain"; }
    const char* title() const override { return "Рельеф"; }
    const char* group() const override { return "world"; }
    const char* dockArea() const override { return "right-bottom"; }
    int order() const override { return 5; }
    bool openByDefault() const override { return false; }

    void draw(Editor& ed) override {
        Workspace& ws = ed.ws();
        if (ws.project.dir.empty()) { ui::emptyStateHere(ICON_TERRAIN, "Игра не открыта", "Файл → Открыть проект"); return; }
        MapData& d = ws.map.data;
        const bool hf = !d.corners.empty();
        // what the map has: one dim line + hints
        {
            std::string info;
            if (hf) { char b[96]; std::snprintf(b, sizeof b, "Высоты %d×%d углов", d.w + 1, d.h + 1); info = b; info += ws.project.rules.terrain.heightfield() ? " · влияют на игру" : " · только вид"; }
            else info = "Карты высот ещё нет — первый мазок создаст её из текущего рельефа";
            if (!ed.planetMap() && d.detailScale > 0) { char b[64]; std::snprintf(b, sizeof b, " · детально ×%d", d.detailScale); info += b; }
            ui::hint(info.c_str());
            ui::help("Высоты влияют на игру, только если в rules.json terrain.model = \"heightfield\". Иначе рельеф только визуальный.");
        }
        if (ed.planetMap()) ui::callout(ui::Tone::Accent, "Карта с планетами: кисти работают на теле под курсором в «Сцене» (переходят через рёбра граней).");

        bool active = ed.tool == Tool::Count && owner_;
        ImGui::PushStyleColor(ImGuiCol_Text, active ? ui::toneColor(ui::Tone::Accent) : ImGui::GetStyle().Colors[ImGuiCol_Text]);
        if (ui::checkbox(ICON_BRUSH "  Кисть рельефа в «Сцене»", &active)) setActive(ed, active);
        ImGui::PopStyleColor();
        if (!ed.planetMap()) {
            ui::checkbox("Детальная карта высот", &detail_);
            ImGui::SetItemTooltip("Кисти работают по детальной карте высот (k точек на клетку, мягкий край), а не по углам клеток. "
                                  "Первый мазок создаёт её из того, что нарисовано сейчас. На картах heightfield углы клеток следуют за ней.");
        }
        static const char* opsLabels[] = {"Поднять", "Опустить", "Сгладить", "Выровнять", "Шум", "Задать высоту", "Материал", "Эрозия", "Штамп"};
        const int nOps = detail_ && !ed.planetMap() ? 9 : 7;
        if (mode_ >= nOps) mode_ = 0;
        if (ui::choiceGrid("##ops", mode_, opsLabels, nOps, 96)) setActive(ed, true);
        if (ui::beginProps("##brush")) {
            if (mode_ == 8) {
                static const char* stamps[] = {"Кратер", "Плато", "Дюны", "Скалы"};
                ui::prop("Штамп");
                ImGui::Combo("##stamp", &stamp_, stamps, 4);
                ui::prop("Поворот");
                ImGui::SliderFloat("##angle", &angle_, 0.0f, 360.0f, "%.0f°");
            }
            ui::prop("Радиус");
            ImGui::SliderFloat("##radius", &radius_, detail_ ? 0.25f : 0.5f, 32.0f, "%.2f");
            if (detail_) { ui::prop("Жёсткость", "0 — мягкий край, ближе к 1 — резкий"); ImGui::SliderFloat("##hard", &hardness_, 0.0f, 0.95f, "%.2f"); }
            if (mode_ <= 1 || mode_ == 4) { ui::prop("Сила"); ImGui::SliderFloat("##str", &strength_, 0.02f, 2.0f, "%.2f"); }
            else if (mode_ <= 3 || mode_ == 7) { ui::prop("Сила"); ImGui::SliderFloat("##blend", &blend_, 0.05f, 1.0f, "%.2f"); }
            else if (mode_ == 8) { ui::prop("Высота штампа"); ImGui::SliderFloat("##sh", &strength_, -4.0f, 4.0f, "%.2f"); }
            else if (mode_ == 5) {
                ui::prop("Высота", "Пипетка: следующий щелчок в «Сцене» берёт высоту оттуда.");
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - 4);
                ImGui::InputFloat("##target", &target_, 0.25f, 1.0f, "%.2f");
                ImGui::SameLine(0, 4);
                if (ui::toolButton(ICON_PIPETTE, "Пипетка: взять высоту щелчком в «Сцене»", pick_)) pick_ = !pick_;
            } else {
                ui::prop("Слой", "Индекс слоя из terrain.layers (rules.json / game.json). -1 стирает: материал по типу клетки.");
                ImGui::SliderInt("##layer", &layer_, -1, 3, layer_ < 0 ? "стереть" : "%d");
            }
            ui::endProps();
        }
        ui::hint("Shift — обратное действие (поднять ↔ опустить, стереть материал).");

        ui::section(ICON_IMAGE, "Карта высот PNG (16 бит)");
        if (ui::beginProps("##png")) {
            ui::prop("Файл");
            ui::inputString("##file", file_);
            ui::prop("Масштаб", "высота = смещение + значение / 65535 × масштаб (в клетках мира). Экспорт пишет масштаб и смещение в ответе и в PNG (tEXt).");
            ImGui::InputFloat("##scale", &scale_, 0.5f, 2.0f, "%.2f");
            ui::prop("Смещение");
            ImGui::InputFloat("##offset", &offset_, 0.25f, 1.0f, "%.2f");
            if (!ed.planetMap()) { ui::propWide(); ui::checkbox("Детальная (k точек на клетку)", &pngDetail_); }
            ui::endProps();
        }
        const int n = hf ? 3 : 2;
        if (ui::wideButton(ICON_DOWNLOAD, "Импорт", 0, n)) {
            Json p = Json::object();
            p.set("file", Json::string(file_)); p.set("scale", Json::number(double(scale_), 3)); p.set("offset", Json::number(double(offset_), 3));
            p.set("detail", Json::fromBool(pngDetail_));
            report(ed, ws.call("heightmap_import", p));
        }
        ImGui::BeginDisabled(!(hf || (pngDetail_ && d.detailScale > 0)));
        if (ui::wideButton(ICON_SAVE, "Экспорт", 1, n)) {
            Json p = Json::object();
            p.set("file", Json::string(file_));
            p.set("detail", Json::fromBool(pngDetail_));
            Json r = ws.call("heightmap_export", p);
            if (!r.has("error")) { scale_ = float(r["scale"].toDouble(scale_)); offset_ = float(r["offset"].toDouble(offset_)); }
            report(ed, r);
        }
        ImGui::EndDisabled();
        if (!(hf || (pngDetail_ && d.detailScale > 0)))
            ImGui::SetItemTooltip("Нечего экспортировать: сначала нарисуйте рельеф кистью или импортируйте PNG");
        if (hf && ui::wideButton(ICON_TRASH, "Удалить рельеф", 2, n)) {
            Json p = Json::object(); p.set("enabled", Json::fromBool(false));
            report(ed, ws.call("map_heightfield", p));
        }
        ui::resultMessage(message_);
    }

    void update(Editor& ed, double) override {
        if (ed.tool != Tool::Count) owner_ = false;
        if (!owner_ || ed.playing() || !ed.sceneVisible()) { painting_ = false; return; }
        ImGuiIO& io = ImGui::GetIO();
        Viewport& vp = *ed.sceneViewport;
        if (!io.MouseDown[0]) { painting_ = false; stamped_ = false; return; }
        if (stamped_) return;
        if (!painting_ && !(io.MouseClicked[0] && vp.hovered)) return;
        if (ed.planetMap()) { planetStroke(ed, vp, io); return; }
        float x, z;
        if (!ed.groundAt(vp, ed.sceneCam, io.MousePos.x, io.MousePos.y, x, z)) return;
        if (pick_) { target_ = ed.view().heightAt(ed.editGrid(), x, z); pick_ = false; return; }
        // dabs every ~1/4 radius along the stroke, at most ~30 per second
        const double now = ed.time();
        if (painting_ && (std::hypot(x - lastX_, z - lastZ_) < radius_ * 0.25f && now - lastT_ < 0.033)) return;
        if (!painting_) ed.doc().beginEdit();
        painting_ = true;
        lastX_ = x; lastZ_ = z; lastT_ = now;
        const bool inv = io.KeyShift;
        const float lh = float(ed.rules().levelHeight.toDouble());
        if (detail_) { // sub-cell brushes (TerrainDetailOps; one undo step per stroke, begun above)
            if (mode_ == 6) { paintDetailMaterial(ed.ws(), x, z, radius_, std::max(0, layer_), inv ? 0.0f : 0.35f, hardness_); return; }
            Sculpt op = mode_ == 0 ? (inv ? Sculpt::Lower : Sculpt::Raise) : mode_ == 1 ? (inv ? Sculpt::Raise : Sculpt::Lower)
                      : mode_ == 2 ? Sculpt::Smooth : mode_ == 3 ? Sculpt::Flatten : mode_ == 4 ? Sculpt::Noise : mode_ == 5 ? Sculpt::Set
                      : mode_ == 7 ? Sculpt::Erode : Sculpt::Stamp;
            const float s = mode_ <= 1 || mode_ == 4 ? strength_ * 0.25f : mode_ == 5 ? target_ : mode_ == 8 ? (inv ? -strength_ : strength_) : blend_ * 0.5f;
            static const char* stamps[] = {"crater", "mesa", "dunes", "rocks"};
            int x0, y0, x1, y1;
            std::string err;
            if (!detailSculpt(ed.ws(), op, {{x, z}}, radius_, s, hardness_, seed_, stamps[stamp_], angle_ * 0.0174533f, x0, y0, x1, y1, &err)) ed.status = err;
            if (mode_ == 8) stamped_ = true;   // one stamp per click
            return;
        }
        if (mode_ == 6) { paintMaterial(ed.doc(), int(std::floor(x)), int(std::floor(z)), int(radius_), inv ? -1 : layer_); return; }
        Sculpt op = mode_ == 0 ? (inv ? Sculpt::Lower : Sculpt::Raise) : mode_ == 1 ? (inv ? Sculpt::Raise : Sculpt::Lower)
                  : mode_ == 2 ? Sculpt::Smooth : mode_ == 3 ? Sculpt::Flatten : mode_ == 4 ? Sculpt::Noise : Sculpt::Set;
        float s = mode_ <= 1 || mode_ == 4 ? strength_ * 0.25f : mode_ == 5 ? target_ : blend_ * 0.5f; // per dab
        sculpt(ed.doc(), lh, op, x, z, radius_, s, seed_);
    }

private:
    // planets: the same brushes as 3D dabs on the body under the cursor (PlanetOps, crosses face seams)
    void planetStroke(Editor& ed, Viewport& vp, ImGuiIO& io) {
        int body; render::float3 local;
        if (!ed.planetAt(vp, io.MousePos.x, io.MousePos.y, body, local)) return;
        const render::float3 dir = normalize(local);
        const game::PlanetSurface* s = ed.view().planets().surface(body);
        if (pick_) { target_ = s ? s->heightAt(dir) : 0; pick_ = false; return; }
        const double now = ed.time();
        const float cellAngle = s ? 1.5708f / float(s->faceSize()) : 0.05f;
        if (painting_ && std::acos(std::clamp(dot(dir, lastDir_), -1.0f, 1.0f)) < radius_ * 0.25f * cellAngle && now - lastT_ < 0.033) return;
        if (!painting_) ed.doc().beginEdit();
        painting_ = true;
        lastDir_ = dir; lastT_ = now;
        const bool inv = io.KeyShift;
        const std::string id = ed.planetBodyId(body);
        if (mode_ == 6) { planetMaterial(ed.doc(), id, dir, radius_, inv ? -1 : layer_); return; }
        if (mode_ >= 7) { ed.status = "Эрозия / штамп: только на плоских картах"; return; }
        Sculpt op = mode_ == 0 ? (inv ? Sculpt::Lower : Sculpt::Raise) : mode_ == 1 ? (inv ? Sculpt::Raise : Sculpt::Lower)
                  : mode_ == 2 ? Sculpt::Smooth : mode_ == 3 ? Sculpt::Flatten : mode_ == 4 ? Sculpt::Noise : Sculpt::Set;
        const float st = mode_ <= 1 || mode_ == 4 ? strength_ * 0.25f : mode_ == 5 ? target_ : blend_ * 0.5f;
        if (detail_) { // the body's detail grid (sub-cell, created on first use; the corners follow it)
            std::string err;
            if (planetDetailSculpt(ed.ws(), id, op, dir, radius_, st, hardness_, seed_, &err) < 0) ed.status = err;
            return;
        }
        planetSculpt(ed.doc(), id, op, dir, radius_, st, seed_);
    }

    void setActive(Editor& ed, bool on) {
        owner_ = on;
        ed.tool = on ? Tool::Count : Tool::Select;
        if (on) ++seed_;
    }
    void report(Editor& ed, const Json& r) { message_ = r.has("error") ? "Ошибка: " + r.getStr("error") : r.getStr("summary"); ed.status = message_; }

    int mode_ = 0, layer_ = 0, stamp_ = 0;
    float radius_ = 4, strength_ = 0.5f, blend_ = 0.5f, target_ = 0, scale_ = 8, offset_ = 0, hardness_ = 0.2f, angle_ = 0;
    bool detail_ = true, pngDetail_ = false, stamped_ = false;
    std::string file_ = "maps/heightmap.png", message_;
    bool owner_ = false, painting_ = false, pick_ = false;
    float lastX_ = -1e9f, lastZ_ = -1e9f;
    render::float3 lastDir_;
    double lastT_ = 0;
    uint32_t seed_ = 1;
};

RTS_EDITOR_WINDOW(TerrainWindow);

} // namespace rts::editor
