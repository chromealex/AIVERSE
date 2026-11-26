#include "StudioHierarchyGroups.h"
#include "StudioOverlayPlan.h"
#include "engine/game/Tiles.h"
#include "StudioPlacementGhost.h"
#include "apps/editor/core/LightBakeOps.h"
#include "apps/editor/core/GeometryOps.h"
#include "StudioDocumentIds.h"
#include "engine/view/game/GameLights.h"
#include "StudioFpsLightingDocument.h"
#include "apps/editor/core/LegacyCameraBookmarks.h"
#include "StudioNetworkPlay.h"
#include "StudioGameWizard.h"
#include "apps/editor/core/PortOps.h"
#include "apps/editor/core/WizardOps.h"
#include "engine/game/Script.h"
#include "apps/editor/core/StudioPlayTools.h"
#include "apps/studio-native/StudioHost.h"
#include "StudioLightBakeFiles.h"
#include "apps/editor/core/AnimationPreviewSession.h"
#include "apps/studio-native/StudioSpectraPackageImport.h"
#include "apps/studio-native/StudioWorldProcessesGizmos.h"
#include "apps/studio-native/StudioWorldProcessesEditing.h"
#include "apps/studio-native/StudioHumanLookDocument.h"
#include "apps/studio-native/StudioGameInput.h"
#include "apps/studio-native/StudioBuildingGeneration.h"
#include "apps/studio-native/StudioBuildingPanelPreview.h"
#include "apps/studio-native/StudioTerrainGeometry.h"
#include "apps/editor/core/BrandingOps.h"
#include "apps/editor/core/GraphAssetTransaction.h"
#include <chrono>
#include "apps/editor/core/BuildTool.h"
#include "apps/editor/core/Projects.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <regex>
#include <set>

#include "apps/studio-native/StudioStbCodecs.h"

namespace aiverse::studio {
namespace fs = std::filesystem;

namespace {
void appendBytes(void* context, void* data, int size) {
    auto* out = static_cast<std::string*>(context);
    out->append(static_cast<const char*>(data), size_t(size));
}
// Box-filtered downscale to fit maxSide (never upscales). RGBA8.
std::vector<uint8_t> fitImage(const std::vector<uint8_t>& rgba, int w, int h, int maxSide, int& ow, int& oh) {
    const double k = std::min(1.0, double(maxSide) / double(std::max(w, h)));
    ow = std::max(1, int(w * k)); oh = std::max(1, int(h * k));
    if (ow == w && oh == h) return rgba;
    std::vector<uint8_t> out(size_t(ow) * size_t(oh) * 4);
    for (int y = 0; y < oh; ++y) {
        const int y0 = int(int64_t(y) * h / oh), y1 = std::max(y0 + 1, int(int64_t(y + 1) * h / oh));
        for (int x = 0; x < ow; ++x) {
            const int x0 = int(int64_t(x) * w / ow), x1 = std::max(x0 + 1, int(int64_t(x + 1) * w / ow));
            uint32_t sum[4] = {0, 0, 0, 0}, n = 0;
            for (int sy = y0; sy < y1; ++sy) for (int sx = x0; sx < x1; ++sx, ++n) {
                const uint8_t* p = &rgba[(size_t(sy) * size_t(w) + size_t(sx)) * 4];
                for (int c = 0; c < 4; ++c) sum[c] += p[c];
            }
            uint8_t* q = &out[(size_t(y) * size_t(ow) + size_t(x)) * 4];
            for (int c = 0; c < 4; ++c) q[c] = uint8_t(sum[c] / n);
        }
    }
    return out;
}
std::string encodePng(const std::vector<uint8_t>& rgba, int w, int h) {
    std::string out;
    stbi_write_png_to_func(appendBytes, &out, w, h, 4, rgba.data(), w * 4);
    return out;
}
std::string encodeJpeg(const std::vector<uint8_t>& rgba, int w, int h, int quality) {
    std::vector<uint8_t> rgb(size_t(w) * size_t(h) * 3);
    for (size_t i = 0, n = size_t(w) * size_t(h); i < n; ++i) { rgb[i*3] = rgba[i*4]; rgb[i*3+1] = rgba[i*4+1]; rgb[i*3+2] = rgba[i*4+2]; }
    std::string out;
    stbi_write_jpg_to_func(appendBytes, &out, w, h, 3, rgb.data(), quality);
    return out;
}
std::string lowerExt(const fs::path& p) {
    std::string ext = p.extension().string();
    for (auto& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
    return ext;
}
bool finite(std::initializer_list<double> values) {
    for (double v : values) if (!std::isfinite(v)) return false;
    return true;
}
const std::set<std::string>& documentChangingMethods() {
    static const std::set<std::string> methods = {
        "studio.openMap", "studio.newMap", "studio.entityCreate", "studio.entityDelete", "studio.entityDuplicate",
        "studio.mapPropertiesSet", "studio.mapPlayer", "studio.mapArea", "studio.mapRegionName", "studio.setProperty", "studio.save", "studio.undo", "studio.redo", "studio.nereidFlowSet",
        "studio.planetForestPlant", "studio.planetForestMove", "studio.planetForestSet", "studio.renderLightingSet", "studio.lightingSet", "studio.lightAdd", "studio.lightUpdate", "studio.lightDelete",
        "studio.materialPatch", "studio.materialCreate", "studio.buildingPublishMaterial", "studio.setProperties", "studio.componentAdd",
        "studio.componentRemove", "studio.entityMove", "studio.entitiesDelete", "studio.entitiesDuplicate",
        "studio.assetPlace", "studio.worldProcessAreaSet", "studio.worldProcessAreaBatchMove", "studio.worldProcessAreaBatchResize", "studio.entityFlags", "studio.rulesSet", "studio.rulesDocumentSave", "studio.entitiesAlign", "studio.historyJump",
        "studio.projectHistoryStep", "studio.assetMetaPatch", "studio.assetHistoryStep", "studio.assetTransferCommit", "studio.assetDelete", "studio.assetFolderCreate", "studio.assetTrashApply", "studio.assetHistoryResetApply", "studio.agentHistoryStep", "studio.agentUndoAll", "studio.agentBatchApply"};
    return methods;
}
} // namespace

StudioHost::StudioHost(StudioHostPlatform& platform, const std::string& engineRoot, bool htmlComposite)
    : platform_(platform), engineRoot_(engineRoot), htmlComposite_(htmlComposite) {
    // The project opens asynchronously after the UI loads (splash and progress).
    session_ = std::make_unique<StudioSession>(engineRoot, std::string());
    session_->workspace().registerOp({"animation_editor","Open an authoring model's clips or embedded graph in HTML Studio.",
        Json::parse(R"({"type":"object","properties":{"name":{"type":"string","default":"robot"},"tab":{"enum":["clips","graph"],"type":"string"}}})"),
        [this](const Json& params){
            const auto tab=params.getStr("tab","clips");
            if(tab!="clips"&&tab!="graph")throw std::runtime_error("Choose clips or graph");
            Json args=Json::object();args.set("name",Json::string(params.getStr("name","robot")));
            auto state=session_->workspace().call("modeling_get",args);
            if(state.has("error"))return state;
            args.set("project",Json::string(session_->workspace().project.dir));
            args.set("tab",Json::string(tab));
            sendEventPayload("animation.authoringOpen",args);
            return state;
        },true});
    registerStudioOverlayPlan(*session_, encodePng, [this](const Json& state, bool reveal) {
        sendEventPayload("overlay.preview", state);
        if (reveal) sendEventPayload("ui.showPanel", Json::parse(R"({"panel":"overlays"})"));
    });
    session_->setPlanetViewAgentProvider([this](const std::string& name,const Json& args){return planetViewAgentRequest(name,args);});
    session_->setMaterialSlotProvider([this](const std::string& asset) {
        return preview_ ? onScene([&] { return preview_->modelMaterialSlots(asset); }) : std::vector<std::string>{};
    });
    session_->setTextureGraphGpuProvider([this](editor::Workspace& ws,const Json& p){return textureGraphGpu(ws,p);});
    // Workspace::toolList is the common aiversemcp discovery surface.
    // Authenticated agent calls and HTML5 buttons use the same runtime
    // commands, compiled graph validation and render-thread ownership.
    editor::registerStudioPlayTools(session_->workspace(), [this](const std::string& method, const Json& params) {
        return localPlayOperation(method, params, agentPlayReview_);
    });
    editor::registerLightBakeRuntimeOps(session_->workspace(),[this](const std::string& name,const Json& params){
        return linkedLightBake(name,params);
    });
    session_->workspace().setSpectraPreviewHandler([this](const Json& p) {
        return runSpectraPreview(p);
    });
}
StudioHost::~StudioHost() { shutdown(); }
void StudioHost::cancelSpectraCapture() {
    auto shot=std::move(fxShot_);
    if(shot){std::lock_guard<std::mutex> lock(shot->mutex);shot->cancelled=true;}
}
void StudioHost::shutdown() {
    wizardDictation_.cancel();
    *alive_ = false;
    retireBuildingPanelPreview();
    cancelSpectraCapture();
    if(session_) {
        editor::registerLightBakeRuntimeOps(session_->workspace());
        session_->workspace().setSpectraPreviewHandler({});
        editor::registerStudioPlayTools(session_->workspace());
    }
    // Release editor-only EVOLVE scratch even if the webview never sends pagehide.
    wpNumericPreview_=Json();
    wpPreviewOwner_.clear();
    wpPreviewSelection_.clear();
    wpPreviewRevision_=-1;
    wpPivotSelection_.clear();
    wpCustomPivot_=false;
    wpDrag_={};
    game_.reset(); // hosted client and its device retire before the native surface
    if (renderThread_) {
        // The Scene and its GPU resources are released on their owner thread, then the thread stops.
        try { renderThread_->invoke([&] { gpuEmitterSurface_.renderer.reset(); effectFileSurface_.renderer.reset(); lightingSurface_.renderer.reset(); lensSurface_.renderer.reset(); attachmentSurface_.renderer.reset(); sourceModelSurface_.renderer.reset(); importedClipSurface_.renderer.reset(); animationGraphSurface_.renderer.reset(); animationRigSurface_.renderer.reset(); appearanceSurface_.renderer.reset(); characterSurface_.renderer.reset(); facialRigSurface_.renderer.reset(); materialSurface_.renderer.reset(); shaderSurface_.renderer.reset(); fxPreview_.reset(); preview_.reset(); }); } catch (const std::exception&) {}
        renderThread_->stop();
        renderThread_.reset();
    }
    fxPreview_.reset();
    preview_.reset(); // GPU resources before the native view
}

// ---------------------------------------------------------------- events
void StudioHost::sendEventPayload(const std::string& event, const Json& payload) {
    Json packet = Json::object();
    packet.set("event", Json::string(event));
    packet.set("payload", payload);
    platform_.evaluateScript("window.__aiverseReceive && window.__aiverseReceive(" + packet.dump(-1) + ");", false);
}
void StudioHost::sendEvent(const std::string& event, const std::string& message) {
    Json payload = Json::object();
    payload.set("message", Json::string(message));
    sendEventPayload(event, payload);
}
void StudioHost::sendReply(const Json& reply) {
    platform_.evaluateScript("window.__aiverseReceive && window.__aiverseReceive(" + reply.dump(-1) + ");", true);
}
void StudioHost::sendError(const Json& id, const std::string& message) {
    Json r = Json::object();
    r.set("id", id);
    r.set("error", Json::string(message));
    sendReply(r);
}
Json StudioHost::request(const char* method, Json params) {
    Json r = Json::object();
    r.set("id", Json::number(int64_t(0)));
    r.set("method", Json::string(method));
    r.set("params", std::move(params));
    return session_->dispatch(r);
}
void StudioHost::sendLoading(const std::string& phase, const std::string& message, int done, int total) {
    Json payload = Json::object();
    payload.set("phase", Json::string(phase));
    payload.set("message", Json::string(message));
    payload.set("done", Json::number(int64_t(done)));
    payload.set("total", Json::number(int64_t(total)));
    payload.set("active", Json::fromBool(phase != "done"));
    sendEventPayload("project.loading", payload);
}
void StudioHost::sendProjectCancelled(int64_t requestId) {
    Json reply = Json::object();
    reply.set("id", Json::number(requestId));
    Json payload = Json::object();
    payload.set("cancelled", Json::fromBool(true));
    payload.set("snapshot", Json());
    reply.set("result", std::move(payload));
    sendReply(reply);
}

// ---------------------------------------------------------------- images
bool StudioHost::readImage(const fs::path& file, int& w, int& h, std::vector<uint8_t>& rgba) {
    std::error_code ec;
    if (!fs::is_regular_file(file, ec) || fs::file_size(file, ec) > 64ull * 1024 * 1024) return false;
    std::ifstream in(file, std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(in)), {});
    int n = 0;
    if (stbi_uc* px = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(bytes.data()), int(bytes.size()), &w, &h, &n, 4)) {
        rgba.assign(px, px + size_t(w) * size_t(h) * 4);
        stbi_image_free(px);
        return w > 0 && h > 0;
    }
    return platform_.decodeImage(file, w, h, rgba) && w > 0 && h > 0 && rgba.size() >= size_t(w) * size_t(h) * 4;
}
std::string StudioHost::imageDataUrl(const fs::path& file, int maxSide, bool jpeg) {
    int w = 0, h = 0, ow = 0, oh = 0;
    std::vector<uint8_t> rgba;
    if (!readImage(file, w, h, rgba)) return {};
    const auto fitted = fitImage(rgba, w, h, maxSide, ow, oh);
    const std::string data = jpeg ? encodeJpeg(fitted, ow, oh, 80) : encodePng(fitted, ow, oh);
    if (data.empty()) return {};
    return std::string(jpeg ? "data:image/jpeg;base64," : "data:image/png;base64,") + editor::base64Encode(data);
}

// ---------------------------------------------------------------- project
// Splash while a project opens: the game's own name, splash art (game.json "splash": background, image, first
// logo) or icon, read straight from game.json before the project loads. Images go as small data URLs.
void StudioHost::sendProjectSplash(const std::string& path) {
    Json payload = Json::object();
    std::error_code ec;
    fs::path dir = fs::path(path);
    if (fs::is_regular_file(dir, ec)) dir = dir.parent_path();
    payload.set("path", Json::string(dir.string()));
    payload.set("name", Json::string(dir.filename().string()));
    auto image = [&](const std::string& rel, int maxSide, bool jpeg) -> std::string {
        if (rel.empty() || rel.find("..") != std::string::npos) return {};
        return imageDataUrl(dir / fs::path(rel), maxSide, jpeg);
    };
    try {
        const Json m = Json::parseFile((dir / "game.json").string());
        if (!m.getStr("name").empty()) payload.set("name", Json::string(m.getStr("name")));
        payload.set("module", Json::string(m.getStr("genre", m.getStr("module", "game"))));
        payload.set("maps", Json::number(int64_t(m["maps"].isArray() ? m["maps"].size() : 0)));
        const Json& sp = m["splash"];
        if (sp.isObject()) {
            if (sp["background"].isString()) payload.set("background", Json::string(sp.getStr("background")));
            const std::string art = image(sp.getStr("image"), 1280, true);
            if (!art.empty()) payload.set("art", Json::string(art));
            if (sp["logos"].isArray()) for (const auto& l : sp["logos"].items()) {
                const std::string logo = image(l.getStr("image"), 512, false);
                if (!logo.empty()) { payload.set("logo", Json::string(logo)); break; }
            }
        }
        if (!payload.has("logo")) {
            const std::string icon = image(m.getStr("icon"), 256, false);
            if (!icon.empty()) payload.set("logo", Json::string(icon));
        }
    } catch (const std::exception&) {}
    // Automation: keep the finished splash up for N seconds (screenshots of the splash design).
    if (const char* hold = std::getenv("AIVERSE_STUDIO_SPLASH_HOLD")) payload.set("holdMs", Json::number(std::atof(hold) * 1000.0));
    sendEventPayload("project.splash", payload);
}
void StudioHost::webReady() {
    if (pendingProject_.empty()) return;
    const std::string project = pendingProject_;
    pendingProject_.clear();
    openProject(project, -1);
}
// Opens a project on a background thread (the UI shows the splash), then resumes the Scene, which streams
// models in with a per-frame budget. requestId < 0: --project / last project.
void StudioHost::openProject(const std::string& path, int64_t requestId) {
    wizardDictation_.cancel();
    if (projectLoading_) return;
    if(preview_&&!textureGraphScope_.empty()){
        try{onScene([&]{preview_->invalidateTextureGraphProject();});}catch(const std::exception& e){if(requestId>=0)sendError(Json::number(requestId),e.what());return;}
        textureGraphWork_=true;textureGraphScope_.clear();
    }
    session_->strokeCancel();mouse_={};
    // A temporary building Model belongs to the previous project's
    // renderer epoch. Retire it BEFORE the Workspace changes, even
    // when the generator panel is not mounted and cannot send Stop.
    if(preview_){
        std::string issue;
        const bool retired=onScene([&]{
            return preview_->setBuildingPreview({},&issue);
        });
        if(!retired){
            if(requestId>=0)
                sendError(Json::number(requestId),issue);
            else platform_.log("Building preview retirement blocks project switch: "+issue);
            return;
        }
    }
    retireBuildingPanelPreview();
    buildingGraphOverlayScene_=0;rebuildOverlay();
    session_->setBuildingGraphReader({});
    projectLoading_ = true;
    ++packageProjectEpoch_;
    cancelSpectraCapture(); // invalidate old project callbacks before opening
    if (game_) game_->stop();
    wpLiveSnapshot_=Json();wpLivePlanetProject_.clear();wpLivePlanetMapPath_.clear();wpLivePlanetRevision_=0;wpLiveRefresh_=0;
    rebuildOverlay();
    clearPlayReviews();
    sendProjectSplash(path);
    sendLoading("project", "Opening " + fs::path(path).filename().string(), 0, 0);
    StudioSession* session = session_.get();
    auto snapshot = std::make_shared<Json>();
    auto error = std::make_shared<std::string>();
    auto alive = alive_;
    platform_.runInBackground(
        [session, path, snapshot, error] {
            try { *snapshot = session->openProject(path); }
            catch (const std::exception& e) { *error = e.what(); }
        },
        [this, alive, path, requestId, snapshot, error] {
            if (!*alive) return;
            // Reopening the same directory is a new preview lifetime too.
            // Invalidate on successful project open before accepting commands.
            if(error->empty()&&fxPreview_)onScene([&]{fxPreview_.reset();});
            projectLoading_ = false;
            if (preview_) onScene([&] { preview_->resetCamera(); });
            mapSnapshot_.reset(); rulesSnapshot_.reset();
            lastLoadDone_ = lastLoadTotal_ = -1;
            if (requestId >= 0) {
                Json reply = Json::object();
                reply.set("id", Json::number(requestId));
                if (!error->empty()) reply.set("error", Json::string(*error));
                else {
                    Json payload = Json::object();
                    payload.set("cancelled", Json::fromBool(false));
                    payload.set("snapshot", *snapshot);
                    reply.set("result", payload);
                }
                sendReply(reply);
            }
            if (!error->empty()) {
                platform_.log("AIVERSE open project failed: " + *error);
                sendLoading("error", *error, 0, 0);
            } else {
                platform_.rememberLastProject(path);
                // Scene models stream in next; the tick reports their progress.
                sendLoading("scene", "Preparing scene", 0, 0);
                loadingShown_ = true;
            }
            sendEvent("workspace.changed", "");
        });
}
bool StudioHost::confirmClose() {
    const auto& ws = session_->workspace();
    if (!ws.map.dirty && !ws.project.rulesDirty && !ws.project.manifestDirty) return true;
    return platform_.confirmDiscardChanges("Discard unsaved changes?", "Save the current document first to keep your changes.", "Discard Changes");
}

// ---------------------------------------------------------------- tick
Json StudioHost::gameState(bool includeScriptErrors) {
    if (game_) {auto out=game_->state(includeScriptErrors);out.set("canReviewPlayChanges",Json::fromBool(game_->canReview(session_->workspace())));return out;}
    auto out = Json::parse(R"({"paused":false,"canControl":false,"canStep":false,"active":false,"loading":false,"progress":0,"inMatch":false,"tick":0,"error":""})");
    out.set("device", StudioGameDevice{}.json());
    return out;
}
void StudioHost::gameEvent(const platform::Event& event) {
    if (game_ && platform_.surfaceDrawable(StudioSurface::Game)) game_->event(event);
}
void StudioHost::surfaceDetached(StudioSurface surface) {
    if (surface == StudioSurface::Scene) { if(!projectLoading_&&session_->strokeCancel())sendEvent("document.changed", "");mouse_={};placement_ = {}; readyEmitted_ = false; }
    else if(surface>=StudioSurface::Material) {
        auto& asset=assetSurface(surface);
        asset.placement={};
        if(asset.renderer)onScene([&]{asset.renderer.reset();});
    }
    else if(surface == StudioSurface::Fx) {
        fxPlacement_={};fxSurfaceVisible_=false;
        cancelSpectraCapture();
        if(fxPreview_)onScene([&]{fxPreview_.reset();});
    }
    else { gamePlacement_ = {}; if (game_) game_->suspend(); }
}
void StudioHost::restartGame() {
    clearPlayReviews();
    if(!game_->state().getBool("active"))throw std::runtime_error("Start a game before restarting");
    buildingGraphOverlayScene_=0;session_->setBuildingGraphReader({});rebuildOverlay();
    const auto old=game_->launchPlan();
    if (old.localSeat && old.sourceMap!=session_->workspace().map.path)
        throw std::runtime_error("Select a player on the current map before restarting");
    auto plan=studioGameLaunch(session_->workspace(),old.map.has_value(),old.localSeat);
    plan.camera=old.camera;
    game_->start(plan,engineRoot_+"/engine/assets");gameFrameTime_=0;
    wpLivePlanetProject_=plan.project;
    wpLivePlanetMapPath_=plan.map?session_->workspace().map.path:std::string{};
    wpLivePlanetRevision_=plan.map?session_->workspace().map.revision:0;
    wpLiveSnapshot_=Json();wpLiveRefresh_=0;rebuildOverlay();
}

void StudioHost::pollScriptChanges() {
    auto& ws = session_->workspace();
    const auto& project = ws.project.dir;
    const double now = platform_.now();
    const bool baseline = project != scriptWatchProject_;
    if (!baseline && now < nextScriptPoll_) return;
    nextScriptPoll_ = now + 1.0;
    std::map<std::string,fs::file_time_type> stamps;
    std::error_code ec;
    const auto root = fs::path(project) / "scripts";
    if (!project.empty() && fs::exists(root,ec) && !fs::is_symlink(root,ec)) {
        for (fs::recursive_directory_iterator it(root,ec), end; !ec && it!=end; it.increment(ec)) {
            const auto& file = *it;
            if (file.path().extension() != ".lua" || file.is_symlink(ec) || !file.is_regular_file(ec)) continue;
            const auto stamp = file.last_write_time(ec);
            if (ec) break;
            stamps.emplace(file.path().lexically_relative(project).generic_string(),stamp);
        }
    }
    if (ec) return;
    if (baseline) { scriptWatchProject_=project; scriptStamps_=std::move(stamps); return; }
    if (stamps == scriptStamps_) return;
    for (const auto& [path,stamp] : stamps) {
        const auto previous=scriptStamps_.find(path);
        if (previous!=scriptStamps_.end() && previous->second==stamp) continue;
        std::ifstream in(fs::path(project)/path,std::ios::binary);
        if (!in) {ws.log("script","Cannot read "+path,true);continue;}
        std::string text((std::istreambuf_iterator<char>(in)),{});
        const auto error=game::checkScriptSyntax(path,text,path.rfind("scripts/view/",0)!=0);
        ws.log("script",error.empty()?"Changed "+path:error,!error.empty());
    }
    for (const auto& [path,stamp] : scriptStamps_)
        if (!stamps.count(path)) ws.log("script","Removed "+path);
    scriptStamps_=std::move(stamps);
    if (restartOnScriptChange_ && game_ && game_->launchPlan().project==project && game_->state().getBool("active")) {
        try { restartGame();ws.log("script","Restarted play-test after Lua changes"); }
        catch (const std::exception& error) {ws.log("script",error.what(),true);}
    }
    sendEvent("document.changed","");
}

