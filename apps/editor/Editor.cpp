// Editor: lifecycle, docking layout, menus, documents, play-test and viewport rendering.
// Window contents live in apps/editor/windows/*.cpp; the agent link in EditorAgent.cpp.
#include "Editor.h"
#include "engine/render/ModelSurfacePicking.h"
#include "apps/editor/AuthoringComponents.h"
#include "apps/editor/AuthoringWater.h"
#include "windows/StudioContextMenus.h"
#include "core/SimTools.h"
#include "core/RouteOps.h"
#include "engine/fx/FxModelMesh.h"
#include "engine/render/PrismaVolumeData.h"
#include "engine/render/GlobalGeometryPool.h"
#include "engine/fx/SpectraGpuPoolWorld.h"
#include "engine/fx/SpectraDiagnosticBundle.h"
#include "libraries/include/aiverselib/mechanics/SpectraGpuCaptureMailbox.h"
#include "libraries/include/aiverselib/mechanics/SpectraGpuRecoveryHost.h"
#include "engine/fx/SpectraDiagnosticReport.h"
#include "imgui.h"
#include "imgui_internal.h" // DockBuilder

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <unordered_map>
#include "engine/game/Tiles.h"
#include "engine/render/CameraBehavior.h"
#include "engine/view/game/GameOverlayData.h"
#include "engine/view/game/GameSelectionPresentation.h"
#include <sstream>

namespace fs = std::filesystem;

