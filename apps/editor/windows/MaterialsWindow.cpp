// Materials: the game's material library (materials.json) with a live preview on a sphere / box,
// property editing, procedural textures (texture_generate) and applying a material to a type's
// model by material name or node (material_apply). Every change goes through the material_* /
// texture_* operations, exactly what agents call over MCP; `material_preview` shows a material here
// so an agent can look at it with view_screenshot.
#include "../Editor.h"
#include "../EditorIcons.h"
#include "../EditorUtil.h"
#include "../core/MaterialOps.h"
#include "engine/render/TextureGen.h"
#include "modules/rts/view/RtsPortrait.h"

namespace rts::editor {

namespace {
float numOf(const Json& j, float def) { return j.isNumber() ? float(j.toDouble(def)) : def; }
void color3(const Json& j, float out[3], float def) {
    for (int i = 0; i < 3; ++i) out[i] = j.size() >= 3 ? float(j[size_t(i)].toDouble(def)) : def;
}
Json arr(const float* v, int n) {
    Json a = Json::array();
    for (int i = 0; i < n; ++i) a.push(Json::number(double(v[i]), 3));
    return a;
}
} // namespace

class MaterialsWindow : public EditorWindow {
public:
    const char* id() const override { return "materials"; }
    const char* title() const override { return "Материалы"; }
    const char* group() const override { return "world"; }
    const char* icon() const override { return ICON_PALETTE; }
    const char* dockArea() const override { return "right-bottom"; }
    int order() const override { return 4; }
    bool openByDefault() const override { return false; }

    ~MaterialsWindow() override { preview_.shutdown(); }