void StudioHost::tickGame() {
    const double now = platform_.now(), dt = gameFrameTime_ > 0 ? now - gameFrameTime_ : 0;
    gameFrameTime_ = now;
    if (!game_) {session_->setBuildingGraphReader({});return;}
    const auto invalidate=[&]{
        if(!wpLiveSnapshot_.getBool("active"))return;
        const bool planet=wpLiveSnapshot_.getStr("topology")=="planet";
        if(planet?(!game_->planetSnapshotCurrent(wpLiveSnapshot_)||!worldProcessPlanetMapCurrent()):!game_->state().getBool("inMatch")){
            wpLiveSnapshot_=Json();wpLiveRefresh_=0;rebuildOverlay();
        }
    };
    invalidate();
    if (!gamePlacement_.visible || !platform_.surfaceDrawable(StudioSurface::Game)) { game_->suspend(); return; }
    game_->frame(gamePlacement_.pixelsW, gamePlacement_.pixelsH, float(gamePlacement_.width), float(gamePlacement_.height),
                 dt, platform_.gameFocused());
    invalidate();
    if(wpLiveOptions_.live && now-wpLiveRefresh_>=.25){
        wpLiveRefresh_=now;
        wpLiveSnapshot_=sampleWorldProcessLive();
        rebuildOverlay();
    }
    session_->setBuildingGraphReader(game_->buildingGraphReader());
}
void StudioHost::captureLensFrame(int64_t cameraId) {
    if (!lensSurface_.renderer || lensCameraId_!=cameraId || !lensSurface_.placement.visible)
        throw std::runtime_error("Open this camera preview to capture a frame");
    if (lensShot_) throw std::runtime_error("A camera capture is already pending");
    auto shot = std::make_shared<AgentShot>();
    onScene([&] { lensSurface_.renderer->captureNextFrame([shot](int w,int h,std::vector<uint8_t> pixels) {
        try {
            if (w>0 && h>0 && int64_t(w)*h<=16*1024*1024 && size_t(w)*size_t(h)*4==pixels.size()) {
                for (size_t i=3;i<pixels.size();i+=4) pixels[i]=255;
                shot->png=encodePng(pixels,w,h);
            }
        } catch (const std::exception&) {}
        shot->done.store(true);
    }); });
    lensShot_=std::move(shot);
    lensCaptureStarted_=platform_.now();
}
void StudioHost::finishLensCapture() {
    if (!lensShot_) return;
    if (lensProject_!=session_->workspace().project.dir) {lensShot_.reset();return;}
    if (!lensShot_->done.load() && platform_.now()-lensCaptureStarted_<12) return;
    auto shot=std::move(lensShot_);
    Json status=Json::object();
    status.set("id",Json::number(lensCameraId_));
    status.set("project",Json::string(lensProject_));
    try {
        if (!shot->done.load() || shot->png.empty()) throw std::runtime_error("Camera frame readback failed");
        const auto stamp=std::chrono::system_clock::now().time_since_epoch().count();
        const auto relative=fs::path("captures")/"lens"/("camera-"+std::to_string(lensCameraId_)+"-"+std::to_string(stamp)+".png");
        const auto file=fs::path(lensProject_)/relative;
        editor::graph_asset::Transaction transaction(file,lensProject_);
        if(fs::exists(transaction.target()))throw std::runtime_error("Capture file already exists");
        transaction.replace(shot->png,80*1024*1024);
        status.set("path",Json::string(relative.generic_string()));
    }catch(const std::exception& e){status.set("error",Json::string(e.what()));}
    sendEventPayload("lens.capture",status);
}
void StudioHost::tickAgentScreenshot() {
    if (agentShot_) {
        // The capture callback may run on a GPU completion thread; it encodes there and flags done.
        if (!agentShot_->done.load()) return;
        if (agentShot_->png.empty()) session_->finishAgentScreenshot("", 0, 0, "Scene capture failed");
        else session_->finishAgentScreenshot(editor::base64Encode(agentShot_->png), agentShot_->w, agentShot_->h, "");
        agentShot_.reset();
        return;
    }
    if (!session_->agentScreenshotPending()) return;
    if (!preview_ || !placement_.visible) {
        session_->finishAgentScreenshot("", 0, 0, "The Scene panel is not visible in Studio; ask the user to show it");
        return;
    }
    auto shot = std::make_shared<AgentShot>();
    agentShot_ = shot;
    onScene([&] { preview_->captureNextFrame([shot](int w, int h, std::vector<uint8_t> rgba) {
        if (w > 0 && h > 0 && rgba.size() >= size_t(w) * size_t(h) * 4) {
            // At most 1280 px wide: enough for the agent, small for the MCP answer.
            const int ow = std::min(w, 1280), oh = std::max(1, int(int64_t(h) * ow / w));
            std::vector<uint8_t> out(size_t(ow) * size_t(oh) * 4);
            for (int y = 0; y < oh; ++y) for (int x = 0; x < ow; ++x) {
                const size_t sx = size_t(x) * size_t(w) / size_t(ow), sy = size_t(y) * size_t(h) / size_t(oh);
                const uint8_t* p = &rgba[(sy * size_t(w) + sx) * 4];
                uint8_t* q = &out[(size_t(y) * size_t(ow) + size_t(x)) * 4];
                q[0] = p[0]; q[1] = p[1]; q[2] = p[2]; q[3] = 255;
            }
            shot->png = encodePng(out, ow, oh);
            shot->w = ow; shot->h = oh;
        }
        shot->done.store(true);
    }); });
}
// Agent snapshot handshake (MCP studio_character_snapshot): the tool writes
// <project>/.aiverse/agent-requests/character-snapshot.json {id, view}; the
// next shown character frame is saved as .aiverse/agent-snapshots/<id>.png.
void StudioHost::pollAgentRequests() {
    const double now = platform_.now();
    if (now < nextAgentPoll_) return;
    nextAgentPoll_ = now + 0.4;
    const fs::path root = fs::path(session_->workspace().project.dir);
    const fs::path req = root / ".aiverse" / "agent-requests" / "character-snapshot.json";
    std::error_code ec;
    if (!fs::is_regular_file(req, ec) || fs::file_size(req, ec) > 4096) return;
    std::string bytes;
    { std::ifstream in(req, std::ios::binary); bytes.assign(std::istreambuf_iterator<char>(in), {}); }
    fs::remove(req, ec);
    try {
        const auto r = Json::parse(bytes);
        const std::string id = r.getStr("id");
        if (id.empty() || id.size() > 40 || id.find_first_not_of("0123456789abcdefghijklmnopqrstuvwxyz-") != std::string::npos) return;
        const fs::path out = root / ".aiverse" / "agent-snapshots";
        fs::create_directories(out, ec);
        if (!activeHumanRenderer() || !session_->facialPreviewBinding().bound()) {
            std::ofstream err(out / (id + ".error"));
            err << "Покажите персонажа во вьюпорте Character Creator";
            return;
        }
        const std::string view = r.getStr("view");
        if (view == "face" || view == "torso" || view == "full") {
            Json p = Json::object();
            p.set("view", Json::string(view));
            (void)request("studio.humanCamera", p);
        }
        onScene([&] { activeHumanRenderer()->captureFacialThumbnail((out / (id + ".png")).string(), false); });
    } catch (const std::exception&) {}
}
StudioHost::AssetSurface& StudioHost::assetSurface(StudioSurface surface) {
    switch(surface){
    case StudioSurface::GpuEmitter:return gpuEmitterSurface_;
    case StudioSurface::EffectFile:return effectFileSurface_;
    case StudioSurface::Lighting:return lightingSurface_;
    case StudioSurface::LensCamera:return lensSurface_;
    case StudioSurface::Attachments:return attachmentSurface_;
    case StudioSurface::ImportedClip:return importedClipSurface_;
    case StudioSurface::SourceModel:return sourceModelSurface_;
    case StudioSurface::AnimationGraph:return animationGraphSurface_;
    case StudioSurface::AnimationRig:return animationRigSurface_;
    case StudioSurface::Material:return materialSurface_;
    case StudioSurface::Shader:return shaderSurface_;
    case StudioSurface::FacialRig:return facialRigSurface_;
    case StudioSurface::Character:return characterSurface_;
    case StudioSurface::Appearance:return appearanceSurface_;
    default:throw std::invalid_argument("Invalid asset viewport");
    }
}
StudioScene* StudioHost::activeHumanRenderer(){
    if(characterSurface_.placement.visible&&characterSurface_.renderer)return characterSurface_.renderer.get();
    return appearanceSurface_.placement.visible?appearanceSurface_.renderer.get():nullptr;
}
void StudioHost::tickAnimationPreview(StudioSurface surface) {
    auto& asset=assetSurface(surface);
    if(!asset.placement.visible||!platform_.surfaceDrawable(surface))return;
    try {
        const auto& ws=session_->workspace();
        const auto frame=ws.animationPreviews->drawFrame(ws.project.dir,surface==StudioSurface::AnimationRig);
        if(!renderThread_){
            const char* threaded=std::getenv("AIVERSE_STUDIO_RENDER_THREAD");
            renderThread_=std::make_unique<StudioRenderThread>([this]{renderSceneFrame();},!(threaded&&std::string(threaded)=="0"));
        }
        onScene([&]{platform_.sceneThreadScope([&]{
            if(!asset.renderer)asset.renderer=std::make_unique<StudioScene>(platform_.createDevice(surface));
            asset.renderer->drawAnimationPose(asset.placement.pixelsW,asset.placement.pixelsH,frame.source,frame.pose,frame.yaw);
        });});
    }catch(const std::exception& e){platform_.log(std::string("Studio animation preview: ")+e.what());surfaceDetached(surface);}
}
void StudioHost::tickCharacterPreview(StudioSurface surface) {
    auto& asset=assetSurface(surface);
    const bool rig=surface==StudioSurface::FacialRig;
    auto& binding=rig?session_->rigPreviewBinding():session_->humanPreviewBinding();
    if(!binding.bound()||!asset.placement.visible||!platform_.surfaceDrawable(surface))return;
    try {
        if(!renderThread_){
            const char* threaded=std::getenv("AIVERSE_STUDIO_RENDER_THREAD");
            renderThread_=std::make_unique<StudioRenderThread>([this]{renderSceneFrame();},!(threaded&&std::string(threaded)=="0"));
        }
        onScene([&]{platform_.sceneThreadScope([&]{
            if(!asset.renderer)asset.renderer=std::make_unique<StudioScene>(platform_.createDevice(surface));
            const auto& ws=session_->workspace();
            asset.renderer->frame(asset.placement.pixelsW,asset.placement.pixelsH,ws.map,false,"","",Json(),0,
                float(ws.project.rules.levelHeight.toDouble()),ws.project.dir,ws.project.rulesJson,&binding);
            if(!rig)session_->setFacialPreviewClips(asset.renderer->facialClips(),asset.renderer->facialBaseHeight());
            binding.setGpuStatus(asset.renderer->facialReady(),asset.renderer->facialError());
        });});
    }catch(const std::exception& e){
        binding.setGpuStatus(false,e.what());
        platform_.log(std::string("Studio facial rig preview: ")+e.what());
        surfaceDetached(surface);
    }
}
void StudioHost::tickAssetPreview(StudioSurface surface) {
    const bool shader=surface==StudioSurface::Shader;
    auto& asset=shader?shaderSurface_:materialSurface_;
    const auto& title=shader?session_->previewGraphTitle():session_->previewMaterialName();
    if(!asset.placement.visible || title.empty() || !platform_.surfaceDrawable(surface))return;
    std::string error;
    try {
        if(!renderThread_) {
            const char* threaded=std::getenv("AIVERSE_STUDIO_RENDER_THREAD");
            renderThread_=std::make_unique<StudioRenderThread>([this]{renderSceneFrame();},!(threaded&&std::string(threaded)=="0"));
        }
        onScene([&]{
            platform_.sceneThreadScope([&]{
                if(!asset.renderer)asset.renderer=std::make_unique<StudioScene>(platform_.createDevice(surface));
                const auto& ws=session_->workspace();
                asset.renderer->frame(asset.placement.pixelsW,asset.placement.pixelsH,ws.map,false,"",title,
                    shader?session_->previewGraphFactors():session_->previewMaterialFactors(),
                    shader?session_->previewGraphRevision():session_->previewMaterialRevision(),
                    float(ws.project.rules.levelHeight.toDouble()),ws.project.dir);
                if(shader)error=asset.renderer->shaderPreviewError();
            });
        });
    }catch(const std::exception& e){
        error=e.what();platform_.log("Studio asset preview: "+error);
        surfaceDetached(surface);
    }
    if(shader&&asset.error!=error){
        asset.error=error;
        sendEvent("shader.previewError",error);
    }
}
void StudioHost::ensureScene() {
    if (!renderThread_) {
        // The Scene renders on its own thread (Unreal-style render thread);
        // AIVERSE_STUDIO_RENDER_THREAD=0 runs it on the UI thread instead.
        const char* threaded = std::getenv("AIVERSE_STUDIO_RENDER_THREAD");
        renderThread_ = std::make_unique<StudioRenderThread>([this] { renderSceneFrame(); }, !(threaded && std::string(threaded) == "0"));
    }
    if (!preview_) {
        const char* continuous = std::getenv("AIVERSE_STUDIO_CONTINUOUS_REDRAW");
        const bool always = continuous && std::string(continuous) == "1";
        const bool composite = htmlComposite_;
        renderThread_->invoke([&] {
            // Created on its owner thread: GPU resource caches bind to it.
            auto created = std::make_unique<StudioScene>(platform_.createDevice(StudioSurface::Scene));
            // Default host: the native view covers HTML, so the renderer draws the
            // orientation gizmo itself; composite mode uses the HTML gizmo.
            created->setBakedSceneEnabled(showSceneBake_);
            created->setOrientationGizmo(!composite);
            // Interactive viewport: redraw on change only, debounce lighting rebuilds.
            // AIVERSE_STUDIO_CONTINUOUS_REDRAW=1 restores drawing every tick.
            created->setContinuousRedraw(always);
            preview_ = std::move(created);
        });
    }

}
Json StudioHost::textureGraphGpu(editor::Workspace& ws,const Json& p) {
    const auto action=p.getStr("action");
    if(action=="capabilities"){
        try{
            if(projectLoading_||ws.project.dir.empty())throw std::runtime_error("Open a project first");
            if(!textureGraphError_.empty())throw std::runtime_error(textureGraphError_);
            ensureScene();
            return onScene([&]{return preview_->textureGraphCapabilities();});
        }catch(const std::exception& e){
            Json r=Json::object();r.set("ok",Json::fromBool(true));r.set("available",Json::fromBool(false));
            r.set("residentApply",Json::fromBool(false));r.set("reason",Json::string(e.what()));return r;
        }
    }
    if(projectLoading_||ws.project.dir.empty())throw std::runtime_error("Open a project first");
    if(!textureGraphError_.empty())throw std::runtime_error(textureGraphError_);
    if(action=="start"||action=="enqueue"||action=="studioModelTargets"||action=="studioPbrPrepare")ensureScene();
    if(!preview_)throw std::runtime_error("Start a Studio GPU preview first");
    Json result;
    onScene([&]{platform_.sceneThreadScope([&]{result=preview_->textureGraphOperation(ws,p);});});
    textureGraphScope_=ws.project.dir;
    textureGraphWork_=true;
    return result;
}
void StudioHost::tickTextureGraph(const std::string& project) {
    if(!preview_||(!textureGraphWork_&&textureGraphScope_==project))return;
    if(!textureGraphWork_&&textureGraphScope_.empty())return;
    try{
        bool pending=false;
        onScene([&]{platform_.sceneThreadScope([&]{pending=preview_->serviceTextureGraph(project);});});
        textureGraphWork_=pending;textureGraphScope_=project;
    }catch(const std::exception& e){
        textureGraphWork_=false;textureGraphScope_=project;
        textureGraphError_=e.what();
        platform_.log("Studio Texture Graph: "+textureGraphError_);
    }
}
Json StudioHost::buildingPanelFrame(const BuildingPreviewJob& job){
    // Only the Building render worker accesses its device/model.
    if(!buildingPanelPreview_)throw std::runtime_error("No active Building panel preview");
    std::vector<uint8_t> pixels;
    platform_.sceneThreadScope([&]{pixels=buildingPanelPreview_->frame(job.yaw,job.pitch,job.zoom,job.panX,job.panY);});
    const auto bytes=encodeJpeg(pixels,StudioBuildingPanelPreview::Side,StudioBuildingPanelPreview::Side,90);
    if(bytes.empty())throw std::runtime_error("Building preview JPEG encoding failed");
    Json result=Json::object();
    result.set("active",Json::fromBool(true));
    result.set("token",Json::string(std::to_string(job.token)));
    result.set("url",Json::string("data:image/jpeg;base64,"+editor::base64Encode(bytes)));
    return result;
}
void StudioHost::renderBuildingPanelFrame(){
    std::optional<BuildingPreviewJob> job;
    {std::lock_guard<std::mutex> lock(buildingPreviewMutex_);job=std::move(buildingPreviewJob_);buildingPreviewJob_.reset();}
    if(!job)return;
    Json reply=Json::object();reply.set("id",job->id);
    try{reply.set("result",buildingPanelFrame(*job));}
    catch(const std::exception& e){reply.set("error",Json::string(e.what()));}
    catch(...){reply.set("error",Json::string("Unknown Building preview error"));}
    {std::lock_guard<std::mutex> lock(buildingPreviewMutex_);buildingPreviewReply_=std::move(reply);}
}
void StudioHost::publishBuildingPanelFrame(){
    std::optional<Json> reply;
    {std::lock_guard<std::mutex> lock(buildingPreviewMutex_);reply=std::move(buildingPreviewReply_);buildingPreviewReply_.reset();}
    if(!reply)return;
    buildingPreviewPending_=false;
    // A delayed reply is no longer inside its initiating WebView callback.
    // Bridge request IDs are window-scoped; broadcast the addressed reply.
    if(*alive_)platform_.evaluateScript("window.__aiverseReceive && window.__aiverseReceive("+reply->dump(-1)+");",false);
}
void StudioHost::retireBuildingPanelPreview(){
    buildingPreviewActive_=false;
    if(buildingPreviewThread_){
        buildingPreviewThread_->flush();
        buildingPreviewThread_->invoke([&]{platform_.sceneThreadScope([&]{buildingPanelPreview_.reset();});});
        buildingPreviewThread_->stop();buildingPreviewThread_.reset();
    }
    publishBuildingPanelFrame();
    buildingPreviewPending_=false;
    ++buildingPreviewToken_;
}
void StudioHost::tickFx() {
    if(!fxPlacement_.visible || !platform_.surfaceDrawable(StudioSurface::Fx))return;
    try {
        if(!renderThread_) {
            const char* threaded=std::getenv("AIVERSE_STUDIO_RENDER_THREAD");
            renderThread_=std::make_unique<StudioRenderThread>(
                [this]{renderSceneFrame();},!(threaded&&std::string(threaded)=="0"));
        }
        if(!fxPreview_) {
            onScene([&] {
                auto created=std::make_unique<StudioScene>(
                    platform_.createDevice(StudioSurface::Fx));
                created->setOrientationGizmo(false);
                created->setContinuousRedraw(true);
                created->setSpectraDockMode(true);
                created->setSpectraEngineAssetRoot(engineRoot_+"/engine/assets/");
                fxPreview_=std::move(created);
            });
        }
        const auto& ws=session_->workspace();
        const auto width=fxPlacement_.pixelsW,height=fxPlacement_.pixelsH;
        // Only one native render owner; the independent FX drawable does not
        // advance, block or copy Scene's simulation, selection or GPU device.
        onScene([&] {
            platform_.sceneThreadScope([&] {
                fxPreview_->frame(width,height,ws.map,ws.map.data.worldSpace,
                    "","",Json::object(),0,
                    float(ws.project.rules.levelHeight.toDouble()),
                    ws.project.dir,ws.project.rulesJson);
            });
        });
    } catch(const std::exception& ex) {
        platform_.log(std::string("AIVERSE independent FX GPU view: ")+ex.what());
        ViewportRect hidden;NativeViewportPlacement none;
        (void)platform_.placeSurface(StudioSurface::Fx,hidden,none);
        fxPlacement_={};fxSurfaceVisible_=false;
        cancelSpectraCapture();
        try{onScene([&]{fxPreview_.reset();});}catch(const std::exception&){}
    }
}
void StudioHost::fxCameraOrbit(StudioSurface surface,double dx,double dy) {
    if(!finite({dx,dy})||std::abs(dx)>250||std::abs(dy)>250)return;
    if(surface==StudioSurface::EffectFile || surface==StudioSurface::GpuEmitter){
        auto& asset=assetSurface(surface);
        if(projectLoading_||!asset.renderer||!asset.placement.visible)return;
        Json input=Json::object();
        input.set("orbitX",Json::number(dx));input.set("orbitY",Json::number(dy));
        onScene([&]{if(surface==StudioSurface::GpuEmitter)asset.renderer->gpuEmitterCommand(gpuEmitterProject_,input);
            else asset.renderer->effectFileCommand(effectFileProject_,input);});
        return;
    }
    if(surface!=StudioSurface::Fx||!fxPreview_ || !fxPlacement_.visible)return;
    onScene([&]{
        if(!fxPreview_->spectraPreviewCommand(Json::parse(R"({"action":"status"})"),session_->workspace().project.dir).getBool("replayPending"))
            fxPreview_->orbit(float(dx),float(dy));
    });
}
void StudioHost::fxCameraZoom(StudioSurface surface,double wheel) {
    if(!std::isfinite(wheel)||std::abs(wheel)>250)return;
    if(surface==StudioSurface::EffectFile || surface==StudioSurface::GpuEmitter){
        auto& asset=assetSurface(surface);
        if(projectLoading_||!asset.renderer||!asset.placement.visible)return;
        Json input=Json::object();
        input.set("zoom",Json::number(wheel));
        onScene([&]{if(surface==StudioSurface::GpuEmitter)asset.renderer->gpuEmitterCommand(gpuEmitterProject_,input);
            else asset.renderer->effectFileCommand(effectFileProject_,input);});
        return;
    }
    if(surface!=StudioSurface::Fx||!fxPreview_ || !fxPlacement_.visible)return;
    onScene([&]{
        if(!fxPreview_->spectraPreviewCommand(Json::parse(R"({"action":"status"})"),session_->workspace().project.dir).getBool("replayPending"))
            fxPreview_->zoom(float(wheel));
    });
}
void StudioHost::tickLightBake() {
    if (!bakeFiles_) return;
    const auto& ws=session_->workspace();
    const bool stale=session_->graphDocumentScope().getStr("epoch")!=bakeEpoch_ || ws.map.revision!=bakeRevision_;
    if (stale && !pendingBake_) bakeJob_.cancel();
    try {
        if(pendingBake_){
            if(stale)throw std::runtime_error("Map changed during capture");
            ensureScene();
            const bool current=mapSnapshot_ && rulesSnapshot_ && rulesSnapshotProject_==ws.project.dir &&
                mapSnapshot_->path==ws.map.path && mapSnapshot_->revision==ws.map.revision && *rulesSnapshot_==ws.project.rulesJson;
            auto input=onScene([&]{return preview_->prepareLightBake(current?*mapSnapshot_:ws.map,ws.project.dir,ws.project.rules,
                current?*rulesSnapshot_:ws.project.rulesJson,float(ws.project.rules.levelHeight.toDouble()));});
            if(!input)return;
            input->settings=pendingBake_->settings;input->folder=pendingBake_->folder;
            input->force=pendingBake_->force;input->oldHash=pendingBake_->oldHash;
            bakeJob_.start(std::move(*input),
                [files=bakeFiles_](const auto& path,auto& bytes){return files->read(path,bytes);},
                [files=bakeFiles_](const auto& path,const auto& bytes){return files->stage(path,bytes);});
            pendingBake_.reset();
            return;
        }
        const auto result=bakeJob_.poll();
        if (!result) return;
        bakeFailed_=stale || !result->error.empty();
        if (stale) bakeMessage_="Map changed during baking";
        else if (!result->error.empty()) bakeMessage_=result->error;
        else if (result->unchanged) {
            if(result->generatedUvs)bakeFiles_->publishRecoveredUvs();
            bakeMessage_="Baked lighting is up to date";
        }
        else {
            bakeFiles_->publish();
            bakeMessage_="Baked "+std::to_string(result->stats.pages)+
                (result->stats.pages==1?" atlas page and ":" atlas pages and ")+
                std::to_string(result->stats.probes)+(result->stats.probes==1?" probe":" probes");
            if(!result->warnings.empty())bakeMessage_+="\n"+result->warnings;
        }
    } catch (const std::exception& e) { bakeFailed_=true; bakeMessage_=e.what(); }
    pendingBake_.reset();bakeFiles_.reset();
    sendEventPayload("lightBake.finished",Json::string(bakeEpoch_));
}
Json StudioHost::linkedLightBake(const std::string& name,const Json& params) {
    const auto epoch=session_->graphDocumentScope().getStr("epoch");
    Json command=Json::object();
    command.set("expectedDocumentEpoch",Json::string(epoch));
    if(name=="lightbake_bake") {
        Json settings=Json::object();
        for(const auto& [key,value]:params.members())if(key!="force")settings.set(key,value);
        if(params.has("force") && params["force"].type()!=Json::Type::Bool)throw std::runtime_error("Expected boolean force");
        command.set("action",Json::string("start"));
        command.set("force",Json::fromBool(params.getBool("force")));
        lightBakeJob(command,settings);
        Json result=Json::object();
        result.set("ok",Json::fromBool(true));result.set("started",Json::fromBool(true));
        result.set("next",Json::string("Poll lightbake_status until state is idle"));
        return result;
    }
    auto status=lightBakeJob(command);
    const auto display=preview_?onScene([&]{return preview_->bakedSceneStatus();}):Json::object();
    status.set("state",Json::string(pendingBake_?"capture":status.getBool("running")?"baking":"idle"));
    status.set("progress",status["fraction"]);
    status.set("failed",Json::fromBool(bakeEpoch_==epoch && bakeFailed_));
    status.set("bake",editor::lightBakeInfo(session_->workspace()));
    status.set("loaded",Json::fromBool(display.getBool("loaded")));
    status.set("lightmappedPrimitives",Json::number(int64_t(display.getInt("lightmapped"))));
    status.set("probePrimitives",Json::number(int64_t(display.getInt("probed"))));
    return status;
}

Json StudioHost::lightBakeJob(const Json& params,const Json& settings) {
    if (projectLoading_) throw std::runtime_error("Project is loading");
    const auto& ws=session_->workspace();
    const auto epoch=session_->graphDocumentScope().getStr("epoch");
    if (params.getStr("expectedDocumentEpoch")!=epoch) throw std::runtime_error("Map changed");
    const auto action=params.getStr("action","status");
    if (action=="start") {
        const auto relative=editor::lightBakeMapRel(ws);
        if (bakeFiles_) throw std::runtime_error("A light bake is already running");
        if (relative.empty()) throw std::runtime_error("Save the active map before baking");
        if (game_ && game_->state().getBool("active")) throw std::runtime_error("Stop the play-test before baking");
        if(ws.map.data.planet())throw std::runtime_error("Lightmap capture requires a planar map");
        ensureScene();
        bake::CapturedBake input;
        input.folder=bake::bakeFolderFor(relative);
        input.settings=editor::lightBakeSettings(ws);
        if(settings.size()){
            std::string error;
            if(!bake::settingsFromJson(settings.dump(-1),input.settings,error))throw std::runtime_error(error);
        }
        input.force=params.getBool("force");
        const auto info=editor::lightBakeInfo(ws);
        if(info.getBool("exists"))bake::parseHex64(info.getStr("hash"),input.oldHash);
        auto files=std::make_shared<StudioLightBakeFiles>(ws.project.dir,input.folder);
        if(settings.size())editor::applyLightBakeSettings(session_->workspace(),input.settings);
        pendingBake_=std::move(input);
        bakeFailed_=false;
        bakeFiles_=std::move(files);
        bakeEpoch_=epoch;
        bakeRevision_=ws.map.revision;
        bakeMessage_.clear();
        sendEventPayload("lightBake.started",Json::string(epoch));
    } else if (action=="cancel") {
        if (bakeFiles_ && bakeEpoch_==epoch) {
            if(pendingBake_){
                pendingBake_.reset();bakeFiles_.reset();bakeFailed_=true;bakeMessage_="cancelled";
                sendEventPayload("lightBake.finished",Json::string(epoch));
            } else bakeJob_.cancel();
        }
    } else if (action!="status") throw std::runtime_error("Unknown light bake action");
    const auto progress=bakeJob_.progress();
    Json reply=Json::object();
    reply.set("running",Json::fromBool(bool(bakeFiles_)));
    reply.set("stage",Json::string(pendingBake_?"capture":bakeFiles_?progress.stage:"idle"));
    reply.set("fraction",Json::number(pendingBake_?0.0:double(progress.fraction),4));
    reply.set("message",Json::string(bakeEpoch_==epoch?bakeMessage_:""));
    return reply;
}

void StudioHost::tick() {
    publishBuildingPanelFrame();
    if (projectLoading_) return; // the background thread owns the session right now
    tickLightBake();
    if(!launchedPlayers_.empty() && platform_.now()>=nextPlayerCheck_){
        nextPlayerCheck_=platform_.now()+0.5;
        std::erase_if(launchedPlayers_,[&](const LaunchedPlayer& player){
            const auto status=player.exitStatus();
            if(!status)return false;
            if(*status && session_->workspace().project.dir==player.project){
                if(player.mode=="standalone"){
                    const auto message="AiverseGame exited with code "+std::to_string(*status);
                    session_->workspace().log("play",message,true);
                    sendEvent("standalonePlay.error",message);
                    return true;
                }
                const std::string name=player.mode=="listen"?"Host":"Client";
                const auto message=name+" exited with code "+std::to_string(*status)+
                    (player.mode=="listen"?". Check that ports 27100 and 27101 are available.":". Check the server address and project.");
                session_->workspace().log("multiplayer",message,true);
                sendEvent("networkPlay.error",message);
            }
            return true;
        });
    }
    pollScriptChanges();
    tickGame();
    tickTextureGraph(session_->workspace().project.dir); // independent of Scene visibility
    // An empty Workspace owns an internal default MapDoc, NOT a loaded project.
    // Never render that provisional map as if it belonged to the user.
    if (session_->workspace().project.dir.empty()) return;
    // Agent panel: tool calls from aiversemcp run here, on the main thread, between frames.
    if (session_->pollAgentLink()) {
        sendEvent("assets.changed", "");
        sendEvent("document.changed", "");
        sendEvent("agent.changed", "");
    }
    // Extension gizmos (editor/extensions.lua scene_gizmos) a few times a second.
    if (preview_ && platform_.now() >= nextGizmoPoll_) {
        nextGizmoPoll_ = platform_.now() + 0.2;
        std::vector<StudioScene::OverlayShape> shapes;
        try {
            const Json gizmos = session_->extensionGizmos(); // a temporary would die before the loop (C++20)
            for (const auto& g : gizmos.items()) {
                if (!g.isObject() || shapes.size() >= 256) continue;
                const std::string kind = g.getStr("kind");
                StudioScene::OverlayShape sh;
                sh.kind = kind == "line" ? 1 : kind == "circle" ? 2 : kind == "point" ? 3 : kind == "label" ? 4 : 0;
                if (sh.kind == 4) { sh.text = g.getStr("text"); if (sh.text.empty() || sh.text.size() > 200) continue; }
                if (!sh.kind) continue;
                auto f = [&](const char* k, double d) { const double v = g[k].toDouble(d); return float(std::isfinite(v) ? v : d); };
                sh.a = {f("x", 0), f("y", 0), f("z", 0)};
                sh.b = {f("x2", 0), f("y2", 0), f("z2", 0)};
                sh.radius = std::clamp(f("radius", sh.kind == 3 ? 4 : 12), 1.f, 120.f);
                sh.thickness = std::clamp(f("thickness", 2), 1.f, 10.f);
                auto c = [&](const char* k, double d) { return uint8_t(std::clamp(f(k, float(d)), 0.f, 1.f) * 255); };
                sh.r = c("r", 0.35); sh.g = c("g", 0.7); sh.bl = c("b", 1.0);
                shapes.push_back(sh);
            }
        } catch (...) {}
        extShapes_ = std::move(shapes);
        if (!session_->splineEdit().empty() && splineDrag_.empty()) splineCache_ = session_->splineHandles();
        else if (session_->splineEdit().empty()) splineCache_ = Json();
        rebuildOverlay();
    }
    tickAgentScreenshot();
    pollAgentRequests();
    finishLensCapture();
    if(gpuEmitterSurface_.renderer && gpuEmitterProject_!=session_->workspace().project.dir)
        surfaceDetached(StudioSurface::GpuEmitter);
    if(gpuEmitterSurface_.renderer && gpuEmitterSurface_.placement.visible && platform_.surfaceDrawable(StudioSurface::GpuEmitter)){
        try { onScene([&]{platform_.sceneThreadScope([&]{
            const auto& rect=gpuEmitterSurface_.placement;
            gpuEmitterSurface_.renderer->drawGpuEmitter(rect.pixelsW,rect.pixelsH);
        });}); } catch(const std::exception& e){sendEvent("gpuEmitter.previewError",e.what());surfaceDetached(StudioSurface::GpuEmitter);}
    }
    if(effectFileSurface_.renderer && effectFileProject_!=session_->workspace().project.dir)
        surfaceDetached(StudioSurface::EffectFile);
    if(effectFileSurface_.renderer && effectFileSurface_.placement.visible && platform_.surfaceDrawable(StudioSurface::EffectFile)){
        try { onScene([&]{platform_.sceneThreadScope([&]{
            const auto& rect=effectFileSurface_.placement;
            effectFileSurface_.renderer->drawEffectFile(rect.pixelsW,rect.pixelsH);
        });}); } catch(const std::exception& e){sendEvent("effectFile.previewError",e.what());surfaceDetached(StudioSurface::EffectFile);}
    }
    if(weatherLights_ && lightingSurface_.placement.visible && platform_.surfaceDrawable(StudioSurface::Lighting)) {
        if(session_->graphDocumentScope().getStr("epoch")!=weatherEpoch_) {
            weatherLights_.reset();
            if(lightingSurface_.renderer)onScene([&]{lightingSurface_.renderer.reset();});
        } else try {
            if(!renderThread_){
                const char* threaded=std::getenv("AIVERSE_STUDIO_RENDER_THREAD");
                renderThread_=std::make_unique<StudioRenderThread>([this]{renderSceneFrame();},!(threaded&&std::string(threaded)=="0"));
            }
            const double seconds=platform_.now()-weatherStart_;
            onScene([&]{platform_.sceneThreadScope([&]{
                auto& asset=lightingSurface_;
                if(!asset.renderer)asset.renderer=std::make_unique<StudioScene>(platform_.createDevice(StudioSurface::Lighting));
                render::Lighting environment;
                weatherLights_->updateSky(environment,seconds);
                weatherDay_.night=weatherLights_->night();
                asset.renderer->setWeatherPreview(environment,weatherDay_,float(seconds));
                const auto& ws=session_->workspace();
                asset.renderer->frame(asset.placement.pixelsW,asset.placement.pixelsH,ws.map,ws.map.data.worldSpace,
                    "","",Json{},0,float(ws.project.rules.levelHeight.toDouble()),ws.project.dir,ws.project.rulesJson);
            });});
        } catch(const std::exception& e) {
            weatherLights_.reset();
            sendEvent("lighting.previewError",e.what());
            surfaceDetached(StudioSurface::Lighting);
        }
    }
    if(lensSurface_.renderer && lensProject_!=session_->workspace().project.dir) surfaceDetached(StudioSurface::LensCamera);
    if(lensSurface_.renderer && lensSurface_.placement.visible && platform_.surfaceDrawable(StudioSurface::LensCamera)) {
        try {
            onScene([&] { platform_.sceneThreadScope([&] {
                if(!lensSurface_.renderer->lensCameraReady())return;
                const auto& ws=session_->workspace();
                lensSurface_.renderer->frame(lensSurface_.placement.pixelsW,lensSurface_.placement.pixelsH,
                    ws.map,ws.map.data.worldSpace,"","",Json{},0,float(ws.project.rules.levelHeight.toDouble()),
                    ws.project.dir,ws.project.rulesJson);
            }); });
        } catch(const std::exception& e) {
            sendEvent("lens.previewError",e.what());
            surfaceDetached(StudioSurface::LensCamera);
        }
    }
    for (const auto surface : {StudioSurface::Attachments, StudioSurface::SourceModel, StudioSurface::ImportedClip}) {
        auto& asset = assetSurface(surface);
        const bool source = surface == StudioSurface::SourceModel;
        const bool clip = surface == StudioSurface::ImportedClip;
        const auto& project = clip ? importedClipProject_ : source ? sourceModelProject_ : attachmentProject_;
        if (asset.renderer && project != session_->workspace().project.dir) surfaceDetached(surface);
        if (!asset.renderer || !asset.placement.visible || !platform_.surfaceDrawable(surface)) continue;
        try {
            onScene([&] { platform_.sceneThreadScope([&] {
                if(source)asset.renderer->drawSourceModel(asset.placement.pixelsW,asset.placement.pixelsH);
                else asset.renderer->drawAttachments(asset.placement.pixelsW,asset.placement.pixelsH);
            }); });
        } catch(const std::exception& e) {
            sendEvent(clip ? "importedClip.previewError" : source ? "sourceModel.previewError" : "attachments.previewError",e.what());
            surfaceDetached(surface);
        }
    }
    tickAnimationPreview(StudioSurface::AnimationGraph);
    tickAnimationPreview(StudioSurface::AnimationRig);
    tickCharacterPreview(StudioSurface::FacialRig);
    tickCharacterPreview(StudioSurface::Character);
    tickCharacterPreview(StudioSurface::Appearance);
    if(!facialRigSurface_.placement.visible)session_->rigPreviewBinding().setGpuStatus(false,"Facial Rig preview hidden");
    if(!activeHumanRenderer())session_->setFacialPreviewGpuStatus(false,"Character preview hidden");
    tickAssetPreview(StudioSurface::Material);
    tickAssetPreview(StudioSurface::Shader);
    tickFx(); // dedicated native FX drawable continues even if Scene is hidden
    if (!placement_.visible || !platform_.surfaceDrawable(StudioSurface::Scene)) {
        return;
    }
    try {
        ensureScene();
        tickScene();
    } catch (const std::exception& e) {
        failScene(e.what());
    }
}
// UI thread: snapshot what the map frame reads (document, rules, editor state), hand it to the
// render thread and publish the last finished frame's results.
void StudioHost::tickScene() {
    const auto& ws = session_->workspace();
    const double now = platform_.now();
    const auto& map = ws.map;
    // Copy only on change (and once a second for edits that did not bump the revision).
    if (!mapSnapshot_ || mapSnapshotSource_ != &map || mapSnapshot_->revision != map.revision ||
        mapSnapshot_->path != map.path || now >= nextMapSnapshot_) {
        auto copy = std::make_shared<editor::MapDoc>();
        copy->data = map.data; copy->path = map.path; copy->revision = map.revision;
        mapSnapshot_ = std::move(copy); mapSnapshotSource_ = &map; nextMapSnapshot_ = now + 1.0;
    }
    if (!rulesSnapshot_ || rulesSnapshotProject_ != ws.project.dir || now >= nextRulesCheck_) {
        const size_t hash = std::hash<std::string>{}(ws.project.rulesJson.dump(-1));
        const bool rulesChanged=!rulesSnapshot_ || rulesSnapshotProject_ != ws.project.dir || hash != rulesSnapshotHash_;
        const auto& manifestTerrain=ws.project.manifest["terrain"];
        if(rulesChanged || manifestTerrain!=planetManifestTerrain_){
            auto look=std::make_shared<game::TerrainLook>();
            game::applyTerrainLook(*look,ws.project.rulesJson["terrain"]);
            game::applyTerrainLook(*look,manifestTerrain);
            planetLookSnapshot_=std::move(look);planetManifestTerrain_=manifestTerrain;
        }
        if (rulesChanged) {
            rulesSnapshot_ = std::make_shared<const Json>(ws.project.rulesJson);
            rulesSnapshotHash_ = hash; rulesSnapshotProject_ = ws.project.dir;
        }
        nextRulesCheck_ = now + 1.0;
    }
    FrameInputs in;
    in.width = placement_.pixelsW; in.height = placement_.pixelsH;
    in.map = mapSnapshot_; in.rules = rulesSnapshot_;in.planetLook=planetLookSnapshot_;
    const auto& brush=session_->sceneBrush();
    in.brushArmed=brush.armed;
    in.editBaseTerrain=brush.editsBaseGrid() && !map.data.worldSpace;
    if(brush.armed && brush.kind=="region")in.regionBorderLetter=brush.regionLetter.front();
    else if(brush.regionBorders)in.regionBorderLetter='.';
    in.fps = ws.map.data.worldSpace;
    in.geometryFile=editor::configuredGeometryScene(ws);
    in.selected = session_->selectedObjectId();
    in.levelHeight = float(ws.project.rules.levelHeight.toDouble());
    in.project = ws.project.dir;
    const auto ids = session_->selectedObjectIds();
    in.selection = std::set<std::string>(ids.begin(), ids.end());
    in.hidden = session_->hiddenIds(); in.locked = session_->lockedIds(); in.hover = hoverId_;
    in.wind = session_->windPreview();
    {
        std::lock_guard<std::mutex> io(renderIoMutex_);
        renderInputs_ = std::move(in);
    }
    renderThread_->kick();
    FrameOutputs out;
    {
        std::lock_guard<std::mutex> io(renderIoMutex_);
        if (!renderOutputsFresh_) return;
        out = renderOutputs_; renderOutputsFresh_ = false;
    }
    if (!out.error.empty()) { failScene(out.error); return; }
    publishSceneFrame(out);
}
// Render thread: one map frame from the latest published inputs.
void StudioHost::renderSceneFrame() {
    FrameInputs in;
    {
        std::lock_guard<std::mutex> io(renderIoMutex_);
        in = renderInputs_;
    }
    if (!in.map || !in.rules || !preview_) return;
    FrameOutputs out;
    try {
        platform_.sceneThreadScope([&] {
            preview_->setEditorState(std::move(in.selection), std::move(in.hidden), std::move(in.locked), std::move(in.hover));
            if(!in.brushArmed)preview_->setBrushCursor(false,{},0);
            preview_->syncStaticGeometry(in.project,in.geometryFile);
            preview_->setPlanetTerrainLook(in.planetLook);
            preview_->setBaseTerrainView(in.editBaseTerrain);
            preview_->setRegionBorders(in.regionBorderLetter);
            preview_->setWindPreview(in.wind);
            preview_->frame(in.width, in.height, *in.map, in.fps, in.selected, "", Json::object(), 0,
                            in.levelHeight, in.project, *in.rules, nullptr);
            out.lightingStatus = preview_->lightingStatus();
            out.load = preview_->modelLoadProgress();
            out.camera = preview_->cameraState();
            out.thumbnails = preview_->takeFinishedThumbnails();
            out.drawCalls = preview_->drawCalls();
        });
    } catch (const std::exception& e) {
        out.error = e.what();
        if (out.error.empty()) out.error = "Scene render failed";
    }
    std::lock_guard<std::mutex> io(renderIoMutex_);
    renderOutputs_ = std::move(out);
    renderOutputsFresh_ = true;
}
// Events of a finished Scene frame (UI thread): Stats history, GPU and lighting status,
// model streaming progress, viewport.ready and the HTML gizmo camera.
void StudioHost::publishSceneFrame(const FrameOutputs& out) {
    {
        // Stats panel: smoothed time between finished Scene frames.
        const double now = platform_.now();
        if (lastSceneFrame_ > 0) {
            const double ms = (now - lastSceneFrame_) * 1000.0;
            sceneFrameMs_ = sceneFrameMs_ <= 0 ? ms : sceneFrameMs_ * 0.95 + ms * 0.05;
            frameHistory_.emplace_back(float(ms), out.drawCalls);
            if (frameHistory_.size() > 240) frameHistory_.pop_front();
        }
        lastSceneFrame_ = now;
        session_->setSceneFrameStats(sceneFrameMs_, ++sceneFrameCount_);
    }
    for (const auto& asset : out.thumbnails) {
        Json payload = Json::object();
        payload.set("path", Json::string(asset));
        sendEventPayload("asset.thumbnail", payload);
    }
    if ((lightingStatusTicks_++ % 60) == 0) sendEvent("lighting.status", out.lightingStatus);
    {
        // Scene model streaming progress (status bar / splash).
        const auto& load = out.load;
        if (load.total > 0 && load.done < load.total) {
            if (load.done != lastLoadDone_ || load.total != lastLoadTotal_) {
                lastLoadDone_ = load.done; lastLoadTotal_ = load.total; loadingShown_ = true;
                sendLoading("models", load.current, load.done, load.total);
            }
        } else if (loadingShown_) {
            loadingShown_ = false; lastLoadDone_ = lastLoadTotal_ = -1;
            sendLoading("done", "", load.total, load.total);
        }
    }
    if (!readyEmitted_) {
        sendEvent("viewport.ready", "Native Renderer3D");
        readyEmitted_ = true;
        cameraSent_ = false;
    }
    publishCamera(out.camera);
}
void StudioHost::failScene(const std::string& message) {
    platform_.log("AIVERSE native Scene failed: " + message);
    sendEvent("viewport.error", message);
    readyEmitted_ = false;
    placement_.visible = false;
    if(!projectLoading_&&session_->strokeCancel())sendEvent("document.changed", "");mouse_={};
    {
        ViewportRect hidden;
        NativeViewportPlacement none;
        (void)platform_.placeSurface(StudioSurface::Scene, hidden, none);
    }
    {
        std::lock_guard<std::mutex> io(renderIoMutex_);
        renderOutputsFresh_ = false;
    }
    textureGraphWork_=false;textureGraphScope_.clear();textureGraphError_.clear();
    try { onScene([&] { preview_.reset(); }); }
    catch (const std::exception& e) { platform_.log(std::string("AIVERSE native Scene release failed: ") + e.what()); }
}
void StudioHost::publishCamera(const StudioScene::CameraState& camera) {
    if (!htmlComposite_) return;
    if (cameraSent_ && std::abs(camera.yaw - lastCameraYaw_) < 1e-4 && std::abs(camera.pitch - lastCameraPitch_) < 1e-4 &&
        std::abs(camera.distance - lastCameraDistance_) <= 1e-3 * std::max(1.0, lastCameraDistance_) &&
        lastCameraOrtho_ == camera.ortho) return;
    lastCameraYaw_ = camera.yaw; lastCameraPitch_ = camera.pitch;
    lastCameraDistance_ = camera.distance; lastCameraOrtho_ = camera.ortho; cameraSent_ = true;
    Json payload = Json::object();
    payload.set("yaw", Json::number(double(camera.yaw), 5));
    payload.set("pitch", Json::number(double(camera.pitch), 5));
    payload.set("distance", Json::number(double(camera.distance), 4));
    payload.set("ortho", Json::fromBool(camera.ortho));
    sendEventPayload("viewport.camera", payload);
}

