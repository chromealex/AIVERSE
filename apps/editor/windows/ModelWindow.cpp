// Models: the selected type as the game draws it (portrait with its idle clip), with what its model
// contains — clips, nodes, materials, variants, baked skinning — and the warnings of its view
// block. The editor-owned VisualAssetDependencyService watches model files,
// even with this panel closed. Agents can inspect assets via `model_info`.
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include "../Editor.h"
#include "../EditorUtil.h"
#include "modules/rts/view/RtsPortrait.h"
#include "engine/render/RenderResourceOwnerGroup.h"

namespace aiverse::editor {

namespace {
Json modelInfo(const render::Model& m) {
    Json r = Json::object();
    r.set("path", Json::string(m.path));
    Json clips = Json::array();
    for (const auto& c : m.clips) {
        Json cj = Json::object();
        cj.set("name", Json::string(c.name));
        cj.set("duration", Json::number(double(c.duration)));
        clips.push(cj);
    }
    r.set("clips", clips);
    Json nodes = Json::array();
    for (const auto& n : m.nodes) nodes.push(Json::string(n.name));
    r.set("nodes", nodes);
    Json mats = Json::array();
    static const char* alpha[] = {"opaque", "mask", "blend"};
    static const char* team[] = {"none", "tint", "mask", "alpha", "vertex"};
    for (const auto& mt : m.materials) {
        Json mj = Json::object();
        mj.set("name", Json::string(mt.name));
        mj.set("alpha", Json::string(alpha[int(mt.alpha)]));
        mj.set("team", Json::string(team[int(mt.team)]));
        mj.set("textured", Json::fromBool(mt.baseTex >= 0));
        mats.push(mj);
    }
    r.set("materials", mats);
    // The agent receives the exact imported GLB LOD chain, not an
    // assumption based on a source asset's filename or generated LOD report.
    Json lods = Json::array();
    for (const auto& node : m.nodes) {
        if (node.mesh < 0 || size_t(node.mesh) >= m.meshes.size()) continue;
        const auto& mesh = m.meshes[size_t(node.mesh)];
        if (mesh.lods.empty()) continue;
        Json entry = Json::object();
        entry.set("node", Json::string(node.name));
        Json levels = Json::array();
        auto addLevel = [&](const render::MeshDef& lodMesh, int level, float threshold) {
            uint64_t tris = 0;
            for (const auto& p : lodMesh.primitives) tris += p.indexCount / 3;
            Json item = Json::object();
            item.set("level", Json::number(int64_t(level)));
            item.set("triangles", Json::number(int64_t(tris)));
            if (level) item.set("maxPixels", Json::number(double(threshold)));
            levels.push(item);
        };
        addLevel(mesh, 0, 0);
        for (size_t i = 0; i < mesh.lods.size(); ++i) {
            const auto& lod = mesh.lods[i];
            if (lod.mesh < 0 || size_t(lod.mesh) >= m.meshes.size()) continue;
            addLevel(m.meshes[size_t(lod.mesh)], int(i) + 1, lod.maxPixels);
        }
        entry.set("levels", levels);
        lods.push(entry);
    }
    r.set("lods", lods);
    Json autoLod = Json::object();
    autoLod.set("attempted", Json::fromBool(m.autoLod.attempted));
    autoLod.set("generatedBaseMeshes", Json::number(int64_t(m.autoLod.generatedMeshes)));
    autoLod.set("generatedLevels", Json::number(int64_t(m.autoLod.generatedLevels)));
    autoLod.set("addedIndices", Json::number(int64_t(m.autoLod.addedIndices)));
    autoLod.set("reason", Json::string(m.autoLod.reason));
    r.set("autoLod", autoLod);
    Json vars = Json::array();
    for (const auto& v : m.variants) vars.push(Json::string(v));
    r.set("variants", vars);
    r.set("skins", Json::number(int64_t(m.skins.size())));
    r.set("bakedClips", Json::fromBool(m.baked.tex.valid()));
    Json b = Json::array();
    b.push(Json::number(double(m.boundsMax.x - m.boundsMin.x)));
    b.push(Json::number(double(m.boundsMax.y - m.boundsMin.y)));
    b.push(Json::number(double(m.boundsMax.z - m.boundsMin.z)));
    r.set("size", b);
    return r;
}
} // namespace

class ModelWindow : public EditorWindow {
public:
    const char* id() const override { return "models"; }
    const char* title() const override { return "Модели"; }
    const char* group() const override { return "look"; }
    const char* dockArea() const override { return "right-bottom"; }
    int order() const override { return 3; }
    bool openByDefault() const override { return false; }

