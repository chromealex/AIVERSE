// Overlays of the open game (rules.json / game.json "overlays", docs/game-format.md "Overlays"):
// list in draw order (built-ins + game entries), add from templates, edit the JSON of an entry,
// enable / disable, delete, and preview: checked overlays are drawn in the Scene and Game views
// regardless of their conditions (with a ghost of a chosen structure for placement overlays).
// Every change goes through the overlay_* operations (validated, saved, live in the play-test).
#include <algorithm>
#include "../Editor.h"
#include "../EditorUtil.h"
#include "modules/rts/view/RtsOverlayData.h"

namespace aiverse::editor {

class OverlaysWindow : public EditorWindow {
public:
    const char* id() const override { return "overlays"; }
    const char* title() const override { return "Оверлеи"; }
    const char* group() const override { return "look"; }
    const char* dockArea() const override { return "right-bottom"; }
    int order() const override { return 7; }
    bool openByDefault() const override { return false; }

    void update(Editor& ed, double) override {
        if (registered_) return;
        registered_ = true;
        Editor* e = &ed;
        ed.ws().registerOp({"overlay_preview",
            "Editor preview: draw these overlays in the Scene and Game views regardless of their 'when' (selected-entity overlays "
            "use every object when nothing is selected, event overlays spawn sample popups). placeType = a structure type id to "
            "preview ghost / placement overlays at the cursor (or the view centre); x, z move the Scene camera there. off=true ends "
            "the preview. Combine with view_screenshot.",
            Json::parse(R"({"properties": {"ids": {"type": "array", "items": {"type": "string"}}, "placeType": {"type": "string"},
                "x": {"type": "number"}, "z": {"type": "number"}, "off": {"type": "boolean"}}})"),
            [e](const Json& p) {
                if (p.getBool("off")) { e->overlayPreview.clear(); e->overlayPreviewType = -1; }
                else {
                    if (p["ids"].isArray()) {
                        e->overlayPreview.clear();
                        for (auto& i : p["ids"].items()) e->overlayPreview.push_back(i.str());
                    }
                    if (p.has("placeType")) {
                        const int t = e->rules().findType(p.getStr("placeType"));
                        if (t < 0 && !p.getStr("placeType").empty()) throw std::runtime_error("unknown type '" + p.getStr("placeType") + "'");
                        e->overlayPreviewType = t;
                    }
                    if (p["x"].isNumber() && p["z"].isNumber()) e->focusScene(float(p["x"].toDouble()), float(p["z"].toDouble()));
                }
                Json r = Json::object();
                Json ids = Json::array();
                for (auto& i : e->overlayPreview) ids.push(Json::string(i));
                r.set("ok", Json::fromBool(true));
                r.set("preview", ids);
                return r;
            }, true});
    }

    void draw(Editor& ed) override {
        auto& ws = ed.ws();
        if (ws.project.dir.empty()) { ui::emptyStateHere(ICON_LAYERS, "Игра не открыта", "Файл → Открыть проект"); return; }
        Json list = ws.call("overlay_list", Json::object());
        if (list.has("error")) { ui::callout(ui::Tone::Error, list.getStr("error")); return; }

        // ---- add from a template
        const Json tpl = game::overlayTemplates();
        const float addW = ImGui::CalcTextSize(ICON_PLUS "  Добавить").x + ImGui::GetStyle().FramePadding.x * 2;
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - addW - ImGui::GetStyle().ItemSpacing.x);
        const std::string cur = tpl[size_t(tplIndex_)].getStr("name");
        if (ImGui::BeginCombo("##tpl", cur.c_str())) {
            for (size_t i = 0; i < tpl.size(); ++i)
                if (ImGui::Selectable(tpl[i].getStr("name").c_str(), int(i) == tplIndex_)) tplIndex_ = int(i);
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tpl[size_t(tplIndex_)].getStr("description").c_str());
        ImGui::SameLine();
        if (ui::button(ICON_PLUS, "Добавить", true)) {
            Json p = Json::object();
            p.set("template", Json::string(tpl[size_t(tplIndex_)].getStr("id")));
            Json r = ws.call("overlay_set", p);
            message_ = r.has("error") ? r.getStr("error") : r.getStr("summary");
            if (!r.has("error")) select(ed, r.getStr("id"));
        }

        // ---- preview options
        ui::section(ICON_EYE, "Предпросмотр");
        ui::help("Оверлеи со значком глаза рисуются в «Сцене» и «Игре» без условий «when». Для оверлеев размещения (сетка, зона) выберите здание: "
                 "его призрак стоит под курсором. В «Сцене» инструмент «Объекты» тоже показывает оверлеи размещения.");
        const auto& R = ed.rules();
        const char* ghost = ed.overlayPreviewType >= 0 && size_t(ed.overlayPreviewType) < R.types.size() ? R.types[size_t(ed.overlayPreviewType)].name.c_str() : "— нет —";
        const bool props = ui::beginProps("##pv");
        if (props) ui::prop("Здание под курсором", "Призрак здания для оверлеев размещения");
        if (ImGui::BeginCombo("##ghost", ghost)) {
            if (ImGui::Selectable("— нет —", ed.overlayPreviewType < 0)) ed.overlayPreviewType = -1;
            for (size_t i = 0; i < R.types.size(); ++i)
                if (R.types[i].structure && ImGui::Selectable((R.types[i].name + "##" + R.types[i].id).c_str(), int(i) == ed.overlayPreviewType))
                    ed.overlayPreviewType = int(i);
            ImGui::EndCombo();
        }
        if (props) ui::endProps();

        // ---- list
        ui::section(ICON_LAYERS, "Оверлеи (порядок отрисовки)");
        ui::hint(ICON_EYE "  — показать в «Сцене» сейчас, без условий    " ICON_CHECK "  — включён в игре");
        const float listH = std::max(120.0f, ImGui::GetContentRegionAvail().y * 0.45f);
        ImGui::BeginChild("##list", ImVec2(0, listH), ImGuiChildFlags_Borders);
        for (auto& o : list["overlays"].items()) {
            const std::string id = o.getStr("id");
            ImGui::PushID(id.c_str());
            bool pv = std::find(ed.overlayPreview.begin(), ed.overlayPreview.end(), id) != ed.overlayPreview.end();
            if (ui::toolButton(pv ? ICON_EYE : ICON_EYE_OFF, pv ? "Предпросмотр без условий: вкл" : "Предпросмотр без условий: выкл", pv)) {
                if (!pv) ed.overlayPreview.push_back(id);
                else ed.overlayPreview.erase(std::remove(ed.overlayPreview.begin(), ed.overlayPreview.end(), id), ed.overlayPreview.end());
            }
            ImGui::SameLine();
            bool en = o.getBool("enabled", true);
            if (ui::checkbox("##en", &en)) {
                Json p = Json::object();
                Json e = Json::object();
                e.set("id", Json::string(id));
                e.set("enabled", Json::fromBool(en));
                p.set("overlay", e);
                p.set("merge", Json::fromBool(true));
                Json r = ws.call("overlay_set", p);
                message_ = r.has("error") ? r.getStr("error") : r.getStr("summary");
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Включён в игре");
            ImGui::SameLine();
            std::string label = (o.has("error") ? std::string(ICON_ALERT " ") : std::string()) + id;
            if (o.has("error")) ImGui::PushStyleColor(ImGuiCol_Text, ui::toneColor(ui::Tone::Error));
            ImGui::AlignTextToFramePadding();
            if (ImGui::Selectable(label.c_str(), id == selected_, 0, ImVec2(0, ImGui::GetFrameHeight()))) select(ed, id);
            {   // shape · origin, dim, at the right
                const std::string meta = o.getStr("shape") + " · " + o.getStr("origin");
                const ImVec2 mx = ImGui::GetItemRectMax(), mn = ImGui::GetItemRectMin();
                const float w = ImGui::CalcTextSize(meta.c_str()).x;
                if (mx.x - mn.x > ImGui::CalcTextSize(label.c_str()).x + w + 24)
                    ImGui::GetWindowDrawList()->AddText(ImVec2(mx.x - w - 6, mn.y + (mx.y - mn.y - ImGui::GetTextLineHeight()) * 0.5f), ImGui::GetColorU32(ImGuiCol_TextDisabled), meta.c_str());
            }
            if (o.has("error")) { ImGui::PopStyleColor(); if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", o.getStr("error").c_str()); }
            ImGui::PopID();
        }
        ImGui::EndChild();

        // ---- editor of the selected entry
        if (!selected_.empty()) {
            ui::section(ICON_PENCIL, ("Правка: " + selected_).c_str());
            static const char* targets[] = {"rules.json", "game.json"};
            if (ui::segmented("##target", target_, targets, 2)) select(ed, selected_);
            ui::help("Куда записать. Для встроенных оверлеев (selection, health_bars, ...) запись — это переопределение: достаточно "
                     "изменённых ключей, остальное берётся из значения по умолчанию.");
            ui::inputMultiline("##json", text_, ImVec2(-1, std::max(160.0f, ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() - (message_.empty() ? 6 : 40))));
            if (ui::wideButton(ICON_CHECK, "Применить", 0, 3, true)) {
                try {
                    Json p = Json::object();
                    p.set("overlay", Json::parse(text_));
                    p.set("target", Json::string(target_ ? "game" : "rules"));
                    Json r = ws.call("overlay_set", p);
                    message_ = r.has("error") ? r.getStr("error") : r.getStr("summary");
                } catch (const std::exception& e) { message_ = std::string("JSON: ") + e.what(); }
            }
            if (ui::wideButton(ICON_TRASH, "Удалить запись", 1, 3)) {
                Json p = Json::object();
                p.set("id", Json::string(selected_));
                p.set("target", Json::string(target_ ? "game" : "rules"));
                Json r = ws.call("overlay_delete", p);
                message_ = r.has("error") ? r.getStr("error") : r.getStr("summary");
                if (!r.has("error")) select(ed, selected_);
            }
            if (ui::wideButton(ICON_REFRESH, "Перечитать", 2, 3)) select(ed, selected_);
        }
        ui::resultMessage(message_);
    }

private:
    // Edit the entry as written in rules / game.json, or (built-in without an override) the default.
    void select(Editor& ed, const std::string& id) {
        selected_ = id;
        Json p = Json::object();
        p.set("id", Json::string(id));
        Json r = ed.ws().call("overlay_get", p);
        if (r.has("error")) { message_ = r.getStr("error"); text_.clear(); return; }
        const Json& own = target_ ? r["game"] : r["rules"];
        text_ = (own.isObject() ? own : r["overlay"]).dump(2);
    }

    bool registered_ = false;
    int tplIndex_ = 0, target_ = 0;
    std::string selected_, text_, message_;
};

AIVERSE_EDITOR_WINDOW(OverlaysWindow);

} // namespace aiverse::editor