// ---------------------------------------------------------------- Scene operations
void StudioHost::orbit(double dx, double dy) { if (preview_ && finite({dx, dy})) onScene([&] { preview_->orbit(float(dx), float(dy)); }); }
void StudioHost::zoom(double amount) { if (preview_ && std::isfinite(amount)) onScene([&] { preview_->zoom(float(amount)); }); }
void StudioHost::look(double dx, double dy) { if (preview_ && finite({dx, dy})) onScene([&] { preview_->look(float(dx), float(dy)); }); }
void StudioHost::pan(double dx, double dy) { if (preview_ && finite({dx, dy})) onScene([&] { preview_->pan(float(dx), float(dy)); }); }
void StudioHost::moveCamera(double right, double up, double forward) {
    if (preview_ && finite({right, up, forward})) onScene([&] { preview_->move(float(right), float(up), float(forward)); });
}
void StudioHost::focusSelection() {
    if (projectLoading_ || !preview_) return;
    onScene([&] { preview_->focusSelected(session_->workspace().map, session_->workspace().map.data.worldSpace, session_->selectedObjectId()); });
}
void StudioHost::resetView() { if (preview_) onScene([&] { preview_->resetView(); }); }
void StudioHost::frameAll() {
    if (projectLoading_ || !preview_) return;
    onScene([&] { preview_->frameAll(session_->workspace().map, session_->workspace().map.data.worldSpace); });
}
void StudioHost::setPivotSnap(bool on) { if (preview_) onScene([&] { preview_->setPivotSnap(on); }); }
void StudioHost::setTool(int tool) {
    if (!preview_ || tool < 0 || tool > 3) return;
    onScene([&] { preview_->setTransformTool(StudioScene::TransformTool(tool)); });
    static const char* names[4] = {"view", "move", "rotate", "scale"};
    sendEvent("viewport.tool", names[tool]);
}
int StudioHost::tool() { return preview_ ? int(onScene([&] { return preview_->transformTool(); })) : 1; }
bool StudioHost::gizmoAt(double x, double y, double w, double h) {
    if (!preview_) return false;
    const int hit = onScene([&] { return preview_->orientationGizmoHit(float(x), float(y), float(w), float(h)); });
    if (hit < 0) return false;
    onScene([&] { preview_->activateOrientationGizmo(hit); });
    return true;
}
bool StudioHost::moveBegin(double x, double y, double w, double h) {
    if (projectLoading_ || !scenePickerToken_.empty()) return false;
    return preview_ && onScene([&] { return preview_->beginTransformDrag(float(x), float(y), float(w), float(h)); });
}
void StudioHost::moveUpdate(double x, double y, double w, double h, bool invertSnap) {
    if (!preview_) return;
    const std::string caption = onScene([&] {
        preview_->updateTransformDrag(float(x), float(y), float(w), float(h), invertSnap);
        return preview_->transformCaption();
    });
    if (caption != lastActionCaption_) { lastActionCaption_ = caption; sendEvent("viewport.action", caption); }
}
void StudioHost::moveEnd() {
    if (projectLoading_) { if (preview_) onScene([&] { preview_->clearTransformPreview(); }); return; }
    if (!preview_) return;
    const auto commit = onScene([&] { return preview_->endTransformDrag(); });
    lastActionCaption_.clear();
    sendEvent("viewport.action", "");
    if(commit&&commit->expectedRevision&&
       (*commit->expectedRevision!=session_->workspace().map.revision||
        session_->lockedIds().contains(commit->id)||session_->hiddenIds().contains(commit->id))){
        onScene([&]{preview_->clearTransformPreview();});return;
    }
    if(commit&&commit->surfacePlacement.isObject()){
        Json params=Json::object();params.set("entityId",Json::string(commit->id));
        params.set("componentId",Json::string("SurfacePlacement"));
        params.set("key",Json::string("placement"));params.set("value",commit->surfacePlacement);
        if(commit->expectedRevision)params.set("expectedRevision",Json::number(int64_t(*commit->expectedRevision)));
        const auto outcome=request("studio.setProperty",params);
        if(outcome.has("error"))platform_.log("AIVERSE planet move: "+outcome.getStr("error"));
        else sendEvent("document.changed","");
        return;
    }
    using Tool = StudioScene::TransformTool;
    const bool unchanged = !commit ||
        (commit->tool == Tool::Move && std::abs(commit->move.x) + std::abs(commit->move.y) + std::abs(commit->move.z) < 1e-4f) ||
        (commit->tool == Tool::Rotate && (!std::isfinite(commit->degrees) || std::abs(commit->degrees) < 1e-3f)) ||
        (commit->tool == Tool::Scale && (!std::isfinite(commit->factor) || std::abs(commit->factor - 1.0f) < 1e-4f));
    if (unchanged) { onScene([&] { preview_->clearTransformPreview(); }); return; }
    // Read current Transform values through the same inspector API the UI uses, then
    // write ONE undoable studio.setProperties (y promotes legacy units; lights use height).
    Json params = Json::object();
    params.set("entityId", Json::string(commit->id));
    const auto inspected = request("studio.inspect", params);
    Json colliderScope;
    std::map<std::string, double> current;
    for (const auto& component : inspected["result"]["components"].items()) {
        const bool box=component.getStr("id")=="OrientedBoxCollider" && commit->tool!=Tool::Move;
        if(box)colliderScope=component["scope"];
        if (!box && component.getStr("id") != "Transform") continue;
        for (const auto& field : component["properties"].items())
            if (field.getBool("editable", true) && field["value"].isNumber()) current[field.getStr("key")] = field["value"].toDouble();
    }
    Json values = Json::object();
    auto add = [&](std::initializer_list<const char*> keys, auto apply, int digits) {
        for (const char* key : keys) if (current.count(key)) { values.set(key, Json::number(apply(current[key]), digits)); return; }
    };
    if (commit->tool == Tool::Move) {
        const float d[3] = {commit->move.x, commit->move.y, commit->move.z};
        if (std::abs(d[0]) > 1e-5f) add({"x"}, [&](double v) { return v + d[0]; }, 4);
        if (std::abs(d[1]) > 1e-5f) add({"y", "height"}, [&](double v) { return v + d[1]; }, 4);
        if (std::abs(d[2]) > 1e-5f) add({"z"}, [&](double v) { return v + d[2]; }, 4);
        // Surface snap: the moved object rests on the terrain at its new spot.
        if (surfaceSnap_ && commit->id.rfind("entity:", 0) == 0 && current.count("x") && current.count("z") && current.count("y")) {
            const auto& ws = session_->workspace();
            const float nx = float(current["x"] + d[0]), nz = float(current["z"] + d[2]);
            values.set("y", Json::number(double(terrainPreviewHeight(ws.map.data, nx, nz, float(ws.project.rules.levelHeight.toDouble()))), 4));
        }
    } else if (commit->tool == Tool::Rotate && commit->axis >= 0 && commit->axis < 3) {
        static const char* keys[3] = {"rotationX", "rotationY", "rotationZ"};
        add({keys[commit->axis]}, [&](double v) { return std::remainder(v + double(commit->degrees), 360.0); }, 3);
    } else if (commit->tool == Tool::Scale) {
        static const char* keys[3] = {"scaleX", "scaleY", "scaleZ"};
        for (int a = 0; a < 3; ++a) if (commit->axis == 3 || commit->axis == a || (commit->axis >= 4 && commit->axis - 4 != a))
            add({keys[a]}, [&](double v) { return std::clamp(v * double(commit->factor), 0.001, 10000.0); }, 4);
    }
    if (!values.members().empty()) {
        Json set = Json::object();
        set.set("entityId", Json::string(commit->id));
        set.set("componentId", Json::string(colliderScope.isObject()?"OrientedBoxCollider":"Transform"));
        if(colliderScope.isObject())set.set("scope",colliderScope);
        set.set("values", values);
        if(commit->expectedRevision)set.set("expectedRevision",Json::number(int64_t(*commit->expectedRevision)));
        const auto outcome = request("studio.setProperties", set);
        if (outcome.has("error")) platform_.log("AIVERSE transform: " + outcome.getStr("error"));
    }
    onScene([&] { preview_->clearTransformPreview(); });
    sendEvent("document.changed", "");
}
void StudioHost::marquee(double ax, double ay, double bx, double by, double w, double h) {
    if (!preview_ || w < 2 || h < 2) return;
    onScene([&] { preview_->setMarquee(true, float(ax / w), float(ay / h), float(bx / w), float(by / h)); });
}
void StudioHost::marqueeEnd(double ax, double ay, double bx, double by, double w, double h, const std::string& mode) {
    if (projectLoading_) { if (preview_) onScene([&] { preview_->setMarquee(false); }); return; }
    if (!preview_) return;
    const auto ids = onScene([&] {
        preview_->setMarquee(false);
        return preview_->idsInRect(float(ax), float(ay), float(bx), float(by), float(w), float(h),
                                   session_->workspace().map, session_->workspace().map.data.worldSpace);
    });
    Json params = Json::object(), list = Json::array();
    for (const auto& id : ids) list.push(Json::string(id));
    params.set("ids", list);
    params.set("mode", Json::string(mode));
    if (!request("studio.select", params).has("error")) sendEvent("selection.changed", "");
}
void StudioHost::hover(double x, double y, double w, double h) {
    if (projectLoading_ || !preview_ || onScene([&] { return preview_->transformDragActive(); })) return;
    if (updateBrushCursor(x, y, w, h)) return;
    const std::string id = x < 0 || y < 0 ? std::string() : onScene([&] {
        return preview_->pick(float(x), float(y), float(w), float(h), session_->workspace().map, session_->workspace().map.data.worldSpace); });
    if (id == hoverId_) return;
    hoverId_ = id;
    sendEvent("viewport.hover", id);
}
void StudioHost::selectionAction(const std::string& method) {
    if (projectLoading_) return;
    Json params = Json::object(), list = Json::array();
    for (const auto& id : session_->selectedObjectIds()) list.push(Json::string(id));
    if (list.size() == 0) return;
    params.set("entityIds", list);
    const auto outcome = request(method.c_str(), params);
    if (outcome.has("error")) { platform_.log("AIVERSE " + method + ": " + outcome.getStr("error")); return; }
    sendEvent("document.changed", "");
    sendEvent("selection.changed", "");
}
// Ring under the cursor while the Scene brush is armed (radius; river: half its width; road: a dot).
bool StudioHost::resolveBrushPoint(double x,double y,double w,double h,render::float3& at,std::string* error,std::string* planetBody,double* elevation) {
    if(!preview_||x<0||y<0||w<2||h<2||x>=w||y>=h)return false;
    const auto& brush=session_->sceneBrush();
    const auto& map=session_->workspace().map;
    if(map.data.planet() && (brush.kind=="terrain" || brush.kind=="paint")){
        std::string body;double height=0;
        if(!onScene([&]{return preview_->baseTerrainView()==brush.editsBaseGrid() && preview_->planetBrushPoint(x,y,w,h,map,at,body,height);}))return false;
        if(planetBody)*planetBody=body;
        if(elevation)*elevation=height;
        return true;
    }
    if((brush.editsBaseGrid() || (brush.kind=="terrain" && (brush.op=="stamp" || brush.op=="set"))) && !map.data.worldSpace && !map.data.planet()){
        const auto hit=onScene([&]() -> std::optional<render::float3> {
            if(preview_->baseTerrainView()!=brush.editsBaseGrid())return {};
            return preview_->flatMapPoint(float(x),float(y),float(w),float(h),map,brush.pickHeight);
        });
        if(!hit)return false;
        at=*hit;if(elevation)*elevation=at.y;return true;
    }
    if(session_->sceneBrush().kind!="caverns")
        return onScene([&]{return preview_->groundPoint(float(x),float(y),float(w),float(h),at);});
    render::float3 origin{},direction{};
    if(!onScene([&]{return preview_->caveScreenRay(float(x),float(y),float(w),float(h),origin,direction);}))return false;
    double px=0,py=0,pz=0;
    if(!session_->caveBrushPoint(origin.x,origin.y,origin.z,direction.x,direction.y,direction.z,px,py,pz,error))return false;
    at={float(px),float(py),float(pz)};return true;
}
bool StudioHost::updateBrushCursor(double x, double y, double w, double h) {
    if (!preview_) return false;
    const auto& b = session_->sceneBrush();
    render::float3 at{};
    if (!b.armed || !resolveBrushPoint(x,y,w,h,at)) {
        onScene([&] { preview_->setBrushCursor(false, {}, 0); });
        return b.armed;
    }
    const float r = b.kind == "road" ? 0.6f : b.kind == "river" ? float(b.width * 0.5) : float(b.radius);
    onScene([&] { preview_->setBrushCursor(true, at, r,b.kind=="caverns",b.caveMode=="fill"); });
    return true;
}
void StudioHost::brushAdjust(double factor, bool disarm) {
    if(projectLoading_)return;
    if(disarm&&session_->strokeCancel()){mouse_={};sendEvent("document.changed", "");}
    const auto& b = session_->sceneBrush();
    Json params = Json::object();
    if (disarm) params.set("armed", Json::fromBool(false));
    else params.set("radius", Json::number(std::clamp(b.radius * factor, 0.25, 64.0), 2));
    const auto reply = request("studio.sceneBrush", params);
    if (reply.has("result")) sendEventPayload("brush.changed", reply["result"]);
    if (disarm && preview_) onScene([&] { preview_->setBrushCursor(false, {}, 0); });
}
bool StudioHost::brushArmed() const { return !projectLoading_ && preview_ && session_->sceneBrush().armed; }
// Scene overlay: extension gizmos + spline handles (Roads / Water "Edit nodes").
std::string StudioHost::worldProcessSceneBody(){
    if(projectLoading_||!session_||!preview_)return {};
    const auto& ws=session_->workspace();
    if(!ws.map.data.planet())return {};
    return onScene([&]{return preview_->planetSceneBody(ws.project.dir,ws.map.path,ws.map.revision);});
}
std::optional<StudioForestCameraFocus> StudioHost::worldProcessForestCamera(){
    if(projectLoading_||!session_||!preview_||!placement_.visible)return {};
    const auto& ws=session_->workspace();
    if(!ws.map.data.planet())return {};
    return onScene([&]{return preview_->planetForestCamera(ws.project.dir,ws.map.path,ws.map.revision,placement_.pixelsW,placement_.pixelsH);});
}
Json StudioHost::sampleWorldProcessLive(){
    if(!game_||!wpLiveOptions_.live||(game_->planetMatch()&&!worldProcessPlanetMapCurrent())){wpForestRenderSnapshot_=Json{};return {};}
    if(!wpLiveOptions_.forest){wpForestRenderSnapshot_=Json{};return game_->ecologyState(wpLiveOptions_);}
    const auto camera=worldProcessForestCamera();
    const auto body=camera?camera->body:worldProcessSceneBody();
    return game_->ecologyState(wpLiveOptions_,body,camera,&wpForestRenderSnapshot_);
}
Json StudioHost::planetSceneBodyRequest(const Json& params){
        if(projectLoading_||!session_||!preview_)throw std::runtime_error("Scene unavailable");
        const auto& ws=session_->workspace();
        if(!ws.map.data.planet())throw std::runtime_error("Planetary map required");
        if(params.has("body")){
            if(!params["body"].isString()||params.getStr("mapPath")!=ws.map.path||params.getStr("expectedRevision")!=std::to_string(ws.map.revision))
                throw std::runtime_error("Reload the current planetary map");
            if(session_->strokeActive()||brushArmed()||wpDrag_.active||mouse_.active||onScene([&]{return preview_->transformDragActive();}))throw std::runtime_error("Finish the active Scene gesture first");
            const auto body=params.getStr("body");const int bi=ws.map.data.findBody(body);
            if(!body.empty()&&(bi<0||!ws.map.data.bodies[size_t(bi)].surface()))throw std::runtime_error("Unknown surface body");
            const bool armed=forestPlantArm_.isObject();forestPlantArm_=Json{};
            onScene([&]{preview_->setPlanetSceneBody(ws.project.dir,ws.map.path,body);});
            if(armed){Json payload=Json::object();payload.set("cancelled",Json::fromBool(true));sendEventPayload("planetForest.planted",payload);}
            wpLiveSnapshot_=Json{};wpLiveRefresh_=0;rebuildOverlay();
            Json event=Json::object();event.set("override",Json::string(body));event.set("mapPath",Json::string(ws.map.path));event.set("revision",Json::string(std::to_string(ws.map.revision)));
            sendEventPayload("planetScene.bodyChanged",event);
        }
        Json out=Json::object();out.set("override",Json::string(onScene([&]{return preview_->planetSceneBodyOverride(ws.project.dir,ws.map.path);})));out.set("activeBody",Json::string(worldProcessSceneBody()));
        return out;
}
Json StudioHost::planetForestLiveRequest(){
        if(game_&&worldProcessPlanetMapCurrent()){
            const auto body=worldProcessSceneBody();
            if(wpLiveOptions_.live&&wpLiveOptions_.forest&&game_->planetSnapshotCurrent(wpLiveSnapshot_)&&
               wpLiveSnapshot_["forest"].getBool("active")&&wpLiveSnapshot_["forest"].getStr("focusBody")==body)
                return wpLiveSnapshot_["forest"];
            const auto camera=worldProcessForestCamera();
            return game_->planetForestState(camera?camera->body:body,camera);
        }
        Json out=Json::object();out.set("active",Json::fromBool(false));return out;
}
Json StudioHost::planetViewAgentRequest(const std::string& name,const Json& params){
    if(projectLoading_||!session_||!preview_)throw std::runtime_error("Native Scene unavailable");
    const auto& ws=session_->workspace();
    if(!ws.map.data.planet()||!params["expectedProject"].isString()||params.getStr("expectedProject")!=ws.project.dir||
       !params["mapPath"].isString()||params.getStr("mapPath")!=ws.map.path||
       !params["expectedRevision"].isString()||params.getStr("expectedRevision")!=std::to_string(ws.map.revision))
        throw std::runtime_error("Planetary project/map changed; read current identity before retry");
    if(name=="studio_planet_forest_live"){
        if(!worldProcessPlanetMapCurrent())throw std::runtime_error("Start local Play on the current saved map");
        const auto out=planetForestLiveRequest();
        if(!out.getBool("active"))throw std::runtime_error("Completed local forest snapshot unavailable");
        return out;
    }
    if(name=="studio_planet_scene_body_set"){
        if(!params["body"].isString())throw std::runtime_error("Body string required");
        return planetSceneBodyRequest(params);
    }
    if(name!="studio_planet_scene_get"&&name!="studio_planet_scene_camera_set")throw std::runtime_error("Unknown planetary view operation");
    if(name=="studio_planet_scene_camera_set"){
        if(session_->strokeActive()||brushArmed()||wpDrag_.active||mouse_.active||forestPlantArm_.isObject())
            throw std::runtime_error("Finish the active Scene gesture first");
        const auto body=worldProcessSceneBody();
        if(body.empty()||params.getStr("body")!=body)throw std::runtime_error("Wait for the requested rendered body");
        if(!params["target"].isArray()||params["target"].size()!=3||params["ortho"].type()!=Json::Type::Bool)
            throw std::runtime_error("Camera target and orthographic flag required");
        const auto bounded=[](const Json& j,double lo,double hi){
            if(!j.isNumber())throw std::runtime_error("Numeric camera field required");
            const double v=j.toDouble();
            if(!std::isfinite(v)||v<lo||v>hi)throw std::runtime_error("Camera field out of range");
            return float(v);
        };
        StudioScene::CameraState camera;
        camera.target={bounded(params["target"][0],-8192,8192),bounded(params["target"][1],-8192,8192),bounded(params["target"][2],-8192,8192)};
        camera.distance=bounded(params["distance"],2,8192);
        camera.yaw=std::remainder(bounded(params["yaw"],-1000,1000),6.28318530718f);
        camera.pitch=bounded(params["pitch"],-1.5707963,1.5707963);camera.ortho=params.getBool("ortho");
        onScene([&]{
            if(preview_->planetSceneBody(ws.project.dir,ws.map.path,ws.map.revision)!=body||preview_->transformDragActive())
                throw std::runtime_error("Scene changed or transform gesture active");
            preview_->setPlanetOrbitCamera(camera);
        });
        wpLiveRefresh_=0;
        cameraSent_=false;publishCamera(camera);
    }
    Json out=planetSceneBodyRequest(Json::object());
    out.set("project",Json::string(ws.project.dir));out.set("mapPath",Json::string(ws.map.path));
    out.set("revision",Json::string(std::to_string(ws.map.revision)));
    if(!out.getStr("activeBody").empty()){
        const auto camera=onScene([&]{return preview_->cameraState();});
        Json view=Json::object(),target=Json::array();
        for(float value:{camera.target.x,camera.target.y,camera.target.z})target.push(Json::number(double(value),4));
        view.set("target",std::move(target));view.set("distance",Json::number(double(camera.distance),4));
        view.set("yaw",Json::number(double(camera.yaw),6));view.set("pitch",Json::number(double(camera.pitch),6));
        view.set("ortho",Json::fromBool(camera.ortho));out.set("camera",std::move(view));
    }
    return out;
}
bool StudioHost::worldProcessPlanetMapCurrent() const {
    if(projectLoading_||!session_||!game_||!game_->launchedMapCurrent()||wpLivePlanetMapPath_.empty())return false;
    const auto& ws=session_->workspace();
    return ws.project.dir==wpLivePlanetProject_&&ws.map.path==wpLivePlanetMapPath_&&
        ws.map.revision==wpLivePlanetRevision_&&!ws.map.dirty;
}
void StudioHost::rebuildOverlay() {
    if (!preview_) return;
    auto shapes = extShapes_;
    // World-process authoring overlays use the existing native Scene
    // OverlayShape renderer. This is a read-only projection of MapDoc,
    // never a second ECS simulation or a new viewport.
    if(session_ && !session_->workspace().project.dir.empty()) {
        auto worldShapes=worldprocess_gizmos::collect(session_->workspace().map,{},wpLiveOptions_.layer);
        shapes.reserve(shapes.size()+worldShapes.size());
        for(auto& shape:worldShapes)shapes.push_back(std::move(shape));
        if(wpLiveOptions_.live&&wpLiveSnapshot_.getBool("active")){
            for(const auto& r:wpLiveSnapshot_["regions"].items()){
                if(shapes.size()+5>=1390)break;
                const float x0=float(r.getInt("minXcm",0))/100.f,z0=float(r.getInt("minZcm",0))/100.f;
                const float x1=float(r.getInt("maxXcm",0))/100.f,z1=float(r.getInt("maxZcm",0))/100.f;
                const auto u=[&](const char* key){return std::clamp(float(r.getInt(key,0))/1e9f,0.f,1.f);};
                float red=u("pollution"),green=u("fertility"),blue=.35f;
                if(wpLiveOptions_.layer=="fertility")red=.15f;
                if(wpLiveOptions_.layer=="pollution")green=.15f;
                if(wpLiveOptions_.layer=="succession"){red=.15f;green=u("succession");blue=.85f;}
                if(wpLiveOptions_.layer=="durability"){green=u("durability");red=1-green;}
                if(wpLiveOptions_.layer=="off")continue;
                const auto rr=uint8_t(red*255),gg=uint8_t(green*255),bb=uint8_t(blue*255);
                render::float3 center{(x0+x1)*.5f,.9f,(z0+z1)*.5f};
                worldprocess_gizmos::rectangle(shapes,center,x1-x0,z1-z0,rr,gg,bb);
                shapes.push_back(worldprocess_gizmos::makeLabel(
                    {center.x,1.3f,center.z},"LIVE F:"+std::to_string(int(u("fertility")*100))+
                    "% P:"+std::to_string(int(u("pollution")*100))+"%",rr,gg,bb));
            }
        }
        if(wpLiveOptions_.live&&wpLiveSnapshot_.getBool("active")){
            for(const auto& p:wpLiveSnapshot_["plants"].items()){
                if(shapes.size()>=1250)break;
                const bool fallen=p.getBool("fallen"),dead=p.getBool("dead");
                const bool juvenile=p.has("mature")&&!p.getBool("mature");
                shapes.push_back(worldprocess_gizmos::makePoint(
                    float(p.getInt("x",0))/100.f,1.f,float(p.getInt("z",0))/100.f,
                    dead?160:fallen?240:100,dead?95:fallen?150:230,juvenile?240:80,juvenile?3.f:5.f));
            }
            for(const auto& c:wpLiveSnapshot_["weather"].items()){
                if(shapes.size()+4>=1300)break;
                const float size=float(c.getInt("size",0))/100.f;
                if(size<=0)continue;
                const float wet=std::clamp(float(c["wetness"].toDouble(0)),0.f,1.f);
                if(wet<.05f)continue;
                worldprocess_gizmos::rectangle(shapes,
                    {float(c.getInt("x",0))/100.f+size*.5f,1.1f,
                     float(c.getInt("z",0))/100.f+size*.5f},
                    size,size,60,uint8_t(80+wet*120),240);
            }
        }
        const auto selected=session_->selectedObjectId();
        const auto pivotSelection=session_->selectedObjectIds();
        std::string currentPreviewSelection;
        for(const auto& id:pivotSelection){
            currentPreviewSelection+=id;
            currentPreviewSelection.push_back('|');
        }
        if(wpNumericPreview_.isObject()&&pivotSelection.size()>1&&
           currentPreviewSelection==wpPreviewSelection_){
            render::float3 center{};
            if(worldprocess_edit::groupCentroid(session_->workspace().map,
                                                pivotSelection,center)){
                std::int64_t pivotX=std::llround(double(center.x)*100.);
                std::int64_t pivotZ=std::llround(double(center.z)*100.);
                if(wpNumericPreview_.getStr("pivot")=="custom"){
                    pivotX=wpNumericPreview_["pivotXcm"].integer();
                    pivotZ=wpNumericPreview_["pivotZcm"].integer();
                }
                std::vector<worldprocess_edit::AreaPreview> numeric;
                if(worldprocess_edit::previewGroupScale2D(
                       session_->workspace().map,pivotSelection,
                       wpNumericPreview_["scaleNumerator"].integer(),100,
                       wpNumericPreview_["scaleNumeratorZ"].integer(),100,
                       pivotX,pivotZ,numeric,
                       wpNumericPreview_.getStr("pivot")=="individual") &&
                   shapes.size()+numeric.size()*4+2<1390){
                    for(const auto& region:numeric)
                        worldprocess_gizmos::rectangle(shapes,
                            {float(region.centerX),.62f,float(region.centerZ)},
                            float(region.halfWidth*2.),
                            float(region.halfDepth*2.),90,210,255);
                    shapes.push_back(worldprocess_gizmos::makePoint(
                        float(pivotX)/100.f,.8f,float(pivotZ)/100.f,
                        90,210,255,11.f));
                }
            }
        }

        if(pivotSelection!=wpPivotSelection_){
            wpPivotSelection_=pivotSelection;
            wpCustomPivot_=false;
            // Never retain an old numeric overlay across a new selection.
            wpNumericPreview_=Json();
            wpPreviewOwner_.clear();
            wpPreviewSelection_.clear();
        }
        render::float3 centroid{};
        if(worldprocess_edit::groupCentroid(session_->workspace().map,
                                            pivotSelection,centroid)){
            const auto pivot=wpCustomPivot_?wpGroupPivot_:centroid;
            if(shapes.size()+3<1390){
                shapes.push_back(worldprocess_gizmos::makePoint(
                    pivot.x,pivot.y,pivot.z,100,195,255,12.f));
                shapes.push_back(worldprocess_gizmos::makeLabel(
                    {pivot.x,pivot.y+.6f,pivot.z},
                    "Group pivot X "+std::to_string(std::llround(double(pivot.x)*100.))+
                    " Z "+std::to_string(std::llround(double(pivot.z)*100.))+" cm",
                    100,195,255));
            }
        }
        if(selected.rfind("entity:@",0)==0){
            auto handles=worldprocess_edit::handles(
                session_->workspace().map,selected);
            for(const auto& handle:handles){
                if(shapes.size()>=1400)break;
                auto marker=worldprocess_gizmos::makePoint(
                    handle.position.x,handle.position.y,handle.position.z,
                    (wpDrag_.active&&wpDrag_.owner==handle.owner&&
                     wpDrag_.component==handle.component&&wpDrag_.key==handle.key)?100:245,
                    (wpDrag_.active&&wpDrag_.owner==handle.owner&&
                     wpDrag_.component==handle.component&&wpDrag_.key==handle.key)?255:245,
                    (wpDrag_.active&&wpDrag_.owner==handle.owner&&
                     wpDrag_.component==handle.component&&wpDrag_.key==handle.key)?160:245,
                    8.f);
                shapes.push_back(std::move(marker));
            }
            if(wpDrag_.active && wpDrag_.valid && shapes.size()<1398){
                const auto index=session_->workspace().map.findEntity(wpDrag_.owner);
                if(index>=0){
                    const auto origin=worldprocess_edit::point(
                        session_->workspace().map.data.entities[std::size_t(index)]);
                    if(wpDrag_.neighborSnap && shapes.size()+2<1398){
                        shapes.push_back(worldprocess_gizmos::makeLine(
                            wpDrag_.snapGuideA,wpDrag_.snapGuideB,255,205,90,4.f));
                        shapes.push_back(worldprocess_gizmos::makeLabel(
                            wpDrag_.snapGuideA,
                            "Snap: "+wpDrag_.snapTarget,255,205,90));
                    }
                    // Read-only candidate gameplay rectangle during drag.
                    const auto& owner=session_->workspace().map.data.entities[std::size_t(index)];
                    worldprocess_edit::AreaPreview areaPreview;
                    if(wpDrag_.component=="EvolveArea" &&
                       worldprocess_edit::previewArea(owner,
                           static_cast<worldprocess_edit::Handle::Mode>(wpDrag_.mode),
                           double(wpDrag_.value),wpDrag_.symmetric,areaPreview) &&
                       shapes.size()+4<1398){
                        worldprocess_gizmos::rectangle(shapes,
                            {float(areaPreview.centerX),.38f,float(areaPreview.centerZ)},
                            float(areaPreview.halfWidth*2.),
                            float(areaPreview.halfDepth*2.),100,255,160);
                    }
                    if(wpDrag_.component=="EvolveArea" &&
                       wpDrag_.key=="cornerXZ"){
                        const auto selected=session_->selectedObjectIds();
                        const auto& authored=owner.extra["components"]["EvolveArea"];
                        const auto oldX=authored.getInt("halfWidthCm",0);
                        const auto oldZ=authored.getInt("halfDepthCm",0);
                        const auto newX=std::llround(std::abs(
                            double(wpDrag_.value)-authored.getInt("offsetXcm",0)));
                        const auto newZ=std::llround(std::abs(
                            double(wpDrag_.valueZ)-authored.getInt("offsetZcm",0)));
                        render::float3 centroid{};
                        if(selected.size()>1 && oldX>=50 && oldZ>=50 &&
                           newX>=50 && newZ>=50 &&
                           worldprocess_edit::groupCentroid(
                               session_->workspace().map,selected,centroid)){
                            const auto pivot=wpCustomPivot_&&!wpDrag_.symmetric?
                                wpGroupPivot_:centroid;
                            std::vector<worldprocess_edit::AreaPreview> group;
                            if(worldprocess_edit::previewGroupScale2D(
                                session_->workspace().map,selected,
                                newX,oldX,newZ,oldZ,
                                std::llround(double(pivot.x)*100.),
                                std::llround(double(pivot.z)*100.),group,
                                wpDrag_.symmetric) &&
                               shapes.size()+group.size()*4+2<1390){
                                for(const auto& region:group)
                                    worldprocess_gizmos::rectangle(shapes,
                                        {float(region.centerX),.5f,float(region.centerZ)},
                                        float(region.halfWidth*2.),
                                        float(region.halfDepth*2.),255,195,90);
                                shapes.push_back(worldprocess_gizmos::makePoint(
                                    pivot.x,pivot.y,pivot.z,255,195,90,11.f));
                                shapes.push_back(worldprocess_gizmos::makeLabel(
                                    {pivot.x,pivot.y+.6f,pivot.z},
                                    "Scale X "+std::to_string(newX*100/oldX)+
                                    "% Z "+std::to_string(newZ*100/oldZ)+"%",
                                    255,195,90));
                            }
                        }
                    }
                    // One group preview: individual, shared centroid or
                    // explicit custom pivot. Never draw conflicting overlays.
                    if(wpDrag_.component=="EvolveArea" &&
                       (wpDrag_.key=="halfWidthCm"||wpDrag_.key=="halfDepthCm")){
                        const auto selected=session_->selectedObjectIds();
                        if(selected.size()>1){
                            std::vector<worldprocess_edit::AreaPreview> group;
                            render::float3 pivot{};
                            bool valid=false;
                            const auto mode=static_cast<worldprocess_edit::Handle::Mode>(wpDrag_.mode);
                            if(wpDrag_.symmetric||!wpCustomPivot_){
                                valid=worldprocess_edit::previewGroupResize(
                                    session_->workspace().map,selected,wpDrag_.owner,
                                    mode,double(wpDrag_.value),wpDrag_.symmetric,
                                    group,pivot);
                            }else{
                                worldprocess_edit::AreaPreview active;
                                const auto& authored=owner.extra["components"]["EvolveArea"];
                                if(worldprocess_edit::previewArea(
                                       owner,mode,double(wpDrag_.value),false,active)){
                                    const bool x=wpDrag_.key=="halfWidthCm";
                                    const auto oldHalf=authored.getInt(wpDrag_.key.c_str(),0);
                                    const auto newHalf=std::llround(
                                        (x?active.halfWidth:active.halfDepth)*100.);
                                    if(oldHalf>0){
                                        pivot=wpGroupPivot_;
                                        valid=worldprocess_edit::previewGroupScale2D(
                                            session_->workspace().map,selected,
                                            x?newHalf:1,x?oldHalf:1,
                                            x?1:newHalf,x?1:oldHalf,
                                            std::llround(double(pivot.x)*100.),
                                            std::llround(double(pivot.z)*100.),group);
                                    }
                                }
                            }
                            if(valid && shapes.size()+group.size()*4+2<1390){
                                for(const auto& region:group)
                                    worldprocess_gizmos::rectangle(shapes,
                                        {float(region.centerX),.47f,float(region.centerZ)},
                                        float(region.halfWidth*2.),
                                        float(region.halfDepth*2.),255,195,90);
                                shapes.push_back(worldprocess_gizmos::makePoint(
                                    pivot.x,pivot.y,pivot.z,255,195,90,11.f));
                                shapes.push_back(worldprocess_gizmos::makeLabel(
                                    {pivot.x,pivot.y+.6f,pivot.z},
                                    wpDrag_.symmetric?"Individual centers":
                                    (wpCustomPivot_?"Custom group pivot":"Group centroid"),
                                    255,195,90));
                            }
                        }
                    }
                    shapes.push_back(worldprocess_gizmos::makeLine(
                        origin,wpDrag_.previewPoint,245,245,245,3.f));
                    shapes.push_back(worldprocess_gizmos::makePoint(
                        wpDrag_.previewPoint.x,wpDrag_.previewPoint.y,
                        wpDrag_.previewPoint.z,100,255,160,10.f));
                    if(wpDrag_.component=="EvolveArea" &&
                       worldprocess_edit::previewArea(owner,
                           static_cast<worldprocess_edit::Handle::Mode>(wpDrag_.mode),
                           double(wpDrag_.value),wpDrag_.symmetric,areaPreview) &&
                       shapes.size()<1395){
                        shapes.push_back(worldprocess_gizmos::makeLabel(
                            {float(areaPreview.centerX),.95f,float(areaPreview.centerZ)},
                            "W "+std::to_string(int(std::lround(areaPreview.halfWidth*2.*100.)))+
                            "cm / D "+std::to_string(int(std::lround(areaPreview.halfDepth*2.*100.)))+
                            "cm",100,255,160));
                    }
                    if(shapes.size()<1399)
                        shapes.push_back(worldprocess_gizmos::makeLabel(
                            {wpDrag_.previewPoint.x,
                             wpDrag_.previewPoint.y+0.8f,
                             wpDrag_.previewPoint.z},
                            wpDrag_.label+(wpDrag_.component=="EvolveArea" &&
                               (wpDrag_.key=="halfWidthCm"||wpDrag_.key=="halfDepthCm")?
                               (wpDrag_.symmetric?" [symmetric]":" [opposite fixed]"):"")+
                             (wpDrag_.snap?" [1m snap]":""),
                             245,245,245));
                }
            }
        }
    }
    if(buildingGraphOverlayScene_){
        const auto snapshot=session_->buildingGraphSnapshot(buildingGraphOverlayScene_);
        size_t boxes=0;
        auto drawBounds=[&](const Json& row,uint8_t r,uint8_t g,uint8_t bl){
            if(boxes>=128)return;
            const auto& lo=row["min"];const auto& hi=row["max"];
            if(!lo.isArray()||!hi.isArray()||lo.size()!=3||hi.size()!=3)return;
            float a[3]{},b[3]{};
            for(size_t i=0;i<3;++i){
                const double x=lo[i].toDouble(),y=hi[i].toDouble();
                if(!lo[i].isNumber()||!hi[i].isNumber()||!std::isfinite(x)||!std::isfinite(y)||x>y||std::abs(x)>1e7||std::abs(y)>1e7)return;
                a[i]=float(x);b[i]=float(y);
            }
            ++boxes;
            auto corner=[&](unsigned c){return render::float3{c&1?b[0]:a[0],c&2?b[1]:a[1],c&4?b[2]:a[2]};};
            for(unsigned c=0;c<8;++c)for(unsigned bit:{1u,2u,4u})if(!(c&bit)){
                if((a[1]==b[1])&&(bit==2u||(c&2u)))continue;
                StudioScene::OverlayShape sh;sh.kind=1;sh.thickness=2.f;
                sh.r=r;sh.g=g;sh.bl=bl;sh.a=corner(c);sh.b=corner(c|bit);
                shapes.push_back(sh);
            }
        };
        if(buildingGraphOverlayLayers_&1u)drawBounds(snapshot["lastChange"],255,120,40);
        const auto& owners=snapshot["ownerTelemetry"];
        if(owners.isObject())for(const auto& [name,report]:owners.members()){
            (void)name;
            const auto& rows=report["overlayBounds"];
            if(!rows.isArray())continue;
            size_t inspected=0;
            for(const auto& row:rows.items()){
                if(++inspected>128||boxes>=128)break;
                const auto kind=row.getStr("kind",report.getStr("overlayKind"));
                if(kind=="nav-tiles"&&(buildingGraphOverlayLayers_&2u))drawBounds(row,70,200,255);
                if(kind=="grid-cells"&&(buildingGraphOverlayLayers_&4u))drawBounds(row,80,230,130);
                if(kind=="hpa-clusters"&&(buildingGraphOverlayLayers_&8u))drawBounds(row,185,120,255);
                if(kind=="owner-bounds"&&(buildingGraphOverlayLayers_&16u))drawBounds(row,255,210,70);
            }
        }
    }
    for (const auto& seg : splineCache_["segments"].items()) {
        StudioScene::OverlayShape sh; sh.kind = 1; sh.thickness = 2; sh.r = 255; sh.g = 200; sh.bl = 80;
        sh.a = {float(seg[0].toDouble()), 0.15f, float(seg[1].toDouble())};
        sh.b = {float(seg[2].toDouble()), 0.15f, float(seg[3].toDouble())};
        shapes.push_back(sh);
    }
    for (const auto& hd : splineCache_["handles"].items()) {
        const bool active = hd.getStr("id") == splineDrag_;
        StudioScene::OverlayShape sh; sh.kind = 3; sh.radius = active ? 6 : 4;
        sh.r = 255; sh.g = active ? 255 : 220; sh.bl = active ? 255 : 120;
        sh.a = {float(hd["x"].toDouble()), 0.2f, float(hd["z"].toDouble())};
        shapes.push_back(sh);
    }
    onScene([&] {
        preview_->setOverlayShapes(std::move(shapes));
        Json sceneSnapshot=wpLiveOptions_.live&&wpLiveOptions_.anyPlanetLayer()&&worldProcessPlanetMapCurrent()&&
            wpLiveSnapshot_.getBool("active")&&wpLiveSnapshot_.getStr("topology")=="planet"
            ?wpLiveSnapshot_:Json{};
        if(sceneSnapshot.getBool("active")&&wpLiveOptions_.forest&&wpForestRenderSnapshot_.getBool("active")&&
           wpForestRenderSnapshot_.getInt("tick",-2)+1==sceneSnapshot.getInt("tick",-1)){
            sceneSnapshot.set("forest",wpForestRenderSnapshot_);
            const auto& terrain=wpForestRenderSnapshot_["nativeTerrain"];
            if(terrain.getBool("active")&&terrain.getInt("tick",-1)==wpForestRenderSnapshot_.getInt("tick",-2))
                sceneSnapshot.set("forestTerrain",terrain);
        }
        preview_->setPlanetLiveSnapshot(std::move(sceneSnapshot),wpLivePlanetProject_,wpLivePlanetMapPath_,wpLivePlanetRevision_,wpLiveOptions_.layer,wpLiveOptions_.aetherLayer);
    });
}
// World Processes: drag authored stable-ID references or area handles.
// Hit-testing and hover preview read ONLY the MapDoc. Commit is a single
// validated StudioSession operation at mouse-up; never edit ECS/physics here.
bool StudioHost::worldProcessDrag(int phase,double x,double y,
                                  double w,double h,bool symmetric,bool snap){
    if(projectLoading_||!preview_||!session_||
       session_->workspace().project.dir.empty())return false;
    const auto& map=session_->workspace().map;
    if(map.data.planet()){
        if(phase==0){
            wpDrag_={};
            if(session_->selectedObjectIds().size()>1)return false;
            const auto id=session_->selectedObjectId();
            if(id.empty()&&session_->selectedObjectIds().empty()){
                if(!onScene([&]{return preview_->beginPlanetGravityDrag(map,float(x),float(y),float(w),float(h));}))return false;
                wpDrag_.active=true;wpDrag_.component="PlanetGravity";return true;
            }
            if(session_->lockedIds().contains(id)||session_->hiddenIds().contains(id))return false;
            if(!onScene([&]{return preview_->beginPlanetRegionDrag(map,id,float(x),float(y),float(w),float(h));}))return false;
            wpDrag_.active=true;wpDrag_.component="PlanetEvolveArea";return true;
        }
        if(wpDrag_.active&&wpDrag_.component=="PlanetGravity"){
            auto params=onScene([&]{return preview_->updatePlanetGravityDrag(map,float(x),float(y),float(w),float(h),snap,phase==2);});
            if(phase==2){
                wpDrag_={};
                if(params){
                    const auto reply=request("studio.op",std::move(*params));
                    if(reply.has("error"))platform_.log("AIVERSE planet gravity: "+reply.getStr("error"));
                    else sendEvent("document.changed","");
                }
            }
            return true;
        }
        if(!wpDrag_.active||wpDrag_.component!="PlanetEvolveArea")return false;
        auto params=onScene([&]{return preview_->updatePlanetRegionDrag(map,
            float(x),float(y),float(w),float(h),symmetric,snap,phase==2);});
        if(phase==2){
            wpDrag_={};
            if(params){
                const auto reply=request("studio.worldProcessAreaSet",std::move(*params));
                if(reply.has("error"))platform_.log("AIVERSE planet region: "+reply.getStr("error"));
                else sendEvent("document.changed","");
            }
        }
        return true;
    }
    if(phase==0){
        wpDrag_={};
        const auto& selected=session_->selectedObjectId();
        if(selected.rfind("entity:@",0)!=0)return false;
        if(session_->lockedIds().contains(selected)||
           session_->hiddenIds().contains(selected))return false;
        auto handles=worldprocess_edit::handles(map,selected);
        const auto selection=session_->selectedObjectIds();
        if(selection!=wpPivotSelection_){
            wpPivotSelection_=selection;
            wpCustomPivot_=false;
        }
        render::float3 centroid{};
        if(worldprocess_edit::groupCentroid(map,selection,centroid)){
            const auto pivot=wpCustomPivot_?wpGroupPivot_:centroid;
            handles.push_back({worldprocess_edit::Handle::Mode::GroupPivotX,
                               selected.substr(8),"EvolveGroupPivot","pivotXcm",
                               {},pivot,0});
            handles.push_back({worldprocess_edit::Handle::Mode::GroupPivotZ,
                               selected.substr(8),"EvolveGroupPivot","pivotZcm",
                               {},{pivot.x,pivot.y+.25f,pivot.z},0});
        }
        if(selection.size()>1){
            const int ownerIndex=map.findEntity(selected.substr(8));
            if(ownerIndex>=0){
                const auto& entity=map.data.entities[std::size_t(ownerIndex)];
                const auto& area=entity.extra["components"]["EvolveArea"];
                if(area.isObject()&&
                   worldprocess_edit::groupCentroid(map,selection,centroid)){
                    const float cx=entity.x+float(area.getInt("offsetXcm",0))/100.f;
                    const float cz=entity.z+float(area.getInt("offsetZcm",0))/100.f;
                    handles.push_back({worldprocess_edit::Handle::Mode::GroupCornerXZ,
                        entity.id,"EvolveArea","cornerXZ",{},
                        {cx+float(area.getInt("halfWidthCm",0))/100.f,.35f,
                         cz+float(area.getInt("halfDepthCm",0))/100.f},0});
                }
            }
        }
        if(handles.empty())return false;
        const auto candidates=onScene([&]{
            return worldprocess_edit::hitCandidates(
                handles,*preview_,x,y,w,h);
        });
        if(candidates.empty())return false;
        // Repeated presses at one screen location cycle deterministic
        // overlapping endpoints, even when multiple links point to
        // precisely the same world object.
        if(wpHitOwner_==selected&&std::hypot(x-wpHitX_,y-wpHitY_)<=9.)
            ++wpHitOrdinal_;
        else wpHitOrdinal_=0;
        wpHitOwner_=selected;
        wpHitX_=x;wpHitY_=y;
        const int index=candidates[wpHitOrdinal_%candidates.size()];
        const auto& hit=handles[std::size_t(index)];
        wpDrag_.active=true;
        wpDrag_.mode=static_cast<std::uint8_t>(hit.mode);
        wpDrag_.owner=hit.owner;
        wpDrag_.component=hit.component;
        wpDrag_.key=hit.key;
        wpDrag_.targetType=hit.targetType;
        wpDrag_.label=hit.component+"."+hit.key+
            (candidates.size()>1?
                 " ("+std::to_string(wpHitOrdinal_%candidates.size()+1)+
                 "/"+std::to_string(candidates.size())+")":"");
        wpDrag_.baseline=hit.baseline;
        wpDrag_.previewPoint=hit.position;
        wpDrag_.valid=false;
        rebuildOverlay();
        return true;
    }
    if(!wpDrag_.active)return false;
    wpDrag_.symmetric=symmetric;
    wpDrag_.snap=snap;
    if(phase==1||phase==2){
        if(wpDrag_.mode==static_cast<std::uint8_t>(
                worldprocess_edit::Handle::Mode::Reference)){
            // A dropped endpoint must be an actual stable authored map
            // entity with the required compatible component.
            worldprocess_edit::Handle handle;
            handle.targetType=wpDrag_.targetType;
            const auto* candidate=onScene([&]{
                return worldprocess_edit::nearestTarget(
                    map,handle,*preview_,x,y,w,h);
            });
            wpDrag_.previewTarget=candidate?candidate->id:std::string{};
            wpDrag_.valid=candidate!=nullptr;
            if(candidate)wpDrag_.previewPoint=
                worldprocess_edit::point(*candidate);
        }else{
            const int ownerIndex=map.findEntity(wpDrag_.owner);
            render::float3 ground{};
            const bool groundOK=ownerIndex>=0&&onScene([&]{
                return preview_->groundPoint(
                    float(x),float(y),float(w),float(h),ground);
            });
            if(!groundOK)wpDrag_.valid=false;
            else{
                worldprocess_edit::Handle handle;
                handle.mode=static_cast<worldprocess_edit::Handle::Mode>(
                    wpDrag_.mode);
                handle.baseline=wpDrag_.baseline;
                float value=0;
                if(wpDrag_.component=="EvolveGroupPivot"){
                    wpDrag_.valid=std::isfinite(ground.x)&&std::isfinite(ground.z);
                    value=wpDrag_.key=="pivotXcm"?
                        float(std::llround(double(ground.x)*100.)):
                        float(std::llround(double(ground.z)*100.));
                }else wpDrag_.valid=worldprocess_edit::extent(
                    map.data.entities[std::size_t(ownerIndex)],handle,
                    ground,value);
                if(wpDrag_.valid){
                    wpDrag_.neighborSnap=false;
                    wpDrag_.snapTarget.clear();
                    if(wpDrag_.component=="EvolveArea"&&wpDrag_.snap){
                        // Snap only to overlapping neighboring edge segments.
                        const auto snap=worldprocess_edit::snapNeighborEdge(
                            map,map.data.entities[std::size_t(ownerIndex)],
                            handle.mode,value,100.f);
                        if(snap.found){
                            value=snap.valueCm;
                            wpDrag_.neighborSnap=true;
                            wpDrag_.snapTarget=snap.targetId;
                            const auto& owner=map.data.entities[std::size_t(ownerIndex)];
                            const bool x=handle.mode==worldprocess_edit::Handle::Mode::AreaWidth||
                                handle.mode==worldprocess_edit::Handle::Mode::AreaWidthNegative;
                            const float edge=(x?owner.x:owner.z)+value/100.f;
                            wpDrag_.snapGuideA=x?
                                render::float3{edge,.42f,snap.guideStart}:
                                render::float3{snap.guideStart,.42f,edge};
                            wpDrag_.snapGuideB=x?
                                render::float3{edge,.42f,snap.guideEnd}:
                                render::float3{snap.guideEnd,.42f,edge};
                        }else value=std::round(value/100.f)*100.f;
                    }
                    wpDrag_.value=value;
                    if(handle.mode==worldprocess_edit::Handle::Mode::GroupCornerXZ){
                        const auto& entity=map.data.entities[std::size_t(ownerIndex)];
                        wpDrag_.valueZ=float(std::round((double(ground.z)-entity.z)*100.));
                        if(wpDrag_.snap)
                            wpDrag_.valueZ=std::round(wpDrag_.valueZ/100.f)*100.f;
                    }
                    wpDrag_.previewPoint=ground;
                }
            }
        }
    }
    if(phase==1){
        rebuildOverlay();
        return true;
    }
    // Pointer release: the preview is still the current drag result.
    const auto current=wpDrag_;
    wpDrag_={};
    rebuildOverlay();
    if(!current.valid)return true;
    if(current.component=="EvolveGroupPivot"){
        render::float3 centroid{};
        if(!worldprocess_edit::groupCentroid(map,session_->selectedObjectIds(),centroid))
            return true;
        if(!wpCustomPivot_)wpGroupPivot_=centroid;
        const float metres=current.value/100.f;
        if(current.key=="pivotXcm")wpGroupPivot_.x=metres;
        else wpGroupPivot_.z=metres;
        wpCustomPivot_=true;
        Json payload=Json::object();
        payload.set("pivotXcm",Json::number(
            std::int64_t(std::llround(double(wpGroupPivot_.x)*100.))));
        payload.set("pivotZcm",Json::number(
            std::int64_t(std::llround(double(wpGroupPivot_.z)*100.))));
        sendEventPayload("worldProcess.pivotChanged",payload);
        rebuildOverlay();
        return true; // editor-only pivot; never write an ECS or MapDoc component
    }
    Json params=Json::object();
    params.set("entityId",Json::string("entity:@"+current.owner));
    params.set("componentId",Json::string(current.component));
    params.set("key",Json::string(current.key));
    if(current.mode==static_cast<std::uint8_t>(
           worldprocess_edit::Handle::Mode::Reference)){
        if(current.previewTarget.empty())return true;
        params.set("value",Json::string(current.previewTarget));
    }else{
        if(current.mode==static_cast<std::uint8_t>(
               worldprocess_edit::Handle::Mode::Radius))
            params.set("value",Json::number(double(current.value),3));
        else
            params.set("value",Json::number(int64_t(std::lround(current.value))));
    }
    // Explicit edge operation: existing numeric extent RPC remains backward
    // compatible for inspectors and automation clients.
    const auto mode=static_cast<worldprocess_edit::Handle::Mode>(current.mode);
    if(!current.symmetric && (mode==worldprocess_edit::Handle::Mode::AreaWidth||
       mode==worldprocess_edit::Handle::Mode::AreaDepth||
       mode==worldprocess_edit::Handle::Mode::AreaWidthNegative||
       mode==worldprocess_edit::Handle::Mode::AreaDepthNegative)){
        params.set("edgeSide",Json::string(
            (mode==worldprocess_edit::Handle::Mode::AreaWidthNegative||
             mode==worldprocess_edit::Handle::Mode::AreaDepthNegative)?"negative":"positive"));
    }
    if(current.symmetric && current.component=="EvolveArea" &&
       (current.key=="halfWidthCm"||current.key=="halfDepthCm")){
        const int index=map.findEntity(current.owner);
        if(index<0)return true;
        worldprocess_edit::AreaPreview areaPreview;
        if(!worldprocess_edit::previewArea(
               map.data.entities[std::size_t(index)],mode,
               double(current.value),true,areaPreview))return true;
        const double half=current.key=="halfWidthCm"?
            areaPreview.halfWidth:areaPreview.halfDepth;
        params.set("value",Json::number(int64_t(std::lround(half*100.))));
    }
    if(current.component=="EvolveArea" &&
       mode==worldprocess_edit::Handle::Mode::GroupCornerXZ){
        const auto selected=session_->selectedObjectIds();
        if(selected.size()<2)return true;
        const int index=map.findEntity(current.owner);
        if(index<0)return true;
        const auto& owner=map.data.entities[std::size_t(index)];
        const auto& area=owner.extra["components"]["EvolveArea"];
        const auto oldX=area.getInt("halfWidthCm",0);
        const auto oldZ=area.getInt("halfDepthCm",0);
        const auto centerX=area.getInt("offsetXcm",0);
        const auto centerZ=area.getInt("offsetZcm",0);
        if(oldX<50||oldZ<50)return true;
        const auto newX=std::llround(std::abs(double(current.value)-centerX));
        const auto newZ=std::llround(std::abs(double(current.valueZ)-centerZ));
        if(newX<50||newZ<50)return true;
        Json ids=Json::array();
        for(const auto& id:selected)ids.push(Json::string(id));
        Json batch=Json::object();
        batch.set("entityIds",std::move(ids));
        batch.set("axis",Json::string("both"));
        const bool custom=wpCustomPivot_&&!current.symmetric;
        batch.set("pivot",Json::string(current.symmetric?"individual":
                                      (custom?"custom":"shared")));
        if(custom){
            batch.set("pivotXcm",Json::number(
                std::int64_t(std::llround(double(wpGroupPivot_.x)*100.))));
            batch.set("pivotZcm",Json::number(
                std::int64_t(std::llround(double(wpGroupPivot_.z)*100.))));
        }
        batch.set("scaleNumerator",Json::number(newX));
        batch.set("scaleDenominator",Json::number(oldX));
        batch.set("scaleNumeratorZ",Json::number(newZ));
        batch.set("scaleDenominatorZ",Json::number(oldZ));
        const auto response=request("studio.worldProcessAreaBatchResize",std::move(batch));
        if(response.has("error")){
            Json error=Json::object();
            error.set("message",Json::string(
                "EVOLVE corner resize: "+response.getStr("error")));
            sendEventPayload("assets.importError",error);
        }else{
            sendEvent("document.changed","");
            sendEvent("selection.changed","");
            rebuildOverlay();
        }
        return true;
    }
    // Group edge scaling is one atomic MapDoc operation. Shift selects
    // individual pivots; default group scaling uses the shared centroid.
    if(current.component=="EvolveArea" &&
       (mode==worldprocess_edit::Handle::Mode::AreaWidth||
        mode==worldprocess_edit::Handle::Mode::AreaWidthNegative||
        mode==worldprocess_edit::Handle::Mode::AreaDepth||
        mode==worldprocess_edit::Handle::Mode::AreaDepthNegative)){
        const auto selected=session_->selectedObjectIds();
        if(selected.size()>1){
            const int index=map.findEntity(current.owner);
            if(index<0)return true;
            const auto& owner=map.data.entities[std::size_t(index)];
            const auto& area=owner.extra["components"]["EvolveArea"];
            worldprocess_edit::AreaPreview candidate;
            if(!worldprocess_edit::previewArea(owner,mode,
                    double(current.value),current.symmetric,candidate))return true;
            const bool x=current.key=="halfWidthCm";
            const auto oldHalf=area.getInt(current.key.c_str(),0);
            const auto newHalf=std::int64_t(std::llround(
                (x?candidate.halfWidth:candidate.halfDepth)*100.));
            if(oldHalf<50||newHalf<50)return true;
            Json ids=Json::array();
            for(const auto& id:selected)ids.push(Json::string(id));
            Json batch=Json::object();
            batch.set("entityIds",std::move(ids));
            batch.set("axis",Json::string(x?"x":"z"));
            const bool custom=wpCustomPivot_&&!current.symmetric;
            batch.set("pivot",Json::string(current.symmetric?"individual":
                                          (custom?"custom":"shared")));
            if(custom){
                batch.set("pivotXcm",Json::number(
                    std::int64_t(std::llround(double(wpGroupPivot_.x)*100.))));
                batch.set("pivotZcm",Json::number(
                    std::int64_t(std::llround(double(wpGroupPivot_.z)*100.))));
            }
            batch.set("scaleNumerator",Json::number(newHalf));
            batch.set("scaleDenominator",Json::number(oldHalf));
            const auto response=request("studio.worldProcessAreaBatchResize",std::move(batch));
            if(response.has("error")){
                Json error=Json::object();
                error.set("message",Json::string(
                    "EVOLVE group resize: "+response.getStr("error")));
                sendEventPayload("assets.importError",error);
            }else{
                sendEvent("document.changed","");
                sendEvent("selection.changed","");
                rebuildOverlay();
            }
            return true;
        }
    }
    // Multi-selected EVOLVE areas translate by the same center delta.
    // A dedicated RPC validates all candidates before one MapDoc Undo entry.
    if(current.component=="EvolveArea" &&
       (mode==worldprocess_edit::Handle::Mode::AreaCenterX||
        mode==worldprocess_edit::Handle::Mode::AreaCenterZ)){
        const auto selected=session_->selectedObjectIds();
        if(selected.size()>1){
            const int index=map.findEntity(current.owner);
            if(index<0)return true;
            const auto& area=map.data.entities[std::size_t(index)]
                                .extra["components"]["EvolveArea"];
            const auto old=area.getInt(current.key.c_str(),0);
            const auto delta=std::int64_t(std::lround(current.value))-old;
            Json ids=Json::array();
            for(const auto& id:selected)ids.push(Json::string(id));
            Json batch=Json::object();
            batch.set("entityIds",std::move(ids));
            batch.set("axis",Json::string(
                mode==worldprocess_edit::Handle::Mode::AreaCenterX?"x":"z"));
            batch.set("deltaCm",Json::number(delta));
            const auto reply=request("studio.worldProcessAreaBatchMove",std::move(batch));
            if(reply.has("error")){
                Json error=Json::object();
                error.set("message",Json::string(
                    "EVOLVE group move: "+reply.getStr("error")));
                sendEventPayload("assets.importError",error);
            }else{
                sendEvent("document.changed","");
                sendEvent("selection.changed","");
                rebuildOverlay();
            }
            return true;
        }
    }
    const char* method=current.mode==static_cast<std::uint8_t>(
        worldprocess_edit::Handle::Mode::Reference)?
        "studio.setProperty":"studio.worldProcessAreaSet";
    const auto reply=request(method,std::move(params));
    if(reply.has("error")){
        Json error=Json::object();
        error.set("message",Json::string(
            "World Processes gizmo: "+reply.getStr("error")));
        sendEventPayload("assets.importError",error);
    }else{
        sendEvent("document.changed","");
        sendEvent("selection.changed","");
        rebuildOverlay();
    }
    return true;
}

