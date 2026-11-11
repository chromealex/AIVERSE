#include "ModelingUi.h"
#include <algorithm>
#include <map>
namespace aiverse::editor {
class AnimationWindow final : public EditorWindow {
    ModelingLabels labels_;
    std::string title_ = "Анимация", name_ = "robot", clip_ = "Walk", text_, graphText_, message_, project_;
    Json state_, clipData_;
    int channel_ = 0, key_ = 0;
    float time_ = 0, duration_ = 1;
    int fps_ = 30;
    bool initialized_ = false, registered_ = false;
    std::string requestedTab_;
    Json call(Editor &ed, const char *op, Json p) {
        auto r = ed.ws().call(op, p);
        message_ = r.has("error") ? r.getStr("error") : labels_.text("done");
        return r;
    }
    void read(Editor &ed) {
        auto r = call(ed, "animation_get", modelParams(name_));
        state_ = r;
        if (r.has("error")) {
            graphText_.clear();
            selectClip();
            return;
        }
        graphText_ = r["document"]["graph"].dump(2);
        selectClip();
    }
    void selectClip() {
        clipData_ = Json{};
        for (auto &c : state_["document"]["clips"].items())
            if (c.getStr("id") == clip_)
                clipData_ = c;
        text_ = clipData_.dump(2);
        channel_ = key_ = 0;
    }
    Json params() { return modelParams(name_, state_.getInt("revision")); }
    void preview(Editor &ed) {
        auto p = params();
        p.set("clip", Json::string(clip_));
        p.set("time", Json::number(double(time_), 6));
        call(ed, "animation_preview", p);
    }
    void saveClip(Editor &ed) {
        auto p = params();
        p.set("commands", putModelItem("clips", clip_, clipData_));
        if (!call(ed, "animation_edit", p).has("error"))
            read(ed);
    }

