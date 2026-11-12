#include "ModelingUi.h"
#include "tools/modeling/Modeling.h"
#include "engine/render/AnimationGraph.h"
#include <algorithm>
#include <cmath>
namespace aiverse::editor {
class ModelingWindow final : public EditorWindow {
    ModelingLabels labels_;
    std::string title_ = "Моделирование", name_ = "robot", newPart_ = "part", selected_, editText_, message_, project_;
    Json state_;
    int64_t revision_ = -1;
    std::unique_ptr<render::Model> model_;
    render::Renderer3D renderer_;
    Viewport viewport_;
    gfx::Device *device_ = nullptr;
    modeling::GeometryCache cache_;
    render::Pose pose_;
    std::vector<render::mat4> world_;
    render::AnimationGraphInstance graph_;
    std::string clip_ = "Walk";
    float time_ = 0, yaw_ = .65f, pitch_ = .4f;
    bool registered_ = false, wire_ = false, skeleton_ = false, play_ = false, pinned_ = false, useGraph_ = false;
    double poll_ = 0;
    Json call(Editor &ed, const char *op, Json p) {
        auto r = ed.ws().call(op, p);
        message_ = r.has("error") ? r.getStr("error") : labels_.text("done");
        return r;
    }
    void load(Editor &ed) {
        pinned_ = false;
        auto r = call(ed, "modeling_get", modelParams(name_));
        if (r.has("error"))
            return;
        auto built = modeling::build(r["document"], nullptr, &cache_);
        if (!device_) {
            device_ = &ed.device();
            if (!renderer_.init(*device_))
                throw std::runtime_error("modeling renderer initialization failed");
            renderer_.setShadowQuality(render::ShadowQuality::Low);
        }
        if (!render::uploadModel(*device_, *built.model, true))
            throw std::runtime_error("modeling upload failed");
        graph_.reset({});
        if (model_)
            render::releaseModel(*device_, *model_);
        model_ = std::move(built.model);
        state_ = r;
        revision_ = r.getInt("revision");
        project_ = ed.ws().project.dir;
        time_ = 0;
        if (state_["document"]["graph"].isObject())
            graph_.reset(
                render::AnimationGraph::compile(*model_, modeling::evaluateDocument(state_["document"])["graph"]));
        render::bindPose(*model_, pose_);
        select(selected_);
    }
    void select(const std::string &id) {
        selected_ = id;
        editText_.clear();
        for (auto &n : state_["document"]["nodes"].items())
            if (n.getStr("id") == id)
                editText_ = n.dump(2);
    }
    void edit(Editor &ed, const Json &commands) {
        auto p = modelParams(name_, revision_);
        p.set("commands", commands);
        if (!call(ed, "modeling_edit", p).has("error"))
            load(ed);
    }
    void evaluate(float t) {
        if (!model_)
            return;
        render::bindPose(*model_, pose_);
        if (useGraph_ && graph_.graph()) {
            graph_.reset(
                render::AnimationGraph::compile(*model_, modeling::evaluateDocument(state_["document"])["graph"]));
            int frames = int(std::ceil(t * 60));
            for (int i = 0; i < frames; ++i)
                graph_.update(std::min(1.f / 60.f, t - float(i) / 60.f));
            graph_.evaluate(pose_);
        } else {
            int c = model_->findClip(clip_);
            if (c >= 0)
                render::sampleClip(model_->clips[size_t(c)], t, pose_);
        }
    }