// phase 0 press (true when a handle was hit), 1 drag, 2 release (commits the move).
bool StudioHost::splineDrag(int phase, double x, double y, double w, double h) {
    if (projectLoading_ || !preview_ || session_->splineEdit().empty()) return false;
    if (phase == 0) {
        float best = 12.f;
        std::string hit;
        for (const auto& hd : splineCache_["handles"].items()) {
            render::float2 p;
            if (!onScene([&] { return preview_->projectPoint({float(hd["x"].toDouble()), 0.2f, float(hd["z"].toDouble())}, float(w), float(h), p); })) continue;
            const float d = std::hypot(p.x - float(x), p.y - float(y));
            if (d < best) { best = d; hit = hd.getStr("id"); }
        }
        splineDrag_ = hit;
        rebuildOverlay();
        return !hit.empty();
    }
    if (splineDrag_.empty()) return false;
    render::float3 at{};
    const bool onGround = onScene([&] { return preview_->groundPoint(float(x), float(y), float(w), float(h), at); });
    if (phase == 1) {
        if (!onGround) return true;
        Json handles = Json::array();
        for (const auto& hd : splineCache_["handles"].items()) {
            if (hd.getStr("id") != splineDrag_) { handles.push(hd); continue; }
            Json m = hd;
            m.set("x", Json::number(double(at.x), 3));
            m.set("z", Json::number(double(at.z), 3));
            handles.push(std::move(m));
        }
        splineCache_.set("handles", std::move(handles));
        rebuildOverlay();
        return true;
    }
    // release: the cached handle holds the last ground position
    double hx = 0, hz = 0;
    bool found = false;
    for (const auto& hd : splineCache_["handles"].items())
        if (hd.getStr("id") == splineDrag_) { hx = hd["x"].toDouble(); hz = hd["z"].toDouble(); found = true; }
    std::string error;
    const std::string id = splineDrag_;
    splineDrag_.clear();
    if (found && !session_->splineMove(id, hx, hz, &error)) {
        Json payload = Json::object();
        payload.set("message", Json::string("Move node: " + error));
        sendEventPayload("assets.importError", payload);
    }
    splineCache_ = session_->splineHandles();
    rebuildOverlay();
    sendEvent("document.changed", "");
    return true;
}
// phase 0 begin, 1 drag, 2 end. Map coordinates: world x, world z (the terrain ops' x, y).
bool StudioHost::terrainStroke(int phase, double x, double y, double w, double h, bool shift) {
    if (projectLoading_ || !preview_) return false;
    std::string error;
    auto report = [&] {
        Json payload = Json::object();
        payload.set("message", Json::string("Scene brush: " + error));
        sendEventPayload("assets.importError", payload);
    };
    if (phase == 2) {
        if (!session_->strokeEnd(&error)) report();
        sendEvent("document.changed", "");
        return true;
    }
    render::float3 at{};
    std::string planetBody;
    if (!resolveBrushPoint(x,y,w,h,at,&error,&planetBody)) {
        if(!error.empty())report();
        onScene([&]{preview_->setBrushCursor(false,{},0);});
        return phase != 0;
    }
    const auto& brush=session_->sceneBrush();
    const float radius=brush.kind=="road"?.6f:brush.kind=="river"?float(brush.width*.5):float(brush.radius);
    onScene([&]{preview_->setBrushCursor(true,at,radius,brush.kind=="caverns",brush.caveMode=="fill");});
    const bool ok = phase == 0 ? session_->strokeBegin(at.x, at.z, &error,at.y,shift,planetBody) : session_->strokeDab(at.x, at.z, &error,at.y,shift,planetBody);
    if (phase==0&&ok)sendEvent("document.changed", ""); // History controls observe stroke ownership immediately.
    if (!ok && !error.empty()) {report();if(!session_->strokeActive())sendEvent("document.changed", "");}
    return ok;
}
bool StudioHost::placeAsset(const std::string& asset, double x, double y, double w, double h) {

    if (projectLoading_ || !preview_) return false;
    render::float3 at{};
    if (!onScene([&] { return preview_->groundPoint(float(x), float(y), float(w), float(h), at); })) return false;
    Json params = Json::object();
    params.set("asset", Json::string(asset));
    params.set("x", Json::number(double(at.x), 3));
    params.set("z", Json::number(double(at.z), 3));
    const auto outcome = request("studio.assetPlace", params);
    if (outcome.has("error")) {
        Json payload = Json::object();
        payload.set("message", Json::string("Place in Scene: " + outcome.getStr("error")));
        sendEventPayload("assets.importError", payload);
        return false;
    }
    sendEvent("document.changed", "");
    sendEvent("selection.changed", "");
    return true;
}
bool StudioHost::placeType(const std::string& type, double x, double y, double w, double h) {

    if (projectLoading_ || !preview_ || type.empty() || type.size() > 128) return false;
    render::float3 at{};
    if (!onScene([&] { return preview_->groundPoint(float(x), float(y), float(w), float(h), at); })) return false;
    // Resources and scenery start neutral, everything else belongs to player 1.
    const auto& t = session_->workspace().project.rulesJson["types"][type];
    const int owner = (t.has("resourceNode") || t.getBool("invulnerable")) ? 0 : 1;
    Json params = Json::object();
    params.set("type", Json::string(type));
    params.set("owner", Json::number(int64_t(owner)));
    params.set("x", Json::number(double(at.x), 3));
    params.set("z", Json::number(double(at.z), 3));
    const auto outcome = request("studio.entityCreate", params);
    if (outcome.has("error")) {
        Json payload = Json::object();
        payload.set("message", Json::string("Place " + type + ": " + outcome.getStr("error")));
        sendEventPayload("assets.importError", payload);
        return false;
    }
    sendEvent("document.changed", "");
    sendEvent("selection.changed", "");
    return true;
}
void StudioHost::humanOrbit(StudioSurface surface,double dx,double dy) {
    auto& asset=assetSurface(surface);
    if(asset.renderer&&finite({dx,dy}))onScene([&]{asset.renderer->orbit(float(dx),float(dy));});
}
void StudioHost::humanZoom(StudioSurface surface,double amount) {
    auto& asset=assetSurface(surface);
    if(asset.renderer&&std::isfinite(amount))onScene([&]{asset.renderer->zoom(float(amount));});
}
bool StudioHost::humanPick(StudioSurface surface,double x,double y,double w,double h) {
    auto& asset=assetSurface(surface);
    if(projectLoading_||!asset.renderer||(!session_->sculptBrush().armed&&!session_->sculptBrush().regionPick))return false;
    // Human Creator: a face blend region choice, or a sculpt brush stroke on the character.
    const bool region = session_->sculptBrush().regionPick;
    const auto& face = session_->facialPreviewBinding().face();
    vivid::HumanFaceHit hit;
    Json payload = Json::object();
    std::string pickError;
    if (face && onScene([&] { return asset.renderer->sculptPick(float(x), float(y), float(w), float(h), *face, hit, &pickError); })) {
        Json p = Json::array();
        for (float v : {hit.point.x, hit.point.y, hit.point.z}) p.push(Json::number(double(v), region ? 5 : 6));
        payload.set("hit", Json::fromBool(true));
        if (region) payload.set("point", std::move(p));
        else {
            Json params = Json::object(), n = Json::array();
            for (float v : {hit.normal.x, hit.normal.y, hit.normal.z}) n.push(Json::number(double(v), 6));
            params.set("point", std::move(p));
            params.set("normal", std::move(n));
            const Json outcome = request("studio.humanSculptStroke", std::move(params));
            if (outcome.has("error")) payload.set("error", outcome["error"]);
        }
    } else {
        payload.set("hit", Json::fromBool(false));
        if (!pickError.empty()) payload.set("error", Json::string(pickError));
    }
    sendEventPayload(region ? "human.facePick" : "human.sculpt", payload);
    return true;
}
void StudioHost::finishScenePick(Json result){
    if(scenePickerToken_.empty())return;
    result.set("token",Json::string(scenePickerToken_));
    const bool road=pickingRoad_;
    pickingRoad_=false;roadOrigin_.clear();roadScope_=Json{};
    const bool area=pickingArea_;
    scenePickerToken_.clear();pickingArea_=false;areaOrigin_.reset();areaScope_=Json{};
    if(area && preview_)onScene([&]{preview_->showAreaBounds({});});
    sendEventPayload(road?"roadPick.finished":area?"area.picked":"playerStart.picked",result);
}
void StudioHost::updateAreaPick(int phase,double x,double y,double w,double h){
    if(projectLoading_ || !session_ || !preview_ || session_->graphDocumentScope()!=areaScope_){
        finishScenePick(Json::object());return;
    }
    const auto hit=onScene([&]{return preview_->flatMapPoint(float(x),float(y),float(w),float(h),session_->workspace().map);});
    std::optional<StudioMapAreaBounds> bounds;
    if(hit){
        const StudioMapCell cell{int(std::floor(hit->x)),int(std::floor(hit->z))};
        if(phase==0)areaOrigin_=cell;
        if(areaOrigin_)bounds=studioAreaBetween(*areaOrigin_,cell);
    }
    if(phase==1 || (phase==0 && bounds)){
        onScene([&]{preview_->showAreaBounds(bounds);});return;
    }
    Json result=Json::object();
    if(bounds){
        Json rect=Json::array();
        for(int coord:*bounds)rect.push(Json::number(int64_t(coord)));
        result.set("rect",std::move(rect));
    }else result.set("error",Json::string("Both corners must be inside the map."));
    finishScenePick(std::move(result));
}
void StudioHost::roadPick(double x, double y, double w, double h, bool chain) {
    if (!pickingRoad_) return;
    Json update = Json::object();
    update.set("token", Json::string(scenePickerToken_));
    try {
        if (projectLoading_ || !session_ || !preview_ || session_->graphDocumentScope() != roadScope_)
            throw std::runtime_error("Map changed. Start a new road.");
        const auto& doc = session_->workspace().map;
        const auto hit = onScene([&] { return preview_->pick(float(x),float(y),float(w),float(h),doc,false); });
        if (hit == roadOrigin_ && !hit.empty()) return;
        Json ids = Json::array();
        const bool endpoint = hit.starts_with("entity:");
        if (endpoint && !roadOrigin_.empty()) ids.push(Json::string(roadOrigin_));
        if (endpoint) ids.push(Json::string(hit));
        Json params = Json::object();
        params.set("ids", ids);
        const auto selection = request("studio.select", params);
        if (selection.has("error")) throw std::runtime_error(selection.getStr("error"));
        sendEvent("selection.changed","");
        if (ids.size() == 2) {
            const auto& connected = selection["result"]["selectedRoad"];
            if (connected.isNull()) throw std::runtime_error("Choose two editable map objects.");
            params.set("scope", roadScope_);
            params.set("linked", Json::fromBool(!connected.boolean()));
            const auto edit = request("studio.selectedRoadSet", params);
            if (edit.has("error")) throw std::runtime_error(edit.getStr("error"));
            const auto& result = edit["result"];
            roadOrigin_ = chain ? result["selectedIds"][1].str() : "";
            roadScope_ = result["graphDocument"];
            sendEvent("document.changed","");
        } else roadOrigin_ = endpoint ? hit : "";
        update.set("hasOrigin",Json::fromBool(!roadOrigin_.empty()));
        sendEventPayload("roadPick.changed",update);
    } catch (const std::exception& e) {
        update.set("error",Json::string(e.what()));
        finishScenePick(std::move(update));
    }
}