namespace aiverse::editor {

using namespace render;

namespace {
// Models / textures of the open game (view blocks reference them relative to the game folder).
// Avoid exposing absolute GPU frame serials even in the file name of a
// PRIVACY-SAFE diagnostic. Bounded distinct local names, no overwrite.
fs::path unusedSafeDiagnosticName(const fs::path& directory,
                                  const std::string& prefix,size_t group,
                                  const std::string& suffix) {
    for(uint32_t ordinal=1;ordinal<=1000;++ordinal) {
        const fs::path candidate=directory/(
            prefix+"-group-"+std::to_string(group)+"-"+
            std::to_string(ordinal)+suffix);
        std::error_code check;
        if(!fs::exists(candidate,check) && !check)return candidate;
        if(check)return {};
    }
    return {};
}

FileReader projectReader(const std::string& dir) {
    return [dir](const std::string& p, std::string& out) {
        std::ifstream f(dir + "/" + p, std::ios::binary);
        if (!f) return platform::readContentFile(p, out);
        std::stringstream ss; ss << f.rdbuf(); out = ss.str();
        return true;
    };
}
} // namespace

void Editor::refreshPrismaVolumes() {
    const Json& game = ws_.project.manifest["prismaVolumes"];
    const Json& map = doc().data.extra["prismaVolumes"];
    const std::string signature = game.dump(-1) + "|" + map.dump(-1);
    if (signature == prismaVolumesSignature_) return;
    prismaVolumesSignature_ = signature;
    std::vector<std::string> issues;
    prismaVolumes_ = render::parsePrismaVolumes(game, map, &issues);
    for (const auto& issue : issues) ws_.log("prisma", issue, true);
}

Editor::Editor(Options o) : opt_(std::move(o)) {}

Editor::~Editor() {
    captureLifetime_->store(false,std::memory_order_release);
    // Explicit shutdown proves GPU idle. Otherwise retain the GPU owner.
    if(spectraPreviewGpu_) (void)spectraPreviewGpu_.release();
    for(auto& retired:retiredSpectraPreviews_) (void)retired.pool.release();
}

// ============================ viewport ============================

void Viewport::ensure(gfx::Device& dev, int pw, int ph,
                      ViewportGpuRetirement& owner, const char* debugName) {
    ownerDevice = &dev;
    retirement = &owner;
    if(gpuDeviceLease.device() && !gpuDeviceLease.validFor(dev))
        throw std::logic_error("Editor viewport GPU generation expired");
    pw = std::max(pw, 8); ph = std::max(ph, 8);
    if (pw == w && ph == h && color.valid() && depth.valid()) return;
    // The old color may still be referenced by ImGui's GPU draw list and
    // the depth by the previous scene pass. Never destroy them here.
    release(dev);
    gfx::TextureDesc td;
    td.width = pw; td.height = ph;
    td.renderTarget = true;
    td.label = "editor viewport";
    color = dev.createTexture(td);
    td.format = gfx::PixelFormat::Depth;
    td.filter = gfx::Filter::Nearest;
    depth = dev.createTexture(td);
    w = pw; h = ph;
    owner.noteLive(this, debugName, w, h, color, depth);
}

void Viewport::release(gfx::Device& dev) {
    if(gpuDeviceLease.device() && !gpuDeviceLease.validFor(dev))
        throw std::logic_error("Editor viewport release GPU generation expired");
    if (retirement) retirement->forgetLive(this);
    // Color handles used in ImGui commands must survive GPU submission,
    // including when an entire Scene/Game/FX window is destroyed.
    if (retirement) {
        retirement->retire(dev,color);
        retirement->retire(dev,depth);
    } else {
        // Legacy host not using an Editor owner: preserve its old explicit
        // teardown contract (caller must guarantee GPU idle).
        if (color.valid()) dev.destroyTexture(color);
        if (depth.valid()) dev.destroyTexture(depth);
        color = depth = {};
    }
    w = h = 0;
    hovered = false;
    drawnFrame = -100; // never expose a released ImGui texture as live
}

void Viewport::image(gfx::Device& dev, float wPts, float hPts) {
    if(gpuDeviceLease.device() && !gpuDeviceLease.validFor(dev))
        throw std::logic_error("Editor viewport image GPU generation expired");
    ImVec2 p = ImGui::GetCursorScreenPos();
    originX = p.x; originY = p.y;
    pointsW = wPts; pointsH = hPts;
    // GL render targets are bottom-up; Metal's are top-down
    bool flip = !dev.info().renderTargetFlipY;
    ImGui::Image(ImTextureRef(ImTextureID(color.id)), ImVec2(wPts, hPts), ImVec2(0, flip ? 1.f : 0.f), ImVec2(1, flip ? 0.f : 1.f));
    hovered = ImGui::IsItemHovered();
}

// ============================ lifecycle ============================

void Editor::init(gfx::Device& dev) {
    gpuLifetime_=std::make_unique<render::RenderDeviceLifetime>(dev);
    view_.setGpuDeviceLease(gpuDeviceLease());
    dev_ = &dev;
    captureLifetime_ = std::make_shared<std::atomic<bool>>(true);
    ws_.engineDir = opt_.engineDir;
    editor::setEditorLanguage(ws_.engineDir + "/engine/assets/lang");   // editor messages (map checks)
    ws_.buildTool = opt_.buildTool;
    ws_.gameExe = !opt_.ownGameExe.empty() ? opt_.ownGameExe : opt_.gameExe;   // the per-project build has its own client (modules)
    ws_.clock = [this] { return time_; };
    std::string font = opt_.engineDir + "/engine/assets/fonts/ui.ttf";
    std::error_code fec;
    if (!fs::exists(font, fec)) font = "engine/fonts/ui.ttf";
    ui_.init(dev, font.c_str(), 16.0f);
    // the editor's own look (the game's ImGui overlay keeps ImGuiLayer's style)
    if (!theme_.loadFonts(opt_.engineDir)) ws_.log("editor", "Шрифты редактора не найдены (third_party/fonts): используется ui.ttf", true);
    theme_.registerSettings();
    theme_.apply();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    static std::string iniPath = platform::userDataDir() + "/editor_layout.ini";
    fs::create_directories(platform::userDataDir(), fec);
    io.IniFilename = iniPath.c_str();
    resetLayout_ = !fs::exists(iniPath, fec);
    r3d_.setGpuDeviceLease(gpuDeviceLease());
    r3d_.init(dev);
    view_.setAppearancePipelineValidator(
        [this](const render::Model& model,
               const std::vector<render::Material>& materials) {
            return r3d_.warmModelPipelinesChecked(model,&materials);
        });
    prisma_.setGpuDeviceLease(gpuDeviceLease());
    prisma_.init(dev);
    prisma_.setQuality(render::Prisma::defaultQualityFor(dev, false));
    r3d_.setShadowQuality(Renderer3D::defaultQualityFor(dev, false));
    r3d_.lights.setQuality(aiverse::render::LightSystem::defaultQualityFor(dev, false));
    for (auto& f : WindowRegistry::factories()) {
        windows_.push_back(f());
        windows_.back()->open = windows_.back()->openByDefault();
    }
    // registration order depends on the linker; tabs follow order()
    std::stable_sort(windows_.begin(), windows_.end(), [](auto& a, auto& b) { return a->order() < b->order(); });
    registerGuiOps();
    registerPlayOps();
    registerWorldOps();
    registerFxPreviewOps();
    if (!opt_.projectDir.empty()) {
        std::string err;
        if (!ws_.openGame(opt_.projectDir, &err)) { status = "Ошибка проекта: " + err; showPicker_ = true; }
    } else showPicker_ = true;
    initFileAssoc();
    registerNativeBuildOps(ws_, opt_.editorExe);   // up-to-date check against this binary
    checkNativeModules(false);
    // Only the map's types need resident GPU models. In particular, do not
    // eagerly upload the full rules.json type catalog into editor memory.
    // Escape hatch for diagnosing assets that rely on legacy eager loading.
    const char* eagerAssets = std::getenv("AIVERSE_EDITOR_EAGER_MODELS");
    lazyEditorAssets_ = !(eagerAssets && std::strcmp(eagerAssets, "1") == 0);
    modelDemand_.resize(rules().types.size());
    view_.setRetainPickingGeometry(true);
    view_.setDeferredLoading(lazyEditorAssets_);
    view_.setDeferAppearanceVariants(false); // editing always exposes every variant
    view_.setContentBase(ws_.project.dir.empty() ? "" : ws_.project.dir + "/");
    view_.init(dev, rules(), projectReader(ws_.project.dir));
    gpuRetirementRing_=std::make_shared<render::RetirementDiagnosticRing>(128);
    const auto gpuTrace=render::RetirementDiagnosticRing::observe(
        gpuRetirementRing_);
    view_.setModelRetirementDiagnosticObserver(
        [gpuTrace](render::RetirementDiagnosticEvent event) {
            event.resource=render::RetirementResourceKind::Model;
            gpuTrace(event);
        });
    view_.setImageRetirementDiagnosticObserver(
        [gpuTrace](render::RetirementDiagnosticEvent event) {
            event.resource=render::RetirementResourceKind::Image;
            gpuTrace(event);
        });
    for (auto& w : view_.warnings()) ws_.log("view", w, true);
    fxTextures_.init(dev);
    // ShaderGraph/FX hot reload can occur while the previous frame still
    // samples the old atlases and pipelines. The editor owns the fence queue.
    fxTextures_.setGpuResourceRetirement(&gpuResourceRetirement_);
    gameFx_.setGpuResourceRetirement(&gpuResourceRetirement_);
    gameFx_.setQuality(fx::defaultQualityFor(dev, false));
    gameFx_.setFileReader([this](const std::string& path, std::string& out) { return projectReader(ws_.project.dir)(path, out); });
    gameFx_.setTextureLoader([this](const std::string& name, fx::FxTexture& out) {
        render::TextureRef r = fxTextures_.resolve(name, false, ws_.project.dir.empty() ? "" : ws_.project.dir + "/");
        if (!r.valid()) return false;
        out = {r.tex, r.u0, r.v0, r.u1, r.v1};
        return true;
    });
    gameFx_.setGroundHeight([this](float x, float z) { return sim_ ? view_.heightAt(simModule()->state(*sim_).grid, x, z) : surfaceHeightAt(x, z); });
    gameFx_.setMeshLoader([this](const std::string& name, fx::FxMesh& out) { return fx::meshFromModelFile(name, projectReader(ws_.project.dir), out); });
    gameFx_.setGpuDeviceLease(gpuDeviceLease());
    if (!gameFx_.init(&dev)) ws_.log("fx", gameFx_.lastError(), true);
    reloadFx();
    if (!opt_.openMap.empty()) openMap(opt_.openMap);
    else if (doc().data.cells.empty()) newMap(48, 32);
    ws_.changes.clear();
    syncGrid();
    fitScene();
    gameCam = sceneCam;
    if (opt_.listenPort != 0) {
        const bool listening = opt_.listenPort == -1 ? server_.listenAvailable() : server_.listen(opt_.listenPort);
        if (listening) ws_.log("editor", "MCP listening on port " + std::to_string(server_.port()));
        else ws_.log("editor", "Could not open MCP listener (requested port " + std::to_string(opt_.listenPort) + ")", true);
    }
}

void Editor::retirePreviewModel(std::unique_ptr<render::Model>& model) noexcept {
    if(!model)return;
    if(dev_)previewModelRetirement_.retireModel(*dev_,model);
    else (void)model.release(); // Device no longer exists: fail closed
}

void Editor::shutdown() {
    graphGpuProfile_.clear();
    if (!dev_) return;
    // Invalidate any captureNextFrame callbacks still owned by the RHI.
    captureLifetime_->store(false,std::memory_order_release);
    captureArmed_ = false;
    shots_.clear();
    // Editor windows own offscreen Model/portrait/viewport allocations and
    // can still have prior-frame submissions in flight. Prove full device
    // completion BEFORE allowing destructors to destroy native GPU objects.
    if(!dev_->waitForResourceIdle())
        throw std::runtime_error(
            "Editor shutdown: GPU completion barrier failed");
    if(spectraGpuTelemetry_==spectraPreviewGpu_.get())
        spectraGpuTelemetry_=nullptr;
    spectraPreviewGpu_.reset();
    retiredSpectraPreviews_.clear();
    spectraPreviewFrameOpen_=false;
    spectraPreviewRequest_.pending=false;
    spectraGpuTelemetry_=nullptr;
    clientHost_.stop();
    // A hosted GameClient may have submitted resources in an earlier GUI
    // frame. Its GPU handles must survive until a true graphics completion
    // signal; do not release them simply because the Game tab was closed.
    if (dev_->gpuHealthy() && dev_->waitForSubmittedGpuWork())
        clientHost_.drainAfterGpuIdle();
    else if (!dev_->gpuHealthy())
        clientHost_.abandonOnDeviceLoss();
    gameFxBridge_.detach();
    // Nature preview retires grass Models through previewModelRetirement_
    // and stops its particles while gameFx_ is still alive.
    physisNaturePreview_.release(*dev_, gameFx_);
    sim_.reset();
    gameFx_.shutdown();
    fxTextures_.shutdown();
    for(auto& window:windows_)window->shutdownAfterGpuIdle(*this);
    windows_.clear(); // All windows (including ShaderGraph previews) retire owned resources.
    // The last submitted GPU frame may still reference ShaderGraph
    // pipelines/buffers/portrait resources queued by window destruction.
    if (dev_->gpuHealthy() && dev_->waitForSubmittedGpuWork())
        gpuResourceRetirement_.drainAfterGpuIdle(*dev_);
    else if (!dev_->gpuHealthy())
        gpuResourceRetirement_.abandonDevice(*dev_);
    // Shut down after last frame submission. Only an actual successful
    // graphics-queue idle barrier permits destroying ImGui-referenced
    // viewport handles; otherwise leave them owned by the device until
    // its own teardown instead of risking a GPU use-after-free.
    if (dev_->waitForSubmittedGpuWork())
        viewportRetirement_.drainAfterGpuIdle(*dev_);
    if(!previewModelRetirement_.completeForShutdown(*dev_))
        throw std::runtime_error("Editor preview Models: GPU retirement barrier failed");
    if (studioLogo_.valid()) { dev_->destroyTexture(studioLogo_); studioLogo_ = {}; }
    terraWeaveForestPreview_.release(*dev_);
    nereidPreview_.shutdown(*dev_);
    terraWeaveSparsePreview_.releaseAfterGpuIdle(*dev_);
    view_.shutdown(*dev_);
    view_.setModelRetirementDiagnosticObserver({});
    view_.setImageRetirementDiagnosticObserver({});
    gpuRetirementRing_.reset();
    prisma_.shutdown();
    if(fpsPreviewMesh_.vb.valid())dev_->destroyBuffer(fpsPreviewMesh_.vb);
    if(fpsPreviewMesh_.ib.valid())dev_->destroyBuffer(fpsPreviewMesh_.ib);
    r3d_.shutdown();
    ui_.shutdown();
    // The token is invalidated only AFTER all cache-backed GPU shutdowns,
    // and before the platform can destroy or reuse its Device address.
    if(gpuLifetime_)gpuLifetime_->retire();
    gpuLifetime_.reset();
    dev_ = nullptr;
}

EditorWindow* Editor::window(const std::string& id) {
    for (auto& w : windows_) if (id == w->id()) return w.get();
    return nullptr;
}

void Editor::focusWindow(const std::string& id) {
    EditorWindow* w = window(id);
    if (!w) return;
    w->open = true;
    // windows that change the picture (fog, terrain, lighting, post effects…) are useless next to the Game tab's
    // "test not running" placeholder: bring the Scene to the front of the center area as well
    const std::string g = w->group(), area = w->dockArea();
    if ((g == "world" || g == "look") && area != "center" && !playing() && !clientPlaying())
        if (EditorWindow* scene = window("scene"); scene && scene->open) ImGui::SetWindowFocus(scene->imguiName());
    ImGui::SetWindowFocus(w->imguiName());
}

// ============================ documents ============================

void Editor::openGame(const std::string& idOrDir) {
    std::string err;
    if (playing()) stopPlay();
    if (!ws_.openGame(idOrDir, &err)) {
        status = "Не удалось открыть проект: " + err;
        ws_.log("editor", status + " (" + idOrDir + ")", true);
        return;
    }
    ws_.changes.clear();
    if (dev_) terraWeaveForestPreview_.release(*dev_);
    terraWeaveSparsePreview_.release(
        [this](std::unique_ptr<render::Model>& model){
            retirePreviewModel(model);
        });
    terraWeaveForestPreview_.setEnabled(false);
    if (dev_) physisNaturePreview_.release(*dev_, gameFx_);
    reloadRules();
    selection = {};
    showPicker_ = false;
    fitScene();
    status = "Игра: " + ws_.project.manifest.getStr("name", idOrDir);
    checkNativeModules(false);
    if(ws_.map.data.worldSpace) {
        focusWindow("actor");
        const auto& spawn=doc().data.extra["spawns"][0];
        sceneCam.target={float(spawn[0].toDouble()),0,float(spawn[2].toDouble())};sceneCam.distance=35;
    }
}

void Editor::reloadFx() {
    fxWarnings.clear();
    gameFx_.reloadAll();
    if (dev_) { fxTextures_.shutdown(); fxTextures_.init(*dev_); }   // re-imported textures (fx_import_unity) show up
    gameFx_.clearErrors();
    Json bank;
    std::string text;
    if (projectReader(ws_.project.dir)("fx/fx.json", text)) {
        try { bank = Json::parse(text); }
        catch (const std::exception& e) { fxWarnings.push_back(std::string("fx/fx.json: ") + e.what()); }
    }
    std::string err;
    if (!gameFxBridge_.init(gameFx_, rules(), bank, &err)) fxWarnings.push_back(err);
    for (auto& w : gameFxBridge_.warnings()) fxWarnings.push_back(w);
}

void Editor::reloadRules() {
    editSceneCacheValid_ = false;
    editSceneCache_.clear();
    // The view can destroy/recreate Model objects while reusing allocator
    // addresses and GPU handles. Never carry their CPU bind caches forward.
    r3d_.invalidateStaticModelCaches();
    view_.shutdown(*dev_);
    modelDemand_.resize(rules().types.size());
    modelDemand_.markSweep(time_);
    visualAssets_.reset();
    view_.setRetainPickingGeometry(true);
    view_.setDeferredLoading(lazyEditorAssets_);
    view_.setDeferAppearanceVariants(false); // editing always exposes every variant
    view_.setContentBase(ws_.project.dir.empty() ? "" : ws_.project.dir + "/");
    view_.init(*dev_, rules(), projectReader(ws_.project.dir));
    for (auto& w : view_.warnings()) ws_.log("view", w, true);
    gridRevision_ = ~0ull;
    ++rulesRevision_;   // the world tree's preview rebuilds
    worldCfg_ = parseWorldConfig(ws_.project.manifest);
    for (auto& e : worldCfg_.errors) ws_.log("world", "game.json " + e, true);
    placeType = std::clamp(placeType, 0, std::max(0, int(rules().types.size()) - 1));
    reloadFx();
}

// Presentation-only asset refresh. The old view joins its model-preparation
// worker and releases GPU resources before new model/graph pointers are exposed.
// Unlike reloadRules(), do not reparse rules, restart FX or reset editor demand
// history. The caller defers this transaction until after play mode.
void Editor::reloadVisualAssets() {
    if (!dev_ || playing() || clientPlaying()) return;
    editSceneCacheValid_ = false;
    editSceneCache_.clear();
    view_.shutdown(*dev_);
    modelDemand_.invalidate(); // discard requests for the old resource generation
    view_.setRetainPickingGeometry(true);
    view_.setDeferredLoading(lazyEditorAssets_);
    view_.setDeferAppearanceVariants(false);
    view_.setContentBase(ws_.project.dir.empty() ? "" : ws_.project.dir + "/");
    view_.init(*dev_, rules(), projectReader(ws_.project.dir));
    for (const auto& warning : view_.warnings())
        ws_.log("view", warning, true);
    // Preview entities and the height queries must rebind after resources
    // change; this is not a rules-document or gameplay state revision.
    gridRevision_ = ~0ull;
    ++rulesRevision_;
    reclaimEditorAssets(true); // requeue authored/selected types, not the full catalog
}

size_t Editor::reloadVisualModelSources(
        const std::vector<std::string>& paths) {
    if(!dev_ || playing() || clientPlaying() || paths.empty())return 0;
    // The caller runs at the editor frame boundary, after the previous
    // render submission. TypeLook/animation sources are detached by GameView
    // before the independent resource owner destroys GPU allocations.
    const size_t invalidated=view_.reloadModelSources(paths);
    if(invalidated) {
        editSceneCacheValid_=false;
        editSceneCache_.clear();
        // Scene-dependent previews rebind to the newly loaded model
        // geometry. Do not reparse rules, restart FX or rebuild terrain.
        ++rulesRevision_;
    }
    return invalidated;
}

// Model conversion remains a host/tooling concern, not part of the
// genre-neutral resource index. Cache one attempted timestamp in the
// VisualAssetDependencyService to avoid repeated external processes.
bool Editor::convertVisualModelSource(const std::string& src) {
    std::error_code ec;
    fs::path tool=fs::path(ws_.buildTool).parent_path() /
#if defined(_WIN32)
        "modelconv.exe";
#else
        "modelconv";
#endif
    if(!fs::exists(tool,ec)) {
        ws_.log("view","нужна конвертация "+src+
                       ", но modelconv не найден рядом с aiversebuild");
        return false;
    }
    // modelconv runs via a legacy shell pipe for diagnostics; never allow
    // author-controlled model paths to break out of quoted arguments.
    const auto unsafeShellPath=[](const std::string& value) {
#if defined(_WIN32)
        // Windows file separators are legal; cmd.exe expands %variables%
        // and may expand !delayed! values even inside double quotes.
        return value.find_first_of("\"%!^\r\n")!=std::string::npos;
#else
        return value.find_first_of("\"$\\\r\n")!=std::string::npos ||
               value.find(char(96))!=std::string::npos; // backtick
#endif
    };
    if(unsafeShellPath(src) || unsafeShellPath(tool.string())) {
        ws_.log("view","отклонён небезопасный путь модели: "+src,true);
        return false;
    }
    const std::string cmd="\"" + tool.string() + "\" \"" + src + "\" 2>&1";
#if defined(_WIN32)
    FILE* pipe=_popen(cmd.c_str(),"r");
#else
    FILE* pipe=popen(cmd.c_str(),"r");
#endif
    if(!pipe)return false;
    std::string output;
    char buffer[256];
    while(std::fgets(buffer,sizeof buffer,pipe))output+=buffer;
#if defined(_WIN32)
    const int rc=_pclose(pipe);
#else
    const int rc=pclose(pipe);
#endif
    ws_.log("view",(rc==0?"сконвертирована модель ":"не удалось сконвертировать ")+
                   src+(rc==0?std::string{}:": "+output));
    return rc==0;
}

void Editor::pollVisualAssets(double dt) {
    if(!dev_ || ws_.project.dir.empty() || ws_.project.actorSession()) {
        visualAssets_.reset();
        return;
    }
    if(!visualAssets_.due(ws_.project.dir,dt))return;
    visualAssets_.scan(ws_.project.dir,rules().types,view_.materialLibrary(),
        view_.modelResources(),
        [this](const std::string& src){return convertVisualModelSource(src);},
        [this](const std::string& message){ws_.log("view",message);});
    // Changes are retained throughout editor/client play. GPU mutations
    // happen only here, before the first rendering pass of a new frame.
    if(!visualAssets_.pending() || playing() || clientPlaying())return;
    const auto impact=visualAssets_.impact();
    // A previous candidate may be waiting for native Metal/Vulkan shader
    // jobs already owned by Renderer3D. Do not rebuild scratch Models and
    // reupload speculative textures while those SAME jobs remain Pending.
    // The source watcher retains the edit until the compiler finishes.
    if((impact.graph||impact.materialLibrary) &&
       view_.gpuAppearancePending() &&
       r3d_.hasPendingModelPipelineJobs() &&
       r3d_.pollPendingModelPipelineJobs())
        return;
    // Source mutations to a model and a graph/material in the same scan
    // cannot safely share the appearance-only transaction. Keep the
    // existing full-view barrier for mixed or unclassified GPU sources.
    if(((impact.graph || impact.materialLibrary) &&
        (impact.model || impact.texture)) ||
       (impact.texture && impact.models.empty())) {
        reloadVisualAssets();
        ws_.log("view","полное обновление смешанных ресурсов: "+
                 std::to_string(visualAssets_.changedSources().size())+
                 " источников");
    } else if(impact.graph || impact.materialLibrary) {
        size_t refreshed=0;
        if(view_.reloadAppearanceTypes(impact.types,impact.materialLibrary,
                                       &refreshed)) {
            if(refreshed) {
                editSceneCacheValid_=false;
                editSceneCache_.clear();
                ++rulesRevision_;
            }
            ws_.log("view","обновлено визуальных типов (без выгрузки моделей): "+
                     std::to_string(refreshed));
        } else if(view_.gpuAppearancePending()) {
            // Async GPU shaders may need several device frames; keep the
            // source watcher pending instead of acknowledging this edit.
            ws_.log("view","компиляция GPU шейдеров продолжается; "
                           "старая версия сохранена, повторим обновление");
            return;
        } else {
            // A bad AnimationGraph JSON, material library, or native GPU
            // pipeline compiler result keeps the last-good appearance.
            ws_.log("view","обновление графа/материалов отклонено; "
                           "старая версия сохранена",true);
        }
    } else if(!impact.empty()) {
        if(impact.texture) {
            auto cache=view_.textureCache();
            for(const auto& source:visualAssets_.changedSources()) {
                const auto* entry=visualAssets_.index().lookup(source);
                if(entry && entry->has(
                    game::view_adapter::ModelSourceKind::Texture))
                    cache->invalidateSource(source);
            }
        }
        std::vector<std::string> models(
            impact.models.begin(),impact.models.end());
        const auto invalidated=reloadVisualModelSources(models);
        ws_.log("view","обновлено ключей моделей: "+
                 std::to_string(invalidated)+
                 "; исходников: "+
                 std::to_string(visualAssets_.changedSources().size()));
    }
    visualAssets_.acknowledge();
}

void Editor::retainEditorType(uint16_t type) {
    if (type >= rules().types.size()) return;
    if (modelDemand_.size() != rules().types.size())
        modelDemand_.resize(rules().types.size());
    modelDemand_.touch(type,time_,game::view_adapter::ModelDemandPriority::Interactive);
    // The Model/Materials windows and model_info must work when the type has
    // no instances on the map. The GameView loader caches this call per type.
    // Eager mode already has resident types; still consume the demand
    // queue to avoid retaining stale requests across editor frames.
    modelDemand_.drain(modelDemand_.pendingCount(),
        [&](size_t requested) {
            if(lazyEditorAssets_)view_.loadType(uint16_t(requested));
        });
}

void Editor::reclaimEditorAssets(bool force) {
    if (!lazyEditorAssets_ || !dev_ || playing() || clientPlaying() ||
        ws_.project.actorSession()) return;
    constexpr double kSweepInterval = 5.0;
    constexpr double kUnusedGrace = 15.0;
    if (!modelDemand_.shouldSweep(time_,kSweepInterval,force)) return;
    modelDemand_.markSweep(time_);
    const size_t typeCount = rules().types.size();
    if (modelDemand_.size() != typeCount) return;
    std::vector<bool> keep(typeCount, false);
    // Keep the entire map's authored types, including currently hidden
    // objects: switching an edit layer must never cause a load/unload loop.
    for (const auto& entity : doc().data.entities) {
        const int type = rules().findType(entity.type);
        if (type >= 0 && size_t(type) < typeCount) keep[size_t(type)] = true;
    }
    // Editor thumbnails, model inspector, dynamic world preview, and ghost
    // placements get a grace period. On-demand loading marks those types.
    for (size_t i = 0; i < typeCount; ++i)
        if (modelDemand_.recentlyUsed(i,time_,kUnusedGrace)) keep[i] = true;
    if (selection.kind == Selection::Type)
        for (size_t i = 0; i < typeCount; ++i)
            if (rules().types[i].id == selection.key) keep[i] = true;
    if (tool == Tool::Place && placeType >= 0 && size_t(placeType) < typeCount)
        keep[size_t(placeType)] = true;
    if (overlayPreviewType >= 0 && size_t(overlayPreviewType) < typeCount)
        keep[size_t(overlayPreviewType)] = true;
    // Queue map/editor assets that were retained but are not yet resident.
    // They load opportunistically after the active viewport's immediate
    // interactive demand. Never enqueue the entire authored type catalog.
    for(size_t i=0;i<typeCount;++i) {
        if(keep[i] && !view_.typeLoaded(uint16_t(i)))
            modelDemand_.touch(i,time_,
                game::view_adapter::ModelDemandPriority::Background);
    }
    // This only releases models no longer referenced by any kept TypeLook.
    // GPU work from their last use is older than the grace interval.
    view_.trimTypes(keep);
}

void Editor::newMap(int w, int h) {
    if (dev_) terraWeaveForestPreview_.release(*dev_);
    terraWeaveForestPreview_.setEnabled(false);
    if (dev_) physisNaturePreview_.release(*dev_, gameFx_);
    doc().reset(w, h);
    selection = {};
    syncGrid();
    fitScene();
    status = "Новая карта " + std::to_string(w) + "x" + std::to_string(h);
}

void Editor::openMap(const std::string& file) {
    std::string err;
    std::error_code ec;
    std::string norm = fs::weakly_canonical(fs::path(file), ec).string();
    if (doc().load(ec ? file : norm, &err)) {
        if (dev_) terraWeaveForestPreview_.release(*dev_);
        terraWeaveForestPreview_.setEnabled(false);
        if (dev_) physisNaturePreview_.release(*dev_, gameFx_);
        status = "Открыто: " + ws_.relative(doc().path);
        selection = {};
        syncGrid();
        fitScene();
    } else status = "Не удалось открыть: " + err;
}

bool Editor::saveMap(const std::string& file) {
    std::string err;
    std::error_code ec;
    fs::create_directories(fs::path(file).parent_path(), ec);
    if (!doc().save(file, &err)) { status = "Ошибка сохранения: " + err; return false; }
    status = "Сохранено: " + ws_.relative(file);
    return true;
}

void Editor::runBuild(const std::string& target) {
    try {
        ws_.startBuild(target, buildConfig ? "Release" : "Debug");
        if (auto* w = window("console")) w->open = true;
    } catch (const std::exception& e) { status = e.what(); }
}

// ============================ camera / helpers ============================

void Editor::fitScene() {
    const auto& d = doc().data;
    sceneCam.target = {d.w * 0.5f, 0, d.h * 0.5f};
    sceneCam.maxDistance = std::max(80.0f, float(std::max(d.w, d.h)) * 2.0f);
    sceneCam.minDistance = 3.0f;
    sceneCam.distance = std::clamp(float(std::max(d.w, d.h)) * 0.95f + 8, sceneCam.minDistance, sceneCam.maxDistance);
    sceneFlying_ = false;
    if (view_.planetMode()) { // planets: start body, whole planet in view
        const auto& W = view_.planets().world();
        view_.planetCamera().reset(W, W.start, view_.planets().surface(W.start), 0);
        view_.planetCamera().distance = W.empty() ? 100.0f : W.bodies[size_t(W.start)].radius * 2.4f;
        return;
    }
    // 2D games: projection and angle of game.json "camera" (zoom stays the editor's)
    render::CameraOverhead cs = render::overheadCameraFrom(ws_.project.manifest);
    sceneCam.ortho = cs.ortho;
    if (cs.present) { sceneCam.pitch = std::min(cs.pitch, 1.5707f); sceneCam.yaw = cs.yaw; sceneCam.fovY = cs.fovY; }
}

void Editor::focusScene(float x, float z, float distance) {
    if (view_.planetMode()) { // x, z = cells on the face the camera looks at (agents' view_focus)
        const auto& pc = view_.planetCamera();
        const game::PlanetSurface* s = view_.planets().surface(pc.body());
        if (s) {
            int f; float a, b;
            s->faceFromDir(pc.dir(), f, a, b);
            view_.planetCamera().setDir(s->dirFromFace(f, std::clamp(x, 0.0f, float(s->faceSize())), std::clamp(z, 0.0f, float(s->faceSize()))));
        }
        if (distance > 0) view_.planetCamera().distance = distance;
        return;
    }
    sceneGoal_ = {x, 0, z};
    sceneFlying_ = true;
    if (distance > 0) sceneCam.distance = std::clamp(distance, sceneCam.minDistance, sceneCam.maxDistance);
}

void Editor::flash(float x0, float z0, float x1, float z1, double seconds) { flashes_.push_back({x0, z0, x1, z1, time_ + seconds, seconds}); }

bool Editor::groundAt(const Viewport& vp, const Camera& cam, float sx, float sy, float& x, float& z) const {
    float3 g;
    if (view_.planetMode()) { // cells on the face under the cursor (planet tools use planetPosAt)
        const Json at = planetPosAt(vp, sx, sy);
        if (!at.isObject()) return false;
        x = float(at["x"].toDouble()); z = float(at["y"].toDouble());
        return true;
    }
    // the drawn surface under the cursor (high ground, detail relief): march the view ray against
    // GameView::heightAt, then bisect; the y = 0 plane only when the ray never meets it
    float3 o, d;
    if (cam.screenRay(sx - vp.originX, sy - vp.originY, vp.pointsW, vp.pointsH, o, d) && d.y < -1e-4f) {
        const game::MapGrid& gr = editGrid();
        auto above = [&](float t) { const float3 p = o + d * t; return p.y - view_.heightAt(gr, p.x, p.z); };
        float t = std::max(0.0f, (64.0f - o.y) / d.y);          // from 64 units up (or the eye)
        const float tEnd = (-16.0f - o.y) / d.y;                 // down to 16 units below 0
        float prev = t;
        if (above(t) > 0)
            for (int i = 0; i < 4096 && t < tEnd; ++i) {
                const float step = 0.2f * (1.0f + t * 0.01f);
                t = std::min(t + step, tEnd);
                if (above(t) <= 0) {
                    float lo = prev, hi = t;
                    for (int k = 0; k < 20; ++k) { const float m = 0.5f * (lo + hi); (above(m) > 0 ? lo : hi) = m; }
                    const float3 p = o + d * hi;
                    x = p.x; z = p.z;
                    return true;
                }
                prev = t;
            }
    }
    if (!cam.screenToGround(sx - vp.originX, sy - vp.originY, vp.pointsW, vp.pointsH, g)) return false;
    x = g.x; z = g.z;
    return true;
}

float Editor::snapCoord(float v, int typeIdx) const {
    if (!snap || rules().types.empty()) return v;
    const auto& t = rules().types[size_t(std::clamp(typeIdx, 0, int(rules().types.size()) - 1))];
    if (t.structure) return (t.footprintW % 2) ? std::floor(v) + 0.5f : std::round(v);
    return std::round(v * 4) / 4;
}

int Editor::pickEntity(const Viewport& vp,const Camera& cam,float sx,float sy) const {
    float3 origin{},direction{};
    const auto& pickCamera=view_.planetMode()?view_.planetCamera().camera():cam;
    if(!pickCamera.screenRay(sx-vp.originX,sy-vp.originY,vp.pointsW,vp.pointsH,origin,direction))return -1;
    const auto& owner=view_.modelResources();
    if(owner.generation()!=visualPickCacheGeneration_)return -1;
    int nearest=-1;float distance=std::numeric_limits<float>::infinity();
    for(size_t i=0;i<visualPickModelCount_;++i){
        const auto& packet=visualPickModels_[i];size_t index=packet.documentIndex;
        if(!packet.entityId.empty()&&(index>=doc().data.entities.size()||doc().data.entities[index].id!=packet.entityId)){
            index=0;while(index<doc().data.entities.size()&&doc().data.entities[index].id!=packet.entityId)++index;
        }
        if(index>=doc().data.entities.size())continue;
        const auto& entity=doc().data.entities[index];
        if(objectHidden(entity.owner,entity.type)||objectLocked(entity.owner,entity.type))continue;
        const auto lease=owner.acquireModelRead(packet.model);
        if(!lease)continue; // never dereference a retired or foreign Model
        const auto hit=render::pickModelSurface(*lease.get(),packet.world,origin,direction,1000000,
            packet.nodes.empty()?nullptr:&packet.nodes,packet.materials.empty()?nullptr:&packet.materials,
            packet.nodeMaterials.empty()?nullptr:&packet.nodeMaterials);
        if(hit&&hit.distance<distance){distance=hit.distance;nearest=int(index);}
    }
    return nearest;
}

float Editor::surfaceHeightAt(float x,float z) const {
    const float legacy=view_.heightAt(grid_,x,z);
    const auto& d=doc().data;
    if(d.extra.getStr("terrainPresentation","legacy")!="entities"||!std::isfinite(x)||!std::isfinite(z))return legacy;
    const auto& owner=view_.modelResources();
    if(owner.generation()!=visualPickCacheGeneration_)return legacy;
    // Rebuild with the document and the pick cache (models, transforms):
    // rasterize every terrain-tile triangle top-down into a 0.5 m grid once.
    const uint64_t key=doc().revision*1000003ull^visualPickCacheGeneration_^(uint64_t(visualPickModelCount_)<<40);
    constexpr float kStep=.5f;
    if(key!=surfaceHeightCacheKey_){
        surfaceHeightCacheKey_=key;
        surfaceGridW_=std::clamp(int(std::ceil(float(d.w)/kStep))+1,1,8192);
        surfaceGridH_=std::clamp(int(std::ceil(float(d.h)/kStep))+1,1,8192);
        surfaceGrid_.assign(size_t(surfaceGridW_)*size_t(surfaceGridH_),-std::numeric_limits<float>::infinity());
        for(size_t i=0;i<visualPickModelCount_;++i){
            const auto& packet=visualPickModels_[i];
            if(packet.documentIndex>=d.entities.size())continue;
            const auto& entity=d.entities[packet.documentIndex];
            if(entity.type.rfind("TerraTerrain",0)!=0&&!entity.extra["components"].has("TerraTerrain"))continue;
            const auto lease=owner.acquireModelRead(packet.model);
            if(!lease)continue;
            const auto& m=*lease.get();
            if(m.bindWorld.size()!=m.nodes.size())continue;
            for(size_t n=0;n<m.nodes.size();++n){
                const auto& node=m.nodes[n];
                if(node.mesh<0||size_t(node.mesh)>=m.meshes.size())continue;
                const mat4 xf=packet.world*(packet.nodes.size()==m.nodes.size()?packet.nodes[n]:m.bindWorld[n]);
                for(const auto& prim:m.meshes[size_t(node.mesh)].primitives){
                    if(prim.firstIndex>m.indices.size()||prim.indexCount>m.indices.size()-prim.firstIndex)continue;
                    for(size_t t=0;t+2<prim.indexCount;t+=3){
                        float3 v[3];bool ok=true;
                        for(int k=0;k<3;++k){const uint32_t idx=m.indices[prim.firstIndex+t+size_t(k)];if(idx>=m.vertices.size()){ok=false;break;}v[k]=xf.transformPoint(m.vertices[idx].pos);}
                        if(!ok)continue;
                        const float area=(v[1].x-v[0].x)*(v[2].z-v[0].z)-(v[1].z-v[0].z)*(v[2].x-v[0].x);
                        if(std::fabs(area)<1e-8f)continue;
                        const int x0=std::max(0,int(std::floor(std::min({v[0].x,v[1].x,v[2].x})/kStep)));
                        const int x1=std::min(surfaceGridW_-1,int(std::ceil(std::max({v[0].x,v[1].x,v[2].x})/kStep)));
                        const int z0=std::max(0,int(std::floor(std::min({v[0].z,v[1].z,v[2].z})/kStep)));
                        const int z1=std::min(surfaceGridH_-1,int(std::ceil(std::max({v[0].z,v[1].z,v[2].z})/kStep)));
                        for(int gz=z0;gz<=z1;++gz)for(int gx=x0;gx<=x1;++gx){
                            const float px=float(gx)*kStep,pz=float(gz)*kStep;
                            const float w0=((v[1].x-px)*(v[2].z-pz)-(v[1].z-pz)*(v[2].x-px))/area;
                            const float w1=((v[2].x-px)*(v[0].z-pz)-(v[2].z-pz)*(v[0].x-px))/area;
                            const float w2=1.f-w0-w1;
                            if(w0<-1e-4f||w1<-1e-4f||w2<-1e-4f)continue;
                            float& cell=surfaceGrid_[size_t(gz)*size_t(surfaceGridW_)+size_t(gx)];
                            cell=std::max(cell,w0*v[0].y+w1*v[1].y+w2*v[2].y);
                        }
                    }
                }
            }
        }
    }
    if(surfaceGrid_.empty())return legacy;
    // Bilinear over covered samples; uncovered -> the legacy surface.
    const float fx=x/kStep,fz=z/kStep;
    const int gx=int(std::floor(fx)),gz=int(std::floor(fz));
    float sum=0,weight=0;
    for(int dz=0;dz<2;++dz)for(int dx=0;dx<2;++dx){
        const int cx=gx+dx,cz=gz+dz;
        if(cx<0||cz<0||cx>=surfaceGridW_||cz>=surfaceGridH_)continue;
        const float h=surfaceGrid_[size_t(cz)*size_t(surfaceGridW_)+size_t(cx)];
        if(!std::isfinite(h))continue;
        const float w=(dx?fx-float(gx):1.f-(fx-float(gx)))*(dz?fz-float(gz):1.f-(fz-float(gz)));
        sum+=h*w;weight+=w;
    }
    return weight>1e-6f?sum/weight:legacy;
}

int Editor::pickRoad(const Viewport& vp, const Camera& cam, float sx, float sy) const {
    float x, z;
    if (!groundAt(vp, cam, sx, sy, x, z)) return -1;
    int best = -1;
    float bestD = 0.5f;
    const auto& d = ws_.map.data;
    for (size_t i = 0; i < d.roads.size(); ++i) {
        auto [a, b] = d.roads[i];
        float ax = d.entities[size_t(a)].x, az = d.entities[size_t(a)].z, bx = d.entities[size_t(b)].x, bz = d.entities[size_t(b)].z;
        float dx = bx - ax, dz = bz - az, l2 = dx * dx + dz * dz;
        float t = l2 > 0 ? std::clamp(((x - ax) * dx + (z - az) * dz) / l2, 0.0f, 1.0f) : 0.0f;
        float dd = std::hypot(ax + dx * t - x, az + dz * t - z);
        if (dd < bestD) { bestD = dd; best = int(i); }
    }
    return best;
}

// ============================ scene data ============================

void Editor::syncGrid() {
    const auto& d = doc().data;
    if (grid_.w != d.w || grid_.h != d.h) grid_.init(d.w, d.h);
    grid_.corner = d.corners;
    for (int i = 0; i < d.w * d.h; ++i) {
        grid_.flags[size_t(i)] = game::cellFlagsFromChar(d.cells[size_t(i)]);
        // diggable rock (rules "diggable"): solid cells drawn as rock blocks, like the client does
        const int dk = rules().findDiggable(d.cells[size_t(i)]);
        grid_.dig[size_t(i)] = dk >= 0 ? uint8_t(dk + 1) : 0;
        grid_.digHp[size_t(i)] = dk >= 0 ? rules().diggables[size_t(dk)].hardness : Fixed{};
        if (dk >= 0) grid_.flags[size_t(i)] = 0;
        grid_.height[size_t(i)] = d.heights[size_t(i)];
        grid_.level[size_t(i)] = d.levels[size_t(i)];
        grid_.region[size_t(i)] = d.regions[size_t(i)] == '.' ? int16_t(-1) : int16_t(d.regions[size_t(i)]);
        int8_t fr = d.fieldRes[size_t(i)];
        grid_.fieldRes[size_t(i)] = fr >= 0 ? int16_t(std::max(0, rules().findResource(d.fieldResources[size_t(fr)]))) : int16_t(-1);
        grid_.fieldAmt[size_t(i)] = Fixed::fromInt(d.fieldAmt[size_t(i)]);
    }
    // starting territory (rules territory.startRadius around owned structures), so the scene shows the claimed floor
    std::fill(grid_.territory.begin(), grid_.territory.end(), uint8_t(0));
    if (const int r = rules().territory.startRadius; rules().territory.enabled && r > 0 && !d.planet())
        for (const auto& e : d.entities) {
            const int ti = rules().findType(e.type);
            if (e.owner <= 0 || e.owner > 8 || ti < 0 || !rules().types[size_t(ti)].structure) continue;
            const game::UnitType& t = rules().types[size_t(ti)];
            int x0, y0; game::GameCore::footprintAt(t, {Fixed::fromRatio(int64_t(std::lround(e.x * 1000)), 1000), Fixed{}, Fixed::fromRatio(int64_t(std::lround(e.z * 1000)), 1000)}, x0, y0);   // editor only: float -> fixed
            for (int y = y0 - r; y < y0 + t.footprintH + r; ++y)
                for (int x = x0 - r; x < x0 + t.footprintW + r; ++x) {
                    if (!grid_.inside(x, y)) continue;
                    const size_t i = size_t(grid_.index(x, y));
                    if ((grid_.flags[i] & game::kCellGround) && !grid_.territory[i]) grid_.territory[i] = uint8_t(e.owner);
                }
        }
    view_.setTiles(d.extra["tiles"]);
    {   // planets: the world block (MapDoc bodies); sculpt / paint rebuild only what changed
        const Json world = d.planet() ? doc().worldJson() : Json();
        const std::string text = world.isObject() ? world.dump(0) : std::string();
        uint64_t h = 1469598103934665603ull;
        for (unsigned char c : text) { h ^= c; h *= 1099511628211ull; }
        if (h != worldHash_) {
            Json m = Json::object();
            if (!text.empty()) m.set("world", world);
            if (worldHash_ == 0 || !view_.planetMode() || text.empty()) view_.setWorld(m); else view_.updateWorld(m);
            worldHash_ = h;
        }
    }
    view_.setMapSurfaceEnabled(d.extra.getStr("terrainPresentation","legacy")!="entities");
    view_.setTerrainMaterial(d.extra["terrainMaterial"], d.w, d.h);
    if (!d.detail.empty() && d.detailScale > 0) {   // visual detail layer (TerrainOps sculpt / import)
        game::terrain::DetailSurface ds;
        game::terrain::detailFromInt16(d.w, d.h, d.detailScale, d.detail, ds);
        view_.setTerrainDetail(&ds, d.detailSplat.empty() ? nullptr : &d.detailSplat);
    } else view_.setTerrainDetail(nullptr, nullptr);
    view_.buildTerrain(*dev_, grid_);
    {   // spline roads (docs/roads.md): rebuilt when the network or the ground under it changes
        uint64_t h = 1469598103934665603ull;
        auto mix = [&h](const void* p, size_t n) { const auto* b = static_cast<const unsigned char*>(p); for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; } };
        const std::string rn = d.extra["roadNetwork"].dump(0);
        mix(rn.data(), rn.size());
        const std::string wn = d.extra["waterNetwork"].dump(0);   // water shores (docs/water.md)
        mix(wn.data(), wn.size());
        mix(d.heights.data(), d.heights.size() * sizeof(int16_t));
        mix(d.levels.data(), d.levels.size());
        mix(d.corners.data(), d.corners.size() * sizeof(int16_t));
        mix(d.detail.data(), d.detail.size() * sizeof(int16_t));
        if (h != roadHash_) { view_.setRoadMap(d.extra); roadHash_ = h; }
    }
    gridRevision_ = doc().revision;
}