    ~ModelWindow() override {
        if(rawDevice_)rawModels_.shutdown(*rawDevice_);
        retirePortrait();
    }
    void retirePortrait() {
        if (!portraitInit_) return;
        auto former = std::move(portrait_);
        portrait_ = std::make_shared<game::Portrait>();
        portraitInit_ = false;
        if (!former || !portraitDev_) return;
        if (!gpuResourceOwner_) {
            // Standalone owner: requires its existing GPU-idle teardown.
            former->shutdown();
            return;
        }
        gfx::Device& gpu = *portraitDev_;
        gpuResourceOwner_->retireOwned(gpu,
            [former = std::move(former)] { former->shutdown(); });
    }

    void draw(Editor& ed) override {
        gpuResourceOwner_ = &ed.gpuResourceRetirement();
        portraitDev_ = &ed.device();
        const auto& types = ed.rules().types;
        if (types.empty()) { ImGui::TextDisabled("Нет типов"); return; }
        if (ed.selection.kind == Selection::Type) {
            for (size_t i = 0; i < types.size(); ++i) {
                if (types[i].id == ed.selection.key) type_ = int(i);
            }
        } else if (ed.selection.kind == Selection::Entity &&
                 ed.selection.index >= 0 &&
                 size_t(ed.selection.index) < ed.doc().data.entities.size()) {
            const int chosen = ed.rules().findType(
                ed.doc().data.entities[size_t(ed.selection.index)].type);
            if (chosen >= 0) type_ = chosen;
        }
        type_ = std::clamp(type_, 0, int(types.size()) - 1);
        if (ui::beginProps("##model")) {
            ui::prop("Тип");
            if (ImGui::BeginCombo("##type", types[size_t(type_)].id.c_str())) {
                for (size_t i = 0; i < types.size(); ++i)
                    if (ImGui::Selectable(types[i].id.c_str(), int(i) == type_)) type_ = int(i);
                ImGui::EndCombo();
            }
            ui::prop("Игрок", "Цвет игрока на превью (0 — нейтральный)");
            ImGui::SliderInt("##owner", &owner_, 0, 8);
            ui::endProps();
        }
        // A type chosen in this panel might not occur on the open map.
        // Load it on demand and keep it resident while its preview is visible.
        ed.retainEditorType(uint16_t(type_));
        const render::Model* current=ed.view().modelOf(uint16_t(type_));
        // Explicit alternate INSPECTION mode: the imported static asset
        // without game-type tint, graph, materials or transform overrides.
        // Ordinary default portraits always retain their original path.
        bool canShowRaw=current && current->clips.empty() &&
            current->skins.empty() && !current->path.empty() &&
            current->path.rfind("shape:",0)!=0;
        if(canShowRaw) {
            const std::filesystem::path path(current->path);
            canShowRaw=path.is_relative();
            for(const auto& part:path)if(part=="..")canShowRaw=false;
        }
        if(canShowRaw)ImGui::Checkbox("Исходная 3D-модель (без overrides)",&rawMode_);
        else rawMode_=false;
        if (!portraitInit_) { portraitInit_ = true; portrait_->init(ed.device(), 256); }
        time_ += ImGui::GetIO().DeltaTime;
        bool rawDrawn=false;
        if(rawMode_ && canShowRaw) {
            // Keep this provider entirely separate from RtsView's model
            // ownership; rebind at project/cache revision boundaries.
            const auto& project=ed.ws().project.dir;
            const auto revision=ed.view().modelResources().generation();
            if(rawProject_!=project || rawRevision_!=revision) {
                if(rawDevice_)rawModels_.shutdown(*rawDevice_);
                rawModels_.setReader([project](const std::string& path,
                                              std::string& bytes) {
                    std::ifstream in(std::filesystem::path(project)/path,
                                     std::ios::binary);
                    if(!in)return false;
                    bytes.assign(std::istreambuf_iterator<char>(in),
                                 std::istreambuf_iterator<char>());
                    return true;
                });
                // Advisory size estimate, not a hard VRAM cap. Busy GPU
                // allocations remain alive until their retirement token.
                rawModels_.resources().setEstimatedRetirementByteLimit(
                    64ull*1024ull*1024ull);
#ifndef __EMSCRIPTEN__
                rawModels_.setCpuWorkerCount(2);
                rawModels_.setCpuMaxQueued(8);
#endif
                rawDevice_=&ed.device();
                rawProject_=project;
                rawRevision_=revision;
            }
            // One resource budget across THREE live native host owners:
            // the world RtsView, this portrait's generated geometry and the
            // independent raw-source model provider. Shared GPU pool pages
            // and texture-owner allocations are never charged per Model.
            render::RenderResourceOwnerGroup ownerGroup;
            const auto deviceEpoch=ed.gpuDeviceLease();
            const auto sceneLease=ownerGroup.registerScoped(
                deviceEpoch,ed.view().modelResources());
            const auto portraitLease=ownerGroup.registerScoped(
                deviceEpoch,portrait_->modelResources());
            const auto rawLease=ownerGroup.registerScoped(
                deviceEpoch,rawModels_.resources());
            ownerGroup.setEstimatedBudgetBytes(256ull*1024ull*1024ull);
            const auto group=ownerGroup.snapshot(ed.device());
            const bool admission=ownerGroup.mayAdmitNewSources(group);
            ImGui::TextDisabled("Ресурсы: %zu хоста · %zu модели · %.1f MiB (оценка)",
                group.uniqueOwners,group.residentModels+group.retiredModels,
                double(group.estimatedKnownBytes())/(1024.0*1024.0));
            if(group.health==render::RenderResourceHealth::DeviceUnhealthy)
                ImGui::TextDisabled("GPU сообщил об ошибке; новые источники не загружаются");
            else if(group.health==render::RenderResourceHealth::RetirementFenceFailed)
                ImGui::TextDisabled("Ошибка GPU fence (%zu): ресурсы удерживаются, новые импорты отложены",
                                    group.unresolvedFailedFences);
            else if(!admission)
                ImGui::TextDisabled("Бюджет визуальных ресурсов занят, загрузка отложена");
            else if(group.health==render::RenderResourceHealth::PendingGpuRetirement)
                ImGui::TextDisabled("GPU завершает освобождение предыдущих моделей");
#ifndef __EMSCRIPTEN__
            if(admission && !rawModels_.resources().contains(current->path))
                rawModels_.requestAsync(
                    current->path,render::ModelLoadPriority::Interactive);
            if(admission)rawModels_.publishReady(ed.device(),1,1.0);
            const auto progress=rawModels_.cpuStats();
            if(progress.workerActive || progress.queued)
                ImGui::TextDisabled("CPU моделей: %zu / %zu задач · очередь %zu",
                    progress.activeWorkers,progress.configuredWorkers,
                    progress.queued);
#else
            if(admission)rawModels_.load(ed.device(),current->path);
#endif
            if(rawModels_.find(current->path))
                rawDrawn=portrait_->renderStaticModel(
                    rawModels_,current->path,0.5f);
            else ImGui::TextDisabled("Загрузка исходной модели...");
        }
        if(!rawDrawn)
            portrait_->render(ed.view(), uint16_t(type_), uint8_t(owner_), time_);
        if (portrait_->texture().valid()) {
            const bool flip = ed.device().info().renderTargetFlipY;
            const float side = std::clamp(ImGui::GetContentRegionAvail().x, 120.0f, 260.0f);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - side) * 0.5f);
            ImGui::Image(ImTextureRef(ImTextureID(portrait_->texture().id)), ImVec2(side, side), ImVec2(0, flip ? 0.f : 1.f), ImVec2(1, flip ? 1.f : 0.f));
        }
        const std::string& tid = types[size_t(type_)].id;
        for (const auto& w : ed.view().warnings())
            if (w.find("type '" + tid + "'") == 0) ui::toneText(ui::Tone::Warn, "%s", w.c_str());
        const render::Model* m = ed.view().modelOf(uint16_t(type_));
        if (!m) return;
        ImGui::TextUnformatted(m->path.c_str());
        ImGui::PushStyleColor(ImGuiCol_Text, ui::pal().textDim);
        ImGui::TextWrapped("Узлов %zu, мешей %zu, материалов %zu, текстур %zu, скинов %zu%s", m->nodes.size(), m->meshes.size(), m->materials.size(),
                    m->images.size(), m->skins.size(), m->baked.tex.valid() ? ", клипы запечены" : "");
        ImGui::PopStyleColor();
        if (ui::collapsing("Клипы", ImGuiTreeNodeFlags_DefaultOpen))
            for (const auto& c : m->clips) ImGui::BulletText("%s  %.2f с", c.name.c_str(), double(c.duration));
        if (ui::collapsing("Материалы"))
            for (const auto& mt : m->materials)
                ImGui::BulletText("%s%s%s", mt.name.c_str(), mt.team != render::TeamMode::None ? "  [цвет игрока]" : "",
                                  mt.alpha == render::AlphaMode::Blend ? "  [прозрачный]" : mt.alpha == render::AlphaMode::Mask ? "  [маска]" : "");
        if (ui::collapsing("LOD модели", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (m->autoLod.generatedLevels) {
                ImGui::Text("Авто-LOD: %zu уровней для %zu мешей",
                            m->autoLod.generatedLevels, m->autoLod.generatedMeshes);
            } else if (m->autoLod.attempted) {
                ImGui::TextDisabled("Авто-LOD: %s", m->autoLod.reason.c_str());
            }
            size_t chains = 0;
            for (const auto& node : m->nodes) {
                if (node.mesh < 0 || size_t(node.mesh) >= m->meshes.size()) continue;
                const auto& mesh = m->meshes[size_t(node.mesh)];
                if (mesh.lods.empty()) continue;
                ++chains;
                uint64_t baseTris = 0;
                for (const auto& p : mesh.primitives) baseTris += p.indexCount / 3;
                ImGui::Text("%s: LOD0 %llu тр.", node.name.c_str(),
                            (unsigned long long)baseTris);
                for (size_t i = 0; i < mesh.lods.size(); ++i) {
                    const auto& lod = mesh.lods[i];
                    if (lod.mesh < 0 || size_t(lod.mesh) >= m->meshes.size()) continue;
                    uint64_t tris = 0;
                    for (const auto& p : m->meshes[size_t(lod.mesh)].primitives)
                        tris += p.indexCount / 3;
                    ImGui::BulletText("LOD%zu: %llu тр., порог %.1f px",
                        i + 1, (unsigned long long)tris, double(lod.maxPixels));
                }
            }
            if (!chains) ImGui::TextDisabled("Нет LOD: проверь параметры автоупрощения и исходный GLB.");
            ImGui::TextDisabled("Авто-LOD создаются до GPU upload; LOD0/анимации сохранены.");
            ImGui::TextDisabled("AIVERSE_AUTOLOD=off возвращает только импортированные уровни.");
            ImGui::TextDisabled("Выбор камерой/тенями: Сцена → LOD.");
        }
        if (!m->variants.empty() && ui::collapsing("Варианты"))
            for (const auto& v : m->variants) ImGui::BulletText("%s", v.c_str());
        if (ui::collapsing("Узлы (для procedural, attach, fx, muzzle)"))
            for (const auto& n : m->nodes) ImGui::BulletText("%s", n.name.c_str());
    }