void StudioHost::select(double x, double y, double w, double h, const std::string& mode) {
    if (pickingRoad_) { roadPick(x,y,w,h,false); return; }
    if(!scenePickerToken_.empty() && mode=="replace"){
        if(pickingArea_)return;
        Json result=Json::object(),start;
        if(!projectLoading_ && session_ && preview_ && placement_.visible && w>=2 && h>=2 && x>=0 && y>=0 && x<w && y<h){
            const auto& map=session_->workspace().map;
            const auto at=onScene([&]{return preview_->playerStartPlacement(float(x),float(y),float(w),float(h),map);});
            if(at)start=*at;
        }
        if(start.isNull())result.set("error",Json::string("No map surface at this point. Pick a point inside the map."));
        else result.set("start",std::move(start));
        finishScenePick(std::move(result));
        return;
    }
    if(forestPlantArm_.isObject()&&mode=="replace"){
        const auto arm=forestPlantArm_;forestPlantArm_=Json{};
        if(preview_)onScene([&]{preview_->setForestDragPreview(Json{});});
        Json payload=Json::object();
        try{
            if(projectLoading_||!preview_||!session_||session_->strokeActive())
                throw std::runtime_error("Scene planting unavailable");
            const auto& ws=session_->workspace();
            if(arm.getStr("project")!=ws.project.dir||arm.getStr("mapPath")!=ws.map.path||
               arm.getStr("expectedRevision")!=std::to_string(ws.map.revision))
                throw std::runtime_error("Forest planting cancelled: document changed");
            if(arm.getStr("sceneBody")!=worldProcessSceneBody())throw std::runtime_error("Forest action cancelled: Scene body changed");
            if(wpLiveOptions_.live&&wpLiveOptions_.forest)throw std::runtime_error("Disable live forest before authoring");
            const auto action=arm.getStr("action","plant");
            if(action=="pick"){
                const auto hit=onScene([&]{return preview_->forestPick(float(x),float(y),float(w),float(h),ws.project.dir,ws.map.path,ws.map.revision);});
                if(!hit)throw std::runtime_error("No visible authored plant hit; enable Scene gizmos and disable live forest");
                Json selected=*hit;selected.set("project",Json::string(ws.project.dir));selected.set("mapPath",Json::string(ws.map.path));
                selected.set("expectedRevision",Json::string(std::to_string(ws.map.revision)));
                onScene([&]{preview_->setForestSelection(selected);});
                payload.set("selected",*hit);sendEventPayload("planetForest.planted",payload);return;
            }
            const auto at=onScene([&]{return preview_->forestGroundPlacement(float(x),float(y),float(w),float(h),ws.map.path,ws.map.revision);});
            if(!at)throw std::runtime_error("No current planetary surface hit");
            Json params=arm;params.set("at",*at);
            const auto outcome=request(action=="move"?"studio.planetForestMove":"studio.planetForestPlant",params);
            if(outcome.has("error"))throw std::runtime_error(outcome.getStr("error"));
            if(action=="move"){
                Json selected=arm;selected.set("expectedRevision",outcome["result"]["revision"]);
                onScene([&]{preview_->setForestSelection(selected);});
                Json hit=Json::object();hit.set("species",arm["species"]);hit.set("key",arm["key"]);hit.set("direct",Json::fromBool(true));hit.set("body",(*at)["body"]);
                payload.set("selected",std::move(hit));
            }
            payload.set("document",outcome["result"]);
        }catch(const std::exception& e){payload.set("error",Json::string(e.what()));}
        sendEventPayload("planetForest.planted",payload);
        if(payload.has("document"))sendEvent("document.changed","");
        return;
    }
    if (projectLoading_ || !preview_) return;
    const std::string objectId = onScene([&] { return preview_->pick(float(x), float(y), float(w), float(h),
        session_->workspace().map, session_->workspace().map.data.worldSpace); });
    // A modifier click on empty space keeps the current selection.
    if (objectId.empty() && mode != "replace") return;
    Json params = Json::object();
    if (!objectId.empty()) params.set("id", Json::string(objectId));
    else params.set("id", Json());
    params.set("mode", Json::string(mode));
    if (!request("studio.select", std::move(params)).has("error")) sendEvent("selection.changed", "");
}
void StudioHost::importDroppedFiles(const std::vector<fs::path>& files, const std::string& folder) {
    if (projectLoading_ || files.empty() || files.size() > 64 || session_->workspace().project.dir.empty()) return;
    try {
        if(std::any_of(files.begin(),files.end(),isSpectraPackagePath)){
            if(files.size()!=1)throw std::runtime_error("Drop one VFX package at a time");
            sendEventPayload("spectrafx.packageStaged",stageSpectraPackage(session_->workspace().project.dir,files.front()));
            return;
        }
        sendEventPayload("assets.importPreview", session_->previewExternalAssets(files, folder));
    } catch (const std::exception& ex) {
        Json payload = Json::object();
        payload.set("message", Json::string(ex.what()));
        sendEventPayload("assets.importError", payload);
    }
}

// ---------------------------------------------------------------- neutral Scene input
// Unity Scene view mapping, shared with the HTML composite path (apps/studio-web/src/panels/sceneCamera.ts):
// LMB select (drag: marquee), Alt+LMB orbit, Alt+Command+LMB / MMB pan, RMB flythrough (WASD/QE), Alt+RMB zoom.
void StudioHost::sceneMouse(int phase, int button, double x, double y, double dx, double dy, double w, double h, StudioSceneMods mods) {
    // Embedded VFX preview is *not* the game map. Its pointer input controls
    // its isolated camera only; never pick, sculpt, paint, select, drag or
    // delete an authored entity while this native layer belongs to FX.
    auto& m = mouse_;
    if(preview_&&onScene([&]{return preview_->caveGlobeRequested();})){
        // Globe coordinates are never passed to flat entity/terrain tools.
        if(button==1){if(phase==1){if(mods.alt)zoom((dx+dy)*.6);else orbit(dx,dy);}return;}
        if(button!=0)return;
        if(phase==0){m={};m.active=true;m.pressX=x;m.pressY=y;return;}
        if(phase==1){
            if(m.active&&(m.dragged||std::hypot(x-m.pressX,y-m.pressY)>=5.)){
                m.dragged=true;orbit(dx,dy);
            }
            return;
        }
        const auto st=m;m={};
        if(st.active&&!st.dragged&&!mods.alt&&std::hypot(x-st.pressX,y-st.pressY)<5.){
            const auto& ws=session_->workspace();
            auto hit=onScene([&]{return preview_->pickCaveGlobe(float(x),float(y),float(w),float(h),ws.map,ws.project.dir);});
            if(hit.getBool("hit",false)){
                hit.set("project",Json::string(ws.project.dir));
                hit.set("map",Json::string(ws.map.path.empty()?"":ws.relative(ws.map.path)));
                sendEventPayload("caverns.globePicked",hit);
            }
        }
        return;
    }
    if (button == 1) { // right: look, Alt zooms
        if (phase == 1) { if (mods.alt) zoom((dx + dy) * 0.6); else look(dx, dy); }
        return;
    }
    if (button == 2) { if (phase == 1) pan(dx, dy); return; }
    if (phase == 0) {
        if(!projectLoading_&&session_->strokeCancel())sendEvent("document.changed", "");
        m = {};
        m.active = true; m.pressX = x; m.pressY = y;
        if(!mods.alt && brushArmed() && session_->sceneBrush().pickHeight){
            m.active=false;
            render::float3 hit{};
            double elevation=0;
            if(resolveBrushPoint(x,y,w,h,hit,nullptr,nullptr,&elevation)){
                Json params=Json::object();
                params.set("targetHeight",Json::number(std::clamp(elevation,-2048.,2047.9375),6));
                params.set("pickHeight",Json::fromBool(false));
                const auto reply=request("studio.sceneBrush",params);
                if(reply.has("result"))sendEventPayload("brush.changed",reply["result"]);
            }
            return;
        }
        if(!scenePickerToken_.empty()&&!mods.alt&&!mods.command&&!mods.ctrl&&(pickingRoad_||!mods.shift)){m.playerStart=true;if(pickingArea_)updateAreaPick(0,x,y,w,h);return;}
        if(forestPlantArm_.isObject()&&!mods.alt&&!mods.command&&!mods.ctrl&&!mods.shift){m.forest=true;return;}
        // Explicit authored World Processes handles have priority only when
        // the clicked point is within their small screen-space hit radius.
        if(!mods.alt && !mods.command && !mods.ctrl && !mods.shift &&
           worldProcessDrag(0,x,y,w,h)){m.worldProcess=true;return;}
        // Spline handles (Roads / Water "Edit nodes"): LMB on a handle drags it.
        if (!mods.alt && splineDrag(0, x, y, w, h)) { m.spline = true; return; }
        // Scene brush armed: plain LMB paints; Alt still orbits.
        if (!mods.alt && brushArmed()) { m.stroke = terrainStroke(0, x, y, w, h, mods.shift); if (m.stroke||session_->sceneBrush().kind=="caverns"||session_->sceneBrush().editsBaseGrid()) { if(!m.stroke)m.active=false;return; } }
        // Plain LMB on a handle starts a Move / Rotate / Scale drag (Unity W/E/R tools).
        m.move = !mods.alt && moveBegin(x, y, w, h);
        return;
    }
    if (phase == 1) {
        if (!m.active) { // plain hover, throttled (a pick per ~50 ms)
            const double now = platform_.now();
            if (now - lastHoverTime_ >= 0.05) { lastHoverTime_ = now; hover(x, y, w, h); }
            return;
        }
        if(m.playerStart){if(pickingArea_){updateAreaPick(1,x,y,w,h);return;}if(std::hypot(x-m.pressX,y-m.pressY)>=5.0)m.dragged=true;return;}
        if(m.forest){
            if(std::hypot(x-m.pressX,y-m.pressY)>=5.0)m.dragged=true;
            if(forestPlantArm_.getStr("action")=="move"&&session_&&preview_&&!projectLoading_){
                const auto& ws=session_->workspace();Json ghost;
                if(forestPlantArm_.getStr("project")==ws.project.dir&&forestPlantArm_.getStr("mapPath")==ws.map.path&&
                   forestPlantArm_.getStr("expectedRevision")==std::to_string(ws.map.revision)&&forestPlantArm_.getStr("sceneBody")==worldProcessSceneBody()&&
                   !(wpLiveOptions_.live&&wpLiveOptions_.forest)&&!mods.alt&&!mods.command&&!mods.ctrl&&!mods.shift){
                    const auto at=onScene([&]{return preview_->forestGroundPlacement(float(x),float(y),float(w),float(h),ws.map.path,ws.map.revision);});
                    if(at){ghost=forestPlantArm_;ghost.set("at",*at);}
                }
                onScene([&]{preview_->setForestDragPreview(std::move(ghost));});
            }
            return;
        }
        if(m.worldProcess){worldProcessDrag(1,x,y,w,h,mods.shift,mods.ctrl||mods.command);return;}
        if (m.spline) { splineDrag(1, x, y, w, h); return; }
        if (m.stroke) { terrainStroke(1, x, y, w, h, mods.shift); return; }
        if (m.move) { m.dragged = true; moveUpdate(x, y, w, h, mods.ctrl || mods.command); return; }
        // Hand tool (Q): plain LMB drag pans the view.
        if (!mods.alt && tool() == 0) { m.dragged = true; pan(dx, dy); return; }
        if (!mods.alt) {
            if (!m.marquee && std::hypot(x - m.pressX, y - m.pressY) < 5.0) return;
            m.marquee = true; m.dragged = true;
            marquee(m.pressX, m.pressY, x, y, w, h);
            return;
        }
        m.dragged = true;
        if (mods.command) pan(dx, dy); else orbit(dx, dy);
        return;
    }
    // release
    const auto st = m;
    m = {};
    if (!st.active) return;
    if(st.playerStart){
        if(pickingRoad_){
            if(!st.dragged && std::hypot(x-st.pressX,y-st.pressY)<5.0 && !mods.alt && !mods.command && !mods.ctrl)
                roadPick(x,y,w,h,mods.shift);
            return;
        }
        if(pickingArea_){
            if(mods.alt||mods.command||mods.ctrl||mods.shift)finishScenePick(Json::object());
            else updateAreaPick(2,x,y,w,h);
            return;
        }
        if(!scenePickerToken_.empty()&&!st.dragged&&std::hypot(x-st.pressX,y-st.pressY)<5.0&&
           !mods.alt&&!mods.command&&!mods.ctrl&&!mods.shift)select(x,y,w,h,"replace");
        return;
    }
    if(st.forest){
        const bool move=forestPlantArm_.getStr("action")=="move";
        if(preview_)onScene([&]{preview_->setForestDragPreview(Json{});});
        if(forestPlantArm_.isObject()&&(move||(!st.dragged&&std::hypot(x-st.pressX,y-st.pressY)<5.0))&&
           !mods.alt&&!mods.command&&!mods.ctrl&&!mods.shift)select(x,y,w,h,"replace");
        return;
    }
    if(st.worldProcess){worldProcessDrag(2,x,y,w,h,mods.shift,mods.ctrl||mods.command);return;}
    if (st.spline) { splineDrag(2, 0, 0, w, h); return; }
    if (st.stroke) { terrainStroke(2, 0, 0, w, h); return; }
    if (st.move) { moveEnd(); return; }
    // Shift adds to the selection, Command toggles.
    const std::string mode = mods.command ? "toggle" : mods.shift ? "add" : "replace";
    if (st.marquee) { marqueeEnd(st.pressX, st.pressY, x, y, w, h, mode); return; }
    if (st.dragged || std::hypot(x - st.pressX, y - st.pressY) >= 5.0) return;
    if (!forestPlantArm_.isObject()&&gizmoAt(x, y, w, h)) return;
    select(x, y, w, h, mode);
}
void StudioHost::sceneWheel(double amount) { zoom(amount); }
bool StudioHost::sceneKey(StudioSceneKey key, bool down, StudioSceneMods mods, bool rightHeld) {
    using K = StudioSceneKey;
    if (!down) {
        if (key == K::V) { setPivotSnap(false); return true; }
        return false;
    }
    if(key==K::Escape&&!scenePickerToken_.empty()){finishScenePick(Json::object());mouse_={};return true;}
    if(key==K::Escape&&forestPlantArm_.isObject()){
        forestPlantArm_=Json{};mouse_={};if(preview_)onScene([&]{preview_->setForestDragPreview(Json{});});Json out=Json::object();out.set("cancelled",Json::fromBool(true));sendEventPayload("planetForest.planted",out);return true;
    }
    if(key==K::Escape&&wpDrag_.active){
        if(preview_)onScene([&]{preview_->cancelPlanetRegionDrag();preview_->cancelPlanetGravityDrag();});
        wpDrag_={};mouse_={};rebuildOverlay();
        return true; // cancel with no authored MapDoc edit
    }
    const double scale = mods.shift ? 3.0 : 1.0;
    const bool plain = !mods.ctrl && !mods.alt && !mods.command;
    // Delete / Command+D act on the whole Scene selection (one undo step).
    if (key == K::Delete && plain) { selectionAction("studio.entitiesDelete"); return true; }
    if (key == K::D && mods.command && !mods.alt && !mods.shift) { selectionAction("studio.entitiesDuplicate"); return true; }
    if (key == K::F && mods.shift && plain) { frameAll(); return true; }          // Shift+F: frame everything
    if (key == K::V && plain && !mods.shift) { setPivotSnap(true); return true; } // hold V: snap to pivots
    // Scene brush: [ / ] resize it, Esc puts it down.
    if (brushArmed() && plain) {
        if (key == K::BracketLeft) { brushAdjust(0.8, false); return true; }
        if (key == K::BracketRight) { brushAdjust(1.25, false); return true; }
        if (key == K::Escape) { brushAdjust(1.0, true); return true; }
    }
    if (!plain) return false;
    switch (key) {
        case K::Up: moveCamera(0, 0, scale); return true;
        case K::Down: moveCamera(0, 0, -scale); return true;
        case K::Left: moveCamera(-scale, 0, 0); return true;
        case K::Right: moveCamera(scale, 0, 0); return true;
        case K::Home: resetView(); return true;
        case K::F: if (!rightHeld) { focusSelection(); return true; } break;
        default: break;
    }
    if (!rightHeld) {
        // Q/W/E/R pick the Hand / Move / Rotate / Scale tool.
        switch (key) {
            case K::Q: setTool(0); return true;
            case K::W: setTool(1); return true;
            case K::E: setTool(2); return true;
            case K::R: setTool(3); return true;
            default: return false;
        }
    }
    switch (key) {
        case K::W: moveCamera(0, 0, scale); return true;
        case K::S: moveCamera(0, 0, -scale); return true;
        case K::A: moveCamera(-scale, 0, 0); return true;
        case K::D: moveCamera(scale, 0, 0); return true;
        case K::Q: moveCamera(0, -scale, 0); return true;
        case K::E: moveCamera(0, scale, 0); return true;
        default: return false;
    }
}

