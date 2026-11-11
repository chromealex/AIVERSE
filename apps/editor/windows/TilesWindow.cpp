// Tile tool for 2D tile maps (Red Alert / Dune II / Warcraft II style): tileset palette, brush,
// autotile on/off. Painting happens in the Scene view while this tool is active; the same edits
// are available to agents as tile_paint / tiles_init / tileset_list (core/TileOps.cpp).
#include "../Editor.h"
#include "../EditorUtil.h"
#include "../core/TileOps.h"
#include "engine/render/TextureCache.h"

namespace aiverse::editor {

class TilesWindow : public EditorWindow {
public:
    const char* id() const override { return "tiles"; }
    const char* title() const override { return "Тайлы"; }
    const char* group() const override { return "world"; }
    const char* dockArea() const override { return "right-bottom"; }
    int order() const override { return 4; }
    bool openByDefault() const override { return false; }

    ~TilesWindow() override { textures_.shutdown(); }

    void draw(Editor& ed) override {
        Workspace& ws = ed.ws();
        if (ws.project.dir.empty()) { ui::emptyStateHere(ICON_TILES, "Игра не открыта", "Файл → Открыть проект"); return; }
        std::string err;
        const game::Tileset* ts = mapTileset(ws, &err);
        if (!ts) { drawInit(ed, err); return; }
        ui::headerCard(ICON_TILES, ts->name, "Набор тайлов · " + ws.map.data.extra["tiles"].getStr("tileset"));
        bool active = ed.tool == Tool::Count;
        if (ui::checkbox(ICON_BRUSH "  Рисовать в «Сцене»", &active)) ed.tool = active ? Tool::Count : Tool::Select;
        ui::help("Левая кнопка мыши в «Сцене» рисует выбранной местностью. Края и углы подбираются автоматически по соседям (автотайлы).");
        ImGui::SameLine();
        ui::checkbox("Автотайлы", &autotile_);
        ui::help("Выключено: ставится ровно выбранный тайл атласа (ручная правка краёв, декор).");
        if (ui::beginProps("##brush")) {
            ui::prop("Кисть");
            ImGui::SliderInt("##b", &ed.brush, 0, 12, "%d клет.");
            ui::endProps();
        }
        terrain_ = std::clamp(terrain_, 0, int(ts->terrains.size()) - 1);

        ui::section(ICON_PALETTE, "Местность");
        for (size_t i = 0; i < ts->terrains.size(); ++i) {
            const auto& t = ts->terrains[i];
            ImVec4 c(t.color[0] / 255.0f, t.color[1] / 255.0f, t.color[2] / 255.0f, 1);
            ImGui::PushID(int(i));
            if (ImGui::ColorButton("##c", c, ImGuiColorEditFlags_NoTooltip, ImVec2(18, 18))) terrain_ = int(i);
            ImGui::SameLine();
            std::string label = t.name + "  [" + std::string(1, t.cell) + (t.level >= 0 ? " ур." + std::to_string(t.level) : "") +
                                (t.fieldResource.empty() ? "" : " " + t.fieldResource) + "]";
            if (ImGui::Selectable(label.c_str(), terrain_ == int(i))) { terrain_ = int(i); ed.tool = Tool::Count; }
            ImGui::PopID();
        }
        if (!autotile_) drawAtlas(ed, *ts);
    }