  public:
    const char *id() const override { return "animation_authoring"; }
    const char *title() const override { return title_.c_str(); }
    const char *group() const override { return "look"; }
    const char *dockArea() const override { return "bottom"; }
    bool openByDefault() const override { return false; }
    void update(Editor &ed, double) override {
        if (project_ != ed.ws().project.dir) {
            project_ = ed.ws().project.dir;
            state_ = Json{};
            clipData_ = Json{};
            initialized_ = false;
        }
        labels_.load(ed);
        title_ = labels_.text("animationTitle");
        if (!registered_) {
            registered_ = true;
            Json schema;
            for (auto &op : ed.ws().ops())
                if (op.name == "animation_editor")
                    schema = op.schema;
            ed.ws().registerOp({"animation_editor", "Open asset in clips or graph tab.", schema,
                                [this, &ed](const Json &p) {
                                    name_ = p.getStr("name", "robot");
                                    read(ed);
                                    requestedTab_ = p.getStr("tab", "clips");
                                    ed.focusWindow("animation_authoring");
                                    return state_;
                                },
                                true});
        }
    }
    void draw(Editor &ed) override {
        auto tr = [&](const char *k) { return labels_.text(k); };
        ui::inputString("##name", name_);
        ImGui::SameLine();
        if (ImGui::Button(tr("open").c_str()))
            read(ed);
        if (!initialized_ && !ed.ws().project.dir.empty()) {
            initialized_ = true;
            read(ed);
        }
        if (!message_.empty())
            ImGui::TextWrapped("%s", message_.c_str());
        if (!state_.isObject())
            return;
        if (ImGui::BeginTabBar("authoring")) {
            if (ImGui::BeginTabItem(tr("clips").c_str(), nullptr,
                                    requestedTab_ == "clips" ? ImGuiTabItemFlags_SetSelected : 0)) {
                if (ImGui::BeginCombo(tr("clip").c_str(), clip_.c_str())) {
                    for (auto &c : state_["document"]["clips"].items())
                        if (ImGui::Selectable(c.getStr("id").c_str(), clip_ == c.getStr("id"))) {
                            clip_ = c.getStr("id");
                            selectClip();
                        }
                    ImGui::EndCombo();
                }
                float end = 1;
                for (auto &c : clipData_["channels"].items())
                    for (auto &t : c["times"].items())
                        end = std::max(end, float(t.toDouble()));
                if (ImGui::SliderFloat(tr("time").c_str(), &time_, 0, end))
                    preview(ed);
                const auto &channels = clipData_["channels"];
                if (channels.size()) {
                    channel_ = std::clamp(channel_, 0, int(channels.size()) - 1);
                    auto label =
                        channels[size_t(channel_)].getStr("node") + " / " + channels[size_t(channel_)].getStr("path");
                    if (ImGui::BeginCombo(tr("channel").c_str(), label.c_str())) {
                        for (size_t i = 0; i < channels.size(); ++i) {
                            ImGui::PushID(int(i));
                            auto s = channels[i].getStr("node") + " / " + channels[i].getStr("path");
                            if (ImGui::Selectable(s.c_str(), channel_ == int(i))) {
                                channel_ = int(i);
                                key_ = 0;
                            }
                            ImGui::PopID();
                        }
                        ImGui::EndCombo();
                    }
                    auto &c = clipData_.member("channels").at(size_t(channel_));
                    int count = int(c["times"].size());
                    if (count) {
                        key_ = std::clamp(key_, 0, count - 1);
                        ImGui::SliderInt(tr("key").c_str(), &key_, 0, count - 1);
                        float t = float(c["times"][size_t(key_)].toDouble());
                        if (ImGui::DragFloat(tr("keyTime").c_str(), &t, .01f, 0, 600))
                            c.member("times").at(size_t(key_)) = Json::number(double(t), 6);
                        int valueIndex = c.getStr("interpolation") == "cubic" ? key_ * 3 + 1 : key_;
                        if (size_t(valueIndex) < c["values"].size()) {
                            auto &v = c.member("values").at(size_t(valueIndex));
                            float values[4] = {};
                            for (size_t i = 0; i < v.size() && i < 4; ++i)
                                values[i] = float(v[i].toDouble());
                            bool changed = v.size() == 4 ? ImGui::DragFloat4(tr("value").c_str(), values, .01f)
                                                         : ImGui::DragFloat3(tr("value").c_str(), values, .01f);
                            if (changed)
                                for (size_t i = 0; i < v.size() && i < 4; ++i)
                                    v.at(i) = Json::number(double(values[i]), 8);
                        }
                        auto start = ImGui::GetCursorScreenPos();
                        float width = std::max(100.f, ImGui::GetContentRegionAvail().x);
                        auto *dl = ImGui::GetWindowDrawList();
                        dl->AddLine({start.x, start.y + 20}, {start.x + width, start.y + 20},
                                    ImGui::GetColorU32(ImGuiCol_Border));
                        for (int k = 0; k < count; ++k) {
                            float x = start.x + float(c["times"][size_t(k)].toDouble()) / end * width;
                            dl->AddCircleFilled({x, start.y + 20}, k == key_ ? 5.f : 3.f,
                                                ImGui::GetColorU32(ImGuiCol_PlotLines));
                        }
                        ImGui::InvisibleButton("timeline", {width, 40});
                        if (ImGui::IsItemClicked()) {
                            float tClick = (ImGui::GetIO().MousePos.x - start.x) / width * end;
                            float best = 1e9f;
                            for (int k = 0; k < count; ++k) {
                                float d = std::abs(float(c["times"][size_t(k)].toDouble()) - tClick);
                                if (d < best) {
                                    best = d;
                                    key_ = k;
                                }
                            }
                            time_ = float(c["times"][size_t(key_)].toDouble());
                            preview(ed);
                        }
                    }
                    if (ImGui::Button(tr("addKey").c_str())) {
                        if (c.getStr("interpolation") != "cubic" && c["values"].size()) {
                            float nextTime = float(c["times"][c["times"].size() - 1].toDouble()) + .1f;
                            c.member("times").push(Json::number(double(nextTime), 6));
                            c.member("values").push(c["values"][size_t(key_)]);
                            key_ = int(c["times"].size()) - 1;
                        }
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(tr("deleteKey").c_str()) && c["times"].size() > 1 &&
                        c.getStr("interpolation") != "cubic") {
                        c.member("times").eraseAt(size_t(key_));
                        c.member("values").eraseAt(size_t(key_));
                        key_ = 0;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(tr("saveKeys").c_str())) {
                        try {
                            saveClip(ed);
                        } catch (const std::exception &e) {
                            message_ = e.what();
                        }
                    }
                }
                if (ImGui::CollapsingHeader(tr("json").c_str())) {
                    ui::inputMultiline("##clipJson", text_, ImVec2(-1, 160));
                    if (ImGui::Button(tr("apply").c_str())) {
                        try {
                            clipData_ = Json::parse(text_);
                            saveClip(ed);
                        } catch (const std::exception &e) {
                            message_ = e.what();
                        }
                    }
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(tr("graph").c_str(), nullptr,
                                    requestedTab_ == "graph" ? ImGuiTabItemFlags_SetSelected : 0)) {
                bool openGraph = ImGui::Button(tr("openGraphEditor").c_str());
                ImGui::SameLine();
                bool rig = ImGui::Button(tr("openRigEditor").c_str());
                if (openGraph || rig) {
                    auto p = params();
                    auto existing = ed.ws().call("animation_graph_get", p);
                    if (existing.has("error"))
                        existing = call(ed, "modeling_graph_publish", p);
                    if (!existing.has("error"))
                        call(ed, rig ? "rig_editor_open" : "animation_editor_open", p);
                }
                if (ImGui::Button(tr("importGraph").c_str())) {
                    if (!call(ed, "modeling_graph_import", params()).has("error"))
                        read(ed);
                }
                ui::inputMultiline("##graphJson", graphText_, ImVec2(-1, 180));
                if (ImGui::Button(tr("applyGraph").c_str())) {
                    try {
                        auto p = params();
                        p.set("graph", Json::parse(graphText_));
                        if (!call(ed, "modeling_graph_set", p).has("error"))
                            read(ed);
                    } catch (const std::exception &e) {
                        message_ = e.what();
                    }
                }
                ImGui::DragFloat(tr("duration").c_str(), &duration_, .1f, .01f, 60);
                ImGui::SliderInt(tr("fps").c_str(), &fps_, 1, 120);
                if (ImGui::Button(tr("bake").c_str())) {
                    auto p = params();
                    p.set("clip", Json::string("Baked"));
                    p.set("duration", Json::number(double(duration_), 6));
                    p.set("fps", Json::number(int64_t(fps_)));
                    if (!call(ed, "animation_bake", p).has("error"))
                        read(ed);
                }
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
            requestedTab_.clear();
        }
    }
};
AIVERSE_EDITOR_WINDOW(AnimationWindow);
} // namespace aiverse::editor