bool Editor::planetAt(const Viewport& vp, float sx, float sy, int& body, render::float3& local) const {
    return view_.pickPlanet(view_.planetCamera().camera(), sx - vp.originX, sy - vp.originY, vp.pointsW, vp.pointsH, body, local);
}

Json Editor::planetPosAt(const Viewport& vp, float sx, float sy) const {
    int body; float3 local;
    if (!planetAt(vp, sx, sy, body, local)) return Json();
    const game::PlanetSurface* s = view_.planets().surface(body);
    if (!s) return Json();
    int f; float a, b;
    s->faceFromDir(normalize(local), f, a, b);
    Json at = Json::object();
    at.set("body", Json::string(planetBodyId(body)));
    at.set("face", Json::number(int64_t(f)));
    at.set("x", Json::number(double(a), 2));
    at.set("y", Json::number(double(b), 2));
    return at;
}

bool Editor::planetLocal(const Json& at, int& body, float3& local) const {
    if (!at.isObject()) return false;
    body = view_.planets().world().find(at.getStr("body"));
    return body >= 0 && view_.facePoint(body, int(at.getInt("face", -1)), float(at["x"].toDouble()), float(at["y"].toDouble()), local);
}

std::string Editor::planetBodyId(int body) const {
    const auto& W = view_.planets().world();
    return body >= 0 && size_t(body) < W.bodies.size() ? W.bodies[size_t(body)].id : std::string();
}