// ---------------------------------------------------------------- bridge
// Assets panel thumbnails, cached outside the project (<cache>/AIVERSE/thumbnails): images are scaled here,
// models are rendered offscreen by the Scene (asset.thumbnail event).
Json StudioHost::assetThumbnail(const std::string& path) {
    const auto& ws = session_->workspace();
    if (ws.project.dir.empty()) throw std::runtime_error("Open a project first");
    static const std::regex valid(R"(^content/[^\\]{1,500}$)");
    if (!std::regex_match(path, valid) || path.find("..") != std::string::npos) throw std::runtime_error("Invalid asset path");
    const fs::path source = fs::path(ws.project.dir) / fs::path(path);
    std::error_code ec;
    if (fs::is_symlink(fs::symlink_status(source, ec)) || !fs::is_regular_file(source, ec)) throw std::runtime_error("Asset not found");
    const auto size = fs::file_size(source, ec);
    const auto stamp = fs::last_write_time(source, ec).time_since_epoch().count();
    uint64_t hash = 1469598103934665603ull;
    for (char c : (ws.project.dir + "|" + path + "|" + std::to_string(size) + "|" + std::to_string(static_cast<long long>(stamp)))) {
        hash ^= uint8_t(c);
        hash *= 1099511628211ull;
    }
    const fs::path dir = platform_.cacheDir() / "AIVERSE" / "thumbnails";
    fs::create_directories(dir, ec);
    char name[32];
    std::snprintf(name, sizeof name, "%016llx.png", (unsigned long long)hash);
    const fs::path file = dir / name;
    Json out = Json::object();
    auto dataUrl = [&](const fs::path& png) -> bool {
        std::error_code e2;
        const auto bytes = fs::file_size(png, e2);
        if (e2 || bytes == 0 || bytes > 2 * 1024 * 1024) return false;
        std::ifstream in(png, std::ios::binary);
        const std::string data((std::istreambuf_iterator<char>(in)), {});
        out.set("url", Json::string("data:image/png;base64," + editor::base64Encode(data)));
        return true;
    };
    if (fs::is_regular_file(file, ec) && dataUrl(file)) return out;
    const std::string ext = lowerExt(source);
    static const std::set<std::string> images = {".png", ".jpg", ".jpeg", ".tga", ".bmp", ".gif", ".tif", ".tiff", ".webp", ".psd", ".hdr"};
    static const std::set<std::string> models = {".glb", ".gltf", ".obj", ".fbx", ".dae", ".ply", ".stl"};
    if (images.count(ext)) {
        if (size > 64ull * 1024 * 1024) throw std::runtime_error("Image too large to preview");
        int w = 0, h = 0, ow = 0, oh = 0;
        std::vector<uint8_t> rgba;
        if (!readImage(source, w, h, rgba)) throw std::runtime_error("Image cannot be read");
        const auto fitted = fitImage(rgba, w, h, 160, ow, oh);
        const std::string png = encodePng(fitted, ow, oh);
        { std::ofstream o(file, std::ios::binary); o.write(png.data(), std::streamsize(png.size())); if (!o) throw std::runtime_error("Thumbnail write failed"); }
        if (!dataUrl(file)) throw std::runtime_error("Thumbnail read failed");
        return out;
    }
    if (models.count(ext)) {
        if (!preview_) { out.set("pending", Json::fromBool(false)); return out; }
        onScene([&] { preview_->requestThumbnail(ws.project.dir, path, file.string()); });
        out.set("pending", Json::fromBool(true));
        return out;
    }
    out.set("none", Json::fromBool(true));
    return out;
}
// One-shot real GPU frame snapshot. The native RHI owns the readback; an
// immutable shared state lives until its GPU completion callback finishes.
// JPEG encoding stays in the callback and its result is read on the host UI
// thread. No continuous bitmap uploads, no arbitrary output file paths.
Json StudioHost::captureSpectraFrame(const Json& params) {
    const std::string action=params.getStr("action");
    Json out=Json::object();
    // Preserve the ordinary preview status envelope so the same typed
    // Studio/MCP response can carry a capture result without resetting
    // playback controls in React.
    const auto respond=[&](Json capture) {
        Json status=fxPreview_?onScene([&]{
            return fxPreview_->spectraPreviewCommand(
                Json::parse(R"({"action":"status"})"),
                session_->workspace().project.dir);
        }):Json::object();
        status.set("capture",std::move(capture));
        return status;
    };
    if(action=="captureCancel"||action=="captureStatus") {
        const auto shot=fxShot_;
        if(shot){
            if(!params["captureId"].isNull()&&params["captureId"].integer(-1)!=int64_t(shot->id))throw std::runtime_error("Stale capture ID");
            out.set("captureId",Json::number(int64_t(shot->id)));
            if(action=="captureCancel"){
                {std::lock_guard<std::mutex> lock(shot->mutex);shot->cancelled=true;}
                fxShot_.reset();out.set("pending",Json::fromBool(false));out.set("ready",Json::fromBool(false));out.set("cancelled",Json::fromBool(true));return respond(std::move(out));
            }
        }
        if(!shot) {
            out.set("pending",Json::fromBool(false));
            out.set("ready",Json::fromBool(false));
            out.set("error",Json::string("No pending SpectraFX frame capture"));
            return respond(std::move(out));
        }
        std::string jpeg,error;
        Json provenance=Json::object();
        int w=0,h=0;
        bool done=false;
        {
            std::lock_guard<std::mutex> lock(shot->mutex);
            done=shot->done;
            if(done) {
                jpeg=shot->jpeg;
                error=shot->error;
                provenance=shot->provenance;
                w=shot->width;
                h=shot->height;
            }
        }
        if(!done) {
            if(platform_.now()-fxShotStartedAt_>10.) {
                {std::lock_guard<std::mutex> lock(shot->mutex);shot->cancelled=true;}
                fxShot_.reset(); // callback owns its shared state until completion
                out.set("pending",Json::fromBool(false));
                out.set("ready",Json::fromBool(false));
                out.set("error",Json::string("SpectraFX GPU capture timed out"));
                return respond(std::move(out));
            }
            out.set("pending",Json::fromBool(true));
            out.set("ready",Json::fromBool(false));
            return respond(std::move(out));
        }
        fxShot_.reset();
        out.set("pending",Json::fromBool(false));
        out.set("ready",Json::fromBool(!jpeg.empty()&&error.empty()));
        out.set("width",Json::number(int64_t(w)));
        out.set("height",Json::number(int64_t(h)));
        out.set("mime",Json::string("image/jpeg"));
        out.set("provenance",std::move(provenance));
        if(!error.empty())out.set("error",Json::string(error));
        if(!jpeg.empty()&&error.empty())
            out.set("image",Json::string("data:image/jpeg;base64,"+
                editor::base64Encode(jpeg)));
        return respond(std::move(out));
    }
    if(action!="capture")throw std::runtime_error("Unknown FX capture operation");
    if(!fxPreview_ || !fxPlacement_.visible ||
       !platform_.surfaceDrawable(StudioSurface::Fx))
        throw std::runtime_error("Open the native SpectraFX GPU viewport before capture");
    const Json& raw=params["maxSide"];
    if(!raw.isNull() &&
       (!raw.isNumber()||raw.numberLiteral().find_first_of(".eE-")!=std::string::npos||
        raw.integer(0)<128||raw.integer(0)>768))
        throw std::runtime_error("FX snapshot maxSide must be an integer in 128..768");
    if(!params["requireReplayReady"].isNull() &&
       params["requireReplayReady"].type()!=Json::Type::Bool)
        throw std::runtime_error("requireReplayReady must be boolean");
    const bool requireReplayReady=params.getBool("requireReplayReady");
    const int maxSide=int(raw.isNull()?512:raw.integer());
    if(fxShot_) {
        std::lock_guard<std::mutex> lock(fxShot_->mutex);
        if(!fxShot_->done && platform_.now()-fxShotStartedAt_<=10.)
            throw std::runtime_error("An FX GPU capture is still pending");
    }
    if(fxShot_){std::lock_guard<std::mutex> lock(fxShot_->mutex);fxShot_->cancelled=true;}
    auto shot=std::make_shared<FxShot>();
    shot->id=nextFxShotId_++;
    out.set("captureId",Json::number(int64_t(shot->id)));
    fxShot_=shot;
    fxShotStartedAt_=platform_.now();
    try {
        onScene([&] {
            // Sample provenance on the same native render-owner that schedules readback.
            const Json nativeStatus=fxPreview_->spectraPreviewCommand(
                Json::parse(R"({"action":"status"})"),session_->workspace().project.dir);
            // Check and register capture in one render-owner dispatch. No
            // second IPC hop can interleave a simulation update here.
            if(requireReplayReady && !nativeStatus.getBool("replayReady"))
                throw std::runtime_error("FX replay has not reached its requested frame");
            Json metadata=Json::object();
            const auto requestedProject=session_->workspace().project.dir;
            const auto requestedGeneration=nativeStatus["generation"].integer(-1);
            const auto requestedTick=nativeStatus["tick"].integer(-1);
            metadata.set("projectPathAtRequest",Json::string(requestedProject));
            metadata.set("sampling",Json::string("render-owner-readback-request"));
            metadata.set("tick",nativeStatus["tick"]);
            metadata.set("generation",nativeStatus["generation"]);
            metadata.set("backend",nativeStatus["backend"]);
            metadata.set("camera",nativeStatus["camera"]);
            metadata.set("background",nativeStatus["background"]);
            metadata.set("gpu",nativeStatus["gpu"]);
            metadata.set("replayReady",nativeStatus["replayReady"]);
            metadata.set("frameSerialAtRequest",Json::number(int64_t(fxPreview_->fxFrameSerial())));
            metadata.set("completedFrameSerialAtRequest",Json::number(int64_t(fxPreview_->fxCompletedFrameSerial())));
            metadata.set("submissionFenceAtRequest",Json::number(int64_t(fxPreview_->fxSubmissionFence())));
            metadata.set("completedFenceAtRequest",Json::number(int64_t(fxPreview_->fxCompletedFence())));
            metadata.set("frameExact",Json::fromBool(false));
            {std::lock_guard<std::mutex> lock(shot->mutex);shot->provenance=std::move(metadata);}
            fxPreview_->captureNextFrameTagged(
                [shot,maxSide,requireReplayReady,requestedProject,requestedGeneration,requestedTick](uint64_t capturedFrame,int w,int h,std::vector<uint8_t> rgba,Json frameMetadata) {
                    {std::lock_guard<std::mutex> lock(shot->mutex);if(shot->cancelled)return;}
                    const bool identityMatches=frameMetadata.isObject()&&frameMetadata.getStr("projectPath")==requestedProject&&frameMetadata["generation"].integer(-2)==requestedGeneration;
                    {std::lock_guard<std::mutex> lock(shot->mutex);
                     shot->provenance.set("identityMatches",Json::fromBool(identityMatches));
                     shot->provenance.set("capturedFrameSerial",Json::number(int64_t(capturedFrame)));
                     shot->provenance.set("gpuFrameIdentified",Json::fromBool(capturedFrame!=0&&w>0&&h>0));
                     const bool matched=capturedFrame!=0&&w>0&&h>0&&identityMatches;
                     if(matched){
                         for(const char* key:{"tick","generation","backend","camera","background","grid","axes","gpu","replayReady","projectPath"})
                             shot->provenance.set(key,frameMetadata[key]);
                         shot->provenance.set("sampling",Json::string("matched-rendered-frame"));
                     }
                     shot->provenance.set("frameExact",Json::fromBool(matched));}
                    std::string jpeg,error;
                    int ow=0,oh=0;
                    if(frameMetadata.isObject()&&!identityMatches)error="Capture belongs to a different graph generation or project";
                    else if(requireReplayReady&&(!identityMatches||!frameMetadata.getBool("replayReady")||frameMetadata["tick"].integer(-2)!=requestedTick))
                        error="Captured frame has no matching replay-ready provenance";
                    else if(w<=0||h<=0||w>8192||h>8192||
                       uint64_t(w)*uint64_t(h)*4>64ull*1024ull*1024ull ||
                       rgba.size()<uint64_t(w)*uint64_t(h)*4)
                        error="Invalid native FX GPU capture dimensions";
                    else {
                        const int longest=std::max(w,h);
                        ow=std::max(1,w*maxSide/longest);
                        oh=std::max(1,h*maxSide/longest);
                        std::vector<uint8_t> pixels(size_t(ow)*oh*4);
                        for(int y=0;y<oh;++y)for(int x=0;x<ow;++x) {
                            const size_t sx=size_t(x)*w/ow,sy=size_t(y)*h/oh;
                            const auto* p=&rgba[(sy*size_t(w)+sx)*4];
                            auto* q=&pixels[(size_t(y)*ow+x)*4];
                            q[0]=p[0];q[1]=p[1];q[2]=p[2];q[3]=255;
                        }
                        jpeg=encodeJpeg(pixels,ow,oh,80);
                        // Bound a JSON-RPC response: 600 KiB JPEG encodes to
                        // <= 800 KiB base64, under the 1 MiB bridge limit.
                        if(jpeg.empty()||jpeg.size()>600u*1024u) {
                            jpeg.clear();
                            error="Native FX JPEG capture unavailable or too large";
                        }
                    }
                    {
                        std::lock_guard<std::mutex> lock(shot->mutex);
                        if(shot->cancelled)return;
                        shot->jpeg=std::move(jpeg);
                        shot->error=std::move(error);
                        shot->width=ow;shot->height=oh;
                        shot->done=true;
                    }
                });
        });
    } catch(...) {
        cancelSpectraCapture();
        throw;
    }
    out.set("pending",Json::fromBool(true));
    out.set("ready",Json::fromBool(false));
    return respond(std::move(out));
}
// Both HTML5 and authenticated MCP call this on the host UI thread.
// GPU work stays on the existing render owner; record actual dispatch time.
Json StudioHost::runSpectraPreview(const Json& params) {
    const auto sequence=++spectraDispatchSequence_;
    const double started=platform_.now();
    try {
        Json result=dispatchSpectraPreview(params);
        if(!result.getStr("error").empty()||!result["capture"].getStr("error").empty())
            ++spectraDispatchFailed_;
        Json metrics=Json::object();
        metrics.set("sequence",Json::number(int64_t(sequence)));
        metrics.set("action",Json::string(params.getStr("action")));
        metrics.set("elapsedMs",Json::number(std::max(0.,platform_.now()-started)*1000.,3));
        metrics.set("failed",Json::number(int64_t(spectraDispatchFailed_)));
        result.set("dispatch",std::move(metrics));
        return result;
    } catch(...) {
        ++spectraDispatchFailed_;
        throw;
    }
}
Json StudioHost::dispatchSpectraPreview(const Json& params) {
    if(projectLoading_ || session_->workspace().project.dir.empty())
        throw std::runtime_error("Open a project before SpectraFX preview");
    if(!fxPreview_)
        throw std::runtime_error("Show the native SpectraFX Graph preview to initialize its GPU device");
    const std::string project=session_->workspace().project.dir;
    if(params.has("expectedProjectPath") &&
       (!params["expectedProjectPath"].isString()||params.getStr("expectedProjectPath")!=project))
        throw std::runtime_error("Stale SpectraFX project");
    const std::string action=params.getStr("action");
    const auto current=onScene([&]{
        return fxPreview_->spectraPreviewCommand(Json::parse(R"({"action":"status"})"),project);
    });
    if(current.getBool("replayPending")&&action!="status"&&action!="stop"&&action!="replayCancel")
        throw std::runtime_error("Replay restore pending; cancel or wait");
    if(params.has("expectedGeneration")) {
        const auto& expected=params["expectedGeneration"];
        if(!expected.isNumber()||expected.numberLiteral().find_first_of(".eE-")!=std::string::npos||
           expected.integer(-1)<0)
            throw std::runtime_error("Invalid SpectraFX expected generation");
        if(current["generation"].integer(-2)!=expected.integer(-1))
            throw std::runtime_error("Stale SpectraFX runtime generation");
    }
    if(action=="capture"||action=="captureStatus"||action=="captureCancel")
        return captureSpectraFrame(params);
    // Stopping and inspecting a ready runtime never require the view to
    // remain on screen. Load/play/step/seek need a drawable to advance.
    if(action!="status" && action!="stop" && action!="replayCancel" &&
       (!fxPlacement_.visible || !platform_.surfaceDrawable(StudioSurface::Fx)))
        throw std::runtime_error("Open the dedicated SpectraFX native preview viewport");
    if(action=="cameraReset"||action=="cameraOrbit"||action=="cameraZoom") {
        if(!fxSurfaceVisible_)throw std::runtime_error("Show the embedded FX viewport for its isolated camera");
        if(action=="cameraReset") {
            return onScene([&] {
                fxPreview_->resetSpectraDockCamera();
                return fxPreview_->spectraPreviewCommand(Json::parse(R"({"action":"status"})"),project);
            });
        }
        if(action=="cameraOrbit") {
            const double dx=params["dx"].toDouble(),dy=params["dy"].toDouble();
            if(!params["dx"].isNumber()||!params["dy"].isNumber()||
               !finite({dx,dy})||std::abs(dx)>250||std::abs(dy)>250)
                throw std::runtime_error("FX cameraOrbit requires finite dx/dy within +/-250");
            return onScene([&] {
                fxPreview_->orbit(float(dx),float(dy));
                return fxPreview_->spectraPreviewCommand(Json::parse(R"({"action":"status"})"),project);
            });
        }
        const double amount=params["amount"].toDouble();
        if(!params["amount"].isNumber()||!finite({amount})||std::abs(amount)>250)
            throw std::runtime_error("FX cameraZoom amount must be finite within +/-250");
        return onScene([&] {
            fxPreview_->zoom(float(amount));
            return fxPreview_->spectraPreviewCommand(Json::parse(R"({"action":"status"})"),project);
        });
    }
    Json result=onScene([&]{return fxPreview_->spectraPreviewCommand(params,project);});
    if(action=="stop"||action=="replayRestore"||(action=="load"&&result.getBool("ready")&&result.getStr("error").empty()))
        cancelSpectraCapture();
    return result;
}
void StudioHost::applyViewport(const Json& params) {
    const std::string id = params.getStr("id");
    ViewportRect rect;
    rect.x = params["x"].toDouble(); rect.y = params["y"].toDouble();
    rect.width = params["width"].toDouble(); rect.height = params["height"].toDouble();
    rect.dpr = params["dpr"].toDouble(1); rect.visible = params.getBool("visible");
    if (id == "game") {
        NativeViewportPlacement placed;
        if (!platform_.placeSurface(StudioSurface::Game, rect, placed)) return;
        gamePlacement_ = placed;
        if (gamePlacement_.visible) platform_.gameDeviceChanged(gameState());
        else if (game_) game_->suspend();
        return;
    }
    if(id=="importedClip"||id=="sourceModel"||id=="gpuEmitter"||id=="effectFile"||id=="lighting"||id=="shader"||id=="material"||id=="facialRig"||id=="character"||id=="appearance"||id=="animationGraph"||id=="animationRig"||id=="attachments"||id=="lensCamera") {
        const auto surface=id=="importedClip"?StudioSurface::ImportedClip:id=="sourceModel"?StudioSurface::SourceModel:id=="gpuEmitter"?StudioSurface::GpuEmitter:id=="effectFile"?StudioSurface::EffectFile:id=="lighting"?StudioSurface::Lighting:id=="lensCamera"?StudioSurface::LensCamera:id=="attachments"?StudioSurface::Attachments:id=="animationRig"?StudioSurface::AnimationRig:id=="animationGraph"?StudioSurface::AnimationGraph:id=="character"?StudioSurface::Character:id=="appearance"?StudioSurface::Appearance:id=="facialRig"?StudioSurface::FacialRig:id=="shader"?StudioSurface::Shader:StudioSurface::Material;
        auto& asset=assetSurface(surface);
        NativeViewportPlacement placed;
        if(platform_.placeSurface(surface,rect,placed)){
            asset.placement=placed;
            if(!placed.visible){asset.error.clear();if(surface!=StudioSurface::ImportedClip&&surface!=StudioSurface::SourceModel&&surface!=StudioSurface::GpuEmitter&&surface!=StudioSurface::EffectFile&&surface!=StudioSurface::LensCamera&&surface!=StudioSurface::Attachments&&asset.renderer)onScene([&]{asset.renderer.reset();});}
        }
        return;
    }
    if(id=="fx") {
        NativeViewportPlacement placed;
        if(!platform_.placeSurface(StudioSurface::Fx,rect,placed))return;
        fxPlacement_=placed;
        fxSurfaceVisible_=placed.visible;
        return;
    }
    if(id!="scene")return;
    NativeViewportPlacement placed;
    if (!platform_.placeSurface(StudioSurface::Scene, rect, placed)) return;
    placement_ = placed;
    if (!placement_.visible) {finishScenePick(Json::object());readyEmitted_ = false;if(!projectLoading_&&session_->strokeCancel())sendEvent("document.changed", "");mouse_={};}
}
Json StudioHost::viewportInput(const Json& input) {
    const std::string kind = input.getStr("kind");
    const auto target=input.getStr("surface");
    if(target=="character"||target=="appearance"){
        const auto surface=target=="character"?StudioSurface::Character:StudioSurface::Appearance;
        const auto& rect=assetSurface(surface).placement;
        if(kind=="reference"||kind=="brushCursor"){platform_.humanReference(surface,input);return Json::object();}
        if(!rect.visible||!platform_.surfaceDrawable(surface))return Json::object();
        if(kind=="orbit")humanOrbit(surface,input["dx"].toDouble(),input["dy"].toDouble());
        else if(kind=="zoom")humanZoom(surface,input["amount"].toDouble());
        else if(kind=="pick"){
            const auto x=input["x"].toDouble(),y=input["y"].toDouble();
            if(finite({x,y})&&x>=0&&y>=0&&x<=1&&y<=1)
                humanPick(surface,x*rect.width,y*rect.height,rect.width,rect.height);
        }else throw std::runtime_error("Invalid character viewport input");
        return Json::object();
    }
    if(kind=="focusLight") {
        const auto id=input.getStr("entityId");
        if(projectLoading_ || input.getStr("expectedDocumentEpoch")!=session_->graphDocumentScope().getStr("epoch") ||
           id.rfind("light:",0)!=0 || id!=session_->selectedObjectId())
            throw std::runtime_error("Select the light again before showing it in Scene");
        const auto& map=session_->workspace().map;
        if(map.data.planet())throw std::runtime_error("Scene light framing requires a planar map");
        for(size_t i=0;i<map.data.extra["lights"].size();++i) {
            const auto ref=editor::DocumentEntityRef::light(i,map.data.worldSpace);
            if(studioDocumentId(map,ref)!=id)continue;
            editor::DocumentTransform position;
            if(!editor::readDocumentTransform(map,ref,position))break;
            ensureScene();
            onScene([&]{preview_->requestLightFocus({float(position.x),float(position.y),float(position.z)});});
            return Json::object();
        }
        throw std::runtime_error("The selected light is no longer available");
    }
    Json result = Json::object();
    // Pointer/keyboard navigation comes from HTML only in composite mode (the native view handles it
    // otherwise); toolbar commands work in both.
    const bool toolbarCommand = kind == "focus" || kind == "reset" || kind == "view" || kind == "projection" || kind == "tool" ||
                                kind == "hover" || kind == "frameAll" || kind == "camera";
    if ((!htmlComposite_ && !toolbarCommand) || !placement_.visible || !preview_ || !platform_.surfaceDrawable(StudioSurface::Scene))
        throw std::runtime_error("Viewport input unavailable");
    auto delta = [&](const char* a, const char* b, double limit, const char* what) {
        const double x = input[a].toDouble(), y = input[b].toDouble();
        if (!input[a].isNumber() || !input[b].isNumber() || !finite({x, y}) || std::abs(x) > limit || std::abs(y) > limit)
            throw std::runtime_error(what);
        return std::pair<double, double>{x, y};
    };
    auto ok = [&] { result.set("ok", Json::fromBool(true)); return result; };
    auto realBool = [&](const char* k) { return input[k].boolean(false) == input[k].boolean(true); };
    if (kind == "orbit") { auto [dx, dy] = delta("dx", "dy", 1000, "Invalid orbit delta"); orbit(dx, dy); return ok(); }
    if (kind == "look") { auto [dx, dy] = delta("dx", "dy", 1000, "Invalid look delta"); look(dx, dy); return ok(); }
    if (kind == "pan") { auto [dx, dy] = delta("dx", "dy", 1000, "Invalid pan delta"); pan(dx, dy); return ok(); }
    if (kind == "move") {
        const double r = input["right"].toDouble(), u = input["up"].toDouble(), f = input["forward"].toDouble();
        if (!input["right"].isNumber() || !input["up"].isNumber() || !input["forward"].isNumber() || !finite({r, u, f}) ||
            std::abs(r) > 50 || std::abs(u) > 50 || std::abs(f) > 50) throw std::runtime_error("Invalid camera movement");
        moveCamera(r, u, f);
        return ok();
    }
    if (kind == "focus") { focusSelection(); return ok(); }
    if (kind == "reset") { resetView(); return ok(); }
    if (kind == "view") {
        auto [yaw, pitch] = delta("yaw", "pitch", 100, "Invalid camera orientation");
        if (std::abs(pitch) > 1.5708) throw std::runtime_error("Invalid camera orientation");
        onScene([&] { preview_->setOrientation(float(yaw), float(pitch)); });
        return ok();
    }
    if (kind == "areaGesture" || kind == "handle") {
        const double x = input["x"].toDouble(), y = input["y"].toDouble(), w = input["width"].toDouble(), h = input["height"].toDouble();
        const std::string phase = input.getStr("phase");
        if (!input["x"].isNumber() || !input["y"].isNumber() || !input["width"].isNumber() || !input["height"].isNumber() ||
            !finite({x, y, w, h}) || w < 2 || h < 2 || w > 20000 || h > 20000) throw std::runtime_error("Invalid move coordinates");
        if(kind=="areaGesture"){
            if(!pickingArea_ || input.getStr("token")!=scenePickerToken_)return ok();
            if(phase=="down")updateAreaPick(0,x,y,w,h);
            else if(phase=="drag" || phase=="up"){
                if(areaOrigin_)updateAreaPick(phase=="up"?2:1,x,y,w,h);
            }else throw std::runtime_error("Invalid area gesture phase");
            return ok();
        }
        if (phase == "down") { result.set("hit", Json::fromBool(moveBegin(x, y, w, h))); return result; }
        if (phase == "drag") { moveUpdate(x, y, w, h, input["invertSnap"].boolean(false)); return ok(); }
        if (phase == "up") { moveEnd(); return ok(); }
        throw std::runtime_error("Invalid move phase");
    }
    if (kind == "hover") {
        // Hierarchy row hover highlights the object in the Scene.
        const std::string id = input["id"].isString() ? input["id"].str() : "";
        if (id.size() > 256) throw std::runtime_error("Invalid hover id");
        hoverId_ = id;
        return ok();
    }
    if (kind == "frameAll") { frameAll(); return ok(); }
    if (kind == "camera") {
        // Camera bookmarks: read the Scene camera, or tween to a stored one.
        const auto& set = input["set"];
        if (set.isObject()) {
            const double x = set["x"].toDouble(), y = set["y"].toDouble(), z = set["z"].toDouble(), dist = set["distance"].toDouble(),
                         yaw = set["yaw"].toDouble(), pitch = set["pitch"].toDouble();
            for (double v : {x, y, z, dist, yaw, pitch}) if (!std::isfinite(v) || std::abs(v) > 1e6) throw std::runtime_error("Invalid camera");
            const double fov = set["fov"].toDouble();
            if (set.has("fov") && (!set["fov"].isNumber() || !std::isfinite(fov) || fov <= 0 || fov >= 3.141592653589793))
                throw std::runtime_error("Invalid camera field of view");
            onScene([&] {
                if (set.has("fov")) preview_->setFieldOfView(float(fov));
                preview_->setOrthographic(set.getBool("ortho"));
                preview_->tweenCamera({float(x), float(y), float(z)}, float(std::clamp(dist, 0.5, 100000.0)), float(yaw), float(pitch));
            });
        }
        const auto cam = onScene([&] { return preview_->cameraState(); });
        result.set("x", Json::number(double(cam.target.x), 3)); result.set("y", Json::number(double(cam.target.y), 3));
        result.set("z", Json::number(double(cam.target.z), 3)); result.set("distance", Json::number(double(cam.distance), 3));
        result.set("yaw", Json::number(double(cam.yaw), 5)); result.set("pitch", Json::number(double(cam.pitch), 5));
        result.set("ortho", Json::fromBool(cam.ortho));
        result.set("fov", Json::number(double(cam.fov), 9));
        return result;
    }
    if (kind == "tool") {
        // Scene toolbar: tool, Global/Local, snapping, gizmo visibility.
        static const char* names[4] = {"view", "move", "rotate", "scale"};
        const std::string t = input.getStr("tool");
        onScene([&] {
            for (int i = 0; i < 4; ++i) if (t == names[i]) preview_->setTransformTool(StudioScene::TransformTool(i));
            if (realBool("local")) preview_->setLocalSpace(input["local"].boolean());
            if (realBool("gizmos")) preview_->setGizmosVisible(input["gizmos"].boolean());
        });
        if (realBool("surface")) surfaceSnap_ = input["surface"].boolean();
        if (realBool("snap"))
            onScene([&] { preview_->setSnap(input["snap"].boolean(), float(input["snapMove"].toDouble(0)),
                              float(input["snapRotate"].toDouble(0)), float(input["snapScale"].toDouble(0))); });
        result.set("tool", Json::string(names[int(onScene([&] { return preview_->transformTool(); }))]));
        return ok();
    }
    if (kind == "projection" && realBool("ortho")) { onScene([&] { preview_->setOrthographic(input["ortho"].boolean()); }); return ok(); }
    if (kind == "zoom" && input["amount"].isNumber()) {
        const double wheel = input["amount"].toDouble();
        if (!std::isfinite(wheel) || std::abs(wheel) > 1000) throw std::runtime_error("Invalid zoom delta");
        zoom(wheel);
        return ok();
    }
    if (kind == "pick") {
        const double x = input["x"].toDouble(), y = input["y"].toDouble(), w = input["width"].toDouble(), h = input["height"].toDouble();
        if (!input["x"].isNumber() || !input["y"].isNumber() || !input["width"].isNumber() || !input["height"].isNumber() ||
            !finite({x, y, w, h}) || w < 2 || h < 2 || w > 20000 || h > 20000 || x < 0 || y < 0 || x > w || y > h)
            throw std::runtime_error("Invalid pick coordinates");
        if (input.has("roadToken")) {
            if (pickingRoad_ && input.getStr("roadToken")==scenePickerToken_)
                roadPick(x,y,w,h,input.getBool("chain"));
        } else if (!pickingRoad_) select(x, y, w, h, "replace");
        return ok();
    }
    throw std::runtime_error("Unsupported viewport input");
}
void StudioHost::clearPlayReviews() {
    playReview_ = Json();
    agentPlayReview_ = Json();
}