  public:
    const char *id() const override { return "modeling"; }
    const char *title() const override { return title_.c_str(); }
    const char *group() const override { return "look"; }
    const char *dockArea() const override { return "center"; }
    bool openByDefault() const override { return false; }
    ~ModelingWindow() override {
        graph_.reset({});
        if (device_) {
            if (model_)
                render::releaseModel(*device_, *model_);
            viewport_.release(*device_);
            renderer_.shutdown();
        }
    }
    void update(Editor &ed, double dt) override {
        labels_.load(ed);
        title_ = labels_.text("title");
        if (!registered_) {
            registered_ = true;
            for (auto op : {"modeling_preview", "animation_preview"}) {
                Json schema;
                for (auto &item : ed.ws().ops())
                    if (item.name == op)
                        schema = item.schema;
                ed.ws().registerOp(
                    {op, "Show model source preview at revision; then view_screenshot {window: all} captures it.",
                     schema,
                     [this, &ed](const Json &p) {
                         auto name = p.getStr("name", name_);
                         bool changed = name != name_ || project_ != ed.ws().project.dir;
                         name_ = name;
                         auto latest = ed.ws().call("modeling_get", modelParams(name_));
                         if (latest.has("error"))
                             return latest;
                         if (changed || latest.getInt("revision") != revision_)
                             load(ed);
                         if (p.has("expectedRevision"))
                             modeling::require(p.getInt("expectedRevision") == revision_, "preview revision conflict");
                         clip_ = p.getStr("clip", clip_);
                         useGraph_ = p.getBool("useGraph", !p.has("clip"));
                         time_ = modeling::number(p["time"], 0, "time");
                         modeling::require(time_ >= 0 && time_ <= 60, "preview time must be 0..60");
                         yaw_ = modeling::number(p["yaw"], yaw_, "yaw");
                         pitch_ = std::clamp(modeling::number(p["pitch"], pitch_, "pitch"), -.9f, 1.4f);
                         wire_ = p.getBool("wireframe");
                         skeleton_ = p.getBool("skeleton");
                         play_ = false;
                         pinned_ = true;
                         evaluate(time_);
                         ed.focusWindow("modeling");
                         auto r = Json::object();
                         r.set("revision", Json::number(revision_));
                         r.set("next", Json::string("view_screenshot {window: all}"));
                         return r;
                     },
                     true});
            }
        }
        if (project_ != ed.ws().project.dir && model_) {
            graph_.reset({});
            render::releaseModel(*device_, *model_);
            model_.reset();
            state_ = Json{};
            revision_ = -1;
        }
        if (play_ && model_) {
            time_ += float(std::min(dt, .1));
            if (useGraph_ && graph_.graph()) {
                graph_.update(float(std::min(dt, .1)));
                graph_.evaluate(pose_);
            } else {
                int c = model_->findClip(clip_);
                if (c >= 0) {
                    float duration = model_->clips[size_t(c)].duration;
                    if (duration > 0)
                        time_ = std::fmod(time_, duration);
                    render::bindPose(*model_, pose_);
                    render::sampleClip(model_->clips[size_t(c)], time_, pose_);
                }
            }
        }
        poll_ += dt;
    }
    void draw(Editor &ed) override {
        try {
            auto tr = [&](const char *key) { return labels_.text(key); };
            if (ed.ws().project.dir.empty()) {
                ImGui::TextUnformatted(tr("openProject").c_str());
                return;
            }
            ImGui::SetNextItemWidth(150);
            ui::inputString("##asset", name_);
            ImGui::SameLine();
            if (ImGui::Button(tr("open").c_str()))
                load(ed);
            ImGui::SameLine();
            if (ImGui::Button(tr("create").c_str())) {
                if (!call(ed, "modeling_create", modelParams(name_)).has("error"))
                    load(ed);
            }
            ImGui::SameLine();
            if (ImGui::Button(tr("robot").c_str())) {
                auto p = modelParams(name_);
                p.set("preset", Json::string("robot"));
                if (!call(ed, "modeling_create", p).has("error"))
                    load(ed);
            }
            if (model_) {
                ImGui::SameLine();
                if (ImGui::Button(tr("undo").c_str())) {
                    if (!call(ed, "modeling_undo", modelParams(name_, revision_)).has("error"))
                        load(ed);
                }
                ImGui::SameLine();
                if (ImGui::Button(tr("redo").c_str())) {
                    if (!call(ed, "modeling_redo", modelParams(name_, revision_)).has("error"))
                        load(ed);
                }
                ImGui::SameLine();
                if (ImGui::Button(tr("export").c_str()))
                    call(ed, "modeling_export", modelParams(name_, revision_));
            }
            if (!message_.empty())
                ImGui::TextWrapped("%s", message_.c_str());
            if (!model_) {
                if (poll_ > 1) {
                    poll_ = 0;
                    auto found = ed.ws().call("modeling_get", modelParams(name_));
                    if (!found.has("error"))
                        load(ed);
                }
                return;
            }
            if (poll_ > 1 && !pinned_) {
                poll_ = 0;
                auto fresh = ed.ws().call("modeling_get", modelParams(name_));
                if (!fresh.has("error") && fresh.getInt("revision") != revision_)
                    load(ed);
            }
            {
                ModelingChild child("parts", ImVec2(220, 0), ImGuiChildFlags_Borders);
                ImGui::TextUnformatted(tr("parts").c_str());
                for (auto &n : model_->nodes) {
                    int depth = 0, p = n.parent;
                    while (p >= 0) {
                        ++depth;
                        p = model_->nodes[size_t(p)].parent;
                    }
                    ImGui::Indent(float(depth) * 8);
                    if (ImGui::Selectable(n.name.c_str(), selected_ == n.name))
                        select(n.name);
                    ImGui::Unindent(float(depth) * 8);
                }
                ui::inputString("##part", newPart_);
                if (ImGui::Button(tr("addBox").c_str())) {
                    edit(ed, putModelItem("nodes", newPart_, Json::parse(R"({"geometry":{"kind":"box"}})")));
                    select(newPart_);
                }
                if (!selected_.empty() && ImGui::Button(tr("remove").c_str())) {
                    auto c = Json::object(), a = Json::array();
                    c.set("op", Json::string("remove"));
                    c.set("collection", Json::string("nodes"));
                    c.set("id", Json::string(selected_));
                    a.push(c);
                    edit(ed, a);
                }
                if (ImGui::CollapsingHeader(tr("parameters").c_str())) {
                    auto params = state_["document"]["parameters"];
                    for (auto &[key, value] : params.members()) {
                        float f = float(value.toDouble());
                        if (ImGui::DragFloat(key.c_str(), &f, .01f)) {
                            auto c = Json::object(), values = Json::object(), a = Json::array();
                            values.set(key, Json::number(double(f), 6));
                            c.set("op", Json::string("parameters"));
                            c.set("values", values);
                            a.push(c);
                            edit(ed, a);
                        }
                    }
                }
            }
            ImGui::SameLine();
            {
                ModelingChild child("view", ImVec2(0, 0));
                if (graph_.graph()) {
                    if (ImGui::Checkbox(tr("graph").c_str(), &useGraph_))
                        evaluate(time_);
                }
                ImGui::Checkbox(tr("play").c_str(), &play_);
                ImGui::SameLine();
                if (ImGui::BeginCombo(tr("clip").c_str(), clip_.c_str())) {
                    for (auto &c : model_->clips)
                        if (ImGui::Selectable(c.name.c_str(), c.name == clip_)) {
                            clip_ = c.name;
                            time_ = 0;
                            evaluate(0);
                        }
                    ImGui::EndCombo();
                }
                float duration = 5;
                int ci = model_->findClip(clip_);
                if (ci >= 0)
                    duration = model_->clips[size_t(ci)].duration;
                if (ImGui::SliderFloat(tr("time").c_str(), &time_, 0, std::max(duration, .1f))) {
                    play_ = false;
                    evaluate(time_);
                }
                ImGui::Checkbox(tr("wireframe").c_str(), &wire_);
                ImGui::SameLine();
                ImGui::Checkbox(tr("skeleton").c_str(), &skeleton_);
                auto avail = ImGui::GetContentRegionAvail();
                float height = std::max(160.f, avail.y * .55f);
                viewport_.ensure(*device_, std::clamp(int(avail.x), 64, 1600), std::clamp(int(height), 64, 1000));
                render::RTSCamera cam;
                cam.target = (model_->boundsMin + model_->boundsMax) * .5f;
                cam.distance = std::max(2.f, render::length(model_->boundsMax - model_->boundsMin) * 1.15f);
                cam.minDistance = .01f;
                cam.yaw = yaw_;
                cam.pitch = pitch_;
                renderer_.beginScene(cam, viewport_.w, viewport_.h);
                render::worldMatrices(*model_, pose_, world_);
                render::ModelDraw draw;
                draw.model = model_.get();
                draw.nodeWorld = &world_;
                draw.pose = &pose_;
                renderer_.drawModel(draw);
                if (skeleton_)
                    for (size_t i = 0; i < model_->nodes.size(); ++i) {
                        int parent = model_->nodes[i].parent;
                        if (parent >= 0)
                            renderer_.line(world_[i].transformPoint({}), world_[size_t(parent)].transformPoint({}),
                                           0xff33ffff);
                    }
                if (wire_)
                    for (size_t ni = 0; ni < model_->nodes.size(); ++ni) {
                        auto &n = model_->nodes[ni];
                        if (n.mesh < 0 || n.skin >= 0)
                            continue;
                        for (auto &p : model_->meshes[size_t(n.mesh)].primitives)
                            for (uint32_t i = 0; i < p.indexCount; i += 3)
                                for (uint32_t k = 0; k < 3; ++k) {
                                    auto a = model_->vertices[model_->indices[p.firstIndex + i + k]].pos,
                                         b = model_->vertices[model_->indices[p.firstIndex + i + (k + 1) % 3]].pos;
                                    renderer_.line(world_[ni].transformPoint(a), world_[ni].transformPoint(b),
                                                   0xff55ddff);
                                }
                    }
                gfx::PassDesc pass;
                pass.colorTarget = viewport_.color;
                pass.depthTarget = viewport_.depth;
                pass.clearColor[0] = .08f;
                pass.clearColor[1] = .09f;
                pass.clearColor[2] = .12f;
                pass.clearColor[3] = 1;
                renderer_.render(pass);
                device_->endPass();
                viewport_.image(*device_, avail.x, height);
                if (ImGui::IsItemHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                    yaw_ += ImGui::GetIO().MouseDelta.x * .01f;
                    pitch_ = std::clamp(pitch_ + ImGui::GetIO().MouseDelta.y * .01f, -.9f, 1.4f);
                }
                if (!selected_.empty()) {
                    ImGui::TextUnformatted(tr("properties").c_str());
                    ui::inputMultiline("##node", editText_, ImVec2(-1, 100));
                    if (ImGui::Button(tr("apply").c_str()))
                        edit(ed, putModelItem("nodes", selected_, Json::parse(editText_)));
                }
            }
        } catch (const std::exception &e) {
            message_ = e.what();
        }
    }
};
AIVERSE_EDITOR_WINDOW(ModelingWindow);
} // namespace aiverse::editor