void Editor::buildEditScene(game::GameScene& out) const {
    const auto& d = ws_.map.data;
    out.objects.clear();
    out.roads.clear();
    out.moveMarkers.clear();
    // Keep these caches local to the frame: rules, visibility and selections can
    // change through editor commands without a map revision. Cache the expensive
    // type lookup and visibility result for repeated entity types/owners.
    out.objects.reserve(d.entities.size() + (showGameObjects ? world_.rows.size() : 0));
    out.roads.reserve(d.roads.size());
    std::unordered_map<std::string, int> typeCache;
    typeCache.reserve(std::min(d.entities.size(), rules().types.size()));
    std::unordered_map<std::string, bool> visibilityCache;
    visibilityCache.reserve(std::min(d.entities.size(), rules().types.size() * size_t(4)));
    std::vector<uint8_t> selectedEntities(d.entities.size(), 0);
    for (const Selection& item : selectionSet)
        if (item.kind == Selection::Entity && item.index >= 0 && size_t(item.index) < selectedEntities.size())
            selectedEntities[size_t(item.index)] = 1;
    if (selection.kind == Selection::Entity && selection.index >= 0 && size_t(selection.index) < selectedEntities.size())
        selectedEntities[size_t(selection.index)] = 1;
    auto visible = [&](int owner, const std::string& type) {
        std::string key = std::to_string(owner);
        key.push_back('/');
        key += type;
        auto it = visibilityCache.find(key);
        if (it != visibilityCache.end()) return it->second;
        return visibilityCache.emplace(std::move(key), !objectHidden(owner, type)).first->second;
    };
    auto inSet = [&](const Selection& item) { return item == selection || std::find(selectionSet.begin(), selectionSet.end(), item) != selectionSet.end(); };
    // A document scene can contain thousands of unchanged static placements.
    // Reuse their type resolution, facing, position and visibility until the
    // map, project rules or visibility filters change. Do not cache the world
    // preview (it has an independent asynchronous lifetime). Selection and
    // hover are applied to fresh output copies every frame.
    static const bool cacheEditScene = [] {
        const char* value = std::getenv("AIVERSE_EDITOR_SCENE_CACHE");
        return value && std::strcmp(value, "1") == 0;
    }();
    const bool cacheEligible = cacheEditScene && !d.planet();
    const bool cacheHit = cacheEligible && editSceneCacheValid_ &&
        editSceneCacheMapRevision_ == ws_.map.revision &&
        editSceneCacheRulesRevision_ == static_cast<uint64_t>(rulesRevision_) &&
        editSceneCacheEntities_ == d.entities.data() &&
        editSceneCacheEntityCount_ == d.entities.size() &&
        editSceneCacheHidden_ == sceneHidden;
    if (cacheHit) {
        for (const EditSceneCacheEntry& entry : editSceneCache_) {
            game::SceneObject o = entry.object;
            const size_t index = entry.documentIndex;
            o.selected = selectedEntities[index] != 0 || int(index) == roadStart;
            o.hovered = hover.kind == Selection::Entity && hover.index == int(index);
            out.objects.push_back(std::move(o));
        }
    } else {
        if (cacheEligible) {
            editSceneCacheValid_ = false;
            editSceneCache_.clear();
            editSceneCache_.reserve(d.entities.size());
        }
    for (size_t i = 0; i < d.entities.size(); ++i) {
        const auto& e = d.entities[i];
        auto typeIt = typeCache.find(e.type);
        if (typeIt == typeCache.end())
            typeIt = typeCache.emplace(e.type, rules().findType(e.type)).first;
        const int t = typeIt->second;
        if (t < 0 || !visible(e.owner, e.type)) continue;
        game::SceneObject o;
        // Editor document entity index is stable while the document structure
        // is unchanged. If indices shift, changed records are uploaded again.
        // Keep these keys in a separate namespace from simulation entity IDs.
        o.persistentKey = (uint64_t(0xED) << 56) | (uint64_t(i) + 1);
        o.type = uint16_t(t);
        o.owner = e.owner < 0 ? uint8_t(game::kHostileNeutral) : uint8_t(std::clamp(e.owner, 0, 16));
        o.pos = {e.x, 0, e.z};
        if(authoredEntity(e)){
            if(e.extra["components"].has("NereidWater"))continue;
            const auto local=authoredLocalTransform(e);o.pos.y=float(local.position.y.toDouble());o.parented=true;o.parentFrame=authoredModelMatrix(e);
        }
        if (e.extra.has("facing")) {   // degrees clockwise from north (-z), as GameCore spawns it: 0 north, 90 east, 180 south
            const float a = float(e.extra["facing"].toDouble()) * 3.14159265f / 180.0f;
            o.facing = {std::sin(a), 0, -std::cos(a)};
        }
        if (d.planet()) { // body-local surface point of "at"
            int body; float3 local;
            if (!planetLocal(e.at, body, local)) continue;
            o.body = int16_t(body); o.pos = local;
        }
        if (rules().types[size_t(t)].garrisonMax > 0) o.garrison = e.garrison >= 0 ? e.garrison : 0;
        const Selection me{Selection::Entity, int(i), {}};
        o.selected = selectedEntities[i] != 0 || int(i) == roadStart;
        o.hovered = hover == me;
        if (cacheEligible) {
            game::SceneObject base = o;
            base.selected = base.hovered = false;
            editSceneCache_.push_back({i, std::move(base)});
        }
        out.objects.push_back(std::move(o));
    }
        if (cacheEligible) {
            editSceneCacheMapRevision_ = ws_.map.revision;
            editSceneCacheRulesRevision_ = rulesRevision_;
            editSceneCacheEntities_ = d.entities.data();
            editSceneCacheEntityCount_ = d.entities.size();
            editSceneCacheHidden_ = sceneHidden;
            editSceneCacheValid_ = true;
        }
    }
    // what the game creates at start (world tree preview: faction start units, script / C++ module objects); read-only
    if (showGameObjects && !worldLive_ && !d.planet() && !sceneHidden.count("game")) wantWorld();   // the preview is built for the Scene too
    if (showGameObjects && !worldLive_ && !d.planet() && !sceneHidden.count("game"))
        for (auto& r : world_.rows) {
            if (r.doc >= 0 || r.type < 0 || !r.hasPos || (r.flags & game::kUnitOffMap) || size_t(r.type) >= rules().types.size()) continue;
            if (!visible(r.owner, rules().types[size_t(r.type)].id)) continue;
            game::SceneObject o;
            o.type = uint16_t(r.type);
            o.owner = r.owner < 0 ? uint8_t(game::kHostileNeutral) : uint8_t(std::clamp(r.owner, 0, 16));
            o.pos = {r.x, 0, r.z};
            o.hp = r.maxHp > 0 ? std::clamp(r.hp / r.maxHp, 0.0f, 1.0f) : 1.0f;
            Selection me{Selection::Live, -1, {}};
            me.ref = r.ref;
            o.selected = inSet(me);
            o.hovered = hover == me;
            out.objects.push_back(o);
        }
    if (!d.planet()) for (auto [a, b] : d.roads) out.roads.push_back({{d.entities[size_t(a)].x, 0, d.entities[size_t(a)].z}, {d.entities[size_t(b)].x, 0, d.entities[size_t(b)].z}});
}

void Editor::drawEditOverlays3D() {
    const auto& d = doc().data;
    const float W = float(d.w), H = float(d.h);
    uint32_t border = rgba(255, 255, 255, 120);
    r3d_.line({0, 0.05f, 0}, {W, 0.05f, 0}, border); r3d_.line({W, 0.05f, 0}, {W, 0.05f, H}, border);
    r3d_.line({W, 0.05f, H}, {0, 0.05f, H}, border); r3d_.line({0, 0.05f, H}, {0, 0.05f, 0}, border);
    auto cellQuad = [&](float x0, float z0, float x1, float z1, uint32_t col, float y) {
        r3d_.triangle({x0, y, z0}, {x0, y, z1}, {x1, y, z1}, col);
        r3d_.triangle({x0, y, z0}, {x1, y, z1}, {x1, y, z0}, col);
    };
    for (int y = 0; y < d.h; ++y)
        for (int x = 0; x < d.w; ++x) {
            size_t i = d.idx(x, y);
            bool field = d.fieldRes[i] >= 0 && d.fieldAmt[i] > 0, region = showRegions && d.regions[i] != '.', ramp = showLevels && d.cells[i] == '/';
            if (!field && !region && !ramp) continue;
            float gy = view_.heightAt(grid_, x + 0.5f, y + 0.5f) + 0.06f;
            if (field) {
                uint32_t a = uint32_t(60 + d.fieldAmt[i] * 18);
                uint32_t col = d.fieldRes[i] % 3 == 0 ? rgba(240, 170, 40, uint8_t(a)) : d.fieldRes[i] % 3 == 1 ? rgba(80, 220, 255, uint8_t(a)) : rgba(230, 90, 220, uint8_t(a));
                cellQuad(x + 0.08f, y + 0.08f, x + 0.92f, y + 0.92f, col, gy);
            }
            if (region) {
                char c = d.regions[i];
                uint32_t col = rgba(uint8_t(80 + (c * 47) % 175), uint8_t(80 + (c * 91) % 175), uint8_t(80 + (c * 23) % 175), 230);
                if (d.inside(x + 1, y) && d.regions[d.idx(x + 1, y)] != c) r3d_.line({x + 1.0f, gy + 0.02f, float(y)}, {x + 1.0f, gy + 0.02f, y + 1.0f}, col);
                if (d.inside(x, y + 1) && d.regions[d.idx(x, y + 1)] != c) r3d_.line({float(x), gy + 0.02f, y + 1.0f}, {x + 1.0f, gy + 0.02f, y + 1.0f}, col);
            }
            if (ramp) cellQuad(x + 0.2f, y + 0.2f, x + 0.8f, y + 0.8f, rgba(255, 255, 255, 60), gy);
        }
    if (showAreas)
        for (auto& [name, a] : d.extra["areas"].members()) {
            float x0 = float(a[0].toDouble()), z0 = float(a[1].toDouble()), x1 = x0 + float(a[2].toDouble(1)), z1 = z0 + float(a[3].toDouble(1));
            bool sel = selection.kind == Selection::Area && selection.key == name;
            uint32_t c = sel ? rgba(255, 255, 255, 255) : rgba(255, 220, 80, 200);
            r3d_.line({x0, 0.3f, z0}, {x1, 0.3f, z0}, c); r3d_.line({x1, 0.3f, z0}, {x1, 0.3f, z1}, c);
            r3d_.line({x1, 0.3f, z1}, {x0, 0.3f, z1}, c); r3d_.line({x0, 0.3f, z1}, {x0, 0.3f, z0}, c);
            if (sel) cellQuad(x0, z0, x1, z1, rgba(255, 220, 80, 50), 0.3f);
        }
    for (size_t p = 0; p < d.players.size(); ++p) {
        if (!d.players[p].hasStart) continue;
        float4 c = game::GameView::playerColor(int(p + 1));
        float sx = d.players[p].startX, sz = d.players[p].startZ, gy = view_.heightAt(grid_, sx, sz);
        r3d_.circleXZ({sx, 0, sz}, 1.2f, rgbaf({c.x, c.y, c.z, 1}), 24, gy + 0.15f);
        r3d_.line({sx, gy, sz}, {sx, gy + 2.5f, sz}, rgbaf({c.x, c.y, c.z, 1}));
    }
    // authored routes (map "routes", docs/game-format.md § Routes): every line, line 0 brighter
    for (const auto& rl : routePreviewLines(rules(), doc())) {
        const uint32_t c = rl.line == 0 ? rgba(255, 240, 160, 230) : rgba(220, 190, 110, 120);
        for (size_t i = 1; i < rl.pts.size(); ++i) {
            const auto& a = rl.pts[i - 1]; const auto& b = rl.pts[i];
            r3d_.line({a[0], view_.heightAt(grid_, a[0], a[1]) + 0.12f, a[1]}, {b[0], view_.heightAt(grid_, b[0], b[1]) + 0.12f, b[1]}, c);
        }
    }
    for (auto& f : flashes_) {
        float k = float(std::clamp((f.until - time_) / f.length, 0.0, 1.0));
        uint32_t c = rgba(120, 220, 255, uint8_t(40 + 150 * k));
        float y = 0.35f + (1 - k) * 0.4f;
        cellQuad(f.x0, f.z0, f.x1, f.z1, rgba(120, 220, 255, uint8_t(70 * k)), y);
        r3d_.line({f.x0, y, f.z0}, {f.x1, y, f.z0}, c); r3d_.line({f.x1, y, f.z0}, {f.x1, y, f.z1}, c);
        r3d_.line({f.x1, y, f.z1}, {f.x0, y, f.z1}, c); r3d_.line({f.x0, y, f.z1}, {f.x0, y, f.z0}, c);
    }
}

void Editor::queueSpectraGpuPreview(fx::EffectDef effect,
    std::array<float,8> params,fx::float3 position,bool inGame,bool loop) {
    spectraPreviewRequest_.effect=std::move(effect);
    spectraPreviewRequest_.params=params;
    spectraPreviewRequest_.position=position;
    spectraPreviewRequest_.inGame=inGame;
    spectraPreviewRequest_.loop=loop;
    spectraPreviewRequest_.pending=true;
    spectraPreviewStop_=false;
}
void Editor::stopSpectraGpuPreview() {
    spectraPreviewRequest_.pending=false;
    spectraPreviewStop_=true; // resources release before a future scene pass
    spectraPreviewRestart_=false;
    spectraPreviewStepRequests_=0;
}
void Editor::setSpectraGpuPreviewSpeed(float speed) {
    if(std::isfinite(speed))
        spectraPreviewSpeed_=std::clamp(speed,0.1f,2.0f);
}
bool Editor::queueSpectraGpuPreviewFrames(uint32_t frames) {
    // Advance only while frozen, where a frame request has an unambiguous
    // 1/60-second visual-only meaning. No secret GPU frame replay/scrubbing.
    constexpr uint32_t kMaxAdvanceFrames=300;
    if(!spectraGpuPreviewActive() || !spectraPreviewFrozen_ || frames==0 ||
       frames>kMaxAdvanceFrames ||
       spectraPreviewStepRequests_>kMaxAdvanceFrames-frames)
        return false;
    spectraPreviewStepRequests_+=frames;
    return true;
}
bool Editor::restartSpectraGpuPreview() {
    if(!spectraGpuPreviewActive() || spectraPreviewStop_)return false;
    spectraPreviewRestart_=true; // executes before the next scene pass
    spectraPreviewStepRequests_=0;
    return true;
}
bool Editor::requestSpectraGpuPreviewCheckpoint() {
    if(!spectraGpuPreviewActive() || spectraPreviewStop_)return false;
    // The GPU must stop changing particle state while readback chunks
    // complete on a later graphics frame. Queued Step GPU is cancelled.
    spectraPreviewFrozen_=true;
    spectraPreviewStepRequests_=0;
    spectraGpuCaptureState_=SpectraGpuCaptureState::Queued;
    spectraGpuCaptureNote_="Awaiting a frozen graphics pre-pass";
    return true;
}
bool Editor::queueSpectraExternalGpuCapture(size_t groupIndex) {
    // This is only a request for another native graphics owner to consume
    // at its own pre-render phase. Never perform Metal reads from ImGui.
    return spectraGpuMailbox_ && spectraGpuTelemetry_ &&
           spectraGpuTelemetry_!=spectraPreviewGpu_.get() &&
           spectraGpuTelemetry_->ready() &&
           groupIndex<spectraGpuTelemetry_->gpuGroupCount() &&
           spectraGpuMailbox_->request(groupIndex);
}
bool Editor::releaseSpectraExternalGpuCapture() {
    return spectraGpuMailbox_ &&
           spectraGpuMailbox_->requestRelease();
}
bool Editor::queueSpectraExternalVisualReset() {
    // This cannot encode a Metal reset. The native host receives status
    // only and services it after Device.beginFrame at its own safe point.
    return spectraGpuRecoveryHost_ &&
           spectraGpuTelemetry_ &&
           spectraGpuTelemetry_!=spectraPreviewGpu_.get() &&
           spectraGpuRecoveryHost_->requestPresentationReset(
               "explicit editor VFX-only reset");
}