    void update(Editor& ed, double dt) override {
        time_ += dt;
        if (registered_) return;
        registered_ = true;
        ed.ws().registerOp({"material_preview",
            "Show a material in the editor's Materials window on a sphere or a box (library name or inline material object), "
            "then call view_screenshot to look at it. Without material: the window shows its selection.",
            Json::parse(R"({"properties": {"material": {"description": "library name or material object"},
                "shape": {"type": "string", "enum": ["sphere", "box"]}}})"),
            [this, &ed](const Json& p) {
                if (p.has("material")) {
                    if (p["material"].isString()) select(ed, p["material"].str());
                    else { selected_.clear(); edit_ = p["material"]; }
                }
                if (p.has("shape")) shape_ = p.getStr("shape") == "box" ? 1 : 0;
                ed.focusWindow("materials");
                Json r = Json::object();
                r.set("ok", Json::fromBool(true));
                r.set("summary", Json::string("Материал показан в окне «Материалы»: view_screenshot покажет его"));
                return r;
            }, true});
    }

    void draw(Editor& ed) override {
        if (ed.ws().project.dir.empty()) { ImGui::TextDisabled("Нет открытой игры"); return; }
        Json lib;
        try { lib = loadMaterialLibrary(ed.ws()); }
        catch (const std::exception& e) { ui::toneText(ui::Tone::Error, "%s", e.what()); return; }

        // ---- library list
        const float createW = ImGui::CalcTextSize("+  Создать").x + ImGui::GetStyle().FramePadding.x * 2;
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - createW - ImGui::GetStyle().ItemSpacing.x);
        ui::inputHint("##newname", "имя нового материала (латиница)", newName_);
        ImGui::SameLine();
        ImGui::BeginDisabled(newName_.empty());
        const bool create = ui::button(ICON_PLUS, "Создать");
        ImGui::EndDisabled();
        if (create && !newName_.empty()) {
            Json m = Json::object();
            m.set("color", Json::parse("[0.8, 0.8, 0.8]"));
            m.set("roughness", Json::number(0.6, 2));
            call(ed, "material_set", nameParams(newName_, m));
            select(ed, newName_);
            newName_.clear();
        }
        ImGui::BeginChild("##lib", ImVec2(0, lib.size() == 0 ? ImGui::GetFrameHeightWithSpacing() * 1.6f : 110), ImGuiChildFlags_Borders);
        for (const auto& [name, m] : lib.members()) {
            std::string label = name + (m.has("base") ? "  ← " + m.getStr("base") : "");
            if (ImGui::Selectable(label.c_str(), name == selected_)) select(ed, name);
            if (m.has("description") && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", m.getStr("description").c_str());
        }
        if (lib.size() == 0) ImGui::TextDisabled("Библиотека пуста: введите имя и нажмите «Создать»");
        ImGui::EndChild();

        // ---- preview
        if (!previewInit_) { previewInit_ = true; preview_.init(ed.device(), 256); }
        if (!edit_.isObject() && !edit_.isString()) edit_ = Json::object();
        preview_.renderMaterial(ed.view(), edit_, shape_, time_);
        if (preview_.texture().valid()) {
            const bool flip = ed.device().info().renderTargetFlipY;
            const float side = std::clamp(ImGui::GetContentRegionAvail().x, 120.0f, 220.0f);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - side) * 0.5f);
            ImGui::Image(ImTextureRef(ImTextureID(preview_.texture().id)), ImVec2(side, side), ImVec2(0, flip ? 0.f : 1.f), ImVec2(1, flip ? 1.f : 0.f));
        }
        static const char* shapes[] = {"Шар", "Куб"};
        ui::segmented("##shape", shape_, shapes, 2);
        ImGui::TextDisabled("%s", selected_.empty() ? "Материал не сохранён" : selected_.c_str());

        // ---- properties: widgets show the effective value (own key, else inherited through "base"),
        // a change writes the own key only
        if (!edit_.isObject()) return;
        Json eff = edit_;
        {
            std::string base = edit_.getStr("base");
            for (int depth = 0; depth < 8 && !base.empty() && lib[base].isObject(); ++depth) {
                for (const auto& [k, v] : lib[base].members()) if (!eff.has(k) && k != "base") eff.set(k, v);
                base = lib[base].getStr("base");
            }
        }
        bool changed = false;
        if (ui::beginProps("##mat")) {
            const std::string cur = edit_.getStr("base");
            ui::prop("База", "Материал, от которого наследуются незаданные свойства");
            if (ImGui::BeginCombo("##base", cur.empty() ? "(нет)" : cur.c_str())) {
                if (ImGui::Selectable("(нет)", cur.empty())) { edit_.erase("base"); changed = true; }
                for (const auto& [name, m] : lib.members())
                    if (name != selected_ && ImGui::Selectable(name.c_str(), name == cur)) { edit_.set("base", Json::string(name)); changed = true; }
                ImGui::EndCombo();
            }
            float c[3];
            color3(eff["color"], c, 1);
            ui::prop("Цвет");
            if (ImGui::ColorEdit3("##color", c)) { edit_.set("color", arr(c, 3)); changed = true; }
            float metal = numOf(eff["metallic"], 0), rough = numOf(eff["roughness"], 0.8f);
            ui::prop("Металл");
            if (ImGui::SliderFloat("##metal", &metal, 0, 1)) { edit_.set("metallic", Json::number(double(metal), 2)); changed = true; }
            ui::prop("Шероховатость");
            if (ImGui::SliderFloat("##rough", &rough, 0, 1)) { edit_.set("roughness", Json::number(double(rough), 2)); changed = true; }
            float e[3];
            color3(eff["emissive"], e, 0);
            ui::prop("Свечение");
            if (ImGui::ColorEdit3("##emissive", e)) { edit_.set("emissive", arr(e, 3)); changed = true; }
            static const char* alphaKeys[] = {"opaque", "mask", "blend"};
            static const char* alphaNames[] = {"непрозрачный", "вырез по маске", "полупрозрачный"};
            int ai = 0;
            for (int i = 0; i < 3; ++i) if (eff.getStr("alpha", "opaque") == alphaKeys[i]) ai = i;
            ui::prop("Прозрачность", "alpha: opaque | mask | blend");
            if (ImGui::Combo("##alpha", &ai, alphaNames, 3)) { edit_.set("alpha", Json::string(alphaKeys[ai])); changed = true; }
            static const char* teams[] = {"(как в модели)", "tint", "mask", "alpha", "vertex", "none"};
            static const char* teamNames[] = {"как в модели", "оттенок (tint)", "по маске (mask)", "по альфе (alpha)", "по вершинам (vertex)", "нет (none)"};
            int ti = 0;
            for (int i = 1; i < 6; ++i) if (eff.getStr("team") == teams[i]) ti = i;
            ui::prop("Цвет игрока", "team: как цвет игрока ложится на материал");
            if (ImGui::Combo("##team", &ti, teamNames, 6)) { if (ti == 0) edit_.erase("team"); else edit_.set("team", Json::string(teams[ti])); changed = true; }
            bool two = eff.getBool("doubleSided"), unlit = eff.getBool("unlit");
            ui::propWide();
            if (ui::checkbox("Двусторонний", &two)) { edit_.set("doubleSided", Json::fromBool(two)); changed = true; }
            ui::propWide();
            if (ui::checkbox("Без освещения", &unlit)) { edit_.set("unlit", Json::fromBool(unlit)); changed = true; }
            static const char* texKeys[] = {"texture", "normalMap", "emissiveMap", "teamMask"};
            static const char* texNames[] = {"Текстура", "Карта нормалей", "Карта свечения", "Маска игрока"};
            for (int i = 0; i < 4; ++i) {
                const char* k = texKeys[i];
                std::string v = edit_.getStr(k);
                const std::string inherited = !edit_.has(k) && eff.has(k) ? "база: " + eff.getStr(k) : std::string("путь к png");
                ui::prop(texNames[i], k);
                ImGui::PushID(k);
                if (ui::inputHint("##tex", inherited.c_str(), v, ImGuiInputTextFlags_EnterReturnsTrue)) { if (v.empty()) edit_.erase(k); else edit_.set(k, Json::string(v)); changed = true; }
                ImGui::PopID();
            }
            float uv[2] = {eff["uvScale"].size() == 2 ? float(eff["uvScale"][size_t(0)].toDouble(1)) : 1.0f,
                           eff["uvScale"].size() == 2 ? float(eff["uvScale"][size_t(1)].toDouble(1)) : 1.0f};
            ui::prop("Тайлинг", "uvScale: повтор текстуры по U и V");
            if (ImGui::DragFloat2("##uv", uv, 0.05f, 0.05f, 64)) { edit_.set("uvScale", arr(uv, 2)); changed = true; }
            ui::endProps();
        }
        (void)changed;
        if (!selected_.empty() && ui::wideButton(ICON_SAVE, "Сохранить", 0, 2, true)) call(ed, "material_set", nameParams(selected_, edit_));
        if (!selected_.empty()) {
            if (ui::wideButton(ICON_TRASH, "Удалить", 1, 2)) { Json p = Json::object(); p.set("name", Json::string(selected_)); call(ed, "material_delete", p); selected_.clear(); edit_ = Json::object(); }
        }

        // ---- procedural texture
        if (ui::collapsing("Текстура по узору")) {
            static std::vector<std::string> pats = render::texturePatterns();
            if (ImGui::BeginCombo("Узор", pats[size_t(pattern_)].c_str())) {
                for (size_t i = 0; i < pats.size(); ++i) if (ImGui::Selectable(pats[i].c_str(), int(i) == pattern_)) pattern_ = int(i);
                ImGui::EndCombo();
            }
            ImGui::ColorEdit3("Цвет 1", genA_);
            ImGui::ColorEdit3("Цвет 2", genB_);
            ImGui::SliderFloat("Масштаб", &genScale_, 1, 32);
            ImGui::SliderInt("Зерно", &genSeed_, 1, 99);
            auto gen = [&](const char* output, const char* key) {
                Json p = Json::object();
                p.set("pattern", Json::string(pats[size_t(pattern_)]));
                Json cols = Json::array();
                cols.push(arr(genA_, 3));
                cols.push(arr(genB_, 3));
                p.set("colors", cols);
                p.set("scale", Json::number(double(genScale_), 2));
                p.set("seed", Json::number(int64_t(genSeed_)));
                p.set("output", Json::string(output));
                Json r = call(ed, "texture_generate", p);
                if (r.has("path")) edit_.set(key, r["path"]);
            };
            if (ImGui::Button("Цвет → texture")) gen("color", "texture");
            ImGui::SameLine();
            if (ImGui::Button("Рельеф → normalMap")) gen("normal", "normalMap");
            ImGui::SameLine();
            if (ImGui::Button("Маска → teamMask")) gen("mask", "teamMask");
        }

        // ---- apply to a type
        if (!selected_.empty() && ui::collapsing("Назначить типу")) {
            const auto& types = ed.rules().types;
            if (!types.empty()) {
                applyType_ = std::clamp(applyType_, 0, int(types.size()) - 1);
                if (ImGui::BeginCombo("Тип", types[size_t(applyType_)].id.c_str())) {
                    for (size_t i = 0; i < types.size(); ++i) if (ImGui::Selectable(types[i].id.c_str(), int(i) == applyType_)) applyType_ = int(i);
                    ImGui::EndCombo();
                }
                ImGui::RadioButton("Материал модели", &applyByNode_, 0);
                ImGui::SameLine();
                ImGui::RadioButton("Узел", &applyByNode_, 1);
                ui::inputString(applyByNode_ ? "Узел (glob)" : "Имя или *", applyKey_);
                if (const render::Model* m = ed.view().modelOf(uint16_t(applyType_))) {
                    std::string hint;
                    if (applyByNode_) { for (const auto& n : m->nodes) hint += n.name + "  "; }
                    else { for (const auto& mt : m->materials) hint += mt.name + "  "; }
                    ImGui::TextWrapped("%s", hint.c_str());
                }
                if (ImGui::Button("Назначить")) {
                    Json p = Json::object();
                    p.set("type", Json::string(types[size_t(applyType_)].id));
                    p.set("material", Json::string(selected_));
                    p.set(applyByNode_ ? "node" : "target", Json::string(applyKey_.empty() ? "*" : applyKey_));
                    call(ed, "material_apply", p);
                }
            }
        }
    }

private:
    static Json nameParams(const std::string& name, const Json& m) {
        Json p = Json::object();
        p.set("name", Json::string(name));
        p.set("material", m);
        return p;
    }
    Json call(Editor& ed, const char* op, const Json& p) {
        Json r = ed.ws().call(op, p, "editor");
        if (r.has("error")) ed.status = r.getStr("error");
        return r;
    }
    void select(Editor& ed, const std::string& name) {
        selected_ = name;
        Json lib;
        try { lib = loadMaterialLibrary(ed.ws()); } catch (...) {}
        edit_ = lib[name].isObject() ? lib[name] : Json::object();
    }

    game::Portrait preview_;
    bool previewInit_ = false, registered_ = false;
    double time_ = 0;
    int shape_ = 0, pattern_ = 4, genSeed_ = 1, applyType_ = 0, applyByNode_ = 0;
    float genA_[3] = {0.55f, 0.27f, 0.2f}, genB_[3] = {0.78f, 0.75f, 0.7f}, genScale_ = 8;
    std::string selected_, newName_, applyKey_ = "*";
    Json edit_ = Json::object();
};

RTS_EDITOR_WINDOW(MaterialsWindow);

} // namespace rts::editor