Json StudioHost::localPlayOperation(const std::string& method, const Json& params, Json& reviewSlot) {
    if (method == "studio.gameProfile") {
        const auto& project=session_->workspace().project.dir;
        if(project.empty()||params.getStr("expectedProject")!=project)
            throw std::runtime_error("Simulation Profiler project changed");
        const auto action=params.getStr("action");
        if(action!="status"&&action!="start"&&action!="stop")throw std::runtime_error("Invalid profiler action");
        uint32_t steps=0;
        if(action=="start"){
            const auto n=params["steps"].toDouble();
            if(!params["steps"].isNumber()||!(n>=1&&n<=120)||double(int(n))!=n)
                throw std::runtime_error("Invalid profile capture size");
            steps=uint32_t(n);
        }
        if(game_)return game_->profile(action,steps);
        if(action!="status")throw std::runtime_error("No local match is running");
        Json out=Json::object();out.set("available",Json::fromBool(false));
        out.set("epoch",Json::string("0"));out.set("reason",Json::string("No local match is running"));return out;
    }
    if (method == "studio.gameRegions") {
        const auto& project=session_->workspace().project.dir;
        if(project.empty()||params.getStr("expectedProject")!=project)
            throw std::runtime_error("World Explorer project changed; refresh the panel");
        if(game_)return game_->regions();
        Json out=Json::object();
        out.set("active",Json::fromBool(false));out.set("available",Json::fromBool(false));
        out.set("epoch",Json::string("0"));out.set("tick",Json::number(int64_t(0)));
        out.set("reason",Json::string("No local match is running"));return out;
    }
    if(method=="studio.gameEdit"){
        const auto& project=session_->workspace().project.dir;
        if(project.empty()||params.getStr("expectedProject")!=project)
            throw std::runtime_error("Runtime edit project changed");
        if(!game_)throw std::runtime_error("No local game running");
        return game_->editRuntime(params);
    }
    if (method == "studio.gameSelectEntity" || method == "studio.gameEntities" || method == "studio.gameInspect") {
        // Older clients omit expectedProject. Entity Debugger always sends it.
        if(params.has("expectedProject")&&(session_->workspace().project.dir.empty()||
            params.getStr("expectedProject")!=session_->workspace().project.dir))
            throw std::runtime_error("Runtime inspection project changed");
        if(method=="studio.gameSelectEntity"){
            if(!game_ || !params.has("expectedProject"))throw std::runtime_error("Select an entity in an open game");
            game_->selectEntity(params.getStr("epoch"),params.getStr("ref"));
            return gameState();
        }
        if (method == "studio.gameEntities") {
            if (!params["offset"].isNumber() || !params["limit"].isNumber() || !params["query"].isString() ||
                params["offset"].toDouble() < 0 || params["offset"].toDouble() > 10000000 ||
                params["limit"].toDouble() < 1 || params["limit"].toDouble() > 100) throw std::runtime_error("Invalid runtime page");
            const auto offset = int(params["offset"].integer()), limit = int(params["limit"].integer());
            StudioRuntimeInspect::GroupQuery group;
            if(params.has("group")){
                group.config=studioHierarchyConfig(session_->workspace().project.manifest);
                const int index=params.getInt("group",-1);
                if(index<0 || size_t(index)>=group.config.groupings.size() || !params["path"].isArray())
                    throw std::runtime_error("Unknown live hierarchy group");
                group.keys=group.config.groupings[size_t(index)].by;
                if(group.keys==std::vector<std::string>{"parent"}){
                    if(params["path"].size())throw std::runtime_error("Invalid nesting path");
                    if(params.has("collapsed") && !params["collapsed"].isArray())throw std::runtime_error("Invalid collapsed entities");
                    for(const auto& value:params["collapsed"].items()){
                        uint64_t ref=0;
                        const auto text=value.str();
                        auto [end,error]=std::from_chars(text.data(),text.data()+text.size(),ref);
                        if(error!=std::errc{} || end!=text.data()+text.size() || !ref)throw std::runtime_error("Invalid collapsed entity");
                        group.collapsed.insert(ref);
                    }
                }
                if(params["path"].size()>group.keys.size())throw std::runtime_error("Invalid live hierarchy depth");
                for(const auto& part:params["path"].items()){
                    if(!part.isString())throw std::runtime_error("Invalid live hierarchy path");
                    group.path.push_back(part.str());
                }
            }
            if (game_) return game_->entities(offset, limit, params.getStr("query"),group);
            StudioRuntimeInspect empty;
            return empty.entities(nullptr, "0", offset, limit, params.getStr("query"));
        }
        if (!game_) throw std::runtime_error("No game is running");
        return game_->inspect(params.getStr("epoch"), params.getStr("ref"));
    }
    if(method=="studio.previewPlayChanges"){
        reviewSlot=Json();
        if(!game_||!game_->canReview(session_->workspace()))
            throw std::runtime_error("Pause a local saved-map session with unchanged source before reviewing changes");
        if(params.getStr("expectedProject")!=session_->workspace().project.dir)
            throw std::runtime_error("Play review project changed");
        for(const char* flag:{"positions","amounts","removeDead"})
            if(params[flag].type()!=Json::Type::Bool)throw std::runtime_error("Invalid play review options");
        const auto states=game_->mapEntityStates();
        Json p=Json::object(),r=Json::object();
        p.set("entities",states["entities"]);p.set("positions",params["positions"]);
        p.set("amounts",params["amounts"]);p.set("removeDead",params["removeDead"]);p.set("dryRun",Json::fromBool(true));
        r.set("id",Json::number(int64_t(0)));
        r.set("method",Json::string("studio.keepPlayChanges"));r.set("params",p);
        const auto preview=session_->dispatch(r);
        if(preview.has("error"))throw std::runtime_error(preview.getStr("error"));
        const auto token=editor::net::randomToken();
        reviewSlot.set("token",Json::string(token));reviewSlot.set("epoch",game_->state()["epoch"]);
        reviewSlot.set("states",states);p.erase("dryRun");reviewSlot.set("params",p);
        auto out=preview["result"];out.set("token",Json::string(token));out.set("tick",states["tick"]);return out;
    }
    if (method == "studio.keepPlayChanges") {
        if (params.getStr("token").empty() || params.getStr("token") != reviewSlot.getStr("token"))
            throw std::runtime_error("Play review expired; capture a new preview");
        const auto review = std::move(reviewSlot);
        reviewSlot = Json();
        if (!game_ || !game_->canReview(session_->workspace()) ||
            params.getStr("expectedProject") != session_->workspace().project.dir ||
            game_->state()["epoch"] != review["epoch"] || game_->mapEntityStates() != review["states"])
            throw std::runtime_error("Play review expired; capture a new preview");
        Json request = Json::object();
        request.set("id", Json::number(int64_t(0)));
        request.set("method", Json::string("studio.keepPlayChanges"));
        request.set("params", review["params"]);
        const auto reply = session_->dispatch(request);
        if (reply.has("error")) throw std::runtime_error(reply.getStr("error"));
        clearPlayReviews();
        return reply["result"];
    }
    throw std::invalid_argument("Unknown local Studio play operation");
}