    void update(Editor& ed, double) override {
        if (ed.tool != Tool::Count || ed.playing() || !ed.sceneVisible()) { painting_ = false; return; }
        ImGuiIO& io = ImGui::GetIO();
        Viewport& vp = *ed.sceneViewport;
        if (!io.MouseDown[0]) { painting_ = false; lastX_ = lastY_ = -9999; return; }
        if (!painting_ && !(io.MouseClicked[0] && vp.hovered)) return;
        float x, z;
        if (!ed.groundAt(vp, ed.sceneCam, io.MousePos.x, io.MousePos.y, x, z)) return;
        int cx = int(std::floor(x)), cy = int(std::floor(z));
        if (painting_ && cx == lastX_ && cy == lastY_) return;
        if (!mapTileset(ed.ws(), nullptr)) return;
        if (!painting_) ed.doc().beginEdit();
        painting_ = true;
        lastX_ = cx; lastY_ = cy;
        try { paintTiles(ed.ws(), cx, cy, ed.brush, terrain_, autotile_ ? -1 : tile_); }
        catch (const std::exception& e) { ed.status = e.what(); painting_ = false; }
    }

private:
    void drawInit(Editor& ed, const std::string& err) {
        Workspace& ws = ed.ws();
        if (ws.map.data.extra["tiles"].isObject()) ui::callout(ui::Tone::Error, err);
        else ui::callout(ui::Tone::Normal, "У карты нет слоя тайлов. Тайлы нужны 2D-играм с тайловой картой (Red Alert, Dune II, Warcraft II).");
        auto sets = listTilesets(ws);
        if (sets.empty()) {
            ui::hint("В игре нет наборов тайлов (tilesets/*.json). Готовый набор есть в шаблоне «Crystal Dunes» (templates/tiles2d): "
                     "скопируйте его папку tilesets/ с картинками в папку игры — набор появится здесь.");
            return;
        }
        pick_ = std::clamp(pick_, 0, int(sets.size()) - 1);
        if (ui::beginProps("##init")) {
            ui::prop("Набор");
            if (ImGui::BeginCombo("##set", sets[size_t(pick_)].c_str())) {
                for (size_t i = 0; i < sets.size(); ++i) if (ImGui::Selectable(sets[i].c_str(), pick_ == int(i))) pick_ = int(i);
                ImGui::EndCombo();
            }
            ui::propWide();
            ui::checkbox("По текущему рельефу", &fromTerrain_);
            ImGui::SetItemTooltip("Подобрать местность каждой клетке по её типу (вода, скалы...), иначе залить первой местностью набора.");
            ui::endProps();
        }
        if (ui::wideButton(ICON_PLUS, "Создать слой тайлов", 0, 1, true)) {
            Json p = Json::object();
            p.set("tileset", Json::string(sets[size_t(pick_)]));
            p.set("fromTerrain", Json::fromBool(fromTerrain_));
            Json r = ws.call("tiles_init", p);
            if (r.has("error")) ed.status = "Ошибка: " + r.getStr("error");
        }
    }

    void drawAtlas(Editor& ed, const game::Tileset& ts) {
        ui::section(ICON_GRID, "Тайл атласа");
        if (ui::beginProps("##tile")) { ui::prop("Номер"); ImGui::InputInt("##n", &tile_); ui::endProps(); }
        tile_ = std::max(0, tile_);
        if (!texInit_) { textures_.init(ed.device()); texInit_ = true; }
        render::TextureRef a = textures_.resolve(ts.atlas, true, ed.ws().project.dir + "/");
        if (!a.valid()) { ImGui::TextDisabled("атлас не найден: %s", ts.atlas.c_str()); return; }
        const int rows = a.height / std::max(1, ts.tileH);
        const float cell = 28;
        const float avail = ImGui::GetContentRegionAvail().x;
        int perLine = std::max(1, int(avail / (cell + 4)));
        int n = 0;
        for (int r = 0; r < rows; ++r)
            for (int c = 0; c < ts.columns; ++c, ++n) {
                float u0 = float(c * ts.tileW) / a.width, v0 = float(r * ts.tileH) / a.height;
                float u1 = u0 + float(ts.tileW) / a.width, v1 = v0 + float(ts.tileH) / a.height;
                ImGui::PushID(n);
                if (n % perLine) ImGui::SameLine(0, 4);
                bool sel = n == tile_;
                if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ui::accentColor());
                if (ImGui::ImageButton("##t", ImTextureRef(ImTextureID(a.tex.id)), ImVec2(cell, cell), ImVec2(a.u(u0), a.v(v0)), ImVec2(a.u(u1), a.v(v1)))) { tile_ = n; ed.tool = Tool::Count; }
                if (sel) ImGui::PopStyleColor();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%d", n);
                ImGui::PopID();
            }
    }

    int terrain_ = 0, tile_ = 0, pick_ = 0;
    bool autotile_ = true, fromTerrain_ = true, painting_ = false, texInit_ = false;
    int lastX_ = -9999, lastY_ = -9999;
    render::TextureCache textures_;
};

AIVERSE_EDITOR_WINDOW(TilesWindow);

} // namespace aiverse::editor