bool Editor::saveFreshSpectraGpuCheckpoint(
    std::string& output,size_t requestedGroup) const {
    const auto* pool=spectraGpuTelemetry_;
    if(!pool || !pool->ready() || pool->presentationBlocked()) {
        output="No healthy active SpectraFX GPU pool is attached";
        return false;
    }
    // Export only existing current-revision snapshot bytes. This calls no
    // blocking RHI readBuffer and never requests a new GPU capture.
    std::vector<uint8_t> bytes;
    std::string reason;
    size_t chosen=0;
    bool found=false;
    if(requestedGroup!=static_cast<size_t>(-1)) {
        if(requestedGroup>=pool->gpuGroupCount()) {
            output="Selected SpectraFX GPU group is out of range";
            return false;
        }
        chosen=requestedGroup;
        found=pool->exportFreshCheckpoint(
            chosen,bytes,spectra::kGpuCheckpointExportMaxBytes,&reason);
    } else {
        // Backwards compatible single-argument API: select first ready group.
        for(size_t i=0;i<pool->gpuGroupCount();++i) {
            if(pool->exportFreshCheckpoint(
                i,bytes,spectra::kGpuCheckpointExportMaxBytes,&reason)) {
                chosen=i;found=true;break;
            }
        }
    }
    if(!found) {
        output="No fresh and complete GPU checkpoint is ready: "+reason;
        return false;
    }
    // Same-build diagnostic file integrity is independently verifiable.
    // Never write bytes that fail the V2 length/magic/CRC contract.
    if(!spectra::validateGpuCheckpointDiagnostic(bytes)) {
        output="SpectraFX checkpoint export failed V2 header/CRC validation";
        return false;
    }
    const fs::path directory=fs::path(platform::userDataDir())/
                             "spectrafx-diagnostics";
    std::error_code ec;
    fs::create_directories(directory,ec);
    if(ec) {
        output="Cannot create SpectraFX diagnostic directory: "+ec.message();
        return false;
    }
    const std::string filename="checkpoint-"+
        std::to_string(dev_?dev_->frameSerial():0)+
        "-group-"+std::to_string(chosen)+".spfx";
    const fs::path destination=directory/filename;
    const fs::path temporary=directory/(filename+".tmp");
    {
        std::ofstream file(temporary,std::ios::binary|std::ios::trunc);
        if(!file) {
            output="Cannot open SpectraFX diagnostic temporary file";
            return false;
        }
        file.write(reinterpret_cast<const char*>(bytes.data()),
                   std::streamsize(bytes.size()));
        file.flush();
        if(!file) {
            file.close();
            fs::remove(temporary,ec);
            output="Failed to write complete SpectraFX checkpoint payload";
            return false;
        }
    }
    fs::rename(temporary,destination,ec);
    if(ec) {
        fs::remove(temporary);
        output="Cannot publish SpectraFX checkpoint: "+ec.message();
        return false;
    }
    output=destination.string();
    return true;
}
bool Editor::saveSafeSpectraGpuReport(
    std::string& output,size_t groupIndex,
    bool includeWorldBounds,bool includeGpuIdentifiers) const {
    const auto* pool=spectraGpuTelemetry_;
    if(!pool || !pool->ready() || pool->presentationBlocked() ||
       groupIndex>=pool->gpuGroupCount()) {
        output="A healthy SpectraFX pool and valid GPU group are required";
        return false;
    }
    // This is an EXPLICIT local action. It copies one already-completed
    // checkpoint through the strict Stage-39 freshness gate without RHI reads.
    std::vector<uint8_t> diagnostic;
    std::string why;
    if(!pool->exportFreshCheckpoint(groupIndex,diagnostic,
            spectra::kGpuCheckpointExportMaxBytes,&why)) {
        output="No current-revision GPU checkpoint is ready: "+why;
        return false;
    }
    const auto inspected=spectra::inspectGpuCheckpointDiagnostic(diagnostic);
    if(!inspected.structurallyValid()) {
        output="SpectraFX diagnostic CRC or header failed integrity check";
        return false;
    }
    const spectra::GpuDiagnosticReportPrivacy privacy{
        includeWorldBounds,includeGpuIdentifiers
    };
    // The JSON report NEVER includes the original GPU blob, individual
    // particle positions or VM scalar values. Even if includeWorldBounds
    // is true, only min/max coordinates are included, never per-slot data.
    Json report=Json::parse(spectra::gpuDiagnosticReportJson(inspected,privacy));
    report.set("groupIndex",Json::number(int64_t(groupIndex)));
    Json timeline=Json::array();
    // An externally attached GPU pool belongs to another renderer. Do not
    // include the Editor's unrelated preview-history samples in its report.
    const size_t historyCount=
        pool==spectraPreviewGpu_.get()?spectraPreviewHistory_.size():0u;
    for(size_t i=0;i<historyCount;++i) {
        const auto& sample=spectraPreviewHistory_[i];
        Json row=Json::object();
        row.set("sample",Json::number(int64_t(i)));
        row.set("estimatedParticles",
                Json::number(int64_t(sample.estimatedParticles)));
        row.set("stepAccepted",Json::fromBool(sample.stepAccepted));
        row.set("freshGroups",Json::number(int64_t(sample.freshGroups)));
        row.set("staleGroups",Json::number(int64_t(sample.staleGroups)));
        row.set("pendingGroups",Json::number(int64_t(sample.pendingGroups)));
        row.set("killsAwaitingCompletion",
                Json::number(int64_t(sample.killedSlotsWaiting)));
        row.set("deviceHealthy",Json::fromBool(sample.deviceHealthy));
        // No absolute graphics frame serial, visual time or scene pose.
        timeline.push(std::move(row));
    }
    report.set("previewTimelineSummary",std::move(timeline));
    report.set("automaticUpload",Json::fromBool(false));
    const std::string serialized=report.dump(2);
    const fs::path directory=fs::path(platform::userDataDir())/
                             "spectrafx-diagnostics";
    std::error_code ec;
    fs::create_directories(directory,ec);
    if(ec) {
        output="Cannot create diagnostics directory: "+ec.message();
        return false;
    }
    const fs::path target=unusedSafeDiagnosticName(
        directory,"safe-report",groupIndex,".json");
    if(target.empty()) {
        output="No unused privacy-safe diagnostic filename is available";
        return false;
    }
    const fs::path tmp=target.string()+".tmp";
    {
        std::ofstream file(tmp,std::ios::binary|std::ios::trunc);
        if(!file) {
            output="Cannot open redacted report temporary file";
            return false;
        }
        file.write(serialized.data(),std::streamsize(serialized.size()));
        file.flush();
        if(!file) {
            file.close();
            fs::remove(tmp,ec);
            output="Failed to write redacted SpectraFX report";
            return false;
        }
    }
    fs::rename(tmp,target,ec);
    if(ec) {
        fs::remove(tmp);
        output="Cannot publish redacted report: "+ec.message();
        return false;
    }
    output=target.string();
    return true;
}
bool Editor::saveSafeSpectraGpuBundle(
    std::string& output,size_t groupIndex) const {
    const auto* pool=spectraGpuTelemetry_;
    if(!pool || !pool->ready() || pool->presentationBlocked() ||
       groupIndex>=pool->gpuGroupCount()) {
        output="Safe ZIP requires a healthy pool and valid group";
        return false;
    }
    std::vector<uint8_t> diagnostic;
    std::string error;
    if(!pool->exportFreshCheckpoint(groupIndex,diagnostic,
            spectra::kGpuCheckpointExportMaxBytes,&error)) {
        output="Safe ZIP needs an existing fresh GPU checkpoint: "+error;
        return false;
    }
    const spectra::GpuDiagnosticInspection inspected=
        spectra::inspectGpuCheckpointDiagnostic(diagnostic);
    if(!inspected.structurallyValid()) {
        output="Safe ZIP rejected invalid checkpoint CRC/header";
        return false;
    }
    // Stronger policy than ordinary JSON report: every ZIP entry is created
    // from strict fixed-schema aggregate values. Raw bytes are NEVER included
    // regardless of sensitive checkboxes on the SpectraFX window.
    std::vector<spectra::GpuSafeTimelineFrame> frames;
    const size_t historyCount=
        pool==spectraPreviewGpu_.get()?spectraPreviewHistory_.size():0u;
    frames.reserve(historyCount);
    for(size_t i=0;i<historyCount;++i) {
        const auto& src=spectraPreviewHistory_[i];
        spectra::GpuSafeTimelineFrame frame{};
        frame.sample=uint32_t(i);
        frame.estimatedParticles=src.estimatedParticles;
        frame.freshGroups=src.freshGroups;
        frame.staleGroups=src.staleGroups;
        frame.pendingGroups=src.pendingGroups;
        frame.killsAwaitingCompletion=src.killedSlotsWaiting;
        frame.stepAccepted=src.stepAccepted;
        frame.deviceHealthy=src.deviceHealthy;
        frames.push_back(frame);
    }
    std::vector<uint8_t> zip;
    if(!spectra::makeSafeGpuDiagnosticZip(inspected,frames,zip)) {
        output="Safe ZIP exceeded 128 KiB or has invalid diagnostic state";
        return false;
    }
    const fs::path directory=fs::path(platform::userDataDir())/
                             "spectrafx-diagnostics";
    std::error_code ec;
    fs::create_directories(directory,ec);
    if(ec) {
        output="Cannot create SpectraFX diagnostic directory: "+ec.message();
        return false;
    }
    const fs::path target=unusedSafeDiagnosticName(
        directory,"safe-bundle",groupIndex,".zip");
    if(target.empty()) {
        output="No unused privacy-safe ZIP filename is available";
        return false;
    }
    const fs::path temporary=target.string()+".tmp";
    {
        std::ofstream out(temporary,std::ios::binary|std::ios::trunc);
        if(!out) {
            output="Cannot open SpectraFX safe ZIP temporary file";
            return false;
        }
        out.write(reinterpret_cast<const char*>(zip.data()),
                  std::streamsize(zip.size()));
        out.flush();
        if(!out) {
            out.close();
            fs::remove(temporary,ec);
            output="Failed to write complete SpectraFX safe ZIP";
            return false;
        }
    }
    fs::rename(temporary,target,ec);
    if(ec) {
        fs::remove(temporary);
        output="Cannot publish SpectraFX safe ZIP: "+ec.message();
        return false;
    }
    output=target.string();
    return true;
}
bool Editor::spectraGpuPreviewActive() const {
    return spectraPreviewGpu_ && spectraPreviewGpu_->ready();
}
std::string Editor::spectraGpuPreviewHistoryJson() const {
    Json out=Json::object();
    out.set("kind",Json::string("spectrafx_gpu_preview_diagnostics"));
    out.set("replayable",Json::fromBool(false));
    out.set("historyCapacity",Json::number(int64_t(180)));
    Json frames=Json::array();
    for(const auto& frame:spectraPreviewHistory_) {
        Json item=Json::object();
        item.set("deviceFrame",Json::number(int64_t(frame.deviceFrame)));
        item.set("visualSeconds",Json::number(frame.visualSeconds,6));
        item.set("estimatedParticles",Json::number(int64_t(frame.estimatedParticles)));
        item.set("freshGroups",Json::number(int64_t(frame.freshGroups)));
        item.set("staleGroups",Json::number(int64_t(frame.staleGroups)));
        item.set("pendingGroups",Json::number(int64_t(frame.pendingGroups)));
        item.set("killedSlotsWaiting",Json::number(int64_t(frame.killedSlotsWaiting)));
        item.set("stepAccepted",Json::fromBool(frame.stepAccepted));
        item.set("deviceHealthy",Json::fromBool(frame.deviceHealthy));
        frames.push(std::move(item));
    }
    out.set("frames",std::move(frames));
    return out.dump(2);
}
void Editor::retireSpectraPreview(std::unique_ptr<spectra::GpuPoolWorld>& pool) {
    if(!pool)return;
    // Includes any references already encoded in this frame. Unknown fences
    // retain the owner until shutdown proves complete GPU idle.
    retiredSpectraPreviews_.push_back({dev_?dev_->gpuSubmissionFence():0,std::move(pool)});
}
void Editor::collectRetiredSpectraPreviews() {
    const uint64_t done=dev_ && dev_->gpuHealthy()?dev_->gpuCompletedFence():0;
    if(!done)return;
    std::erase_if(retiredSpectraPreviews_,[done](const auto& owner) {
        return owner.fence && owner.fence<=done;
    });
}
void Editor::tickSpectraGpuPreview(const Camera& cam,int w,int h,bool game) {
    collectRetiredSpectraPreviews();
    if(spectraPreviewStop_) {
        if(spectraGpuTelemetry_==spectraPreviewGpu_.get())
            spectraGpuTelemetry_=nullptr;
        retireSpectraPreview(spectraPreviewGpu_);
        spectraPreviewStop_=false;
        spectraPreviewLoop_=false;
        spectraPreviewPaused_=spectraPreviewFrozen_=false;
        spectraPreviewStepRequests_=0;
        spectraPreviewRestart_=false;
        spectraPreviewVisualSeconds_=0;
        spectraPreviewLastError_.clear();
        spectraPreviewHistory_.clear();
        spectraGpuCaptureState_=SpectraGpuCaptureState::Idle;
        spectraGpuCaptureNote_.clear();
    }
    if(spectraPreviewRequest_.pending && spectraPreviewRequest_.inGame==game &&
       retiredSpectraPreviews_.size()>=8) {
        spectraPreviewLastError_="Metal preview replacement waiting for GPU resource retirement";
    }
    if(spectraPreviewRequest_.pending && spectraPreviewRequest_.inGame==game &&
       retiredSpectraPreviews_.size()<8) {
        auto pending=std::move(spectraPreviewRequest_);
        spectraPreviewRequest_.pending=false;
        auto candidate=std::make_unique<spectra::GpuPoolWorld>();
        spectra::PoolWorldBudgets budgets;
        budgets.maxGpuGroups=1;
        bool ok=candidate->init(dev_,budgets);
        if(ok) {
            spectra::GraphRuntimeOptions options;
            options.preference=spectra::ComputePreference::RequireGpu;
            options.quality=fx::FxQuality::High;
            options.gpuProgram=true;options.gpuMaterial=true;
            // The Metal preview is opt-in. Bound periodic checkpoints make
            // freshness/restore diagnostics and the Stage 39 export useful;
            // all normal native projects retain checkpointEveryFrames=0.
            options.checkpointEveryFrames=15;
            options.adaptiveCheckpoint.enabled=true;
            options.adaptiveCheckpoint.maxIntervalFrames=120;
            ok=candidate->defineEffect("__editor_gpu_preview",pending.effect,options);
        }
        if(!ok) {
            spectraPreviewLastError_="Metal preview not available: "+candidate->reason();
            ws_.log("spectrafx",spectraPreviewLastError_,true);
            retireSpectraPreview(candidate);
        } else {
            if(spectraGpuTelemetry_==spectraPreviewGpu_.get())
                spectraGpuTelemetry_=nullptr;
            retireSpectraPreview(spectraPreviewGpu_);
            spectraPreviewGpu_=std::move(candidate);
            spectraGpuTelemetry_=spectraPreviewGpu_.get();
            spectraPreviewInGame_=game;
            spectraPreviewSimulatedFrame_=~uint64_t{0};
            spectraPreviewLoop_=pending.loop;
            spectraPreviewPaused_=spectraPreviewFrozen_=false;
            spectraPreviewSpeed_=1.f;
            spectraPreviewStepRequests_=0;
            spectraPreviewRestart_=false;
            spectraPreviewVisualSeconds_=0;
            spectraPreviewLastError_.clear();
            spectraPreviewHistory_.clear();
            spectraGpuCaptureState_=SpectraGpuCaptureState::Idle;
            spectraGpuCaptureNote_.clear();
            spectraPreviewParams_=pending.params;
            spectraPreviewPosition_=pending.position;
            spectraPreviewNextBurst_=-1; // guaranteed first-frame spawn
        }
    }
    if(!spectraPreviewGpu_ || spectraPreviewInGame_!=game || w<=0 || h<=0)
        return;
    if(spectraPreviewRestart_) {
        // resetForRollback is a restartable transaction: keep the request
        // until it is accepted (retried next frame) and log only when the
        // failure text changes, instead of a dead preview + per-frame log.
        if(!spectraPreviewGpu_->resetForRollback()) {
            const std::string error="Metal preview restart rejected: "+
                                    spectraPreviewGpu_->reason();
            if(error!=spectraPreviewLastError_) {
                spectraPreviewLastError_=error;
                ws_.log("spectrafx",spectraPreviewLastError_,true);
            }
            return;
        }
        spectraPreviewRestart_=false;
        spectraPreviewVisualSeconds_=0;
        spectraPreviewHistory_.clear();
        spectraGpuCaptureState_=SpectraGpuCaptureState::Idle;
        spectraGpuCaptureNote_.clear();
        spectraPreviewNextBurst_=-1;
        spectraPreviewStepRequests_=0;
        spectraPreviewLastError_.clear();
    }
    const auto viewProjection=cam.proj(float(w)/float(h),
        dev_->info().clipDepthZeroToOne)*cam.view();
    if(!spectraPreviewGpu_->beginPresentationFrame(cam,viewProjection,w,h)) {
        const std::string error="Metal preview begin: "+spectraPreviewGpu_->reason();
        if(error!=spectraPreviewLastError_) { // once per state change, not per frame
            spectraPreviewLastError_=error;
            ws_.log("spectrafx",spectraPreviewLastError_,true);
        }
        return;
    }
    spectraPreviewFrameOpen_=true;
    // Scene and Game can render the same preview in one device frame.
    // Rebuild view-dependent billboards for each, but advance time/births once.
    const uint64_t previewFrame=dev_->frameSerial();
    const bool advancePreview=spectraPreviewSimulatedFrame_!=previewFrame;
    spectraPreviewSimulatedFrame_=previewFrame;
    if(advancePreview && !spectraPreviewPaused_ && !spectraPreviewFrozen_ &&
       (spectraPreviewNextBurst_<0 ||
       (spectraPreviewLoop_ &&
        spectraPreviewVisualSeconds_>=spectraPreviewNextBurst_))) {
        if(!spectraPreviewGpu_->burst("__editor_gpu_preview",
                spectraPreviewPosition_,{0,1,0},{1,1,1,1},&spectraPreviewParams_))
            ws_.log("spectrafx","Metal preview emission: "+spectraPreviewGpu_->reason(),true);
        // Repeat using visual elapsed time, not the editor's wall clock.
        spectraPreviewNextBurst_=spectraPreviewVisualSeconds_+0.5;
    }
    // Freeze stops visual time; Pause only blocks future births while living
    // particles continue updating. Neither state changes gameplay simulation.
    float dt=(!advancePreview || spectraPreviewFrozen_)?0.f:
        std::clamp(ImGui::GetIO().DeltaTime*spectraPreviewSpeed_,0.f,0.1f);
    if(advancePreview && spectraPreviewFrozen_ && spectraPreviewStepRequests_>0) {
        dt=1.f/60.f; // one simulation frame, no new bursts while frozen
        --spectraPreviewStepRequests_;
    }
    graph_gpu_profile::Scope fxGpuScope(*dev_,graphGpuProfile_,"spectrafx-preview-step",game?2ULL:1ULL);
    const bool accepted=spectraPreviewGpu_->step(dt);
    if(accepted)fxGpuScope.finish();else fxGpuScope.abort();
    if(!accepted) {
        spectraPreviewLastError_="Metal preview step: "+spectraPreviewGpu_->reason();
        ws_.log("spectrafx",spectraPreviewLastError_,true);
    } else {
        spectraPreviewVisualSeconds_+=dt;
        spectraPreviewLastError_.clear();
    }
    const auto checkpoint=spectraPreviewGpu_->checkpointStats();
    if(spectraGpuCaptureState_==SpectraGpuCaptureState::Queued) {
        if(!accepted) {
            spectraGpuCaptureState_=SpectraGpuCaptureState::Failed;
            spectraGpuCaptureNote_="Metal simulation rejected before checkpoint: "+
                                   spectraPreviewLastError_;
        } else if(checkpoint.exportReadyGroups>0) {
            spectraGpuCaptureState_=SpectraGpuCaptureState::Ready;
            spectraGpuCaptureNote_="Current GPU revision is ready for binary export";
        } else if(checkpoint.pendingGroups>0) {
            spectraGpuCaptureState_=SpectraGpuCaptureState::InFlight;
            spectraGpuCaptureNote_="Waiting for GPU checkpoint readback";
        } else if(spectraPreviewGpu_->requestGpuCheckpoint(0)) {
            spectraGpuCaptureState_=SpectraGpuCaptureState::InFlight;
            spectraGpuCaptureNote_="Checkpoint readback queued on the Metal graphics frame";
        } else {
            spectraGpuCaptureState_=SpectraGpuCaptureState::Failed;
            spectraGpuCaptureNote_="GPU checkpoint rejected: "+
                                   spectraPreviewGpu_->reason();
        }
    } else if(spectraGpuCaptureState_==SpectraGpuCaptureState::InFlight) {
        if(checkpoint.exportReadyGroups>0) {
            spectraGpuCaptureState_=SpectraGpuCaptureState::Ready;
            spectraGpuCaptureNote_="Fresh GPU checkpoint complete; save the binary diagnostic";
        } else if(checkpoint.pendingGroups==0) {
            spectraGpuCaptureState_=SpectraGpuCaptureState::Failed;
            spectraGpuCaptureNote_="GPU checkpoint completed without fresh data; retry while frozen";
        }
    } else if(spectraGpuCaptureState_==SpectraGpuCaptureState::Ready &&
              checkpoint.exportReadyGroups==0) {
        spectraGpuCaptureState_=SpectraGpuCaptureState::Idle;
        spectraGpuCaptureNote_="GPU particle revision advanced; previous checkpoint is stale";
    }
    SpectraGpuPreviewFrameRecord record{};
    record.deviceFrame=dev_?dev_->frameSerial():0;
    record.visualSeconds=spectraPreviewVisualSeconds_;
    record.estimatedParticles=spectraPreviewGpu_->stats().gpuParticlesEstimated;
    record.freshGroups=checkpoint.freshGroups;
    record.staleGroups=checkpoint.staleGroups;
    record.pendingGroups=checkpoint.pendingGroups;
    record.killedSlotsWaiting=checkpoint.killedSlotsAwaitingFence;
    record.stepAccepted=accepted;
    record.deviceHealthy=checkpoint.gpuDeviceHealthKnown &&
                         checkpoint.gpuDeviceHealthy;
    constexpr size_t kHistoryFrames=180;
    if(spectraPreviewHistory_.size()>=kHistoryFrames)
        spectraPreviewHistory_.pop_front();
    spectraPreviewHistory_.push_back(record);
}
void Editor::drawSpectraGpuPreview() {
    if(!spectraPreviewFrameOpen_ || !spectraPreviewGpu_)return;
    spectraPreviewGpu_->draw({r3d_.lighting.fogColor,r3d_.lighting.fogDensity});
}