// Methods the host answers itself (windows, native views, game, dialogs). False: StudioSession's.
bool StudioHost::handleHostMethod(const Json& req) {
    const std::string method = req.getStr("method");
    const bool hasId = req["id"].isNumber();
    const Json& params = req["params"];
    auto answer = [&](auto&& body) {
        Json reply = Json::object();
        reply.set("id", req["id"]);
        try { reply.set("result", body()); }
        catch (const std::exception& e) { reply.set("error", Json::string(e.what())); }
        sendReply(reply);
        return true;
    };
    if (method == "studio.windowDrag" || method == "studio.windowZoom" ||
        method == "studio.windowMinimize" || method == "studio.windowResize") {
        try { (void)platform_.windowRequest(method, params); } catch (const std::exception&) {}
        return true;
    }
    if (method == "studio.webDiagnostic") {
        const std::string note = params.getStr("message");
        if (note.size() <= 1200) platform_.log("[studio-web] JS: " + note);
        return true; // diagnostics never touch Workspace or the filesystem
    }
    if(method=="studio.placementGhost") {
        const auto scope=session_->graphDocumentScope();
        try {
            if(scope.getStr("epoch")!=params.getStr("expectedDocumentEpoch") ||
               scope.getStr("revision")!=params.getStr("expectedDocumentRevision"))
                throw std::runtime_error("Map changed; refresh placement");
            auto& ws=session_->workspace();
            const auto patch=ws.call("overlay_placement_preview",params);
            if(patch.has("error"))throw std::runtime_error(patch.getStr("error"));
            if(!patch["rows"].size())throw std::runtime_error("Choose a structure on a planar map");
            auto ghost=std::make_shared<StudioPlacementGhost>(ws,patch,params.getStr("placeType"));
            auto reply=std::make_shared<Json>(Json::object());
            platform_.runInBackground([ghost,reply]{
                try {
                    const auto pixels=ghost->frame();
                    const auto png=encodePng(pixels,StudioPanelRenderTarget::Side,StudioPanelRenderTarget::Side);
                    if(png.empty())throw std::runtime_error("Placement model capture failed");
                    reply->set("url",Json::string("data:image/png;base64,"+editor::base64Encode(png)));
                } catch(const std::exception& e){reply->set("error",Json::string(e.what()));}
            },[this,alive=alive_,id=req["id"],scope,reply]{
                if(!*alive)return;
                if(!sameDocumentScope(scope,session_->graphDocumentScope()))sendError(id,"Map changed; refresh placement");
                else if(reply->has("error"))sendError(id,reply->getStr("error"));
                else {Json response=Json::object();response.set("id",id);response.set("result",*reply);sendReply(response);}
            });
        } catch(const std::exception& e){sendError(req["id"],e.what());}
        return true;
    }
    if(method=="studio.lightBakeJob")return answer([&]{return lightBakeJob(params);});
    if(method=="studio.lightBakeDisplay")return answer([&]{
        if(params.has("enabled")){
            if(params["enabled"].type()!=Json::Type::Bool)throw std::runtime_error("Expected baked lighting visibility");
            showSceneBake_=params.getBool("enabled");
            if(preview_)onScene([&]{preview_->setBakedSceneEnabled(showSceneBake_);});
            sendEvent("lightBake.displayChanged","");
        }
        Json out=preview_?onScene([&]{return preview_->bakedSceneStatus();}):Json::object();
        out.set("enabled",Json::fromBool(showSceneBake_));
        return out;
    });
    if(method=="studio.lightBakeAtlas" && hasId){
        const auto& ws=session_->workspace();
        const auto map=editor::lightBakeMapRel(ws);
        const auto epoch=params.getStr("expectedDocumentEpoch");
        if(projectLoading_ || map.empty() || epoch!=session_->graphDocumentScope().getStr("epoch")){
            sendError(req["id"],"Map changed. Reopen the atlas.");return true;
        }
        if(atlasPreviewBusy_){sendError(req["id"],"Another atlas page is loading.");return true;}
        auto reply=std::make_shared<Json>(Json::object());reply->set("id",req["id"]);
        atlasPreviewBusy_=true;
        platform_.runInBackground([project=ws.project.dir,map,params,reply]{
            try { reply->set("result",editor::lightBakeAtlasPreview(project,map,params)); }
            catch(const std::exception& e){reply->set("error",Json::string(e.what()));}
        },[this,alive=alive_,reply,epoch]{
            if(!*alive)return;
            atlasPreviewBusy_=false;
            if(epoch!=session_->graphDocumentScope().getStr("epoch"))sendError((*reply)["id"],"Map changed. Reopen the atlas.");
            else sendReply(*reply);
        });
        return true;
    }
    if(method=="studio.lightingPreview")return answer([&]{
        const auto& ws=session_->workspace();
        const auto epoch=session_->graphDocumentScope().getStr("epoch");
        if(ws.project.dir.empty() || epoch!=params.getStr("expectedDocumentEpoch"))
            throw std::runtime_error("Map changed; reopen Lighting");
        auto patch=studioMapLightingPatch(params);
        if(params.getStr("key")!="weather")throw std::runtime_error("Expected preview weather");
        const float time=float(patch["time"].toDouble()), clouds=float(patch["clouds"].toDouble()), storm=float(patch["storm"].toDouble());
        auto lights=std::make_unique<game::GameLights>();
        const auto block=ws.map.data.worldSpace?ws.map.toJson()["lighting"]:ws.map.data.extra["lighting"];
        lights->configure(ws.project.manifest["lighting"],block);
        lights->setPreview(true,time,clouds,storm);
        if(!weatherLights_ || weatherEpoch_!=epoch)weatherStart_=platform_.now();
        weatherLights_=std::move(lights);weatherEpoch_=epoch;
        weatherDay_.clouds=std::max(clouds,.85f*storm);
        return Json::object();
    });
    if(method=="studio.worldProcessGroupPivot")return answer([&]{
        Json result=Json::object();
        const auto selection=session_->selectedObjectIds();
        render::float3 centroid{};
        if(!worldprocess_edit::groupCentroid(session_->workspace().map,
                                             selection,centroid))
            throw std::runtime_error("Select at least two EVOLVE regions");
        if(selection!=wpPivotSelection_){
            wpPivotSelection_=selection;
            wpCustomPivot_=false;
        }
        const auto pivot=wpCustomPivot_?wpGroupPivot_:centroid;
        result.set("pivotXcm",Json::number(
            std::int64_t(std::llround(double(pivot.x)*100.))));
        result.set("pivotZcm",Json::number(
            std::int64_t(std::llround(double(pivot.z)*100.))));
        result.set("custom",Json::fromBool(wpCustomPivot_));
        return result;
    });
    if(method=="studio.worldProcessDiagnostics")return answer([&]{
        const auto cap=params.has("budget")?params["budget"].integer(-1):64;
        if(cap<0||cap>512||(params.has("budget")&&(!params["budget"].isNumber()||
            params["budget"].fx()!=Fixed::fromInt(cap))))
            throw std::invalid_argument("Diagnostic budget must be an integer 0..512");
        if(params.has("focus")&&params["focus"].type()!=Json::Type::String)
            throw std::invalid_argument("Diagnostic focus must be an entity reference string");
        if(game_)return game_->worldProcessDiagnostics(int(cap),params.getStr("focus"));
        Json out=Json::object();out.set("active",Json::fromBool(false));return out;
    });
    if(method=="studio.planetSceneBody")return answer([&]{return planetSceneBodyRequest(params);});
    if(method=="studio.playerStartPick" || method=="studio.areaPick" || method=="studio.roadPick")return answer([&]{
        const auto token=params.getStr("token");
        if(token.empty()||token.size()>128)throw std::runtime_error("Invalid Scene picker token");
        if(params.getBool("armed")){
            if(projectLoading_||!session_||!preview_||!placement_.visible||!platform_.surfaceDrawable(StudioSurface::Scene))
                throw std::runtime_error("Open the Scene view to pick map coordinates.");
            if(brushArmed()||session_->strokeActive()||mouse_.active||wpDrag_.active||forestPlantArm_.isObject()||
               onScene([&]{return preview_->caveGlobeRequested();}))throw std::runtime_error("Finish the active Scene tool first.");
            finishScenePick(Json::object());
            if(method=="studio.areaPick"){
                const auto& map=session_->workspace().map.data;
                if(map.planet()||map.worldSpace)throw std::runtime_error("Area drawing requires a flat grid map.");
                areaScope_=session_->graphDocumentScope();pickingArea_=true;
            }
            if(method=="studio.roadPick"){
                const auto& map=session_->workspace().map.data;
                const auto current = session_->graphDocumentScope();
                if (map.planet() || map.worldSpace || !sameDocumentScope(params["scope"],current))
                    throw std::runtime_error("Choose a current grid map to edit roads.");
                roadScope_=current;pickingRoad_=true;
                Json armed=Json::object();armed.set("token",Json::string(token));
                sendEventPayload("roadPick.armed",armed);
            }
            scenePickerToken_=token;
            if(pickingArea_ && htmlComposite_){
                Json armed=Json::object();armed.set("token",Json::string(token));
                sendEventPayload("area.armed",armed);
            }
        }else if(scenePickerToken_==token)finishScenePick(Json::object());
        return Json::object();
    });
    if(method=="studio.planetForestArm")return answer([&]{
        Json out=Json::object();forestPlantArm_=Json{};
        if(preview_)onScene([&]{preview_->setForestDragPreview(Json{});});
        if(!params.getBool("armed",false)&&preview_)onScene([&]{preview_->setForestSelection(Json{});});
        if(params.getBool("armed",false)){
            if(projectLoading_||!session_||!preview_||session_->strokeActive()||brushArmed())
                throw std::runtime_error("Finish the active Scene tool first");
            const auto& ws=session_->workspace();
            if(!ws.map.data.planet()||params.getStr("mapPath")!=ws.map.path||
               params.getStr("expectedRevision")!=std::to_string(ws.map.revision))
                throw std::runtime_error("Reload the current forest document");
            const auto action=params.getStr("action","plant");
            if(action!="plant"&&action!="pick"&&action!="move")throw std::runtime_error("Invalid forest Scene action");
            if(action!="pick"&&(!params["species"].isNumber()||params["species"].integer()<1||params["species"].integer()>1000000||
               params["species"].fx()!=Fixed::fromInt(params["species"].integer())))throw std::runtime_error("Invalid forest species");
            if(action=="plant"&&params.getStr("habit")!="tree"&&params.getStr("habit")!="shrub")
                throw std::runtime_error("Invalid planting habit");
            if(action=="move"&&(!params["key"].isNumber()||params["key"].integer()<1||params["key"].integer()>1000000000||
               params["key"].fx()!=Fixed::fromInt(params["key"].integer())))throw std::runtime_error("Invalid plant key");
            if(wpLiveOptions_.live&&wpLiveOptions_.forest)throw std::runtime_error("Disable live forest before authoring");
            if(mouse_.active||wpDrag_.active)throw std::runtime_error("Finish the active Scene gesture first");
            const auto sceneBody=worldProcessSceneBody();if(sceneBody.empty())throw std::runtime_error("Wait for a current planetary Scene");
            finishScenePick(Json::object());
            forestPlantArm_=params;forestPlantArm_.set("project",Json::string(ws.project.dir));forestPlantArm_.set("sceneBody",Json::string(sceneBody));
        }
        out.set("armed",Json::fromBool(forestPlantArm_.isObject()));return out;
    });
    if(method=="studio.physisLive")return answer([&] {
        const auto& project=session_->workspace().project.dir;
        if(project.empty() || projectLoading_ || params.getStr("expectedProject")!=project)
            throw std::runtime_error("PHYSIS project changed; reopen the panel");
        if(game_ && game_->launchPlan().project==project) return game_->physisFrame(params);
        Json out=Json::object();out.set("active",Json::fromBool(false));return out;
    });
    if(method=="studio.planetForestLive")return answer([&]{return planetForestLiveRequest();});
    if(method=="studio.evolveState")return answer([&]{
        // Browser polling can reuse the same completed-tick geometry sampled
        // for the native Scene. No duplicate heavy GRANULAR query.
        if(wpLiveOptions_.live&&game_&&worldProcessPlanetMapCurrent()&&game_->planetSnapshotCurrent(wpLiveSnapshot_))return wpLiveSnapshot_;
        if(wpLiveOptions_.live&&game_&&game_->planetMatch()&&!worldProcessPlanetMapCurrent()){
            Json out=Json::object();out.set("active",Json::fromBool(false));out.set("regions",Json::array());return out;
        }
        if(game_)return game_->ecologyState();
        Json out=Json::object();out.set("active",Json::fromBool(false));
        out.set("regions",Json::array());return out;
    });
    if(method=="studio.evolveLayer")return answer([&]{
        const auto next=wpLiveOptions_.patched(params);
        if(next.live&&next.forest&&forestPlantArm_.isObject()){
            forestPlantArm_=Json{};mouse_={};if(preview_)onScene([&]{preview_->setForestDragPreview(Json{});});
            Json cancelled=Json::object();cancelled.set("cancelled",Json::fromBool(true));sendEventPayload("planetForest.planted",cancelled);
        }
        if(next!=wpLiveOptions_){
            wpLiveOptions_=next;
            wpLiveSnapshot_=sampleWorldProcessLive();
            wpLiveRefresh_=platform_.now();
            rebuildOverlay();
        }
        return wpLiveOptions_.json();
    });
    if(method=="studio.worldProcessAreaPreview")return answer([&]{
        const auto selected=session_->selectedObjectIds();
        std::string selectionKey;
        for(const auto& id:selected){selectionKey+=id;selectionKey.push_back('|');}
        const auto revision=params["revision"].integer(-1);
        const auto owner=params.getStr("owner");
        if(owner.empty()||owner.size()>128)
            throw std::runtime_error("Invalid EVOLVE preview owner");
        if(revision<0)throw std::runtime_error("Missing EVOLVE preview revision");
        // A release is allowed even after the selection changed, but
        // cannot release another window's active preview.
        if(params.getStr("selectionKey")!=selectionKey && !params.getBool("clear"))
            throw std::runtime_error("EVOLVE preview selection changed");
        if(owner!=wpPreviewOwner_ && params.getBool("clear")){
            Json ignored=Json::object();
            ignored.set("stale",Json::fromBool(true));
            return ignored;
        }
        if(owner==wpPreviewOwner_ && revision<wpPreviewRevision_){
            Json ignored=Json::object();
            ignored.set("stale",Json::fromBool(true));
            return ignored;
        }
        wpPreviewOwner_=owner;
        wpPreviewRevision_=revision;
        wpPreviewSelection_=selectionKey;
        if(params.getBool("clear")||selected.size()<2){
            wpNumericPreview_=Json();
            wpPreviewOwner_.clear();
            wpPreviewSelection_.clear();
        }else{
            const auto& sx=params["scaleNumerator"];
            const auto& sz=params["scaleNumeratorZ"];
            const auto pivot=params.getStr("pivot");
            if(!sx.isNumber()||!sz.isNumber()||
               sx.integer()<1||sx.integer()>1000000||
               sz.integer()<1||sz.integer()>1000000||
               (pivot!="shared"&&pivot!="individual"&&pivot!="custom")||
               (pivot=="custom"&&(!params["pivotXcm"].isNumber()||
                                   !params["pivotZcm"].isNumber())))
                throw std::runtime_error("Invalid numeric EVOLVE preview");
            for(const auto& id:selected)
                if(session_->lockedIds().contains(id)||session_->hiddenIds().contains(id))
                    throw std::runtime_error("EVOLVE preview requires editable regions");
            if(pivot=="custom"){
                const auto px=params["pivotXcm"].integer();
                const auto pz=params["pivotZcm"].integer();
                if(px < -evolve::kAreaCoordinateCm || px > evolve::kAreaCoordinateCm ||
                   pz < -evolve::kAreaCoordinateCm || pz > evolve::kAreaCoordinateCm ||
                   params["pivotXcm"].numberLiteral()!=std::to_string(px) ||
                   params["pivotZcm"].numberLiteral()!=std::to_string(pz))
                    throw std::runtime_error("Invalid custom EVOLVE preview pivot");
                wpGroupPivot_={
                    float(params["pivotXcm"].integer())/100.f,.65f,
                    float(params["pivotZcm"].integer())/100.f};
                wpCustomPivot_=true;
                wpPivotSelection_=selected;
            }
            wpNumericPreview_=params;
        }
        rebuildOverlay();
        Json result=Json::object();
        result.set("ok",Json::fromBool(true));
        return result;
    });
    if (!hasId) return false;
    if(method=="studio.buildingGraphOverlay")return answer([&]{
        const auto raw=params.getStr("sceneId");
        if(raw.empty()){buildingGraphOverlayScene_=0;rebuildOverlay();}
        else{
            if(!preview_)throw std::runtime_error("Open the native Scene viewport");
            if(raw.size()>19||raw[0]=='0')throw std::runtime_error("Invalid scene ID");
            uint64_t id=0;
            for(char c:raw){if(c<'0'||c>'9'||id>(uint64_t(INT64_MAX)-uint64_t(c-'0'))/10)throw std::runtime_error("Invalid scene ID");id=id*10+uint64_t(c-'0');}
            const auto snap=session_->buildingGraphSnapshot(id);
            if(!snap["lastChange"].isObject())throw std::runtime_error("No committed building invalidation snapshot");
            const auto layers=params.getInt("layers",1);
            if(layers<1||layers>31)throw std::runtime_error("Invalid graph overlay layers");
            buildingGraphOverlayLayers_=uint32_t(layers);
            buildingGraphOverlayScene_=id;rebuildOverlay();
        }
        Json out=Json::object();out.set("active",Json::fromBool(buildingGraphOverlayScene_!=0));return out;
    });
    if(method=="studio.buildingPreview")return answer([&]{
        if(projectLoading_||buildingPreviewPending_)throw std::runtime_error("Building preview is busy");
        const auto& project=session_->workspace().project.dir;
        if(project.empty())throw std::runtime_error("Open a project before preview");
        auto model=studioBuildingPreviewCpuModel(fs::path(session_->workspace().engineDir),fs::path(project),params);
        ++buildingPreviewToken_;buildingPreviewActive_=false;
        if(!buildingPreviewThread_)buildingPreviewThread_=std::make_unique<StudioRenderThread>([this]{renderBuildingPanelFrame();});
        const BuildingPreviewJob job{req["id"],buildingPreviewToken_,.65f,.42f,1.f,0.f,0.f};
        auto result=buildingPreviewThread_->invoke([&]{
            platform_.sceneThreadScope([&]{
                if(!buildingPanelPreview_)buildingPanelPreview_=std::make_unique<StudioBuildingPanelPreview>();
                buildingPanelPreview_->setModel(std::move(model));
            });
            return buildingPanelFrame(job);
        });
        buildingPreviewActive_=true;return result;
    });
    if(method=="studio.buildingPreviewView"){
        if(projectLoading_||!buildingPreviewActive_||!buildingPreviewThread_||
           params.getStr("token")!=std::to_string(buildingPreviewToken_)){
            sendError(req["id"],"Building preview is no longer current");return true;
        }
        if(buildingPreviewPending_){sendError(req["id"],"Building preview camera is busy");return true;}
        for(const char* key:{"yaw","pitch","zoom","panX","panY"})if(!params[key].isNumber()){
            sendError(req["id"],"Building preview camera values required");return true;
        }
        BuildingPreviewJob job{req["id"],buildingPreviewToken_,float(params["yaw"].toDouble()),float(params["pitch"].toDouble()),
            float(params["zoom"].toDouble()),float(params["panX"].toDouble()),float(params["panY"].toDouble())};
        if(!std::isfinite(job.yaw)||!std::isfinite(job.pitch)||!std::isfinite(job.zoom)||!std::isfinite(job.panX)||!std::isfinite(job.panY)||
           std::abs(job.yaw)>10000.f||std::abs(job.panX)>2.f||std::abs(job.panY)>2.f||job.zoom<.55f||job.zoom>2.5f){
            sendError(req["id"],"Invalid Building preview camera");return true;
        }
        buildingPreviewPending_=true;
        {std::lock_guard<std::mutex> lock(buildingPreviewMutex_);buildingPreviewJob_=std::move(job);}
        buildingPreviewThread_->kick();
        return true; // reply from UI tick after worker finishes
    }
    if(method=="studio.buildingPreviewStop")return answer([&]{
        if(params.getStr("token")==std::to_string(buildingPreviewToken_))retireBuildingPanelPreview();
        Json result=Json::object();result.set("active",Json::fromBool(false));return result;
    });
    if (method == "studio.windowOpen" || method == "studio.windowClose" || method == "studio.windowTitle")
        return answer([&] { return platform_.windowRequest(method, params); });
    if (method == "studio.windowBroadcast") return answer([&] {
        // UI-only notifications between Studio windows (no Workspace access).
        const std::string event = params.getStr("event");
        if (event.rfind("ui.", 0) != 0 || event.size() > 64) throw std::runtime_error("Invalid window event");
        sendEventPayload(event, params["payload"]);
        Json r = Json::object(); r.set("ok", Json::fromBool(true)); return r;
    });
    if (method == "studio.buildReveal") return answer([&] {
        // Show a build folder (or the last build log); the path is the session's, never JS-provided.
        const auto& ws = session_->workspace();
        if (ws.project.dir.empty()) throw std::runtime_error("Open a project first");
        const std::string target = params.getStr("target");
        static const std::regex valid("^(macos|ios|android|web|windows|linux)?$");
        if (!std::regex_match(target, valid)) throw std::runtime_error("Unknown build target");
        fs::path path = fs::path(ws.project.dir) / "Builds";
        if (!target.empty()) path /= target;
        if (params.getBool("log")) path = const_cast<editor::Workspace&>(ws).lastLogFile();
        if (params.getBool("logFolder")) path = ws.logsDir();
        std::error_code ec;
        if (path.empty() || !fs::exists(path, ec)) throw std::runtime_error("Nothing built there yet");
        platform_.revealInFileManager(path, params.getBool("logFolder"));
        Json r = Json::object(); r.set("ok", Json::fromBool(true)); return r;
    });
    if (method == "studio.humanMakeupAlign") return answer([&] {
        // Makeup layers placed from the shown face (needs the GPU preview's CPU copy).
        if (!activeHumanRenderer()) throw std::runtime_error("Покажите персонажа во вьюпорте");
        Json r = Json::object(); r.set("values", onScene([&] { return activeHumanRenderer()->alignMakeup(session_->facialPreviewBinding()); })); return r;
    });
    if (method == "studio.humanLookCapture") return answer([&] {
        // Preset card picture: the next character frame is cropped around the head and written beside the look.
        if (!activeHumanRenderer() || !session_->facialPreviewBinding().bound())
            throw std::runtime_error("Покажите персонажа во вьюпорте, чтобы снять превью");
        const auto path = studioHumanLookThumbnailFile(session_->workspace().project.dir, params.getStr("name"), true);
        onScene([&] { activeHumanRenderer()->captureFacialThumbnail(path.string()); });
        Json r = Json::object(); r.set("ok", Json::fromBool(true)); return r;
    });
    if (method == "studio.spectraPreview")
        return answer([&]{return runSpectraPreview(params);});
    if(method=="studio.networkPlay" || method=="studio.standalonePlay")return answer([&]{
        const auto& ws=session_->workspace();
        const bool standalone=method=="studio.standalonePlay";
        std::vector<std::string> args;
        if (standalone) {
            if (params.getStr("project")!=ws.project.dir) throw std::runtime_error("Project changed before launch");
            args={"--project",studioGameLaunch(ws,false).project};
        } else args=studioNetworkArguments(ws,params);
        auto status=platform_.launchGamePlayer(args);
        launchedPlayers_.push_back({ws.project.dir,standalone?"standalone":params.getStr("mode"),std::move(status)});
        return Json::object();
    });
    if (method == "studio.legacyCameraBookmarks") return answer([&] {
        const auto& project = session_->workspace().project.dir;
        if (project.empty() || project != params.getStr("project")) throw std::runtime_error("Project changed");
        const auto dir = platform_.legacyEditorSettingsDir();
        Json views = Json::array();
        if (dir.empty()) return views;
        const auto slots = editor::legacy_camera::read(editor::legacy_camera::bookmarkFile(dir, project));
        for (size_t i = 0; i < slots.size(); ++i) {
            const auto& c = slots[i];
            if (!c.valid) continue;
            Json view = Json::object();
            view.set("slot", Json::number(int64_t(i + 1)));
            view.set("name", Json::string("Imported view " + std::to_string(i + 1)));
            view.set("x", Json::number(double(c.target.x), 9));
            view.set("y", Json::number(double(c.target.y), 9));
            view.set("z", Json::number(double(c.target.z), 9));
            view.set("yaw", Json::number(double(c.yaw), 9));
            view.set("pitch", Json::number(double(c.pitch), 9));
            view.set("distance", Json::number(double(c.distance), 9));
            view.set("fov", Json::number(double(c.fov), 9));
            view.set("ortho", Json::fromBool(c.ortho));
            views.push(std::move(view));
        }
        return views;
    });
    if (method == "studio.tileAtlas") return answer([&] {
        if(projectLoading_)throw std::runtime_error("Wait for the project to open");
        const auto& ws=session_->workspace();
        const auto path=ws.map.data.extra["tiles"].getStr("tileset");
        if(ws.project.dir.empty() || params.getStr("tileset")!=path ||
           params.getStr("project")!=ws.project.dir)throw std::runtime_error("The map tileset changed");
        std::string definition;
        if(!readStudioOverlayAsset(ws.project.dir,path,definition))throw std::runtime_error("Tileset file unavailable");
        const auto ts=game::loadTileset(Json::parse(definition));
        const auto read=[&](const std::string& name,std::string& bytes){return readStudioOverlayAsset(ws.project.dir,name,bytes);};
        const auto decode=[](const std::string& bytes,atlas::Image& image){
            return render::decodeImage(bytes,image.width,image.height,image.rgba);
        };
        atlas::ImageSource packed(read,decode);
        atlas::Image source;
        if(!packed.load(ts.atlas,source)){
            std::string bytes;
            if(!read(ts.atlas,bytes)||!decode(bytes,source))throw std::runtime_error("Cannot load tile atlas: "+ts.atlas);
        }
        if(ts.columns>source.width/ts.tileW || ts.tileH>source.height)throw std::runtime_error("Tile layout exceeds atlas dimensions");
        int width=0,height=0;
        auto pixels=fitImage(source.rgba,source.width,source.height,768,width,height);
        Json out=Json::object();
        out.set("width",Json::number(int64_t(source.width)));out.set("height",Json::number(int64_t(source.height)));
        out.set("tileWidth",Json::number(int64_t(ts.tileW)));out.set("tileHeight",Json::number(int64_t(ts.tileH)));
        out.set("columns",Json::number(int64_t(ts.columns)));
        out.set("url",Json::string("data:image/png;base64,"+editor::base64Encode(encodePng(pixels,width,height))));
        return out;
    });
    if (method == "studio.viewportInput") return answer([&] { return viewportInput(params); });
    if (method == "studio.gameSelectEntity" || method == "studio.gameEntities" || method == "studio.gameInspect" || method == "studio.gameRegions" ||
        method == "studio.gameProfile" || method == "studio.gameEdit" ||
        method == "studio.previewPlayChanges" || method == "studio.keepPlayChanges") {
        return answer([&] {
            auto result = localPlayOperation(method, params, playReview_);
            if (method == "studio.keepPlayChanges") sendEvent("document.changed", "");
            return result;
        });
    }
    if (method == "studio.scriptWatch") return answer([&] {
        if (params.has("restart")) restartOnScriptChange_=params.getBool("restart");
        Json result=Json::object();result.set("restart",Json::fromBool(restartOnScriptChange_));return result;
    });
    if (method == "studio.gameScriptLog") return answer([&] { return game_ ? game_->scriptLog(false) : Json::array(); });
    if (method == "studio.gameDevices") return answer([&] { return studioGameDevices(); });
    if (method == "studio.gameStart" || method == "studio.gameStop" || method == "studio.gameState" ||
        method == "studio.gameControl" || method == "studio.gameDevice") return answer([&] {
        auto ensureGame = [&] { if (!game_) game_ = std::make_unique<StudioGameSurface>(platform_.createDevice(StudioSurface::Game)); };
        if (method == "studio.gameDevice") {
            const auto profile = StudioGameDevice::parse(params);
            ensureGame();
            game_->setDevice(profile);
            platform_.gameDeviceChanged(gameState());
        } else if (method == "studio.gameStart") {
            const auto entry = params.getStr("entry");
            if (entry != "map" && entry != "menu") throw std::runtime_error("Invalid game entry");
            std::optional<uint8_t> seat;
            if (params.has("localSeat")) {
                if (!sameDocumentScope(params["scope"],session_->graphDocumentScope())) throw std::runtime_error("Map changed. Choose a player again.");
                const double value=params["localSeat"].toDouble(-1);
                if (!params["localSeat"].isNumber() || !(value>=0 && value<=255) || value!=std::floor(value))
                    throw std::runtime_error("Invalid local seat");
                seat=static_cast<uint8_t>(value);
            }
            auto plan = studioGameLaunch(session_->workspace(), entry == "map",seat);
            if (entry == "map" && params.getBool("fromScene") && preview_) {
                const auto cam = onScene([&] { return preview_->cameraState(); });
                plan.camera = std::array<float, 5>{cam.target.x, cam.target.z, cam.distance, float(cam.pitch * 57.29578), float(cam.yaw * 57.29578)};
            }
            ensureGame();
            wpLiveSnapshot_=Json();wpLivePlanetProject_.clear();wpLivePlanetMapPath_.clear();wpLivePlanetRevision_=0;wpLiveRefresh_=0;
            buildingGraphOverlayScene_=0;rebuildOverlay();
            session_->setBuildingGraphReader({});
            clearPlayReviews();
            game_->start(plan, engineRoot_ + "/engine/assets");
            if(plan.map){
                wpLivePlanetProject_=plan.project;
                wpLivePlanetMapPath_=session_->workspace().map.path;
                wpLivePlanetRevision_=session_->workspace().map.revision;
            }
            rebuildOverlay();
            gameFrameTime_ = 0;
        } else if (method == "studio.gameStop" && game_) {
            buildingGraphOverlayScene_=0;session_->setBuildingGraphReader({});
            clearPlayReviews();game_->stop();wpLiveSnapshot_=Json();wpLivePlanetMapPath_.clear();rebuildOverlay();
        } else if (method == "studio.gameControl") {
            if (!game_) throw std::runtime_error("No game is running");
            clearPlayReviews();
            const auto action=params.getStr("action");
            if(action=="restart"){
                restartGame();
            }else if(action=="setSpeed"){
                game_->setSimulationSpeed(params["speed"].isNumber() ? params["speed"].toDouble() : 0);
            }else game_->control(action);
            if(action=="checkpointRestore"){
                wpLiveSnapshot_=Json();wpLiveRefresh_=0;rebuildOverlay();
            }
        }
        auto state = gameState(method == "studio.gameState" && params.getBool("scriptErrors"));
        if (method == "studio.gameState" && params.has("geometryProject")) {
            const auto& project = session_->workspace().project.dir;
            if (params.getStr("geometryProject") != project) throw std::runtime_error("Geometry project changed");
            if (game_)state.set("geometry",game_->staticGeometryReport(session_->workspace()));
            if (preview_ && placement_.visible && state["geometry"].isNull()) {
                const auto file=editor::configuredGeometryScene(session_->workspace());
                auto authored=onScene([&]{return preview_->staticGeometryReport(project,file);});
                if(!authored.isNull()) {
                    state.set("geometrySource",Json::string("Scene"));
                    state.set("geometry",std::move(authored));
                }
            }
        }
        return state;
    });
    if(method=="studio.portFolder"){
        const auto id=req["id"];
        auto alive=alive_;
        platform_.chooseFolder("Choose project folder","",[this,alive,id](std::string folder){
            if(!*alive)return;
            Json reply=Json::object();reply.set("id",id);reply.set("result",Json::string(folder));sendReply(reply);
        });
        return true;
    }
    if(method=="studio.projectPort"){
        const auto id=req["id"];
        const auto operation=params.getStr("action");
        if(projectLoading_ || portJobActive_ || (operation!="scan" && operation!="import")){
            sendError(id,"Project import unavailable or already running");return true;
        }
        portJobActive_=true;
        auto result=std::make_shared<Json>();
        auto error=std::make_shared<std::string>();
        auto alive=alive_;
        platform_.runInBackground([operation,params,engine=session_->workspace().engineDir,result,error]{
            try{
                Json input=Json::object();
                input.set("path",params["path"]);
                if(operation=="import")for(const char* key:{"out","mode","scale"})input.set(key,params[key]);
                const Json report=editor::runProjectPort(engine,operation,input);
                *result=Json::object();
                for(const char* key:{"ok","engine","version","summary","counts","error","errors","destination"})
                    if(report.has(key))result->set(key,report[key]);
                if(operation=="scan"){
                    Json counts=report["counts"].isObject()?report["counts"]:Json::object();
                    for(const char* key:{"assets","scenes","scripts"})if(report[key].isArray())counts.set(key,Json::number(int64_t(report[key].size())));
                    result->set("counts",counts);
                }
            }catch(const std::exception& e){*error=e.what();}
        },[this,alive,id,result,error,operation]{
            if(!*alive)return;
            portJobActive_=false;
            if(!error->empty()){sendError(id,*error);return;}
            if(operation=="import" && result->getBool("ok",true) && !result->has("error"))
                importedProject_=result->getStr("destination");
            Json reply=Json::object();reply.set("id",id);reply.set("result",std::move(*result));sendReply(reply);
        });
        return true;
    }
    if(method=="studio.openImported"){
        if(projectLoading_ || importedProject_.empty() || params.getStr("dir")!=importedProject_){
            sendError(req["id"],"Import the destination before opening it");return true;
        }
        const auto id=req["id"].integer();
        if(!confirmClose())sendProjectCancelled(id);else openProject(importedProject_,id);
        return true;
    }
    if (method == "studio.hubTemplates" || method == "studio.wizardPlan") {
        // Catalog requests and scanning the chosen artwork folder can both take time.
        const int64_t requestId = req["id"].integer();
        auto result = std::make_shared<Json>();
        auto error = std::make_shared<std::string>();
        auto alive = alive_;
        platform_.runInBackground(
            [engineDir = session_->workspace().engineDir, approvedArt = session_->wizardArtDir(), method, params, result, error] {
                try {
                    if (method == "studio.wizardPlan") *result = editor::planGame(engineDir, studioWizardSpec(engineDir, params, approvedArt));
                    else *result = hubTemplatesJson(engineDir, params);
                }
                catch (const std::exception& e) { *error = e.what(); }
            },
            [this, alive, requestId, result, error] {
                if (!*alive) return;
                Json reply = Json::object();
                reply.set("id", Json::number(requestId));
                if (!error->empty()) reply.set("error", Json::string(*error));
                else reply.set("result", *result);
                sendReply(reply);
            });
        return true;
    }
    if (method == "studio.wizardDictation") {
        Json reply = Json::object(); reply.set("id", req["id"]);
        reply.set("result", wizardDictation_.handle(params));
        if (params.getStr("action") == "start") platform_.dictationStarted();
        sendReply(reply);
        return true;
    }
    if (method == "studio.wizardFetchArt") {
        const auto id = req["id"];
        const std::string url = params.getStr("url");
        if (url.empty() || url.size() > 8192) { sendError(id, "Enter an artwork link"); return true; }
        auto dir = std::make_shared<std::string>();
        auto error = std::make_shared<std::string>();
        auto alive = alive_;
        const auto sourceRequest = ++wizardArtRequest_;
        platform_.runInBackground([engine = session_->workspace().engineDir, url, dir, error] {
            try { *dir = editor::fetchWizardArt(engine, url, editor::wizardStagingDir(), error.get()); }
            catch (const std::exception& e) { *error = e.what(); }
        }, [this, alive, sourceRequest, id, dir, error] {
            if (!*alive) return;
            if (sourceRequest != wizardArtRequest_) { sendError(id, "Another artwork source was chosen"); return; }
            if (dir->empty()) { sendError(id, error->empty() ? "Could not download artwork" : *error); return; }
            session_->setWizardArtDir(*dir);
            Json result = Json::object(); result.set("dir", Json::string(*dir));
            Json reply = Json::object(); reply.set("id", id); reply.set("result", result); sendReply(reply);
        });
        return true;
    }
    if (method == "studio.wizardArt") {
        const auto id = req["id"];
        auto alive = alive_;
        const auto sourceRequest = ++wizardArtRequest_;
        platform_.chooseFolder("Choose a folder with artwork, models or audio", session_->wizardArtDir(),
            [this, alive, id, sourceRequest](std::string folder) {
                if (!*alive) return;
                if (sourceRequest != wizardArtRequest_) { sendError(id, "Another artwork source was chosen"); return; }
                if (!folder.empty()) session_->setWizardArtDir(folder);
                Json result = Json::object(); result.set("dir", Json::string(folder));
                Json reply = Json::object(); reply.set("id", id); reply.set("result", result); sendReply(reply);
            });
        return true;
    }
    if (method == "studio.wizardCreate") {
        const auto id = req["id"];
        if (wizardCreating_) { sendError(id, "A game is already being created"); return true; }
        const auto engine = session_->workspace().engineDir;
        auto spec = studioWizardSpec(engine, params, session_->wizardArtDir());
        if (spec.title.empty() || spec.title.size() > 120) { sendError(id, "Give the game a title of up to 120 bytes"); return true; }
        wizardCreating_ = true;
        auto alive = alive_;
        const auto epoch = packageProjectEpoch_;
        platform_.chooseFolder("Choose the parent folder for " + spec.title, editor::defaultProjectsDir(),
            [this, alive, id, engine, epoch, spec](std::string parent) mutable {
                if (!*alive) return;
                if (parent.empty()) { wizardCreating_ = false; sendProjectCancelled(id.integer()); return; }
                spec.location = parent;
                auto dir = std::make_shared<std::string>();
                auto error = std::make_shared<std::string>();
                platform_.runInBackground([engine, spec, dir, error] {
                    try {
                        editor::Workspace workspace;
                        workspace.engineDir = engine;
                        editor::GameMaker maker(workspace, spec);
                        const auto report = maker.runAll();
                        if (maker.failed()) {
                            *error = report.getStr("error", "Game generation failed");
                            if (!maker.projectDir().empty()) *error += " (project folder: " + maker.projectDir() + ")";
                        }
                        else *dir = maker.projectDir();
                    } catch (const std::exception& e) { *error = e.what(); }
                }, [this, alive, id, dir, error, epoch] {
                    if (!*alive) return;
                    wizardCreating_ = false;
                    if (dir->empty()) { sendError(id, error->empty() ? "Game generation failed" : *error); return; }
                    if (epoch != packageProjectEpoch_ || projectLoading_ || !confirmClose()) {
                        sendError(id, "Game created at " + *dir + ". Open it from Project Hub."); return;
                    }
                    openProject(*dir, id.integer());
                });
            });
        return true;
    }
    if (method == "studio.hubOpen" || method == "studio.hubCreate") {
        // Project hub: open a RECENT project (only folders the registry lists, never a path from JavaScript
        // alone), or copy a template into a folder the user picks in a native dialog, then open it.
        const int64_t requestId = req["id"].integer();
        if (projectLoading_) { sendError(req["id"], "A project is loading"); return true; }
        if (!confirmClose()) { sendProjectCancelled(requestId); return true; }
        if (method == "studio.hubOpen") {
            const std::string dir = params.getStr("dir");
            bool listed = false;
            for (const auto& p : editor::recentProjects()) if (p.dir == dir && p.exists) listed = true;
            if (!listed) { sendError(req["id"], "This project is not in the recent list"); return true; }
            openProject(dir, requestId);
            return true;
        }
        const std::string name = params.getStr("name"), templ = params.getStr("template");
        if (name.empty() || name.size() > 120) { sendError(req["id"], "Give the project a name"); return true; }
        bool known = false;
        if (editor::isCatalogTemplate(templ)) {   // a catalog template package: its id is checked by aiverse_pkg.py
            known = templ.size() > 4 && templ.size() < 70 &&
                    templ.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-", 4) == std::string::npos;
        }
        for (const auto& t : editor::listTemplates(session_->workspace().engineDir)) if (t.id == templ) known = true;
        if (!known) { sendError(req["id"], "Unknown template"); return true; }
        const std::string location = editor::defaultProjectsDir();
        std::error_code ec;
        fs::create_directories(fs::path(location), ec);
        auto alive = alive_;
        platform_.chooseFolder("Choose where to create “" + name + "”", location, [this, alive, requestId, name, templ](std::string folder) {
            if (!*alive) return;
            if (folder.empty()) { sendProjectCancelled(requestId); return; }
            // Copying or downloading a template package can take a while: do it off the UI thread.
            auto dir = std::make_shared<std::string>();
            auto error = std::make_shared<std::string>();
            platform_.runInBackground(
                [engineDir = session_->workspace().engineDir, templ, name, folder, dir, error] {
                    try { *dir = editor::createProject(engineDir, templ, name, folder, "", error.get()); }
                    catch (const std::exception& e) { *error = e.what(); }
                },
                [this, alive, requestId, dir, error] {
                    if (!*alive) return;
                    if (dir->empty()) {
                        sendError(Json::number(requestId), error->empty() ? "Could not create the project" : *error);
                        return;
                    }
                    openProject(*dir, requestId);
                });
        });
        return true;
    }
    if (method == "studio.openProject") {
        // The path is chosen in a native dialog, never accepted from untrusted JavaScript.
        const int64_t requestId = req["id"].integer();
        const auto& ws = session_->workspace();
        if ((ws.map.dirty || ws.project.rulesDirty || ws.project.manifestDirty) &&
            !platform_.confirmDiscardChanges("Discard unsaved AIVERSE changes?",
                "Opening another project discards unsaved map and project changes.", "Discard and Open")) {
            sendProjectCancelled(requestId);
            return true;
        }
        auto alive = alive_;
        platform_.chooseProject([this, alive, requestId](std::string path) {
            if (!*alive) return;
            if (path.empty()) sendProjectCancelled(requestId);
            else openProject(path, requestId); // replies when the background load finishes
        });
        return true;
    }
    if (!session_->workspace().project.dir.empty()) {
        if (method == "studio.saveAs" || (method == "studio.save" && session_->workspace().map.path.empty())) {
            const int64_t requestId = req["id"].integer();
            auto alive = alive_;
            platform_.chooseSaveFile(session_->workspace().project.dir + "/maps", "scene.json", [this, alive, requestId](std::string file) {
                if (!*alive) return;
                Json r = Json::object();
                r.set("id", Json::number(requestId));
                r.set("method", Json::string("studio.snapshot"));
                Json reply = session_->dispatch(r);
                if (!file.empty()) {
                    try {
                        reply.set("result", session_->saveMapAs(file));
                        sendEvent("document.changed", "");
                    } catch (const std::exception& error) {
                        reply = Json::object();
                        reply.set("id", Json::number(requestId));
                        reply.set("error", Json::string(error.what()));
                    }
                }
                sendReply(reply);
            });
            return true;
        }
        if ((method == "studio.openMap" || method == "studio.newMap") && !confirmClose()) {
            auto cancelled = req;
            cancelled.set("method", Json::string("studio.snapshot"));
            sendReply(session_->dispatch(cancelled));
            return true;
        }
    }
    if(method=="studio.spectraPackageChoose"){
        if(projectLoading_||session_->workspace().project.dir.empty()){
            sendError(req["id"],"Open a project first");return true;
        }
        const auto project=session_->workspace().project.dir;
        const auto epoch=packageProjectEpoch_;
        const auto requestId=req["id"].integer();
        auto alive=alive_;
        platform_.chooseImportSources(project,[this,alive,project,epoch,requestId](std::vector<fs::path> sources){
            if(!*alive)return;
            Json reply=Json::object();reply.set("id",Json::number(requestId));
            try{
                if(projectLoading_||epoch!=packageProjectEpoch_||session_->workspace().project.dir!=project)
                    throw std::runtime_error("Project changed while choosing package");
                Json result=Json::object();
                if(sources.empty())result.set("cancelled",Json::fromBool(true));
                else{
                    if(sources.size()!=1)throw std::runtime_error("Select one .aivfx package");
                    result=stageSpectraPackage(project,sources.front());
                    sendEventPayload("spectrafx.packageStaged",result);
                }
                reply.set("result",std::move(result));
            }catch(const std::exception& e){reply.set("error",Json::string(e.what()));}
            sendReply(reply);
        });
        return true;
    }
    if (method == "studio.assetImportPreview") {
        // The browser can choose a verified relative DESTINATION only. Source paths come from a native dialog.
        const std::string destination = params.getStr("destination");
        if (destination.size() > 512) { sendError(req["id"], "Invalid import destination"); return true; }
        if (session_->workspace().project.dir.empty()) { sendError(req["id"], "Open a project before importing assets"); return true; }
        const int64_t requestId = req["id"].integer();
        auto alive = alive_;
        platform_.chooseImportSources(session_->workspace().project.dir, [this, alive, requestId, destination](std::vector<fs::path> sources) {
            if (!*alive) return;
            Json reply = Json::object();
            reply.set("id", Json::number(requestId));
            if (sources.empty()) {
                Json cancelled = Json::object();
                cancelled.set("cancelled", Json::fromBool(true));
                reply.set("result", std::move(cancelled));
            } else {
                try {
                    if (sources.size() > 64) throw std::runtime_error("Select at most 64 assets");
                    reply.set("result", session_->previewExternalAssets(sources, destination));
                } catch (const std::exception& error) { reply.set("error", Json::string(error.what())); }
            }
            sendReply(reply);
        });
        return true;
    }
    return false;
}
void StudioHost::afterSessionReply(const Json& req, const Json& reply) {
    if (reply.has("error")) return;
    const std::string method = req.getStr("method");
    if(method=="studio.openProject"||method=="studio.openMap"||method=="studio.newMap"||method=="studio.hubOpen"||method=="studio.hubCreate"){
        const bool armed=forestPlantArm_.isObject();forestPlantArm_=Json{};
        if(preview_)onScene([&]{preview_->setPlanetSceneBody("","","");});
        if(armed){Json out=Json::object();out.set("cancelled",Json::fromBool(true));sendEventPayload("planetForest.planted",out);}
    }
    if(method=="studio.sceneBrush" && req["params"].size())sendEventPayload("brush.changed",reply["result"]);
    if (method == "studio.select" && platform_.hasFloatingWindows()) sendEvent("selection.changed", ""); // floats follow it
    if (preview_ && (method == "studio.openMap" || method == "studio.newMap")) {
        onScene([&] { preview_->resetCamera(); });
        mapSnapshot_.reset(); rulesSnapshot_.reset();
    }
    if(method=="studio.materialCreate"||method=="studio.materialsToFiles"||method=="studio.materialPatch"||
       (method=="studio.op"&&req["params"].getStr("op")=="shader_graph_apply"))sendEvent("assets.changed","");
    if (method == "studio.materialPreview") sendEvent("material.preview", req["params"].getStr("name"));
    if(method=="studio.assetMetaPatch"||method=="studio.assetHistoryStep"||method=="studio.assetTransferCommit"||method=="studio.assetDelete"||method=="studio.assetFolderCreate"||method=="studio.assetTrashApply"||method=="studio.assetHistoryResetApply"||method=="studio.agentBatchApply"||(method=="studio.agentHistoryStep"&&req["params"].getStr("scope")=="assets"))sendEvent("assets.changed", "");
    if (documentChangingMethods().count(method) || (method=="studio.projectRule" && req["params"].has("value")) || (method == "studio.op" && reply["result"].getBool("changed")))
        sendEvent("document.changed", "");
}
void StudioHost::handleMessage(const std::string& json) {
    if (json.size() > 1024 * 1024) return;
    Json req;
    try { req = Json::parse(json); }
    catch (const std::exception& e) { platform_.log(std::string("AIVERSE bridge: bad message: ") + e.what()); return; }
    try {
        if (handleHostMethod(req)) return;
        const std::string method = req.getStr("method");
        if (game_ && (method == "studio.openMap" || method == "studio.newMap")){
            buildingGraphOverlayScene_=0;session_->setBuildingGraphReader({});game_->stop();wpLiveSnapshot_=Json();wpLivePlanetMapPath_.clear();rebuildOverlay();
        }
        if (projectLoading_ && method != "studio.viewportRect") { sendError(req["id"], "Project is loading"); return; }
        Json reply;
        if (method == "studio.assetThumbnail" && req["id"].isNumber()) {
            reply = Json::object();
            reply.set("id", req["id"]);
            try { reply.set("result", assetThumbnail(req["params"].getStr("path"))); }
            catch (const std::exception& e) { reply.set("error", Json::string(e.what())); }
        } else if (method == "studio.cavernsPreview" && req["id"].isNumber()) {
            const auto& ws=session_->workspace();const auto& params=req["params"];
            if(ws.project.dir.empty()||params.getStr("expectedProject")!=ws.project.dir||
               !params["expectedMap"].isString()||params.getStr("expectedMap")!=(ws.map.path.empty()?"":ws.relative(ws.map.path)))
                throw std::runtime_error("Caverns preview project/map changed");
            if(!preview_)throw std::runtime_error("Open the native Scene panel first");
            reply=Json::object();reply.set("id",req["id"]);
            if(params.has("globeBody")){
                if(!params["globeBody"].isString())throw std::runtime_error("Caverns globe body must be text");
                const auto body=params.getStr("globeBody");
                if(!body.empty()){
                    if(session_->sceneBrush().armed)throw std::runtime_error("Disarm the Scene brush before entering globe mode");
                    editor::planet_caves::validateAnchor(ws.map.data,{body,0,{}, {},100});
                }
                onScene([&]{preview_->setCaveGlobe(body,ws.project.dir,ws.map.path);});
                mouse_={};
            }
            reply.set("result",onScene([&]{return preview_->cavernsPreview(params);}));
        } else if (method == "studio.gpuStats" && req["id"].isNumber()) {
            // Frame Capture / Texture Resources: Scene frame history (CPU frame time, draw calls) and live GPU textures.
            reply = Json::object();
            reply.set("id", req["id"]);
            Json result = Json::object(), history = Json::array();
            for (const auto& f : frameHistory_) {
                Json o = Json::array();
                o.push(Json::number(double(f.first), 2));
                o.push(Json::number(int64_t(f.second)));
                history.push(std::move(o));
            }
            if (req["params"].getBool("prisma")) {
                const auto& ws=session_->workspace();
                if(req["params"].getStr("project")!=ws.project.dir)throw std::runtime_error("PostFX project changed");
                Json sources=Json::object();
                if (game_ && game_->launchPlan().project==ws.project.dir)
                    sources.set("Game", game_->prismaFrameReports());
                onScene([&]{
                    if (preview_ && placement_.visible) sources.set("Scene", preview_->prismaFrameReports(ws.project.dir,ws.map.path));
                    if (lensSurface_.renderer && lensSurface_.placement.visible && lensProject_==ws.project.dir)
                        sources.set("LENS "+std::to_string(lensCameraId_),lensSurface_.renderer->prismaFrameReports(ws.project.dir,ws.map.path));
                });
                result.set("prisma", std::move(sources));
            }
            result.set("history", history);
            result.set("frames", Json::number(int64_t(sceneFrameCount_)));
            if (preview_) {
                result.set("drawCalls", Json::number(int64_t(onScene([&] { return preview_->drawCalls(); }))));
                if (req["params"].getBool("textures")) result.set("gpu", onScene([&] { return preview_->textureResources(); }));
            } else result.set("error", Json::string("The Scene panel is not visible"));
            reply.set("result", std::move(result));
        } else if (method == "studio.importedClipPreview" || method == "studio.sourceModelPreview" || method == "studio.gpuEmitterPreview" || method == "studio.effectFilePreview" || method == "studio.attachmentsPreview" || method == "studio.lensPreview") {
            const bool importedClip = method == "studio.importedClipPreview";
            const bool sourceModel = method == "studio.sourceModelPreview";
            const bool lens = method == "studio.lensPreview";
            const bool gpuEmitter = method == "studio.gpuEmitterPreview";
            const bool effectFile = method == "studio.effectFilePreview";
            const auto surface = importedClip ? StudioSurface::ImportedClip : sourceModel ? StudioSurface::SourceModel : gpuEmitter ? StudioSurface::GpuEmitter : effectFile ? StudioSurface::EffectFile : lens ? StudioSurface::LensCamera : StudioSurface::Attachments;
            auto& asset = assetSurface(surface);
            auto& ownerProject = importedClip ? importedClipProject_ : sourceModel ? sourceModelProject_ : gpuEmitter ? gpuEmitterProject_ : effectFile ? effectFileProject_ : lens ? lensProject_ : attachmentProject_;
            const auto& params = req["params"];
            const auto& project = session_->workspace().project.dir;
            if (project.empty() || params.getStr("project") != project)
                throw std::runtime_error("Project changed");
            Json result = Json::object();
            if (lens && params.getBool("capture")) {
                captureLensFrame(params.getInt("id"));
            } else if (lens && params.getBool("useInScene")) {
                ensureScene();
                result=onScene([&] { return preview_->configureLensCamera(params); });
            } else if (lens && params.getBool("copyScene")) {
                if (!preview_) throw std::runtime_error("Open Scene to copy its camera");
                result = onScene([&] { return preview_->lensViewPose(); });
            } else if (params.getBool("close")) {
                onScene([&] { asset.renderer.reset(); });
                if(lens){lensShot_.reset();lensCameraId_=0;}
                ownerProject.clear();
            } else {
                const bool publishing = importedClip ? params["model"].isString() : sourceModel ? (params["model"].isString()||params["type"].isString()) : (gpuEmitter || effectFile) ? params["source"].isString() : params["document"].isObject();
                if (!renderThread_) {
                    if (!publishing) throw std::runtime_error("Open the attachment preview first");
                    const char* threaded = std::getenv("AIVERSE_STUDIO_RENDER_THREAD");
                    renderThread_ = std::make_unique<StudioRenderThread>([this] { renderSceneFrame(); },
                        !(threaded && std::string(threaded) == "0"));
                }
                onScene([&] {
                    platform_.sceneThreadScope([&] {
                        if (!asset.renderer || ownerProject != project) {
                            if (!publishing) throw std::runtime_error("Open the attachment preview first");
                            asset.renderer = std::make_unique<StudioScene>(platform_.createDevice(surface));
                            if(lens)asset.renderer->setContinuousRedraw(false);
                            ownerProject = project;
                        }
                        if (lens) {
                            if(lensShot_)throw std::runtime_error("Wait for the camera capture");
                            result=asset.renderer->configureLensCamera(params);
                            lensCameraId_=params.getInt("id");
                        } else if(gpuEmitter) result=asset.renderer->gpuEmitterCommand(project,params);
                        else if(effectFile) result=asset.renderer->effectFileCommand(project,params);
                        else if (importedClip) {
                            Json settings=params;
                            if(publishing){
                                Json layout=Json::object();layout.set("parent",params["model"]);layout.set("attachments",Json::array());
                                settings.set("document",std::move(layout));
                            }
                            result=asset.renderer->attachmentCommand(project,settings);
                        } else if (sourceModel) {
                            result=asset.renderer->sourceModelCommand(project,session_->workspace().project.rulesJson,params);
                        } else result=asset.renderer->attachmentCommand(project,params);
                    });
                });
            }
            reply = Json::object();
            reply.set("id", req["id"]);
            reply.set("result", std::move(result));
        } else if (method == "studio.viewportRect" && req["id"].isNumber()) {
            applyViewport(req["params"]);
            reply = Json::object();
            reply.set("id", req["id"]);
            Json result = Json::object();
            result.set("ok", Json::fromBool(true));
            reply.set("result", std::move(result));
        } else {
            if (preview_ && (method == "studio.wind" || method == "studio.windSet")) session_->setWindSceneStats(onScene([&] { return preview_->windStats(); }));
            if (method=="studio.lightAdd" && req["params"].getBool("atSceneTarget")) {
                const auto& map=session_->workspace().map;
                if(map.data.planet())throw std::runtime_error("Scene target placement requires a planar map");
                if(!preview_)throw std::runtime_error("Open Scene before placing a light at its target");
                const auto target=onScene([&] {
                    if(!preview_->hasSceneCamera())throw std::runtime_error("Open Scene before placing a light at its target");
                    return preview_->cameraState().target;
                });
                auto& params=req.member("params");
                params.set("x",Json::number(std::round(double(target.x)*2)*.5,3));
                params.set("z",Json::number(std::round(double(target.z)*2)*.5,3));
            }
            reply = session_->dispatch(req);
        }
        sendReply(reply);
        afterSessionReply(req, reply);
    } catch (const std::exception& e) {
        // Every request with an id gets an answer: the UI waits for it (there is no RPC timeout).
        platform_.log(std::string("AIVERSE bridge exception: ") + e.what());
        if (req["id"].isNumber() && req["id"].integer() != 0) sendError(req["id"], e.what());
    }
}

} // namespace aiverse::studio