    void update(Editor& ed, double dt) override {
        // Dock-hidden and closed tabs do not need live 3D preview resources.
        // Archive the complete previous Portrait until backend GPU completion.
        if (portraitInit_ && (!open || !ed.isWindowInFront(this)))
            retirePortrait();
        if (!opRegistered_) {
            opRegistered_ = true;
            ed.ws().registerOp({"model_info", "Contents of the model of a unit type (clips, nodes, materials, variants) and its view warnings.",
                                Json::parse(R"({"type": "object", "properties": {"type": {"type": "string", "description": "type id from rules.json"}}, "required": ["type"]})"),
                                [&ed](const Json& p) {
                                    Json r = Json::object();
                                    const std::string id = p.getStr("type");
                                    const auto& types = ed.rules().types;
                                    for (size_t i = 0; i < types.size(); ++i) {
                                        if (types[i].id != id) continue;
                                        ed.retainEditorType(uint16_t(i));
                                        if (const render::Model* m = ed.view().modelOf(uint16_t(i))) r = modelInfo(*m);
                                        Json w = Json::array();
                                        for (const auto& s : ed.view().warnings()) if (s.find("type '" + id + "'") == 0) w.push(Json::string(s));
                                        r.set("warnings", w);
                                        return r;
                                    }
                                    r.set("error", Json::string("unknown type '" + id + "'"));
                                    return r;
                                }, true});
        }
        (void)dt; // visual asset polling is owned by Editor, not this window
    }

private:
    int type_ = 0, owner_ = 1;
    double time_ = 0;
    bool portraitInit_ = false, opRegistered_ = false;
    std::shared_ptr<game::Portrait> portrait_ =
        std::make_shared<game::Portrait>();
    render::GpuResourceRetirement* gpuResourceOwner_ = nullptr;
    gfx::Device* portraitDev_ = nullptr;
    // Owned by the editor Model panel; the default type preview does not
    // borrow from this independent native-source cache.
    render::ModelAssetProvider rawModels_;
    gfx::Device* rawDevice_=nullptr;
    std::string rawProject_;
    uint64_t rawRevision_=std::numeric_limits<uint64_t>::max();
    bool rawMode_=false;
};

AIVERSE_EDITOR_WINDOW(ModelWindow);

} // namespace aiverse::editor