void Editor::renderScene(Viewport& vp, const Camera& cam, bool editOverlays) {
    if (!vp.color.valid()) return;
    if (playing() && !ws_.project.actorSession()) { renderGame(vp, cam, 1ULL); return; }
    const bool traceSceneStages = std::getenv("AIVERSE_FRAME_TRACE") != nullptr;
    const auto stageStart = std::chrono::steady_clock::now();
    if (!ws_.map.data.worldSpace) {
        buildEditScene(scene_);
        if(!editOverlays)for(auto& object:scene_.objects)object.selected=object.hovered=false;
        scene_.gameplayOverlays=false;
        if (lazyEditorAssets_) {
            // Preserve generated preview types without loading the entire catalog.
            for (const auto& o : scene_.objects)
                if (!o.isProjectile && o.type < modelDemand_.size() &&
                    modelDemand_.lastUsed(o.type) != time_) retainEditorType(o.type);
        }
    }
    const auto afterBuild = std::chrono::steady_clock::now();
    if (!ws_.map.data.worldSpace) fieldPreview_.fill(ws_.project.rules, ws_.map, scene_);   // grid fields: the map's initial values
    const auto afterFields = std::chrono::steady_clock::now();
    const bool planets = !ws_.map.data.worldSpace && view_.planetMode();
    // A map undo may have removed the TerraWeave recipe since last frame.
    // Release stale preview buffers even when the Terrain panel is closed.
    if (terraWeaveForestPreview_.enabled() &&
        !doc().data.extra["terraweave"].isObject())
        setTerraWeaveForestPreviewEnabled(false);
    // planet maps: the orbit camera of the view (Scene window input drives it)
    const Camera& useCam = planets ? view_.updatePlanetCamera(0, float(ImGui::GetIO().DeltaTime), vp.h) : cam;
    refreshPrismaVolumes();
    prisma_.setViewId(1ULL);
    prisma_.applyCameraVolumes(prismaVolumes_, useCam.eye());
    // Editor Scene is a lower-priority preview than the live Game viewport.
    // The cap applies only to this view's two output HDR histories.
    prisma_.setViewQualityPolicy(1ULL,{64,128});
    const auto sceneAssets=view_.ownedPresentationGpuMemoryStats(*dev_);
    const auto sceneFxImages=fxTextures_.ownedGpuMemoryStats();
    const auto sceneFxBuffers=gameFx_.ownedGpuMemoryStats();
    prisma_.setExternalPresentationAssetsGpuMemory(
        render::GpuOwnedMemoryCounter::saturatingAdd(
            render::GpuOwnedMemoryCounter::saturatingAdd(
                sceneAssets.knownBytes(),sceneFxImages.knownBytes()),
            sceneFxBuffers.knownBytes()));
    const auto sceneRendererMemory=r3d_.residentMemoryStats();
    prisma_.setExternalRendererGpuMemory(
        sceneRendererMemory.liveTextureKnownBytes+
        sceneRendererMemory.liveBufferKnownBytes,
        sceneRendererMemory.pendingMotionKnownBytes);
    prisma_.setExternalGeometryPoolGpuMemory(
        render::geometryPoolStats(*dev_).allocatedBytes);
    const bool postEnabledForScene = !r3d_.studioDebugMode() && postFxInScene;
    // Only the renderer's camera receives jitter. UI, picking, overlays,
    // camera navigation and authored scene positions use the stable useCam.
    const auto [renderW,renderH] = postEnabledForScene
        ? prisma_.renderSizeForOutput(vp.w,vp.h)
        : std::pair<int,int>{vp.w,vp.h};
    if(cleanSceneCaptureRequested())for(auto& shot:shots_)if(shot.metadata.getBool("clean")) {
        shot.metadata.set("render_width",Json::number(int64_t(renderW)));
        shot.metadata.set("render_height",Json::number(int64_t(renderH)));
    }
    const Camera renderCam = postEnabledForScene
        ? prisma_.jitteredCamera(useCam,renderW,renderH) : useCam;
    // Existing map extra is the presentation manifest, not a new water solver.
    try {
        nereidPreview_.configure(authoredWaterDocument(doc()));
        nereidPreview_.prepare(*dev_,useCam.eye());
        nereidPreview_.updateUnderwater(prisma_,ImGui::GetIO().DeltaTime);
    } catch(const std::exception& e) {
        std::fprintf(stderr,"[nereid-preview] %s\n",e.what());
    }
    visualPickModelCount_=0;
    struct RestorePickTap {
        Renderer3D& renderer;Renderer3D::ModelPacketTap previous;
        ~RestorePickTap(){renderer.setModelPacketTap(std::move(previous));}
    } pickTap{r3d_,r3d_.exchangeModelPacketTap({})};
    r3d_.setModelPacketTap([this,&pickTap](const ModelDraw& draw){
        if(pickTap.previous)pickTap.previous(draw);
        if(!draw.model||(draw.persistentId>>56)!=0xED||visualPickModelCount_>=4096)return;
        const uint64_t encoded=draw.persistentId&0x00ffffffffffffffULL;
        if(encoded==0||encoded>doc().data.entities.size())return;
        if(visualPickModelCount_==visualPickModels_.size())visualPickModels_.emplace_back();
        auto& out=visualPickModels_[visualPickModelCount_++];
        out.model=draw.model;out.documentIndex=size_t(encoded-1);out.entityId=doc().data.entities[out.documentIndex].id;out.world=draw.world;
        if(draw.nodeWorld)out.nodes=*draw.nodeWorld;else out.nodes.clear();
        out.materials=draw.materials?*draw.materials:draw.model->materials;
        for(auto& material:out.materials)material.baseColor.w*=draw.tint.w;
        if(draw.nodeMaterial)out.nodeMaterials=*draw.nodeMaterial;else out.nodeMaterials.clear();
    });
    r3d_.beginScene(renderCam,renderW,renderH);
    // Scene selection outline is a depth-tested posteffect, not an
    // inverted-hull ModelDraw vertex extrusion. Never enable in Game.
    r3d_.setSceneSelectionOutline(editOverlays);
    if (!ws_.map.data.worldSpace) setOverlayContext(vp, useCam, false);
    // Scene-only silhouette outline through the existing ModelDraw outline path.
    // Keep the Game view unaffected by restoring the flag after draw.
    if (ws_.map.data.worldSpace) {
        enqueueFpsSceneGeometry(useCam,editOverlays);
    } else {
        view_.setSelectionOutline(editOverlays);
        view_.draw(r3d_, scene_, time_, nullptr, grid_);
        view_.setSelectionOutline(false);
    }
    nereidPreview_.draw(r3d_);
    visualPickCacheGeneration_=view_.modelResources().generation();
    const auto afterView = std::chrono::steady_clock::now();
    if (const auto& oc = view_.overlays().context(); editOverlays && !ws_.map.data.worldSpace && overlayPreviewType >= 0 && oc.placeType >= 0)   // overlay preview ghost
        view_.drawGhost(r3d_, uint16_t(oc.placeType), oc.placeAt, oc.placeOk, grid_, time_);
    if (editOverlays && !planets && !ws_.map.data.worldSpace) drawEditOverlays3D();
    // Editor-only TerraWeave forest overlay. Explicit opt-in from TerrainWindow:
    // shares the standard Renderer3D model/instancing path, no gameplay state.
    if (!planets && !ws_.map.data.worldSpace && terraWeaveForestPreview_.enabled()) {
        try {
            const auto cameraPosition = r3d_.cameraPos();
            const double xCm = double(cameraPosition.x) * 100.0;
            const double zCm = double(cameraPosition.z) * 100.0;
            if (!std::isfinite(xCm) || !std::isfinite(zCm) ||
                std::abs(xCm) > double(terraweave::kWorldLimitCm) ||
                std::abs(zCm) > double(terraweave::kWorldLimitCm))
                throw std::out_of_range("TerraWeave forest camera outside world extent");
            terraWeaveForestPreview_.setEngineDir(ws_.engineDir);
            terraWeaveForestPreview_.setProjectDir(ws_.project.dir);
            terraWeaveForestPreview_.draw(*dev_, r3d_, doc(),
                int64_t(std::llround(xCm)), int64_t(std::llround(zCm)),
                [this](int64_t x, int64_t z) -> int32_t {
                    // The display surface (including manual sculpt) takes priority.
                    const float height = view_.heightAt(grid_, float(x) * 0.01f, float(z) * 0.01f);
                    if (!std::isfinite(height) || std::abs(height) > 1000000.0f)
                        throw std::out_of_range("TerraWeave preview invalid map height");
                    return int32_t(std::llround(double(height) * 100.0));
                });
        } catch (const std::exception& e) {
            status = std::string("TerraWeave forest: ") + e.what();
            terraWeaveForestPreview_.setEnabled(false);
        }
    }
    // PHYSIS «Живая природа»: wildfire on TerraWeave grass, wind, smoke and
    // sound without a play-test (private preview World or read-only live sim).
    if (!planets && !ws_.map.data.worldSpace && physisNaturePreview_.settings.enabled) {
        try {
            physisNaturePreview_.setWater(authoredWater());
            physisNaturePreview_.draw(*dev_, r3d_, gameFx_, projectReader(ws_.project.dir), doc(),
                sim_ ? &sim_->world() : nullptr, useCam, time_,
                [this](float x, float z) { return surfaceHeightAt(x, z); },
                [this](std::unique_ptr<render::Model>& model) { retirePreviewModel(model); });
        } catch (const std::exception& e) {
            status = std::string("PHYSIS природа: ") + e.what();
            physisNaturePreview_.settings.enabled = false;
            physisNaturePreview_.release(*dev_, gameFx_);
        }
    } else if (physisNaturePreview_.hasPreviewWorld() || physisNaturePreview_.flames())
        physisNaturePreview_.release(*dev_, gameFx_);
    // Models with <model>.wind.json sway with the PHYSIS preview wind.
    r3d_.setSceneWind(physisNaturePreview_.sceneWind(time_));
    // Real editor sparse terrain. Camera-distance visual LOD is independent
    // from authoritative Earthworks/physics. Preview models are retired
    // through Editor's completed-GPU fence owner, never destroyed mid-frame.
    if(editOverlays && !planets) {
        try {
            const auto eye=r3d_.cameraPos();
            const double x=double(eye.x)*100.0,z=double(eye.z)*100.0;
            if(!std::isfinite(x)||!std::isfinite(z) ||
               std::abs(x)>=double(terraweave::kWorldLimitCm) ||
               std::abs(z)>=double(terraweave::kWorldLimitCm))
                throw std::out_of_range("TerraWeave sparse preview camera extent");
            terraWeaveSparsePreview_.draw(
                *dev_,r3d_,doc(),int64_t(std::llround(x)),
                int64_t(std::llround(z)),
                [this](std::unique_ptr<render::Model>& model){
                    retirePreviewModel(model);
                });
        }catch(const std::exception& e){
            status=std::string("TerraWeave sparse terrain: ")+e.what();
        }
    }
    if (!planets && !ws_.map.data.worldSpace) tickFx(cam,renderW,renderH,nullptr);   // agent previews (fx_preview) in the Scene view
    tickSpectraGpuPreview(useCam,renderW,renderH,false);
    gfx::PassDesc pass;
    pass.colorTarget = vp.color;
    pass.depthTarget = vp.depth;
    pass.clearColor[0] = 0.13f; pass.clearColor[1] = 0.15f; pass.clearColor[2] = 0.17f;
    if (planets) pass.clearColor[0] = pass.clearColor[1] = pass.clearColor[2] = 0.0f;
    // Clean captures (agent beauty shots) have no UI: show the horizon/fog
    // colour instead of the editor's neutral backdrop.
    else if (cleanSceneCaptureRequested()) {
        pass.clearColor[0] = r3d_.lighting.fogColor.x;
        pass.clearColor[1] = r3d_.lighting.fogColor.y;
        pass.clearColor[2] = r3d_.lighting.fogColor.z;
    }
    // Accurate diagnostic colours must not pass through post-processing.
    const auto beforeRender = std::chrono::steady_clock::now();
    const bool postActiveForScene = postEnabledForScene && prisma_.active();
    static const bool measurePrismaViews=[] {
        const char* value=std::getenv("AIVERSE_PRISMA_VIEWPORT_GPU_TIMING");
        return value && std::strcmp(value,"1")==0;
    }();
    graph_gpu_profile::Scope graphSceneScope(*dev_,graphGpuProfile_,"scene-viewport-render",1ULL,measurePrismaViews);
    const bool sceneGpuScope=!graphSceneScope.active() && measurePrismaViews && dev_->beginGpuScope(1ULL);
    r3d_.render(pass, postEnabledForScene ? &prisma_ : nullptr);
    if (postActiveForScene) prismaSceneRendered_ = true;
    const auto afterRender = std::chrono::steady_clock::now();
    r3d_.setSceneSelectionOutline(false);
    if (!ws_.map.data.worldSpace && !r3d_.studioDebugMode()) {
        gameFx_.render({r3d_.lighting.fogColor, r3d_.lighting.fogDensity});
        // Scene-view particle reactive coverage; per-scene callback is
        // consumed synchronously by resolvePrisma and never persists.
        if (gameFx_.hasReactiveGeometry())
            r3d_.addMotionReactiveDraw([this](gfx::Device& gpu,
                    gfx::TextureHandle sceneDepth, int width, int height) {
                gameFx_.renderReactive(gpu,sceneDepth,width,height);
            });
    }
    drawSpectraGpuPreview();
    r3d_.resolvePrisma();
    prisma_.clearCameraVolumes();
    dev_->endPass();
    if (sceneGpuScope) dev_->endGpuScope();
    graphSceneScope.finish();
    if(spectraPreviewFrameOpen_ && spectraPreviewGpu_) {
        spectraPreviewGpu_->endPresentationFrame();
        spectraPreviewFrameOpen_=false;
    }
    // Scene authoring has no gameplay health/build HUD.
    if (traceSceneStages) {
        static unsigned samples = 0;
        static double sums[6] = {};
        const auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
        sums[0] += ms(stageStart, afterBuild);
        sums[1] += ms(afterBuild, afterFields);
        sums[2] += ms(afterFields, afterView);
        sums[3] += ms(afterView, beforeRender);
        sums[4] += ms(beforeRender, afterRender);
        sums[5] += ms(afterRender, std::chrono::steady_clock::now());
        if (++samples == 120) {
            std::fprintf(stderr,
                "[scene-stages] avg/120 build=%.3f fields=%.3f view=%.3f overlays-fx=%.3f renderer=%.3f post-overlays=%.3f ms\n",
                sums[0]/samples, sums[1]/samples, sums[2]/samples,
                sums[3]/samples, sums[4]/samples, sums[5]/samples);
            samples = 0;
            for (double& v : sums) v = 0.0;
        }
    }
}

void Editor::renderGame(Viewport& vp, const Camera& cam, uint64_t prismaViewId) {
    if (!vp.color.valid() || !sim_) return;
    auto& gs = simModule()->state(*sim_);
    // fields / creep changing during play are read by the terrain shader: build once per play session
    if (!playTerrainBuilt_) {
        if (const Json* gm = simModule()->generatedMap()) {   // map "generate": the view shows what the sim was built from
            if ((*gm)["world"].isObject()) { view_.setWorld(*gm); worldHash_ = 0; }
            view_.setTerrainMaterial((*gm)["terrainMaterial"], gs.grid.w, gs.grid.h);
        }
        view_.buildTerrain(*dev_, gs.grid);
        playTerrainBuilt_ = true;
    }
    game::GameScene sc;
    game::sceneFromSim(*sim_, sc, float(sim_->alpha()), uint8_t(human_));
    sc.paintLayers = paintLayers;
    sc.entityFx = entityFx;
    const bool sceneView = &vp == sceneViewport;
    sc.gameplayOverlays=!sceneView;   // the Scene shows the editor's selection / hover of the world tree
    for (auto& o : sc.objects) {
        o.selected = std::find(gameSelection.begin(), gameSelection.end(), o.entity) != gameSelection.end();
        if (sceneView && o.entity.valid()) {
            Selection me{Selection::Live, -1, {}};
            me.ref = worldRef(o.entity);
            o.selected = std::find(selectionSet.begin(), selectionSet.end(), me) != selectionSet.end();
            o.hovered = hover == me;
        }
    }
    refreshPrismaVolumes();
    prisma_.setViewId(prismaViewId);
    prisma_.applyCameraVolumes(prismaVolumes_, cam.eye());
    prisma_.setViewQualityPolicy(prismaViewId,
        {uint8_t(prismaViewId==1ULL?64:224),
         uint32_t(prismaViewId==1ULL?128:256)});
    const auto gameAssets=view_.ownedPresentationGpuMemoryStats(*dev_);
    const auto gameFxImages=fxTextures_.ownedGpuMemoryStats();
    const auto gameFxBuffers=gameFx_.ownedGpuMemoryStats();
    prisma_.setExternalPresentationAssetsGpuMemory(
        render::GpuOwnedMemoryCounter::saturatingAdd(
            render::GpuOwnedMemoryCounter::saturatingAdd(
                gameAssets.knownBytes(),gameFxImages.knownBytes()),
            gameFxBuffers.knownBytes()));
    const auto gameRendererMemory=r3d_.residentMemoryStats();
    prisma_.setExternalRendererGpuMemory(
        gameRendererMemory.liveTextureKnownBytes+
        gameRendererMemory.liveBufferKnownBytes,
        gameRendererMemory.pendingMotionKnownBytes);
    prisma_.setExternalGeometryPoolGpuMemory(
        render::geometryPoolStats(*dev_).allocatedBytes);
    const auto [renderW,renderH]=prisma_.renderSizeForOutput(vp.w,vp.h);
    const Camera renderCam=prisma_.jitteredCamera(cam,renderW,renderH);
    try {
        nereidPreview_.configure(authoredWaterDocument(doc()));
        nereidPreview_.prepare(*dev_,cam.eye());
        nereidPreview_.updateUnderwater(prisma_,ImGui::GetIO().DeltaTime);
    } catch(const std::exception& e) {
        std::fprintf(stderr,"[nereid-game-preview] %s\n",e.what());
    }
    r3d_.beginScene(renderCam,renderW,renderH);
    r3d_.setSceneSelectionOutline(sceneView);
    setOverlayContext(vp, cam, true);
    view_.draw(r3d_, sc, time_, human_ ? &gs.vision[size_t(human_)] : nullptr, gs.grid);
    if (const auto& oc = view_.overlays().context(); overlayPreviewType >= 0 && oc.placeType >= 0)
        view_.drawGhost(r3d_, uint16_t(oc.placeType), oc.placeAt, oc.placeOk, gs.grid, time_);
    nereidPreview_.draw(r3d_);
    tickFx(cam,renderW,renderH,&sc.objects);
    tickSpectraGpuPreview(cam,renderW,renderH,true);
    gfx::PassDesc pass;
    pass.colorTarget = vp.color;
    pass.depthTarget = vp.depth;
    pass.clearColor[0] = 0.55f; pass.clearColor[1] = 0.66f; pass.clearColor[2] = 0.78f;
    const bool postActiveForGame = prisma_.active();
    static const bool measureGameGpu=[] {
        const char* value=std::getenv("AIVERSE_PRISMA_VIEWPORT_GPU_TIMING");
        return value && std::strcmp(value,"1")==0;
    }();
    graph_gpu_profile::Scope graphGameScope(*dev_,graphGpuProfile_,"play-viewport-render",prismaViewId,measureGameGpu);
    const bool gameGpuScope=!graphGameScope.active() && measureGameGpu && dev_->beginGpuScope(prismaViewId);
    r3d_.render(pass, &prisma_);
    if (postActiveForGame && prismaViewId == 1ULL) prismaSceneRendered_ = true;
    if (postActiveForGame && prismaViewId == 2ULL) prismaGameRendered_ = true;
    gameFx_.render({r3d_.lighting.fogColor, r3d_.lighting.fogDensity});
    if (gameFx_.hasReactiveGeometry())
        r3d_.addMotionReactiveDraw([this](gfx::Device& gpu,
                gfx::TextureHandle sceneDepth, int width, int height) {
            gameFx_.renderReactive(gpu,sceneDepth,width,height);
        });
    drawSpectraGpuPreview();
    r3d_.resolvePrisma();
    r3d_.setSceneSelectionOutline(false);
    prisma_.clearCameraVolumes();
    dev_->endPass();
    if (gameGpuScope) dev_->endGpuScope();
    graphGameScope.finish();
    if(spectraPreviewFrameOpen_ && spectraPreviewGpu_) {
        spectraPreviewGpu_->endPresentationFrame();
        spectraPreviewFrameOpen_=false;
    }
    if(!sceneView)view_.drawOverlays(cam, sc, vp.pointsW, vp.pointsH, vp.originX, vp.originY, ImGui::GetWindowDrawList());
}

// Overlays get what the player does in this view: cursor, held keys, the structure being placed
// (Scene: the Place tool; both: overlay_preview), pause / match over.
void Editor::setOverlayContext(const Viewport& vp, const Camera& cam, bool play) {
    game::OverlayContext oc;
    oc.sim = play ? sim_.get() : nullptr;
    oc.player = uint8_t(play ? std::max(0, human_) : 1);
    const ImVec2 mp = ImGui::GetIO().MousePos;
    float x = 0, z = 0;
    if (vp.hovered && groundAt(vp, cam, mp.x, mp.y, x, z)) { oc.hasCursor = true; oc.cursor = {x, 0, z}; }
    using platform::Key;
    for (int k = 0; k < 26; ++k) overlayKeys_[int(Key::A) + k] = ImGui::IsKeyDown(ImGuiKey(ImGuiKey_A + k));
    for (int k = 0; k < 10; ++k) overlayKeys_[int(Key::Num0) + k] = ImGui::IsKeyDown(ImGuiKey(ImGuiKey_0 + k));
    overlayKeys_[int(Key::LeftShift)] = ImGui::GetIO().KeyShift;
    overlayKeys_[int(Key::LeftCtrl)] = ImGui::GetIO().KeyCtrl;
    overlayKeys_[int(Key::LeftAlt)] = ImGui::GetIO().KeyAlt;
    overlayKeys_[int(Key::LeftSuper)] = ImGui::GetIO().KeySuper;
    overlayKeys_[int(Key::Space)] = ImGui::IsKeyDown(ImGuiKey_Space);
    oc.keys = overlayKeys_;
    oc.keyCount = int(Key::Count);
    int place = overlayPreviewType >= 0 ? overlayPreviewType : (!play && tool == Tool::Place ? placeType : -1);
    if (place >= 0 && size_t(place) < rules().types.size() && rules().types[size_t(place)].structure) {
        const auto& t = rules().types[size_t(place)];
        const float px = oc.hasCursor ? x : cam.target.x, pz = oc.hasCursor ? z : cam.target.z;
        oc.placeType = place;
        oc.placeAt = {(t.footprintW % 2) ? std::floor(px) + 0.5f : std::round(px), 0, (t.footprintH % 2) ? std::floor(pz) + 0.5f : std::round(pz)};
        const game::MapGrid& g = play && sim_ ? simModule()->state(*sim_).grid : grid_;
        oc.placeOk = g.areaBuildable(int(std::floor(oc.placeAt.x - t.footprintW * 0.5f + 0.01f)), int(std::floor(oc.placeAt.z - t.footprintH * 0.5f + 0.01f)),
                                     t.footprintW, t.footprintH);
    }
    oc.paused = play && playPaused;
    oc.over = play && sim_ && sim_->result() != 0;
    oc.areas = &doc().data.extra["areas"];
    oc.preview = overlayPreview.empty() ? nullptr : &overlayPreview;
    view_.setOverlayContext(oc);
}

// Agent edits: fly the scene camera there and flash the area.
void Editor::applyChanges() {
    if (ws_.changes.empty()) return;
    bool rulesChanged = false, projectChanged = false, fxChanged = false;
    for (auto& c : ws_.changes) {
        if (c.kind == Change::Rules) rulesChanged = true;
        if (c.kind == Change::Project) projectChanged = true;
        if (c.kind == Change::Fx) fxChanged = true;
        if (c.x1 >= c.x0 && c.y1 >= c.y0) {
            flash(float(c.x0), float(c.y0), float(c.x1 + 1), float(c.y1 + 1));
            if (followAgent && c.kind != Change::Map) focusScene((c.x0 + c.x1 + 1) * 0.5f, (c.y0 + c.y1 + 1) * 0.5f);
        }
        if (c.entity >= 0 && c.kind == Change::Entity) selectEntity(c.entity);
    }
    if (projectChanged || rulesChanged) reloadRules();
    else if (fxChanged) reloadFx();
    if (fxChanged || projectChanged) ++fxRevision;
    if (projectChanged) { fitScene(); selection = {}; }
    ws_.changes.clear();
    if (selectedEntity() >= int(doc().data.entities.size())) selection = {};
}

// ============================ play-test ============================

void Editor::startPlay(int humanSlot) {
    if(ws_.project.actorSession()){startFpsPlay();return;}
    if (checkNativeModules(true)) return;   // the banner offers to build an editor with them
    stopClientPlay();
    auto issues = doc().validate(rules(), ws_.project.rulesJson);
    // every problem goes to the console / editor.log (the status bar shows only the first error)
    std::string firstError;
    for (auto& i : issues) {
        ws_.log("play", (i.error ? "Нельзя запустить: " : "Предупреждение: ") + i.text, i.error);
        if (i.error && firstError.empty()) firstError = i.text;
    }
    if (!firstError.empty()) { status = "Нельзя запустить: " + firstError; return; }
    try {
        SimConfig cfg;
        cfg.seed = uint64_t(std::time(nullptr));
        sim_ = std::make_unique<Simulation>(cfg, std::make_unique<game::GameCore>());
        Json mapJson = doc().toJson();
        std::string terr, dir = ws_.project.dir;
        auto read = [&dir](const std::string& p, std::string& out) {
            std::ifstream f(dir + "/" + p, std::ios::binary);
            if (!f) return false;
            out.assign(std::istreambuf_iterator<char>(f), {});
            return true;
        };
        if (!game::applyTilesToMap(mapJson, read, &terr)) throw std::runtime_error(terr);
        sim_->setup(ws_.project.simRules(mapJson), mapJson);
        playIds_ = mapEntityIds(*sim_, *simModule(), mapJson);
        playDocLinks_ = linkMapEntities(*sim_, *simModule(), doc().data);   // world tree: map entity -> live object
        view_.lights().configure(ws_.project.manifest["lighting"], mapJson["lighting"], mapJson["lights"]); // day/night, map lights
        if (auto pe = aiverse::game::configurePlayerColors(ws_.project.manifest["playerColors"]); !pe.empty()) { status = pe; ws_.log("play", pe, true); }
    } catch (const std::exception& e) {
        status = std::string("Ошибка запуска: ") + e.what();
        ws_.log("play", status, true);
        sim_.reset();
        return;
    }
    if(graphPlayEpoch_!=~uint64_t{0})++graphPlayEpoch_; // saturated epoch disables further telemetry
    graphPlayHistory_.clear();
    graphWatch_.clear();
    view_.clearSharedAnimationProfile();
    human_ = humanSlot;
    gameFx_.clear();
    fxPreview_ = {};
    fxFrozen_ = false;
    gameFxBridge_.setHeightFn([this](float x, float z) { return sim_ ? view_.heightAt(simModule()->state(*sim_).grid, x, z) : 0.0f; });
    gameFxBridge_.setPlayerColorFn([](int p) { return game::GameView::playerColor(p); });
    gameFxBridge_.setAttackOriginFn([this](Entity e, fx::float3& pos, fx::float3& dir) { return view_.muzzleOf(e, pos, dir); });   // shots leave the model's muzzle
    gameFxBridge_.attach(*sim_, uint8_t(human_));
    fxTime_ = -1;
    bots_.clear();
    for (auto& p : sim_->players()) if (p.slot != human_) bots_.emplace_back(p.slot, sim_->config().seed);
    playPaused = false;
    playTerrainBuilt_ = false;
    playHistory.clear();
    gameSelection.clear();
    gameCam = sceneCam;
    gameCam.pitch = 0.95f;
    if (human_ > 0) {
        const auto& d = doc().data;
        for (auto& e : d.entities) if (e.owner == human_) { gameCam.target = {e.x, 0, e.z}; break; }
        if (size_t(human_ - 1) < d.players.size() && d.players[size_t(human_ - 1)].hasStart) gameCam.target = {d.players[size_t(human_ - 1)].startX, 0, d.players[size_t(human_ - 1)].startZ};
        gameCam.distance = 24;
    }
    render::applyOverheadCamera(render::overheadCameraFrom(ws_.project.manifest), gameCam, 720.0f);
    focusWindow("game");
    status = human_ ? "Тест: вы играете за игрока " + std::to_string(human_) : "Тест: боты играют на карте";
}

void Editor::stopPlay() {
    graphPlayHistory_.clear();
    graphWatch_.clear();
    view_.clearSharedAnimationProfile();
    gameFxBridge_.detach();
    gameFx_.clear();
    fxPreview_ = {};
    fxFrozen_ = false;
    sim_.reset();
    bots_.clear();
    gameSelection.clear();
    gridRevision_ = ~0ull; // the sim's grid was shown; rebuild the editor's
    status = "Редактирование";
}

void Editor::issueCommand(Command c) {
    if (!sim_ || human_ <= 0) return;
    c.player = uint8_t(human_);
    sim_->submit(c);
}

void Editor::samplePlayStats() {
    if (!sim_) return;
    float t = float(sim_->tick()) / float(sim_->tickRate());
    if (!playHistory.empty() && t - playHistory.back().t < 1.0f) return;
    auto* m = simModule();
    auto& gs = m->state(*sim_);
    PlaySample s;
    s.t = t;
    for (auto& p : sim_->players()) {
        int units = 0, structs = 0;
        sim_->world().each<game::Unit, Owner>([&](aiverse::Entity, game::Unit& u, Owner& o) { if (o.player == p.slot) (rules().types[u.type].structure ? structs : units)++; });
        s.units.push_back(units);
        s.structures.push_back(structs);
        std::vector<float> res;
        for (size_t r = 0; r < rules().resources.size(); ++r) res.push_back(float(gs.players[p.slot].stock[r].toDouble()));
        s.resources.push_back(res);
    }
    playHistory.push_back(std::move(s));
    if (playHistory.size() > 3600) playHistory.erase(playHistory.begin());
}

// ============================ frame ============================

void Editor::event(const platform::Event& e) {
    ui_.handleEvent(e);
    if(clientHost_.active()){
        // The selected dock tab may change without GameWindow::drawClient running.
        // Never forward events into a hidden hosted game (especially text/rebind keys).
        EditorWindow* game=window("game");
        const bool eligible=game && game->open && isWindowInFront(game);
        if(!eligible)clientHost_.suspendInput();
        if(eligible || e.type==platform::EventType::FocusLost)clientHost_.event(e);
    }
    if (e.type == platform::EventType::QuitRequested) quit_ = true;
    if (e.type == platform::EventType::OpenFile) requestOpenProject(e.path);   // double-clicked .aiverse (macOS)
}

void Editor::handleShortcuts() {
    ImGuiIO& io = ImGui::GetIO();
    const bool mod = io.KeyCtrl || io.KeySuper;
    if (showPicker_ || io.WantTextInput) return;
    for (const auto& window : windows_)
        if (window->open && window->captureGlobalShortcuts()) return;
    if (mod && ImGui::IsKeyPressed(ImGuiKey_S, false) && !doc().path.empty()) saveMap(doc().path);
    if (mod && ImGui::IsKeyPressed(ImGuiKey_Z, false) && !playing()) { if (io.KeyShift) doc().redo(); else doc().undo(); selection = {}; }
    if (mod && ImGui::IsKeyPressed(ImGuiKey_Y, false) && !playing()) { doc().redo(); selection = {}; }
    if (ImGui::IsKeyPressed(ImGuiKey_F5, false)) {
        if (clientPlaying()) stopClientPlay();
        else if (playing()) stopPlay();
        else if (prefersClientPlay()) startClientPlay();
        else startPlay(lastPlaySlot_);
    }
    if (mod && ImGui::IsKeyPressed(ImGuiKey_B, false)) focusWindow("build");
}

void Editor::drawDockspace() {
    ImGuiViewport* mv = ImGui::GetMainViewport();
    const float statusH = statusBarHeight();
    ImVec2 pos(mv->WorkPos.x, mv->WorkPos.y), size(mv->WorkSize.x, mv->WorkSize.y - statusH);
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGui::SetNextWindowViewport(mv->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##dockhost", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                           ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBackground);
    ImGui::PopStyleVar(3);
    ImGuiID dock = ImGui::GetID("MainDock");
    // layouts saved before window names became "Title###id" dock nothing: rebuild them once
    if (!layoutChecked_) { layoutChecked_ = true; if (theme_.loadedLayoutVersion() < EditorTheme::kLayoutVersion) resetLayout_ = true; }
    if (resetLayout_ || !ImGui::DockBuilderGetNode(dock)) {
        resetLayout_ = false;
        ImGui::DockBuilderRemoveNode(dock);
        ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dock, size);
        ImGuiID center = dock;
        // AIVERSE Studio: wide scene, slim hierarchy, compact asset shelf and dominant inspector.
        // Existing dockable window IDs and persistence remain intact.
        ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.16f, nullptr, &center);
        ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.24f, nullptr, &center);
        ImGuiID rightBottom = ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.16f, nullptr, &right);
        ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.20f, nullptr, &center);
        ImGuiID leftBottom = ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.40f, nullptr, &left);
        for (auto& w : windows_) {
            std::string a = w->dockArea();
            ImGuiID node = a == "left" ? left : a == "left-bottom" ? leftBottom : a == "right-bottom" ? rightBottom : a == "bottom" ? bottom : a == "center" ? center : right;
            ImGui::DockBuilderDockWindow(w->imguiName(), node);
        }
        ImGui::DockBuilderFinish(dock);
        focusFirst_ = 2; // select the first tab of every area once the windows exist
    }
    ImGui::PushFont(theme_.headingFont(), EditorTheme::kUiSize); // tab titles: semibold
    ImGui::DockSpace(dock, ImVec2(0, 0), ImGuiDockNodeFlags_None);
    ImGui::PopFont();
    ImGui::End();
}

void Editor::frame(gfx::Device& dev, double dt) {
    // PRE-RENDER GPU-retirement maintenance. Only here may the editor
    // explicitly synchronize previously SUBMITTED work on memory pressure:
    // the current frame has not encoded its viewport/ImGui render yet.
    // Do not do an idle wait after scene/ImGui commands are recorded.
    static const uint64_t retirementBudgetBytes = []() -> uint64_t {
        uint64_t mb = 128;
        if (const char* value = std::getenv("AIVERSE_PRISMA_RETIRE_BUDGET_MB")) {
            char* end = nullptr;
            const unsigned long long n = std::strtoull(value,&end,10);
            if (end != value && end && *end == '\0')
                mb = std::clamp<uint64_t>(n,32,4096);
        }
        return mb * 1024 * 1024;
    }();
    viewportRetirement_.setBudgetBytes(retirementBudgetBytes);
    // Only completed scoped GPU queries are returned. This is a distinct
    // queue from whole-frame timing/benchmark APIs; never consume those.
    graphGpuProfile_.checkProject(ws_.project.dir);
    for (const auto& timing : dev.takeGpuScopeTimings()) {
        if (timing.valid && std::isfinite(timing.milliseconds) &&
            timing.milliseconds>0 && (timing.tag==1ULL || timing.tag==2ULL))
            prisma_.acceptViewGpuTiming(timing.tag,timing.milliseconds);
        else if(const auto view=graphGpuProfile_.complete(timing,dev.frameSerial()))
            prisma_.acceptViewGpuTiming(view,timing.milliseconds);
    }
    graphGpuProfile_.expire(dev.frameSerial());
    viewportRetirement_.collect(dev);
    gpuResourceRetirement_.collect(dev);
    clientHost_.collectGpuRetirements();
    prisma_.collectGpuRetirements();
    r3d_.collectCompletedObjectMotion(dev.gpuCompletedFence());
    const auto vpStats = viewportRetirement_.stats(dev);
    const auto postStats = prisma_.retirementStats();
    const auto motionStats = r3d_.motionRetirementStats();
    const auto genericStats = gpuResourceRetirement_.stats(dev);
    const uint64_t pendingKnownBytes = vpStats.knownPendingBytes +
        postStats.knownPendingBytes + motionStats.knownPendingBytes +
        genericStats.knownPendingBytes;
    const bool flushRequested = gpuRetirementFlushRequested_;
    gpuRetirementFlushRequested_ = false; // one attempt, no repeated UI stalls
    if (flushRequested ||
        (retirementBudgetBytes && pendingKnownBytes > retirementBudgetBytes) ||
        clientHost_.pendingRetiredClients() > 2 ||
        genericStats.bundles > 8) {
        // Old ShaderGraph preview bundles include unknown portrait/FX GPU
        // allocations. Bundle count is pressure only, never GPU completion.
        // A retired hosted GameClient has deep renderer/textures whose
        // GPU byte count is not cheaply enumerable. Bound the number of
        // archived clients as an additional pressure signal (never as a
        // GPU-completion signal). Only a real completed idle may drain them.
        // This can block, but only at the safe PRE-RENDER boundary. If the
        // backend can't guarantee submitted-work idle (e.g. WebGL), keep
        // every resource quarantined rather than freeing in-flight textures.
        gpuRetirementLastFlushSucceeded_ = dev.waitForSubmittedGpuWork();
        if (gpuRetirementLastFlushSucceeded_) {
            viewportRetirement_.drainAfterGpuIdle(dev);
            gpuResourceRetirement_.drainAfterGpuIdle(dev);
            clientHost_.drainAfterGpuIdle();
            prisma_.releaseRetiredSizesAfterGpuIdle();
            r3d_.drainObjectMotionAfterGpuIdle();
        }
    }
    // Window tabs can close or become dock-hidden without a destructor.
    // Only a viewport that actually calls Prisma this frame stays live.
    prismaSceneRendered_ = prismaGameRendered_ = false;
    auto t0 = std::chrono::steady_clock::now();
    const bool traceEditor = std::getenv("AIVERSE_FRAME_TRACE") != nullptr;
    time_ += dt;
    // The previous frame's model buffers may still be read by Metal/Vulkan.
    // Retire on the owner thread before recording this frame's view passes.
    // An unavailable/failed fence retains retired resources for a retry.
    // A false result is expected while an asynchronous retirement token
    // is still in flight, not necessarily a GPU error. Poll silently.
    if(view_.pendingModelRetirements()!=0 ||
       view_.pendingRolledBackImages()!=0)
        view_.collectRetiredModels();
    if(view_.pendingPaintGpuRetirements()!=0)
        view_.collectRetiredPaintGpuResources();
    if(previewModelRetirement_.pending()!=0)
        (void)previewModelRetirement_.collect(dev);
    if(r3d_.pendingProgramRetirements()!=0)
        r3d_.collectRetiredPrograms();
    if(r3d_.pendingTargetRetirements()!=0)
        r3d_.collectRetiredTargets();
    // Poll previously submitted native GPU compiler jobs once per owner
    // frame. Scratch authoring resumes only when they have completed.
    if(r3d_.hasPendingModelPipelineJobs())
        r3d_.pollPendingModelPipelineJobs();
    pollAgentLink();
    applyChanges();
    graphGpuProfile_.checkProject(ws_.project.dir);
    // Poll changed presentation assets before any new frame submissions.
    // This runs even when every editor property/model window is closed.
    pollVisualAssets(dt);
    // Background asset residency is paced on the editor thread. A single
    // slow upload can overrun the advisory time budget, so also cap count.
    // Interactive Inspector and viewport requests still load immediately.
    if(lazyEditorAssets_ && !playing() && !clientPlaying() &&
       !ws_.project.actorSession()) {
        modelDemand_.drainBudget(1.0,2,[&](size_t type){
            if(type<rules().types.size() && !view_.typeLoaded(uint16_t(type)))
                view_.loadType(uint16_t(type));
        });
    }
    // Sweep only after a substantial no-use interval, before new viewport
    // submission. Never unload while the editor's play simulation is active.
    reclaimEditorAssets();
    if (!playing() && gridRevision_ != doc().revision && time_ >= gridRebuildAt_) { syncGrid(); gridRebuildAt_ = time_ + 0.05; }
    if (sceneFlying_) {
        float k = float(std::min(1.0, dt * 4.0));
        sceneCam.target += (sceneGoal_ - sceneCam.target) * k;
        if (length(sceneGoal_ - sceneCam.target) < 0.05f) sceneFlying_ = false;
    }
    if (sim_ && !playPaused && sim_->result() == 0) {
        auto s0 = std::chrono::steady_clock::now();
        for (auto& b : bots_) b.update(*sim_);
        sim_->advance(dt * double(playSpeed), 200);
        lastSimMs_ = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - s0).count();
        samplePlayStats();
    }
    flashes_.erase(std::remove_if(flashes_.begin(), flashes_.end(), [&](const Flash& f) { return f.until < time_; }), flashes_.end());
    hover = hoverNext_;   // set by the Scene / Hierarchy while drawing the previous frame
    hoverNext_ = {};
    syncSelection();
    updateWorld();
    for (auto& w : windows_) w->update(*this, dt);
    const auto afterUpdate = std::chrono::steady_clock::now();

    theme_.applyPending();
    ui_.newFrame(dt);
    handleShortcuts();
    pollGameWizard();   // the game wizard's agent: progress into the activity feed / status bar
    const auto afterUiSetup = std::chrono::steady_clock::now();
    if (showPicker_) {
        drawGamePicker();
    } else {
        drawMenuBar();
        drawDockspace();
        // Per-window timings identify the expensive viewport or panel without
        // changing frame scheduling. Keep only the top offender per frame.
        double slowestWindowMs = 0.0;
        const char* slowestWindow = nullptr;
        for (auto& w : windows_) {
            if (!w->open) continue;
            const auto windowStart = std::chrono::steady_clock::now();
            if (w->zeroPadding()) ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            if (focusFirst_ == 1 && w->order() == 0) ImGui::SetNextWindowFocus();
            // a window opened for the first time (not in the saved layout) joins the tabs of its dock area
            // instead of floating as a tiny box
            if (!ImGui::FindWindowByName(w->imguiName()))
                for (auto& o : windows_)
                    if (o.get() != w.get() && o->open && std::string(o->dockArea()) == w->dockArea())
                        if (ImGuiWindow* ow = ImGui::FindWindowByName(o->imguiName()); ow && ow->DockId) { ImGui::SetNextWindowDockID(ow->DockId, ImGuiCond_FirstUseEver); ImGui::SetNextWindowFocus(); break; }
            bool visible = ImGui::Begin(w->imguiName(), &w->open, w->flags());
            if (w->zeroPadding()) ImGui::PopStyleVar();
            if (visible) {
                w->draw(*this);
                StudioContextMenus::get().drawWindowFallback(*this, w->id());
            }
            ImGui::End();
            if (traceEditor) {
                const double elapsed = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - windowStart).count();
                if (elapsed > slowestWindowMs) {
                    slowestWindowMs = elapsed;
                    slowestWindow = w->id();
                }
            }
        }
        if (traceEditor) {
            static unsigned windowSamples = 0;
            static double slowestSum = 0.0;
            static std::string slowestId;
            slowestSum += slowestWindowMs;
            if (slowestWindow && slowestWindowMs > 0.0) slowestId = slowestWindow;
            if (++windowSamples == 120) {
                std::fprintf(stderr, "[editor-window] avg-top=%.3f ms last-top=%s\n",
                             slowestSum / windowSamples, slowestId.c_str());
                windowSamples = 0;
                slowestSum = 0.0;
            }
        }
        if (pendingFocusFrames_ > 0 && --pendingFocusFrames_ == 0)
            for (auto& id : pendingFocus_) if (EditorWindow* w = window(id)) ImGui::SetWindowFocus(w->imguiName());
        drawStatusBar();
        drawActivityToasts();
        if (focusFirst_ > 0) --focusFirst_;
    }
    // Published viewport owners have completed CPU preparation and worker joins.
    captureGraphWatch();
    const auto afterWindows = std::chrono::steady_clock::now();
    // At a hidden/closed tab transition, drop only camera-private histories.
    // Shared size-keyed scene render targets remain usable by other tabs.
    // The current GPU submission token fences the previous view's textures.
    auto retireClosedView = [&](uint64_t id, bool rendered, bool& wasLive) {
        if (wasLive && !rendered) {
            prisma_.retireView(id);
            r3d_.retireObjectMotionView(id, dev.gpuSubmissionFence());
        }
        wasLive = rendered;
    };
    retireClosedView(1ULL, prismaSceneRendered_, prismaScenePreviouslyLive_);
    retireClosedView(2ULL, prismaGameRendered_, prismaGamePreviouslyLive_);
    // Also discard stale per-view Prisma quality/temporal state from any
    // future custom viewport ID, not just hard-coded Scene/Game. The same
    // GPU-fenced retireView() owns every private texture; no forced GPU idle.
    std::vector<uint64_t> activePrismaViews;
    if (prismaSceneRendered_) activePrismaViews.push_back(1ULL);
    if (prismaGameRendered_) activePrismaViews.push_back(2ULL);
    prisma_.retireUnregisteredViews(activePrismaViews);
    // Renderer3D keeps a separate per-camera object/deformation velocity
    // history, independent of PRISMA's own temporal/exposure resources.
    // Retire orphaned custom-camera IDs as well as the two built-in views,
    // behind the graphics backend submission fence, never a CPU frame age.
    r3d_.retireUnregisteredObjectMotionViews(activePrismaViews);
    // A cached size can remain after a tab closes or is resized. Retire it
    // only when NO live camera references the same size. This moves its
    // targets and graph pool behind the current GPU submission fence, never
    // destroys resources while the frame is still recording.
    prisma_.retireUnreferencedSizeCaches();
    // Polling also runs when no Scene/Game render pass is active.
    prisma_.collectGpuRetirements();
    r3d_.collectCompletedObjectMotion(dev.gpuCompletedFence());
    drawOpenRequest();
    drawNativeBuild();
    // the screen: only the UI (3D views were rendered offscreen by their windows)
    gfx::PassDesc pass;
    pass.clearColor[0] = 0.10f; pass.clearColor[1] = 0.11f; pass.clearColor[2] = 0.13f;
    dev.beginPass(pass);
    dev.setViewport({0, 0, dev.framebufferWidth(), dev.framebufferHeight()});
    std::string title = "AIVERSE Editor — " + ws_.project.manifest.getStr("name", "без игры") + " — " +
                        (doc().path.empty() ? std::string("без имени") : fs::path(doc().path).filename().string()) + (doc().dirty ? " *" : "");
    static std::string lastTitle;
    if (title != lastTitle) { platform::setTitle(title); lastTitle = title; }
    ui_.render();
    dev.endPass();
    deliverShots(dev);
    const auto afterUiRender = std::chrono::steady_clock::now();
    if (traceEditor) {
        static unsigned samples = 0;
        static double totals[4] = {};
        const auto ms = [](auto begin, auto end) { return std::chrono::duration<double, std::milli>(end - begin).count(); };
        totals[0] += ms(t0, afterUpdate);
        totals[1] += ms(afterUpdate, afterUiSetup);
        totals[2] += ms(afterUiSetup, afterWindows);
        totals[3] += ms(afterWindows, afterUiRender);
        if (++samples == 120) {
            std::fprintf(stderr, "[editor-frame] avg/120 update=%.3f ms ui-setup=%.3f ms windows=%.3f ms ui-render=%.3f ms\n",
                         totals[0] / samples, totals[1] / samples, totals[2] / samples, totals[3] / samples);
            samples = 0;
            for (double& value : totals) value = 0.0;
        }
    }
    // Stats tab reports geometry for the same latest viewport as its draw calls.
    // Do not sum render passes: the model profile already isolates one camera
    // colour pass (no shadow, reflection or SSR replay double counting).
    const auto& sceneProfile = r3d_.profile();
    const uint64_t cameraTriangles = sceneProfile.cameraModelTriangles +
        sceneProfile.cameraMeshTriangles + sceneProfile.cameraOverlayTriangles;
    FrameStats fs_{float(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count()),
                   lastSimMs_, r3d_.drawCalls(), cameraTriangles};
    frameHistory.push_back(fs_);
    prof.frame(fs_.frameMs, time_);
    if (frameHistory.size() > 240) frameHistory.pop_front();
    if (quit_) platform::requestQuit();
}

} // namespace aiverse::editor
